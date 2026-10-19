/* Local integration driver. Proprietary test inputs stay outside the repo. */
#include "codhavoktypes.h"
#include "nav_convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if ((argc >= 4 && argc <= 6) && !strcmp(argv[1], "--check")) {
        NavMeshData mesh = {0}; NavVolumeData volume = {0};
        mesh.mediatorBuffer = (uint8_t *)(argc >= 5 ? argv[4] : "source mesh");
        mesh.mediatorBufferSize = (int)strlen((char *)mesh.mediatorBuffer);
        volume.smallNavVolumeSettingsBuffer = (uint8_t *)(argc == 6 ? argv[5] : "source volume");
        volume.smallNavVolumeSettingsBufferSize = (int)strlen((char *)volume.smallNavVolumeSettingsBuffer);
        return bo3_nav_cache_valid(argv[2], &mesh, atoi(argv[3]) ? &volume : NULL) ? 0 : 1;
    }
    int check = argc == 4 && !strcmp(argv[1], "--check-clump");
    if (check) { argc--; argv++; }
    if (argc != 3) { fprintf(stderr, "usage: convert_clump input.clump output-prefix\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("input"); return 1; }
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 1; }
    long length = ftell(f);
    if (length < (long)sizeof(clumpheader) || length > 512 * 1024 * 1024) { fclose(f); return 1; }
    rewind(f);
    unsigned char *data = malloc((size_t)length);
    if (!data) { fclose(f); return 1; }
    int ok = fread(data, 1, (size_t)length, f) == (size_t)length;
    fclose(f);
    if (!ok) { free(data); return 1; }
    clumpheader header;
    memcpy(&header, data, sizeof(header));
    int sizes[10] = {
        header.navMeshSettingsBufferSize, header.userEdgePairsBufferSize,
        header.clusterGraphBufferSize, header.mediatorBufferSize,
        header.metadataBufferSize, header.clearanceCacheSeederBufferSize,
        header.smallNavVolumeSettingsBufferSize, header.smallNavVolumeMediatorBufferSize,
        header.bigNavVolumeSettingsBufferSize, header.bigNavVolumeMediatorBufferSize,
    };
    unsigned char *buffers[10] = {0};
    size_t offset = sizeof(header);
    for (int i = 0; i < 10; i++) {
        if (sizes[i] < 0 || (size_t)sizes[i] > (size_t)length - offset) { free(data); return 1; }
        buffers[i] = data + offset;
        offset += (size_t)sizes[i];
    }
    if (offset != (size_t)length || (header.bool_hasNavVolume != 0 && header.bool_hasNavVolume != 1)) { free(data); return 1; }
    NavMeshData mesh = {0}; NavVolumeData volume = {0};
#define FIELD(obj, field, i) obj.field##Buffer = buffers[i]; obj.field##BufferSize = sizes[i]
    FIELD(mesh, navMeshSettings, 0); FIELD(mesh, userEdgePairs, 1);
    FIELD(mesh, clusterGraph, 2); FIELD(mesh, mediator, 3);
    FIELD(mesh, metadata, 4); FIELD(mesh, clearanceCacheSeeder, 5);
    FIELD(volume, smallNavVolumeSettings, 6); FIELD(volume, smallNavVolumeMediator, 7);
    FIELD(volume, bigNavVolumeSettings, 8); FIELD(volume, bigNavVolumeMediator, 9);
#undef FIELD
    char error[256];
    if (check) {
        ok = bo3_nav_cache_valid(argv[2], &mesh, header.bool_hasNavVolume ? &volume : NULL);
        free(data);
        return ok ? 0 : 1;
    }
    ok = bo3_convert_nav(&mesh, header.bool_hasNavVolume ? &volume : NULL, argv[2], error, sizeof(error));
    if (!ok) fprintf(stderr, "%s\n", error);
    free(data);
    return ok ? 0 : 1;
}
