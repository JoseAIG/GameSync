"""Saves under a translation's or hack's own slug move to the game's slot."""

from app.config import settings
from app.services import db, rom_db
from migrate_tagged_slug_ids import migrate, old_and_new_ids


def _make_title(title_id: str, payload: bytes, client_timestamp: int) -> None:
    current = settings.save_dir / title_id / "current"
    current.mkdir(parents=True, exist_ok=True)
    (current / "game.srm").write_bytes(payload)
    db.upsert({
        "title_id": title_id,
        "name": "Chrono Trigger",
        "last_sync": "2026-01-01T00_00_00+00_00",
        "last_sync_source": "raw",
        "save_hash": "deadbeef",
        "save_size": len(payload),
        "file_count": 1,
        "client_timestamp": client_timestamp,
        "server_timestamp": "2026-01-01T00:00:00+00:00",
        "console_id": "test",
        "platform": "SNES",
        "system": "SNES",
    })


def _catalog(*filenames: str) -> None:
    rom_db.init_db(settings.save_dir)
    rom_db.upsert([{
        "rom_id": f"SNES_{i}", "title_id": f"SNES_{i}", "system": "SNES", "name": name,
        "filename": name, "path": f"snes/{name}", "size": 1, "crc32": "", "source": "test",
    } for i, name in enumerate(filenames)])


def _ids() -> set[str]:
    return {r["title_id"] for r in db.list_all()}


def test_old_and_new_ids():
    assert old_and_new_ids("SNES", "Chrono Trigger (Japan) [T-En by X v1.1].sfc") == (
        "SNES_chrono_trigger_t_en_by_x_v1_1_japan", "SNES_chrono_trigger_japan")
    assert old_and_new_ids("SNES", "Chrono Trigger (Japan).sfc") is None
    assert old_and_new_ids("PS1", "Game (USA) [T-En].cue") is None  # serial system


def test_a_translations_save_moves_to_the_games_slot(tmp_save_dir):
    _catalog("Chrono Trigger (Japan) [T-En by X v1.1].sfc")
    _make_title("SNES_chrono_trigger_t_en_by_x_v1_1_japan", b"translated", 100)

    assert migrate(apply=False) == (1, 1)
    assert _ids() == {"SNES_chrono_trigger_t_en_by_x_v1_1_japan"}  # dry run
    migrate(apply=True)
    assert _ids() == {"SNES_chrono_trigger_japan"}
    assert (settings.save_dir / "SNES_chrono_trigger_japan" / "current" / "game.srm").read_bytes() \
        == b"translated"
    assert migrate(apply=True) == (0, 0)  # idempotent


def test_two_versions_and_the_original_merge_newest_wins(tmp_save_dir):
    _catalog("Chrono Trigger (Japan) [T-En by X v1.0].sfc",
             "Chrono Trigger (Japan) [T-En by X v1.1].sfc",
             "Chrono Trigger (Japan).sfc")
    _make_title("SNES_chrono_trigger_t_en_by_x_v1_0_japan", b"old", 100)
    _make_title("SNES_chrono_trigger_t_en_by_x_v1_1_japan", b"newest", 300)
    _make_title("SNES_chrono_trigger_japan", b"original", 200)

    migrate(apply=True)

    assert _ids() == {"SNES_chrono_trigger_japan"}
    slot = settings.save_dir / "SNES_chrono_trigger_japan"
    assert (slot / "current" / "game.srm").read_bytes() == b"newest"
    kept = sorted(p.read_bytes() for p in (slot / "history").rglob("game.srm"))
    assert kept == [b"old", b"original"]  # the others are kept in history
