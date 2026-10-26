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
Install `3dssync.cia` using FBI, or update an installed copy from the config menu (L → Check for Updates).

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
| `server_url_alt` | *(empty)* | Optional second server address, e.g. the same server's IP on another home network. At startup the client asks `server_url` for `/status`; if it doesn't answer, it tries `server_url_alt` and uses it for that session. The address in use is shown on the bottom screen. |
| `utc_offset_minutes` | `0` | Offset of the console clock from UTC, in minutes (e.g. `-240` for UTC-4, `60` for UTC+1). The 3DS clock is local time with no time zone; this lets the client send real Unix (UTC) timestamps so they line up with other devices. |
| `local_backups` | `3` | Before a download or history restore overwrites a save, the current save is copied to `sdmc:/3ds/3dssync/backups/<TITLEID>/<YYYYMMDD-HHMMSS>/`. This many copies are kept per title (oldest deleted first); `0` disables the copies. |
| `tls_verify` | `1` | For `https://` server URLs, verify the server certificate. `0` accepts any certificate (the behaviour of older versions; use it for a self-signed server). Has no effect on `http://`. |
| `update_check` | `1` | Allow *Check for Updates* in the config menu (L). The server looks up the latest GitHub release; the client downloads the CIA through the server and installs it. The download must match the size the server reports, and its SHA-256 when the server publishes one (`sha256`, or GitHub's `digest` field). `0` disables update checks and installs. |

### HTTPS

`server_url` can be `http://<lan-ip>:8000` or an `https://` address, e.g. a server published through a Cloudflare Tunnel or a reverse proxy.

With `tls_verify=1` the certificate must chain to a trusted root. The 3DS system trust store is very old, so the client trusts its still-valid roots (COMODO RSA, USERTrust RSA — used by Sectigo and ZeroSSL — and DigiCert High Assurance EV) plus these built-in RSA roots (see `source/root_certs.c` for sources and SHA-256 fingerprints):

- ISRG Root X1 (Let's Encrypt)
- GTS Root R1 (Google Trust Services)
- SSL.com TLS RSA Root CA 2022

To trust another CA, copy its root certificate in DER format as `sdmc:/3ds/3dssync/certs/<name>.der` (up to 8 files, 8 KB each). To convert a PEM file: `openssl x509 -in root.pem -outform DER -out root.der`.

The 3DS SSL module is old: TLS 1.2 at most, a small set of cipher suites, and in practice only RSA certificates with RSA key exchange. If HTTPS requests fail, the status line shows the httpc result code from the failed step (the TLS handshake happens in the `connect` step; httpc reports no separate SSL error code) and `sdmc:/3ds/3dssync/network.log` records each failed request and any root certificate the SSL module rejected.

**Cloudflare:** HTTPS endpoints proxied by Cloudflare (including Cloudflare Tunnel) on the free plan typically serve only an ECDSA certificate and accept only ECDHE-ECDSA cipher suites, which the 3DS cannot negotiate. Use the server's LAN address over `http://` from the 3DS, or an HTTPS endpoint with an RSA certificate (for example your own reverse proxy with a Let's Encrypt RSA certificate).

## Safety when writing saves

- Every file of a downloaded bundle is checked against its SHA-256 before anything is written; a mismatch aborts the download.
- The current save is backed up to the SD card first (see `local_backups`). If it cannot be read or backed up completely, nothing is written.
- If writing or committing the new save fails, the previous save is written back. To recover by hand, copy the files from the backup folder into the save with a save manager such as Checkpoint.
- A save with more than 256 files is refused instead of being uploaded incompletely.

## Limitations

- **Extra data (extdata) is not synced.** Only the title's save data archive (`ARCHIVE_USER_SAVEDATA`) is read and written. Games that keep part of their state in extdata (often StreetPass, downloaded content or photos) will only partly sync; back up extdata separately with a save manager such as Checkpoint.
- A save may have at most 256 files, and its compressed bundle must fit in 448 KB to upload; downloads are limited to about 2 MB.
- File names inside a save are handled as ASCII; non-ASCII names are not supported.
- Cartridge titles are never included in *Sync all*; sync them one at a time.

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
