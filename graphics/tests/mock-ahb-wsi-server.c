/* Disposable fake Wayland compositor to prove protocol v2/v3 semantics.
 * No GPU hardware, Xwayland, Termux-X11 or live Alroot session is touched.
 */
#include <wayland-server.h>
#include "banner-ahb-v1-server-protocol.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void destroy_ahb(struct wl_client *c, struct wl_resource *r) {
    (void)c;
    wl_resource_destroy(r);
}
static void attach_ahb(struct wl_client *c, struct wl_resource *r,
                       struct wl_resource *buffer, int32_t sock,
                       uint32_t width, uint32_t height, uint32_t stride,
                       uint32_t hi, uint32_t lo, uint32_t count) {
    (void)c; (void)r; (void)buffer; (void)width; (void)height;
    (void)stride; (void)hi; (void)lo; (void)count;
    if (sock >= 0) close(sock);
}
static const struct banner_ahb_v1_interface ahb_impl = {
    .destroy=destroy_ahb, .attach=attach_ahb
};

struct mock { uint32_t version, flags; };
static void bind_mock(struct wl_client *client, void *data,
                      uint32_t version, uint32_t id) {
    struct mock *cfg = data;
    if (version > cfg->version) version = cfg->version;
    struct wl_resource *r = wl_resource_create(client, &banner_ahb_v1_interface,
                                               (int)version, id);
    if (!r) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &ahb_impl, NULL, NULL);
    if (version >= 2) banner_ahb_v1_send_mode(r, 0u);
    if (version >= 3)
        banner_ahb_v1_send_gpu_capabilities(r, cfg->flags);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s socket version flags\n", argv[0]);
        return 2;
    }
    struct mock cfg={ .version = (uint32_t)strtoul(argv[2], NULL, 0),
                      .flags = (uint32_t)strtoul(argv[3], NULL, 0) };
    if (cfg.version < 1 || cfg.version > 3) return 2;
    struct wl_display *display=wl_display_create();
    if (!display) return 2;
    if (wl_display_add_socket(display, argv[1]) ||
        !wl_global_create(display, &banner_ahb_v1_interface,
                           cfg.version, &cfg, bind_mock)) {
        wl_display_destroy(display);
        return 2;
    }
    wl_display_run(display);
    wl_display_destroy(display);
    return 0;
}
