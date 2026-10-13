"""Re-key saves stored under a translation's or hack's own slug.

Background
----------
Square-bracket tags used to be part of a name-slug save id, so every fan
translation version and every hack had its own slot:
``SNES_chrono_trigger_t_en_by_x_v1_1_japan`` next to ``..._v1_0_japan`` and
the original ``SNES_chrono_trigger_japan``. A translation or hack plays the
save of the game it patches, so ``shared/rom_id/normalizer.py`` now drops
``[...]`` tags and they all share ``SNES_chrono_trigger_japan`` (what the
Android client always did).

This migration moves saves already stored under a tagged id. The old id of a
ROM is its slug with the tags kept (``normalize_rom_name(name,
keep_tags=True)``), the new one without; the ROM catalog's file names say which
old ids exist. Where the new id already holds a save, they are merged with the
same newest-wins rules as the other re-key migrations:

- the variant with the newest ``client_timestamp`` wins and becomes ``current/``
- the loser's save is preserved under the winner's ``history/`` (never deleted)
- the loser's own history versions are moved across as well
- the loser's DB row and directory are removed; ``metadata.json`` is rewritten

A tagged save whose ROM isn't in the catalog is left as it is (listed at the
end), so it stays addressable.

Idempotent: a second run finds nothing to do. Defaults to a dry run; pass
``--apply`` to write.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

from app.config import settings
from app.services import db, rom_db, storage
from shared.rom_id import normalize_rom_name
from shared.systems import SYNC_ID_RULES, SYSTEM_ALIASES

from migrate_gc_title_case import (
    _merge_loser,
    _rewrite_metadata_json,
    _row_for,
    _same_dir,
    _variant_sort_key,
)


def _slug_system(system: str) -> str | None:
    system = SYSTEM_ALIASES.get((system or "").upper(), (system or "").upper())
    rule = SYNC_ID_RULES.get(system, {"strategy": "slug"})
    return system if rule.get("strategy", "slug") == "slug" else None


def old_and_new_ids(system: str, filename: str) -> tuple[str, str] | None:
    """(id with the tags, id without) for a slug system's ROM, or None."""
    system = _slug_system(system)
    if not system or "[" not in (filename or ""):
        return None
    stem = Path(filename).name
    old = f"{system}_{normalize_rom_name(stem, keep_tags=True)}"
    new = f"{system}_{normalize_rom_name(stem)}"
    return (old, new) if old != new else None


def _stored(title_id: str) -> bool:
    return db.exists(title_id) or (settings.save_dir / title_id).exists()


def _collect_groups(extra: dict[str, str] | None = None) -> dict[str, list[str]]:
    """New id -> the stored ids (old spellings, and the new one) to merge.
    extra maps more old ids to new ones (a ROM no longer in the catalog)."""
    rom_db.init_db(settings.save_dir)
    seen: dict[str, set[str]] = defaultdict(set)
    for old, new in (extra or {}).items():
        if _stored(old):
            seen[new].add(old)
    for row in rom_db.list_all():
        pair = old_and_new_ids(str(row.get("system") or ""), str(row.get("filename") or ""))
        if pair and _stored(pair[0]):
            seen[pair[1]].add(pair[0])
    groups = {}
    for new, olds in seen.items():
        members = set(olds)
        if _stored(new):
            members.add(new)
        groups[new] = sorted(members)
    return groups


def migrate(apply: bool = False, extra: dict[str, str] | None = None) -> tuple[int, int]:
    """Return (groups_examined, groups_changed)."""
    groups = _collect_groups(extra)
    examined = changed = 0
    for canonical, variants in sorted(groups.items()):
        examined += 1
        rows = {v: _row_for(v) for v in variants}
        winner = max(variants, key=lambda v: _variant_sort_key(rows[v]))
        losers = [v for v in variants if v != winner]
        print(f"{canonical}: variants={variants} winner={winner}")

        winner_dir = settings.save_dir / winner
        canonical_dir = settings.save_dir / canonical
        for loser in losers:
            if (settings.save_dir / loser).exists() and not _same_dir(settings.save_dir / loser,
                                                                      winner_dir):
                _merge_loser(loser, winner_dir, rows[loser], apply)
        if winner != canonical:
            print(f"    rename dir {winner} -> {canonical}")
            if apply and winner_dir.exists():
                winner_dir.rename(canonical_dir)
        for variant in variants:
            if variant != canonical and db.exists(variant):
                print(f"    drop DB row {variant}")
                if apply:
                    db.delete(variant)
        winning_row = dict(rows[winner])
        winning_row["title_id"] = canonical
        print(f"    upsert DB row {canonical}")
        if apply:
            db.upsert(winning_row)
            _rewrite_metadata_json(canonical, apply)
            storage.rebuild_metadata_from_current(
                canonical, source=winning_row.get("last_sync_source") or "migration_tagged_slug"
            )
        else:
            _rewrite_metadata_json(canonical, apply)
        changed += 1
    return examined, changed


def unmatched_tagged() -> list[str]:
    """Stored slug ids that look tagged but no catalog ROM explains."""
    looks = ("_t_en", "_t_eng", "_hack", "_by_")
    out = []
    for row in db.list_all():
        tid = str(row.get("title_id") or "")
        system = tid.split("_", 1)[0]
        if _slug_system(system) and any(m in tid.lower() for m in looks):
            out.append(tid)
    return sorted(out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="write changes (default: dry run)")
    parser.add_argument("--also", action="append", default=[], metavar="OLD=NEW",
                        help="also move OLD to NEW (a tagged save whose ROM left the catalog)")
    args = parser.parse_args()
    extra = dict(item.split("=", 1) for item in args.also)
    examined, changed = migrate(apply=args.apply, extra=extra)
    mode = "Applied" if args.apply else "DRY RUN - would update"
    print(f"\n{mode}: {changed} of {examined} save(s) under a translation's or hack's own id.")
    left = unmatched_tagged()
    if left:
        print("Still tagged (their ROM isn't in the catalog, left as they are):")
        for tid in left:
            print("   ", tid)
    if not args.apply and changed:
        print("Re-run with --apply to write.")
