"""Support files for emulators on consoles that can't fetch them themselves.

PS5SX2 (https://github.com/Swordpdf/PS5SX2, GPL-3.0) reads PCSX2's game
database from ``/data/PCSX2/resources/GameIndex.yaml``: the per-game fixes
(Star Ocean 3 does not boot without its ``VuAddSubHack``), names and memory
card filters. Its own installer copies the file, but an install of just the
app (a store such as ProsperoStore) leaves it out, and the PS5 client speaks
plain HTTP to IP addresses only, so it can't get it from GitHub. The server
fetches it from the repository at the latest release's tag and serves it.

``SYNC_EMULATOR_INDEX_ENABLED=false`` turns the endpoint off: it answers 404
and never reaches GitHub (not even to refresh an existing cached copy).
"""

from __future__ import annotations

import asyncio
import hashlib
import json
import logging
import time
from pathlib import Path

import httpx
from fastapi import APIRouter, HTTPException
from fastapi.responses import Response

from app.config import settings

router = APIRouter()
logger = logging.getLogger(__name__)

PS5SX2_REPO = "Swordpdf/PS5SX2"
PS5SX2_RELEASES_URL = f"https://api.github.com/repos/{PS5SX2_REPO}/releases/latest"
# The release zip's PCSX2/resources/GameIndex.yaml is this file at the tag.
PS5SX2_GAME_INDEX_URL = (
    "https://raw.githubusercontent.com/" + PS5SX2_REPO + "/{tag}/bin/resources/GameIndex.yaml"
)
# How long a fetched copy is served before GitHub is asked for a newer release.
CHECK_INTERVAL = 6 * 3600

_lock = asyncio.Lock()


def _cache_dir() -> Path:
    return settings.save_dir / ".emulator_cache" / "ps5sx2"


def _read_cached() -> tuple[bytes, dict] | None:
    folder = _cache_dir()
    try:
        meta = json.loads((folder / "GameIndex.json").read_text("utf-8"))
        data = (folder / "GameIndex.yaml").read_bytes()
    except (OSError, ValueError):
        return None
    if hashlib.sha256(data).hexdigest() != meta.get("sha256"):
        return None
    return data, meta


async def _fetch(client: httpx.AsyncClient) -> tuple[bytes, dict]:
    resp = await client.get(
        PS5SX2_RELEASES_URL,
        headers={"Accept": "application/vnd.github+json"},
        timeout=15.0,
    )
    resp.raise_for_status()
    tag = resp.json()["tag_name"]
    resp = await client.get(
        PS5SX2_GAME_INDEX_URL.format(tag=tag), timeout=60.0, follow_redirects=True
    )
    resp.raise_for_status()
    data = resp.content
    if len(data) < 100_000 or b"\n  name: " not in data:
        raise ValueError(f"unexpected GameIndex.yaml at {tag} ({len(data)} bytes)")
    return data, {"tag": tag, "sha256": hashlib.sha256(data).hexdigest()}


async def get_game_index() -> tuple[bytes, dict]:
    """The latest PS5SX2 release's GameIndex.yaml, cached under save_dir."""
    async with _lock:
        cached = _read_cached()
        if cached and time.time() - cached[1].get("checked", 0) < CHECK_INTERVAL:
            return cached
        try:
            async with httpx.AsyncClient() as client:
                if cached:
                    # same release: only the check time changes
                    resp = await client.get(
                        PS5SX2_RELEASES_URL,
                        headers={"Accept": "application/vnd.github+json"},
                        timeout=15.0,
                    )
                    resp.raise_for_status()
                    if resp.json().get("tag_name") == cached[1].get("tag"):
                        data, meta = cached
                    else:
                        data, meta = await _fetch(client)
                else:
                    data, meta = await _fetch(client)
        except (httpx.HTTPError, KeyError, ValueError) as exc:
            if cached:
                logger.warning("[emulators] PS5SX2 GameIndex.yaml check failed, serving %s: %s",
                               cached[1].get("tag"), exc)
                return cached
            raise
        meta["checked"] = int(time.time())
        folder = _cache_dir()
        folder.mkdir(parents=True, exist_ok=True)
        if not cached or cached[0] is not data:
            (folder / "GameIndex.yaml").write_bytes(data)
            logger.info("[emulators] PS5SX2 GameIndex.yaml %s cached (%d bytes)", meta["tag"], len(data))
        (folder / "GameIndex.json").write_text(json.dumps(meta), "utf-8")
        return data, meta


@router.get("/emulators/ps5sx2/game-index")
async def ps5sx2_game_index():
    """PCSX2's game database as PS5SX2 ships it, for /data/PCSX2/resources/."""
    if not settings.emulator_index_enabled:
        raise HTTPException(status_code=404, detail="Emulator game index is disabled")
    try:
        data, meta = await get_game_index()
    except Exception as exc:  # GitHub unreachable and nothing cached
        raise HTTPException(status_code=502, detail=f"Can't get GameIndex.yaml from GitHub: {exc}")
    return Response(
        content=data,
        media_type="application/x-yaml",
        headers={
            "X-File-Hash": meta["sha256"],
            "X-File-Size": str(len(data)),
            "X-Source-Tag": meta["tag"],
        },
    )
