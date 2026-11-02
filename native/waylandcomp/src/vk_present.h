#ifndef VK_PRESENT_H
#define VK_PRESENT_H
#include <stdint.h>
#include <android/native_window.h>
void vk_present_set_driver(const char *driver_path, const char *library_name,
                           const char *native_lib_dir);

void vk_present_set_window(ANativeWindow *window);
int vkp_apply_window_request(void);

int vkp_quest_layer_presentation(void);

enum vkp_scale_mode { VKP_MODE_OFF = 0, VKP_MODE_FIT = 1, VKP_MODE_STRETCH = 2, VKP_MODE_FILL = 3,
                      VKP_MODE_INTEGER = 4 };
enum vkp_align { VKP_ALIGN_CENTER = 0, VKP_ALIGN_TOP = 1, VKP_ALIGN_BOTTOM = 2, VKP_ALIGN_PICTURE_TOP = 3, VKP_ALIGN_PICTURE_BOTTOM = 4 };
void vk_present_set_scale_mode(int mode, int alignment);
int vkp_game_picture_rect(float rect[4]);
int vkp_output_to_scene(double ox, double oy, double *sx, double *sy);
void vkp_output_size(int *w, int *h);

struct vkp_image;

#define VKP_MOD_LINEAR          0x0000000000000000ULL
#define VKP_MOD_QCOM_COMPRESSED 0x0500000000000001ULL
#define VKP_MOD_INVALID         0x00ffffffffffffffULL
const char *vkp_modifier_name(uint64_t modifier);
int vkp_dmabuf_modifiers(uint32_t drm_format, uint64_t *out, int max);

struct vkp_image *vkp_image_from_dmabuf(int fd, uint32_t drm_format, uint64_t modifier,
                                        int w, int h, uint32_t stride, uint32_t offset);
struct vkp_image *vkp_image_import_dmabuf(int fd, uint32_t drm_format, uint64_t modifier,
                                          int w, int h, uint32_t stride, uint32_t offset,
                                          int as_blit_dst);
int vkp_image_is_dmabuf(const struct vkp_image *img);

struct vkp_image *vkp_image_create_shm(int w, int h);
struct AHardwareBuffer;
struct vkp_image *vkp_image_from_ahb(struct AHardwareBuffer *ahb);
void vkp_wait_idle(void);
void vkp_image_upload_shm(struct vkp_image *img, const void *data, int stride);

int vkp_image_width(const struct vkp_image *img);
int vkp_image_height(const struct vkp_image *img);
/* Pure event-thread query. A busy image must not be CPU-overwritten. */
int vkp_image_busy(const struct vkp_image *img);
void vkp_image_destroy(struct vkp_image *img);

struct vkp_draw {
    struct vkp_image *img;
    float sx, sy, sw, sh;
    int dx, dy, dw, dh;
    int blend;
    /* Borrowed immutable per-commit producer fence. The compositor owns the
     * original FD through frame retirement; the renderer imports a dup.
     * Appending the flag keeps existing zero-initialized draws compatible. */
    int acquire_fence_fd;
    int has_acquire_fence;
    uint64_t acquire_serial;
    /* v3 ownership also applies to an explicitly already-ready commit. */
    int external_ownership;
};

const char *vkp_gpu_name(void);

int vkp_ready(void);
/* Capability probe for optional AHB VkImage import; not a proven frame. */
int vkp_ahb_gpu_copy_supported(void);
int vkp_has_window(void);
int vkp_wait_window_request(int timeout_ms);
int vkp_device_lost(void);

/* Render success remains zero for synchronous and asynchronous frames.
 * RETRY means no new frame was submitted: retain damage/callbacks and retry.
 * Only the normal SDR vkp_render path may leave a frame pending. */
