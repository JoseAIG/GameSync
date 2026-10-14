"""/roms/{id}/file/{member} out of a zipped single-file ROM (a Wii zip holding
an .rvz): stored members with Range, deflated and zstd ones streamed, and the
manifest listing what is inside."""

import os
import struct
import zipfile
import zlib

import pytest
import zstandard

from app.config import settings


def _zstd_zip(path, name: str, data: bytes) -> None:
    """A one-member zip with method 93 (zstd), which zipfile can't write."""
    comp = zstandard.ZstdCompressor().compress(data)
    crc = zlib.crc32(data)
    fname = name.encode()
    local = struct.pack("<4sHHHHHIIIHH", b"PK\x03\x04", 63, 0, 93, 0, 0, crc,
                        len(comp), len(data), len(fname), 0) + fname
    central = struct.pack("<4sHHHHHHIIIHHHHHII", b"PK\x01\x02", 63, 63, 0, 93, 0, 0,
                          crc, len(comp), len(data), len(fname), 0, 0, 0, 0, 0, 0) + fname
    cd_offset = len(local) + len(comp)
    end = struct.pack("<4sHHHHIIH", b"PK\x05\x06", 0, 0, 1, 1, len(central), cd_offset, 0)
    path.write_bytes(local + comp + central + end)


@pytest.fixture()
def wii_client(tmp_path, client):
    from app.services import rom_db, rom_scanner

    rom_dir = tmp_path / "roms"
    (rom_dir / "wii").mkdir(parents=True)
    original, interval = settings.rom_dir, settings.rom_scan_interval
    settings.rom_dir, settings.rom_scan_interval = rom_dir, 0
    rom_db.init_db(settings.save_dir)
    payload = os.urandom(300_000)
    with zipfile.ZipFile(rom_dir / "wii" / "Stored Game (USA).zip", "w", zipfile.ZIP_STORED) as z:
        z.writestr("Stored Game (USA).rvz", payload)
    with zipfile.ZipFile(rom_dir / "wii" / "Deflated Game (USA).zip", "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("Deflated Game (USA).rvz", payload)
    _zstd_zip(rom_dir / "wii" / "Zstd Game (USA).zip", "Zstd Game (USA).rvz", payload)
    rom_scanner.init(rom_dir)
    yield client, payload
    settings.rom_dir, settings.rom_scan_interval = original, interval
    rom_scanner._catalog = None


def _rom_id(client, auth_headers, name):
    roms = client.get("/api/v1/roms?system=WII", headers=auth_headers).json()["roms"]
    return next(r["rom_id"] for r in roms if r["filename"].startswith(name))


@pytest.mark.parametrize("game", ["Stored Game", "Deflated Game", "Zstd Game"])
def test_member_comes_out_whole(wii_client, auth_headers, game):
    client, payload = wii_client
    rid = _rom_id(client, auth_headers, game)
    manifest = client.get(f"/api/v1/roms/{rid}/manifest", headers=auth_headers).json()
    assert manifest["members"][0]["name"] == f"{game} (USA).rvz"
    assert manifest["members"][0]["size"] == len(payload)
    r = client.get(f"/api/v1/roms/{rid}/file/{game} (USA).rvz", headers=auth_headers)
    assert r.status_code == 200
    assert r.content == payload


def test_stored_member_honours_range(wii_client, auth_headers):
    client, payload = wii_client
    rid = _rom_id(client, auth_headers, "Stored Game")
    r = client.get(f"/api/v1/roms/{rid}/file/Stored Game (USA).rvz",
                   headers={**auth_headers, "Range": "bytes=1000-200999"})
    assert r.status_code == 206
    assert r.headers["Content-Range"] == f"bytes 1000-200999/{len(payload)}"
    assert r.content == payload[1000:201000]


def test_compressed_member_ignores_range(wii_client, auth_headers):
    client, payload = wii_client
    rid = _rom_id(client, auth_headers, "Deflated Game")
    r = client.get(f"/api/v1/roms/{rid}/file/Deflated Game (USA).rvz",
                   headers={**auth_headers, "Range": "bytes=1000-"})
    assert r.status_code == 200  # whole member: a compressed offset can't be seeked to
    assert r.content == payload


def test_missing_member_and_escape(wii_client, auth_headers):
    client, _ = wii_client
    rid = _rom_id(client, auth_headers, "Stored Game")
    assert client.get(f"/api/v1/roms/{rid}/file/nope.rvz", headers=auth_headers).status_code == 404
    assert client.get(f"/api/v1/roms/{rid}/file/../x", headers=auth_headers).status_code in (400, 404)
