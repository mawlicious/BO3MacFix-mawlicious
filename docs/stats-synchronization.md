# Synchronizing game settings: stats acknowledgement ordering

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

Replacing masks loses knowledge of earlier receipts. For example, after a
`0x8000` missing mask, a delayed older `0xc000` reply restores chunk 15 to the
retry list even though it was acknowledged. This causes unnecessary retries.
For a receiver that accumulates chunks monotonically, every received mask is
nested: combining those masks does **not** recover a lost final zero reply.
That limitation matters here. The ordering defect is reproducible, but
**does not by itself establish the cause or resolution of either recorded
stall**. The reply diagnostics are needed before claiming an end-to-end fix.

## Change and limits

During state 8, recognized replies from the current server are normalized to
`current_pending & received_missing`. Every cleared bit was acknowledged by a
real prior reply in this transfer. The original engine dispatcher still checks
the source, parses the reply and performs its own completion transition.
No timeout is increased here, no stats packet is treated as received merely
because it was sent, and no connection state is forced.

The hook checks instruction signatures for the dispatcher, connection lookup,
stride and both mask stores before installation. Unknown instructions disable
this fix with a diagnostic. Nonmatching packet syntax, other sources, other
client states and split messages retain the original handling. Input packets
are never edited in place. Engine initialization of new transfer masks also
resets the cumulative knowledge, without a separate persistent ACK cache.

Progress logs include pending masks and the engine state. Repeated replies
and stale-reply notices are sampled at counts 1–4 and powers of two, so a
future session can distinguish arriving but stagnant replies from no replies.
They contain neither stats payloads nor passwords.

This cannot recover a chunk that was never acknowledged by any received
reply, fix a broken route, or repair a host that stops receiving. Those cases
still need a later live log and, potentially, host-side evidence. The old
30-second lobby-info change is preserved as a separate commit in this PR;
it addresses a different connection stage.

## Verification

`bash scripts/test.sh` runs the new parser/runtime regression with ASan/UBSan.
The runtime fixture models the audited engine's mask replacement and zero-mask
transition, and invokes the actual hook through the mocked Dobby installation.
It tests the observed 16-chunk pending shape, reordered replies,
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
installation. A subsequent friend-hosted game must validate whether stalls
are resolved. Do not describe the fix as live-validated before that test.
