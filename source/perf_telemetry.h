#pragma once
/* Frame pacing / CPU / footprint summaries in the session log.
 * Returns 1 when installed, 0 when disabled, negative on failure. */
int bo3_install_perf_telemetry(void);
