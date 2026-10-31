/* S30 isolated Vulkan motion acceptance: after tiny XCB resize, test the
 * renderer's ACTUAL newly presented pixels advance, not a frozen snapshot.
 * Use XGetImage only on our newly created vkcube window, never XFCE root. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int errors;
static int on_error(Display *d, XErrorEvent *event)
{
    (void)d;
    (void)event;
    ++errors;
    return 0;
}

static void sleep_ms(unsigned ms)
{
    struct timespec interval = {
        .tv_sec = ms / 1000,
        .tv_nsec = (long)(ms % 1000) * 1000000L
    };
    nanosleep(&interval, NULL);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    char *end = NULL;
    unsigned long xid = strtoul(argv[1], &end, 0);
    if (!xid || !end || *end) return 2;
    XSetErrorHandler(on_error);
    Display *d = XOpenDisplay(NULL);
    if (!d) return 3;

    uint64_t last_hash = 0;
    int samples = 0, changes = 0, non_grey_frames = 0, invalid = 0;
    for (int t = 0; t < 22; ++t) {
        XWindowAttributes attr;
        if (!XGetWindowAttributes(d, xid, &attr) ||
            attr.map_state != IsViewable || attr.width < 240 ||
            attr.height < 240) {
            ++invalid;
            sleep_ms(65);
            continue;
        }
        const int x = attr.width / 4, y = attr.height / 4;
        const int w = attr.width / 2, h = attr.height / 2;
        XImage *img = XGetImage(d, xid, x, y, w, h,
                               AllPlanes, ZPixmap);
        if (!img) {
            ++invalid;
            sleep_ms(65);
            continue;
        }
        uint64_t hash = UINT64_C(1469598103934665603);
        int non_grey = 0;
        for (int row = 0; row < 15; ++row) {
            for (int col = 0; col < 15; ++col) {
                unsigned long v = XGetPixel(img,
                    (w - 1) * col / 14, (h - 1) * row / 14) & 0xffffff;
                unsigned r = (v >> 16) & 255;
                unsigned g = (v >> 8) & 255;
                unsigned b = v & 255;
                if (r < 30 || r > 75 || g < 30 || g > 75 ||
                    b < 30 || b > 75)
                    ++non_grey;
                hash ^= v;
                hash *= UINT64_C(1099511628211);
            }
        }
        XDestroyImage(img);
        if (samples && hash != last_hash) ++changes;
        last_hash = hash;
        ++samples;
        if (non_grey >= 8) ++non_grey_frames;
        sleep_ms(65);
    }
    XSync(d, False);
    printf("S30_ACTUAL_VULKAN_PRESENT_SAMPLES=%d HASH_CHANGES=%d NON_GREY_FRAMES=%d INVALID=%d XERR=%d\n",
           samples, changes, non_grey_frames, invalid, errors);
    XCloseDisplay(d);
    if (samples >= 15 && changes >= 4 && non_grey_frames >= 10 &&
        !invalid && !errors) {
        puts("S30_VKCUBE_REAL_FRAME_ADVANCE=PASS_NOT_FROZEN");
        return 0;
    }
    puts("S30_VKCUBE_REAL_FRAME_ADVANCE=FAIL_FROZEN_OR_MISSING_CONTENT");
    return 5;
}
