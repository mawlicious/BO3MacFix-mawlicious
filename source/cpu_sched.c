// CPU scheduling fixes for Aspyr's Win32 emulation layer.
//
// Aspyr's WaitForSingleObject for Events (image offset 0x111242) never blocks
// when the timeout is 0 or 1 ms: it locks the event mutex, tests the flag,
// unlocks and returns WAIT_TIMEOUT (`cmpl $2,%r15d; jb` at 0x1112ae). Only
// timeouts >= 2 ms reach pthread_cond_timedwait. SetEvent broadcasts the
// condition variable, so a real wait still wakes immediately when signaled.
//
// The job-queue workers idle in `while (no work) WaitForSingleObject(ev, 1)`
// (call at 0x11a7556). On Windows that sleeps; on the Mac it busy-spins
// through a contended mutex: both "JQ Worker" threads measured ~70% CPU each
// (~24% kernel) at an idle menu, competing with the main and render threads
// for the M4's four performance cores.
//
// Fix (default on, BO3MACFIX_EVENTWAIT=0 disables): the JQ worker passes 2 ms,
// i.e. it actually sleeps until work is queued (SetEvent) or 2 ms pass.
//
// The same applies (default on) to the D3D render thread's idle wait for work
// (0xfeb491, BO3MACFIX_RENDERWAIT=0 disables) and to the main thread's wait
// for renderCompletedEvent (0xd05c07, BO3MACFIX_MAINWAIT=0 disables).
// Alternating in-map runs: same frame rate (77.7/86.5 vs 86.9/81.9 FPS) with
// the main thread's CPU use down from ~70% to ~48%.
#include "cpu_sched.h"
#include "dobby.h"
#include "patch_layer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int env_flag(const char *name, int fallback) {
    const char *v = getenv(name);
    if (!v || !*v) return fallback;
    return strcmp(v, "0") != 0;
}

/* Replace `mov esi, 1` (be 01 00 00 00) feeding a verified event-wait call. */
static int patch_wait_immediate(uint64_t base, uint32_t mov_offset, const uint8_t *context, size_t context_len,
                                uint32_t context_offset) {
    static const uint8_t mov_one[] = {0xbe, 0x01, 0x00, 0x00, 0x00};
    uint8_t *mov = (uint8_t *)(uintptr_t)(base + mov_offset);
    if (bo3_orig_memcmp(mov, mov_one, sizeof(mov_one)) ||
        bo3_orig_memcmp((const void *)(uintptr_t)(base + context_offset), context, context_len))
        return -1;
    uint8_t two = 0x02;
    return DobbyCodePatch(mov + 1, &two, 1) == 0 ? 1 : -2;
}

