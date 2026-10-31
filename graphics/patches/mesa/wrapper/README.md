# Vulkan-wrapper-android: Alroot Wayland GPU-copy WSI

## Current installed follow-up

[S44](../../../docs/gpu-s44-submit-overhead.md) applies patches 0006, 0007 and
0008 to the accepted S42 source. The final default uses fixed submit storage
for at most five batches/eight command buffers and skips null-fence lookups;
larger submissions retain the existing fallback. WSI profiling is opt-in and
required synchronization remains. Package identity and runtime evidence are
in the S44 report; the original 0001 candidate description below is historical.

## Original 0001 candidate

Candidate patch: 0001-alroot-ahb-wayland-wsi-vkimage.patch.

Source: S26U local ~/alroot-vk-lab/vulkan-wrapper-android, pinned baseline
b3b32abb82. This is a Wayland-only, opt-in, non-installed patch.

## Behavior

When an Alroot host advertises alroot_gpu_buffer_v1 and the selected
Vulkan ICD provides VK_ANDROID_external_memory_android_hardware_buffer,
the wrapper's existing WSI_IMAGE_TYPE_ANDROID image allocator may create
RGBA8 UNORM swapchain images as real AHardwareBuffer allocations. During
swapchain creation (not each frame), it transfers that handle over a Unix
socket using public NDK AHardwareBuffer_sendHandleToUnixSocket, creating
an Alroot wl_buffer. It retains the ordinary wl_buffer.release logic.

Other Wayland servers, devices, unsupported formats, and all X11/DRI3 WSI
remain on their established paths. The Meson integration compiles and
defines ALROOT_WSI_AHB only with with_wrapper_vk and with_platform_wayland.
This is not a generic Turnip/RADV implementation; those use independent
driver allocations but share the same Alroot Wayland protocol.

When the Alroot AHB protocol is present and the wrapper's Android AHB
capability callback is available, this adapter also advertises and prefers
VK_FORMAT_R8G8B8A8_UNORM in Wayland surface formats. The v1 host accepts
only RGBA8; otherwise applications that pick BGRA8 by default might
silently fall back to an unusable generic DMA-BUF import. This format
preference is strictly scoped to the Alroot protocol. Unsupported
formats and non-Alroot Wayland compositors keep the existing behavior.

## Synchronization and Stage 1 limitations

The v1 protocol does not transmit explicit acquire/release fences.
Consequently the adapter currently waits for the Vulkan present fence
before wl_surface.commit: a conservative per-frame CPU wait for
correctness, NOT an optimized Stage-2 implementation. Once the host
returns wl_buffer.release, that image may be reused. Separate
cross-process timeline/fence compatibility is required for Stage 2.
Removing the CPU wait before that risks stale/missing frames.

## GPU-S1.2 optional BGRA8 AHB protocol v2

The existing RGBA8 v1 Wayland path is unchanged. Host
alroot_gpu_buffer_v1 version 2 optionally accepts a real BGRA8
AHardwareBuffer (format value 5), but only when Android gralloc and
the host Vulkan VkImage importer pass a physical BGRA8 test. GPUs
without successful BGRA8 import continue advertising version 1.

The private wrapper binds up to version 2, chooses AHB-backed
VK_FORMAT_B8G8R8A8_UNORM only with v2, and sends
create_buffer_with_format. It continues using the original v1
create_buffer for RGBA8 even on a v2 host. Standard Wayland
DMA-BUF WSI and Termux-X11 DRI3 remain untouched. Do not assume
BGRA8 works because a GPU is Adreno/Mali/PowerVR/Xclipse: probe
real import support on that device.

The Vulkan image must actually use a supported RGBA8 memory layout and
be sampleable by the host's Vulkan driver. An extension string or a
successful vkCreateImage is insufficient evidence of visible output.

Run bash graphics/tools/test-wrapper-wayland-wsi.sh for clean patch
application, client protocol scanner consistency, Meson conditions, and
generated C syntax. This never alters original sources or installed
drivers.

## Private Mesa build and ICD validation

