// Freeze sampler: when no frame has started for a while, record where the
// game's own threads are, so the session log explains the freeze.
//
// Off by default (diagnostic). Enable with BO3MACFIX_FREEZELOG=1, or create
// BO3MacFix-mawlicious/enable-freeze-log (the launcher sets the variable).
//
// A watchdog thread checks the frame heartbeat (perf_telemetry.m) every
// 100 ms. Once a stall exceeds 400 ms it samples up to 5 times, 150 ms apart,
// for at most 20 stalls per session. Only allow-listed game threads (Main,
// "JQ Worker ...", "Win (...)") are ever suspended: never Rosetta, system or
// unnamed threads. Each is suspended for a few microseconds while its x86
// registers are read and its frame-pointer chain is copied with
// vm_read_overwrite (no raw dereferences); symbolizing and logging happen
// after resume. The Mach calls used during suspension are bound at startup.
// If a resume ever fails, sampling stops for the rest of the session.
#include "freeze_sampler.h"
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "versioninfo.h"
#define Version_Prefix "[BO3MacFix-mawlicious " MACFIX_VERSION "]"

#define STALL_MS 400
#define SAMPLE_GAP_US 150000
#define MAX_SAMPLES_PER_STALL 5
#define MAX_STALLS 20
#define MAX_FRAMES 24
#define MAX_TRACKED 128

static uint64_t *frame_clock;
static uintptr_t image_base, image_end;
static int aborted;
static uint64_t seen_id[MAX_TRACKED], seen_hash[MAX_TRACKED];

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static uint64_t heartbeat(void) { return __atomic_load_n(frame_clock, __ATOMIC_RELAXED); }

static void describe(uint64_t pc, char *out, size_t n) {
    if (pc >= image_base && pc < image_end) { snprintf(out, n, "game+0x%llx", (unsigned long long)(pc - image_base)); return; }
    Dl_info di;
    if (dladdr((void *)pc, &di) && di.dli_fname) {
        const char *base = strrchr(di.dli_fname, '/');
        if (di.dli_sname) snprintf(out, n, "%s", di.dli_sname);
        else snprintf(out, n, "%s+0x%llx", base ? base + 1 : di.dli_fname, (unsigned long long)(pc - (uint64_t)di.dli_fbase));
        return;
    }
    snprintf(out, n, "0x%llx", (unsigned long long)pc);
}

static int game_thread(const char *name) {
    return !strcmp(name, "Main") || !strncmp(name, "JQ Worker", 9) || !strncmp(name, "Win (", 5);
}

/* Returns 1 if this thread showed the same stack earlier in this stall. */
static int unchanged(uint64_t id, uint64_t hash) {
    for (int i = 0; i < MAX_TRACKED; i++)
        if (seen_id[i] == id) { int same = seen_hash[i] == hash; seen_hash[i] = hash; return same; }
    for (int i = 0; i < MAX_TRACKED; i++)
        if (!seen_id[i]) { seen_id[i] = id; seen_hash[i] = hash; return 0; }
    return 0;
}

