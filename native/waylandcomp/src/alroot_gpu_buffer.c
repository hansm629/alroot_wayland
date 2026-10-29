#define _GNU_SOURCE
#include <android/hardware_buffer.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server.h>
#include "alroot-gpu-buffer-v1-server-protocol.h"
#include "compositor_internal.h"
#include "vk_present.h"

/* Small, always-available stability counters, not a memory allocator.
 * Estimated RGBA8 bytes exclude gralloc padding, alignment and metadata. */
static struct {
    uint64_t created, freed, live_bytes, peak_bytes;
    uint64_t last_report_created, last_report_freed;
    unsigned live, peak;
} g_ahb_lifetime;

static void gpu_buffer_note_allocated(const struct dmabuf_buffer *b) {
    const uint64_t bytes = (uint64_t)b->width * (uint64_t)b->height * 4u;
    g_ahb_lifetime.created++;
    g_ahb_lifetime.live++;
    g_ahb_lifetime.live_bytes += bytes;
    if (g_ahb_lifetime.peak < g_ahb_lifetime.live)
        g_ahb_lifetime.peak = g_ahb_lifetime.live;
    if (g_ahb_lifetime.peak_bytes < g_ahb_lifetime.live_bytes)
        g_ahb_lifetime.peak_bytes = g_ahb_lifetime.live_bytes;
}

void alroot_gpu_buffer_note_freed(const struct dmabuf_buffer *b) {
    if (!b || !b->gpu_ahb) return;
    const uint64_t bytes = (uint64_t)b->width * (uint64_t)b->height * 4u;
    if (!g_ahb_lifetime.live || g_ahb_lifetime.live_bytes < bytes) {
        alroot_log("error", "S1.1 AHB buffer accounting underflow (final reference mismatch)");
        return;
    }
    g_ahb_lifetime.live--;
    g_ahb_lifetime.live_bytes -= bytes;
    g_ahb_lifetime.freed++;
}

void alroot_gpu_buffer_stats_tick(void) {
    if (!g_ahb_lifetime.created) return;
    if (!g_ahb_lifetime.live &&
        g_ahb_lifetime.last_report_created == g_ahb_lifetime.created &&
        g_ahb_lifetime.last_report_freed == g_ahb_lifetime.freed)
        return;
    alroot_log("gpu-lifetime",
               "S1.1 AHB wl_buffers live=%u peak=%u created=%llu freed=%llu "
               "estimated_live=%llu bytes estimated_peak=%llu bytes",
               g_ahb_lifetime.live,g_ahb_lifetime.peak,
               (unsigned long long)g_ahb_lifetime.created,
               (unsigned long long)g_ahb_lifetime.freed,
               (unsigned long long)g_ahb_lifetime.live_bytes,
               (unsigned long long)g_ahb_lifetime.peak_bytes);
    g_ahb_lifetime.last_report_created = g_ahb_lifetime.created;
    g_ahb_lifetime.last_report_freed = g_ahb_lifetime.freed;
}

/* This wl_buffer contains a real Android AHardwareBuffer, *not* a DRM
 * dma-buf emulation. It uses the ordinary compositor surface lifecycle,
 * imports with vkp_image_from_ahb(), and is composited into the host Vulkan
 * swapchain. It does not depend on KGSL, GBM or the desktop session type.
 *
 * Only public NDK AHardwareBuffer_{send,recv}HandleTo/FromUnixSocket APIs.
 * No private native_handle_t, CPU pixel mapping or vendor modifier path.
 */

static void gpu_buffer_destroy_request(struct wl_client *client,
                                       struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}

const struct wl_buffer_interface alroot_gpu_buffer_impl = {
    .destroy = gpu_buffer_destroy_request
};

static void gpu_buffer_resource_destroy(struct wl_resource *resource) {
    struct dmabuf_buffer *buffer = wl_resource_get_user_data(resource);
    dmabuf_buffer_unref(buffer);
}

