# Local lobby join-info timeout experiment

Installed 2026-10-19T19:10:31.
Game executable unchanged, SHA-256 `932f0918fb9871bc625b69a587135a5c2976e9f9e6b7289cfadda8ab996e1cd0`.
Installed dylib SHA-256 `1e6bf8571f50d8174a79fb522820845e0935e9b2eb37f194bdf530db056708d0`.
Previous dylib backup: `/Volumes/Untitled/SteamLibrary/steamapps/common/Call of Duty Black Ops III/BO3MacFix-mawlicious-backups/before-join30-20261019-191031-mawlicious`.

Both requested source dumps (ate47/bo3-source and shiversoftdev/t7-source)
were checked first; neither contains this native timer.

Disassembly evidence from the installed Mac executable:
- `0x100DA1930`: UpdateInfoProbe reads the dvar pointer at `0x11599CDD8`,
  adds the probe start time, and checks the deadline before reporting timeout.
  The corresponding diagnostic string is at `0x1016B256A`.
- `0x101315FF6`: registers dvar hash `0xD23BBF56`, default 10000,
  minimum 0, maximum INT_MAX, flags 0; stores into that same pointer.
- Only the four-byte default at image offset `0x1315FFC` changes to 30000.
  Registration and deadline-reading instructions are checked before patching.
  The constructor logs success/failure before the game's registration runs.

This is active on the next game launch. Successful joins need not wait 30s.
Other association/handshake timers are unchanged. Gameplay benefit is unverified.
No GitHub push, PR, or release was made for this experiment.

Validation: actual-executable fixture test with ASan/UBSan checks both guards,
write-failure handling and the complete image differing only at the default.
The x86_64 test with real Dobby also passes on a read-only executable mapping.
Native dylib build passed. Fixture runner: tests/test_lobby_timeout.c, with
source/lobby_timeout.c and -Isource. Pass the local game executable as its only
argument. Use -fsanitize=address,undefined for the mock writer, or
-arch x86_64 -DREAL_DOBBY -Llib -ldobby -lc++ for the real writer.

After the user launches BO3, verify the session log contains
`Lobby join-info timeout default: 10000 -> 30000 ms.` and inspect probe durations.
Do not automate the game.
