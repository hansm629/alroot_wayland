/* S26: request an EWMH resize of ONE known test window via xfwm4.
 * No pointer, compositor, desktop resolution, or unrelated window changes.
 * Usage: s26-xfwm-resize WINDOW_ID X Y WIDTH HEIGHT
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 6) return 2;
    char *end = NULL;
    const unsigned long xid = strtoul(argv[1], &end, 0);
    if (!xid || !end || *end) return 2;
    int x = atoi(argv[2]), y = atoi(argv[3]);
    int w = atoi(argv[4]), h = atoi(argv[5]);
    if (w < 16 || h < 16 || w > 1920 || h > 1080) return 2;
    Display *d = XOpenDisplay(NULL);
    if (!d) return 3;
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.send_event = True;
    event.xclient.display = d;
    event.xclient.window = (Window)xid;
    event.xclient.message_type = XInternAtom(d, "_NET_MOVERESIZE_WINDOW", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1 | (15 << 8) | (2 << 12);
    event.xclient.data.l[1] = x;
    event.xclient.data.l[2] = y;
    event.xclient.data.l[3] = w;
    event.xclient.data.l[4] = h;
    int sent = XSendEvent(d, DefaultRootWindow(d), False,
                          SubstructureRedirectMask | SubstructureNotifyMask,
                          &event);
    XFlush(d);
    XCloseDisplay(d);
    if (!sent) fputs("S26_XFWM_RESIZE_REQUEST_FAILED\n", stderr);
    return sent ? 0 : 4;
}
