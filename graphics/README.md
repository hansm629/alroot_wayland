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
matches the host XML. The full private wrapper ICD now builds and
detects Adreno 840 / Vulkan 1.4.295, once staged outside Meson's dummy
libnativewindow.so RUNPATH. Private ICD AHB import and GPU clear pass.
The first-frame WSI failure was traced to Mesa's common DRM implicit
sync-file semaphore erroneously being initialized for an AHB-only image
(which has no DMA-BUF FD). vkQueuePresentKHR then failed with
VK_ERROR_FEATURE_NOT_PRESENT before wl_surface.commit. The isolated
wrapper patch 0002-alroot-ahb-no-dmabuf-sync.patch disables that
DMA-BUF-only synchronization setup exclusively for the opt-in Android
AHB image type, preserving X11 DRI3 and standard DRM Wayland.

After rebuilding the private ICD, S26U completed 120 FIFO and 120
MAILBOX vkcube presents, returned 0, and host logs confirmed opening
the vkcube window and importing AHB images through Vulkan VkImage
GPU-copy. A separate private Vulkan wrapper XCB vkcube (:0) regression
also exited successfully, with DRI3 and installed drivers unchanged.
IMMEDIATE was not advertised by the current compositor and is not
claimed as supported. The user still needs to verify the on-device
rotation/visual correctness; Xwayland GLAMOR, other ICDs and explicit
cross-process fences remain pending.

The reusable visible-frame acceptance command is
graphics/tools/test-vkcube-stage1.sh fifo 120 (or mailbox 120).
It never installs or replaces a system driver or APK and checks both
client exit and current host import logs.

An isolated KGSL GLAMOR/DRI3 Xwayland implementation is retained in
graphics/patches/xwayland/kgsl and compiles on S26U, but glxinfo timed
out with "Error getting buffer" in its real Alroot-connected test.
That optional Adreno backend is not the universal GPU path and must not
be enabled on Mali, PowerVR or Xclipse. RADV-Xclipse will use the common
Vulkan Wayland transport when later tested on S24+.

## GPU-S1.1 — AHB lifetime, XDG geometry and resize

The S26U GPU-copy baseline completed real FIFO and MAILBOX Vulkan
vkcube frames using an AHB-only wl_buffer. The next host-side
stabilization changes remain independent of GPU vendor or desktop:

- Normal XDG windowed surfaces receive configure(0,0), allowing the
  client to choose its own initial 640x360 or other size. Fullscreen,
  maximized and compositor-designated desktop surfaces alone receive
  the output size, such as 1920x1080. Unsetting those states returns to
  an unconstrained size. Output rotation and resize reconfigure
  constrained surfaces without forcing unrelated windowed buffers.
- AHB-backed wl_buffers now have final-reference accounting: live,
  peak, total created/freed and estimated RGBA8 byte counts, reported
  periodically with the existing host stats. These bytes are estimates
  and exclude gralloc alignment/metadata; they are NOT true GPU memory
  consumption. This adds observability, not a new release policy.
- Real-device EGL and Vulkan client tests exercise 640x360 -> 320x240
  -> 800x450, require exactly-once wl_buffer.release on buffer
  replacement/detach, reuse released AHBs, and finally destroy a
  wl_buffer while its surface still retains the image.

Against the S26U pre-S1.1 GPU host, EGL passed 32 reuse cycles (35
expected releases, zero unexpected) and Vulkan wrapper passed 20 cycles
(23 releases, zero unexpected). Both also passed destroying an attached
buffer. This verifies the original release behavior before changing
timer pacing or removing correctness fences.

The offline geometry unit test has 12 cases, including Xwayland desktop
root and rotated output, and matches the compiled host helper. An
independent live xdg_toplevel test confirms the old S26U host always
configured 1920x1080 for a normal window. After explicitly installing
the S1.1 host APK it must return configure(0,0) for windowed surfaces
while keeping fullscreen/maximized at the output resolution.

    bash graphics/tools/test-s11-geometry.sh
    bash graphics/tools/test-s11-acceptance.sh

