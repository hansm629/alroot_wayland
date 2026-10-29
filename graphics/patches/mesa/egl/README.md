# GPU-S1.2: Mesa Wayland EGL -> Zink Kopper -> Vulkan AHB WSI

Candidate: 0001-zink-kopper-ahb-wayland-no-rendernode.patch
Source: S26U Mesa in ~/alroot-vk-lab/vulkan-wrapper-android,
commit b3b32abb82af (Mesa 26.3.0-devel).
Modified source: src/egl/drivers/dri2/platform_wayland.c ONLY.
X11/DRI3 and the installed Termux EGL libraries are NOT modified.

## Why the patch is necessary

On S26U, Mesa Zink reports a GPU-backed GLES renderer but Wayland
EGL falls back to wl_shm CPU-upload buffers when no DRM render node
is accessible. eglSwapBuffers succeeds, yet the Android host receives
no importable GPU image. Freedreno KGSL instead creates DMA-BUF
wl_buffers, which Android Vulkan rejects with INVALID_EXTERNAL_HANDLE.

Setting LIBGL_ALWAYS_SOFTWARE=1 to force a swrast-style Wayland EGL
bootstrap is NOT a valid workaround: it forces Zink to seek a CPU
Vulkan physical device and fails on mobile hardware.

The candidate introduces an explicit, per-process opt-in:

    ALROOT_EGL_WAYLAND_KOPPER=1
    MESA_LOADER_DRIVER_OVERRIDE=zink
    GALLIUM_DRIVER=zink
    VK_DRIVER_FILES=/path/to/private/wrapper_icd.private.json

This selects the Wayland swrast-style *initialization* (for wl_shm
format enumeration without a DRM render node), but keeps Zink selected
and ForceSoftware=false. Mesa's existing kopper renderer should
therefore connect GL/GLES rendering to the Vulkan Wayland WSI rather
than uploading pixels through wl_shm. The existing wrapper WSI
uses Alroot's public-NDK AHB-backed wl_buffer protocol on supporting
servers and lets the host composite VkImages.

This switch is opt-in, specific to the Zink GPU backend and not to any
GPU brand. Other Wayland servers and X11/DRI3 remain upstream. A
production runner must verify Alroot's protocol before selecting this.

## Acceptance

graphics/tools/test-s12-wayland-egl.sh compiles a native Wayland EGL
GLES window app, submits changing-color frames with eglSwapBuffers,
and checks host import evidence separately. A GPU renderer string or
successful eglSwapBuffers is NOT a GPU-copy PASS.

graphics/tools/build-s12-zink-egl-private.sh builds a candidate Mesa
EGL in an independent source overlay, never replacing Termux libraries.
Both Zink/Kopper and KGSL paths remain TEST-PENDING until the Android
host actually imports and displays their frames.
