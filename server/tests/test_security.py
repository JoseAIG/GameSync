"""Regression tests for server hardening fixes (untrusted client input)."""

import hashlib
import struct

import pytest

from app.config import settings
from app.models.save import BundleFile, SaveBundle
from app.services import storage
from app.services.bundle import BundleError, create_bundle, parse_bundle
from app.services.safe_path import (
    UnsafePathError,
    safe_join,
    validate_path_component,
    validate_relative_path,
)

TITLE_ID = 0x0004000000055D00
TITLE_HEX = "0004000000055D00"


def _bundle(files, title_id=TITLE_ID, timestamp=1700000000, compress=True) -> bytes:
    bundle_files = [
        BundleFile(
            path=path,
            size=len(data),
            sha256=hashlib.sha256(data).digest(),
            data=data,
        )
        for path, data in files
    ]
    return create_bundle(
        SaveBundle(title_id=title_id, timestamp=timestamp, files=bundle_files),
        compress=compress,
    )


def _upload(client, auth_headers, body, title=TITLE_HEX, **params):
    return client.post(
        f"/api/v1/saves/{title}",
        content=body,
        params={"force": "true", **params},
        headers={**auth_headers, "Content-Type": "application/octet-stream"},
    )


# ── Path traversal ──────────────────────────────────────────────────────────

BAD_PATHS = [
    "",
    "/etc/passwd",
    "../escape",
    "a/../../escape",
    "a/..",
    "..",
    ".",
    "./main",
    "a//b",
    "a/",
    "dir\\..\\escape",
    "C:/windows/evil",
    "C:evil",
    "a\x00b",
]


class TestSafePath:
    @pytest.mark.parametrize("path", BAD_PATHS)
    def test_rejects_unsafe_paths(self, path):
        with pytest.raises(UnsafePathError):
            validate_relative_path(path)

    @pytest.mark.parametrize(
        "path", ["main", "SAVEDATA/slot0.bin", "user/80000001/save.dat", "a.b-c_d"]
    )
    def test_accepts_normal_paths(self, path):
        assert validate_relative_path(path) == path

    def test_component_rejects_separator(self):
        with pytest.raises(UnsafePathError):
            validate_path_component("GC_A/..")
        assert validate_path_component("GC_GM8E") == "GC_GM8E"

    def test_safe_join_blocks_symlink_escape(self, tmp_path):
        base = tmp_path / "base"
        base.mkdir()
        outside = tmp_path / "outside"
        outside.mkdir()
        (base / "link").symlink_to(outside, target_is_directory=True)
        with pytest.raises(UnsafePathError):
            safe_join(base, "link/file")
        assert safe_join(base, "sub/file") == base / "sub/file"


class TestBundlePathTraversal:
    @pytest.mark.parametrize("compress", [True, False])
    @pytest.mark.parametrize("path", ["../../escape.bin", "/tmp/escape.bin", "a\\b"])
    def test_parse_rejects_traversal(self, path, compress):
        data = _bundle([(path, b"pwned")], compress=compress)
        with pytest.raises(BundleError, match="Invalid file path"):
            parse_bundle(data)

    def test_parse_rejects_non_ascii_title_id(self):
        data = bytearray(_bundle([("main", b"x")]))
        # Rewrite as a v4 header with a non-ASCII title id.
        payload_start = 4 + 4 + 8
        header = b"3DSS" + struct.pack("<I", 4) + b"\xff" * 32
        data = header + bytes(data[payload_start:])
        with pytest.raises(BundleError):
            parse_bundle(data)

    def test_upload_with_traversal_path_is_rejected(
        self, client, auth_headers, tmp_save_dir
    ):
        r = _upload(client, auth_headers, _bundle([("../../escape.bin", b"pwned")]))
        assert r.status_code == 400
        assert not (tmp_save_dir.parent / "escape.bin").exists()
        assert not (tmp_save_dir / "escape.bin").exists()

    def test_store_save_rejects_traversal_without_touching_current(self, tmp_save_dir):
        good = SaveBundle(
            title_id=TITLE_ID,
            timestamp=1,
            files=[BundleFile("main", 3, hashlib.sha256(b"old").digest(), b"old")],
        )
        storage.store_save(good)
        evil = SaveBundle(
            title_id=TITLE_ID,
            timestamp=2,
            files=[BundleFile("../x", 3, hashlib.sha256(b"new").digest(), b"new")],
        )
        with pytest.raises(UnsafePathError):
            storage.store_save(evil)
        assert storage.load_save_files(TITLE_HEX) == [("main", b"old")]
        assert not (tmp_save_dir / TITLE_HEX / "x").exists()

    def test_storage_rejects_unsafe_title_id(self):
        with pytest.raises(UnsafePathError):
            storage.title_exists("../outside")


