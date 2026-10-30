/* Enumerate mapped windows of an isolated diagnostic X server only. */
#include <X11/Xlib.h>
#include <stdio.h>

int main(void)
{
    Display *d = XOpenDisplay(NULL);
    if (!d) return 2;
    Window root = DefaultRootWindow(d), parent = 0, *children = NULL;
    unsigned int count = 0;
    if (XQueryTree(d, root, &root, &parent, &children, &count)) {
        for (unsigned i = 0; i < count; ++i) {
            XWindowAttributes attr;
            if (!XGetWindowAttributes(d, children[i], &attr)) continue;
            printf("0x%lx map=%d size=%dx%d\n", (unsigned long)children[i],
                   attr.map_state, attr.width, attr.height);
        }
        if (children) XFree(children);
    }
    XCloseDisplay(d);
    return 0;
}
