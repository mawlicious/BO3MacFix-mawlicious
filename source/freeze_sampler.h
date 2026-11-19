#pragma once
#include <stdint.h>
/* Watchdog that samples game thread stacks when frames stop for >400 ms.
 * last_frame_ns: the frame clock updated on every presented frame. */
int bo3_install_freeze_sampler(uint64_t *last_frame_ns); /* off unless BO3MACFIX_FREEZELOG=1 */
