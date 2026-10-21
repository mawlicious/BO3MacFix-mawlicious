#include "stats_sync.h"
#include <string.h>

static bool decimal(const uint8_t *data, size_t size, size_t *cursor, uint64_t *out) {
    size_t start = *cursor;
    uint64_t value = 0;
    while (*cursor < size && data[*cursor] >= '0' && data[*cursor] <= '9') {
        unsigned digit = data[(*cursor)++] - '0';
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (*cursor == start) return false;
    *out = value;
    return true;
}

bool bo3_stats_ack(const uint8_t *packet, size_t length,
                   const uint64_t pending[2], BO3StatsAck *result) {
    static const uint8_t prefix[] = {255,255,255,255,'s','t','a','t','r','e','s','p','o','n','s','e',' '};
    if (!packet || !pending || !result || length < sizeof(prefix) || length > 128 ||
        memcmp(packet, prefix, sizeof(prefix))) return false;
    size_t cursor = sizeof(prefix);
    uint64_t incoming[2];
    if (!decimal(packet, length, &cursor, &incoming[0]) ||
        cursor == length || packet[cursor++] != ' ' ||
        !decimal(packet, length, &cursor, &incoming[1])) return false;
    /* The engine accepts a newline or NUL terminator; don't normalize packets
       containing another command, embedded NULs or unrecognized syntax. */
    if (cursor < length && packet[cursor] == '\r') ++cursor;
    if (cursor < length && packet[cursor] == '\n') ++cursor;
    if (cursor < length && packet[cursor] == 0) ++cursor;
    if (cursor != length) return false;
    memcpy(result->incoming, incoming, sizeof(incoming));
    result->missing[0] = pending[0] & incoming[0];
    result->missing[1] = pending[1] & incoming[1];
    result->stale = result->missing[0] != incoming[0] || result->missing[1] != incoming[1];
    return true;
}

int bo3_stats_sync_supported(uint64_t base) {
    /* Mac v98 CL_ConnectionlessPacket entry and its statresponse mask stores.
       These also pin the client index register and both audited field offsets. */
    static const uint8_t entry[] = {
        0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,
        0x53,0x48,0x81,0xec,0x78,0x08,0x00,0x00,
        0x49,0x89,0xcd,0x48,0x89,0xd3,0x49,0x89,0xf6,0x41,0x89,0xff
    };
    static const uint8_t stores[] = {
        0x4b,0x89,0x9c,0x2c,0x68,0x55,0x02,0x00,
        0x4b,0x89,0x84,0x2c,0x70,0x55,0x02,0x00,0x48,0x09,0xc3
    };
    static const uint8_t connection_lookup[] = {
        0x4c,0x8b,0x25,0x53,0xab,0x71,0x10,0x44,0x89,0xff,
        0xe8,0x0c,0x4c,0x09,0x00,0x4c,0x69,0xad,0x78,0xf7,0xff,0xff,0x80,0x57,0x02,0x00
    };
    return memcmp((const void *)(uintptr_t)(base + 0xe19402), entry, sizeof(entry)) == 0 &&
           memcmp((const void *)(uintptr_t)(base + 0xe19ae2), stores, sizeof(stores)) == 0 &&
           memcmp((const void *)(uintptr_t)(base + 0xe19a46), connection_lookup, sizeof(connection_lookup)) == 0;
}
