# Changelog

BO3MacFix-mawlicious versions are numbered v1, v2, ... The in-game top-right
label and the session logs show `BO3MacFix-mawlicious vN`. Releases up to v4
used longer tags; they are listed below.

## v6 — No more multi-second freezes from reflection-probe streaming

- Shadows of Evil streams its BC6H-compressed reflection probe volumes (3D
  textures) in and out as you move around the map. Aspyr's layer decoded every
  BC-compressed 3D texture on the CPU (under Rosetta) into a format 8× larger,
  freezing the game for 2.5–4 s each time. Apple GPUs read BC formats in 3D
  textures directly, so they now stay compressed: no decode, about 0.9 GB less
  memory at the Shadows of Evil spawn. `BO3MACFIX_BC3D=0` restores the old
  behaviour.

## v5 — Remaining Aspyr spin-waits removed, freeze sampler (PR #9)

- Aspyr's D3D11 device-context lock no longer spins up to 100,000
  `pthread_mutex_trylock` retries before blocking (~+7% FPS in Shadows of
  Evil in alternating runs).
- The D3D render thread's idle wait and the main thread's wait for the
  render thread are real waits instead of busy-spins (same FPS, main thread
  CPU ~70% → ~48%).
- Opt-in freeze sampler: create `BO3MacFix-mawlicious/enable-freeze-log` to
  log where the game's threads are during stalls over 400 ms.
- Opt-in diagnostics: `BO3MACFIX_METALSTATS=1` (Metal calls per frame),
  `BO3MACFIX_QOS=1` (thread QoS experiment, off by default).
- Version label simplified to `vN`; this changelog added.

## v4 — Rosetta AOT preserved, no job-thread spinning (tag `v1.2-beta3-perf1-mawlicious`)

- PR #8: the launcher builds a pre-patched `CoDBlkOps3_Exe_mawlicious` so
  Rosetta keeps its ahead-of-time translation (runtime code patching made the
  whole game run on Rosetta's JIT). Idle job-queue workers wait instead of
  spinning. Frame-time telemetry in session logs. ~+6% FPS in-map with 16%
  less CPU; menus use less than half the CPU.
- PR #7: complete audited Mac counterparts of the T7Patch defensive checks.
- PR #6: T7-style Mac network hardening checks.
- PR #5: lower memory use in native navigation conversion and oversized
  archive stream buffers.
- PR #4: stats-synchronization diagnostics and ACK handling.

## v3 — Roulette callback repair (tag `v1.2-beta3-native3-mawlicious`)

- PR #3: repairs four uninitialized event-flag comparisons in the One Barrier
  Roulette V2 script (exact script revision only).

## v2 — Standalone installer (tag `v1.2-beta3-native2-mawlicious`)

- PR #2: complete standalone distribution with the upstream assets and a
  native Mac installer; no separate BO3MacFix, Wine or Python needed.

## v1 — Wine-free native converter (tag `v1.2-beta3-native1-mawlicious`)

- PR #1: Havok navigation data is converted inside the Mac game process, so
  custom maps no longer need Wine or the Windows modding tools. Separate
  session logs per launch.
