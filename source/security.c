#include "security.h"
#include <string.h>
#include <strings.h>

/* T7Patch's connectionless allowlist, evaluated on the ENGINE token rather
 * than a second tokenizer that might disagree about quotes or comments. */
bool bo3_security_command(const char *command, bool campaign) {
    static const char *const allowed[] = {
        "connectResponse", "statresponse", "LM", "disconnect", "loadoutResponse",
        "infoResponse", "statusResponse", "keyAuthorize", "error", "print",
        "fastrestart", "ping", "pinga", "steamAuthReq", "cfl",
        /* Mac has a distinct host-migration response handler. */
        "connectResponseMigration"
    };
    if (!command) return false;
    if (campaign && !strcasecmp(command, "requeststats")) return true;
    for (size_t i = 0; i < sizeof(allowed)/sizeof(*allowed); ++i)
        if (!strcasecmp(command, allowed[i])) return true;
    return false;
}

bool bo3_security_im(const uint8_t *data, int length) {
    /* Existing MacFix blocked e/m. Also enforce the header and T7's <2048
     * payload limit before the engine reads anything. Join request layout is
     * verified separately on Mac; don't reinterpret Windows structs here. */
    return data && length >= 2 && length - 2 < 2048 && data[0] == '1' &&
           data[1] != 'e' && data[1] != 'm';
}

bool bo3_security_lobby_type(const char *key, uint32_t value) {
    if (!key) return false;
    if (!strcasecmp(key, "lobbytype") || !strcasecmp(key, "srclobbytype") ||
        !strcasecmp(key, "destlobbytype")) return value <= 2;
    return true;
}
