#include "stats_sync.h"
#include "stats_sync_engine.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static uint8_t *image, connection[0x25780 * 2];
static BO3StatsDispatch hook;
static int hook_calls, dispatch_calls, fail_hook;
static bool reject;
static BO3StatsAddress server = {0x01020304, 27017, 0, 4, 0};

static uint64_t *pending(int client) {
    return (uint64_t *)(void *)(connection + client * 0x25780 + 0x25568);
}
static int32_t *state(int client) {
    return (int32_t *)(void *)(image + 0x115324a0 + client * 0x1078 + 8);
}
static BO3StatsMessage packet(uint8_t *buffer, uint64_t low, uint64_t high) {
    memset(buffer, 0, 128);
    memset(buffer, 255, 4);
    int n = snprintf((char *)buffer + 4, 124, "statresponse %" PRIu64 " %" PRIu64 "\n", low, high);
    return (BO3StatsMessage){.data=buffer, .max_size=128, .current_size=n + 4};
}
static bool engine(int client, BO3StatsAddress from, BO3StatsMessage *msg) {
    ++dispatch_calls;
    if (reject || client < 0 || client >= 2 || from.ip != server.ip || from.port != server.port ||
        *state(client) != 8 || !msg || msg->overflowed) return false;
    BO3StatsAck ack;
    const uint64_t all[] = {UINT64_MAX, UINT64_MAX};
    if (!bo3_stats_ack(msg->data, msg->current_size, all, &ack)) return false;
    /* Model the audited engine: REPLACE masks, then transition on exact zero. */
    memcpy(pending(client), ack.missing, sizeof(ack.missing));
    if (!(ack.missing[0] | ack.missing[1])) *state(client) = 7;
    return true;
}
int DobbyHook(void *address, void *replacement, void **original) {
    assert(address == image + 0xe19402);
    ++hook_calls;
    if (fail_hook) return -1;
    hook = (BO3StatsDispatch)replacement;
    *original = (void *)engine;
    return 0;
}
static void reset(int client, uint64_t low, uint64_t high) {
    *state(client) = 8;
    memcpy(connection + client * 0x25780 + 0x10, &server, sizeof(server));
    pending(client)[0] = low;
    pending(client)[1] = high;
}
static void deliver(int client, uint64_t low, uint64_t high) {
    uint8_t buffer[128], before[128];
    BO3StatsMessage msg = packet(buffer, low, high), snapshot = msg;
    memcpy(before, buffer, sizeof(buffer));
    assert(hook(client, server, &msg));
    assert(!memcmp(&msg, &snapshot, sizeof(msg)));
    assert(!memcmp(buffer, before, sizeof(buffer)));
}
int main(int argc, char **argv) {
    const uint64_t all[] = {UINT64_MAX, UINT64_MAX};
    uint8_t buffer[128];
    BO3StatsAck ack;
    BO3StatsMessage msg = packet(buffer, UINT64_MAX, UINT64_C(1) << 63);
    assert(bo3_stats_ack(buffer, msg.current_size, all, &ack));
    assert(ack.missing[0] == UINT64_MAX && ack.missing[1] == (UINT64_C(1) << 63));
    assert(!ack.stale);
    for (int n = 0; n < 19; ++n) assert(!bo3_stats_ack(buffer, n, all, &ack));
    const char *bad[] = {"statresponse -1 0", "statresponse 18446744073709551616 0",
                         "statresponse 1", "statresponse 1 0 extra", "statresponse 1 0\n0",
                         "statresponse  1 0", "statresponse 1 +0", "stats 1 0"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) {
        memset(buffer, 255, 4);
        strcpy((char *)buffer + 4, bad[i]);
        assert(!bo3_stats_ack(buffer, strlen(bad[i]) + 4, all, &ack));
    }
    assert(!bo3_stats_ack(NULL, 20, all, &ack));
    assert(!bo3_stats_ack(buffer, 129, all, &ack));

    const size_t image_size = 0x115345a8;
    image = mmap(NULL, image_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert(image != MAP_FAILED);
    /* Public instruction fixtures; optional private executable verifies the
       guard against actual bytes without shipping any game executable. */
    const uint8_t entry[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,
        0x53,0x48,0x81,0xec,0x78,0x08,0x00,0x00,0x49,0x89,0xcd,0x48,0x89,0xd3,0x49,0x89,0xf6,0x41,0x89,0xff};
    const uint8_t stores[] = {0x4b,0x89,0x9c,0x2c,0x68,0x55,0x02,0x00,
        0x4b,0x89,0x84,0x2c,0x70,0x55,0x02,0x00,0x48,0x09,0xc3};
    const uint8_t lookup[] = {0x4c,0x8b,0x25,0x53,0xab,0x71,0x10,0x44,0x89,0xff,
        0xe8,0x0c,0x4c,0x09,0x00,0x4c,0x69,0xad,0x78,0xf7,0xff,0xff,0x80,0x57,0x02,0x00};
    memcpy(image + 0xe19402, entry, sizeof(entry));
    memcpy(image + 0xe19ae2, stores, sizeof(stores));
    memcpy(image + 0xe19a46, lookup, sizeof(lookup));
    if (argc == 2) {
        FILE *f = fopen(argv[1], "rb"); assert(f);
        assert(fseek(f, 0, SEEK_END) == 0);
        long length = ftell(f); assert(length > 0xe19af5 && length < (long)image_size);
        rewind(f); assert(fread(image, 1, length, f) == (size_t)length); fclose(f);
    } else assert(argc == 1);
    uint8_t *connections = connection;
    memcpy(image + 0x115345a0, &connections, sizeof(connections));
    image[0xe19402] ^= 1;
    assert(bo3_install_stats_sync((uintptr_t)image) == -1 && !hook_calls);
    image[0xe19402] ^= 1;
    image[0xe19ae2] ^= 1;
    assert(bo3_install_stats_sync((uintptr_t)image) == -1 && !hook_calls);
    image[0xe19ae2] ^= 1;
    image[0xe19a46] ^= 1;
    assert(bo3_install_stats_sync((uintptr_t)image) == -1 && !hook_calls);
    image[0xe19a46] ^= 1;
    fail_hook = 1;
    assert(bo3_install_stats_sync((uintptr_t)image) == -2);
    fail_hook = 0;
    assert(bo3_install_stats_sync((uintptr_t)image) == 1);

    /* Exact 16-chunk symptom: a late old ACK restores bits 15/16 in stock.
       Corrected path keeps bit 15 cleared, then honors the real final ACK. */
    reset(0, 0xffff, 0);
    deliver(0, 0xc000, 0);
    deliver(0, 0x8000, 0);
    deliver(0, 0xc000, 0);
    assert(pending(0)[0] == 0x8000 && *state(0) == 8);
    deliver(0, 0, 0);
    assert(*state(0) == 7 && !pending(0)[0]);
    /* A missing final ACK MUST NOT be manufactured. */
    reset(0, 0x8000, 0);
    for (int i = 0; i < 8; ++i) deliver(0, 0xc000, 0);
    assert(*state(0) == 8 && pending(0)[0] == 0x8000);
    /* Real host missing masks are nested. Reordering cannot manufacture a
       zero mask when no complete acknowledgement arrived. */
    reset(1, UINT64_MAX, UINT64_MAX);
    deliver(1, UINT64_MAX, UINT64_C(1) << 63);
    deliver(1, 0, UINT64_C(1) << 63);
    deliver(1, UINT64_MAX, UINT64_C(1) << 63);
    assert(*state(1) == 8 && !pending(1)[0] && pending(1)[1] == (UINT64_C(1) << 63));
    deliver(1, 0, 0);
    assert(*state(1) == 7 && !pending(1)[0] && !pending(1)[1]);
    /* Fresh map transfer resets the engine's masks. Previous progress does
       not leak into it, and local clients remain independent. */
    reset(0, 0xffff, 0);
    deliver(0, 0xc000, 0);
    assert(pending(0)[0] == 0xc000);
    msg = packet(buffer, 0xffff, 0);
    BO3StatsAddress stranger = server; stranger.ip ^= 1;
    assert(!hook(0, stranger, &msg) && pending(0)[0] == 0xc000);
    stranger = server; stranger.port ^= 1;
    assert(!hook(0, stranger, &msg) && pending(0)[0] == 0xc000);
    reject = true;
    assert(!hook(0, server, &msg) && pending(0)[0] == 0xc000 && *state(0) == 8);
    reject = false;
    *state(0) = 6;
    assert(!hook(0, server, &msg) && *state(0) == 6);
    assert(!hook(-1, server, &msg));
    assert(!hook(2, server, &msg));
    assert(dispatch_calls > 10);
    munmap(image, image_size);
    puts("PASS: reordered stats ACKs, 16-chunk stall, uint64 boundaries, source validation, split clients, fresh transfer, guard and hook failure");
}
