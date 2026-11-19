// Every code patch and inline hook in the mod goes through this layer.
//
// Why: Rosetta 2 runs the game from an ahead-of-time (AOT) translation of
// CoDBlkOps3_Exe, but abandons that translation for the *whole image* as soon
// as any page of its __TEXT is made writable — even to rewrite a byte with its
// own value. The game then runs entirely on Rosetta's JIT, which measured ~75%
// more CPU per frame on the render thread. See docs/rosetta-aot.md.
//
// Static mode: the launcher runs CoDBlkOps3_Exe_mawlicious, a copy of the game
// with every patch below already applied on disk (tools/static_patch.py).
// Here we then only verify the baked bytes and fill writable hook slots, so
// nothing touches __TEXT and Rosetta keeps the AOT translation.
//
// Dynamic mode (original executable, or anything not baked): real Dobby.
//
// BO3MACFIX_RECORD_PATCHES=<file> appends every patch inside the game image
// to <file>; tools/static_patch.py turns a recording into the static patch.
#define BO3_PATCH_LAYER_IMPL
#include "patch_layer.h"
#include "dobby.h"
#include "static_manifest.h"
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#include <fcntl.h>
#include <limits.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uintptr_t text_start, text_end, image_base;
static int static_mode = -1; /* -1 unknown, 0 dynamic, 1 static */
static FILE *record_file;
static unsigned fallback_count, baked_hooks, baked_code;
static pthread_mutex_t layer_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t **original_bytes; /* per manifest entry, read from CoDBlkOps3_Exe */

/* The static executable sits next to the untouched Steam executable; read
 * the original bytes of every baked range from it (never shipped by us). */
static void load_original_bytes(void) {
    char exe[PATH_MAX];
    uint32_t size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &size)) return;
    char dir[PATH_MAX];
    strlcpy(dir, exe, sizeof(dir));
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/CoDBlkOps3_Exe", dirname(dir));
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    original_bytes = calloc(BO3_STATIC_ENTRY_COUNT, sizeof(*original_bytes));
    for (unsigned i = 0; original_bytes && i < BO3_STATIC_ENTRY_COUNT; i++) {
        const bo3_static_entry *e = &bo3_static_entries[i];
        original_bytes[i] = malloc(e->length);
        /* non-PIE image: file offset == image offset inside __TEXT */
        if (!original_bytes[i] || pread(fd, original_bytes[i], e->length, e->offset) != (ssize_t)e->length) {
            free(original_bytes[i]);
            original_bytes[i] = NULL;
        }
    }
    close(fd);
}

static void layer_init(void) {
    if (static_mode != -1) return;
    const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(0);
    image_base = (uintptr_t)mh;
    unsigned long size = 0;
    uint8_t *text = getsegmentdata(mh, "__TEXT", &size);
    text_start = (uintptr_t)text;
    text_end = text_start + size;
    static_mode = 0;
    /* The patcher writes a marker naming the manifest it implements. */
    const char *marker = (const char *)(image_base + BO3_STATIC_MARKER_OFFSET);
    if (BO3_STATIC_ENTRY_COUNT > 0 && !memcmp(marker, BO3_STATIC_MARKER, sizeof(BO3_STATIC_MARKER)))
    {
        static_mode = 1;
        for (unsigned i = 0; i < BO3_STATIC_ENTRY_COUNT; i++)
            if (bo3_static_entries[i].kind == BO3_STATIC_HOOK)
                *(void **)(image_base + bo3_static_entries[i].slot) =
                    (void *)(image_base + bo3_static_entries[i].trampoline);
        load_original_bytes();
    }
    const char *rec = getenv("BO3MACFIX_RECORD_PATCHES");
    if (rec && *rec && !static_mode) record_file = fopen(rec, "a");
}

static int in_game_text(const void *addr, size_t len) {
    uintptr_t a = (uintptr_t)addr;
    return a >= text_start && a + len <= text_end;
}

static const bo3_static_entry *find_entry(uint32_t offset, int kind) {
    for (unsigned i = 0; i < BO3_STATIC_ENTRY_COUNT; i++)
        if (bo3_static_entries[i].offset == offset && bo3_static_entries[i].kind == kind)
            return &bo3_static_entries[i];
    return NULL;
}

