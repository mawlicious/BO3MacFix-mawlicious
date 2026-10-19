#include "script_fixes.h"
#include <CommonCrypto/CommonDigest.h>
#include <stdint.h>
#include <string.h>

// Workshop 3587150505, inspected 2026-10-19. Only optional integer event
// flags in the actor-damage and AI-death callbacks are changed. OP_SuperEqual
// (===) returns false for undefined versus 1; OP_Equal (==) throws instead.
// Both opcodes consume the same operands and occupy two bytes. Do not scan
// arbitrary bytecode for opcode values: operands may contain those values too.
static const size_t sites[] = {0x18e00, 0x18eb8, 0x18fb4, 0x1b234};
static const uint8_t original_ops[][2] = {
    {0xf9, 0x27}, {0xe7, 0x21}, {0xa7, 0x34}, {0x17, 0x27}
};
static const uint8_t original_sha256[CC_SHA256_DIGEST_LENGTH] = {
    0x21,0x57,0xfe,0x21,0x9c,0x71,0xaa,0x2d,0xc2,0x0a,0x82,0x41,0xe7,0x8e,0xa6,0x4e,
    0x87,0x97,0xef,0x91,0x16,0x71,0xe8,0xc4,0x9d,0x2b,0xd3,0x15,0x8c,0xef,0xf6,0x63
};

int fix_roulette_callbacks(const char *name, void *data, size_t size) {
    if (!name || strcmp(name, "scripts/zm/zm_roulettev2.gsc") != 0)
        return 0;
    if (!data || size != 263084)
        return -1;
    uint8_t *bytes = data;
    // Normalize only our four edits in the hash input, allowing idempotent
    // lookup without accepting any other change to the script.
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    size_t previous = 0;
    int changed = 0;
    for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++) {
        const uint8_t *op = bytes + sites[i];
        if (memcmp(op, original_ops[i], 2) == 0)
            changed = 1;
        else if (op[0] != 0x6c || op[1] != 0x00)
            return -1;
        CC_SHA256_Update(&context, bytes + previous, (CC_LONG)(sites[i] - previous));
        CC_SHA256_Update(&context, original_ops[i], 2);
        previous = sites[i] + 2;
    }
    // Linking rewrites import operands after the first lookup. If all edits
    // are already present, do nothing; no hash exception can authorize edits.
    if (!changed)
        return 0;
    CC_SHA256_Update(&context, bytes + previous, (CC_LONG)(size - previous));
    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    if (memcmp(digest, original_sha256, sizeof(digest)) != 0)
        return -1;
    for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++) {
        bytes[sites[i]] = 0x6c;
        bytes[sites[i] + 1] = 0x00;
    }
    return 1;
}