int bo3_install_event_wait_fix(uint64_t base) {
    /* Shim itself: prologue and the timeout<2 test that makes 1 ms spin. */
    static const uint8_t shim[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                   0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x38, 0x41, 0x89, 0xf7};
    static const uint8_t short_timeout_test[] = {0x41, 0x83, 0xff, 0x02};
    if (bo3_orig_memcmp((const void *)(uintptr_t)(base + 0x111242), shim, sizeof(shim)) ||
        bo3_orig_memcmp((const void *)(uintptr_t)(base + 0x1112ae), short_timeout_test, sizeof(short_timeout_test)))
        return -1;
    int result = 0;
    if (env_flag("BO3MACFIX_EVENTWAIT", 1)) {
        /* mov rdi,[r13]; mov esi,1; call 0x109bde (event vtable wait) */
        static const uint8_t jq[] = {0x49, 0x8b, 0x7d, 0x00, 0xbe, 0x01, 0x00, 0x00, 0x00, 0xe8, 0x83, 0x26, 0xf6, 0xfe};
        result = patch_wait_immediate(base, 0x11a7551, jq, sizeof(jq), 0x11a754d);
        if (result < 0) return result;
    }
    if (env_flag("BO3MACFIX_RENDERWAIT", 1)) {
        /* call 0xd05eb7; mov edi,1; call 0xd06061 (backend event wait) */
        static const uint8_t renderwait[] = {0xe8, 0x26, 0xaa, 0xd1, 0xff, 0xbf, 0x01, 0x00, 0x00, 0x00,
                                             0xe8, 0xc6, 0xab, 0xd1, 0xff};
        static const uint8_t mov_edi_one[] = {0xbf, 0x01, 0x00, 0x00, 0x00};
        if (bo3_orig_memcmp((const void *)(uintptr_t)(base + 0xfeb48c), renderwait, sizeof(renderwait)) ||
            bo3_orig_memcmp((const void *)(uintptr_t)(base + 0xfeb491), mov_edi_one, sizeof(mov_edi_one)))
            return -5;
        uint8_t two = 0x02;
        if (DobbyCodePatch((void *)(uintptr_t)(base + 0xfeb492), &two, 1) != 0) return -6;
        result |= 8;
    }
    if (env_flag("BO3MACFIX_MAINWAIT", 1)) {
        /* mov rdi,[renderCompletedEvent]; mov esi,1; call 0x109bde */
        static const uint8_t mainwait[] = {0x48, 0x8b, 0x3d, 0x11, 0xf5, 0x64, 0x01, 0xbe, 0x01, 0x00,
                                           0x00, 0x00, 0xe8, 0xcd, 0x3f, 0x40, 0xff};
        int r = patch_wait_immediate(base, 0xd05c07, mainwait, sizeof(mainwait), 0xd05c00);
        if (r < 0) return r;
        result |= 2;
    }
    if (env_flag("BO3MACFIX_LOCKSPIN", 0) == 0) {
        /* Aspyr's device-context lock (0x4e580) retries pthread_mutex_trylock
         * up to ctx->spin times, with no pause, before blocking; the limit is
         * initialised to 100000 at 0x2aa63. Under Rosetta a contending thread
         * burns its core (and the lock's cache line) for milliseconds. A
         * limit of 0 makes it a plain pthread_mutex_lock: same locking
         * semantics, blocking waits. Measured in Shadows of Evil: ~+7% FPS.
         * BO3MACFIX_LOCKSPIN=1 keeps Aspyr's spinning. */
        static const uint8_t init[] = {0xc7, 0x83, 0x80, 0x00, 0x00, 0x00, 0xa0, 0x86, 0x01, 0x00};
        static const uint8_t lock_fn[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x49, 0x89, 0xfe,
                                          0x41, 0x8b, 0x5e, 0x40, 0x85, 0xdb, 0x74, 0x10};
        if (bo3_orig_memcmp((const void *)(uintptr_t)(base + 0x2aa63), init, sizeof(init)) ||
            bo3_orig_memcmp((const void *)(uintptr_t)(base + 0x4e580), lock_fn, sizeof(lock_fn)))
            return -3;
        uint8_t zero[4] = {0, 0, 0, 0};
        if (DobbyCodePatch((void *)(uintptr_t)(base + 0x2aa69), zero, sizeof(zero)) != 0) return -4;
        result |= 4;
    }
    return result;
}

/* Experiment (BO3MACFIX_QOS=1): ask the scheduler to keep the frame-critical
 * threads on performance cores. Thread names are set by the game through
 * pthread_setname_np (an import, so interposing it leaves the game's code
 * and Rosetta's AOT translation untouched). The render thread is recognised
 * by its first CAMetalLayer nextDrawable call (perf_telemetry.m). */
#include <pthread.h>
#include <pthread/qos.h>
#include "dyld-interposing.h"

int bo3_qos_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) enabled = env_flag("BO3MACFIX_QOS", 0);
    return enabled;
}

int bo3_promote_current_thread(void) {
    return bo3_qos_enabled() ? pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) : -1;
}

static int bo3_pthread_setname_np(const char *name) {
    int r = pthread_setname_np(name);
    if (name && (!strcmp(name, "Main") || !strncmp(name, "JQ Worker", 9)))
        bo3_promote_current_thread();
    return r;
}
DYLD_INTERPOSE(bo3_pthread_setname_np, pthread_setname_np)
