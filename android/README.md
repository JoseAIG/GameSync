# GameSync — Android Client

Android app for syncing emulator save files with the GameSync server. Built with Kotlin and Jetpack Compose.

**Minimum Android version:** Android 10 (API 29)

## Requirements

- [Android Studio](https://developer.android.com/studio) (Hedgehog or newer recommended)
- Android SDK 34
- Java 8+

## Build

Open the `android/` folder in Android Studio and let Gradle sync, then:

```
Build → Build App Bundle(s) / APK(s) → Build APK(s)
```

Or from the command line:

```bash
cd android
./gradlew assembleDebug     # debug APK → app/build/outputs/apk/debug/
./gradlew assembleRelease   # release APK (requires signing config)
```

### Output

`app-debug.apk` — install via ADB or by sideloading:

```bash
adb install app/build/outputs/apk/debug/app-debug.apk
```

## Permissions

On Android 11+, the app requires **All Files Access** (`MANAGE_EXTERNAL_STORAGE`) to read save files from emulator directories. A permission prompt is shown on first launch.

## Supported Emulators

| Emulator | System(s) |
|---|---|
| RetroArch | Multi-system |
| Dolphin | GameCube, Wii |
| PPSSPP | PSP |
| DuckStation | PS1 |
| AetherSX2 / NetherSX2 | PS2 |
| melonDS | NDS |
| DraStic | NDS |
| mGBA | GBA |
| Cemu | Wii U (`mlc01/usr/save/00050000/`) |

## Screens

Five top-level tabs — **Saves**, **Catalog**, **Installed**, **Downloads**,
**Settings** — in the same dark GameSync palette and layout as the console
clients: a header with the tab strip and a server status dot (green = the
server answered, red = it did not), a sub-tab chip row (system filter, sync
status, RetroAchievements) at the top of each list, list rows with status
pills, and a footer of controller button hints. The footer (and the L1 / R1 /
SELECT glyphs) show whenever a gamepad is attached or the screen is in
landscape; on a phone in portrait the app is driven by touch as before (tap a
tab, tap a row, the toolbar icons).

The header and footer stay in place while screens change; only the content
moves. The teal pill behind the active tab slides to the next one (on a wrap
from Settings to Saves it fades across instead), and the strip scrolls so the
active tab is never cut off on a narrow phone. While downloads run or wait,
the Downloads tab shows how many.

The teal selection bar marks the row the controller acts on. It shows while
you use a controller or keyboard, and always on a device with built-in
controls or a gamepad attached; on a phone driven by touch there is no
cursor, and a row lifts slightly while your finger is on it. On a phone
without a gamepad, the first key you press after touching the screen only
shows the cursor (it doesn't sync, download or open anything). The list
scrolls only when the cursor comes near its top or bottom edge, and holding
the D-pad glides instead of stepping. After flinging a list by touch, the
next Up / Down picks up from a row that is on screen rather than scrolling
back. Pressing past the first or last row makes the list give a little in
the direction you pressed (with a short tick when touch feedback is on)
instead of doing nothing; holding the stick there gives one tick, not a
stream. When a held Left / Right starts jumping by first letter, the letter
it reached shows in a square in the middle of the list and fades half a
second after you let go. Search opens by sliding down from the header. A
busy spinner always takes the place of the button it belongs to, so nothing
moves under your finger, and rows slide to their new place when the list
changes. Changing the system or status filter (or RetroAchievements only)
slides the new list in from the side the filter moved to. There are no
divider lines under the header or over the hints: a soft shadow appears
there only while content is scrolled under them. Status pills carry an icon
next to their word (a tick for *Synced*, a phone for *Never synced*, a cloud
for *Server only*, arrows for *Local / Server newer*, a warning sign for
*Conflict*).

Dialogs grow in slightly from the centre and shrink back the same way when
they close; a second press while one is closing does nothing.

Messages appear in one place at the bottom of the screen, above the button
hints. A new message replaces the one showing instead of waiting behind it,
and it stays up when you leave the screen, so the save detail screen goes back
as soon as an action finishes. Errors stay up longer and have a close button.
"Server URL not configured" offers a **Settings** shortcut and a failed ROM
download offers **Retry**; both are touch shortcuts for things the controller
reaches through the tabs. Successes and errors also give a short vibration
when the system's touch feedback is on.

Statuses are worked out, never assumed. A row first shows a neutral
*Checking…* pill and then its real status, computed off the main thread from
the same hash sync sends; *Never synced* means this device has no record of a
sync yet (even if the server has a copy), *Unknown* that the save couldn't be
read. With an L2 / R2 status filter on, rows appear as their status is known
("Checking N saves…" above the list) instead of vanishing later, and the
cursor stays on the game it was on even when rows move. Saves keeps
conditions that last in banners at the top (at most two at once): no server
set, server unreachable (statuses are then from this device only), saves with
a conflict (tap, or L2 / R2, to filter them; the Saves tab also shows a red
dot), and an emulator save folder that doesn't exist (tap to fix it on the
emulator screen). Tapping the header's status dot checks the server again.
While a save scan runs, an empty list says *Scanning emulator folders…* and a
filled one shows a thin bar under the chips; pulling down rescans and
re-checks the server, even on an empty list. While a save syncs its pill
reads *Syncing…*; pressing A meanwhile says to wait.

Moving between tabs slides the content the way L1 / R1 point; the save detail
and emulator screens slide in from the right and leave the way they came. With the system's *Remove
animations* accessibility setting on, every slide becomes a short fade and
list scrolling jumps instead of gliding. On Android 14 and later the system
*Contrast* setting (medium or high) brightens secondary text and outlines the
status pills and banners. Rotating the screen keeps the selected row, the
search and the filters.

## Controls

The same scheme on every screen (shared by all GameSync console clients):

| Button | Everywhere |
|---|---|
| D-pad / left stick Up / Down | Move one row (hold repeats) |
| D-pad Left / Right | Page up / page down (hold to speed up, then jump by first letter, shown in the middle of the list) |
| L1 / R1 | Previous / next tab (Saves, Catalog, Installed, Downloads, Settings; wraps around) |
| SELECT | Next sub-tab: the system filter on Saves, Catalog and Installed |
| A | Confirm / the highlighted row's main action |
| B | Cancel / back: closes dialogs and the search (never starts anything, never leaves the app from a tab) |
| START | Exit (asks first: START again or A exits, B stays) |

The touch / system Back gesture behaves as before.

Holding A doesn't repeat it: one press is one action, so a held A can't open a
dialog and confirm it too. Inside a dialog the D-pad never moves onto a button;
each choice answers only to the button drawn on it.

### Saves

| Button | Action |
|---|---|
| A | Smart sync the selected save (upload or download, whichever it needs); a conflict, or a Saturn save, opens its detail screen |
| X | Sync all: exactly the local saves the list shows, after the system filter, search and status filter. It first shows what it will do (see below); with no server set it offers Settings instead |
| Y | Details: compare with the server, force upload / download, download the ROM, normalize the name |
| L2 / R2 | Step the sync-status filter (all / synced / local newer / conflict / ...) |
| SELECT | Next system |
| Touch | Tap a row for details; the search and web-library icons stay in the header |

On the detail screen D-pad moves between the actions, A runs one, Y shows or
hides the technical details (title id, file, hashes), B goes back. The screen
leads with the game, its system and status, then *This device* (modified —
the newest file of the save, the time sync sends —, size, last synced) and
*Server* (saved at, from which device, size and file count), with a *Newer*
mark on the later side. After a smart sync finds a conflict the screen opens
with both copies side by side and the cursor on the Force row of the newer
one. *Smart Sync* is the main action; *Force Upload* and *Force Download* sit
under *Override* and both ask first: Force Download names both copies and
says that a copy of this device's save is kept (RetroArch users are reminded
to close RetroArch, whose auto-loaded save state can undo it).

