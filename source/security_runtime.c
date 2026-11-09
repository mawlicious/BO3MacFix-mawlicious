#include "security.h"
#include "dobby.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static uint64_t image;
static bool (*original_command)(int, void *);
static bool (*original_int)(void *, const char *, int32_t *);
static bool (*original_uint)(void *, const char *, uint32_t *);
static bool (*original_uchar)(void *, const char *, uint8_t *);
static void *(*engine_tls)(void);

static void rejected(const char *reason) {
    static atomic_uint count;
    unsigned n = atomic_fetch_add_explicit(&count, 1, memory_order_relaxed) + 1;
    if (n <= 4 || (n & (n - 1)) == 0)
        printf("[BO3MacFix-mawlicious security] Dropped unsafe network input: %s (%u)\n", reason, n);
}

/* Called after Cmd_TokenizeString, before the client's command handlers.
 * Returning true takes the engine's normal Cmd_EndTokenizedString cleanup. */
static bool checked_command(int client, void *message) {
    uint8_t *tls = engine_tls(), *args = NULL;
    if (tls) memcpy(&args, tls + 24, sizeof(args));
    int32_t nesting = -1, argc = 0;
    const char **argv = NULL;
    if (args) memcpy(&nesting, args, sizeof(nesting));
    if (nesting >= 0 && nesting < 8) {
        memcpy(&argc, args + 0x64 + nesting * 4, sizeof(argc));
        memcpy(&argv, args + 0x88 + nesting * 8, sizeof(argv));
    }
    uint32_t mode;
    memcpy(&mode, (const void *)(uintptr_t)(image + 0x6939a14), sizeof(mode));
    if (!argv || argc <= 0 || !bo3_security_command(argv[0], (mode & 15) == 2)) {
        rejected("connectionless command");
        return true;
    }
    return original_command(client, message);
}

static bool command(int client, void *message) {
    /* This helper is also called by the lobby packet router. Only filter
     * the audited CL_ConnectionlessPacket call; preserve other callers. */
    if ((uintptr_t)__builtin_return_address(0) != image + 0xe194a8)
        return original_command(client, message);
    return checked_command(client, message);
}

static bool package_int(void *msg, const char *key, int32_t *value) {
    bool ok = original_int(msg, key, value);
    if (ok && !bo3_security_lobby_type(key, (uint32_t)*value)) {
        rejected("lobby type"); return false;
    }
    return ok;
}
static bool package_uint(void *msg, const char *key, uint32_t *value) {
    bool ok = original_uint(msg, key, value);
    if (ok && !bo3_security_lobby_type(key, *value)) {
        rejected("lobby type"); return false;
    }
    return ok;
}
static bool package_uchar(void *msg, const char *key, uint8_t *value) {
    bool ok = original_uchar(msg, key, value);
    if (ok && key && !strcasecmp(key, "nattype") && *value > 4) *value = 4;
    return ok;
}

int bo3_install_security(uint64_t base) {
    /* All ABI/layout guards are checked BEFORE installing any new hook. */
    static const uint8_t cmd[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x81,0xec,0x10,0x04,0,0};
    static const uint8_t scalar[] = {0x48,0x89,0xf0,0x8b,0x4f,0x40,0x85,0xc9,0x74};
    static const uint8_t caller[] = {0x44,0x89,0xff,0x4c,0x89,0xee,0xe8,0x6e,0xfb,0xff,0xff,0x84,0xc0};
    static const uint8_t tls_lookup[] = {0xe8,0x0e,0xd0,0xee,0xff,0x48,0x8b,0x40,0x18,0x48,0x63,0x08};
    if (memcmp((void *)(uintptr_t)(base+0xe19016),cmd,sizeof(cmd)) ||
        memcmp((void *)(uintptr_t)(base+0xe1949d),caller,sizeof(caller)) ||
        memcmp((void *)(uintptr_t)(base+0xe1903c),tls_lookup,sizeof(tls_lookup)) ||
        memcmp((void *)(uintptr_t)(base+0xf41d33),scalar,sizeof(scalar)) ||
        memcmp((void *)(uintptr_t)(base+0xf41db0),scalar,sizeof(scalar))) return -1;
    /* UChar wrapper uses exactly the same dispatch convention, with uint8_t. */
    static const uint8_t uchar[] = {0x48,0x89,0xf0,0x8b,0x4f,0x40,0x85,0xc9,0x74};
    if (memcmp((void *)(uintptr_t)(base+0xf41fc0),uchar,sizeof(uchar))) return -1;
    image = base;
    engine_tls = (void *)(uintptr_t)(base + 0xd0604f);
    int installed = 0;
#define HOOK(offset, replacement, original) \
    if (!DobbyHook((void *)(uintptr_t)(base+(offset)), (void *)(replacement), (void **)&(original))) ++installed
    HOOK(0xe19016, command, original_command);
    HOOK(0xf41d33, package_int, original_int);
    HOOK(0xf41db0, package_uint, original_uint);
    HOOK(0xf41fc0, package_uchar, original_uchar);
#undef HOOK
    return installed;
}
