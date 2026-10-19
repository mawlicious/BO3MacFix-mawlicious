#include "nav_convert.h"
#include "codhavoktypes.h"
#include "havok_convert.h"
#include "havok_schema.h"
#include <errno.h>
#include <CommonCrypto/CommonDigest.h>
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

/* A receipt is created only after conversion commits both files. Legacy caches
 * have no receipt and are regenerated rather than trusting a partial packfile.
 * Hashes also detect subsequent truncation or corruption of either output. */
static int file_digest(const char *path, unsigned char digest[CC_SHA256_DIGEST_LENGTH]) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    unsigned char buffer[16384];
    size_t count, total = 0;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        CC_SHA256_Update(&ctx, buffer, (CC_LONG)count);
        total += count;
    }
    int ok = !ferror(file) && total > 0;
    fclose(file);
    CC_SHA256_Final(digest, &ctx);
    return ok;
}

static int source_digest(const NavMeshData *mesh, const NavVolumeData *volume,
                         unsigned char digest[CC_SHA256_DIGEST_LENGTH]) {
    if (!mesh || !mesh->mediatorBuffer || mesh->mediatorBufferSize <= 0) return 0;
    NavBuffer buffers[] = {
        {mesh->navMeshSettingsBuffer, mesh->navMeshSettingsBufferSize},
        {mesh->userEdgePairsBuffer, mesh->userEdgePairsBufferSize},
        {mesh->clusterGraphBuffer, mesh->clusterGraphBufferSize},
        {mesh->mediatorBuffer, mesh->mediatorBufferSize},
        {mesh->metadataBuffer, mesh->metadataBufferSize},
        {mesh->clearanceCacheSeederBuffer, mesh->clearanceCacheSeederBufferSize},
        {volume ? volume->smallNavVolumeSettingsBuffer : NULL, volume ? volume->smallNavVolumeSettingsBufferSize : 0},
        {volume ? volume->smallNavVolumeMediatorBuffer : NULL, volume ? volume->smallNavVolumeMediatorBufferSize : 0},
        {volume ? volume->bigNavVolumeSettingsBuffer : NULL, volume ? volume->bigNavVolumeSettingsBufferSize : 0},
        {volume ? volume->bigNavVolumeMediatorBuffer : NULL, volume ? volume->bigNavVolumeMediatorBufferSize : 0},
    };
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    for (size_t i = 0; i < sizeof(buffers) / sizeof(buffers[0]); i++) {
        int size = buffers[i].size;
        if (size < 0 || size > 512 * 1024 * 1024 || (size && !buffers[i].data)) return 0;
        unsigned char length[4] = {(unsigned char)size, (unsigned char)((unsigned)size >> 8),
                                  (unsigned char)((unsigned)size >> 16), (unsigned char)((unsigned)size >> 24)};
        CC_SHA256_Update(&ctx, length, sizeof(length));
        if (size) CC_SHA256_Update(&ctx, buffers[i].data, (CC_LONG)size);
    }
    CC_SHA256_Final(digest, &ctx);
    return 1;
}

static int cache_receipt(const char *prefix, const NavMeshData *mesh,
                         const NavVolumeData *volume, unsigned char receipt[105]) {
    memcpy(receipt, "BO3NAV02", 8);
    receipt[8] = volume != NULL;
    if (!source_digest(mesh, volume, receipt + 73)) return 0;
    memset(receipt + 9, 0, 64);
    char path[1040];
    if (snprintf(path, sizeof(path), "%s_navmesh.hkt", prefix) >= (int)sizeof(path) ||
        !file_digest(path, receipt + 9)) return 0;
    if (volume &&
        (snprintf(path, sizeof(path), "%s_navvolume.hkt", prefix) >= (int)sizeof(path) ||
         !file_digest(path, receipt + 41))) return 0;
    return 1;
}

int bo3_nav_cache_valid(const char *prefix, const NavMeshData *mesh, const NavVolumeData *volume) {
    if (!prefix) return 0;
    char path[1040];
    if (snprintf(path, sizeof(path), "%s_native.receipt", prefix) >= (int)sizeof(path)) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    unsigned char saved[105], actual[105];
    int ok = fread(saved, 1, sizeof(saved), file) == sizeof(saved) && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    return ok && cache_receipt(prefix, mesh, volume, actual) && !memcmp(saved, actual, sizeof(saved));
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
    char receipt_path[1040], receipt_temp[1060] = {0};
    if (snprintf(receipt_path, sizeof(receipt_path), "%s_native.receipt", prefix) >= (int)sizeof(receipt_path)) {
        snprintf(error, error_size, "Navigation receipt path is too long");
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
        if (unlink(receipt_path) && errno != ENOENT) goto rename_failed;
        if (rename(volume_temp, volume_path)) goto rename_failed;
        volume_temp[0] = 0;
    }
    /* Remove an old receipt before replacing either member of the pair. */
    if (unlink(receipt_path) && errno != ENOENT) goto rename_failed;
    if (rename(mesh_temp, mesh_path)) goto rename_failed;
    mesh_temp[0] = 0;
    unsigned char receipt[105];
    if (!cache_receipt(prefix, mesh, volume, receipt)) {
        snprintf(error, error_size, "Reading committed navigation output for receipt");
        ok = 0;
        goto cleanup;
    }
    if (!temp_file(receipt_path, receipt_temp, sizeof(receipt_temp), &file, error, error_size)) {
        ok = 0;
        goto cleanup;
    }
    ok = fwrite(receipt, 1, sizeof(receipt), file) == sizeof(receipt);
    if (fclose(file)) ok = 0;
    if (!ok) {
        snprintf(error, error_size, "Writing navigation completion receipt: %s", strerror(errno));
        goto cleanup;
    }
    if (rename(receipt_temp, receipt_path)) goto rename_failed;
    receipt_temp[0] = 0;
    goto cleanup;
rename_failed:
    snprintf(error, error_size, "Committing navigation output: %s", strerror(errno));
    ok = 0;
cleanup:
    if (receipt_temp[0]) unlink(receipt_temp);
    if (mesh_temp[0]) unlink(mesh_temp);
    if (volume_temp[0]) unlink(volume_temp);
    return ok;
}
