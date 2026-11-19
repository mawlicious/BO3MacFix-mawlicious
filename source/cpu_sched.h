#pragma once
#include <stdint.h>
/* Stop Aspyr's 1 ms WaitForSingleObject loops from busy-spinning.
 * Returns bitmask: 1 = JQ workers fixed, 2 = main render wait fixed,
 * 4 = device-context lock spin removed, 8 = render thread idle wait fixed; 0 none;
 * negative = unsupported executable. */
int bo3_install_event_wait_fix(uint64_t base);
/* QoS experiment helpers (BO3MACFIX_QOS=1). */
int bo3_qos_enabled(void);
int bo3_promote_current_thread(void); /* 0 ok, -1 disabled, else errno */
