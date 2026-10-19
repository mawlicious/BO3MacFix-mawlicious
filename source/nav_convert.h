#ifndef BO3MACFIX_NAV_CONVERT_H
#define BO3MACFIX_NAV_CONVERT_H
#include <stddef.h>
struct _NavMeshData;
struct _NavVolumeData;

/* Writes the same concatenated packfiles consumed by the Mac game. The
 * navmesh is committed last, so a failed conversion is retried next launch. */
/* Only reuse files covered by a matching native completion receipt. */
int bo3_nav_cache_valid(const char *output_prefix,
                        const struct _NavMeshData *mesh,
                        const struct _NavVolumeData *volume);

int bo3_convert_nav(const struct _NavMeshData *mesh,
                    const struct _NavVolumeData *volume,
                    const char *output_prefix, char *error, size_t error_size);
#endif
