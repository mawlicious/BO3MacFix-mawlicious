#ifndef BO3MACFIX_STATS_SYNC_ENGINE_H
#define BO3MACFIX_STATS_SYNC_ENGINE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Aspyr x86_64 ABI: NetAddress is passed by value in two integer registers. */
typedef struct {
    uint32_t ip;
    uint16_t port, padding;
    int32_t type, local_net_id;
} BO3StatsAddress;
typedef struct {
    int32_t overflowed, read_only;
    uint8_t *data, *split_data;
    int32_t max_size, current_size, split_size, read_count;
    int32_t bit, last_entity_ref, flush, target_local_net_id;
} BO3StatsMessage;
typedef bool (*BO3StatsDispatch)(int, BO3StatsAddress, BO3StatsMessage *);
_Static_assert(sizeof(BO3StatsAddress) == 16, "NetAddress ABI");
_Static_assert(sizeof(BO3StatsMessage) == 0x38, "Message ABI");
_Static_assert(offsetof(BO3StatsMessage, current_size) == 0x1c, "Message length ABI");
#endif
