#include "stats_sync.h"
#include "stats_sync_engine.h"
#include "dobby.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint64_t game_base;
static BO3StatsDispatch original_dispatch;
static unsigned stale_count[2];
static unsigned reply_count[2];

static int32_t read_int(const void *address) {
    int32_t value;
    memcpy(&value, address, sizeof(value));
    return value;
}

static bool dispatch(int client, BO3StatsAddress from, BO3StatsMessage *message) {
    uint8_t *connections;
    memcpy(&connections, (const void *)(uintptr_t)(game_base + 0x115345a0), sizeof(connections));
    if (client < 0 || client >= 2 || !connections || !message || message->overflowed ||
        !message->data || message->split_size != 0 || message->current_size < 0 ||
        message->current_size > message->max_size)
        return original_dispatch(client, from, message);

    const uint8_t *active = (const uint8_t *)(uintptr_t)(game_base + 0x115324a0 + client * 0x1078);
    uint8_t *connection = connections + client * 0x25780;
    /* CA_SENDINGDATA is 8, not the earlier CA_LOADING state 6. Never change
       connection state ourselves, and never make a new transfer inherit ACKs. */
    if (read_int(active + 8) != 8)
        return original_dispatch(client, from, message);
    BO3StatsAddress server;
    memcpy(&server, connection + 0x10, sizeof(server));
    if (from.ip != server.ip || from.port != server.port || from.type != server.type)
        return original_dispatch(client, from, message);

    uint64_t pending[2];
    memcpy(pending, connection + 0x25568, sizeof(pending));
    BO3StatsAck ack;
    if (!bo3_stats_ack(message->data, (size_t)message->current_size, pending, &ack))
        return original_dispatch(client, from, message);

    bool result;
    if (ack.stale) {
        /* Keep the engine's entire source/state validation and zero-mask
           transition. Only normalize ACK bits against already acknowledged
           progress; no receipt is invented and no stats data is skipped. */
        uint8_t packet[96];
        memset(packet, 255, 4);
        int size = snprintf((char *)packet + 4, sizeof(packet) - 4,
                            "statresponse %" PRIu64 " %" PRIu64 "\n",
                            ack.missing[0], ack.missing[1]);
        BO3StatsMessage normalized = *message;
        normalized.data = packet;
        normalized.split_data = NULL;
        normalized.max_size = normalized.current_size = size + 4;
        normalized.split_size = normalized.read_count = normalized.bit = 0;
        result = original_dispatch(client, from, &normalized);
        unsigned count = ++stale_count[client];
        if (count <= 4 || (count & (count - 1)) == 0)
            printf("[BO3MacFix-mawlicious stats] Client %d: normalized stale ACK (%u); pending %016" PRIx64 " %016" PRIx64 "\n",
                   client, count, ack.missing[0], ack.missing[1]);
    } else {
        result = original_dispatch(client, from, message);
    }
    uint64_t after[2];
    memcpy(after, connection + 0x25568, sizeof(after));
    unsigned replies = ++reply_count[client];
    if (replies <= 4 || (replies & (replies - 1)) == 0)
        printf("[BO3MacFix-mawlicious stats] Client %d: reply %u host-missing %016" PRIx64 " %016" PRIx64 "; pending %016" PRIx64 " %016" PRIx64 "; state %d\n",
               client, replies, ack.incoming[0], ack.incoming[1], after[0], after[1], read_int(active + 8));
    if ((after[0] != pending[0] || after[1] != pending[1]) &&
        after[0] == ack.missing[0] && after[1] == ack.missing[1])
        printf("[BO3MacFix-mawlicious stats] Client %d: ACK progress %016" PRIx64 " %016" PRIx64 "; state %d\n",
               client, after[0], after[1], read_int(active + 8));
    return result;
}

int bo3_install_stats_sync(uint64_t base) {
    if (!bo3_stats_sync_supported(base)) return -1;
    game_base = base;
    if (DobbyHook((void *)(uintptr_t)(base + 0xe19402), (void *)dispatch,
                  (void **)&original_dispatch) != 0) return -2;
    return 1;
}
