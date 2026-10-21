#include "stats_sync.h"
#include "stats_sync_engine.h"
#include "dobby.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint64_t game_base;
static BO3StatsDispatch original_dispatch;
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
    /* CA_SENDINGDATA is 8, not the earlier CA_LOADING state 6. Observe only; let the engine
       retain all source checks, mask replacement and state transitions. */
    if (read_int(active + 8) != 8)
        return original_dispatch(client, from, message);
    BO3StatsAddress server;
    memcpy(&server, connection + 0x10, sizeof(server));
    if (from.ip != server.ip || from.port != server.port || from.type != server.type)
        return original_dispatch(client, from, message);

    uint64_t pending[2];
    memcpy(pending, connection + 0x25568, sizeof(pending));
    BO3StatsAck ack;
    if (!bo3_stats_ack(message->data, (size_t)message->current_size, &ack))
        return original_dispatch(client, from, message);

    /* Replies carry no transfer identifier. Preserve stock replacement even
       when bits reappear: this can be an authoritative reply for a new transfer
       following a delayed reply from the previous transfer to the same host. */
    bool result = original_dispatch(client, from, message);
    /* Rejected packets must not consume accepted-reply samples or appear
       as host ACK evidence in diagnostics. */
    if (!result) return result;
    uint64_t after[2];
    memcpy(after, connection + 0x25568, sizeof(after));
    unsigned replies = ++reply_count[client];
    if (replies <= 4 || (replies & (replies - 1)) == 0)
        printf("[BO3MacFix-mawlicious stats] Client %d: reply %u host-missing %016" PRIx64 " %016" PRIx64 "; pending %016" PRIx64 " %016" PRIx64 "; state %d\n",
               client, replies, ack.incoming[0], ack.incoming[1], after[0], after[1], read_int(active + 8));
    if ((after[0] != pending[0] || after[1] != pending[1]) &&
        after[0] == ack.incoming[0] && after[1] == ack.incoming[1])
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