class TestGcImportGameCodeValidation:
    def _card(self):
        """Card with a valid GM8E save plus a hostile ``A/..`` directory entry
        pointing at the same data block."""
        from app.services import gc_cards

        de = bytearray(b"\x00" * 64)
        de[0:4] = b"GM8E"
        de[4:6] = b"01"
        struct.pack_into(">H", de, 54, 99)
        struct.pack_into(">H", de, 56, 1)
        card = bytearray(gc_cards.gc_card_from_gci(bytes(de) + b"\x5a" * 8192))
        first = gc_cards._GC_DIR1_OFFSET
        second = first + gc_cards._GC_DENTRY_SIZE
        card[second : second + 64] = card[first : first + 64]
        card[second : second + 4] = b"A/.."
        return bytes(card)

    def test_traversal_game_code_is_skipped(self, client, auth_headers, tmp_save_dir):
        card = self._card()
        r = client.post(
            "/api/v1/saves/gc-vmc/import",
            content=card,
            headers={**auth_headers, "Content-Type": "application/octet-stream"},
        )
        assert r.status_code == 200
        body = r.json()
        assert [e["title_id"] for e in body["imported"]] == ["GC_GM8E"]
        assert body["skipped"] == ["A/.."]
        # Nothing was written at the save_dir root.
        assert not (tmp_save_dir / "current").exists()


# ── Zip bomb / request size ─────────────────────────────────────────────────


def _bomb(declared_size: int, real_size: int) -> bytes:
    """v2 bundle whose header declares ``declared_size`` but inflates to
    ``real_size`` bytes of zeros."""
    import zlib

    payload = b"\x00" * real_size
    return (
        b"3DSS"
        + struct.pack("<I", 2)
        + struct.pack(">Q", TITLE_ID)
        + struct.pack("<I", 1700000000)
        + struct.pack("<I", 1)
        + struct.pack("<I", declared_size)
        + zlib.compress(payload, 9)
    )


