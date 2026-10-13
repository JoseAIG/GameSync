"""GET /emulators/ps5sx2/game-index: PS5SX2's GameIndex.yaml through the server."""

import json

import httpx
import pytest

from app.routes import emulators

GAME_INDEX = b"SLUS-20488:\n  name: \"Star Ocean 3\"\n  gameFixes:\n    - VuAddSubHack\n" * 4000


class FakeGitHub:
    """httpx transport standing in for api.github.com and raw.githubusercontent.com."""

    def __init__(self, tag="vk-285-163", index=GAME_INDEX):
        self.tag = tag
        self.index = index
        self.calls = []
        self.down = False

    def __call__(self, request: httpx.Request) -> httpx.Response:
        self.calls.append(str(request.url))
        if self.down:
            raise httpx.ConnectError("offline", request=request)
        if request.url.host == "api.github.com":
            return httpx.Response(200, json={"tag_name": self.tag})
        if request.url.path == f"/Swordpdf/PS5SX2/{self.tag}/bin/resources/GameIndex.yaml":
            return httpx.Response(200, content=self.index)
        return httpx.Response(404)


@pytest.fixture()
def github(monkeypatch):
    fake = FakeGitHub()
    real = httpx.AsyncClient

    def client(*args, **kwargs):
        kwargs["transport"] = httpx.MockTransport(fake)
        return real(*args, **kwargs)

    monkeypatch.setattr(emulators.httpx, "AsyncClient", client)
    return fake


def test_serves_and_caches(client, auth_headers, github, tmp_save_dir):
    resp = client.get("/api/v1/emulators/ps5sx2/game-index", headers=auth_headers)
    assert resp.status_code == 200
    assert resp.content == GAME_INDEX
    assert resp.headers["X-Source-Tag"] == "vk-285-163"
    assert len(resp.headers["X-File-Hash"]) == 64
    cached = tmp_save_dir / ".emulator_cache" / "ps5sx2" / "GameIndex.yaml"
    assert cached.read_bytes() == GAME_INDEX

    # within the check interval GitHub isn't asked again
    calls = len(github.calls)
    assert client.get("/api/v1/emulators/ps5sx2/game-index", headers=auth_headers).status_code == 200
    assert len(github.calls) == calls


def test_new_release_replaces_the_copy(client, auth_headers, github, tmp_save_dir):
    client.get("/api/v1/emulators/ps5sx2/game-index", headers=auth_headers)
    meta = tmp_save_dir / ".emulator_cache" / "ps5sx2" / "GameIndex.json"
    stale = json.loads(meta.read_text())
    stale["checked"] = 0
    meta.write_text(json.dumps(stale))
    github.tag = "vk-285-164"
    github.index = GAME_INDEX + b"SLUS-99999:\n  name: \"New\"\n"
    resp = client.get("/api/v1/emulators/ps5sx2/game-index", headers=auth_headers)
    assert resp.headers["X-Source-Tag"] == "vk-285-164"
    assert resp.content == github.index


def test_offline_serves_the_cached_copy(client, auth_headers, github, tmp_save_dir):
    client.get("/api/v1/emulators/ps5sx2/game-index", headers=auth_headers)
    meta = tmp_save_dir / ".emulator_cache" / "ps5sx2" / "GameIndex.json"
    stale = json.loads(meta.read_text())
    stale["checked"] = 0
    meta.write_text(json.dumps(stale))
    github.down = True
    resp = client.get("/api/v1/emulators/ps5sx2/game-index", headers=auth_headers)
    assert resp.status_code == 200
    assert resp.content == GAME_INDEX


def test_offline_without_a_copy_is_502(client, auth_headers, github):
    github.down = True
    assert client.get("/api/v1/emulators/ps5sx2/game-index", headers=auth_headers).status_code == 502


def test_refuses_something_that_is_not_the_database(client, auth_headers, github):
    github.index = b"<html>rate limited</html>"
    assert client.get("/api/v1/emulators/ps5sx2/game-index", headers=auth_headers).status_code == 502


def test_needs_the_api_key(client, github):
    assert client.get("/api/v1/emulators/ps5sx2/game-index").status_code == 401
