#include "nav_convert.h"
#include "codhavoktypes.h"
#include "havok_convert.h"
#include "havok_schema.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { const void *data; int size; } NavBuffer;

static int write_buffers(FILE *file, const NavBuffer *buffers, size_t count,
                         char *error, size_t error_size) {
    for (size_t i = 0; i < count; i++) {
        if (buffers[i].size == 0) continue;
        if (buffers[i].size < 0 || !buffers[i].data) {
            snprintf(error, error_size, "Invalid navigation buffer %zu", i);
            return 0;
        }
        void *output = NULL;
        size_t output_size = 0;
        if (!hkconv_convert(buffers[i].data, (size_t)buffers[i].size,
                            hkconv_schema_classes, HKCONV_SCHEMA_CLASS_COUNT,
                            &output, &output_size, error, error_size))
            return 0;
        int ok = fwrite(output, 1, output_size, file) == output_size;
        hkconv_free(output);
        if (!ok) {
            snprintf(error, error_size, "Writing navigation data: %s", strerror(errno));
            return 0;
        }
    }
    return 1;
}

static int temp_file(const char *target, char *path, size_t path_size,
                     FILE **file, char *error, size_t error_size) {
    if (snprintf(path, path_size, "%s.XXXXXX", target) >= (int)path_size) {
        snprintf(error, error_size, "Navigation output path is too long");
        return 0;
    }
    int fd = mkstemp(path);
    if (fd >= 0) {
        *file = fdopen(fd, "wb");
        if (*file) return 1;
        close(fd);
        unlink(path);
    }
    snprintf(error, error_size, "Creating navigation output: %s", strerror(errno));
    return 0;
}

int bo3_convert_nav(const NavMeshData *mesh, const NavVolumeData *volume,
                    const char *prefix, char *error, size_t error_size) {
    if (!mesh || !prefix || !error || !error_size) return 0;
    error[0] = 0;
    NavBuffer mesh_buffers[] = {
        {mesh->navMeshSettingsBuffer, mesh->navMeshSettingsBufferSize},
        {mesh->userEdgePairsBuffer, mesh->userEdgePairsBufferSize},
        {mesh->clusterGraphBuffer, mesh->clusterGraphBufferSize},
        {mesh->mediatorBuffer, mesh->mediatorBufferSize},
        {mesh->metadataBuffer, mesh->metadataBufferSize},
        {mesh->clearanceCacheSeederBuffer, mesh->clearanceCacheSeederBufferSize},
    };
    if (!mesh->mediatorBuffer || mesh->mediatorBufferSize <= 0) {
        snprintf(error, error_size, "Navigation mesh has no mediator data");
        return 0;
    }
    char mesh_path[1024], volume_path[1024], mesh_temp[1040] = {0}, volume_temp[1040] = {0};
    if (snprintf(mesh_path, sizeof(mesh_path), "%s_navmesh.hkt", prefix) >= (int)sizeof(mesh_path) ||
        snprintf(volume_path, sizeof(volume_path), "%s_navvolume.hkt", prefix) >= (int)sizeof(volume_path)) {
        snprintf(error, error_size, "Navigation output path is too long");
        return 0;
    }
    FILE *file = NULL;
    if (!temp_file(mesh_path, mesh_temp, sizeof(mesh_temp), &file, error, error_size)) return 0;
    int ok = write_buffers(file, mesh_buffers, sizeof(mesh_buffers) / sizeof(mesh_buffers[0]), error, error_size);
    if (fclose(file) && ok) {
        snprintf(error, error_size, "Closing navigation output: %s", strerror(errno));
        ok = 0;
    }
    if (!ok) goto cleanup;
    if (volume) {
        NavBuffer volume_buffers[] = {
            {volume->smallNavVolumeSettingsBuffer, volume->smallNavVolumeSettingsBufferSize},
            {volume->smallNavVolumeMediatorBuffer, volume->smallNavVolumeMediatorBufferSize},
            {volume->bigNavVolumeSettingsBuffer, volume->bigNavVolumeSettingsBufferSize},
            {volume->bigNavVolumeMediatorBuffer, volume->bigNavVolumeMediatorBufferSize},
        };
        if (!temp_file(volume_path, volume_temp, sizeof(volume_temp), &file, error, error_size)) { ok = 0; goto cleanup; }
        ok = write_buffers(file, volume_buffers, sizeof(volume_buffers) / sizeof(volume_buffers[0]), error, error_size);
        if (fclose(file) && ok) {
            snprintf(error, error_size, "Closing navigation output: %s", strerror(errno));
            ok = 0;
        }
        if (!ok) goto cleanup;
        if (rename(volume_temp, volume_path)) goto rename_failed;
        volume_temp[0] = 0;
    }
    if (rename(mesh_temp, mesh_path)) goto rename_failed;
    mesh_temp[0] = 0;
    goto cleanup;
rename_failed:
    snprintf(error, error_size, "Committing navigation output: %s", strerror(errno));
    ok = 0;
cleanup:
    if (mesh_temp[0]) unlink(mesh_temp);
    if (volume_temp[0]) unlink(volume_temp);
    return ok;
}
