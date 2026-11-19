# Performance: Rosetta AOT and job-queue spinning — 2026-11-19

Measured on an Apple M4 MacBook Air (fanless, 4 performance + 6 efficiency
cores, 16 GB), macOS 27.3, game on an exFAT USB SSD, 1080p 60 Hz external
display, the player's own low settings (`ResolutionPercent 60`). The game and
mod are x86_64 and run under Rosetta 2.

## Findings

### 1. The mod made Rosetta abandon its ahead-of-time translation

Rosetta normally runs `CoDBlkOps3_Exe` from an ahead-of-time (AOT)
translation (`/var/db/oah/.../CoDBlkOps3_Exe.aot`). Rosetta drops that
translation for the **whole image** as soon as any `__TEXT` page of the game
is made writable at runtime, even when nothing is written. Every Dobby hook
or byte patch does that, so with BO3MacFix (this fork and upstream) the
entire game ran on Rosetta's JIT.

Evidence, using minimal probe dylibs injected the same way as the mod:

| Probe | `CoDBlkOps3_Exe.aot` mapped | Rosetta JIT dirty |
| --- | --- | --- |
| no mod / no-op dylib | yes | ~1 MB |
| one byte rewritten with its own value (DobbyCodePatch) | no | 9.8 MB after 20 s |
| one page `mach_vm_protect` RW then RX, no write | no | 21.7 MB |
| BO3MacFix 1.2-beta3 (installed release) | no | 34 MB |

Remapping a modified copy of a page over `__TEXT` keeps the AOT mapped, but
Rosetta then keeps executing the stale translation (verified in a small
x86_64 program), so it is not a usable patching method.

At the same 60 FPS title screen, the render thread used 35% CPU with AOT
versus 61% under JIT.

### 2. Job-queue workers busy-spun

Aspyr's `WaitForSingleObject` emulation for events (`0x111242`) never
blocks for timeouts below 2 ms (`cmpl $2,%r15d; jb` at `0x1112ae`). It locks
the event mutex, checks the flag, unlocks and returns `WAIT_TIMEOUT`. The two
"JQ Worker" threads idle in `WaitForSingleObject(event, 1)` loops
(`0x11a7556`). On Windows that sleeps; on the Mac it spun through a contended
mutex at ~70–84% CPU per worker, with ~20% kernel time, even in menus.

## Changes

* **Static executable.** The launcher builds `CoDBlkOps3_Exe_mawlicious`
  next to the untouched Steam executable, with every mod patch applied on
  disk, ad hoc signs it and launches it. Rosetta translates it ahead of time
  once and keeps that translation.
  * `assets/static-patch.txt` (shipped as `BO3MacFix-mawlicious/static-patch.txt`)
    lists written bytes and *copy-from-original* directives. Original game
    instructions are never shipped; they are copied from the player's own
    executable, which must match a known SHA-256.
  * Each inline hook site becomes `jmp` to a stub in zero padding at the end
    of `__TEXT`: `cmp qword [slot],0; je trampoline; jmp [slot]`. The slot is
    an 8-byte pointer in zero-filled slack at the end of `__DATA`, so
    installing a hook at runtime writes data, not code. The trampoline holds
    the relocated original instructions.
  * In the dylib, `DobbyHook`/`DobbyCodePatch` go through
    `source/patch_layer.c`. Under the static executable it verifies baked
    patches, fills slots, and checks signatures against the original bytes
    (read from `CoDBlkOps3_Exe`). Anything not baked falls back to runtime
    patching and logs that the AOT translation will be lost.
  * If the game executable changes (for example after a Steam update),
    the patch is missing or damaged, or signing fails, the launcher uses the
    original executable with runtime patching. `BO3MACFIX_STATIC=0` forces
    that mode.
  * Regenerate after changing hooks: run once with
    `BO3MACFIX_RECORD_PATCHES=<file>` on the original executable, then run
    `tools/static_patch.py <CoDBlkOps3_Exe> <file>` (needs Xcode).
* **Job-queue wait fix.** The worker's 1 ms wait becomes a 2 ms wait,
  which Aspyr's code implements as a real `pthread_cond_timedwait`. `SetEvent`
  broadcasts, so queued work still wakes workers immediately.
  `BO3MACFIX_EVENTWAIT=0` disables it under the original executable; the
  static executable has it baked in.
  `BO3MACFIX_MAINWAIT=1` applies the same change to the main thread's
  `renderCompletedEvent` wait (`0xd05c07`) as an experiment. It is off by
  default because that wait is on the frame's critical path, and turning it
  on uses runtime patching.