static void gpu_manager_destroy(struct wl_client *client,
                                struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void gpu_manager_create(struct wl_client *client,
                               struct wl_resource *manager,
                               uint32_t id, int socket,
                               uint32_t width, uint32_t height) {
    AHardwareBuffer *ahb = NULL;
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        close(socket);
        wl_resource_post_error(manager, ALROOT_GPU_BUFFER_V1_ERROR_INVALID_BUFFER,
                               "AHB dimensions outside supported GPU-copy range");
        return;
    }

    /* The client sends the full AHB handle before marshalling this request.
     * This is buffer-creation-time IPC, never an additional per-frame wait.
     * Keep a bounded poll for malicious or incomplete sockets. */
    struct pollfd pf = { .fd = socket, .events = POLLIN };
    int ready;
    do { ready = poll(&pf, 1, 100); } while (ready < 0 && errno == EINTR);
    int rc = ready > 0 && (pf.revents & POLLIN) ?
             AHardwareBuffer_recvHandleFromUnixSocket(socket, &ahb) : -1;
    close(socket);
    if (rc != 0 || !ahb) {
        wl_resource_post_error(manager, ALROOT_GPU_BUFFER_V1_ERROR_INVALID_BUFFER,
                               "AHardwareBuffer could not be received from socket");
        return;
    }
    AHardwareBuffer_Desc desc = {0};
    AHardwareBuffer_describe(ahb, &desc);
    if (desc.width != width || desc.height != height ||
        desc.layers != 1 || desc.width < 1 || desc.height < 1) {
        AHardwareBuffer_release(ahb);
        wl_resource_post_error(manager, ALROOT_GPU_BUFFER_V1_ERROR_INVALID_BUFFER,
                               "AHB description does not match Wayland dimensions");
        return;
    }
    if (desc.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM ||
        !(desc.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE)) {
        AHardwareBuffer_release(ahb);
        wl_resource_post_error(manager, ALROOT_GPU_BUFFER_V1_ERROR_UNSUPPORTED_FORMAT,
                               "GPU-copy v1 requires sampled RGBA8 AHardwareBuffer");
        return;
    }

    struct dmabuf_buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) {
        AHardwareBuffer_release(ahb);
        wl_client_post_no_memory(client);
        return;
    }
    struct wl_resource *resource = wl_resource_create(client, &wl_buffer_interface,
                                                       1, id);
    if (!resource) {
        free(buffer);
        AHardwareBuffer_release(ahb);
        wl_client_post_no_memory(client);
        return;
    }
    buffer->refs = 1;
    buffer->width = (int32_t)desc.width;
    buffer->height = (int32_t)desc.height;
    /* ABGR8888 describes RGBA bytes in little-endian DRM fourcc notation.
     * Real AHB import uses Android description; no DRM layout is assumed. */
    buffer->format = DRM_ABGR8888;
    buffer->modifier = MOD_INVALID;
    buffer->gpu_ahb = ahb;
    buffer->n_planes = 0;
    for (int i = 0; i < MAX_PLANES; ++i) buffer->fd[i] = -1;
    gpu_buffer_note_allocated(buffer);
    wl_resource_set_implementation(resource, &alroot_gpu_buffer_impl, buffer,
                                   gpu_buffer_resource_destroy);
    struct client_info *info = client_info_of(client);
    if (info) info->dmabuf_buffers++;
    alroot_log("gpu-copy", "new AHB wl_buffer %ux%u RGBA8, Vulkan GPU-copy transport (no DRM fd)",
               width, height);
}

static const struct alroot_gpu_buffer_v1_interface gpu_manager_impl = {
    .destroy = gpu_manager_destroy,
    .create_buffer = gpu_manager_create,
};

static void gpu_manager_bind(struct wl_client *client, void *data,
                             uint32_t version, uint32_t id) {
    (void)data;
    if (version > 1) version = 1;
    struct wl_resource *resource = wl_resource_create(
        client, &alroot_gpu_buffer_v1_interface, (int)version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &gpu_manager_impl, NULL, NULL);
}

void alroot_gpu_buffer_init(struct wl_display *display) {
    if (!vkp_ahb_gpu_copy_supported()) {
        alroot_log("gpu-copy", "AHB-only wl_buffer not advertised (host Vulkan AHB import unavailable)");
        return;
    }
    if (!wl_global_create(display, &alroot_gpu_buffer_v1_interface, 1,
                          NULL, gpu_manager_bind)) {
        alroot_log("error", "AHB-only GPU-copy protocol global creation failed");
        return;
    }
    alroot_log("gpu-copy", "alroot_gpu_buffer_v1: vendor-neutral AHB-only VkImage transport available");
}
