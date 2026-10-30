/* S29: sample known-grey Vulkan vkcube window edges, not desktop content.
 * XGetImage is used only by this diagnostic client, never in the server.
 * Includes center-bottom/right-edge to catch strips that four corners miss.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int xerrors;
static int on_error(Display *d, XErrorEvent *e)
{
    (void)d;
    (void)e;
    xerrors++;
    return 0;
}
static int64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static int grey(unsigned long v)
{
    int r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
    return r >= 36 && r <= 70 && g >= 36 && g <= 70 && b >= 36 && b <= 70;
}

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    char *end = NULL;
    unsigned long id = strtoul(argv[1], &end, 0);
    int duration = atoi(argv[2]), interval = atoi(argv[3]);
    if (!id || *end || duration < 1 || duration > 60 || interval < 2 || interval > 100)
        return 2;
    XSetErrorHandler(on_error);
    Display *d = XOpenDisplay(NULL);
    if (!d) return 3;
    enum {N = 9};
    const char *names[N] = {"tl", "tr", "bl", "br", "right", "right_in",
                             "bottom", "bottom_in", "bottom_deep"};
    int samples = 0, invalid = 0, errors = 0, black[N] = {0}, offcolor[N] = {0};
    int64_t end_us = now_us() + (int64_t)duration * 1000000;
    while (now_us() < end_us) {
        XWindowAttributes attr;
        if (!XGetWindowAttributes(d, id, &attr) || attr.map_state != IsViewable ||
            attr.width < 50 || attr.height < 50) {
            invalid++;
        } else {
            int w = attr.width, h = attr.height;
            int x[N] = {8,w-9,8,w-9,w-3,w-9,w/2,w/2,w/2};
            int y[N] = {8,8,h-9,h-9,h/2,h/2,h-3,h-9,h-16};
            for (int i = 0; i < N; ++i) {
                XImage *im = XGetImage(d, id, x[i], y[i], 1, 1,
                                       AllPlanes, ZPixmap);
                if (!im) { invalid++;continue; }
                unsigned long v = XGetPixel(im, 0, 0) & 0xffffff;
                if (!v) black[i]++;
                if (!grey(v)) offcolor[i]++;
                XDestroyImage(im);
            }
            samples++;
            XSync(d, False);
            if (xerrors) { errors += xerrors;xerrors = 0; }
        }
        struct timespec pause = {0, (long)interval * 1000000L};
        nanosleep(&pause, NULL);
    }
    for (int i = 0; i < N; ++i)
        printf("S29_EDGE_%s samples=%d black=%d off_grey=%d\n",
               names[i], samples, black[i], offcolor[i]);
    printf("S29_EDGE_SUMMARY samples=%d invalid=%d xerror=%d\n",
           samples, invalid, errors);
    XCloseDisplay(d);
    return samples > 0 && errors == 0 ? 0 : 5;
}
