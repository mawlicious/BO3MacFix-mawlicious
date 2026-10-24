# Menu memory investigation — 2026-10-24

Status: implemented navigation-conversion and read-buffer optimizations on a
separate branch, with temporary local game testing. The installed Steam launcher
and graphics configuration remain unchanged. BOIIIMac work remains paused.

## Baseline

Apple M4, 16 GB unified memory, macOS 27.2 (26B5112a), x86_64 game under Rosetta.
Process 10626 launched at 13:38:41 Europe/Berlin. The session stayed at the menu;
its log contains frontend loading but no `MapName: zm_zod` entry.

`vmmap -summary`, `vmmap`, and `footprint --swapped --wide` were collected from
the running process. The first footprint was approximately 6,235 MB, the later
one 6,239 MB. This short idle comparison does not demonstrate a growing leak,
and cannot rule out a leak during gameplay or after map unloads.

Main footprint categories, rounded as reported by `footprint`:

| Category | Charged memory | Attribution limit |
| --- | ---: | --- |
| App-Specific Tag 2 | 2,748 MB | Custom mappings; asset owners not established |
| IOAccelerator graphics | 1,452 MB | Driver-accounted graphics resources |
| Owned unmapped graphics | 273 MB | Additional graphics accounting |
| Ordinary malloc categories | 1,399 MB | Nano 725, medium 295, large 148, tiny 134, small 97 |
| Other categories | approximately 363 MB | Data segments, surfaces, Rosetta, page tables, etc. |

These are process footprint categories, including compressed/swapped charges.
The `Swapped` column is a subset and must not be added again. They are not all
simultaneously resident in physical RAM. GPU memory shares the machine's unified
memory; it is not a separate dedicated VRAM allowance.

The malloc zone table reports roughly 1.2 GB of allocated payload, nearly six
million nano allocations, and about 63 MB of dirty/swapped fragmentation across
malloc zones. The larger category total also includes allocator overhead and
other charged pages. This does not support a claim that changing the system
allocator alone would recover gigabytes.

## Logs and binary inspection

The current log and seven earlier logs repeatedly report the same DB allocation
figures for common, specialist and frontend packages. Those figures exceed the
measured process footprint when added. They cannot be used as per-package
physical RAM measurements without establishing what that engine counter counts.

REA 6.7.0 successfully inspected the Mach-O. No Hopper/Ghidra/IDA analysis
provider is configured on this machine, so this was binary inspection rather
than recovered source. LLVM disassembly located the DB accounting print site at
`0x1014cae88` and two mmap call sites at `0x10010a036` and `0x10010a601`.
The mmap wrapper includes fixed-address mapping and page bitmap management;
this alone does not identify the objects using App-Specific Tag 2.

Inspected executable SHA-256:
`932f0918fb9871bc625b69a587135a5c2976e9f9e6b7289cfadda8ab996e1cd0`.
Mach-O UUID: `3C8EC670-CCAC-3FB4-B303-50AC356E845F`.

## Existing optimization research

