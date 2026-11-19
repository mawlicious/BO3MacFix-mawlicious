#pragma once
/* Opt-in (BO3MACFIX_METALSTATS=1) per-frame Metal call statistics. */
int bo3_install_metal_stats(void);
void bo3_metal_stats_frame(void);
int bo3_install_render_thread_qos(void);