static void sample_threads(unsigned stall, unsigned sample, double stalled_ms) {
    thread_act_array_t threads;
    mach_msg_type_number_t count;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) return;
    mach_port_t self = mach_thread_self();
    for (mach_msg_type_number_t i = 0; i < count && !aborted; i++) {
        thread_t th = threads[i];
        if (th == self) continue;
        char name[64] = "";
        pthread_t pt = pthread_from_mach_thread_np(th);
        if (!pt || pthread_getname_np(pt, name, sizeof(name)) != 0 || !game_thread(name)) continue;
        thread_identifier_info_data_t ident;
        mach_msg_type_number_t ic = THREAD_IDENTIFIER_INFO_COUNT;
        if (thread_info(th, THREAD_IDENTIFIER_INFO, (thread_info_t)&ident, &ic) != KERN_SUCCESS) continue;

        uint64_t pcs[MAX_FRAMES];
        int n = 0;
        if (thread_suspend(th) != KERN_SUCCESS) continue; /* thread exited */
        x86_thread_state64_t st;
        mach_msg_type_number_t sc = x86_THREAD_STATE64_COUNT;
        if (thread_get_state(th, x86_THREAD_STATE64, (thread_state_t)&st, &sc) == KERN_SUCCESS) {
            pcs[n++] = st.__rip;
            uint64_t fp = st.__rbp;
            while (n < MAX_FRAMES && fp && !(fp & 7)) {
                uint64_t frame[2];
                vm_size_t got = 0;
                if (vm_read_overwrite(mach_task_self(), fp, sizeof(frame), (vm_address_t)frame, &got) != KERN_SUCCESS ||
                    got != sizeof(frame) || !frame[1] || frame[0] <= fp)
                    break;
                pcs[n++] = frame[1];
                fp = frame[0];
            }
        }
        if (thread_resume(th) != KERN_SUCCESS) { aborted = 1; break; }

        thread_basic_info_data_t bi;
        mach_msg_type_number_t bc = THREAD_BASIC_INFO_COUNT;
        int running = thread_info(th, THREAD_BASIC_INFO, (thread_info_t)&bi, &bc) == KERN_SUCCESS &&
                      bi.run_state == TH_STATE_RUNNING;
        uint64_t hash = 1469598103934665603ull;
        for (int f = 0; f < n; f++) hash = (hash ^ pcs[f]) * 1099511628211ull;
        if (unchanged(ident.thread_id, hash) && sample > 0) continue;

        char line[2048];
        int len = snprintf(line, sizeof(line), Version_Prefix " [freeze %u.%u %.0fms] %s %s:", stall, sample, stalled_ms,
                           name, running ? "running" : "waiting");
        for (int f = 0; f < n && len < (int)sizeof(line) - 200; f++) {
            char d[160];
            describe(pcs[f], d, sizeof(d));
            len += snprintf(line + len, sizeof(line) - len, " %s", d);
        }
        printf("%s\n", line);
    }
    mach_port_deallocate(mach_task_self(), self);
    for (mach_msg_type_number_t i = 0; i < count; i++) mach_port_deallocate(mach_task_self(), threads[i]);
    vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(thread_t));
}

static void *watchdog(void *arg) {
    (void)arg;
    pthread_setname_np("BO3MacFix freeze sampler");
    unsigned stalls = 0;
    uint64_t sampled = 0;
    while (stalls < MAX_STALLS && !aborted) {
        usleep(100000);
        uint64_t last = heartbeat();
        if (!last || last == sampled || (now_ns() - last) / 1e6 < STALL_MS) continue;
        sampled = last; /* one burst per stall */
        stalls++;
        memset(seen_id, 0, sizeof(seen_id));
        for (unsigned s = 0; s < MAX_SAMPLES_PER_STALL && !aborted && heartbeat() == last; s++) {
            sample_threads(stalls, s, (now_ns() - last) / 1e6);
            usleep(SAMPLE_GAP_US);
        }
        printf(Version_Prefix " [freeze %u] sampling ended after %.0f ms%s\n", stalls, (now_ns() - last) / 1e6,
               aborted ? " (aborted: thread resume failed)" : "");
    }
    return NULL;
}

int bo3_install_freeze_sampler(uint64_t *last_frame_ns) {
    const char *v = getenv("BO3MACFIX_FREEZELOG");
    if (!v || strcmp(v, "1") || !last_frame_ns) return 0;
    frame_clock = last_frame_ns;
    image_base = (uintptr_t)_dyld_get_image_header(0);
    image_end = image_base + 0x1965000; /* __TEXT size of the supported executable */
    /* Bind the Mach calls used inside the suspended window now, so lazy
     * binding (dyld) can never run while a game thread is suspended. */
    mach_port_t me = mach_thread_self();
    x86_thread_state64_t st;
    mach_msg_type_number_t sc = x86_THREAD_STATE64_COUNT;
    thread_get_state(me, x86_THREAD_STATE64, (thread_state_t)&st, &sc);
    uint64_t probe = 0, copy = 0;
    vm_size_t got = 0;
    vm_read_overwrite(mach_task_self(), (vm_address_t)&probe, sizeof(probe), (vm_address_t)&copy, &got);
    thread_resume(me);               /* not suspended: fails harmlessly, binds the stub */
    thread_suspend(MACH_PORT_NULL);  /* invalid port: fails harmlessly, binds the stub */
    mach_port_deallocate(mach_task_self(), me);

    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int r = pthread_create(&t, &attr, watchdog, NULL);
    pthread_attr_destroy(&attr);
    return r == 0 ? 1 : -1;
}