- A [first-hand report in Valve's Proton tracker](https://github.com/ValveSoftware/Proton/issues/864#issuecomment-1345146784)
  attributes rising RAM use to `OmniShadowTextureSize=2048`. It is an individual
  report, not a confirmed Mac fix. The user's value is already **256**, so its
  reported trigger does not match this session.
- [BO3Enhanced](https://github.com/shiversoftdev/BO3Enhanced) uses the Windows
  Store executable and explicitly supports Windows. Its faster executable is
  not a standalone allocation fix that can be recompiled into the Mac port.
  Its `VirtualPretend` hook in `dllmain.cpp` addresses specific anti-cheat call
  sites; it is not evidence for a general-purpose heap reduction.
- [GamersNexus's original settings benchmarks](https://gamersnexus.net/game-bench/2174-black-ops-3-graphics-optimization-guide-settings-benchmarked)
  show substantial texture/graphics memory demand on Windows. They establish
  that graphics settings matter, but are not a Mac menu baseline or proof this
  session has a leak.

No verified, source-backed drop-in fix for this Mac menu footprint was identified
in this investigation. This is not a claim that no such fix exists.

## Implemented optimizations

### Native navigation conversion

These changes affect first-time Workshop navigation conversion, not stock-map
rendering or the idle menu:

- Copy opaque arrays in one operation and retain one contiguous address mapping,
  instead of a mapping for every element. Interior pointers remain supported.
- Reuse the converted data allocation for the serialized output and release
  traversal-only tables before serialization. This avoids keeping a second
  complete output buffer alive unnecessarily.
- Sort pointer fixups once and use binary search for lookups. Duplicate fixups
  still fail validation; input fixups need not arrive sorted.

A synthetic 64 MiB opaque-array packfile was converted by the pre-change and
updated converter, compiled with `clang -O2` on this Mac:

| Measurement | Before | After |
| --- | ---: | ---: |
| Maximum resident set (`/usr/bin/time -l`) | 272,531,456 B | 136,052,736 B |
| Wall time (single run) | 0.51 s | 0.32 s |
| Output size | 67,109,200 B | 67,109,200 B |
| Output FNV-1a checksum | `6dbb03be2a3a9db8` | `6dbb03be2a3a9db8` |

The approximately 50% peak reduction includes benchmark input storage. It is
not a prediction of savings for every map or total gameplay RAM. The benchmark
checks every opaque payload byte. Additional sanitizer tests exercise interior
pointers, shuffled and duplicate fixups, malformed inputs and existing schemas.

Reproduce without game assets:

```sh
clang -O2 -Isource tests/benchmark_navigation.c source/havok_convert.c -o /tmp/bench-nav
/usr/bin/time -l /tmp/bench-nav 64
```

### Archive stream buffers on large-block volumes

The external game volume reports a 1 MiB preferred I/O block size. libc uses
that hint for default FILE buffers, even when the engine does its own seeks and
block reads. Newly opened, read-only regular files on volumes with a block hint
above 64 KiB now use a fully buffered 64 KiB stream. No file data, texture,
model, graphics setting, or cache budget is changed.

The interposer restricts this policy to the game's known `fdopen` caller at
`base + 0x10be0f` (return address `+0x10be14`). The five-byte call signature must
match before enabling it. Other callers, writable streams, pipes and ordinary
small-block filesystems retain their original behavior. Set
`BO3MACFIX_MEMORY_IO=0` before launch to disable it for comparisons.

Fresh-menu heap samples (control PID 33990, optimized PID 39433) showed:

| Allocation size | Control count | Optimized count |
| --- | ---: | ---: |
| 1 MiB | 69 | 2 |
| 64 KiB | 555 | 625 |
| All allocated heap bytes | 1,245,352,544 | 1,181,067,936 |

The 67 replaced buffers imply about 62.8 MiB less buffer storage. Other heap
objects vary between samples. Total process footprints were 6,183 and 6,177 MB;
these noisy snapshots do **not** establish a comparable total-footprint saving.
The allocator can retain freed pages, and most game memory has other owners.

A deterministic seek/read benchmark on the real `base.xpak` returned the same
sample checksum (`107c913b0f0fa470`) for 512 MiB of reads with both buffer sizes.
Alternating runs took 1.885/1.509 s with 1 MiB buffers and 0.122/0.123 s with
64 KiB buffers. This is a warm-cache synthetic access pattern, not a general
loading-time or FPS guarantee. Reproduce locally:

```sh
clang -O2 tests/benchmark_io.c -o /tmp/bench-io
/tmp/bench-io /path/to/base.xpak 1048576
/tmp/bench-io /path/to/base.xpak 65536
```

Stream sanitizer tests check bytes, seeks, EOF and errno preservation, and
exclude writable/nonregular streams. Live game testing verifies that the
interposer actually replaces the large buffers; policy-only unit tests would
not establish that.

A temporary injected build reached the online menu, Zombies menu and a fully
loaded Shadows of Evil round-one HUD. The map rendered and the pause menu
worked. This is a loading/rendering smoke test, not a long-session or
multiplayer performance comparison. The complete sanitizer and installer
suite and packaged clean install/uninstall tests also passed locally.

## Remaining large allocations

Sampled allocation stacks and binary RTTI attribute many of the tiny heap
objects to `DX11Bytecode::SMInsn` shader parsing. Later shader compilation paths
still reference the parsed representation. Freeing it without proving its
lifetime could break shaders or cause compilation stutters, so this PR does
not discard it. Allocation traffic is cumulative and is not retained memory.

The custom mappings and graphics resources remain the largest unresolved
owners. There is no demonstrated fix here for Footlight stretching, freezes,
or the complete 6–10 GB footprint. No quality or cache-budget reductions were
used to obtain the savings above.

Raw snapshots and research extracts are saved locally under
`~/Library/Logs/BO3MacFix-mawlicious/memory-20261024-menu-mawlicious/` and temporary
benchmark files under `/tmp/bo3-memory-mawlicious/` during investigation.
No live memory contents or game binary are included in this document.
