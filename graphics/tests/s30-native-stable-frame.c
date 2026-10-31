/* S30 stable-last-real-frame proof over tiny X11 window extents.
 * No GLX client redraws are issued after the resize: exactly the server's
 * previous-frame preservation path must determine the visible geometry.
 * XGetImage is used in this TEST ONLY, never in Xwayland GPU code.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void sleep_ms(long ms)
{
    struct timespec ts = {.tv_sec = ms / 1000,
                          .tv_nsec = (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

int main(void)
{
    Display *d = XOpenDisplay(NULL);
    if (!d) return 2;
    const int oldw = 200, oldh = 140, neww = 400, newh = 280;
    const int original_red_width = 20;
    const unsigned long background = 0x333333;
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 60, 60,
                                  oldw, oldh, 0, 0, background);
    if (!w) return 3;
    /* Identical to the normal Mesa Vulkan XCB WSI window marker; this
     * synthetic test window never touches an existing user application. */
    const Atom marker = XInternAtom(d, "_MESA_DRV", False);
    XChangeProperty(d, w, marker, XA_STRING, 8, PropModeReplace,
                    (const unsigned char *)"0", 1);
    XMapWindow(d, w);
    XSync(d, False);
    /* XFWM4's asynchronous MapRequest, reparent and XComposite redirect
     * replace the initial window pixmap. Start the *synthetic test* drawing
     * only after that handshake, like real vkcube/glxgears applications
     * that continually repaint after their window becomes mapped. */
    for (int i = 0; i < 20; ++i) {
        XWindowAttributes a;
        if (XGetWindowAttributes(d, w, &a) && a.map_state == IsViewable)
            break;
        sleep_ms(30);
    }
    sleep_ms(230);

    GC gc = XCreateGC(d, w, 0, NULL);
    XSetForeground(d, gc, background);
    XFillRectangle(d, w, gc, 0, 0, oldw, oldh);
    XSetForeground(d, gc, 0xff0000);
    XFillRectangle(d, w, gc, (oldw - original_red_width) / 2,
                   60, original_red_width, 20);
    /* Intentionally put distinct colored strips at the old right and
     * bottom edges. S28's full-image bilinear expansion pulls both strips
     * out to the new right/bottom edges (the user's gear-trail symptom),
     * whereas S29 must fill those new outer pixels with the background. */
    XFillRectangle(d, w, gc, oldw - 6, 25, 6, oldh - 50);
    XSetForeground(d, gc, 0x0000ff);
    XFillRectangle(d, w, gc, 30, oldh - 6, oldw - 60, 6);
    XSync(d, False);
    sleep_ms(160);

    XImage *initial = XGetImage(d, w, oldw / 2, 70, 1, 1,
                                AllPlanes, ZPixmap);
    const unsigned long initial_pixel = initial ?
        (XGetPixel(initial, 0, 0) & 0xffffff) : 0;
    printf("S30_ORIGINAL_RED_PIXEL=0x%06lx\n", initial_pixel);
    if (initial) XDestroyImage(initial);

    /* Reproduce the user's back-and-forth mouse drag. With no new client
     * rendering, an S28 resized placeholder was repeatedly resampled and
     * could lose sharpness/position on every step. S29 must keep original
     * 20px geometry despite the sequence of changing aspect ratios. */
    const int sizes[][2] = {
        {240, 180}, {1, 98}, {320, 360}, {8, 98},
        {260, 260}, {40, 98}, {420, 190}, {380, 320}
    };
    const int resize_steps = 24;
    for (int i = 0; i < resize_steps; ++i) {
        XResizeWindow(d, w, sizes[i % 8][0], sizes[i % 8][1]);
        XSync(d, False);
        sleep_ms(24);
        if (i < 8) {
            const int ew = sizes[i % 8][0], eh = sizes[i % 8][1];
            XImage *step = XGetImage(d, w, ew / 2, eh / 2,
                                     1, 1, AllPlanes, ZPixmap);
            if (step) {
                printf("S30_STEP_%d_CENTER=0x%06lx\n", i + 1,
                       XGetPixel(step, 0, 0) & 0xffffff);
                XDestroyImage(step);
            }
        }
    }
    XResizeWindow(d, w, neww, newh);
    XSync(d, False);
    sleep_ms(35);

    XImage *img = XGetImage(d, w, 0, 0, neww, newh,
                            AllPlanes, ZPixmap);
    if (!img) {
        puts("S30_XCB_NATIVE_FRAME=FAIL_XGETIMAGE");
        return 4;
    }
    int red_width = 0, red_min = neww, red_max = -1, bg_corners = 0;
    for (int x = neww / 2 - 65; x < neww / 2 + 65; ++x) {
        unsigned long value = XGetPixel(img, x, newh / 2) & 0xffffff;
        const unsigned red = (value >> 16) & 255;
        const unsigned green = (value >> 8) & 255;
        const unsigned blue = value & 255;
        if (red > 205 && green < 45 && blue < 45) {
            ++red_width;
            if (x < red_min) red_min = x;
            if (x > red_max) red_max = x;
        }
    }
    const int cx[] = {4, neww - 5, 4, neww - 5};
    const int cy[] = {4, 4, newh - 5, newh - 5};
    for (int i = 0; i < 4; ++i) {
        unsigned long pixel = XGetPixel(img, cx[i], cy[i]) & 0xffffff;
        unsigned r = (pixel >> 16) & 255;
        unsigned g = (pixel >> 8) & 255;
        unsigned b = pixel & 255;
        if (r >= 35 && r <= 70 && g >= 35 && g <= 70 &&
            b >= 35 && b <= 70)
            ++bg_corners;
    }
    const unsigned long right_pixel = XGetPixel(img, neww - 3, newh / 2) & 0xffffff;
    const unsigned long bottom_pixel = XGetPixel(img, neww / 2, newh - 3) & 0xffffff;
    for (int offset = 2; offset <= 12; offset += 2) {
        unsigned long a = XGetPixel(img, neww / 4, newh - offset) & 0xffffff;
        unsigned long b = XGetPixel(img, neww / 2, newh - offset) & 0xffffff;
        unsigned long c = XGetPixel(img, neww * 3 / 4, newh - offset) & 0xffffff;
        printf("S30_BOTTOM_ROW_OFF=%d pixel_quarter=0x%06lx center=0x%06lx three_quarter=0x%06lx\n",
               offset, a, b, c);
    }
    const int right_is_clean =
        ((right_pixel >> 16) & 255) >= 35 && ((right_pixel >> 16) & 255) <= 70 &&
        ((right_pixel >> 8) & 255) >= 35 && ((right_pixel >> 8) & 255) <= 70 &&
        (right_pixel & 255) >= 35 && (right_pixel & 255) <= 70;
    const int bottom_is_clean =
        ((bottom_pixel >> 16) & 255) >= 35 && ((bottom_pixel >> 16) & 255) <= 70 &&
        ((bottom_pixel >> 8) & 255) >= 35 && ((bottom_pixel >> 8) & 255) <= 70 &&
        (bottom_pixel & 255) >= 35 && (bottom_pixel & 255) <= 70;

    printf("S30_NATIVE_RED_STRIP_WIDTH=%d original=%d min=%d max=%d\n",
           red_width, original_red_width, red_min, red_max);
    printf("S30_NATIVE_UNREDRAWN_RESIZE_STEPS=%d\n", resize_steps + 1);
    printf("S30_BACKGROUND_CORNER_MATCH=%d/4\n", bg_corners);
    printf("S30_NEW_RIGHT_EDGE_BG=%d pixel=0x%06lx\n", right_is_clean, right_pixel);
    printf("S30_NEW_BOTTOM_EDGE_BG=%d pixel=0x%06lx\n", bottom_is_clean, bottom_pixel);
    XDestroyImage(img);
    XFreeGC(d, gc);
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    /* The prior S29 repeated placeholder fallback should fail this pixel geometry
     * contract by scaling the old 20px red strip to ~40px. */
    const int in_center = (red_min >= neww / 2 - original_red_width &&
                           red_max <= neww / 2 + original_red_width);
    if (red_width >= 16 && red_width <= 24 && bg_corners == 4 && in_center &&
        right_is_clean && bottom_is_clean) {
        puts("S30_NATIVE_PIXEL_GEOMETRY=PASS_NO_SCALING_OR_EDGE_GHOST");
        return 0;
    }
    puts("S30_NATIVE_PIXEL_GEOMETRY=FAIL_STRETCHED_OR_STALE");
    return 5;
}
