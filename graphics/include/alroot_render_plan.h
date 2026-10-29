#ifndef ALROOT_RENDER_PLAN_H
#define ALROOT_RENDER_PLAN_H

/* Default API routing after buffer sharing is PROVEN, not merely advertised.
 * Vendor names never participate. RADV-Xclipse is another Vulkan ICD.
 *
 * A route being selected here does NOT mean it is wired into the launcher:
 * the live Xwayland adapter and host GPU-copy path must pass acceptance.
 */
enum alroot_app_api {
    ALROOT_API_VULKAN,
    ALROOT_API_OPENGL,
    ALROOT_API_GLES,
    ALROOT_API_DXVK,
    ALROOT_API_VKD3D,
    ALROOT_API_XWAYLAND_COMPOSITOR
};

enum alroot_render_backend {
    ALROOT_RENDER_SOFTWARE,
    ALROOT_RENDER_VULKAN_WSI,
    ALROOT_RENDER_ZINK_VULKAN_WSI,
    ALROOT_RENDER_EGL_NATIVE,
    ALROOT_RENDER_EGL_ANGLE_VULKAN,
    ALROOT_RENDER_VIRGL_MOBILEGL,
    ALROOT_RENDER_GLAMOR_GBM,
    ALROOT_RENDER_GLAMOR_ZINK,
    ALROOT_RENDER_GLAMOR_EGL_SURFACELESS,
    ALROOT_RENDER_GLAMOR_ANGLE_AHB
};

struct alroot_render_caps {
    /* Set only after VkImage import and visible present are validated. */
    int native_vulkan_wsi_visible;
    int zink_over_vulkan_visible;
    /* EGL context creation by itself must NEVER set these. */
    int opengl_egl_visible;
    int gles_egl_visible;
    int egl_uses_angle_vulkan;
    int virgl_mobilegl_visible;
    /* Xwayland's internal GLAMOR path is a separate GPU client. */
    int glamor_gbm_visible;
    int glamor_zink_visible;
    int glamor_egl_surfaceless_visible;
    int glamor_angle_ahb_visible;
};

struct alroot_render_plan {
    enum alroot_render_backend backend;
    /* API translators only; not an FPS or an estimate of memory copies. */
    unsigned api_translation_layers;
    int gpu_accelerated;
};

struct alroot_render_plan alroot_choose_render_plan(
    enum alroot_app_api api, const struct alroot_render_caps *caps);
const char *alroot_render_plan_name(enum alroot_render_backend backend);

#endif
