#ifndef BO3MACFIX_SCRIPT_FIXES_H
#define BO3MACFIX_SCRIPT_FIXES_H
#include <stddef.h>

// Returns 1 when patched, 0 for other/already-patched scripts, -1 for an
// unsupported revision of the named script. Never edits an unknown revision.
int fix_roulette_callbacks(const char *name, void *data, size_t size);
#endif
