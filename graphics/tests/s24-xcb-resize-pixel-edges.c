/* Read only the newly-created S24 vkcube X11 window, never the root screen.
 * The cube's background (dark neutral grey) must remain nonblack and stable
 * all the way to the last logical X11 row after the AHB physical padding.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>

static int xerrors;
static int on_error(Display *display, XErrorEvent *e)
{
    (void)display;
    fprintf(stderr, "S24_X11_EDGE_XERROR=%u\n", e->error_code);
    ++xerrors;
    return 0;
}

static int grey(unsigned long rgb)
{
    unsigned r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    return r >= 36 && r <= 70 && g >= 36 && g <= 70 && b >= 36 && b <= 70;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    char *end = NULL;
    unsigned long xid = strtoul(argv[1], &end, 0);
    if (!xid || !end || *end) return 2;
    XSetErrorHandler(on_error);
    Display *display = XOpenDisplay(NULL);
    if (!display) return 3;
    XWindowAttributes attr;
    if (!XGetWindowAttributes(display, xid, &attr) || attr.map_state != IsViewable ||
        attr.width < 96 || attr.height < 96) { XCloseDisplay(display);return 4; }

    const int x_coords[] = {8,attr.width-9};
    const int y_coords[] = {8,attr.height-8,attr.height-4,
                            attr.height-3,attr.height-2,attr.height-1};
    int pass = 0, total = 0;
    unsigned long minval = ~0UL, maxval = 0;
    for (unsigned i = 0; i < sizeof(x_coords)/sizeof(x_coords[0]); ++i) {
        for (unsigned j = 0; j < sizeof(y_coords)/sizeof(y_coords[0]); ++j) {
            const int x = x_coords[i], y = y_coords[j];
            XImage *image = XGetImage(display, xid, x, y, 1, 1, AllPlanes, ZPixmap);
            ++total;
            if (!image) continue;
            unsigned long pixel = XGetPixel(image, 0, 0) & 0xffffff;
            if (pixel < minval) minval = pixel;
            if (pixel > maxval) maxval = pixel;
            if (grey(pixel)) ++pass;
            XDestroyImage(image);
        }
    }
    XSync(display, False);
    printf("S24_X11_LOGICAL_SIZE=%dx%d PADDED_BGRA_LAST_ROW=%d/%d "
           "background_range=0x%lx..0x%lx xerrors=%d\n",
           attr.width,attr.height,pass,total,minval,maxval,xerrors);
    XCloseDisplay(display);
    return pass == total && xerrors == 0 ? 0 : 5;
}
