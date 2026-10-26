# GameSync — 3DS Client

Homebrew client for Nintendo 3DS / 2DS. Syncs save files with the GameSync server over local WiFi using a three-way hash protocol to handle multiple consoles without conflicts.

## Requirements

- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with 3DS support
- Required packages (install via `pacman` inside the devkitPro MSYS2 shell):

```bash
pacman -S 3ds-dev 3ds-zlib
```

## Build

**On Windows** — open the devkitPro MSYS2 shell (not Git Bash), then:

```bash
make          # produces 3dssync.3dsx
make cia      # produces 3dssync.cia
make clean
```

**On Linux / macOS:**

```bash
make          # produces 3dssync.3dsx
make cia      # produces 3dssync.cia
make clean
```

### Output files

| File | How to use |
|---|---|
| `3dssync.3dsx` | Run via [Homebrew Launcher](https://github.com/fincs/generic-hid) from the SD card |
| `3dssync.cia` | Install with [FBI](https://github.com/Steveice10/FBI) for a permanent home menu icon |

## Installation

### Homebrew Launcher (.3dsx)
Copy `3dssync.3dsx` to `sdmc:/3ds/3dssync/3dssync.3dsx` on the 3DS SD card.

### Home menu app (.cia)
Install `3dssync.cia` using FBI or trigger an in-app auto-update from the SELECT button.

## Configuration

On first launch a default config is created at:

```
sdmc:/3ds/3dssync/config.txt
```

Edit it with the details of your GameSync server:

```ini
server_url=http://192.168.1.100:8000
api_key=your-secret-key
```

The in-app config editor (L button) can also edit these values directly on the console using the system keyboard.

### Optional settings

| Key | Default | Meaning |
|---|---|---|
| `utc_offset_minutes` | `0` | Offset of the console clock from UTC, in minutes (e.g. `-240` for UTC-4, `60` for UTC+1). The 3DS clock is local time with no time zone; this lets the client send real Unix (UTC) timestamps so they line up with other devices. |
| `local_backups` | `3` | Before a download or history restore overwrites a save, the current save is copied to `sdmc:/3ds/3dssync/backups/<TITLEID>/<YYYYMMDD-HHMMSS>/`. This many copies are kept per title (oldest deleted first); `0` disables the copies. |

## Safety when writing saves

- Every file of a downloaded bundle is checked against its SHA-256 before anything is written; a mismatch aborts the download.
- The current save is backed up to the SD card first (see `local_backups`). If it cannot be read or backed up completely, nothing is written.
- If writing or committing the new save fails, the previous save is written back. To recover by hand, copy the files from the backup folder into the save with a save manager such as Checkpoint.
- A save with more than 64 files is refused instead of being uploaded incompletely.

## Controls

| Button | Action |
|---|---|
| A | Sync selected save (upload or download based on sync plan) |
| X | Sync all saves |
| B | Cancel / back |
| L | Open config editor |
| R | Show save details (local/server hashes, sync status) |
| SELECT | Check for updates |
| START | Exit |
