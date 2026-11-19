// Frame-time and resource telemetry for performance comparisons.
//
// Each -[CAMetalLayer nextDrawable] call starts one presented frame in this
// port, so the interval between calls is the frame interval the player sees.
// Every BO3MACFIX_PERFLOG_INTERVAL seconds (default 30) one summary line is
// written to the session log; long frames (>= 250 ms) are also logged as they
// happen so freezes can be lined up with the surrounding game output.
//
// Each line also counts synchronous Metal compiles in the interval (render
// pipeline states and libraries built from source) with total and longest
// time, so hitches from first-use pipeline creation show up.
//
// Disable with BO3MACFIX_PERFLOG=0. Overhead: one clock read and one array
// store per frame; sorting happens once per interval.
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#include <mach/mach.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include "perf_telemetry.h"
#include "metal_stats.h"
#include "cpu_sched.h"
#include "freeze_sampler.h"
#include "versioninfo.h"
#define Version_Prefix "[BO3MacFix-mawlicious " MACFIX_VERSION "]"

#define MAX_SAMPLES 16384
#define HITCH_LOG_MS 250.0

static IMP orig_nextDrawable;
static pthread_mutex_t perf_lock = PTHREAD_MUTEX_INITIALIZER;
static double interval_s = 30.0;
static uint64_t last_frame_ns; /* heartbeat, read atomically by the freeze sampler */
static uint64_t window_start_ns, drawable_wait_ns;
static float samples[MAX_SAMPLES];
static unsigned sample_count, frames, over_33, over_50, over_100, over_250;
static double cpu_start_s;
static unsigned hitch_logged;

/* Synchronous Metal compiles (render pipeline states, libraries from source)
 * per interval: count, total and longest time. */
enum { COMPILE_PSO, COMPILE_LIB, COMPILE_KINDS };
static IMP orig_pso, orig_pso_refl, orig_lib_src;
static unsigned compile_count[COMPILE_KINDS];
static uint64_t compile_ns[COMPILE_KINDS], compile_max_ns[COMPILE_KINDS];

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static double process_cpu_seconds(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru)) return 0;
    return ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
}

static double footprint_gb(void) {
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return 0;
    return info.phys_footprint / 1073741824.0;
}