**Sync all shows its plan first.** X opens a dialog that says *Checking with
the server…* and then what the sync will do: how many saves go up, how many
server copies will replace saves on this device (a copy of each is kept),
how many are new from the server, which conflicts are left alone and how
many are up to date. Nothing has moved yet: A *Sync* runs it, B cancels, Y
lists every game by what happens to it. A save that is much smaller than
the server's copy (under half its size) or has fewer files looks like a new
game or an old backup about to overwrite your progress everywhere; such
saves are listed under *Smaller than the server copy — new game or old
backup?* and are **skipped unless you include them** — move to one with the
D-pad and press X (or tap it) to switch it between *Skip* and *Include*. A
always means *Sync* here (it reads *Done* when nothing is left to do). Only
the first 20 uploads are size-checked; the dialog says how many weren't. If
you wait on the dialog, each save is checked again before it moves, and one
that changed in the meantime is skipped and named in the result. Auto sync
never uploads such a save on its own: it leaves it and tells you the next
time you open the app. A smart sync (A) of such a save doesn't upload it
either and says so; use *Force Upload* on its detail screen if it is the
save you want.

Every download that replaces a save — smart sync, Sync all, Force Download —
first copies the current save to the app's private storage
(`<app files dir>/local_backups/<title>/<date-time>/`, the newest five per
game, never in cloud backups). If that copy can't be made the save is not
overwritten. The result offers *Undo*, and the detail screen gets a *Restore
previous copy (<when>)* action for the same thing with the controller. A
restored save is never downloaded over again by a sync (smart sync, Sync all or
auto sync): it shows as a conflict until you choose *Force Upload* (keep it and
send it to the server) or *Force Download* (take the server's copy after all).

### Catalog

| Button | Action |
|---|---|
| A | Download the selected game (asks first, showing the folder it goes to and the free space there; a Wii U game queues its update and DLC too) |
| X | Only games with RetroAchievements / all games |
| Y | Search |
| B | Clear and close the search |
| SELECT | Next system |

### Installed

| Button | Action |
|---|---|
| A | The game's actions: A sync its saves, X delete it (asks again — it can't be undone; a save kept next to the ROM is never deleted with it), B cancel |
| X | Rescan the ROM folders |
| Y | Search |
| B | Clear and close the search |
| SELECT | Next system |

