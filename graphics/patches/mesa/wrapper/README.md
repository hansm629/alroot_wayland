# Vulkan-wrapper-android: Alroot Wayland GPU-copy WSI

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