The second command requires the signed S1.1 test APK, and exercises
XDG configure, EGL/Vulkan AHB buffer lifecycle, FIFO/MAILBOX 360-frame
present, host AHB reference telemetry and protected Termux-X11 DRI3.
It does not install an APK or restart XFCE. Preserve P20 Korean IME,
caret, pointer, DPI, battery and PiP. Fences and zero-copy are separate
future stages and MUST NOT be removed as a supposed S1.1 fix.

## GPU-S1.2: real Wayland EGL/OpenGL ES and Mesa Kopper

The first new client at graphics/tests/wayland-egl-swaps.c creates a
real EGL Wayland window, renders time-varying GLES pixels and calls
eglSwapBuffers. With S26U's current Termux Mesa Zink and Vulkan wrapper,
it reports GPU-backed Zink GLES but sends Wayland wl_shm buffers:
the host does NOT receive GPU-copy-capable buffers. With Freedreno KGSL,
it exports DMA-BUF buffers but the Android Vulkan host rejects them
with INVALID_EXTERNAL_HANDLE. Neither result is a GPU-copy PASS.

The isolated candidate at graphics/patches/mesa/egl allows a Vulkan-
backed Zink Kopper Wayland path without requiring inaccessible DRM
render nodes, and without forcing a CPU Vulkan device. The actual
Mesa EGL/Gallium private compile and running GPU display validation
are separate gates. For ANGLE-backed Xclipse, PowerVR and Mali, a
native AHB EGL producer can use the same host protocol without any
KGSL-specific assumptions. Turnip/RADV ICD support requires
independent Vulkan WSI memory allocation tests, and the installed
Termux-X11 DRI3 must stay unchanged.

### S1.2 implementation continuation: Android AHB protocol v2

S26U proved the generic Android GLES -> AHB -> host Vulkan VkImage
GPU-copy path with 90 sequential frames, 3 AHB slots, 90 wl_buffer
releases and zero premature buffer reuse. This direct GLES producer
uses the public NDK AHB transport, not any Qualcomm-specific KGSL
allocation. On future Xclipse devices, validate ANGLE's actual AHB
render target rather than inferring export from EGL extension names.

Mesa Zink/Kopper now builds privately without a DRM render node using
graphics/patches/mesa/egl/0001. Its ABI uses a Bionic TLS export-map
fix (0002) and conditional optional driconf lookup (0003).
The Kopper EGLConfig normally chooses BGRA8, NOT RGBA8; synthesizing
an unsupported RGBA8 config caused a software wl_shm fallback.

To preserve v1 RGBA8 while allowing Kopper's normal BGRA8 swapchain,
alroot_gpu_buffer_v1 protocol v2 adds a separate format-aware request
for BGRA8 AHardwareBuffer images. The host advertises v2 ONLY after
its Android gralloc and Vulkan VkImage importer physically accept a
BGRA8 buffer. S26U passed allocation, cross-process import, Vulkan
GPU clear and native GLES framebuffer binding with BGRA8. GPUs
without these capabilities retain protocol v1 and cannot be forced.
The private wrapper WSI negotiates v2 and uses normal B8G8R8A8_UNORM
AHB images, leaving normal Wayland DMA-BUF and Termux-X11 DRI3 intact.

Reusable probes:

    bash graphics/tools/test-s12-ahb-formats.sh
    bash graphics/tools/test-s12-android-gles-ahb.sh 90 rgba8

After explicitly installing a host APK that advertises v2, first
rebuild the **private** Mesa EGL/Gallium library from the current patch
set, then test:

    bash graphics/tools/build-s12-zink-egl-private.sh
    bash graphics/tools/test-s12-acceptance.sh

