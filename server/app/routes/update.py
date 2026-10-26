"""Update checking endpoint - proxies GitHub releases for 3DS/NDS clients."""

import re
from urllib.parse import unquote, urljoin, urlsplit

from fastapi import APIRouter, HTTPException
from fastapi.responses import Response
from pydantic import BaseModel
import httpx

from app.config import settings

router = APIRouter()

# The server proxies GitHub releases because 3DS/NDS can't do HTTPS.
# The repository is configurable (SYNC_UPDATE_REPO) so forks can point their
# clients at their own releases.
GITHUB_API_URL = "https://api.github.com/repos/{repo}/releases/latest"
_REPO_RE = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")

# /update/download only ever fetches release assets of the configured repo,
# and only follows redirects that stay on GitHub's own asset hosts.  Anything
# else would turn the server into an open proxy into the operator's network.
_DOWNLOAD_HOST = "github.com"
_REDIRECT_HOSTS = {
    "github.com",
    "objects.githubusercontent.com",
    "release-assets.githubusercontent.com",
}
_MAX_REDIRECTS = 5
_MAX_DOWNLOAD_BYTES = 64 * 1024 * 1024
_DOWNLOAD_TIMEOUT = httpx.Timeout(60.0, connect=10.0)


def _update_repo() -> str:
    repo = settings.update_repo.strip().strip("/")
    if not _REPO_RE.match(repo):
        raise HTTPException(status_code=500, detail="SYNC_UPDATE_REPO must be 'owner/repo'")
    return repo


def _http_client(**kwargs) -> httpx.AsyncClient:
    """Factory so tests can inject a mock transport."""
    return httpx.AsyncClient(**kwargs)


def _is_https_url_on(url: str, hosts: set[str]) -> bool:
    parts = urlsplit(url)
    return (
        parts.scheme == "https"
        and parts.hostname in hosts
        and parts.port in (None, 443)
        and parts.username is None
        and parts.password is None
    )


def _validate_release_asset_url(url: str, repo: str) -> str:
    """Return the asset filename if ``url`` is a release asset of ``repo``."""
    if not _is_https_url_on(url, {_DOWNLOAD_HOST}):
        raise HTTPException(status_code=400, detail="URL is not a GitHub release asset")
    parts = urlsplit(url)
    if parts.query or parts.fragment:
        raise HTTPException(status_code=400, detail="URL is not a GitHub release asset")
    # /<owner>/<repo>/releases/download/<tag>/<asset>
    segments = [unquote(seg) for seg in parts.path.split("/")]
    owner, name = repo.split("/")
    if (
        len(segments) != 7
        or segments[0] != ""
        or segments[1].lower() != owner.lower()
        or segments[2].lower() != name.lower()
        or segments[3:5] != ["releases", "download"]
        or any(seg in ("", ".", "..") or "/" in seg or "\\" in seg for seg in segments[5:])
    ):
        raise HTTPException(
            status_code=400, detail=f"URL is not a release asset of {repo}"
        )
    return segments[6]


# Map platform to expected asset file extension
PLATFORM_EXTENSIONS = {
    "3ds": ".cia",
    "nds": ".nds",
}


class UpdateInfo(BaseModel):
    available: bool
    current_version: str
    latest_version: str | None = None
    download_url: str | None = None
    changelog: str | None = None
    file_size: int | None = None


