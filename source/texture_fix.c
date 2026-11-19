// Native BC formats for 3D textures.
//
// Aspyr's D3D11 layer maps DXGI formats to Metal pixel formats in two
// functions: 0xd8630 for most textures and 0xd8a98 for 3D textures. For 3D
// textures, 0xd8a98 replaces every BC format (BC1-BC7) with an uncompressed
// one, probably because the Intel and AMD GPUs of the time could not use BC
// formats in 3D textures. Each upload is then decoded on the CPU
// (0xd8bf7 -> 0xd7426 for BC6H) into RGBA16Float, 8 bytes per pixel instead
// of 1, and Apple's driver compresses the result again (lossless
// framebuffer compression) inside -replaceRegion:, also on the CPU.
//
// Shadows of Evil streams its BC6H reflection probe volumes
// (volume0_state0_reflection_probes_60f6e17c) in and out as the player moves.
// Each load decoded on a job worker under Rosetta while the main thread
// waited on the device lock: 2.5-4 s freezes in the freeze log, mid-round.
//
// Apple GPUs sample BC formats in 3D textures natively. A GPU test
// (BC1/2/3/3-sRGB/4/5/6H-U/6H-S/7, five sizes up to 128x128x32, arm64 and
// x86_64 under Rosetta on an M4) read back bit-identical texels from a 3D
// texture and from a 2D array holding the same blocks. Under Rosetta (Apple
// silicon) 0xd8a98 now returns 0xd8630's mapping, so 3D textures keep their
// BC format and their uploads are plain copies. Uncompressed formats map the
// same either way. BO3MACFIX_BC3D=0 restores Aspyr's behaviour.
#include "texture_fix.h"
#include "dobby.h"
#include "patch_layer.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include "versioninfo.h"
#define Version_Prefix "[BO3MacFix-mawlicious " MACFIX_VERSION "]"

typedef bool (*format_map_fn)(uint32_t dxgi_format, int64_t *mtl_format, uint8_t *needs_conversion);
static format_map_fn native_map, original_map3d;
static unsigned logged;

static int is_bc(uint32_t f) { return (f >= 0x46 && f <= 0x54) || (f >= 0x5e && f <= 0x63); }

static bool map3d(uint32_t dxgi_format, int64_t *mtl_format, uint8_t *needs_conversion) {
    bool ok = native_map(dxgi_format, mtl_format, needs_conversion);
    if (is_bc(dxgi_format) && __atomic_fetch_add(&logged, 1, __ATOMIC_RELAXED) < 4) {
        int64_t mtl = 0;
        native_map(dxgi_format, &mtl, NULL);
        printf(Version_Prefix " 3D texture kept in BC format: DXGI %u -> Metal %lld.\n", dxgi_format, (long long)mtl);
    }
    return ok;
}

static int rosetta(void) {
    int translated = 0;
    size_t size = sizeof(translated);
    return sysctlbyname("sysctl.proc_translated", &translated, &size, NULL, 0) == 0 && translated == 1;
}

int bo3_install_bc3d_fix(uint64_t base) {
    /* Both mapping functions: prologue plus argument setup. */
    static const uint8_t map3d_sig[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83,
                                        0xec, 0x18, 0x49, 0x89, 0xd6, 0x49, 0x89, 0xf7, 0x89, 0xfb};
    static const uint8_t map_sig[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x50, 0x49, 0x89,
                                      0xd6, 0x49, 0x89, 0xf7, 0x89, 0xfb, 0xe8, 0x23, 0xa7, 0x01, 0x00};
    void *map3d_addr = (void *)(uintptr_t)(base + 0xd8a98);
    void *map_addr = (void *)(uintptr_t)(base + 0xd8630);
    if (bo3_orig_memcmp(map3d_addr, map3d_sig, sizeof(map3d_sig)) || bo3_orig_memcmp(map_addr, map_sig, sizeof(map_sig)))
        return -1;
    const char *v = getenv("BO3MACFIX_BC3D");
    if ((v && !strcmp(v, "0")) || !rosetta()) return 0;
    native_map = (format_map_fn)map_addr;
    return DobbyHook(map3d_addr, (void *)map3d, (void **)&original_map3d) == 0 ? 1 : -2;
}
