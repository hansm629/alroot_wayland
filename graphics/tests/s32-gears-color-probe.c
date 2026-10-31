/* Test-only GLX/Zink color continuity under XCB resize, never screen root.
 * Count colored gear pixels in the center of our own glxgears client XID;
 * detect blanking between resize and the next real Present. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int errors;
static int on_error(Display *d, XErrorEvent *e)
{
    (void)d;
    (void)e;
    ++errors;
    return 0;
}
static long long monotonic_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    unsigned long window = strtoul(argv[1], 0, 0);
    int duration = atoi(argv[2]);
    if (!window || duration < 2 || duration > 35) return 2;
    XSetErrorHandler(on_error);
    Display *d = XOpenDisplay(0);
    if (!d) return 3;

    int samples = 0, colored = 0, blank = 0, max_blank = 0,
        consecutive_blank = 0, red_frames = 0, blue_frames = 0,
        green_frames = 0, changes = 0, invalid = 0;
    uint64_t prev_hash = 0;
    long long until = monotonic_ms() + duration * 1000LL;
    while (monotonic_ms() < until) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(d, window, &a) ||
            a.map_state != IsViewable || a.width < 85 || a.height < 85) {
            ++invalid;
        } else {
            int width = a.width / 2;
            int height = a.height / 2;
            if (width > 300) width = 300;
            if (height > 300) height = 300;
            XImage *image = XGetImage(d, window, (a.width - width) / 2,
                                      (a.height - height) / 2,
                                      width, height, AllPlanes, ZPixmap);
            if (image) {
                int red = 0, blue = 0, green = 0;
                uint64_t hash = UINT64_C(1469598103934665603);
                for (int y = 0; y < 27; ++y) {
                    for (int x = 0; x < 27; ++x) {
                        unsigned long p = XGetPixel(image,
                            (width - 1) * x / 26,
                            (height - 1) * y / 26) & 0xffffff;
                        unsigned r = (p >> 16) & 255;
                        unsigned g = (p >> 8) & 255;
                        unsigned b = p & 255;
                        if (r > g + 48 && r > b + 48 && r > 90) ++red;
                        if (g > r + 37 && g > b + 37 && g > 80) ++green;
                        if (b > r + 37 && b > g + 37 && b > 80) ++blue;
                        hash ^= p;
                        hash *= UINT64_C(1099511628211);
                    }
                }
                XDestroyImage(image);
                if (samples && prev_hash != hash) ++changes;
                prev_hash = hash;
                ++samples;
                if (red >= 2) ++red_frames;
                if (blue >= 2) ++blue_frames;
                if (green >= 2) ++green_frames;
                if (red + blue + green >= 3) {
                    ++colored;
                    consecutive_blank = 0;
                } else {
                    ++blank;
                    ++consecutive_blank;
                    if (consecutive_blank > max_blank)
                        max_blank = consecutive_blank;
                }
            } else {
                ++invalid;
            }
        }
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 40000000L};
        nanosleep(&pause, NULL);
    }
    printf("S32_GLX_COLOR_SAMPLES=%d COLORED=%d BLANK=%d MAX_BLANK_RUN=%d RED=%d GREEN=%d BLUE=%d CHANGED=%d INVALID=%d ERRORS=%d\n",
           samples, colored, blank, max_blank, red_frames, green_frames,
           blue_frames, changes, invalid, errors);
    XCloseDisplay(d);
    return samples > 10 ? 0 : 5;
}