#define VKP_RENDER_RETRY 1
enum vkp_frame_poll_result {
    VKP_FRAME_FAILED = -1,
    VKP_FRAME_IDLE = 0,
    VKP_FRAME_PENDING = 1,
    VKP_FRAME_COMPLETE = 2,
};
int vkp_frame_pending(void);
/* Borrowed, stable until retirement. -1 means no pollable FD (including an
 * already-complete exported payload). The renderer owns and closes this FD.
 * After a successful SYNC_FD export it never queries the reset VkFence. */
int vkp_frame_completion_fd(void);
int vkp_frame_completion_exported(void);
/* Only a successful, full-source async generation can give this proof.
 * The compositor still matches exact immutable use serial/image/FD pairs and
 * validates the original sync_file's terminal error status at completion. */
int vkp_frame_sources_submitted(void);
int vkp_s47_enabled(void);
int vkp_s47_profile_enabled(void);
struct vkp_s47_stats {
    int enabled, completion_capable;
    uint64_t async_submitted, async_retired, source_proof_frames, empty_async_frames;
    uint64_t export_attempts, export_fds, export_ready, export_errors;
    uint64_t completion_fd_polls, completion_fd_queries, native_fence_polls, completion_errors;
    uint64_t fence_wait_calls, startup_fence_wait_calls, cancel_wait_calls;
    uint64_t device_idle_calls, reconfigure_idle_calls, error_idle_calls, shutdown_idle_calls;
};
void vkp_s47_get_stats(struct vkp_s47_stats *out);
/* COMPLETE/FAILED with pending==0 retires exactly one source generation.
 * FAILED with pending==1 is a terminal/status failure: resources remain
 * pinned and the caller must not declare their GPU reads completed. */
enum vkp_frame_poll_result vkp_frame_poll(void);
int vkp_render(int scene_w, int scene_h, const struct vkp_draw *draws, int n);
/* Stream/special callers keep the existing synchronous rendering contract. */
int vkp_render_sync(int scene_w, int scene_h, const struct vkp_draw *draws, int n);
int vkp_render_plain(int scene_w, int scene_h);
int vkp_base_black(int scene_w, int scene_h);

struct vkp_perf {
    unsigned acquires, presents, waits;
    int64_t acquire_ns, acquire_max_ns;
    int64_t present_ns, present_max_ns;
    int64_t wait_ns, wait_max_ns;
    unsigned base_presents;
    unsigned base_kept;
    unsigned gpu_release_waits;
    unsigned gpu_acquire_waits, acquire_import_failures;
    unsigned async_frames, async_completed, async_polls, async_retries;
    int64_t async_ns, async_max_ns;
};
void vkp_perf_take(struct vkp_perf *out);
int vkp_can_wait_sync_fd(void);

struct alroot_color;
struct vkp_hdr_frame {
    const unsigned char *is_hdr;
    float peak_nits;
    int tonemap;
    const struct alroot_color *color;
};
int vkp_render_hdr(int scene_w, int scene_h, const struct vkp_draw *draws, int n,
                   const struct vkp_hdr_frame *hf, int *how);
int vkp_pass_begin_hdr(int scene_w, int scene_h, const struct vkp_draw *draws, int n,
                       const struct vkp_hdr_frame *hf, int *rw, int *rh);

/* RETRY is returned before starting work and leaves wait_fd with the caller.
 * Layer callers must check vkp_frame_pending before taking a pool slot. */
int vkp_blit_image(struct vkp_image *src, struct vkp_image *dst, int wait_fd);
int vkp_image_readback(struct vkp_image *src, uint32_t *out, int max_px);
int vkp_update_map(int scene_w, int scene_h);
int vkp_map_draw(const struct vkp_draw *d, int out[8]);
int vkp_map_rect(int img_w, int img_h, int scene_w, int scene_h, int out[8]);
int vkp_surface_rotation_degrees(void);

ANativeWindow *vkp_window(void);
void vkp_signal_first_frame(void);

int vkp_pass_begin(int scene_w, int scene_h, const struct vkp_draw *draws, int n, int *rw, int *rh);
int vkp_pass_copy_to(struct vkp_image *dst, int wait_fd);
void vkp_pass_abort(void);

void alroot_log(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif
