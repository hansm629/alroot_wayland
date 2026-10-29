#include <assert.h>
#include <stdio.h>
#include "xdg_geometry_policy.h"

static void check(const char *scenario,
                  int ow, int oh, int fullscreen, int maximized, int desktop,
                  int expected_w, int expected_h)
{
    int w = -1, h = -1;
    alroot_xdg_configure_dimensions(ow, oh, fullscreen, maximized, desktop,
                                    &w, &h);
    assert(w == expected_w && h == expected_h);
    printf("PASS: %-36s configure(%d,%d)\n", scenario, w, h);
}

int main(void)
{
    /* The user's 640x360 window was forced to allocate 1920x1080 AHBs.
     * Windowed 0x0 configure lets the client choose its own dimensions. */
    check("windowed Vulkan 640x360", 1920, 1080, 0, 0, 0, 0, 0);
    check("windowed GL and X11", 1920, 1080, 0, 0, 0, 0, 0);
    check("fullscreen Vulkan", 1920, 1080, 1, 0, 0, 1920, 1080);
    check("maximized GTK/Qt", 1920, 1080, 0, 1, 0, 1920, 1080);
    check("Xwayland desktop role", 1920, 1080, 0, 0, 1, 1920, 1080);
    check("rotated full-screen", 1080, 1920, 1, 0, 0, 1080, 1920);
    check("rotated desktop", 1080, 1920, 0, 0, 1, 1080, 1920);
    check("windowed after rotate", 1080, 1920, 0, 0, 0, 0, 0);
    check("unmaximize normal", 1920, 1080, 0, 0, 0, 0, 0);
    check("unset fullscreen", 1920, 1080, 0, 0, 0, 0, 0);
    check("uninitialized output", 0, 0, 1, 0, 0, 0, 0);
    check("partial output unknown", 1920, 0, 0, 0, 1, 1920, 0);
    puts("PASS: 12 cross-vendor, cross-desktop XDG resize policy cases");
    return 0;
}
