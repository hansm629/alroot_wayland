// Read the actual parent Wayland wl_output CURRENT mode; no mode can be
// advertised by this diagnostic unless the Android host sent it.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

struct probe {
    struct wl_output *output;
    int32_t mhz;
};

static void geom(void *p, struct wl_output *out, int x, int y,
        int w, int h, int subpixel, const char *make,
        const char *model, int transform) {}
static void mode(void *p, struct wl_output *out, uint32_t flags,
        int w, int h, int refresh) {
    struct probe *probe = p;
    if ((flags & WL_OUTPUT_MODE_CURRENT) && refresh > 0)
        probe->mhz = refresh;
}
static void done(void *p, struct wl_output *out) {}
static void scale(void *p, struct wl_output *out, int factor) {}
static const struct wl_output_listener output_listener = {
    .geometry = geom, .mode = mode, .done = done, .scale = scale,
};
static void global(void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version) {
    struct probe *probe = data;
    if (!probe->output && !strcmp(interface, wl_output_interface.name)) {
        probe->output = wl_registry_bind(registry, name, &wl_output_interface,
                version >= 2 ? 2 : 1);
        if (probe->output)
            wl_output_add_listener(probe->output, &output_listener, probe);
    }
}
static void global_remove(void *p, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {
    .global = global, .global_remove = global_remove,
};
int main(void) {
    struct probe probe = {0};
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) return 2;
    struct wl_registry *reg = wl_display_get_registry(display);
    if (!reg) return 2;
    wl_registry_add_listener(reg, &registry_listener, &probe);
    int result = wl_display_roundtrip(display);
    if (result >= 0) result = wl_display_roundtrip(display);
    if (probe.output) wl_output_destroy(probe.output);
    wl_registry_destroy(reg);
    wl_display_disconnect(display);
    if (result < 0 || probe.mhz < 30000 || probe.mhz > 240000) return 3;
    printf("%d\n", probe.mhz);
    return 0;
}