The full acceptance verifies direct EGL RGBA8, optional Android EGL
BGRA8, native Mesa Zink/Kopper EGL, Vulkan vkcube and Termux-X11 DRI3.
The older S1.1 host advertises version 1 and MUST NOT be replaced
automatically. The new S1.2 host advertises v2 on compatible hardware.
The test refuses a stale private ELF staging directory, an outdated
patch hash or a host with only v1. The optional six-frame wire check
inspects actual Wayland AHB buffer creation and rejects wl_shm fallback.
The KGSL direct DRM/DMA-BUF import and MobileGL/virgl Wayland
presenters remain separate targets; neither is counted as completed.

### S1.2 S26U resumed acceptance (2026-10-29)

The first v2-host test had 90 successful Zink eglSwapBuffers but no
AHB VkImage import. A Wayland protocol trace showed two wl_shm buffers
and no alroot_gpu_buffer_v1 bind. The private Mesa build/patch manifest
was not synchronized with the current source patches. After rebuilding
both private EGL and Gallium from all three pinned patches and restaging
the shared objects (no global library replacement), the same test
presented BGRA8 AHB images through Vulkan Kopper WSI successfully.

The uninstrumented private runtime passed 90/90 EGL swaps with host
AHB -> VkImage import. A separate six-frame WAYLAND_DEBUG=client test
showed four alroot_gpu_buffer_v1.create_buffer_with_format requests,
zero wl_shm_pool.create_buffer calls, and host GPU-copy import.
Android GLES RGBA8 passed 90/90, optional BGRA8 passed 45/45, the
original Vulkan vkcube path passed another 120 FIFO frames, and
Termux-X11 :0 DRI3 plus protected renderer file hashes remained intact.
The host's last AHB lifetime tick reported created=80, freed=80,
live=0. This is host transport acceptance, not proof of real display
pixel correctness, zero-copy, async fence removal, Xwayland GLAMOR,
non-Adreno drivers or other desktop compositors. User visual inspection
is still required. The installed S1.2 APK need not be updated again.

## GPU-S1.3-A: experimental Freedreno KGSL native AHB producer

On S26U (Adreno 840), regular Mesa KGSL Wayland EGL renders through the
freedreno driver but submits DMA-BUF buffers that the Android host Vulkan
renderer cannot import. Neither the vendor Android EGL nor Mesa KGSL EGL
advertises EGL_EXT_image_dma_buf_import, and KGSL EGL does not advertise
EGL_ANDROID_image_native_buffer. Do **not** treat a successful EGL swap or
GL renderer string as proof of AHB-backed Wayland presentation.

The experimental S1.3-A path instead imports a real Android-allocated AHB
GPU image FD into a KGSL GL_EXT_memory_object_fd resource. It renders
with freedreno directly into that external memory, calls glFinish for
Stage-1 correctness, and queues the **same AHB** through the existing
alroot_gpu_buffer_v1 Wayland protocol. The host imports it as VkImage
and GPU-composites the image to the Android output swapchain. The
producer contains no glReadPixels, CPU pixel upload or vendor GLES
rendering. It does not use Vulkan for the KGSL application renderer.

The Android VNDK AHardwareBuffer_getNativeHandle entrypoint is **not** a
public NDK API. It is resolved only when available; the KGSL-specific
adapter inspects the stable native_handle structure, selects a uniquely
plausible pixel FD by size rather than assuming fd[0], and fails closed
on absent/ambiguous layouts. Android vendor EGL readback of **five tiny
pixel samples in a standalone diagnostic only** proves the KGSL writes
were actually visible in the same AHB on this device. This extra
readback is not part of the per-frame producer. Other KGSL devices,
other Adreno models and different gralloc layouts require their own
physical acceptance; the adapter is not a universal NDK API.

**Important stride and height correction:** the S26U gralloc allocates
480x270 RGBA8 with stride=768, whereas importing that allocation into
KGSL GL with a 480x270 texture produced incorrect pixels despite
successful GL imports and 360 successful Wayland commits. This is **not
accepted**. The validated variant allocates an AHB of **480x320**
(stride=768), imports it into a KGSL GL texture of **768x270**, and uses
wp_viewporter to crop the displayed image to the intended 480x270. All
five hardware cross-driver samples agree, including the bottom corners.
Buffers whose external-memory layout cannot be validated must not be
submitted as if they were correct.

