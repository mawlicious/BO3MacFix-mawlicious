# Network hardening coverage

This build ports a verified subset of T7Patch's defensive checks. It is **not
certified safe for public matchmaking**, and does not claim complete T7Patch
RCE protection. Mac and Windows have different executables and ABIs.

## Reference

Compared against the local T7Patch-mawlicious source at
`eec0c628b84d3a9d8825a439effca9c5fe9a5064`, based on Scroptss/T7Patch-src.
Reference files: Hooks.cpp (`hkCL_ConnectionlessCMD`,
`hkdwInstantDispatchMessage`, `hkLobbyMsgRW_PackageInt/UInt/UChar`) and
framework.h (`legit_packets`). Checks here are independently implemented for
the Mac binary; Windows function addresses and packet structures are not reused.

## Added

- Filter the **engine-tokenized** connectionless command against the T7 allowlist.
  This prevents alternate quoting/comment syntax bypassing a separate parser.
  `requeststats` remains enabled in campaign only. The Mac's distinct
  `connectResponseMigration` command remains enabled for host migration.
- Check instant-message pointer, header, minimum size and T7's less-than-2048
  payload size before entering the original parser. Existing e/m command-message
  blocks and optional friends-only restrictions remain in place.
- Validate signed/unsigned lobbytype, srclobbytype and destlobbytype fields in
  the range 0..2 after successful scalar parsing. Return failure on invalid data.
- Sanitize parsed NAT types above 4 to unknown (4), matching T7Patch.

## Mac integration

Inspected Mac executable SHA256:
`932f0918fb9871bc625b69a587135a5c2976e9f9e6b7289cfadda8ab996e1cd0`.

The command filter hooks `+0xe19016` only when entered from the audited
`CL_ConnectionlessPacket` return address `+0xe194a8`. Two lobby-router callers
of the same helper retain original behavior. The engine has already tokenized
the message. The filter reads its thread-local argument stack using the same
layout seen in the original helper (`Sys_GetTLS +0xd0604f`, args at TLS+24,
nesting at +0, argc at +0x64, argv at +0x88). Returning handled follows the
original dispatcher's token-stack cleanup path.

Scalar hooks use the package dispatch wrappers `+0xf41d33` (int), `+0xf41db0`
(uint), `+0xf41fc0` (uchar). ABI signatures, TLS lookup and caller bytes are
checked before hooking. Startup reports installed hooks out of four: a mismatch
or partial installation must not be interpreted as protection being enabled in
full. The hook count does not include the existing instant-message hook.
Logs sample rejections and do not include packet contents.

## Remaining coverage gaps

This is not a full RCE audit. T7 protections not yet ported/verified include:

- Config-string `mspreload` handling and UI string/directive/model validation.
- Complete lobby-state/member/array inspection and schema-specific count limits.
- Pending info-response correlation and the Windows-specific join-request layout.
- Additional platform callbacks, decoder/sign-extension patches and internal
  debug-parser paths. Windows patch bytes must not be transplanted blindly.

The Mac glob reader already compares its length with an unsigned branch;
changing its sign-extension opcode without auditing all callers is not a
substitute for validating its actual bounds.

Network passwords are an additional compatibility barrier for private games,
not a cryptographic authentication guarantee. Public matchmaking requires
accepting traffic from strangers; friends-only/password options do not establish
that all packet parsers are secure. No known exploit succeeding on Windows
should be assumed either to work or to be impossible on Mac without analysis.

## Validation

`bash scripts/test.sh` exercises command and IM boundaries, scalar ranges,
original forwarding, campaign handling, invalid TLS nesting and instruction
mismatch behavior under ASan/UBSan. The security runtime fixture uses mocked
original functions; it does not reproduce a live exploit. An optional private
binary argument checks guards against the actual executable without distributing
it. Installer/package tests cover the complete distribution separately.
