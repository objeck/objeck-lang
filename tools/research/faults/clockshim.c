/* LD_PRELOAD shim: steps the REALTIME clock (not the monotonic one) when the process receives SIGUSR1.
 * FAULTS_CLOCK_STEP_S is the step in seconds (may be negative). This models an NTP step or a manual
 * `date -s` without touching the machine's real clock, which other work on this box depends on.
 *   gcc -shared -fPIC -O2 -o clockshim.so clockshim.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

static volatile long long off_ns = 0;
static long long step_ns = 0;

static void on_usr1(int sig) { (void)sig; off_ns += step_ns; }

__attribute__((constructor)) static void init(void) {
    const char *e = getenv("FAULTS_CLOCK_STEP_S");
    step_ns = e ? (long long)(atof(e) * 1e9) : 0;
    signal(SIGUSR1, on_usr1);
}

int clock_gettime(clockid_t id, struct timespec *ts) {
    static int (*real)(clockid_t, struct timespec *);
    if (!real) real = dlsym(RTLD_NEXT, "clock_gettime");
    int r = real(id, ts);
    if (r == 0 && (id == CLOCK_REALTIME || id == CLOCK_REALTIME_COARSE)) {
        long long t = (long long)ts->tv_sec * 1000000000LL + ts->tv_nsec + off_ns;
        ts->tv_sec = t / 1000000000LL;
        ts->tv_nsec = t % 1000000000LL;
    }
    return r;
}

int gettimeofday(struct timeval *tv, void *tz) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (tv) { tv->tv_sec = ts.tv_sec; tv->tv_usec = ts.tv_nsec / 1000; }
    (void)tz;
    return 0;
}

time_t time(time_t *out) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (out) *out = ts.tv_sec;
    return ts.tv_sec;
}
