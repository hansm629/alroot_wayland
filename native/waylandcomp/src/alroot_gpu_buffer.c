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

/* Android gralloc's BGRA8 allocation value. It is intentionally used only
 * after a real AHardwareBuffer + host Vulkan VkImage import probe succeeds.
 * Do not assume support on Mali, PowerVR, Xclipse, or every Adreno model. */
#define ALROOT_AHB_BGRA8_FORMAT ALROOT_GPU_BUFFER_V1_FORMAT_BGRA8888
static int g_bgra8_import_supported;

static int probe_bgra8_import(void) {
    AHardwareBuffer_Desc desc = {
        .width=32, .height=32, .layers=1,
        .format=ALROOT_AHB_BGRA8_FORMAT,
        .usage=AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
    };
    /* AHardwareBuffer_isSupported is API 29+, but this helper still targets
     * API 26. Allocation + Vulkan import below is the definitive probe. */
    AHardwareBuffer *buffer=NULL;
    if (AHardwareBuffer_allocate(&desc,&buffer)!=0 || !buffer)
        return 0;
    struct vkp_image *image=vkp_image_from_ahb(buffer);
    if (image) vkp_image_destroy(image);
    AHardwareBuffer_release(buffer);
    return image!=NULL;
}

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

static void gpu_manager_create_common(struct wl_client *client,
                                      struct wl_resource *manager,
                                      uint32_t id, int socket,
                                      uint32_t width, uint32_t height,
                                      uint32_t expected_format,
                                      int format_request_v2) {
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
    if (desc.format != expected_format ||
        (format_request_v2 &&
         expected_format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM &&
         expected_format != ALROOT_AHB_BGRA8_FORMAT) ||
        (expected_format == ALROOT_AHB_BGRA8_FORMAT &&
         !g_bgra8_import_supported) ||
        !(desc.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE)) {
        AHardwareBuffer_release(ahb);
        wl_resource_post_error(manager, ALROOT_GPU_BUFFER_V1_ERROR_UNSUPPORTED_FORMAT,
                               "AHB format not negotiated, mismatched or not GPU-sampleable");
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
    /* FourCC for diagnostics; actual VkImage import always uses the
     * gralloc description. AB24 is RGBA8; AR24 is BGRA8. */
    buffer->format = desc.format == ALROOT_AHB_BGRA8_FORMAT ?
                     DRM_ARGB8888 : DRM_ABGR8888;
    buffer->modifier = MOD_INVALID;
    buffer->gpu_ahb = ahb;
    buffer->n_planes = 0;
    for (int i = 0; i < MAX_PLANES; ++i) buffer->fd[i] = -1;
    gpu_buffer_note_allocated(buffer);
    wl_resource_set_implementation(resource, &alroot_gpu_buffer_impl, buffer,
                                   gpu_buffer_resource_destroy);
    struct client_info *info = client_info_of(client);
    if (info) info->dmabuf_buffers++;
    alroot_log("gpu-copy",
               "new AHB wl_buffer %ux%u %s, Vulkan GPU-copy transport (no DRM fd)",
               width, height,
               desc.format == ALROOT_AHB_BGRA8_FORMAT ? "BGRA8" : "RGBA8");
}

static void gpu_manager_create(struct wl_client *client,
                               struct wl_resource *manager,
                               uint32_t id, int socket,
                               uint32_t width, uint32_t height) {
    /* Strict v1 semantics, including for version-2 clients that still
     * call the legacy request. No behavior change for the S1.1 baseline. */
    gpu_manager_create_common(client,manager,id,socket,width,height,
                              AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,0);
}

static void gpu_manager_create_with_format(struct wl_client *client,
                                           struct wl_resource *manager,
                                           uint32_t id, int socket,
                                           uint32_t width, uint32_t height,
                                           uint32_t format) {
    gpu_manager_create_common(client,manager,id,socket,width,height,format,1);
}

static const struct alroot_gpu_buffer_v1_interface gpu_manager_impl = {
    .destroy = gpu_manager_destroy,
    .create_buffer = gpu_manager_create,
    .create_buffer_with_format = gpu_manager_create_with_format,
};

static void gpu_manager_bind(struct wl_client *client, void *data,
                             uint32_t version, uint32_t id) {
    (void)data;
    const uint32_t highest=g_bgra8_import_supported?2u:1u;
    if (version > highest) version = highest;
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
    g_bgra8_import_supported=probe_bgra8_import();
    const uint32_t protocol_version=g_bgra8_import_supported?2u:1u;
    if (!wl_global_create(display, &alroot_gpu_buffer_v1_interface,
                          (int)protocol_version,
                          NULL, gpu_manager_bind)) {
        alroot_log("error", "AHB-only GPU-copy protocol global creation failed");
        return;
    }
    alroot_log("gpu-copy",
               "alroot_gpu_buffer_v1 version %u: AHB RGBA8 GPU-copy, BGRA8 VkImage import=%s",
               protocol_version,g_bgra8_import_supported?"verified":"not supported");
}