The full Mesa wrapper with this opt-in AHB WSI compiled on S26U
(Android API 35). The private library must be staged **outside**
Meson's build directory: its generated RUNPATH otherwise resolves
Mesa's dummy libnativewindow.so from $ORIGIN/../../android_stub.
That stub silently makes the system Vulkan loader enumerate ZERO GPUs
even though vkCreateInstance succeeds. Moving only the private
library to a sibling runtime folder lets it load the real Android
libnativewindow.so; it then detects Adreno 840 and Vulkan 1.4.295.

    ALROOT_BUILD_WRAPPER_FULL=1 ALROOT_VERIFY_WRAPPER=1 bash graphics/tools/build-wrapper-wayland-private.sh
    bash graphics/tools/test-wrapper-private-icd.sh

The builder stages a private ICD JSON, never a global driver. The
tests pass real GPU enumeration, VK_KHR_wayland_surface, cross-process
AHB import and a completed VkCmdClearColorImage with no CPU pixel
copy. Global Termux ICDs and Termux-X11 DRI3 stay unchanged.

These build/import tests are distinct from actual frame Present.
The first-frame Present fix and its S26U FIFO/MAILBOX results are
documented immediately below. On-device visual quality, resizing and
explicit acquire/release fence optimization remain TEST-PENDING.

## 2026-10-29: first-frame Present failure and fix

The original vkcube run selected Adreno 840 and created eight AHB-backed
wl_buffers, but never called wl_surface.commit. WSI instrumentation showed
vkQueueSubmit2 returned VK_SUCCESS, followed by a failure from the
**DRM DMA-BUF-only** sync-file transfer:

    common: DMA_BUF_SEMAPHORE_SIGNAL=-8

-8 is VK_ERROR_FEATURE_NOT_PRESENT. Generic wsi_swapchain_init had created
a DMA-BUF implicit-sync semaphore for the Android AHB image even though
that image has no DRM DMA-BUF FD. The export-to-sync-file step aborted
the present before reaching the Wayland surface.

Apply 0002-alroot-ahb-no-dmabuf-sync.patch after the first patch.
It skips DRM implicit-sync semaphore setup only for the Android WSI
image type, guarded by __TERMUX__ and ALROOT_WSI_AHB. Normal Wayland
DMA-BUF and the X11 DRI3 image type are entirely unchanged. The AHB
Wayland path still waits on the per-image Vulkan submission fence before
surface commit for Stage-1 correctness; removing that wait is NOT part
of this fix.

On S26U, the initial diagnostic patch submitted 120 FIFO frames with
120 successful WaitForFences results and 120 wl_surface.commit calls;
host logs confirmed imported AHB VkImages and opening the vkcube window.
After removing the trace code and rebuilding from both isolated patches,
120 FIFO and 120 MAILBOX frames each exited successfully, with the host
again importing the vkcube AHB images. IMMEDIATE was *not* advertised by
the current compositor, so vkcube correctly rejected that mode rather
than treating it as a GPU failure.

To reproduce against an active experimental host, run:

    bash graphics/tools/test-vkcube-stage1.sh fifo 120
    bash graphics/tools/test-vkcube-stage1.sh mailbox 120

These run the PRIVATE ICD; they never replace Termux drivers. Full visual
acceptance (correct cube colors/rotation, stutter, resize and later
explicit fences) still requires inspection on the actual Android display.

## 2026-10-30 GPU-S24 — opt-in XCB resize, Adreno/KGSL only

`0003-s24-xcb-ahb-aligned-gpu-copy.patch` is a **separate four-file patch**
on the S16 *private* Mesa wrapper/XCB AHB socket overlay. On S26U the
Qualcomm BGRA8 linear DMA-BUF EGL importer rejects AHB heights not divisible
by four (`eglCreateImageKHR` returns 0x3003). Failed XCB swapchain creation
then allowed a stale VkImage to reach vendor `vkCreateImageView`, causing a
SIGSEGV; Wrapper + Zink GLX also failed at `GLXBadCurrentWindow` on resize.

