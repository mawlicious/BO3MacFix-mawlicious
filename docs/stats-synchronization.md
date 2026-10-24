# Synchronizing game settings: stats acknowledgement diagnostics

This change is prepared for review and is **not installed** on the user's
machine. BOIIIMac development remains paused. The installed MacFix dylib still
has SHA-256 `1e6bf8571f50d8174a79fb522820845e0935e9b2eb37f194bdf530db056708d0`.

## Observed session

The private session `session-2026-10-21_21-17-12-75067.log` contains two remote
hosted Shadows of Evil stalls. Both built an 18,782-byte compressed stats buffer
and sent all 16 chunks. The first retried chunk 15 755 times and chunk 16 754
times, remaining in the transfer stage for roughly 154 seconds before
disconnecting. The second retried chunk 16 730 times and disconnected after
roughly 76 seconds. These durations describe the log; they are not newly
identified timeout constants. Several earlier successful launches in the same
session were local-host transfers and do not establish that this remote path
was working.

The existing log does not contain the host's `statresponse` masks. It proves
the stalled stage, but cannot distinguish lost final acknowledgements,
reordered acknowledgements, or the host still missing data. It cannot prove
that minimizing the game caused either attempt. Later Demonware send failures
and session teardown are additional observations, not proof of the initial
stats-transfer cause.

## Audited engine behavior

Read-only inspection used the installed Mac v98 executable, SHA-256
`932f0918fb9871bc625b69a587135a5c2976e9f9e6b7289cfadda8ab996e1cd0`.
No executable, installed library or running process was modified for diagnosis.
The maintained Windows T7Patch source was also checked; its client OOB filter
allows `statresponse` and does not implement cumulative stats acknowledgements:
[Hooks.cpp](https://github.com/Scroptss/T7Patch-src/blob/main/Hooks.cpp).

Mac addresses below are offsets from the Mach-O image base:

- `0xe17e98`, client resend: splits the compressed buffer into 1,216-byte chunks
  and selects a pending bit from the two 64-bit masks. It can resend a pending
  chunk every frame once all chunks have been sent.
- `0xe19402`, client connectionless packet dispatch. The `statresponse` branch
  accepts state **8** (`CA_SENDINGDATA`), then reads two decimal masks.
- `0xe19ae2` and `0xe19aea`: overwrite the pending masks at connection offsets
  `0x25568` and `0x25570`. The following OR test transitions to state 7
  (`CA_CONNECTED`) only if both newly received masks are zero.
- `0x1402da4`, Mac server stats receiver: can return immediately for a transfer
  it considers complete; ordinary replies report which chunks remain missing.
  This is static evidence for the Mac receiver, not a runtime trace of the
  friend's Windows host.
- Connection pointer global `0x115345a0`, stride `0x25780`; client UI array
  `0x115324a0`, stride `0x1078`, state at `+8`.

A delayed older reply can restore previously cleared bits and cause redundant
retries. However, replies contain no transfer identifier: a reply from the
previous transfer can also arrive during a new transfer to the same host.
Intersecting masks would then prevent a subsequent authoritative reply from
restoring chunks needed by the new transfer. Resetting a local cache cannot
identify which transfer an untagged incoming reply belongs to. The initially
proposed cumulative-mask change was therefore removed during review.

For a receiver that accumulates chunks monotonically, missing masks are nested;
combining them also cannot recover a lost final zero acknowledgement. Neither
observation establishes the cause or resolution of the two recorded stalls.

## Change and limits

The hook only observes recognized replies from the current server in state 8.
It passes the original packet and message to the original engine dispatcher
unchanged. The engine retains source validation, mask replacement and state
transitions. There is no cumulative ACK state and no packet normalization.

The hook checks instruction signatures for the dispatcher, connection lookup,
stride and both mask stores before installation. Unknown instructions disable
diagnostics. Nonmatching syntax, other sources, other states and split
messages retain original handling.

Mask-change logs include both previous and resulting pending masks and engine
state. They label every change neutrally, since restored missing bits do not
represent forward progress. Reply logs include the
host's missing masks and are sampled at counts 1–4 and powers of two. These
counters count only engine-accepted replies over the local client lifetime,
not transfer IDs. Engine-rejected replies do not emit ACK logs or consume
accepted-reply samples.
They contain neither stats payloads nor passwords. A subsequent live session
can distinguish arriving but stagnant replies from no replies.

**This PR does not yet fix the recorded synchronization stalls.** It prepares
the missing evidence needed to choose a supported fix. Host-side evidence may
also be necessary. The existing 30-second lobby-info change is preserved as a
separate commit; it addresses a different connection stage.

## Verification

`bash scripts/test.sh` runs the new parser/runtime regression with ASan/UBSan.
The runtime fixture models the audited engine's mask replacement and zero-mask
transition, and invokes the actual hook through the mocked Dobby installation.
It tests unchanged handling of reordered replies and delayed replies crossing
a new transfer to the same host,
both 64-bit words, source rejection, independent
local clients, new transfers, incomplete transfers remaining incomplete,
instruction guard failures and hook installation failure. This is an offline
regression, not an end-to-end network reproduction of the user's session.

An optional private executable fixture verifies the guards against the actual
game bytes (read-only):

```sh
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isource \
  tests/test_stats_sync.c source/stats_sync.c source/stats_sync_runtime.c \
  -o /tmp/test-stats-mawlicious
/tmp/test-stats-mawlicious '/path/to/CoDBlkOps3_Exe'
```

After review, install only when the user is finished playing and authorizes
installation. A subsequent friend-hosted game must validate the missing ACK evidence. Do not describe the fix as live-validated before that test.
