/* Synthetic packfile benchmark; contains no game data.
 * Pass payload MiB (default 8). All payload bytes are checked after conversion. */
#include "havok_convert.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void w32(unsigned char *p, uint32_t v) { memcpy(p, &v, 4); }

static void section(unsigned char *p, const char *name, uint32_t start,
                    uint32_t local, uint32_t global, uint32_t virt, uint32_t end) {
    memset(p, 255, 64);
    strcpy((char *)p, name);
    w32(p + 20, start);
    w32(p + 24, local);
    w32(p + 28, global);
    w32(p + 32, virt);
    w32(p + 36, end);
    w32(p + 40, end);
    w32(p + 44, end);
}

int main(int argc, char **argv) {
    char *end = NULL;
    unsigned long mib = argc == 2 ? strtoul(argv[1], &end, 10) : 8;
    if (argc > 2 || !mib || mib > 128 || (end && *end)) return 2;
    size_t bytes = mib * 1024 * 1024;
    size_t local = 32 + bytes, total = 272 + local + 32;
    unsigned char *in = malloc(total);
    if (!in) return 1;
    memset(in, 255, total);
    memset(in, 0, 64);
    w32(in, 0x57e0e057);
    w32(in + 4, 0x10c0c010);
    w32(in + 12, 11);
    memcpy(in + 16, "\x08\x01\x00\x01", 4);
    w32(in + 20, 3);
    w32(in + 24, 2);
    w32(in + 32, 0);
    w32(in + 36, 5);
    memcpy(in + 40, "hk_2014.2.0-r1", 14);
    memset(in + 60, 255, 4);
    section(in + 64, "__classnames__", 256, 16, 16, 16, 16);
    section(in + 128, "__types__", 272, 0, 0, 0, 0);
    section(in + 192, "__data__", 272, local, local + 16, local + 16, local + 32);
    w32(in + 256, 0x12345678);
    in[260] = 9;
    memcpy(in + 261, "Large", 6);
    memset(in + 272, 0, 32);
    w32(in + 272 + 24, bytes / 16);
    w32(in + 272 + 28, 0x80000000u | (bytes / 16));
    for (size_t i = 0; i < bytes; i++) in[304 + i] = (unsigned char)(i * 37 + 11);
    w32(in + 272 + local, 16);
    w32(in + 272 + local + 4, 32);
    w32(in + 272 + local + 16, 0);
    w32(in + 272 + local + 20, 0);
    w32(in + 272 + local + 24, 5);
    hkconv_member member = {
        .source_offset = 16, .target_offset = 16, .kind = HKCONV_ARRAY,
        .element_kind = HKCONV_BYTES, .element_size = 16
    };
    hkconv_class cls = {"Large", 32, 32, 8, 8, &member, 1};
    void *out = NULL;
    size_t n = 0;
    char error[256];
    if (!hkconv_convert(in, total, &cls, 1, &out, &n, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        free(in);
        return 1;
    }
    uint32_t start;
    memcpy(&start, (unsigned char *)out + 212, 4);
    if (n != total || start > n || n - start < 32 + bytes ||
        memcmp((unsigned char *)out + start + 32, in + 304, bytes)) {
        hkconv_free(out);
        free(in);
        return 1;
    }
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < n; i++)
        hash = (hash ^ ((unsigned char *)out)[i]) * 1099511628211ull;
    printf("input=%zu output=%zu hash=%016llx\n", total, n, (unsigned long long)hash);
    hkconv_free(out);
    free(in);
    return 0;
}
