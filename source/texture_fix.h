#ifndef BO3MACFIX_TEXTURE_FIX_H
#define BO3MACFIX_TEXTURE_FIX_H
#include <stdint.h>

/* Keep BC-compressed 3D textures compressed on Apple GPUs (see texture_fix.c).
 * 1: installed, 0: disabled (BO3MACFIX_BC3D=0 or not Apple silicon),
 * -1: unsupported executable, -2: hook failed. */
int bo3_install_bc3d_fix(uint64_t base);
#endif