static void record(const char *kind, const void *addr, const uint8_t *new_bytes, uint32_t len) {
    if (!record_file) return;
    fprintf(record_file, "%s 0x%lx %u", kind, (unsigned long)((uintptr_t)addr - image_base), len);
    if (new_bytes) {
        fputc(' ', record_file);
        for (uint32_t i = 0; i < len; i++) fprintf(record_file, "%02x", new_bytes[i]);
        fputc(' ', record_file);
        for (uint32_t i = 0; i < len; i++) fprintf(record_file, "%02x", ((const uint8_t *)addr)[i]);
    }
    fputc('\n', record_file);
    fflush(record_file);
}

int bo3_code_patch(void *address, uint8_t *buffer, uint32_t size) {
    pthread_mutex_lock(&layer_lock);
    layer_init();
    int result;
    if (!in_game_text(address, size)) {
        result = DobbyCodePatch(address, buffer, size); /* data / other images */
    } else if (static_mode == 1 && !memcmp(address, buffer, size) &&
               find_entry((uint32_t)((uintptr_t)address - image_base), BO3_STATIC_CODE)) {
        baked_code++;
        result = 0; /* already on disk */
    } else {
        if (static_mode == 1) {
            fallback_count++;
            fprintf(stderr, "[BO3MacFix-mawlicious] static patch missing at +0x%lx; runtime patch disables Rosetta AOT\n",
                    (unsigned long)((uintptr_t)address - image_base));
        }
        record("code", address, buffer, size);
        result = DobbyCodePatch(address, buffer, size);
    }
    pthread_mutex_unlock(&layer_lock);
    return result;
}

int bo3_hook(void *address, void *replacement, void **original) {
    pthread_mutex_lock(&layer_lock);
    layer_init();
    int result;
    const bo3_static_entry *e = NULL;
    if (in_game_text(address, 5) && static_mode == 1)
        e = find_entry((uint32_t)((uintptr_t)address - image_base), BO3_STATIC_HOOK);
    if (e) {
        /* Site already jumps through the slot; the trampoline runs the
         * relocated original prologue and returns to the function body. */
        void **slot = (void **)(image_base + e->slot);
        if (original) *original = (void *)(image_base + e->trampoline);
        __atomic_store_n(slot, replacement, __ATOMIC_RELEASE);
        baked_hooks++;
        result = 0;
    } else {
        if (in_game_text(address, 5)) {
            if (static_mode == 1) {
                fallback_count++;
                fprintf(stderr, "[BO3MacFix-mawlicious] static hook missing at +0x%lx; runtime hook disables Rosetta AOT\n",
                        (unsigned long)((uintptr_t)address - image_base));
            }
            record("hook", address, NULL, 0);
        }
        result = DobbyHook(address, replacement, original);
    }
    pthread_mutex_unlock(&layer_lock);
    return result;
}

int bo3_orig_memcmp(const void *address, const void *expected, size_t size) {
    pthread_mutex_lock(&layer_lock);
    layer_init();
    int mode = static_mode;
    pthread_mutex_unlock(&layer_lock);
    if (mode != 1 || !in_game_text(address, size)) return memcmp(address, expected, size);
    /* Overlay the original bytes of every baked range on a copy. */
    uint8_t stackbuf[256];
    uint8_t *copy = size <= sizeof(stackbuf) ? stackbuf : malloc(size);
    if (!copy) return memcmp(address, expected, size);
    memcpy(copy, address, size);
    uintptr_t lo = (uintptr_t)address - image_base, hi = lo + size;
    int r_unknown = 0;
    for (unsigned i = 0; i < BO3_STATIC_ENTRY_COUNT; i++) {
        const bo3_static_entry *e = &bo3_static_entries[i];
        uintptr_t s = e->offset, t = (uintptr_t)e->offset + e->length;
        if (t <= lo || s >= hi) continue;
        uintptr_t from = s > lo ? s : lo, to = t < hi ? t : hi;
        if (!original_bytes || !original_bytes[i]) { r_unknown = 1; continue; }
        memcpy(copy + (from - lo), original_bytes[i] + (from - s), to - from);
    }
    int r = r_unknown ? 1 : memcmp(copy, expected, size); /* fail closed */
    if (copy != stackbuf) free(copy);
    return r;
}

int bo3_patch_layer_status(unsigned *hooks, unsigned *code, unsigned *fallbacks) {
    pthread_mutex_lock(&layer_lock);
    layer_init();
    if (hooks) *hooks = baked_hooks;
    if (code) *code = baked_code;
    if (fallbacks) *fallbacks = fallback_count;
    int mode = static_mode;
    pthread_mutex_unlock(&layer_lock);
    return mode;
}
