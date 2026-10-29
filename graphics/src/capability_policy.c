#include "alroot_gpu_capabilities.h"

enum alroot_xwayland_backend alroot_pick_xwayland_backend(const struct alroot_gpu_caps *c) {
    if (!c || !c->egl_usable) return ALROOT_XW_PIXMAN;
    if (c->drm_render_node_accessible && c->egl_gbm &&
        c->wl_dmabuf_available)
        return ALROOT_XW_GLAMOR_GBM;
    if (c->egl_surfaceless && c->egl_dmabuf_export &&
        c->egl_dmabuf_import && c->wl_dmabuf_available &&
        c->xwayland_surfaceless_export_adapter)
        return ALROOT_XW_GLAMOR_SURFACELESS_DMABUF;
    if (c->egl_ahb_render_target_supported && c->host_ahb_vk_import &&
        c->client_ahb_transport && c->xwayland_ahb_present_adapter)
        return ALROOT_XW_GLAMOR_AHB_BRIDGE;
    return ALROOT_XW_PIXMAN;
}

enum alroot_frame_transport alroot_pick_frame_transport(const struct alroot_gpu_caps *c) {
    if (!c) return ALROOT_FRAME_UNAVAILABLE;
    if (c->wl_dmabuf_available && c->client_dmabuf_export &&
        c->host_dmabuf_vk_import && c->dma_format_compatible)
        return ALROOT_FRAME_DMABUF_VKIMAGE;
    if (c->client_ahb_transport && c->host_ahb_vk_import)
        return ALROOT_FRAME_AHB_VKIMAGE;
    if (c->wl_shm_fallback_available)
        return ALROOT_FRAME_WL_SHM;
    return ALROOT_FRAME_UNAVAILABLE;
}

const char *alroot_xwayland_backend_name(enum alroot_xwayland_backend v) {
    switch (v) {
    case ALROOT_XW_GLAMOR_GBM: return "glamor-gbm";
    case ALROOT_XW_GLAMOR_SURFACELESS_DMABUF: return "glamor-surfaceless-dmabuf";
    case ALROOT_XW_GLAMOR_AHB_BRIDGE: return "glamor-ahb-bridge";
    default: return "pixman";
    }
}

const char *alroot_frame_transport_name(enum alroot_frame_transport v) {
    switch (v) {
    case ALROOT_FRAME_DMABUF_VKIMAGE: return "dmabuf-vkimage-gpu-copy";
    case ALROOT_FRAME_AHB_VKIMAGE: return "ahb-vkimage-gpu-copy";
    case ALROOT_FRAME_WL_SHM: return "wl-shm-fallback";
    default: return "unavailable";
    }
}
