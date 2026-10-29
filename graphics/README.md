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

## Android system ANGLE/Vulkan: minimal-translation target

graphics/tools/probe-android-egl.sh queries the REAL Android system EGL/GLES
loader, distinct from Termux Mesa. This matters especially on Xclipse
devices whose GLES is implemented by ANGLE over Vulkan. S26U reported
Android EGL 1.5 with native-buffer and native-fence support, no EGL
DMA-BUF export, and Qualcomm native GLES. Xclipse is not yet tested.

graphics/tools/probe-android-ahb-egl.sh creates a GPU-only AHardwareBuffer,
imports it through Android EGLImage into a GLES framebuffer and submits a
GPU clear without CPU readback. It passed on S26U, but reports Wayland
present as NOT_TESTED. On S24+ this probe will test ANGLE's actual AHB
render target, without assuming that the driver supports DMA-BUF export.

For GLES apps with a verified ANGLE Vulkan AHB path, the performance goal
is GLES -> ANGLE Vulkan -> AHB -> host Vulkan VkImage GPU-copy. Do not
add GL4ES -> ANGLE -> additional GPU-copy stages by default. OpenGL apps
prefer native EGL or a proven Zink -> Vulkan ICD route; DXVK/VKD3D and
native Vulkan apps use direct Vulkan WSI. A future RADV-Xclipse ICD
uses the same Vulkan interface as Turnip/wrapper; common code does
not branch on the vendor name. API translation counts are structural
estimates, NOT measured FPS or CPU fence performance.

The policy is implemented as stand-alone C at
graphics/include/alroot_render_plan.h and graphics/src/render_plan.c,
with Xclipse ANGLE, future RADV, Mali/PowerVR and virgl cases tested.
The planner is NOT yet wired into the running launcher; success flags
must only be set after real buffer export/import/present tests.

## Stage-1 host and private Xwayland validation

graphics/patches/host/README.md documents two independent patches:
AHB-first GPU-copy import and the version-3 banner_ahb_v1 capability
event. Version 3 separates GPU-copy from direct-layer zero-copy and
retains the existing version-2 mode/HDR semantics. The client XML
is graphics/protocols/banner-ahb-v3.xml.

graphics/tools/probe-ahb-wsi.sh queries a host read-only. Current P20
reported version 2 with no copy event. Test-ahb-protocol.sh passed for
both versions against private fake Wayland servers, including a v3
GPU-copy-only host with direct layers OFF. The patched host C code
passes syntax-checking on Android API 35; the active APK is unmodified.

graphics/tools/build-xwayland-private.sh builds a separate Xwayland
24.1.14 with GLAMOR+DRI3 enabled from SHA-256-pinned upstream sources.
The Bionic SHMLBA fix is isolated in graphics/patches/xwayland/termux.
The binary BUILT on S26U, but private headless Xwayland runtime did not
yet establish DRI3/GLAMOR GPU presentation. It MUST NOT replace the
current Alroot Xwayland or global Termux Xwayland without runtime tests.

Run graphics/tools/test-stage1-inert.sh for non-invasive policy,
host-overlay, protocol v2/v3 and Termux-X11 DRI3 protection checks.

## Additional Android IPC/Vulkan proof (2026-10-29)

On S26U, graphics/tools/test-ahb-vulkan-ipc.sh allocates a GPU-only
RGBA8 AHardwareBuffer, sends its public NDK handle over AF_UNIX to a
second process, receives it there and imports it using the **real
Android system Vulkan loader**, creating VkImage, allocating imported
dedicated memory and binding it successfully. All these checks passed
without calling Android EGL, Termux Mesa, a vendor-specific KGSL ioctl,
any CPU pixel copy or the live Wayland compositor.

This proves a useful cross-process memory-sharing building block,
but NOT that Vulkan and EGL see the same finished pixels, that release
fences are correct, or that Wayland ever presented this image. Those
still need to be tested before GPU-copy is declared functional.

**Cross-vendor transport concern:** legacy banner_ahb_v1 v3 attaches
AHB *to an existing DMA-BUF wl_buffer*. Some Vulkan/ANGLE implementations
cannot portably expose their AHB's raw DMA-BUF FD/layout; private
AHardwareBuffer_getNativeHandle is not an NDK API and must not become
a requirement for Mali/PowerVR/Xclipse support. The cleaner future
solution is a separate optional AHB-created wl_buffer request that
transfers the AHB using the public AHardwareBuffer_sendHandleToUnixSocket
API, receives it using AHardwareBuffer_recvHandleFromUnixSocket, and
imports it directly as VkImage for GPU-copy without inventing a DRM
modifier. Version 3 and normal linux-dmabuf must remain fully
compatible. AHB lifetime/release-fence handling is a REQUIRED gate.

The private Xwayland startup issue was also isolated: Android blocks
setgid(getgid()) in the Xorg Popen child with SIGSYS. An optional
Android-only patch now verifies the non-root real/effective IDs match
instead of making the forbidden privilege-changing syscall. With this
change, the private -glamor off and es servers both start, accept
X11 clients, and expose GLX+Present against headless Labwc. That
headless compositor offers no GPU DMA-BUF, so GLAMOR rightly falls
back to software there. Test using
graphics/tools/test-xwayland-private.py; do not confuse this
rootful startup smoke test with GPU DRI3 acceptance.

## S26U graphics continuation: real GPU buffer export and wrapper WSI

Mesa Freedreno KGSL can export a real GLES texture as a DMA-BUF FD,
but the native GPU exported a QCOM UBWC modifier
0x0500000000000001, and direct Android Vulkan import of that FD
failed. OPAQUE_FD import had no compatible memory type, while generic
DMA_BUF import returned VK_ERROR_INVALID_EXTERNAL_HANDLE. The local
FD_MESA_DEBUG=noubwc experiment changed the layout but did not prove
interoperability. Do not globally alter KGSL, Turnip or EGL settings.

Zink running on Vulkan wrapper advertised EGL_MESA_image_dma_buf_export
and returned valid layout metadata, but the actual exported FD was -1.
This is not a working buffer export, even though GLAMOR and DRI3 can
initialize.

The optional wrapper patch at
graphics/patches/mesa/wrapper/0001-alroot-ahb-wayland-wsi-vkimage.patch
uses the wrapper's existing WSI_IMAGE_TYPE_ANDROID Vulkan swapchain AHB
allocation to create vendor-independent alroot_gpu_buffer_v1 Wayland
buffers. It is opt-in for a supported RGBA8 format and negotiated host
protocol, preserving all X11/DRI3 and ordinary Wayland DMA-BUF paths.

Because this first protocol version lacks explicit acquire/release fence
transfer, the wrapper-side adapter currently waits for the submitted
Vulkan image fence before Wayland commit: deliberate Stage-1 correctness
and a future Stage-2 performance optimization target. The patch applies
to the pinned S26U Mesa wrapper source and the generated client protocol
matches the host XML. Full Mesa recompilation and actual visible
vkcube still remain TEST-PENDING.

An isolated KGSL GLAMOR/DRI3 Xwayland implementation is retained in
graphics/patches/xwayland/kgsl and compiles on S26U, but glxinfo timed
out with "Error getting buffer" in its real Alroot-connected test.
That optional Adreno backend is not the universal GPU path and must not
be enabled on Mali, PowerVR or Xclipse. RADV-Xclipse will use the common
Vulkan Wayland transport when later tested on S24+.
