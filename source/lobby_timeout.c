#include "lobby_timeout.h"
#include "dobby.h"
#include <string.h>

int install_lobby_info_timeout(uint64_t game_base) {
    /* Mac v98: Dvar_RegisterInt(hash D23BBF56, default 10000, min 0,
       max INT_MAX, flags 0) stores its result at image offset 1599CDD8.
       UpdateInfoProbe reads that same dvar at DA1930, adds the probe
       start time, and reports timeout only once the deadline is exceeded. */
    static const uint8_t registration[] = {
        0xbf,0x56,0xbf,0x3b,0xd2,0xba,0x10,0x27,0x00,0x00,0x31,0xc9,
        0x41,0xb8,0xff,0xff,0xff,0x7f,0x45,0x31,0xc9,0x48,0x89,0xde,
        0xe8,0x8b,0xb4,0xc8,0xff,0x48,0x89,0x05,0xbe,0x6d,0x68,0x14
    };
    static const uint8_t timeout_read[] = {
        0x48,0x8d,0x05,0xa1,0xb4,0xbf,0x14,0x48,0x8b,0x38,
        0xe8,0x13,0xe9,0x1f,0x00,0x01,0xd8,0x41,0x39,0xc6,0x7e,0x3e
    };
    uint8_t *site = (uint8_t *)(uintptr_t)(game_base + 0x1315ff6);
    const void *read = (const void *)(uintptr_t)(game_base + 0xda1930);
    if (memcmp(site, registration, sizeof(registration)) != 0 ||
        memcmp(read, timeout_read, sizeof(timeout_read)) != 0)
        return -1;

    /* Only replace the four-byte default, before the game's registration runs. */
    uint8_t milliseconds[] = {0x30, 0x75, 0x00, 0x00}; /* 30000, little endian */
    if (DobbyCodePatch(site + 6, milliseconds, sizeof(milliseconds)) != 0 ||
        memcmp(site + 6, milliseconds, sizeof(milliseconds)) != 0)
        return -2;
    return 1;
}
