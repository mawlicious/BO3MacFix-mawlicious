# Roulette callback compatibility

Workshop map 3587150505 (`zm_roulettev2`) logged 1,204 script exceptions in the
2026-10-19 solo session. Of these, 1,114 reported `undefined` versus integer `1`.
The call stacks passed through Roulette's actor-damage callback, the custom perk
damage wrapper, and the normal zombie callbacks.

Offline inspection of the installed map with
[gsc-tool 1.6.1](https://github.com/xensik/gsc-tool/releases/tag/1.6.1) identified
unguarded checks of optional integer event flags in two map functions:

| Function hash | Field hash | Bytecode offset |
| --- | --- | --- |
| `DFA96982` (actor damage) | `2B1154D3` | `18E00` |
| `DFA96982` (actor damage) | `7BB96ECC` | `18EB8` |
| `DFA96982` (actor damage) | `860692B3` | `18FB4` |
| `57273DA8` (AI death) | `2B1154D3` | `1B234` |

The patch changes only these four `== 1` checks to `=== 1`. An unset flag is
inactive; integer 0 and 1 retain their behavior. Both instructions have the same
size and stack effect. The script's bytes outside those four instructions must
match SHA-256 `2157fe219c71aa2dc20a8241e78ea64e8797ef911671e8c49d2bd3158ceff663`
after normalizing the four edits before any change is made. Unknown revisions
that need edits are left intact and logged. A same-size buffer already containing
all four replacement opcodes is treated as patched: it is left intact without
hashing or an unknown-revision warning. This also handles later lookups after the
engine has rewritten import operands during linking.

The mod applies the edits when the engine requests the ScriptParseTree asset,
before script linking. The downloaded Workshop fastfile is never edited, and no
map script is distributed in this repository. Reloading the original mod removes
the in-memory edits. This is a specific map repair, not a change to global script
VM equality or error handling.

Other observed issues remain separate: Mangler initialization/function-pointer
errors, escape-location array errors near the end of the game, and weapon
accuracy-graph capacity warnings. The four edits do not claim to fix these or
prove any FPS improvement. The original session contains no frame-time series.

Validation includes sanitizer tests, an optional private original-script fixture,
and decompiling the edited fixture to confirm exactly four comparison changes.
Run `bash scripts/test.sh /absolute/path/to/private-script.gscc` to include the
private fixture assertions, or omit the argument to run public tests without
shipping game data. Relative fixture paths are resolved from the caller's
working directory.

The first in-game test applied all four edits. The user played several rounds,
killed zombies, and reported successfully using Pack-a-Punch before dying and
returning to the menu. The log contained zero `undefined` versus `1` errors
(previous session: 1,114). This is not a controlled FPS comparison: the sessions
differed in length and events. There were 71 remaining exceptions: 60 Thrasher
`undefined` versus `2` errors, six escape-location array errors, and five
Mangler/map initialization or round-tracking errors. Inspection traces the
Thrasher error to a counter initialized only when scheduled Thrasher rounds are
enabled, while Roulette can spawn Thrashers separately. That counter needs a
separate initialization repair; changing the comparison alone would change its
spawn-cap behavior. These issues are not suppressed by this patch.

The test build also emitted an inaccurate "revision unrecognized" warning on a
subsequent lookup after linking had modified the script's import operands.
Already-patched buffers are now recognized before hashing, and the private
fixture test covers that second lookup. The shutdown mutex abort also occurred
after the user returned to the menu and selected Quit; it is not specific to
Cmd-Q and remains unresolved.
