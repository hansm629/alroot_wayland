/* SPDX-License-Identifier: MIT
 * Bounded, own-window X11 stress for the S38 frame-lifetime regression.
 * Compile: cc -O2 -Wall -Wextra -o s38-frame-release-stress this.c -lX11
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int xerror;
static int on_error(Display *dpy, XErrorEvent *event)
{
    (void)dpy;
    xerror = event->error_code;
    return 0;
}

static int64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void wait_until(int64_t deadline)
{
    struct timespec t = { deadline / 1000000000LL, deadline % 1000000000LL };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL) == EINTR) {}
}

static unsigned triangle(unsigned step, unsigned period)
{
    unsigned at = step % (2 * period);
    return at <= period ? at : 2 * period - at;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 5) {
        fprintf(stderr, "usage: %s WINDOW_XID [duration_ms=1500] [step_ms=16] [storm_roundtrips=0]\n", argv[0]);
        return 2;
    }
    char *end;
    Window window = (Window)strtoul(argv[1], &end, 0);
    if (!window || *end) return 2;
    unsigned duration_ms = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 1500;
    unsigned step_ms = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 10) : 16;
    unsigned storm = argc > 4 ? (unsigned)strtoul(argv[4], NULL, 10) : 0;
    if (!duration_ms || duration_ms > 6000 || step_ms < 2 || step_ms > 200 || storm > 512) {
        fprintf(stderr, "bounded inputs required: duration 1..6000ms, step 10..20ms, storm 0..512\n");
        return 2;
    }
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "S38_STRESS_OPEN_DISPLAY=FAIL\n"); return 3; }
    XSetErrorHandler(on_error);
    XWindowAttributes original;
    if (!XGetWindowAttributes(dpy, window, &original) || xerror || original.map_state != IsViewable) {
        fprintf(stderr, "S38_STRESS_OWN_WINDOW=FAIL error=%d\n", xerror);
        XCloseDisplay(dpy);
        return 4;
    }
    const int64_t start = now_ns(), finish = start + (int64_t)duration_ms * 1000000LL;
    const int64_t interval = (int64_t)step_ms * 1000000LL;
    unsigned steps = 0, roundtrips = 0;
    while (!xerror && now_ns() < finish) {
        /* Smooth, modest, non-thin dimensions isolate lifetime from gralloc's
         * separate thin-AHB issue. Only this explicitly supplied XID is touched. */
        unsigned width = 400 + 8 * triangle(steps, 40);
        unsigned height = 300 + 6 * triangle(steps + 11, 35);
        XResizeWindow(dpy, window, width, height);
        XSync(dpy, False);
        roundtrips++;
        for (unsigned i = 0; i < storm && !xerror && now_ns() < finish; i++) {
            XNoOp(dpy);
            XSync(dpy, False);
            roundtrips++;
        }
        steps++;
        int64_t next = start + (int64_t)steps * interval;
        if (next > finish) next = finish;
        if (next > now_ns()) wait_until(next);
    }
    int64_t resize_end = now_ns();
    printf("S39_RESIZE_TIMING start_ms=%lld end_ms=%lld\n",
           (long long)(start / 1000000), (long long)(resize_end / 1000000));
    if (!xerror) {
        XResizeWindow(dpy, window, (unsigned)original.width, (unsigned)original.height);
        XSync(dpy, False);
    }
    printf("S38_STRESS steps=%u roundtrips=%u elapsed_ms=%lld storm=%u xerror=%d restore=%dx%d\n",
           steps, roundtrips, (long long)((now_ns() - start) / 1000000), storm,
           xerror, original.width, original.height);
    XCloseDisplay(dpy);
    return xerror ? 5 : 0;
}