When the scoped, explicit `S24_WRAPPER_XCB_AHB_PAD_HEIGHT=1` is used,
XCB uses an ordinary logical VkImage (e.g. 900×650) and a separate Vulkan
AHB-import image with the physically aligned height (900×652).
`vkCmdCopyImage` copies only the logical extent into the matching-sized
AHB-bound destination; the physical AHB/DMA-BUF size is supplied to the
private X11 DRI3 importer. **Never bind a 900×650 Vulkan image directly
to a 900×652 Android hardware buffer:** it violates Vulkan's dedicated AHB
image dimension rules even if Qualcomm happens to accept it.

The patch also restores the real Android `image_type` after WSI metadata
initialization, prevents stale-image `vkCreateImageView` forwarding, and
returns actual AHB import failures. No CPU pixel copy is introduced, but
odd-height windows now have a *GPU-to-GPU transfer* which must be accounted
for in later performance comparisons. Aligned-height XCB paths, Wayland
WSI, other GPUs and system Termux graphics files remain unchanged.

Build and test the **private ICD only**:

```sh
cd ~/alroot_wayland
bash graphics/tools/build-s24-wrapper-xcb-resize.sh
S24_RESIZE_STRESS=1 bash graphics/tools/test-s24-wrapper-xcb-resize.sh
```

Real S26U live XFCE `vkcube` / Mesa27 Zink `glxgears`, odd-height repeat
resizes, and 12/12 logical-last-row pixel samples PASS. This does **not**
replace the system Wrapper, prove physical Android screenshot correctness,
or authorize default `awstart` promotion. See
`graphics/docs/gpu-s24-wrapper-xcb-resize.md`.

## 2026-10-30 GPU-S26 — bounded private XCB AHB pixmap import recovery

`0004-s26-xcb-dri3-transient-retry.patch` builds **after** the S24 XCB
AHB-height alignment fix, with `build-s26-wrapper-xcb-retry.sh`. Opt-in
`S26_WRAPPER_XCB_RETRY_TRANSIENT=1` (together with the existing S24 opt-in)
retries only transient `BadAlloc`/`BadMatch` returned by an Android
AHB-backed DRI3 pixmap creation, at most twice and only while the X11
window still exists. It creates a new AHB, VkImage and pixmap rather than
reusing destroyed resources. It does **not** ignore GLX errors.

The S26U user reported `glxgears` exiting with `GLXBadCurrentWindow` after
~44 seconds of manual resizing (Xwayland and XFCE **alive**). A synthetic
BadAlloc on DRI3 import #10 reproduces the exact GLX error **without**
S26 retry and remains alive **with** retry in both Mesa27 Zink and Vulkan
`vkcube`. Full forced-failure and regular-resize tests passed, but do not
assert that the original intermittent fault was definitely BadAlloc or
that manual resizing is already visually accepted.

```sh
cd ~/alroot_wayland
bash graphics/tools/build-s26-wrapper-xcb-retry.sh
S26_RESIZE_STRESS=1 bash graphics/tools/test-s26-wrapper-xcb-resize.sh
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual zink
```

It stages a **separate** private ICD and leaves the S24 runtime binary,
system Wrapper, Termux-X11, APK, and `awstart` default unchanged. See
`graphics/docs/gpu-s26-wrapper-xcb-transient-resize-recovery.md`.

## 2026-10-31 GPU-S41 — initialize physical XCB AHB padding

`0005-s41-ahb-padding-edge-copy.patch` extends S24's existing GPU image copy
with up to three one-row regions, copying the last logical row into each
physical padding row. DRI3 advertises the aligned physical height, so these
rows can become visible when an old frame reaches a growing window. The
logical image, existing barriers, submission and S26 import retry stay intact.

The separate S41 wrapper is built with
`python graphics/tools/build-s41-wrapper-padding.py`. Run the genuine-GPU
five-case boundary proof with `python graphics/tools/test-s41-ahb-padding.py`
and the S26 comparison with its `--s26` option. The visual entry point is
`python graphics/tools/run-s41-vkcube-visual.py` on an active S39 desktop.
The existing S40 glxgears route keeps its S26 wrapper. See
[the S41 measurements and provenance](../../../docs/gpu-s41-ahb-padding.md).
