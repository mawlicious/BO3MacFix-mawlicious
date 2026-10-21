#ifndef BO3MACFIX_STATS_SYNC_H
#define BO3MACFIX_STATS_SYNC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A statresponse carries two decimal uint64 missing-packet masks. There is
   no transfer identifier, so observations must not accumulate across replies. */
typedef struct {
    uint64_t incoming[2];
} BO3StatsAck;

bool bo3_stats_ack(const uint8_t *packet, size_t length, BO3StatsAck *result);
/* Returns 1 for supported instructions, 0 otherwise. Read-only. */
int bo3_stats_sync_supported(uint64_t base);
/* 1 installed, -1 unsupported instructions, -2 hook failure. */
int bo3_install_stats_sync(uint64_t base);
#endif