### Downloads

| Button | Action |
|---|---|
| A | Pause / resume / retry the selected download |
| B | Pause the running download |
| X | Clear finished downloads |
| Y | Cancel (running) or remove (finished) the selected download, after asking |

A line above the list counts active and finished downloads; the header's
*Clear finished* button is always there and greyed out while there is
nothing to clear. Clearing (X or the button) offers *Undo*: it only removes
list entries, never files. A row's touch cancel asks first, like Y; × removes
a finished row from the list. Progress bars move smoothly between updates.

When the catalog can't be loaded the tab says which server it tried and
reloads by itself once the server answers again (or when you come back to
the tab); *Retry* is a touch shortcut. Queuing a game confirms with
*Queued <name>* and a *View* shortcut to Downloads. START's exit dialog warns
when a save sync is still running.

### Settings

D-pad moves between the entries, A activates the highlighted one. Every
control saves the moment it changes; there is no Save button. A short
*Saved* appears next to a control once its change is stored. The server URL
and API key are saved when you press Done on the keyboard, when you move
off the field, or when you leave the tab; a URL that doesn't start with
`http://` or `https://` is flagged under the field and not saved. The page
is grouped in cards, most-used first:

| Card | What it holds |
|---|---|
| Server | Server URL, API key, Test connection (it saves what you typed, then shows the result on the same line) |
| Library | Rescan saves (reads *Rescanning…* while a scan runs); Refresh catalog: ask the server to rescan its ROM folder (`GET /api/v1/roms/scan`; a server that refuses with 403/404/405 is fine), throw the cached catalog away and download it again |
| Folders | Emudeck folder, ROM directory, per-system folder overrides (Detect, change, reset, add a system) |
| Save formats | Saturn download format, Sega CD save format, PS2 emulator |
| Emulator configuration | Per-emulator save folders (its own screen; B goes back). An override that points to a folder that doesn't exist shows a warning above *Change*, which then becomes the highlighted button; the warning stays until the folder exists or the override is cleared |
| Auto sync | The switch and its interval. A warning under the switch stays visible: auto sync can replace a save while an emulator is running, and the emulator may then overwrite it. Turning it on asks first (*Turn on* / *Keep off*); it is off by default |
| Diagnostics | Collapsed until opened: Test ROM scan, Diagnose RetroArch paths, Prepare ROM folders, and what they found |
| About | App version and console ID |

