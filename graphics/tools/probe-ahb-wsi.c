/* Read-only Wayland negotiation probe. Never creates a surface or buffer.
 *
 * Build using wayland-scanner client-header and private-code from the
 * Stage-1 banner-ahb-v1.xml (v3). Works with legacy v2 servers as well.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "banner-ahb-v1-client-protocol.h"

struct probe_state {
    struct wl_display *display;
    struct banner_ahb_v1 *ahb;
    unsigned version;
    unsigned mode_seen, mode_on;
    unsigned capabilities_seen, flags;
};

static void on_mode(void *data, struct banner_ahb_v1 *ahb, uint32_t enabled) {
    (void)ahb;
    struct probe_state *p = data;
    p->mode_seen = 1;
    p->mode_on = enabled ? 1u : 0u;
}

static void on_gpu_capabilities(void *data, struct banner_ahb_v1 *ahb,
                                uint32_t flags) {
    (void)ahb;
    struct probe_state *p = data;
    p->capabilities_seen = 1;
    p->flags = flags;
}

static const struct banner_ahb_v1_listener ahb_listener = {
    .mode = on_mode,
    .gpu_capabilities = on_gpu_capabilities,
};

static void on_global(void *data, struct wl_registry *registry,
                      uint32_t name, const char *interface, uint32_t version) {
    struct probe_state *p = data;
    if (strcmp(interface, "banner_ahb_v1") || p->ahb) return;
    p->version = version < 3 ? version : 3;
    if (!p->version) return;
    p->ahb = wl_registry_bind(registry, name, &banner_ahb_v1_interface,
                               p->version);
    if (p->ahb) banner_ahb_v1_add_listener(p->ahb, &ahb_listener, p);
}

static void on_global_remove(void *data, struct wl_registry *registry,
                              uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = on_global,
    .global_remove = on_global_remove,
};

int main(void) {
    struct probe_state p = {0};
    p.display = wl_display_connect(NULL);
    if (!p.display) {
        puts("ALROOT_AHB_WSI=NO_WAYLAND_CONNECTION");
        return 2;
    }
    struct wl_registry *registry = wl_display_get_registry(p.display);
    if (!registry || wl_registry_add_listener(registry, &registry_listener, &p) ||
        wl_display_roundtrip(p.display) == -1 ||
        wl_display_roundtrip(p.display) == -1) {
        puts("ALROOT_AHB_WSI=PROTOCOL_ERROR");
        if (registry) wl_registry_destroy(registry);
        wl_display_disconnect(p.display);
        return 2;
    }
    printf("ALROOT_AHB_WSI=%s\n", p.ahb ? "ADVERTISED" : "UNAVAILABLE");
    printf("ALROOT_AHB_PROTOCOL_VERSION=%u\n", p.version);
    printf("ALROOT_DIRECT_LAYER_MODE=%s\n",
           p.mode_seen ? (p.mode_on ? "ON" : "OFF") : "UNKNOWN");
    printf("ALROOT_GPU_COPY_CAPABILITY_EVENT=%u\n", p.capabilities_seen);
    /* Never fabricate copy support when talking to a legacy v1/v2 host. */
    printf("ALROOT_AHB_VKIMAGE_GPU_COPY_CANDIDATE=%u\n",
           p.capabilities_seen && (p.flags & 1u) ? 1u : 0u);
    printf("ALROOT_DIRECT_LAYER_CAPABILITY=%u\n",
           p.capabilities_seen && (p.flags & 2u) ? 1u : 0u);
    if (p.ahb) banner_ahb_v1_destroy(p.ahb);
    wl_registry_destroy(registry);
    wl_display_disconnect(p.display);
    return 0;
}
