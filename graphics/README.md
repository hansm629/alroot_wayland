# Alroot graphics acceleration — capability-first stage 1

This directory holds **isolated, opt-in patches and tests**, not a replacement for
Termux's installed drivers, Termux-X11 `:0`, or the working Alroot P20 APK.

## Non-negotiable contracts

1. **No vendor names in common backend selection.** Adreno/KGSL, Mali,
   PowerVR, and Xclipse use the same capability contract. Driver or API names
   can be used for diagnostics and for *optional* adaptation patches.
2. **ANGLE Vulkan-backend EGL/GLES is an EGL implementation**, not a DRM/GBM
   device. A usable ANGLE context does **not** prove that an Xwayland GLAMOR
   render target is exportable to `linux-dmabuf` or an AHB. Probe functionality.
3. **Preserve Termux-X11 DRI3** for every driver. No changes to `$PREFIX/lib`,
   `$PREFIX/bin/Xwayland`, `/dev/*`, Vulkan ICD JSON or the Termux-X11 APK from
   this tree. The Alroot GPU Xwayland binary must be separate and selected
   only inside the Alroot session (after a private validation).
4. **Stage 1 is GPU-copy (VkImage), not zero-copy.** A shared buffer imported
   as a VkImage is composited or copied into the Android host swapchain.
   Explicit fence/overhead removal is stage 2; AHB direct scanout is stage 3.
5. A capability is *true* only if its complete producer→consumer path works.
   An extension string, EGL context, or `glxinfo -B` GPU name alone is not
   a visible-present PASS. If import/export fails, keep a valid fallback.

## Separate pipelines

```text
Native Vulkan app (wrapper / Mesa Turnip / other Vulkan ICD)
  -> Wayland WSI [DMA-BUF+modifier OR tested AHB-bearing wl_buffer]
  -> Alroot host [VkImage import] -> Vulkan composite/copy -> Android display

Native OpenGL/ES app (Mesa GL, Zink, virgl, ANGLE Vulkan EGL, other EGL)
  -> EGL/GL framebuffer with PROVEN shareable image transport
  -> Wayland wl_buffer -> same host import/copy path

X11 OpenGL/ES/Vulkan app (including DXVK/VKD3D via future Wine tests)
  -> isolated Alroot Xwayland [DRI3/GLX/GLAMOR/PRESENT]
  -> exported Wayland wl_buffer -> same host import/copy path

XFCE/Xfwm/Xfdesktop/Xfce-panel -> isolated GLAMOR Xwayland path,
not a proxy for whether an individual app renders on the GPU.
```

`graphics/include/alroot_gpu_capabilities.h` and
`graphics/src/capability_policy.c` contain the **standalone policy contract**;
they are not yet wired into the running compositor or Xwayland. The adapter
flags must only be enabled after that adapter actually exists. For example:

- DRM/GBM GLAMOR requires an accessible render node and compatible EGL/GBM.
- EGL surfaceless GLAMOR requires an EGL context, usable import/export,
  `linux-dmabuf` acceptance and an implemented Xwayland export adapter.
- ANGLE/Vulkan-backed GLES without `EGL_MESA_image_dma_buf_export` cannot
  silently select an EGL DMA-BUF export path; a separately implemented and
  proven AHB/EGL or Vulkan bridge is required.
- Direct Vulkan Wayland can select a different buffer route from the
  Xwayland EGL compositor. One successful API does not validate the other.
- If no GPU buffer export/import contract works, fall back to `wl_shm`
  **only if the producer supports it**; do not pretend an inaccessible GPU
  buffer can be read or copied by the host CPU.

## Proven S26U baseline, not a cross-vendor claim

On S26U (Adreno 840, Termux 26.3): `vkcube --wsi wayland` succeeded on both
wrapper and Turnip but the Alroot host **failed to import** their frames.
The wrapper submitted a `DRM_FORMAT_MOD_INVALID` buffer; Turnip submitted
`LINEAR` but the Android host returned `VK_ERROR_INVALID_EXTERNAL_HANDLE`.
Xwayland `:90` did not expose DRI3 because Termux upstream Xwayland 24.1.14
was built with `-Dglamor=false`; `glxinfo -B` showing Zink GPU acceleration
did not establish host display import success.

This motivates a generic AHB->VkImage **optional fallback**, not a special
Adreno code path. The AHB client transport and cross-driver import must
still be implemented and independently proven for each target configuration.
The patch under `graphics/patches/host` is a **candidate** only; it is not
applied to P20 or the live session.

## Source pins and later patch ports

- `lfdevs/termux-packages` PR #25 at `d03437f224ce3a85cc7f719bd79bb9e5cc2e6237`;
  Mesa package source `lfdevs/mesa-for-android-container` commit
  `6f90e92dfdacc4759d8e9ccb328805d0f0b4439b` (27.0.0-devel-20261025).
- S26U local Vulkan wrapper Mesa WSI checkout:
  `~/alroot-vk-lab/vulkan-wrapper-android` at `b3b32abb82` (local reference;
  not automatically modified).
- S26U local virgl/MobileGL reference:
  `~/alroot-virpipe` (local files, not automatically modified).
- `lfdevs/xwayland` PR #1 implements an opt-in **KGSL-specific** surfaceless
  GLAMOR+DRI3 adapter. Its patches were made for Debian Xwayland 24.1.6;
  they are *reference code*, not a vendor-independent Termux 24.1.14 patch.
  If ported, isolate them in `graphics/patches/xwayland/kgsl/`, never force
  KGSL or `XWAYLAND_FORCE_KGSL_SURFACELESS=1` in the common launcher.

## Validation gates

`bash graphics/tools/test-capabilities.sh`: unit-test common decisions and
`git apply --check` the optional Alroot host VkImage patch; does not alter
the active APK or session.

`bash graphics/tools/probe-egl.sh surfaceless` (or `wayland` with an explicitly
selected Wayland socket): read-only vendor EGL extension and GLES backend
probe, including ANGLE's real reported capabilities. It NEVER proves that
the compositor can import the resulting buffer.
In the S26U Mesa KGSL probe, the GLES renderer was an actual Adreno 840,
but `EGL_MESA_image_dma_buf_export` was **not advertised**, even though
`eglGetProcAddress("eglExportDMABUFImageMESA")` returned a non-null pointer.
The function pointer alone therefore must **not** enable GLAMOR DMA-BUF
export. The new `EGL_DMA_BUF_EXPORT_CANDIDATE` requires both checks and
still is not a functioning buffer-export proof.

`bash graphics/tools/snapshot-termux-x11.sh snapshot` then, after isolated
development, `... verify`: compare protected Termux renderer file hashes and
read-only DRI3 visibility on `:0`. This is **not** a substitute for visual
Termux-X11 DRI3 regression tests with the chosen driver.

Before declaring a combination supported, require actual window pixels and
logs demonstrating the Wayland client exported the frame, Alroot imported it,
and the Android Vulkan host presented it. Test both windowed/fullscreen and
the XFCE desktop, with resize, FIFO/MAILBOX, suspend/resume and keyboard.
Any Xclipse ANGLE or Mali/PowerVR result is **TEST-PENDING** until a suitable
physical device proves it.
