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
