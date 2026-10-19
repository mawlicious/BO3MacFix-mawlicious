# Native navigation conversion

This local build removes Wine, cod2map64.exe, and the patched Windows SDK from the
runtime conversion path. It does not port the closed-source game to ARM64:
the Mac game and injected mod run as x86_64, using Rosetta on Apple Silicon.

## Why a converter was needed

The map data uses Havok 2014.2 version-11 packfiles. Windows uses layout
`8,1,0,1`; Mac uses `8,1,1,1`. The different reuse of superclass tail padding
changes member offsets, object sizes, array strides, and pointer fixups.
Changing only the header cannot produce usable Mac data.

Reviewed public implementations: [HavokLib](https://github.com/PredatorCZ/HavokLib),
[Spike's class generator](https://github.com/PredatorCZ/Spike/blob/master/classgen/classgen.py),
[soulstruct-havok](https://github.com/Grimrukh/soulstruct-havok), and
[hkxpack](https://github.com/Dexesttp/hkxpack). None supplied a ready converter
for this exact BO3 navigation schema. No code from these projects was copied.
[REA](https://github.com/morluto/rea) was tried for binary inspection; full-game
Ghidra analysis was stopped without useful decompilation. Runtime class reflection
provided the required class names, members, and Mac offsets instead.

## Implementation and limits

`source/havok_convert.c` walks serialized objects and reconstructs them using
paired layouts in `source/havok_schema.h`. It regenerates local/global/virtual
fixups and preserves class signatures. Native object vtables and transient fields
are left for the Havok loader to initialize. The schema generator verifies every
computed Mac size and member offset against captured runtime metadata.

The 55-class schema covers captured navigation meshes and volumes, including
their mediators, trees, streaming sets, and clearance caches. Unknown classes,
unsupported layouts, malformed references, and external imports fail explicitly.
Additional maps may expose classes outside this schema; their data must be
captured and modeled before support can be claimed.

`source/nav_convert.c` writes temporary files and commits the mesh last, after
successful conversion of every requested buffer. Invalid or empty mesh input
cannot create a successful cache entry. Conversion failures remove temporary
files and preserve previous cache files.

## Verification

- Synthetic object graphs cover superclass offsets, changed array strides,
  cycles, shared pointers, aliases, malformed roots, and truncated input.
- Converter tests and captured mesh/volume conversion run under ASan and UBSan.
- Twenty captured navigation buffers converted successfully.
- Ten Shadows of Evil buffers were compared semantically against output from
  the original converter: class identities, scalar values, arrays, strings,
  and pointer relationships agree; padding and object order can differ.
- Optional `python3 tests/test_nav_cache.py /path/to/capture.clump` checks
  complete conversion and early/late failure cleanup using local game data.
- One Barrier Roulette V2 loaded into gameplay after its old cache was moved
  aside. The installed mod generated a fresh 114,304-byte navigation mesh, and
  its session log confirmed native conversion completion. No Wine/cod2map
  process was running during this test. On Cmd-Q after gameplay, the game
  unloaded its assets and then aborted during Steam-server shutdown with
  `std::system_error: mutex lock failed: Invalid argument`. The cause of this
  shutdown-only error is not established; it was preserved in the session log.
- Launcher integration tests check separate session files, stdout/stderr capture,
  argument forwarding, password-option removal, and child exit status.

Proprietary game captures and SDK files are not included in this repository.

## Installation and logging

The release is a standalone folder with the native mod, launcher, installer,
Install/Uninstall commands, and bundled upstream zone/campaign navigation files.
It does not require an existing mod or a Python runtime. Put the folder beside
CoDBlkOps3.app and run Install.command. The installer checks immutable zone
checksums before changing the launcher and retains a baseline backup across
reinstalls. Launcher replacement is staged and committed by an atomic rename.
An installation lock serializes install/uninstall operations. No Wine files are
shipped. Asset provenance and checksums are recorded in ASSETS.md.

Each launcher run writes `BO3MacFix-mawlicious/logs/session-*.log`. Logs include
game output and may contain player identities and private diagnostics. The
game may still overwrite its own console log. Friends-lobby changes are deferred;
Steam friendship behavior is unchanged from the upstream snapshot.

Cache reuse requires a versioned completion receipt with SHA-256 digests of the
mesh and, when required, volume, plus a fingerprint of all source buffer lengths
and bytes. Workshop updates therefore invalidate old conversions. It is committed after both output files and is
invalidated before either existing output is replaced. Legacy caches without a
receipt, missing volumes, and modified/truncated outputs are regenerated.
The release includes the standalone native installer; the historical upgrade installer has been replaced.

The native installer strips `com.apple.quarantine` from the bundled mod/launcher
and the staged replacement launcher before activation. Other extended attributes
are preserved.

When an update removes its navigation volume, committing the new mesh also
removes the obsolete volume before publishing the receipt. Mesh-only receipts
reject caches where a volume file has reappeared.
