"""Validation for client-supplied names that end up on disk.

Bundle file paths, title IDs and anything else a client controls must never
be able to escape the directory it is written into.  Every write that joins
a client-supplied name onto a server directory goes through ``safe_join``.
"""

from __future__ import annotations

from pathlib import Path, PurePosixPath, PureWindowsPath


class UnsafePathError(ValueError):
    pass


def validate_relative_path(path: str) -> str:
    """Return ``path`` if it is a safe, relative path.

    Rejects empty paths, absolute paths (POSIX or Windows drive/UNC),
    NUL bytes and empty / ``.`` / ``..`` components.  ``\\`` counts as a
    separator too: the Xbox client sends ``<saveid>\\savedata.dat``, which
    v055 stored as-is, so it stays accepted, but every component on either
    separator must be a plain name.  The path is returned unchanged so names
    already stored on disk keep round-tripping.
    """
    if not path:
        raise UnsafePathError("empty path")
    if "\x00" in path:
        raise UnsafePathError(f"NUL byte in path {path!r}")
    if PurePosixPath(path).is_absolute() or PureWindowsPath(path).drive:
        raise UnsafePathError(f"absolute path {path!r}")
    for part in path.replace("\\", "/").split("/"):
        if part in ("", ".", ".."):
            raise UnsafePathError(f"invalid path component in {path!r}")
    return path


def validate_path_component(name: str) -> str:
    """Return ``name`` if it is safe to use as a single directory entry."""
    validate_relative_path(name)
    if "/" in name or "\\" in name:
        raise UnsafePathError(f"path separator in name {name!r}")
    return name


def safe_join(base: Path, rel: str) -> Path:
    """Join ``rel`` onto ``base``, guaranteeing the result stays inside ``base``.

    The lexical check rejects traversal syntax; the ``resolve()`` check also
    catches escapes through symlinks that already exist under ``base``.
    """
    validate_relative_path(rel)
    target = base / rel
    try:
        target.resolve().relative_to(base.resolve())
    except ValueError:
        raise UnsafePathError(f"path {rel!r} escapes {base}") from None
    return target
