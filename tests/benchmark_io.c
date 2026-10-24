/* Optional local benchmark: benchmark-io archive-path buffer-bytes.
 * Requires a readable archive of at least 128 MiB. Does not modify the file. */
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    char *end;
    unsigned long buffer = strtoul(argv[2], &end, 10);
    if (*end || !buffer || buffer > 16 * 1024 * 1024) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) { perror("fopen"); return 1; }
    if (setvbuf(file, NULL, _IOFBF, buffer)) { fclose(file); return 1; }
    unsigned char data[65536];
    uint64_t hash = 0, bytes = 0;
    double start = now();
    /* Deterministic seeks cover a 128 MiB window four times. Include sparse
     * byte samples in the hash without making hashing dominate the I/O time. */
    for (unsigned i = 0; i < 8192; i++) {
        if (fseeko(file, (off_t)((i * 7919ULL) % 2048) * 65536, SEEK_SET) ||
            fread(data, 1, sizeof(data), file) != sizeof(data)) {
            fprintf(stderr, "Archive seek/read failed (file must be >=128 MiB)\n");
            fclose(file);
            return 1;
        }
        for (size_t j = 0; j < sizeof(data); j += 4096) hash = hash * 33 + data[j];
        bytes += sizeof(data);
    }
    printf("buffer=%lu bytes=%llu hash=%llx seconds=%.3f\n", buffer,
           (unsigned long long)bytes, (unsigned long long)hash, now() - start);
    fclose(file);
    return 0;
}
