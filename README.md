# BO3MacFix-mawlicious

Fixes Black Ops 3 on Mac so custom maps from the Steam Workshop can be played and online matches can be played
with people running the Windows version of the game.

Wine-free modification maintained by mawlicious. Based on [BO3MacFix](https://github.com/InvoxiPlayGames/BO3MacFix) by Emma / InvoxiPlayGames and Anthony / serious.

Upstream source snapshot: `985f0c2` (existing local checkout). Original licensing is retained in `LICENSE.txt`.

### Any issues you run into with custom maps or workshop mods should be reported here, and *not* to the mod's author!

## Install this version

1. Download and extract the compiled archive from [Releases](https://github.com/mawlicious/BO3MacFix-mawlicious/releases).
2. Quit Black Ops III. Copy the complete `BO3MacFix-mawlicious` folder beside `CoDBlkOps3.app` in the game's Steam folder.
3. Open that folder and double-click `Install.command`.
4. Launch the game normally through Steam.

This is a complete standalone distribution. You do not need the original
BO3MacFix, Wine, Python, Xcode, or another download. Apple Silicon Macs need
Rosetta because the game and mod use Intel machine code. If macOS blocks the
installer, use Finder's **Open** action and approve it in Privacy & Security.

The window title and top-right in-game label show `BO3MacFix-mawlicious`.
Each run saves a separate log in `BO3MacFix-mawlicious/logs`.

Memory optimizations reduce native navigation conversion scratch storage and
cap oversized read-only asset stream buffers on large-block volumes. They do
not lower graphics quality. [Measurements and limitations](docs/memory-investigation.md).

This version also repairs four uninitialized event-flag comparisons in the
inspected One Barrier Roulette V2 script. The repair is restricted to that exact
script revision; Workshop updates with different bytes are left intact.
[Details and remaining map issues](docs/roulette-callbacks.md).

To uninstall, double-click `Uninstall.command` while the game is closed. This
restores the launcher backed up before the first install; if the original
BO3MacFix's stock launcher backup is present, that is used as the baseline.
Logs and mod files are retained. Reinstalling preserves the baseline backup.
When updating, retain your `logs`, `Physics`, and optional `natives` folders.

The required upstream zone and campaign navigation files are bundled in this
repository and release. [ASSETS.md](ASSETS.md) records their origin and checksums.

## Build and test

On macOS with Xcode command-line tools and Rosetta (Apple Silicon):

```sh
make
make -C launcher
make -C installer
bash scripts/test.sh
bash scripts/package.sh
```

The bundled upstream Dobby static library is used to build the x86_64 mod.
`c2mpatch/` retains the original Windows converter patch source for history;
it is not built or included in the Wine-free release.

## Credits

- [Alyssa / alltheamps](https://github.com/shiversoftdev) - guidance, help, t7patch and the cod2map64 patch DLL.
- [The Dobby project](https://github.com/jmpews/Dobby) - code hooking on macOS. (used under the MIT license)

## What does it do? (Nerd stuff)

### Custom Maps

There are two big issues with Steam Workshop map loading in the current (v98, from 2019) Mac build of BO3:

* Texture (.png) conversion crashes the game when hovering or selecting a workshop map
* Havok navmesh/navvolume files are a different format across PC and Mac

This build converts Windows Havok navigation packfiles directly inside the Mac game process.
It no longer launches Wine or the Windows modding SDK. Converted maps are cached in
`BO3MacFix-mawlicious/Physics`; legacy caches are regenerated once; only outputs with a matching native completion receipt are reused. The converter supports the captured
Havok 2014.2 navigation class schemas and reports unsupported data instead of guessing its layout.
The game and injected mod remain Intel binaries and use Rosetta on Apple Silicon.

A campaign mission with broken navigation meshes is also fixed in the same way.

### Online Play

A macOS update updated the version of libcurl that was included with the OS, which makes online login fail
in Black Ops 3 due to improper usage of the cURL library that's technically "undefined behaviour". This
is fixed here by removing one of the HTTP headers the game manually sets and letting cURL do its thing.

Version numbers are spoofed when signing in to match the Windows version's to enable matchmaking between the
two, along with using updated scripts and assets from the Windows version.

The launcher saves a separate log for each run in `BO3MacFix-mawlicious/logs`, including game output,
network-password enabled/disabled status, and local friends-only configuration. Logs can contain
Steam IDs, player names, and other game diagnostics; review them before sharing. Friends-lobby behavior is unchanged from the upstream snapshot; investigation is deferred.

Local validation: `python3 tests/test_havok_schema.py`, `python3 tests/test_launcher.py`, and the
C tests under `tests/`. `tests/convert_clump.c` optionally exercises captured navigation input;
proprietary game data is not included in this repository.
