/* Sample only the known S28 vkcube window corners during resize.
 * The expected vkcube grey background is 0x333333. This deliberately
 * reads X11 pixels, not the Android screen or an unrelated desktop window.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int xerrors;
static int on_error(Display *d, XErrorEvent *event)
{
    (void)d;
    (void)event;
    ++xerrors;
    return 0;
}

static int64_t monotonic_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * INT64_C(1000000) + t.tv_nsec / 1000;
}

static int expected_grey(unsigned long pixel)
{
    const unsigned r = (pixel >> 16) & 255;
    const unsigned g = (pixel >> 8) & 255;
    const unsigned b = pixel & 255;
    return r >= 36 && r <= 70 && g >= 36 && g <= 70 && b >= 36 && b <= 70;
}

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    char *end = NULL;
    const unsigned long xid = strtoul(argv[1], &end, 0);
    const int seconds = atoi(argv[2]);
    const int period_ms = atoi(argv[3]);
    if (!xid || !end || *end || seconds < 1 || seconds > 60 ||
        period_ms < 1 || period_ms > 100) return 2;

    XSetErrorHandler(on_error);
    Display *d = XOpenDisplay(NULL);
    if (!d) return 3;
    const int64_t end_us = monotonic_us() + seconds * INT64_C(1000000);
    int total = 0, invalid = 0, black_samples = 0, non_grey = 0;
    while (monotonic_us() < end_us) {
        XWindowAttributes attr;
        if (!XGetWindowAttributes(d, xid, &attr) || attr.width < 30 ||
            attr.height < 30 || attr.map_state != IsViewable) {
            ++invalid;
        } else {
            const int x[4] = {8, attr.width - 9, 8, attr.width - 9};
            const int y[4] = {8, 8, attr.height - 9, attr.height - 9};
            int blacks = 0, greys = 0;
            for (int i = 0; i < 4; ++i) {
                XImage *img = XGetImage(d, xid, x[i], y[i], 1, 1,
                                       AllPlanes, ZPixmap);
                if (!img) {
                    ++invalid;
                    continue;
                }
                const unsigned long pixel = XGetPixel(img, 0, 0) & 0xffffff;
                if (!pixel) ++blacks;
                if (expected_grey(pixel)) ++greys;
                XDestroyImage(img);
            }
            XSync(d, False);
            ++total;
            if (greys != 4 || xerrors) {
                ++non_grey;
                if (blacks) ++black_samples;
                printf("S28_CORNER_OUTLIER at_us=%lld size=%dx%d gray=%d black=%d xerr=%d\n",
                       (long long)monotonic_us(), attr.width, attr.height,
                       greys, blacks, xerrors);
                fflush(stdout);
            }
        }
        xerrors = 0;
        const struct timespec interval = {
            .tv_sec = 0, .tv_nsec = (long)period_ms * 1000000L
        };
        nanosleep(&interval, NULL);
    }
    printf("S28_CORNER_TOTAL=%d INVALID=%d OUTLIERS=%d BLACK_EVENTS=%d\n",
           total, invalid, non_grey, black_samples);
    XCloseDisplay(d);
    return 0;
}
