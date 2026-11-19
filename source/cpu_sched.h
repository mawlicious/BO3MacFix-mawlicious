#pragma once
#include <stdint.h>
/* Stop Aspyr's 1 ms WaitForSingleObject loops from busy-spinning.
 * Returns bitmask: 1 = JQ workers fixed, 2 = main render wait fixed; 0 none;
 * negative = unsupported executable. */
int bo3_install_event_wait_fix(uint64_t base);