The focused control always has a teal ring in its own shape.

### Folder picker

Browse opens a folder picker. The D-pad moves between folders and buttons,
A opens the highlighted folder (or presses the highlighted button), **X**
goes up one level and B cancels. Under the path a line says what GameSync
would find in that folder, e.g. *Contains: 12 ROMs (.3ds), 3 saves (.srm)*,
system folders such as `psx` or `GBA`, or an EmuDeck layout. Picking an
emulator's save folder runs that emulator's own check instead (Azahar:
*Found sdmc/Nintendo 3DS/…/title (14 titles)*). When nothing there is
recognisable the line turns amber and *Select* asks before using the
folder. An Azahar folder without its 3DS save tree yet keeps a warning on
the Emulators screen, and no 3DS save is downloaded into it until Azahar
has created that tree (start a game in Azahar once). The list slides the way you went, and going up returns to where
you were in the parent folder. With a keyboard or remote on a phone, the
first key only shows which folder is highlighted; a touch hides it again.
On a short screen (a handheld in landscape, or large text) the path and
contents line keep to one line each and the button hints are left out, so
the folder list keeps its room.

### Pickers on the save detail screen

*Normalize* (choose the canonical name) and the Saturn archive picker keep
v055's controls: Up / Down move the cursor, A on a row chooses that name
(Normalize) or includes / leaves out that archive (Saturn), and A on the
*Apply* / *Use selection* row after the last one applies; B cancels. By
touch, tap a row to choose or toggle it and use the Apply / Use selection
button. The cursor starts on the current choice.

### TalkBack

List rows are read as one sentence with their status and, when it is the
controller cursor, "selected"; the tap action is named ("Download",
"Manage", "Open details"). Footer hints read as "A: Sync", dialog buttons by
their label, tappable banners by what they do ("Review conflicts", "Fix
emulator folder"), and download progress bars report their percentage. The
letter of an alphabet jump is announced as it changes, and status icons stay
silent (the word beside them is read).

## Game catalog cache

The Catalog tab keeps the server's ROM catalog on the device in
`<app files dir>/catalog_cache.json` (one JSON file: a format version, then per
system the server's fingerprint and that system's rows), using the same
strategy as the MiSTer client (`shared/catalog_cache.py`):

- On open the app asks `GET /api/v1/roms/fingerprints` and downloads again
  only the systems whose fingerprint changed (`GET /api/v1/roms?system=…`,
  paged 2000 rows at a time); systems the server no longer lists are dropped.
  The file is written to a temp file and renamed, so an interrupted write
  never leaves half a catalog.
- If the server can't be reached, the cached catalog is shown with an
  *Offline — cached catalog* notice.
- A server too old to publish fingerprints (404/405) is fetched whole, live,
  and nothing is cached.
- Search and the RetroAchievements filter run locally over the rows.
- The save scan and the save detail screen reuse the same cache to match
  saves to ROMs instead of downloading the catalog again (until the cache is
  first filled by opening the Catalog tab, the save scan asks the server for
  just the ROMs that have saves, as before).
- *Settings > Refresh catalog* forces a full reload. Bump
  `CatalogCache.VERSION` when the meaning of a stored row changes so old
  caches are refetched.

The cache logic is plain Kotlin (`catalog/CatalogCache.kt`) with JVM unit
tests in `app/src/test/.../catalog/CatalogCacheTest.kt`.

## Configuration

Open the app and go to **Settings** to enter your server URL and API key
(each is saved when you press Done or move off the field):

```
Server URL:  http://192.168.1.100:8000
API Key:     your-secret-key
```

Settings are stored on-device using DataStore and persist across launches.
