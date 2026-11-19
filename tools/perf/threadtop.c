// threadtop: per-thread CPU usage (user/system) by thread name for a PID.
// Usage: threadtop <pid> [interval_seconds]
#include <libproc.h>
#include <sys/proc_info.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { uint64_t handle, user, sys; char name[64]; int pri; } Snap;

static int snap(int pid, Snap *out, int max) {
    uint64_t handles[1024];
    int bytes = proc_pidinfo(pid, PROC_PIDLISTTHREADS, 0, handles, sizeof(handles));
    if (bytes <= 0) return -1;
    int n = bytes / (int)sizeof(uint64_t), k = 0;
    for (int i = 0; i < n && k < max; i++) {
        struct proc_threadinfo ti;
        if (proc_pidinfo(pid, PROC_PIDTHREADINFO, handles[i], &ti, sizeof(ti)) != sizeof(ti)) continue;
        out[k].handle = handles[i];
        out[k].user = ti.pth_user_time;
        out[k].sys = ti.pth_system_time;
        out[k].pri = ti.pth_curpri;
        strncpy(out[k].name, ti.pth_name, sizeof(out[k].name) - 1);
        out[k].name[sizeof(out[k].name) - 1] = 0;
        k++;
    }
    return k;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s pid [seconds]\n", argv[0]); return 2; }
    int pid = atoi(argv[1]);
    double secs = argc > 2 ? atof(argv[2]) : 2.0;
    static Snap a[1024], b[1024];
    int na = snap(pid, a, 1024);
    if (na < 0) { perror("proc_pidinfo"); return 1; }
    usleep((useconds_t)(secs * 1e6));
    int nb = snap(pid, b, 1024);
    double total = 0;
    printf("%-44s %7s %7s %7s %4s\n", "thread", "cpu%", "user%", "sys%", "pri");
    for (int i = 0; i < nb; i++) {
        for (int j = 0; j < na; j++) if (a[j].handle == b[i].handle) {
            double u = (b[i].user - a[j].user) / 1e9 / secs * 100, s = (b[i].sys - a[j].sys) / 1e9 / secs * 100;
            total += u + s;
            if (u + s >= 1.0)
                printf("%-44s %7.1f %7.1f %7.1f %4d\n", b[i].name[0] ? b[i].name : "(unnamed)", u + s, u, s, b[i].pri);
        }
    }
    printf("TOTAL %.1f%% over %.1fs, %d threads\n", total, secs, nb);
    return 0;
}
