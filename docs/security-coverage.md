# T7Patch protection coverage

This is a Mac implementation of the applicable defenses in the pinned T7Patch
reference below. It replaces the earlier `security1` subset. Coverage means
that the identified checks have a Mac implementation or verified native check;
it does not establish that either executable is free of other vulnerabilities.

## Reference and method

Reference: [T7Patch-mawlicious at eec0c628](https://github.com/mawlicious/T7Patch-mawlicious/tree/eec0c628b84d3a9d8825a439effca9c5fe9a5064),
based on [Scroptss/T7Patch-src](https://github.com/Scroptss/T7Patch-src).
The inventory covers active `Hooks::ApplyHooks`, `ApplyMemoryPatches`,
`ApplyVMTHooks`, `Protection::install`, packet inspectors and exception handlers.
It distinguishes defensive behavior from entitlement/rank/name customization.

Mac: Aspyr v98 x86_64 executable SHA256
`932f0918fb9871bc625b69a587135a5c2976e9f9e6b7289cfadda8ab996e1cd0`.
Windows comparison: pristine loaded September 2026 build, PE timestamp
`0x6a7b6355`, SizeOfImage `0x1d75bc00`, without T7Patch injected. Windows RVAs
below use the reference's February address convention; `GameBuild.h` subtracts
`0x6c0` from September code RVAs in its mapped range. Mac offsets are relative
to the executable image base. These are disassembly-based ABI/layout findings,
not recovered original C source. Proprietary game code/dumps are not distributed.

## Coverage matrix

| T7Patch mechanism | Mac counterpart and treatment |
| --- | --- |
| Connectionless allowlist (`hkCL_ConnectionlessCMD`) | Existing engine-tokenized allowlist at `+0xe19016`, restricted to the client caller returning to `+0xe194a8`. Campaign alone accepts `requeststats`; the Mac-specific `connectResponseMigration` remains available. Other lobby-router callers of the shared helper retain their original behavior. |
| IM header, length and e/m blocks | `dwInstantDispatchMessage +0xe36915`: validates `1` envelope, minimum size and payload below 2048; rejects e/m before original dispatch. |
| f join-request guard | Exactly 100 payload bytes, first uint32 nonzero. Verified at native `+0x134c049`; its kind-zero path enters `+0x134bdb2`, which contains an invalid absolute read. Steam P2P also blocks f, matching T7's receive hook. |
| Global IM callback disabled | Native Demonware callback `+0xe374a2` returns false. Steam P2P and the checked game dispatcher remain separate paths. |
| `ReadP2PPacket` restrictions | Steam networking vtable slot 2: pump original queue, then drop e/m/f; friends-only h packets require a cached Steam friend. Invalid output sizes are rejected. |
| Lobby type and NAT validation | PackageInt `+0xf41d33`, UInt `+0xf41db0`, UChar `+0xf41fc0`: lobbytype/srclobbytype/destlobbytype 0..2; NAT above 4 becomes unknown (4). |
| Zero/Zombies datamask guard | Reject zero; reject bit 512 in Zombies, using native session-mode bits at `+0x6939a14`. Validation runs on reads. |
| JoinParty member overflow inspector | Native parser `+0x1234e06` uses the shared readers. Bound membercount to 18 before its loop, reject an element when the caller passes expected=false, mark overflow and return failure. |
| LobbyState client/nominee inspectors | Native `+0x116fa8e`: clientcount <=18; shared element guard prevents extra client/nominee writes. Nominee loops derive expected from the destination XUID, so the read wrapper tracks the named array and allows at most 18 entries independently of its zero-initialized destination. Other counted arrays retain their expected-value check. |
| LobbyStateGame vote/settings inspectors | Native `+0x116ffdc`: votecount <=216, settingssize 1..0xc000 before settings copy; fixed 18-client and counted vote arrays use the guarded element reader. |
| Host heartbeat nominee inspector | Native `+0x11709d0` consumes the same guarded nominee reader, with an explicit 18-entry read limit. |
| Safe InfoResponse parsing (`CheckPendingInfoRequests`, `MSG_InfoResponseSafe`) | Mac wire layout differs. Native `+0xe3528a` and its nested helper `+0x1235758` use the shared scalar/glob/element readers. Lobbies stop at two, clients at guarded clientcount <=18. Extra array elements mark overflow and stop the loop. Top-level package guards reject results with read-mode overflow, including native packagers that otherwise return their last scalar result. The T7 function name does not represent a request/nonce correlation ledger; none is claimed here. |
| Automatic drops of message types 15, 22, 23, 24, 30 | Applied after native PrepReadMsg `+0xf416fb`, before original handlers. Also reject types outside 0..32. These correspond to stats-modified, serverlist/P2P metadata and voice packet paths. |
| Message type name bounds | Getter `+0x1311e7c` bounds -1..32 before pointer-table access. |
| PackageReadGlob sign fix | Already bounded natively: `+0xf422d7` compares the sign-extended value to capacity using an unsigned branch before copying. Negative wire lengths fail that comparison. No Windows opcode is transplanted. |
| `qmemcpy` negative count | The Windows private signed-count copy helper has no corresponding global Mac CRT ABI. Guard native `MSG_ReadData +0x1247f67` against negative count/read position and signed addition overflow; the UI copy-length fixes below guard the other identified shared sinks. Do not hook libc memcpy globally. |
| Two signed-byte UI copy fixes | Verified Windows sites `+0x1ca4c84`, `+0x1f21ef8` read encoded image-name lengths. Mac sites `+0xd50e5e`, `+0x11e57ac` use the same signed byte before memcpy. Change to unsigned, with five related Mac length readers also corrected (`+0x11cbb5e`, `+0xf662c7`, `+0x143cdb9`, `+0x143e175`, `+0x143f905`). Guard the full instruction before changing its opcode. |
| Tokenizer length patch | Already present: `+0xf75f7a` compares the token length with 0x2f. Retain it. |
| `sl` forced-error patches | Native handler `+0xf42b13`: validate 3 arguments and unsigned entnum <1024 before original. Malformed commands return instead of calling Com_Error. Valid handler behavior is retained. |
| Directive/model string guards | `+0xf47799`, `+0xd50fdc`: private bounded copies, terminate/bound ^B and [{...}] tokens, neutralize unterminated $(...) references. Additional independently reachable binding parsers `+0xf47819`, `+0x12c8e15` validate length, offset and tokens before their fixed-array copies. |
| UI model Get/Create path guards | Shared helper `+0x1036426` covers Get/Create wrappers. Reject null paths, whole paths >=4096 and components >=64 before the native stack copy. |
| UI model allocation guard | Already present at `+0x1036158`: null key, 64-byte key limit and free-node pool exhaustion checks. Preserve the native checks. |
| Unnamed character-index hook / matching exception recoveries | Verified as animation notetrack lookup, not a glyph index: Mac `+0xe43821` and Windows `+0x22c9650` both load parts at animations+56+24*index, then the notes pointer at parts+0xc0. Return -1 for null parts before dereference. |
| LuaCrash1 | Windows `+0x1c9f121` is a null text read in text width, not an arbitrary Lua VM exception. Mac `+0xf65fd8` returns 0 for null text, preserves other arguments including floating-point spacing. |
| LuaCrash2 | Windows `+0x221cec3` is a null source read in directive replacement. Covered by the guarded `+0xf47799` wrapper, which substitutes an empty string. |
| LuaCrash3 | Windows `+0x221c726` is a null input read in localization. Mac `+0xf47c10` substitutes empty input/context. |
| mspreload config strings / preload idle | Getter `+0xe2e90a` suppresses mspreload at 3514/3627 outside campaign. Preload idle `+0x131489c` runs only in campaign. |
| Debug message parsing disabled | Dump wrapper `+0xf417ae` is disabled. The Mac shared tag reader `+0xf41836` also handles normal fields, so its replacement preserves ordinary tags and rejects debug-section tag 15 before recursive decoding. A blanket return at this Mac entry would break normal lobby parsing. |
| ExecLuaCMD disabled | Native execlua command `+0x1409b4c` is disabled. The pre-existing MacFix Lua_CmdParseArgs patch is separate and remains. |
| Browser opening disabled | Native platform URL opener `+0xd62926` returns failure, matching T7's UI_BrowserOpen policy. |
| Lobby chat sender and formatting checks | Steam matchmaking slot 27: sender must have an active member entry in a current BO3 private/game session, not merely a Steam lobby. Bound the returned text to the original byte count and neutralize ^, %, $(, [{ and nonprintable/non-ASCII bytes. Uses audited client-session `+0x1362cdf` and member lookup `+0x12b33aa`, with active pointer at member+8. |
| Friends-only invite/join restrictions | `+0x134b1b9`, `+0x134bbf3`, `+0x134bdb2` check the same optional friends-only policy before accept/action/send-info. Public operation does not enable friends-only automatically. |
| MenuResponse and cached counterpart | `+0xf451d1`, `+0xf453b0`: validate menu/event bounds, reject badspawn, restrict remote kill/restart/end requests using entity number and host/campaign checks. Native server-ID checks remain. Guard shared index getters `+0x12a693e` (<64) and `+0x12a6d30` (<256). Other commented-out T7 cache hooks are not active defenses in the reference. |
| Presence player-count guard | Native pack/unpack `+0x15312a1`: reject packed count >18 before decoding, validate stored/advertised counts before packing. The Mac routine's explicit unpack flag differs from Windows's reader-object ABI. |
| Disable embedded Steam game server | Native `+0xf6956d` disabled, matching hkLiveSteam_InitServer. This removes its separate server initialization/advertisement path; Steam client matchmaking is separate. |
| Create friends-visible Steam lobby | Matchmaking slot 13 forces type 1, matching T7 CreateLobby. This concerns Steam lobby visibility, not enabling the mod's incoming friends-only filter. |
| Disable voting / voice production | Apply g_allowvote=0 and maxvoicepacketsperframe=0 after Steam initialization and on first snapshot. Clear native dvar flags as T7 does; verify current values. Unknown/unregistered dvars are reported, not prematurely registered. |
| Automatic UGC subscriptions | Default disabled at `+0xe4971e`, matching T7. Subscribe through Steam explicitly. BO3MACFIX_AUTOINSTALLMODS=1/true opts back into this behavior. |
| Password checksum / lobby prefix | Existing MacFix implements T7-compatible password hashing, checksum XOR and optional lobby prefix. This is a private-game compatibility barrier, not cryptographic authentication. Signed copy inputs/nulls are guarded and checksum trailer reads avoid unaligned casts. |

## Windows-only and unrelated reference code

Windows VEH/SEH, ZwContinue, ntdll/Arxan detours, PE import hooks,
IsProcessorFeaturePresent and MSVC security-cookie recovery cannot be ported as
Windows exception handlers to a Mach-O process. Their shared engine bug sites
(character/notetrack and all three named LuaCrash sites) are handled above at
Mac entry points, before faults. The lobby-inspector VEH trick is replaced by
native parser hooks. Windows process priority patches are performance settings.

T7's ownership/DLC/VAC/ranked-inventory spoofing, custom names/build title and
performance tweaks are not RCE defenses and are not imported as such. Existing
MacFix entitlement handling is unaffected. Local game code and ordinary mod Lua
still execute; this is not a sandbox for arbitrary downloaded mods.

## Verification and operational limits

Startup reports the six common network hooks, 40 engine operations (33 hooks
and seven instruction changes), three Steam hooks once its interfaces are
initialized, and applied security dvars. Each new engine entry/dependency and
instruction is byte-checked before installation. A mismatch or partial count is
not full coverage. Steam vtable writes are read back; repeated initialization
must not replace original pointers with our wrappers.

`bash scripts/test.sh` includes ASan/UBSan policy, runtime-hook and Steam-interface
fixtures. They exercise boundary counts, unexpected array elements, outgoing
forwarding, malformed/normal UI text, null recovery, cached menu authorization,
presence counts, signed read failures, ordinary/debug tags, invite policies,
original arguments/returns, instruction mismatches and repeat initialization.
`test_security`, `test_security_engine` and `test_security_steam` can additionally
be compiled and run with the private Mac executable path to validate guards;
the executable is not needed in public CI. Tests use mocked originals and do
not reproduce every exploit or substitute for live compatibility testing.

The 30-second join timeout, native navigation converter, stats ACK handling and
memory I/O optimization remain separate features. This work does not continue
the experimental BOIIIMac client.

Local compatibility testing (2026-11-10): the first live online pass exposed a
Mac-specific nominee-array convention: its expected flag describes the
preexisting output value on reads. The wrapper now uses the named nominee
array's explicit 18-entry bound; zero-initialized destinations remain valid.
The regression fixture covers 18 valid entries followed by an extra entry.
