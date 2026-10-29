#ifndef ALROOT_AHB_WAYLAND_PRESENTER_H
#define ALROOT_AHB_WAYLAND_PRESENTER_H

#include <stdint.h>
#include <wayland-client.h>
#include <android/hardware_buffer.h>

/*
 * GPU-S1.2: vendor-neutral Wayland AHardwareBuffer presentation provider.
 *
 * The producer renders into an AHB and MUST finish GPU writes (or wait for
 * its acquire fence) BEFORE calling submit. The compositor imports this
 * existing AHB as a VkImage; there is no CPU pixel upload or DRM FD.
 * Full cross-process explicit fence integration is a future protocol.
 *
 * RGBA8 works with Alroot protocol v1, while BGRA8 requires negotiated
 * protocol v2 and a host-confirmed Vulkan import probe. It never guesses
 * support by GPU name or Android version.
 *
 * This API is useful to EGL native-buffer producers, ANGLE Vulkan-backed
 * GLES, MobileGL/virgl's AHB slots, and driver WSI adapters. It never
 * modifies Termux X11/DRI3, global libraries or Vulkan ICDs.
 *
 * Call from the same Wayland client-thread that owns the surface.
 */
struct alroot_ahb_wl_presenter;

struct alroot_ahb_wl_stats {
    uint64_t registered;
    uint64_t submitted;
    uint64_t released;
    uint64_t reused;
    uint64_t evicted;
    uint64_t busy_rejects;
    uint32_t live_slots;
};

/* Returns NULL when alroot_gpu_buffer_v1 is not advertised, with no
 * unsafe pixel-copy fallback. display/surface remain caller-owned. */
struct alroot_ahb_wl_presenter *
alroot_ahb_wl_presenter_create(struct wl_display *display,
                              struct wl_surface *surface);

/* Returns 0 if the wl_surface commit was queued. -EAGAIN means the
 * buffer is still in use by the compositor; do not render into it.
 * Reuse is allowed only after wl_buffer.release. */
int alroot_ahb_wl_presenter_submit(struct alroot_ahb_wl_presenter *presenter,
                                   AHardwareBuffer *ahb,
                                   uint32_t width, uint32_t height);

/* Query before a producer writes a GPU buffer again. Returns 1 if no
 * unreleased client-side wl_buffer references this AHB, 0 while busy,
 * or negative errno for invalid arguments. */
int alroot_ahb_wl_presenter_is_available(struct alroot_ahb_wl_presenter *presenter,
                                         AHardwareBuffer *ahb);

/* Dispatches release events on an isolated Wayland event queue.
 * timeout_ms=0 never blocks. */
int alroot_ahb_wl_presenter_dispatch(struct alroot_ahb_wl_presenter *presenter,
                                     int timeout_ms);

struct alroot_ahb_wl_stats
alroot_ahb_wl_presenter_stats(const struct alroot_ahb_wl_presenter *presenter);
void alroot_ahb_wl_presenter_destroy(struct alroot_ahb_wl_presenter *presenter);

#endif
