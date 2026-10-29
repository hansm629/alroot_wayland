#include "alroot_render_plan.h"

static struct alroot_render_plan plan(enum alroot_render_backend b,
                                     unsigned layers) {
    return (struct alroot_render_plan) {
        .backend=b, .api_translation_layers=layers,
        .gpu_accelerated=b!=ALROOT_RENDER_SOFTWARE
    };
}

struct alroot_render_plan alroot_choose_render_plan(
    enum alroot_app_api api, const struct alroot_render_caps *c) {
    if (!c) return plan(ALROOT_RENDER_SOFTWARE, 0);
    switch (api) {
    case ALROOT_API_VULKAN:
    case ALROOT_API_DXVK:
    case ALROOT_API_VKD3D:
        /* Never wrap Vulkan in ANGLE/GLES, including future RADV-Xclipse. */
        if (c->native_vulkan_wsi_visible)
            return plan(ALROOT_RENDER_VULKAN_WSI, 0);
        break;
    case ALROOT_API_OPENGL:
        /* Native OpenGL/EGL needs no GL-to-Vulkan API translation. */
        if (c->opengl_egl_visible && !c->egl_uses_angle_vulkan)
            return plan(ALROOT_RENDER_EGL_NATIVE, 0);
        /* Vulkan-native Zink is preferable to GL4ES -> ANGLE -> Vulkan.
         * Both ICD and WSI transport must already have passed present. */
        if (c->native_vulkan_wsi_visible && c->zink_over_vulkan_visible)
            return plan(ALROOT_RENDER_ZINK_VULKAN_WSI, 1);
        if (c->virgl_mobilegl_visible)
            return plan(ALROOT_RENDER_VIRGL_MOBILEGL, 1);
        /* Do not auto-chain GL4ES over ANGLE's Vulkan-backed GLES. */
        break;
    case ALROOT_API_GLES:
        if (c->gles_egl_visible) {
            if (c->egl_uses_angle_vulkan)
                /* This is ONE GLES -> Vulkan translation, not a reason
                 * to ban ANGLE; its buffer transport must be proven. */
                return plan(ALROOT_RENDER_EGL_ANGLE_VULKAN, 1);
            return plan(ALROOT_RENDER_EGL_NATIVE, 0);
        }
        if (c->native_vulkan_wsi_visible && c->zink_over_vulkan_visible)
            return plan(ALROOT_RENDER_ZINK_VULKAN_WSI, 1);
        if (c->virgl_mobilegl_visible)
            return plan(ALROOT_RENDER_VIRGL_MOBILEGL, 1);
        break;
    case ALROOT_API_XWAYLAND_COMPOSITOR:
        if (c->glamor_gbm_visible)
            return plan(ALROOT_RENDER_GLAMOR_GBM, 0);
        if (c->native_vulkan_wsi_visible && c->glamor_zink_visible)
            return plan(ALROOT_RENDER_GLAMOR_ZINK, 1);
        if (c->glamor_egl_surfaceless_visible && !c->egl_uses_angle_vulkan)
            return plan(ALROOT_RENDER_GLAMOR_EGL_SURFACELESS, 0);
        if (c->glamor_angle_ahb_visible && c->egl_uses_angle_vulkan)
            return plan(ALROOT_RENDER_GLAMOR_ANGLE_AHB, 1);
        /* An ANGLE context with no verified export is NOT GLAMOR. */
        break;
    }
    return plan(ALROOT_RENDER_SOFTWARE, 0);
}

const char *alroot_render_plan_name(enum alroot_render_backend b) {
    switch (b) {
    case ALROOT_RENDER_VULKAN_WSI: return "vulkan-wsi-vkimage";
    case ALROOT_RENDER_ZINK_VULKAN_WSI: return "zink-vulkan-wsi-vkimage";
    case ALROOT_RENDER_EGL_NATIVE: return "native-egl-export";
    case ALROOT_RENDER_EGL_ANGLE_VULKAN: return "angle-vulkan-egl-export";
    case ALROOT_RENDER_VIRGL_MOBILEGL: return "virgl-mobilegl-export";
    case ALROOT_RENDER_GLAMOR_GBM: return "glamor-gbm";
    case ALROOT_RENDER_GLAMOR_ZINK: return "glamor-zink-vulkan";
    case ALROOT_RENDER_GLAMOR_EGL_SURFACELESS: return "glamor-egl-surfaceless";
    case ALROOT_RENDER_GLAMOR_ANGLE_AHB: return "glamor-angle-ahb";
    default: return "safe-software-fallback";
    }
}
