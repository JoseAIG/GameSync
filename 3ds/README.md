# GameSync — 3DS Client

Homebrew client for Nintendo 3DS / 2DS. Syncs save files with the GameSync server over local WiFi using a three-way hash protocol to handle multiple consoles without conflicts.

## Requirements

- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with 3DS support
- Required packages (install via `pacman` inside the devkitPro MSYS2 shell):

```bash
pacman -S 3ds-dev 3ds-zlib
```

`3ds-dev` includes citro2d and citro3d, which draw the GUI.

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

For performance work, `make clean && make GUI_PERF=1` builds a version that
shows, in the top-right corner of the top screen, the last frame's citro3d
time (processing + drawing, in ms) and `cpu`, the loop's own CPU time that
frame (input, springs and row drawing, from the input scan to the end of
the frame, without the vsync wait). Either one over the 12 ms budget turns
red. The normal build has no overlay.

### Output files

| File | How to use |
|---|---|
| `3dssync.3dsx` | Run via [Homebrew Launcher](https://github.com/fincs/generic-hid) from the SD card |
| `3dssync.cia` | Install with [FBI](https://github.com/Steveice10/FBI) for a permanent home menu icon |

## Installation

### Homebrew Launcher (.3dsx)
Copy `3dssync.3dsx` to `sdmc:/3ds/3dssync/3dssync.3dsx` on the 3DS SD card.

### Home menu app (.cia)
Install `3dssync.cia` using FBI, or update in-app from *Settings > Check for updates now* (see `update_check` below).

## Configuration

The settings live in:

```
sdmc:/3ds/3dssync/config.txt
```

with at least the details of your GameSync server:

```ini
server_url=http://192.168.1.100:8000
api_key=your-secret-key
```

**First run:** if the file is missing (or has no server address or API key),
the app says so and offers **A** *Set up now* (START exits). It asks for the
server address and then the API key with the system keyboard, writes
`config.txt` (creating `sdmc:/3ds/3dssync/` if needed), checks that the server
answers and accepts the key, and starts. If the check fails you can go back
and edit (**B**) or continue anyway (**A**); the values are saved either way.
B in the address field goes back to the first message.

The in-app **Settings** tab edits these values directly on the console (system
keyboard, or a D-pad editor if the keyboard applet can't start).

### Optional settings

These keys can be edited in `config.txt`; all but `utc_offset_minutes` are
also rows in the Settings tab. Saving from Settings keeps every key, including
the ones it doesn't show.

| Key | Default | Meaning |
|---|---|---|
| `server_url_alt` | *(empty)* | Optional second server address, e.g. the same server's IP on another home network. At startup the client asks `server_url` for `/status`; if it doesn't answer, it tries `server_url_alt` and uses it for that session. |
| `utc_offset_minutes` | `0` | Offset of the console clock from UTC, in minutes (e.g. `-240` for UTC-4, `60` for UTC+1). The 3DS clock is local time with no time zone; this lets the client send real Unix (UTC) timestamps so they line up with other devices. |
| `local_backups` | `3` | Before a download or history restore overwrites a save, the current save is copied to `sdmc:/3ds/3dssync/backups/<TITLEID>/<YYYYMMDD-HHMMSS>/`. This many copies are kept per title (oldest deleted first); `0` disables the copies. |
| `tls_verify` | `1` | For `https://` server URLs, verify the server certificate. `0` accepts any certificate (the behaviour of older versions; use it for a self-signed server). Has no effect on `http://`. |
| `reduce_motion` | `0` | `1` turns off sliding and springing: the list highlight and scrolling jump instead of gliding, nothing nudges or bounces, and tabs and dialogs fade in instead of sliding or rising. The fade when a list wraps around, the dimming fading out after a dialog and the progress bar's glide stay. |
| `update_check` | `1` | Allow *Settings > Check for updates now* (the Settings switch *Allow update checks*). The server looks up the latest GitHub release; the client downloads the CIA through the server and installs it. The download must match the size the server reports, and its SHA-256 when the server publishes one (`sha256`, or GitHub's `digest` field). `0` disables update checks and installs. |

### HTTPS

`server_url` can be `http://<lan-ip>:8000` or an `https://` address, e.g. a server published through a Cloudflare Tunnel or a reverse proxy.

With `tls_verify=1` the certificate must chain to a trusted root. The 3DS system trust store is very old, so the client trusts its still-valid roots (COMODO RSA, USERTrust RSA — used by Sectigo and ZeroSSL — and DigiCert High Assurance EV) plus these built-in RSA roots (see `source/root_certs.c` for sources and SHA-256 fingerprints):

- ISRG Root X1 (Let's Encrypt)
- GTS Root R1 (Google Trust Services)
- SSL.com TLS RSA Root CA 2022

To trust another CA, copy its root certificate in DER format as `sdmc:/3ds/3dssync/certs/<name>.der` (up to 8 files, 8 KB each). To convert a PEM file: `openssl x509 -in root.pem -outform DER -out root.der`.

The 3DS SSL module is old: TLS 1.2 at most, a small set of cipher suites, and in practice only RSA certificates with RSA key exchange. If HTTPS requests fail, the status bar above the footer shows the httpc result code from the failed step (the TLS handshake happens in the `connect` step; httpc reports no separate SSL error code) and `sdmc:/3ds/3dssync/network.log` records each failed request and any root certificate the SSL module rejected.

**Cloudflare:** HTTPS endpoints proxied by Cloudflare (including Cloudflare Tunnel) on the free plan typically serve only an ECDSA certificate and accept only ECDHE-ECDSA cipher suites, which the 3DS cannot negotiate. Use the server's LAN address over `http://` from the 3DS, or an HTTPS endpoint with an RSA certificate (for example your own reverse proxy with a Let's Encrypt RSA certificate).

## Screens

The GUI is drawn with citro2d in a dark theme shared with the DS client. The
app has three top-level tabs, **Saves | Catalog | Settings**, shown in the top
screen's header between the `L` and `R` glyphs. The header also shows the WiFi
strength and a server status dot (green = the server answered, red = it
didn't). The top screen has a footer with button hints; the list screens have
an action bar of buttons at the bottom of the touch screen, each showing the
key that does the same thing.

- **Saves**: the list is on the bottom screen, with All / 3DS / NDS sub-tabs, a
  mark box per row (when titles are marked, a `B N marked ×` chip in the header
  unmarks them all), a system tag (`3DS`, `NDS`, cyan `Cart` for game cards) and
  a state badge: a check (up to date, uploaded, downloaded), an up or down
  arrow (needs upload / download), `!` (conflict), `×` (failed) or a small card
  (*manual sync*: game cards, which Sync All skips). When a state changes a
  ring briefly expands from its badge; a new mark pops in. A magnifier at the
  right of the header searches the list by name (see below). The top screen
  shows the selected title: name, its title id and product code as a caption,
  where the save lives, and what the last compare found in words ("Only this
  console changed", "Only the server changed" with the device and date,
  "Matches the server · just now", "Both changed"), the server's size and
  files and when it was last synced. A failed save shows why it failed. The
  bar above the footer shows the last action's result; a new one slides in,
  and after a few seconds it dims so it no longer looks current.
- **Smart Sync / details**: this console's and the server's copy as two equal
  cards side by side (whether this console's copy changed since the last sync
  or the server's date, size and files, and which device it came from), with
  the verdict below and a confirmation card on the bottom screen that says
  what will happen. A save that is already in sync opens no card: the status
  bar says it matches the server. An upload much smaller than the server's
  copy warns that it may be a new game or an old backup. Hashes are only
  shown in **Y** > *Compare with the server*.
- **Errors**: when something fails, a red card says what happened, whether
  anything was changed (for example "Nothing was changed", or "Writing failed;
  your previous save was put back") and what to do next, with **A** *Try
  again* where retrying makes sense. If the server can't be reached, nothing
  is uploaded or downloaded. After *Sync all* or a batch upload with
  failures, a card lists the saves that failed in that run and why, and
  says whether they were changed: if a download couldn't be written and the
  previous save couldn't be put back either, it says so first and points to
  the local backup.
- **Progress**: syncs, restores, updates and catalog installs show a card with
  a progress bar (with speed, elapsed and remaining time for downloads).
  Downloads that can be stopped have a **B Cancel** button. Talking to the
  server (comparing a save, uploads, downloads, *Sync all*, History, game
  names, the update check, *Refresh catalog* and catalog pages) runs in the
  background, so the screen never freezes: the spinner keeps turning and the
  list underneath keeps moving. If the server hasn't answered for 5 seconds
  the card says "Waiting for the server… 0:07", and where stopping is safe
  (anything that only reads: comparing, History, names, the update check,
  *Refresh catalog*, catalog pages) it offers **B Cancel**, which leaves
  everything as it was. Uploads, downloads and *Sync all* can't be cancelled
  once they have started. A cancelled request may still be running on the
  server; the next thing that needs the server waits for it ("Finishing the
  request you cancelled…"). A catalog page that loads quickly shows no card
  at all, only the list's own loading rows.
- **Catalog**, **Settings** and **History** use the same layout: a list on the
  bottom screen, details of the highlighted entry on the top.
- **History restore**: before a saved version is written, the top screen shows
  the save on the console now (size, files, and whether it changed since the
  last sync) beside the chosen version (date, size, files), and the card says
  whether the current save is backed up to the SD first. Only **A** (or
  tapping **Restore**) writes it; **B** or **Cancel** leaves the save alone.

Switching tabs slides the new tab's content in from the side of the
shoulder button you pressed (R from the right, L from the left); the header
and the action bar stay put, and the keys work from the first frame. A
dialog rises into place over a dimmed bottom screen (the Y menu grows out of
the row it is about); when it closes, it goes at once and the dimming fades
out while the screen underneath already responds.

Holding the D-pad repeats, and after a second Up / Down speed up. The
highlight glides between rows; when a list wraps around it jumps to the other
end and the rows fade in, and a page press with nowhere left to go nudges the
highlight. On the touch screen lists scroll like a phone's: drag them, flick
them (they coast and settle on a whole row, and bounce softly at the ends),
touch a coasting list to stop it. Dragging doesn't move the selection; the
next Up / Down picks the nearest row on screen, and A / Y scroll back to the
selection they act on. Dates read "Today 21:14", "Yesterday 21:14" or
"Oct 12 21:14" in the console's time zone (`utc_offset_minutes`); if the
console clock looks wrong (before 2024, or more than a day behind the server) they always
show the year instead.

## Safety when writing saves

- Every file of a downloaded bundle is checked against its SHA-256 before anything is written; a mismatch aborts the download.
- The current save is backed up to the SD card first (see `local_backups`). If it cannot be read or backed up completely, nothing is written.
- A History restore always asks first and says whether the current save will be backed up.
- A restored version counts as a change made on this console: the next sync sends it to the server (or reports a conflict if another device uploaded since), instead of downloading the server's copy back over it.
- *Sync all* never downloads over a save this console has never synced when the server's copy differs (for example one uploaded by an emulator first): it reports a conflict instead, like the single-save sync.
- Uploading marked saves that are in conflict (Sync All marks them) asks "Replace N server copies?" in red and names them; the server keeps the replaced copies in its history.
- If writing or committing the new save fails, the previous save is written back. To recover by hand, copy the files from the backup folder into the save with a save manager such as Checkpoint.
- A save with more than 256 files is refused instead of being uploaded incompletely.

## Limitations

- **Extra data (extdata) is not synced.** Only the title's save data archive (`ARCHIVE_USER_SAVEDATA`) is read and written. Games that keep part of their state in extdata (often StreetPass, downloaded content or photos) will only partly sync; back up extdata separately with a save manager such as Checkpoint.
- A save may have at most 256 files, and its compressed bundle must fit in 448 KB to upload; save downloads are limited to about 2 MB (catalog installs are streamed and have no such limit).
- File names inside a save are handled as ASCII; non-ASCII names are not supported.
- Cartridge titles are never included in *Sync all*; sync them one at a time.

## Controls

The same scheme on every screen (shared by all GameSync console clients):

| Button | Everywhere |
|---|---|
| D-pad Up / Down | Move one row |
| D-pad Left / Right | Page up / page down |
| L / R | Previous / next tab (Saves, Catalog, Settings; wraps around) |
| SELECT | Next sub-tab (All / 3DS / NDS on Saves, 3DS / NDS / DSi on Catalog) |
| A | Confirm / the highlighted row's main action |
| B | Cancel / back; closes dialogs (never starts anything) |
| START | Exit (asks first: START again or A exits, B stays) |

Everything on the touch screen can also be tapped: the action-bar buttons, the
sub-tab segments, dialog buttons and list rows. A button shows pressed as soon
as you touch it and acts when you lift your finger on it (slide off to cancel);
it also shows pressed while its key is held. A message closes when you lift
your finger anywhere on the screen. The touch that opens or closes a dialog
never reaches the screen underneath.

| Touch | Everywhere |
|---|---|
| Tap a row | Select it (on release) |
| Tap the selected row | Its A action in Settings, Catalog (asks first) and History (asks first) |
| Drag / flick a list | Scroll it; a touch stops a flick |
| Tap a sub-tab segment | Show it (same as cycling to it with SELECT) |
| Tap a button | The same as its key |

On a progress card that offers it, **B** or tapping **Cancel** (on release)
stops a request that only reads from the server; see *Progress* above.

### Saves

| Button | Action |
|---|---|
| A | Smart sync the selected save (or upload all marked titles) |
| X | Sync all saves |
| Y | Details: compare with the server, upload, download, history (restore an older version, after a confirmation), mark / unmark, search saves by name |
| B | Unmark all marked titles (also a tap on the header's `N marked` chip); with nothing marked, clear the search |
| ZR | Search saves by name (New 3DS) |
| SELECT | All / 3DS / NDS |
| Touch | Tap a row to select it; tap its mark box to mark / unmark it (on release, so a drag never toggles a mark) |

**Search.** Tap the magnifier at the right of the header, press **ZR** (New
3DS) or pick **Y** > *Search saves by name…*, and type part of a name (case
and accents don't matter; an empty search shows everything). The search
combines with All / 3DS / NDS and stays while you visit other tabs. While it
is active the header shows the query as a pill (tap it to edit, tap its `×`
to clear) and the count as "3 of 41". If nothing matches, the touch screen
offers *Clear search* and *Edit search* buttons, so an Old 3DS (no ZR) can
always change or clear it.

The action bar shows **A** *Sync* (*Resolve* when the selected save is in
conflict, *Upload N marked* when titles are marked), **X** *Sync all* and
**Y** *Details*. Tapping the selected row doesn't sync: use the A button.
Marking for a batch upload is on the touch screen (the mark box) or in the
**Y** menu. When both copies of a save changed, the Smart Sync card shows two
equal buttons under the two cards: **A** *Keep this console* (uploads; the
server keeps its copy in its history) on the left and **X** *Keep server*
(downloads; this console's copy is backed up to the SD first when Local
backups is on) on the right, with **B** *Cancel* below. While your finger is
on one, the top screen outlines the copy it keeps and fades the other; slide
off to cancel. A and X act as soon as they are pressed.

### Catalog

| Button | Action |
|---|---|
| A | Install the selected game (try again after an error) |
| X | Only games with RetroAchievements / all games |
| Y / ZR | Search (system keyboard; an empty search shows everything; ZR on a New 3DS) |
| B | Clear the search; hold B (or touch Cancel) to cancel a download |
| SELECT | 3DS / NDS / DSi, whichever the server has |
| Touch | Tap a game to select it, tap it again to install (after the prompt); the action bar has A *Install*, Y *Search* and X *RA only* (tinted while on) |

On the install prompt, **X** saves a 3DS cart as a `.3ds` file instead of
installing a CIA.

### Settings

The entries are grouped (Server, Saves & ROMs, App, Maintenance; a line
separates the groups) and the list scrolls; **Left** / **Right** jump to the
first / last entry. The action bar's A button names what A does on the
highlighted entry (*Edit*, *Choose*, *Turn on* / *Turn off* or *Run*); tapping
the highlighted entry does the same. The top screen shows the current values
and help for the highlighted entry ("Group · Entry").

| Entry | What A does |
|---|---|
| Server URL, Alternate URL | Edit (system keyboard); saved to `config.txt` right away, then the server is asked whether it answers: the entry shows *Answered · v…* or *No answer (saved anyway)* for the rest of the session (never saved). A URL must start with `http://` or `https://` (the keyboard says so and stays open); an empty Alternate URL turns it off |
| API key | Edit, typed hidden; saved, then checked with the cheapest request that needs the key: *Key accepted*, *Key rejected (401)* or *No answer (saved anyway)* |
| Verify TLS | Switch (`tls_verify`). Turning it off asks first (anyone on the network could read your API key and saves); turning it on doesn't |
| NDS ROM folder | Opens the folder picker (below) |
| Local backups | Pick Off, 1, 3, 5 or 10 copies (`local_backups`); the list opens on the current value, and a value set in `config.txt` that isn't in the list (e.g. 7) is shown as "7 copies (current)" so closing the list never changes it |
| Allow update checks | Switch (`update_check`). Off: *Check for updates now* only explains that update checks are off |
| Reduce motion | Switch (`reduce_motion`), applies at once |
| Rescan titles | Look for installed titles and DS saves again |
| Refresh catalog | Ask the server to rescan its ROM folder, throw the cached catalog away and download it again |
| Check for updates now | Download and install the latest GameSync CIA |

Switches flip and save with A (or a tap on the highlighted entry) without a
dialog, except turning Verify TLS off.

#### Folder picker (NDS ROM folder)

Browse the SD card's folders, FBI-style; only folders are listed, sorted by
name (names starting with `.` are hidden; a folder with more than 512
subfolders shows the first 512 and says so). It opens at the current NDS ROM
folder, or the nearest folder above it that exists, or `sdmc:/`.

| Button | Action |
|---|---|
| Up / Down, Left / Right | Move, page |
| A | Open the highlighted folder (also a tap on the highlighted row) |
| B | Up one level, from any folder (also above the one it opened at); at `sdmc:/` B cancels |
| X / START | Use the folder shown |
| Y | Type a path: an existing folder is opened (X still confirms it), a missing one can be used anyway; an empty path turns the NDS ROM folder off |
| Touch | The header's back arrow and path go up; its *Cancel* chip leaves without changing anything; the action bar has **X** *Use this folder*, **A** *Open* and **Y** *Type…* |

The top screen says what the folder shown holds, counted by the same rules as
the DS scan (`.nds` files at the top of the folder, a `.sav` with the same name
beside each or in `saves/`): e.g. "12 .nds ROMs · 10 saves (4 in saves/)", or
"No .nds ROMs in this folder". Below it, once the highlight rests on a folder,
what that folder holds. Counting reads a few entries per frame, so a big
folder never freezes the screen. Using a folder with no ROMs first looks one
level down and offers to open the subfolder with the most ROMs (**A**), use the
empty folder anyway (**X**) or go back (**B**). The chosen folder is saved at
once and the Settings help panel shows what it holds ("12 ROMs, 10 saves").

If the system keyboard can't start, a D-pad editor takes its place (for every
text field, including the searches): Left / Right move the cursor, Up / Down
change the character, Y inserts, X deletes, A confirms, B cancels. It checks
the text like the keyboard does, and shows the API key as dots except the
character being changed.

## Game catalog

The **Catalog** tab browses the server's ROM catalog and installs games
straight from the 3DS. The list is on the bottom screen, details of the
highlighted game on the top.

The catalog is cached on the SD card in `sdmc:/3ds/3dssync/cache/`, one
`catalog_<SYSTEM>.txt` per system (3DS, NDS, DSI): a tab-separated text file
with a header carrying the cache format version and the server's fingerprint
for that system, one row per game (only the fields the client uses), and an
`END` line with the row count, so a file cut short is ignored. On open the
client asks `GET /api/v1/roms/fingerprints` and downloads again only the
systems whose fingerprint changed (same strategy as the MiSTer client); the
rest come from the SD card. Search and the RetroAchievements filter then run
on the cached rows. If the server can't be reached the cached copy is shown
with an `Offline` tag. A server without the fingerprints route is browsed live
like before (paged from the server, which also does search and the RA
filter). *Settings > Refresh catalog* forces a full reload. The server's
`?extract=` support is cached with the rows: if the server operator enables
CIA conversion later, refresh the catalog to pick it up.

A gold `RA NN` badge marks games with an achievement set (an outlined `RA?`
when matched by name only); a green check marks games that are already
installed.

**3DS games** install as a CIA directly through the system's AM service — the
download is streamed into the install in 64 KB chunks, so nothing is staged on
the SD card and the game appears on the HOME Menu when it finishes. Requires
custom firmware (Luma3DS) like any CIA install.

- A `.cia` in the server's `n3ds` folder is installed as-is.
- A `.3ds` / `.cci` cart image (or a zip holding one) is converted on the
  server with `?extract=cia`. That conversion is an optional server tool: set
  `SYNC_ROM_3DS_CIA_COMMAND` (e.g. to `3dsconv`, see `server/README.md`).
  Big games take a few minutes to convert
  before the download starts; converted CIAs are cached on the server.
- If the server can't convert (HTTP 503), the client offers to save the raw
  `.3ds` to `sdmc:/roms/3ds/` instead; install it from there with GodMode9
  (*Build CIA from file*). Press **X** on the install prompt to pick that
  directly.

Titles installed from the catalog are remembered in
`sdmc:/3ds/3dssync/catalog_cia.txt` (rom id → title id) so the list can show
them as installed.

**DS / DSi games** are saved as `.nds` files in the TWiLight Menu++ layout:
`sdmc:/roms/nds/` (or the configured *NDS ROM Directory*) and
`sdmc:/roms/dsi/`. Zipped ROMs are unzipped by the server (`?extract=nds`).
Downloads go to a `.part` file that is renamed when complete, and the free
space is checked first. After a DS install the save list is rescanned so the
new game's save can be synced.

## Tests

The plain-C parts have host tests: the catalog's parsing and planning
(`source/catalog_data.c`), the History list parser (`source/history_parse.c`),
the springs (`source/gui_motion.c`), the `config.txt` reader/writer
(`source/config_file.c`), the date/size formatting (`source/ui_format.c`) and
the touch and list gestures (`source/ui_gesture.c`: tap and drag recognition,
the touch filter, momentum, rubber band) and the background-job helpers
(`source/ui_job.c`: the copied progress reports, when a card starts waiting,
the wait clock), plus the dialog entry transform in `source/gui_motion.c`:

```bash
sh tests/run_host_tests.sh          # needs a host gcc (Linux / WSL)
```