* **Frame telemetry.** The session log gets a line every 30 s with FPS,
  p50/p95/p99/max frame time, counts of frames over 33/50/100/250 ms, time
  blocked in `nextDrawable`, process CPU and memory footprint. Frames of
  250 ms or longer are logged as they happen, so freezes can be matched to
  game output. `BO3MACFIX_PERFLOG=0` disables it; `BO3MACFIX_PERFLOG_INTERVAL`
  sets the interval in seconds.

### 3. Device-context lock spun 100,000 times before sleeping

Aspyr's D3D11 device-context lock (`0x4e580`, 211 call sites) retries
`pthread_mutex_trylock` with no pause up to a per-context limit before it
falls back to `pthread_mutex_lock`. The limit is set to 100,000 at
`0x2aa63`. Under Rosetta a contending thread burns its core, and the lock's
cache line, for milliseconds. The limit is now 0, so the lock is a plain
blocking `pthread_mutex_lock` with the same locking semantics.
`BO3MACFIX_LOCKSPIN=1` keeps Aspyr's spinning under the original executable.

An alternating 8-run test in Shadows of Evil (windowed, spawn, mean of two
10 s windows per run) measured:

| Lock spin limit | Run 1 | Run 2 |
| --- | --- | --- |
| 100,000 (Aspyr) | 81.9 FPS | 88.4 FPS |
| 2,000 | 89.5 FPS | 89.0 FPS |
| 0 (shipped) | 90.3 FPS | 93.2 FPS |

Run-to-run variance between sessions is about ±5%, so only differences
that repeat across alternating runs are treated as real.

## Results

Through the installed launcher, fullscreen, Shadows of Evil solo, standing at
spawn, 3 × 10 s windows after the intro. "Old" is the same build with
`BO3MACFIX_STATIC=0 BO3MACFIX_EVENTWAIT=0`, matching the previous release.

| | Old (JIT + spinning) | New |
| --- | --- | --- |
| FPS | 71.8 / 70.4 / 69.9 | 77.6 / 74.0 / 73.5 |
| p95 frame time | 16.1–17.4 ms | 15.8–17.0 ms |
| Process CPU | 411% | 347% |
| JQ workers | 84% each | 53% each (real work) |
| Render thread | 90.5% | 91.0% |

Main menu (uncapped, windowed): 112 FPS at ~400% CPU before, 120 FPS at
153% CPU after. Idle title screen: ~360% CPU before, ~73% after. Lower CPU
use leaves more power and thermal headroom on fanless Macs; long-session
thermal behaviour was not measured.

The Metal performance HUD (`MTL_HUD_ENABLED=1`) loads x86 interposers that
add substantial render-thread cost under Rosetta and change presentation, so
it was not used for these numbers.

## Investigated, not shipped

* **Thread QoS** (`BO3MACFIX_QOS=1`, experiment). The main, job-queue and
  D3D11 render threads are promoted to `USER_INTERACTIVE`. The render thread
  is recognised as the one creating render encoders. Results were 91.6 and
  75.9 FPS against 81.9 and 88.4 FPS, which is inconsistent, so it stays off.
* **Redundant Metal state.** `BO3MACFIX_METALSTATS=1` logs per-frame Metal
  calls (diagnostic; it adds overhead). Per frame in Shadows of Evil there
  are about 2,280 draws, 10 render encoders, 570 pipeline binds, 2,630 vertex
  buffer binds, 2,730 vertex buffer offset updates, 3,870 fragment texture
  binds and 645 fragment sampler binds. Only about 5% of binds repeat
  already-bound state, so filtering them would save at most about 2% of
  render-thread time.
* **Display sleep.** The game already holds an SDL
  `PreventUserIdleDisplaySleep` assertion; no change needed.
* Smaller render-path candidates found by decompiling Aspyr's draw path:
  skipping the per-draw autorelease pool on the encoder fast path
  (`0x31fd6`, ~0.5%), caching vertex step modes (`0x46447`, 1–2%), and
  memoising the last pipeline-state key (`0x3725e`, ≤1.5%).

## What limits FPS now

In-map, one render thread is saturated: about 48% of its time is in Aspyr's
D3D11-to-Metal layer, about 37% in Apple's AGX driver encoding draw calls
(also translated by Rosetta), and the rest waiting. The main thread spends
about 30% of its time waiting for it. Further in-map gains need fewer draw
calls (for example dynamic shadow settings, a quality trade-off) or
optimising Aspyr's translation layer.

A native ARM64 build (as in adrbn/bo2-mac) would remove Rosetta entirely but
is a static recompilation project of a different scale; not attempted here.

## Memory note

`footprint` reports ~6 GB at a fresh menu, but 2.6 GB of the game's 2.9 GB
"App-Specific Tag 2" pool is compressed/swapped (touched once during
startup, then unused). Only ~106 MB of that pool is resident, so the menu's
real resident use is closer to 3.5 GB than the Metal HUD's "App" figure.
