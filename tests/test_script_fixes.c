#include "script_fixes.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *name = "scripts/zm/zm_roulettev2.gsc";
    assert(fix_roulette_callbacks(NULL, NULL, 0) == 0);
    assert(fix_roulette_callbacks("scripts/zm/other.gsc", NULL, 0) == 0);
    assert(fix_roulette_callbacks(name, NULL, 263084) == -1);
    unsigned char *data = calloc(263084, 1), *before = malloc(263084);
    assert(data && before);
    memcpy(before, data, 263084);
    assert(fix_roulette_callbacks(name, data, 263083) == -1);
    assert(fix_roulette_callbacks(name, data, 263084) == -1);
    assert(memcmp(before, data, 263084) == 0);
    const size_t sites[] = {0x18e00, 0x18eb8, 0x18fb4, 0x1b234};
    const unsigned char original_ops[][2] = {{0xf9, 0x27}, {0xe7, 0x21}, {0xa7, 0x34}, {0x17, 0x27}};
    for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++)
        memcpy(data + sites[i], original_ops[i], 2);
    memcpy(before, data, 263084);
    // Correct instructions alone cannot authorize a patch on unknown content.
    assert(fix_roulette_callbacks(name, data, 263084) == -1);
    assert(memcmp(before, data, 263084) == 0);

    // Optional private fixture from the installed Workshop map. It is never
    // distributed with the source or release.
    if (argc == 2) {
        FILE *file = fopen(argv[1], "rb");
        assert(file);
        assert(fread(data, 1, 263084, file) == 263084);
        assert(fgetc(file) == EOF);
        fclose(file);
        memcpy(before, data, 263084);
        // A change outside the edit sites must invalidate the entire patch.
        data[100] ^= 1;
        unsigned char *modified = malloc(263084);
        assert(modified);
        memcpy(modified, data, 263084);
        assert(fix_roulette_callbacks(name, data, 263084) == -1);
        assert(memcmp(data, modified, 263084) == 0);
        free(modified);
        memcpy(data, before, 263084);
        assert(fix_roulette_callbacks(name, data, 263084) == 1);
        for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++) {
            before[sites[i]] = 0x6c;
            before[sites[i] + 1] = 0;
        }
        assert(memcmp(before, data, 263084) == 0);
        assert(fix_roulette_callbacks(name, data, 263084) == 0);
        // The game links imports in the buffer after its first lookup. A later
        // lookup of an already-patched buffer must not edit it or report that
        // the map update was unsupported merely because linking changed bytes.
        data[100] ^= 1;
        before[100] ^= 1;
        assert(fix_roulette_callbacks(name, data, 263084) == 0);
        assert(memcmp(before, data, 263084) == 0);
    }
    free(before);
    free(data);
    puts("script fix tests passed");
}
