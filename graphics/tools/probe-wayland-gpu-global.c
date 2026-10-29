/* Read-only Wayland GPU protocol discovery. Does not create a surface. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wayland-client.h>
struct findings {
    uint32_t linux_dmabuf, ahb_only, legacy_ahb, compositor, xdg_wm;
};
static void global(void *data, struct wl_registry *registry,
                   uint32_t name, const char *interface, uint32_t version) {
    (void)registry; (void)name;
    struct findings *f=data;
    if (!strcmp(interface,"zwp_linux_dmabuf_v1")) f->linux_dmabuf=version;
    else if (!strcmp(interface,"alroot_gpu_buffer_v1")) f->ahb_only=version;
    else if (!strcmp(interface,"banner_ahb_v1")) f->legacy_ahb=version;
    else if (!strcmp(interface,"wl_compositor")) f->compositor=version;
    else if (!strcmp(interface,"xdg_wm_base")) f->xdg_wm=version;
}
static void removed(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener listener={
    .global=global, .global_remove=removed
};
int main(void) {
    struct wl_display *display=wl_display_connect(NULL);
    if (!display) { puts("ALROOT_GPU_WAYLAND=NO_CONNECTION"); return 2; }
    struct wl_registry *registry=wl_display_get_registry(display);
    struct findings f={0};
    wl_registry_add_listener(registry,&listener,&f);
    if (wl_display_roundtrip(display)<0) {
        puts("ALROOT_GPU_WAYLAND=REGISTRY_FAILED");
        return 2;
    }
    printf("ALROOT_GPU_WAYLAND_COMPOSITOR_VERSION=%u\n",f.compositor);
    printf("ALROOT_GPU_WAYLAND_XDG_VERSION=%u\n",f.xdg_wm);
    printf("ALROOT_GPU_LINUX_DMABUF_VERSION=%u\n",f.linux_dmabuf);
    printf("ALROOT_GPU_LEGACY_AHB_VERSION=%u\n",f.legacy_ahb);
    printf("ALROOT_GPU_AHB_ONLY_VERSION=%u\n",f.ahb_only);
    printf("ALROOT_GPU_AHB_ONLY_AVAILABLE=%u\n",f.ahb_only>0?1u:0u);
    puts("ALROOT_GPU_FRAME_VISIBLE=NOT_VERIFIED");
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return 0;
}
