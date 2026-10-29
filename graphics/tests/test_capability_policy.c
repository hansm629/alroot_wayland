#include "alroot_gpu_capabilities.h"
#include <assert.h>
#include <stdio.h>

static void scenario(const char *name, struct alroot_gpu_caps c,
                     enum alroot_xwayland_backend xw, enum alroot_frame_transport buffer) {
    assert(alroot_pick_xwayland_backend(&c) == xw);
    assert(alroot_pick_frame_transport(&c) == buffer);
    printf("PASS %-25s Xwayland=%s transport=%s\n", name,
           alroot_xwayland_backend_name(xw), alroot_frame_transport_name(buffer));
}

int main(void) {
    const struct alroot_gpu_caps dma = {
        .egl_usable=1, .egl_surfaceless=1, .egl_dmabuf_export=1,
        .egl_dmabuf_import=1, .wl_dmabuf_available=1,
        .xwayland_surfaceless_export_adapter=1, .client_dmabuf_export=1,
        .host_dmabuf_vk_import=1, .dma_format_compatible=1,
        .wl_shm_fallback_available=1
    };
    scenario("KGSL optional adapter", dma,
             ALROOT_XW_GLAMOR_SURFACELESS_DMABUF, ALROOT_FRAME_DMABUF_VKIMAGE);
    scenario("Mali/PVR EGL exporter", dma,
             ALROOT_XW_GLAMOR_SURFACELESS_DMABUF, ALROOT_FRAME_DMABUF_VKIMAGE);

    struct alroot_gpu_caps gbm = dma;
    gbm.egl_gbm=1; gbm.drm_render_node_accessible=1;
    scenario("accessible DRM/GBM", gbm,
             ALROOT_XW_GLAMOR_GBM, ALROOT_FRAME_DMABUF_VKIMAGE);

    struct alroot_gpu_caps ahb = dma;
    ahb.host_dmabuf_vk_import=0;
    ahb.client_ahb_transport=1; ahb.host_ahb_vk_import=1;
    scenario("wrapper AHB GPU-copy", ahb,
             ALROOT_XW_GLAMOR_SURFACELESS_DMABUF, ALROOT_FRAME_AHB_VKIMAGE);

    /* Xclipse's ANGLE GLES-over-Vulkan does not imply DMA-BUF export.
     * A viable EGL context alone must NEVER force GLAMOR acceleration. */
    struct alroot_gpu_caps angle = {
        .egl_usable=1, .egl_surfaceless=1, .egl_dmabuf_export=0,
        .wl_dmabuf_available=1, .host_ahb_vk_import=1,
        .wl_shm_fallback_available=1
    };
    scenario("Xclipse ANGLE no export", angle,
             ALROOT_XW_PIXMAN, ALROOT_FRAME_WL_SHM);
    angle.egl_ahb_render_target_supported=1;
    angle.client_ahb_transport=1;
    angle.xwayland_ahb_present_adapter=1; /* later, only once implemented */
    scenario("ANGLE explicit AHB bridge", angle,
             ALROOT_XW_GLAMOR_AHB_BRIDGE, ALROOT_FRAME_AHB_VKIMAGE);
    angle.xwayland_ahb_present_adapter=0;
    scenario("ANGLE adapter not built", angle,
             ALROOT_XW_PIXMAN, ALROOT_FRAME_AHB_VKIMAGE);

    struct alroot_gpu_caps no_export = dma;
    no_export.xwayland_surfaceless_export_adapter=0;
    no_export.client_dmabuf_export=0;
    scenario("export adapter absent", no_export,
             ALROOT_XW_PIXMAN, ALROOT_FRAME_WL_SHM);
    struct alroot_gpu_caps unknown = {0};
    scenario("unknown GPU no false accel", unknown,
             ALROOT_XW_PIXMAN, ALROOT_FRAME_UNAVAILABLE);
    assert(alroot_pick_frame_transport(NULL) == ALROOT_FRAME_UNAVAILABLE);
    assert(alroot_pick_xwayland_backend(NULL) == ALROOT_XW_PIXMAN);
    return 0;
}