Run a standalone pixel probe and the guarded on-screen test:

    cd ~/alroot_wayland
    ALROOT_S13_RESOLUTION=480x320 \
      ALROOT_S13_TEXTURE_STRIDE_WIDTH=1 \
      ALROOT_S13_TEXTURE_HEIGHT=270 \
      ALROOT_S13_SAMPLE_HEIGHT=270 \
      bash graphics/tools/test-s13-kgsl-ahb-capabilities.sh
    bash graphics/tools/test-s13-kgsl-ahb-window.sh 360
    bash graphics/tools/test-s13-acceptance.sh

The original direct producer and native Zink path stay private and
separate. No global Termux Mesa, driver, Termux-X11 DRI3 or helper APK
was replaced. Stage S1.3-A is **only** a proof and reusable producer for
KGSL -> AHB transport. It does *not* implement ordinary KGSL
`eglSwapBuffers()` -> AHB for unmodified EGL Wayland applications, nor
Xwayland DRI3/GLAMOR or desktop GPU acceleration. An optional private
Mesa EGL/Wayland integration is a separate next step. The explicit
acquire/release fence replacement and final output zero-copy are later
stages. Color/crop correctness still requires a physical user visual
inspection before declaring an end-to-end on-screen PASS.

### S1.3-B experimental EGL Wayland `eglSwapBuffers` AHB adapter

The S1.3-A demo proves KGSL can directly write an Android AHB, but a
normal Mesa KGSL Wayland EGL app still uses linux-dmabuf, which the
host Android Vulkan driver cannot import. For an **opt-in, test-only**
EGL integration, S1.3-B uses a private `LD_PRELOAD` library to intercept
Wayland `wl_egl_window_create`, `eglCreateWindowSurface`, and
`eglSwapBuffers` for the single client process. It retains the normal
Freedreno KGSL renderer and copies the *GPU* default framebuffer to a
KGSL GL external-memory AHB image via `glBlitFramebuffer`, then commits
that AHB to the Alroot Wayland host. This explicitly adds a **GPU blit**;
it does not claim zero-copy. It performs NO CPU pixel readback/upload on
the frame path and never invokes a failing Linux DMA-BUF Wayland commit.

S26U 480x270 EGL client -> AHB 480x320 (stride 768) -> viewport 480x270
passes 360/360 EGL swaps and 360/360 buffer releases. A separate Wayland
wire trace shows three AHB buffers, zero wl_shm and zero linux-dmabuf
buffers, plus actual host AHB -> VkImage import. Without the per-process
opt-in `ALROOT_S13_KGSL_EGL_AHB=1`, the same preloaded library leaves
normal EGL calls alone, and the old KGSL DMA-BUF path remains unchanged.

    cd ~/alroot_wayland
    bash graphics/tools/test-s13-kgsl-egl-shim.sh 90
    bash graphics/tools/test-s13-kgsl-egl-shim.sh 360

The script first checks all five cross-driver KGSL↔Android EGL pixels,
then builds and preloads the private adapter only in one isolated test
process, and confirms release and AHB-only Wayland wire traffic. No
helper APK update or system library installation is required.

**Not a general-purpose KGSL EGL implementation yet:** this experiment
intentionally only accepts the physically tested S26U 480x270 buffer
layout, includes conservative Stage-1 CPU completion waits, and does
not cover arbitrary resolutions, resize, multiple independent contexts,
OpenGL buffer-age/preservation/swap interval, full EGL context lifetime,
other Android gralloc layouts, EGL X11/GLX, desktop sessions or
Xwayland GLAMOR. It is NOT installed for general applications. Standard
Mesa EGL/Gallium integration and correctness under these transitions
must be developed and tested separately. Visual confirmation of the
animated frame colors/cropping by a person remains necessary.

## GPU-S1.4 — pinned lfdevs PR #25 Mesa 27 revalidation

