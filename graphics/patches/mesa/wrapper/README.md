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
drivers. Full Mesa wrapper build, vkcube visible-pixel acceptance,
FIFO/MAILBOX, resizing and frame synchronization are still pending.
