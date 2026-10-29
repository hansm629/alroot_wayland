#include "alroot_render_plan.h"
#include <assert.h>
#include <stdio.h>

static void expect(const char *name, enum alroot_app_api api,
                   struct alroot_render_caps c,
                   enum alroot_render_backend backend, unsigned translators) {
    struct alroot_render_plan p=alroot_choose_render_plan(api, &c);
    assert(p.backend==backend);
    assert(p.api_translation_layers==translators);
    assert(p.gpu_accelerated==(backend!=ALROOT_RENDER_SOFTWARE));
    printf("PASS %-35s %-29s API translators=%u\n",
           name, alroot_render_plan_name(p.backend), translators);
}

int main(void) {
    const struct alroot_render_caps vk = {
        .native_vulkan_wsi_visible=1, .zink_over_vulkan_visible=1
    };
    expect("Native Vulkan vendor-independent", ALROOT_API_VULKAN, vk,
           ALROOT_RENDER_VULKAN_WSI, 0);
    expect("DXVK/VKD3D direct Vulkan", ALROOT_API_DXVK, vk,
           ALROOT_RENDER_VULKAN_WSI, 0);
    expect("Future RADV Vulkan ICD", ALROOT_API_VKD3D, vk,
           ALROOT_RENDER_VULKAN_WSI, 0);
    expect("OpenGL Zink over native Vulkan", ALROOT_API_OPENGL, vk,
           ALROOT_RENDER_ZINK_VULKAN_WSI, 1);
    struct alroot_render_caps native = vk;
    native.opengl_egl_visible=1;
    expect("Native OpenGL when presentable", ALROOT_API_OPENGL, native,
           ALROOT_RENDER_EGL_NATIVE, 0);
    struct alroot_render_caps angle = vk;
    angle.egl_uses_angle_vulkan=1;
    angle.opengl_egl_visible=1;
    angle.gles_egl_visible=1;
    expect("Xclipse ANGLE: GLES one translation", ALROOT_API_GLES, angle,
           ALROOT_RENDER_EGL_ANGLE_VULKAN, 1);
    expect("Xclipse OpenGL: avoid GL4ES ANGLE", ALROOT_API_OPENGL, angle,
           ALROOT_RENDER_ZINK_VULKAN_WSI, 1);
    expect("Xclipse DXVK avoids ANGLE", ALROOT_API_DXVK, angle,
           ALROOT_RENDER_VULKAN_WSI, 0);
    angle.gles_egl_visible=0; /* EGL works but export to host did NOT work. */
    expect("ANGLE no export: Vulkan-backed Zink", ALROOT_API_GLES, angle,
           ALROOT_RENDER_ZINK_VULKAN_WSI, 1);
    angle.native_vulkan_wsi_visible=0;
    expect("No verified GPU present, no fake accel", ALROOT_API_GLES, angle,
           ALROOT_RENDER_SOFTWARE, 0);
    expect("ANGLE Xwayland without AHB bridge", ALROOT_API_XWAYLAND_COMPOSITOR, angle,
           ALROOT_RENDER_SOFTWARE, 0);
    angle.glamor_angle_ahb_visible=1;
    expect("ANGLE Xwayland with proven AHB", ALROOT_API_XWAYLAND_COMPOSITOR, angle,
           ALROOT_RENDER_GLAMOR_ANGLE_AHB, 1);
    struct alroot_render_caps xz = vk;
    xz.glamor_zink_visible=1;
    expect("Xwayland GLES over direct Vulkan", ALROOT_API_XWAYLAND_COMPOSITOR, xz,
           ALROOT_RENDER_GLAMOR_ZINK, 1);
    xz.glamor_gbm_visible=1;
    expect("DRM/GBM when valid", ALROOT_API_XWAYLAND_COMPOSITOR, xz,
           ALROOT_RENDER_GLAMOR_GBM, 0);
    struct alroot_render_caps virgl = { .virgl_mobilegl_visible=1 };
    expect("Optional virgl MobileGL", ALROOT_API_OPENGL, virgl,
           ALROOT_RENDER_VIRGL_MOBILEGL, 1);
    struct alroot_render_caps other_egl = { .gles_egl_visible=1 };
    expect("Mali/PVR native GLES", ALROOT_API_GLES, other_egl,
           ALROOT_RENDER_EGL_NATIVE, 0);
    assert(alroot_choose_render_plan(ALROOT_API_VULKAN, NULL).backend ==
           ALROOT_RENDER_SOFTWARE);
    puts("PASS: no unconditional ANGLE chain, KGSL assumption or vendor ID");
    return 0;
}