See [Mesa 27 PR #25 revalidation](docs/mesa27-pr25-revalidation.md) for the
fixed source/patch hashes, private build recipe, Turnip/Zink/KGSL GPU test
matrix and explicit Xwayland DRI3/GLAMOR limitations. **Never replace the
known-good Termux 26.3 packages or helper APK during this staged validation.**

## GPU-S1.5 — Turnip 27 Wayland AHB WSI (private, experimental)

See [Turnip 27 AHB WSI](docs/gpu-s15-turnip-wayland-ahb-wsi.md) for the
separate opt-in Vulkan ICD build, real cross-driver AHB memory proof,
GPU-copy architecture and host-confirmed acceptance requirements. This stage
is not a system Mesa update, not an Xwayland DRI3 patch and not zero-copy.


## GPU-S1.6 — isolated Xwayland GLAMOR `VkBuffer` GPU-copy integration

The Adreno/KGSL 24.1.14 private GLAMOR Xwayland can already export LINEAR
EGL textures to Wayland DMA-BUF. Current Android Vulkan drivers may refuse
these FDs as external VkImages, but a separate S16 on-device GPU test proves
DMA-BUF `VkBuffer` import and `vkCmdCopyBufferToImage` GPU-copy with actual
KGSL-exported GL texture pixels. The new `feature/gpu-s16-xwayland-glamor-vkbuffer`
branch stages an import fallback in the Alroot Android host; it is **not
installed** or proven on-screen until the signed APK is rebuilt and tested.

Run the isolated per-device proof with:

`ALROOT_S16_TEST_RESOLUTIONS=full bash graphics/tools/test-s16-xwayland-gpu-copy.sh`

See `graphics/docs/gpu-s16-xwayland-vkbuffer.md` for exact limits, the
1024x768 unpadded allocation size blocker, private Xwayland GLX timeout
and pending visible-present/fence requirements. Do not infer that a glxinfo
GPU renderer string is evidence of a successful on-screen GPU frame.

The follow-on isolated Debian glibc Xwayland GLX GPU test is
`python3 graphics/tools/test-s16-xwayland-glibc-glx.py` (S26U, KGSL only).
It confirms direct Debian glibc `glxinfo` and timed `glxgears` run on Adreno
840 through Alroot, without replacing the current APK/XFCE/Termux-X11.
**CAUTION**: its local private X socket uses test-only `-ac` (mode 0600,
TCP disabled, immediate cleanup); production Xauthority/visible GPU
presentation are still not verified.


Rootful S1.6 Xwayland startup (no `-listenfd`) now also uses the two
Android-only private patches for `TMPDIR` server lock files and atomic
`renameat2(RENAME_NOREPLACE)` fallback when app-data hardlinks are denied.
`python3 graphics/tools/test-s16-xwayland-rootful-lock.py` proves a real
private GLAMOR/DRI3 server and a fresh Android VkBuffer GPU-copy host log
without stopping the active XFCE or replacing the installed APK. Full
user-visible desktop pixels/input and long-running frame pacing remain
unverified. See `graphics/patches/xwayland/README.md`.


## GPU-S1.6 live XFCE regression diagnostics (candidate fixes)

See `graphics/docs/gpu-s16-live-regression-fixes.md`. On S26U the default
GLX loader can select an unusable backend even while private KGSL GLAMOR
works; live test clients use explicit child-scoped KGSL or Turnip ICD via
`graphics/tools/run-s16-x11-client.sh`, not system-wide driver overrides.
XCB vkcube produces mapped, readable X11 pixels, but Android visible pixels
remain unverified. The experimental host attaches the EGL DMA-BUF producer's
sync_file to the Vulkan buffer-copy submission and the Android input view
handles hardware mouse positions before sending buttons. Neither real
on-screen shimmer reduction nor hardware click positioning is a PASS until
the new Android APK is explicitly installed and visually tested.


The follow-up KGSL-only correctness update for the fragmented 2D UI and
mouse drift is `graphics/tools/test-s16-kgsl-full-frame.sh`: full GPU
pixmap preservation on every reused GLAMOR surface, complete Wayland
damage, a deterministic repeat-build/Xorg-lock check, and a direct live
host GPU-copy probe. Java physical/touchpad cursor now sends absolute
Xwayland scene coordinates (input type 12), bypassing a possibly stale
Vulkan output transform. Requires the updated Android host APK for the
pointer change; KGSL Xwayland alone is rebuilt locally. Real visual UX
regression pass still requires the user to inspect XFCE title buttons,
text and moving/scrolling windows.

**GPU-S1.6.3: 1920×1080 desktop right-half text corruption root-caused.**
The private KGSL GLAMOR uses GLES fragment `mediump` 10-bit texcoords.
Reproduced 0 wrong left / **28,672 wrong right** XRender pixels; the private
KGSL-only `highp` precision patch gives **0/0** wrong with identical code
otherwise. `bash graphics/tools/test-s16-kgsl-highp.sh` reproduces the
Adreno GPU and real Xwayland XRender A/B and confirms Android host DMA-BUF
VkBuffer GPU-copy. See `graphics/docs/gpu-s16-glamor-right-half-highp.md`.
An on-device XFCE restart/visual comparison remains the last acceptance gate.

**GPU-S1.7 three Zink backends:** the private KGSL Xwayland now supports
Termux Vulkan-wrapper DRI3's AHB socket modifier 1255 and optional,
server-scoped 96-byte Bionic / 4-byte glibc Xshmfence compatibility.
`S17_MATRIX_MODE=smoke bash graphics/tools/test-s17-zink-matrix.sh` passed
windowed GLmark with all three: Termux wrapper+Zink, Termux Turnip+Zink,
Debian Turnip+Zink. Existing live `:92` must be restarted with
`ALROOT_XWAYLAND_DUAL_ABI_FENCE=1` **after saving desktop work**; the
changed executable cannot unblock a previously deadlocked process. No
universal awstart default promotion yet; see
`graphics/docs/gpu-s17-zink-three-backend.md`.

**GPU-S1.7 startup-lock black-screen recovery (S26U):** after a prior
unresponsive `:92` Xwayland, background children inherited the
`$STATE/start.lock` descriptor and blocked every subsequent `awstart`
startup. This is a **launcher FD lifetime bug**, not Vulkan GPU-copy.
The launcher now closes fd 8 for every long-running child and releases its
startup flock explicitly; `awstart --restart` refuses to terminate its
own calling XFCE terminal. The project-only deadlocked orphan processes
were verified and cleaned, and the recovered S26U `:92` reached XFCE READY
with fresh Android GPU-copy and non-black frame logs. Repeatable read-only
check: `bash graphics/tools/test-s17-start-lock-lifetime.sh`. See
`graphics/docs/gpu-s17-black-screen-start-lock-recovery.md`.

**GPU-S20 pointer-drift correction:** recorded S26U XFCE right-click drift;
actual Wayland→Xwayland absolute point tests were exact, but the installed
APK predated the S16/P19 Java guest-scene pointer change. The S20 candidate
adds one view→guest mapping shared by cursor and atomic click, uses a
single JNI pipe record for motion+button, removes redundant relative
pointer dispatch on rootful absolute moves, and keeps captured game
movement from displacing the Android overlay cursor. Independent Java
50k-gesture and native source tests are in
`bash graphics/tools/test-s20-pointer-atomic.sh`; physical XFCE cursor
alignment requires installing the new signed APK and checking the recorded
terminal right-click/menu reproduction. See
`graphics/docs/gpu-s20-pointer-drift-atomic.md`.

**GPU-S23 Mesa27 Xwayland acceptance (S26U, 2026-10-30):** all three
Zink stacks passed the isolated 9-case GLX/GLmark/Vulkan XCB matrix using
the pinned Mesa27 sources. On the **actual** live XFCE Xwayland `:94`, all
three also created mapped GLmark2 and Vulkan cube windows with nonblank,
varied XGetImage pixels; no existing session restart was needed. Termux
Turnip and Debian glibc Turnip passed three consecutive `vkcube` and
`glxgears` window resizes. **The Vulkan-wrapper route has a release-blocking
resize regression**: `vkcube` SIGSEGV in the Qualcomm `vkCreateImageView`
path, and Zink/GLX `glxgears` reports `GLXBadCurrentWindow`. The latter
also reproduces with system Mesa26.3, so do not blame Mesa27 unilaterally.
Wrapper+Zink still passes the nonresizing windowed GLmark smoke. S20–S22
pointer/cursor policy regressions and protected Termux graphics hashes PASS.
**Do not promote KGSL GPU Xwayland to the awstart default** until those
resizing errors and actual Android UI acceptance are resolved. Read
`graphics/docs/gpu-s23-mesa27-xwayland-validation.md` and use the scoped
test scripts `graphics/tools/test-s23-mesa27-zink-matrix.sh` and
`graphics/tools/test-s23-live-xfce-windowed.py`; all live tests touch only
their own temporary demo windows. Mesa27 and Alroot runtime trial builds
remain private; global drivers, Termux-X11 :0, and the existing APK were
not replaced.

**GPU-S24 Wrapper XCB resizing:** S26U Adreno 840 KGSL EGL requires a
BGRA8 AHB with physical height divisible by 4; previously, a failed
900×650 DRI3 import cascaded into stale VkImage/vkCreateImageView SIGSEGV
and GLXBadCurrentWindow. The S24 *private* wrapper now uses a real 900×650
render VkImage and a **separately bound 900×652 AHB VkImage**, copying only
logical pixels with `vkCmdCopyImage`. Vulkan memory binding dimensions thus
match the AHB. Live XFCE Vulkan and Zink odd-height resize stress,
12/12 last-logical-row samples per resize, driver integrity and process
liveness passed. Keep opt-in `S24_WRAPPER_XCB_AHB_PAD_HEIGHT=1` scoped to
the private ICD and the XCB WSI. See
`graphics/docs/gpu-s24-wrapper-xcb-resize.md` and run
`S24_RESIZE_STRESS=1 bash graphics/tools/test-s24-wrapper-xcb-resize.sh`.
**Do not promote awstart yet** without the user's physical display visual
acceptance and device-vendor capability gates.

**GPU-S25 host pacing / Zink session stability candidate (2026-10-30):**
During real user visual testing of the S24 Wrapper, `vkcube`/Zink
`glxgears` rotation was intermittently uneven, and the Xwayland/XFCE
process group went DOWN while the Android Wayland host remained READY.
Android host logs before the Xwayland disconnect showed **389.06/988.73 ms
average/peak `wl_buffer.release` delay** while 1303 game GPU frames arrived
and 1132 scenes were displayed per ten seconds on a 120 Hz panel. Host
source inspection found an automatically paced release queue with a deadline
that grew with pending buffers and per-import 1080p DMA-BUF GPU-copy log
spam. A candidate fix bounds **automatic** release deadlines to one physical
display interval, retains explicit FPS-limit and GPU-fence behavior, and
throttles duplicate GPU-copy logging. Native C, modeled 130/120 and 150/60
FPS release tests, and S20–S22 cursor regression tests PASS. **The fix is
not yet validated on the user's physical Android screen**, and the exact
Xwayland death mechanism remains uncertain. Full diagnostics and A/B gates:
`graphics/docs/gpu-s25-host-release-pacing.md`.

**S24 visual-test note:** The Termux shell defaults to `DISPLAY=:0` but
Alroot Wayland helper XFCE uses a private display such as `:94`. Automated
`test-s24-wrapper-xcb-resize.sh` cases intentionally close their own demo
windows and now print live progress instead of hiding it in logs. For a
*persistent* window, run
`bash graphics/tools/test-s24-wrapper-xcb-resize.sh --visual vulkan`
or `--visual zink`, then bring the **Alroot Wayland Android helper APK**
foreground. The test window will not appear in Termux-X11 `:0`.
