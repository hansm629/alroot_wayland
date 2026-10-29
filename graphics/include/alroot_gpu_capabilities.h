#ifndef ALROOT_GPU_CAPABILITIES_H
#define ALROOT_GPU_CAPABILITIES_H

/* Vendor-neutral, Android-independent feature policy for Alroot GPU stage 1.
 * This is currently an independently-tested DESIGN CONTRACT, not live routing.
 * Do not confuse EGL context creation with successful display-buffer export.
 * No vendor/device ID, system-wide environment override or DRI3 path here.
 */
enum alroot_xwayland_backend {
    ALROOT_XW_PIXMAN = 0,
    ALROOT_XW_GLAMOR_GBM,
    ALROOT_XW_GLAMOR_SURFACELESS_DMABUF,
    ALROOT_XW_GLAMOR_AHB_BRIDGE
};

enum alroot_frame_transport {
    ALROOT_FRAME_UNAVAILABLE = 0,
    ALROOT_FRAME_DMABUF_VKIMAGE,
    ALROOT_FRAME_AHB_VKIMAGE,
    ALROOT_FRAME_WL_SHM
};

struct alroot_gpu_caps {
    /* Xwayland/EGL requirements (ANGLE Vulkan-backed EGL is allowed here). */
    int egl_usable;
    int egl_gbm;
    int egl_surfaceless;
    int drm_render_node_accessible;
    int egl_dmabuf_export;
    int egl_dmabuf_import;
    int wl_dmabuf_available;
    /* These two adapters MUST be an implemented and tested code path,
     * not simply an EGL or Vulkan extension reported by the driver. */
    int xwayland_surfaceless_export_adapter;
    int xwayland_ahb_present_adapter;
    int egl_ahb_render_target_supported;

    /* wl_buffer -> Alroot VkImage sharing. Both sides must work. */
    int client_dmabuf_export;
    int host_dmabuf_vk_import;
    int dma_format_compatible;
    int client_ahb_transport;
    int host_ahb_vk_import;
    int wl_shm_fallback_available;
};

enum alroot_xwayland_backend alroot_pick_xwayland_backend(const struct alroot_gpu_caps *caps);
enum alroot_frame_transport alroot_pick_frame_transport(const struct alroot_gpu_caps *caps);
const char *alroot_xwayland_backend_name(enum alroot_xwayland_backend value);
const char *alroot_frame_transport_name(enum alroot_frame_transport value);

#endif