static int cmp_float(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static float percentile(unsigned n, double p) {
    if (!n) return 0;
    unsigned i = (unsigned)(p * (n - 1) + 0.5);
    return samples[i < n ? i : n - 1];
}

static void count_compile(int kind, uint64_t start) {
    uint64_t ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - start;
    pthread_mutex_lock(&perf_lock);
    compile_count[kind]++;
    compile_ns[kind] += ns;
    if (ns > compile_max_ns[kind]) compile_max_ns[kind] = ns;
    pthread_mutex_unlock(&perf_lock);
}

static id hooked_pso(id self, SEL cmd, id desc, NSError **error) {
    uint64_t t = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    id r = ((id (*)(id, SEL, id, NSError **))orig_pso)(self, cmd, desc, error);
    count_compile(COMPILE_PSO, t);
    return r;
}

static id hooked_pso_refl(id self, SEL cmd, id desc, NSUInteger options, id *reflection, NSError **error) {
    uint64_t t = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    id r = ((id (*)(id, SEL, id, NSUInteger, id *, NSError **))orig_pso_refl)(self, cmd, desc, options, reflection, error);
    count_compile(COMPILE_PSO, t);
    return r;
}

static id hooked_lib_src(id self, SEL cmd, id source, id options, NSError **error) {
    uint64_t t = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    id r = ((id (*)(id, SEL, id, id, NSError **))orig_lib_src)(self, cmd, source, options, error);
    count_compile(COMPILE_LIB, t);
    return r;
}

static void hook_device(Class cls, SEL sel, IMP imp, IMP *orig) {
    Method m = class_getInstanceMethod(cls, sel);
    if (m) *orig = method_setImplementation(m, imp);
}

static void report_locked(uint64_t t) {
    double span = (t - window_start_ns) / 1e9, cpu = process_cpu_seconds();
    unsigned n = sample_count;
    double sum = 0;
    for (unsigned i = 0; i < n; i++) sum += samples[i];
    qsort(samples, n, sizeof(float), cmp_float);
    printf(Version_Prefix " [perf] %.1fs frames=%u fps=%.1f avg=%.2fms p50=%.2f p95=%.2f p99=%.2f max=%.1f "
           ">33ms=%u >50ms=%u >100ms=%u >250ms=%u drawableWait=%.2fms cpu=%.0f%% footprint=%.2fGB "
           "pso=%u/%.0fms(max %.0f) shaderlib=%u/%.0fms(max %.0f)\n",
           span, frames, frames / span, n ? sum / n : 0, percentile(n, 0.5), percentile(n, 0.95),
           percentile(n, 0.99), n ? samples[n - 1] : 0, over_33, over_50, over_100, over_250,
           frames ? drawable_wait_ns / 1e6 / frames : 0, (cpu - cpu_start_s) / span * 100, footprint_gb(),
           compile_count[COMPILE_PSO], compile_ns[COMPILE_PSO] / 1e6, compile_max_ns[COMPILE_PSO] / 1e6,
           compile_count[COMPILE_LIB], compile_ns[COMPILE_LIB] / 1e6, compile_max_ns[COMPILE_LIB] / 1e6);
    memset(compile_count, 0, sizeof(compile_count));
    memset(compile_ns, 0, sizeof(compile_ns));
    memset(compile_max_ns, 0, sizeof(compile_max_ns));
    window_start_ns = t;
    cpu_start_s = cpu;
    sample_count = frames = over_33 = over_50 = over_100 = over_250 = 0;
    drawable_wait_ns = 0;
    hitch_logged = 0;
}

static id hooked_nextDrawable(id self, SEL cmd) {
    static __thread int promoted;
    if (!promoted) {
        promoted = 1;
        int r = bo3_promote_current_thread();
        char name[64] = "";
        pthread_getname_np(pthread_self(), name, sizeof(name));
        if (r != -1) printf(Version_Prefix " Presenting thread '%s' QoS promotion: %d.\n", name, r);
    }
    bo3_metal_stats_frame();
    uint64_t start = now_ns();
    id drawable = ((id (*)(id, SEL))orig_nextDrawable)(self, cmd);
    uint64_t end = now_ns();
    pthread_mutex_lock(&perf_lock);
    drawable_wait_ns += end - start;
    if (last_frame_ns) {
        double ms = (start - last_frame_ns) / 1e6;
        frames++;
        if (sample_count < MAX_SAMPLES) samples[sample_count++] = (float)ms;
        over_33 += ms > 33.4;
        over_50 += ms > 50.0;
        over_100 += ms > 100.0;
        over_250 += ms > HITCH_LOG_MS;
        if (ms > HITCH_LOG_MS && hitch_logged < 20) {
            hitch_logged++;
            printf(Version_Prefix " [perf] long frame %.0f ms (footprint %.2f GB)\n", ms, footprint_gb());
        }
    } else {
        window_start_ns = start;
        cpu_start_s = process_cpu_seconds();
    }
    __atomic_store_n(&last_frame_ns, start, __ATOMIC_RELAXED);
    if ((start - window_start_ns) / 1e9 >= interval_s) report_locked(start);
    pthread_mutex_unlock(&perf_lock);
    return drawable;
}

int bo3_install_perf_telemetry(void) {
    const char *enabled = getenv("BO3MACFIX_PERFLOG");
    if (enabled && !strcmp(enabled, "0")) return 0;
    const char *interval = getenv("BO3MACFIX_PERFLOG_INTERVAL");
    if (interval && atof(interval) >= 1.0) interval_s = atof(interval);
    Method m = class_getInstanceMethod([CAMetalLayer class], @selector(nextDrawable));
    if (!m) return -1;
    orig_nextDrawable = method_setImplementation(m, (IMP)hooked_nextDrawable);
    if (!orig_nextDrawable) return -2;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device) {
        Class cls = object_getClass(device);
        hook_device(cls, @selector(newRenderPipelineStateWithDescriptor:error:), (IMP)hooked_pso, &orig_pso);
        hook_device(cls, @selector(newRenderPipelineStateWithDescriptor:options:reflection:error:), (IMP)hooked_pso_refl,
                    &orig_pso_refl);
        hook_device(cls, @selector(newLibraryWithSource:options:error:), (IMP)hooked_lib_src, &orig_lib_src);
    }
    int freeze = bo3_install_freeze_sampler(&last_frame_ns);
    printf(Version_Prefix " Freeze sampler: %s (%d).\n", freeze == 1 ? "enabled" : "off", freeze);
    return 1;
}