class TestDecompressionLimits:
    def test_inflation_beyond_declared_size_is_rejected(self):
        # Declares 1 KiB but inflates to 64 MiB: must stop at the declared size.
        with pytest.raises(BundleError, match="exceeds declared"):
            parse_bundle(_bomb(1024, 64 * 1024 * 1024))

    def test_declared_size_over_limit_is_rejected(self, monkeypatch):
        monkeypatch.setattr(settings, "max_decompressed_bytes", 1024 * 1024)
        with pytest.raises(BundleError, match="too large"):
            parse_bundle(_bomb(2 * 1024 * 1024, 2 * 1024 * 1024))

    def test_truncated_stream_is_rejected(self):
        data = _bundle([("main", b"A" * 10000)])
        with pytest.raises(BundleError):
            parse_bundle(data[:-20])

    def test_large_legit_bundle_still_parses(self):
        # An 8 MB memory-card image (PS2 / GC) is a normal upload.
        card = bytes(range(256)) * (8 * 1024 * 1024 // 256)
        parsed = parse_bundle(_bundle([("card.mc2", card)]))
        assert parsed.files[0].data == card

    def test_upload_of_bomb_returns_400(self, client, auth_headers):
        r = _upload(client, auth_headers, _bomb(1024, 16 * 1024 * 1024))
        assert r.status_code == 400


class TestUploadSizeLimit:
    def test_content_length_over_limit_is_rejected(
        self, client, auth_headers, monkeypatch
    ):
        monkeypatch.setattr(settings, "max_upload_bytes", 1024)
        r = client.post(
            f"/api/v1/saves/{TITLE_HEX}/raw",
            content=b"x" * 2048,
            headers={**auth_headers, "Content-Type": "application/octet-stream"},
        )
        assert r.status_code == 413
        assert not storage.title_exists(TITLE_HEX)

    def test_chunked_body_over_limit_is_rejected(
        self, client, auth_headers, monkeypatch
    ):
        monkeypatch.setattr(settings, "max_upload_bytes", 1024)

        def chunks():
            for _ in range(4):
                yield b"x" * 512

        r = client.post(
            f"/api/v1/saves/{TITLE_HEX}/raw",
            content=chunks(),
            headers={**auth_headers, "Content-Type": "application/octet-stream"},
        )
        assert r.status_code == 413
        assert not storage.title_exists(TITLE_HEX)

    def test_body_under_limit_is_accepted(self, client, auth_headers, monkeypatch):
        monkeypatch.setattr(settings, "max_upload_bytes", 4096)
        r = client.post(
            f"/api/v1/saves/{TITLE_HEX}/raw",
            content=b"x" * 2048,
            headers={**auth_headers, "Content-Type": "application/octet-stream"},
        )
        assert r.status_code == 200

    def test_limit_applies_to_json_endpoints(self, client, auth_headers, monkeypatch):
        monkeypatch.setattr(settings, "max_upload_bytes", 64)
        r = client.post(
            "/api/v1/sync",
            json={"titles": [], "console_id": "x" * 200},
            headers=auth_headers,
        )
        assert r.status_code == 413


# ── SSRF: update download proxy ─────────────────────────────────────────────

ASSET_URL = "https://github.com/pepasjc/GameSync/releases/download/v0.5.4/3dssync.cia"


@pytest.fixture()
def github_mock(monkeypatch):
    """Route the update proxy's HTTP client through a mock transport.

    ``routes`` maps a URL to an ``httpx.Response``; every request made is
    recorded in ``seen``.
    """
    import httpx

    from app.routes import update

    state = {"routes": {}, "seen": []}

    def handler(request):
        url = str(request.url)
        state["seen"].append(url)
        if url in state["routes"]:
            return state["routes"][url]
        return httpx.Response(404)

    def factory(**kwargs):
        return httpx.AsyncClient(transport=httpx.MockTransport(handler), **kwargs)

    monkeypatch.setattr(update, "_http_client", factory)
    return state


class TestUpdateProxySSRF:
    @pytest.mark.parametrize(
        "url",
        [
            "http://169.254.169.254/latest/meta-data/",
            "http://localhost:8000/api/v1/status",
            "http://github.com/pepasjc/GameSync/releases/download/v1/a.cia",
            "https://github.com.evil.example/pepasjc/GameSync/releases/download/v1/a.cia",
            "https://user@github.com/pepasjc/GameSync/releases/download/v1/a.cia",
            "https://github.com:8443/pepasjc/GameSync/releases/download/v1/a.cia",
            "https://github.com/someone/else/releases/download/v1/a.cia",
            "https://github.com/pepasjc/GameSync/archive/refs/heads/master.zip",
            "https://github.com/pepasjc/GameSync/releases/download/../../../x",
            "https://github.com/pepasjc/GameSync/releases/download/v1/%2e%2e",
            "https://github.com/pepasjc/GameSync/releases/download/v1/a.cia?x=1",
            "file:///etc/passwd",
        ],
    )
    def test_rejects_non_release_urls(self, client, auth_headers, github_mock, url):
        r = client.get("/api/v1/update/download", params={"url": url}, headers=auth_headers)
        assert r.status_code == 400
        assert github_mock["seen"] == []

    def test_proxies_release_asset_through_allowed_redirect(
        self, client, auth_headers, github_mock
    ):
        import httpx

        cdn = "https://release-assets.githubusercontent.com/github-production/abc?sig=1"
        github_mock["routes"][ASSET_URL] = httpx.Response(302, headers={"Location": cdn})
        github_mock["routes"][cdn] = httpx.Response(200, content=b"CIA-BYTES")
        r = client.get(
            "/api/v1/update/download", params={"url": ASSET_URL}, headers=auth_headers
        )
        assert r.status_code == 200
        assert r.content == b"CIA-BYTES"
        assert "3dssync.cia" in r.headers["content-disposition"]

    def test_redirect_off_github_is_refused(self, client, auth_headers, github_mock):
        import httpx

        github_mock["routes"][ASSET_URL] = httpx.Response(
            302, headers={"Location": "http://192.168.1.1/admin"}
        )
        r = client.get(
            "/api/v1/update/download", params={"url": ASSET_URL}, headers=auth_headers
        )
        assert r.status_code == 502
        assert github_mock["seen"] == [ASSET_URL]

    def test_oversized_download_is_refused(
        self, client, auth_headers, github_mock, monkeypatch
    ):
        import httpx

        from app.routes import update

        monkeypatch.setattr(update, "_MAX_DOWNLOAD_BYTES", 10)
        github_mock["routes"][ASSET_URL] = httpx.Response(200, content=b"x" * 100)
        r = client.get(
            "/api/v1/update/download", params={"url": ASSET_URL}, headers=auth_headers
        )
        assert r.status_code == 502

    def test_configurable_repo(self, client, auth_headers, github_mock, monkeypatch):
        import httpx

        monkeypatch.setattr(settings, "update_repo", "JoseAIG/GameSync")
        fork = "https://github.com/JoseAIG/GameSync/releases/download/v1/3dssync.cia"
        github_mock["routes"][fork] = httpx.Response(200, content=b"FORK")
        assert (
            client.get("/api/v1/update/download", params={"url": fork}, headers=auth_headers).content
            == b"FORK"
        )
        r = client.get(
            "/api/v1/update/download", params={"url": ASSET_URL}, headers=auth_headers
        )
        assert r.status_code == 400

    def test_proxy_can_be_disabled(self, client, auth_headers, github_mock, monkeypatch):
        monkeypatch.setattr(settings, "update_proxy_enabled", False)
        r = client.get(
            "/api/v1/update/download", params={"url": ASSET_URL}, headers=auth_headers
        )
        assert r.status_code == 404
        r = client.get("/api/v1/update/check", params={"current": "0.0.1"}, headers=auth_headers)
        assert r.status_code == 200
        assert r.json()["available"] is False
        assert github_mock["seen"] == []

    def test_check_uses_configured_repo(self, client, auth_headers, github_mock, monkeypatch):
        import httpx

        monkeypatch.setattr(settings, "update_repo", "JoseAIG/GameSync")
        api = "https://api.github.com/repos/JoseAIG/GameSync/releases/latest"
        github_mock["routes"][api] = httpx.Response(
            200,
            json={
                "tag_name": "v9.9.9",
                "body": "",
                "assets": [
                    {
                        "name": "3dssync.cia",
                        "browser_download_url": "https://github.com/JoseAIG/GameSync/releases/download/v9.9.9/3dssync.cia",
                        "size": 1,
                    }
                ],
            },
        )
        r = client.get("/api/v1/update/check", params={"current": "0.0.1"}, headers=auth_headers)
        assert r.json()["available"] is True


# ── Auth ────────────────────────────────────────────────────────────────────


class TestApiKeyAuth:
    def test_uses_constant_time_compare(self, client, api_key, monkeypatch):
        import hmac

        from app.middleware import auth

        calls = []
        real = hmac.compare_digest

        def spy(a, b):
            calls.append((a, b))
            return real(a, b)

        monkeypatch.setattr(auth.hmac, "compare_digest", spy)
        assert client.get("/api/v1/titles", headers={"X-API-Key": api_key}).status_code == 200
        assert client.get("/api/v1/titles", headers={"X-API-Key": "nope"}).status_code == 401
        assert len(calls) == 2

    def test_non_ascii_key_is_rejected_not_crashing(self, client):
        r = client.get("/api/v1/titles", params={"api_key": "ñandú"})
        assert r.status_code == 401

    def test_query_key_accepted_by_default(self, client, api_key):
        r = client.get("/api/v1/titles", params={"api_key": api_key})
        assert r.status_code == 200

    def test_query_key_can_be_disabled(self, client, api_key, monkeypatch):
        monkeypatch.setattr(settings, "allow_query_api_key", False)
        r = client.get("/api/v1/titles", params={"api_key": api_key})
        assert r.status_code == 401
        r = client.get("/api/v1/titles", headers={"X-API-Key": api_key})
        assert r.status_code == 200


# ── Public API docs ─────────────────────────────────────────────────────────


class TestDisableDocs:
    def test_docs_served_by_default(self, client):
        assert client.get("/openapi.json").status_code == 200
        assert client.get("/docs").status_code == 200

    def test_docs_can_be_disabled(self, monkeypatch):
        from fastapi.testclient import TestClient

        from app.main import create_app

        monkeypatch.setattr(settings, "disable_docs", True)
        c = TestClient(create_app())
        for path in ("/docs", "/redoc", "/openapi.json"):
            r = c.get(path, headers={"X-API-Key": settings.api_key})
            assert r.status_code == 404, path


# ── /roms/scan admin check ──────────────────────────────────────────────────


class TestRomScanAdmin:
    @pytest.fixture(autouse=True)
    def _no_rom_dir(self, monkeypatch):
        monkeypatch.setattr(settings, "rom_dir", None)
        monkeypatch.setattr(settings, "admin_users", "admin")

    def _scan(self, client, auth_headers, user=None):
        headers = dict(auth_headers)
        if user is not None:
            headers["X-Remote-User"] = user
        return client.get("/api/v1/roms/scan", headers=headers)

    def test_api_key_client_without_proxy_user_can_scan(self, client, auth_headers):
        assert self._scan(client, auth_headers).status_code == 200

    def test_untrusted_header_is_ignored(self, client, auth_headers, monkeypatch):
        monkeypatch.setattr(settings, "trust_proxy_auth", False)
        assert self._scan(client, auth_headers, "bob").status_code == 200

    def test_trusted_non_admin_is_refused(self, client, auth_headers, monkeypatch):
        monkeypatch.setattr(settings, "trust_proxy_auth", True)
        assert self._scan(client, auth_headers, "bob").status_code == 403

    def test_trusted_admin_can_scan(self, client, auth_headers, monkeypatch):
        monkeypatch.setattr(settings, "trust_proxy_auth", True)
        assert self._scan(client, auth_headers, "admin").status_code == 200

    def test_scan_still_needs_api_key(self, client, monkeypatch):
        monkeypatch.setattr(settings, "trust_proxy_auth", True)
        r = client.get("/api/v1/roms/scan", headers={"X-Remote-User": "admin"})
        assert r.status_code == 401