@router.get("/update/check")
async def check_update(current: str = "0.0.0", platform: str = "3ds") -> UpdateInfo:
    """Check if a newer version is available.

    Args:
        current: The client's current version (e.g., "0.4.3")
        platform: The client platform ("3ds" or "nds")

    Returns:
        Update info with download URL if available.
    """
    extension = PLATFORM_EXTENSIONS.get(platform, ".cia")

    if not settings.update_proxy_enabled:
        return UpdateInfo(available=False, current_version=current)

    try:
        async with _http_client() as client:
            resp = await client.get(
                GITHUB_API_URL.format(repo=_update_repo()),
                headers={"Accept": "application/vnd.github.v3+json"},
                timeout=10.0,
            )

            if resp.status_code != 200:
                return UpdateInfo(available=False, current_version=current)

            data = resp.json()
            latest_version = data.get("tag_name", "").lstrip("v")

            # Find asset matching the platform extension
            download_url = None
            file_size = None
            for asset in data.get("assets", []):
                if asset["name"].endswith(extension):
                    download_url = asset["browser_download_url"]
                    file_size = asset["size"]
                    break

            # Compare versions
            is_newer = _compare_versions(latest_version, current) > 0

            return UpdateInfo(
                available=is_newer and download_url is not None,
                current_version=current,
                latest_version=latest_version,
                download_url=download_url,
                changelog=data.get("body", ""),
                file_size=file_size,
            )

    except Exception:
        # On any error, report no update available
        return UpdateInfo(available=False, current_version=current)


@router.get("/update/download")
async def proxy_download(url: str):
    """Proxy download from GitHub (3DS/NDS can't do HTTPS with GitHub).

    Downloads the full file then returns it with Content-Length,
    which is required by the DS client's HTTP/1.0 implementation.
    Homebrew binaries are small (a few MB) so this is fine.

    Only release assets of SYNC_UPDATE_REPO are fetched, redirects may only
    lead to GitHub asset hosts, and the download is size- and time-limited.
    """
    if not settings.update_proxy_enabled:
        raise HTTPException(status_code=404, detail="Update proxy is disabled")

    filename = _validate_release_asset_url(url, _update_repo())

    try:
        async with _http_client(timeout=_DOWNLOAD_TIMEOUT) as client:
            current = url
            for _ in range(_MAX_REDIRECTS + 1):
                async with client.stream("GET", current, follow_redirects=False) as resp:
                    if resp.is_redirect:
                        location = resp.headers.get("location", "")
                        current = urljoin(current, location)
                        if not _is_https_url_on(current, _REDIRECT_HOSTS):
                            raise HTTPException(
                                status_code=502,
                                detail="Update download redirected off GitHub",
                            )
                        continue
                    if resp.status_code != 200:
                        raise HTTPException(
                            status_code=502,
                            detail=f"GitHub returned HTTP {resp.status_code}",
                        )
                    declared = resp.headers.get("content-length")
                    if declared and declared.isdigit() and int(declared) > _MAX_DOWNLOAD_BYTES:
                        raise HTTPException(status_code=502, detail="Update file too large")
                    chunks: list[bytes] = []
                    total = 0
                    async for chunk in resp.aiter_bytes():
                        total += len(chunk)
                        if total > _MAX_DOWNLOAD_BYTES:
                            raise HTTPException(
                                status_code=502, detail="Update file too large"
                            )
                        chunks.append(chunk)
                    content = b"".join(chunks)
                    break
            else:
                raise HTTPException(status_code=502, detail="Too many redirects")
    except httpx.HTTPError as exc:
        raise HTTPException(status_code=502, detail=f"Update download failed: {exc}")

    safe_name = re.sub(r"[^A-Za-z0-9._-]", "_", filename) or "update"
    return Response(
        content=content,
        media_type="application/octet-stream",
        headers={
            "Content-Disposition": f"attachment; filename={safe_name}",
            "Content-Length": str(len(content)),
        },
    )


def _compare_versions(v1: str, v2: str) -> int:
    """Compare two version strings. Returns >0 if v1 > v2, <0 if v1 < v2, 0 if equal."""
    def parse(v: str) -> list[int]:
        try:
            return [int(x) for x in v.split(".")]
        except ValueError:
            return [0]

    p1, p2 = parse(v1), parse(v2)
    # Pad to same length
    while len(p1) < len(p2):
        p1.append(0)
    while len(p2) < len(p1):
        p2.append(0)

    for a, b in zip(p1, p2):
        if a > b:
            return 1
        if a < b:
            return -1
    return 0
