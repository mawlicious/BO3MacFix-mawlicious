#pragma once
#include <stddef.h>
#include <stdint.h>
/* All game code patches/hooks go through these (see patch_layer.c). */
int bo3_code_patch(void *address, uint8_t *buffer, uint32_t size);
int bo3_hook(void *address, void *replacement, void **original);
/* memcmp against the game's ORIGINAL bytes, also when running the
 * pre-patched static executable. Use for every signature check of game code. */
#ifdef BO3_USE_PATCH_LAYER
int bo3_orig_memcmp(const void *address, const void *expected, size_t size);
#else
#include <string.h>
#define bo3_orig_memcmp memcmp
#endif
/* Returns -1 uninitialised, 0 dynamic (runtime Dobby), 1 static (baked). */
int bo3_patch_layer_status(unsigned *hooks, unsigned *code, unsigned *fallbacks);
