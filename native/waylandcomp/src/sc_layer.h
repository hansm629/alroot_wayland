#ifndef SC_LAYER_H
#define SC_LAYER_H
#include <stddef.h>
#include <stdint.h>
#include <android/hardware_buffer.h>

struct vkp_image;
struct vkp_draw;
struct alroot_color;

enum sc_layer_id { SC_LAYER_GAME = 0, SC_LAYER_OVERLAY = 1, SC_LAYER_COUNT = 2 };

int sc_layer_available(void);

void sc_layer_probe_dmabuf_fd(int fd);

int sc_layer_can_tag_hdr(void);
void sc_layer_hdr_symbols(char *out, size_t size);


void sc_s67_pool_stats(unsigned out[4]);
int sc_layer_present_s67(struct vkp_image *src,int w,int h,const struct alroot_color *c,int allow,int *queued);
int sc_layer_present(struct vkp_image *src, int scene_w, int scene_h, const struct alroot_color *color);

int sc_layer_present_pass(const struct vkp_draw *draws, int n, int scene_w, int scene_h);

struct vkp_hdr_frame;
int sc_layer_present_hdr_scene(const struct vkp_draw *draws, int n, const struct vkp_hdr_frame *hf,
                               int scene_w, int scene_h, const struct alroot_color *color);

int sc_layer_present_ahb(AHardwareBuffer *ahb, int w, int h, int acquire_fd, void *token,
                         int scene_w, int scene_h, const struct alroot_color *color, uint32_t ahb_format);

void sc_layer_set_frame_rate(float fps);

int sc_layer_overlay_affordable(void);

int sc_layer_present_overlay(struct vkp_image *src, const int geo[8], int translucent);

void sc_layer_hide(void);
int sc_layer_base_black(int width, int height);
void sc_layer_hide_base(void);
void sc_layer_hide_overlay(void);

void sc_layer_window_gone(void);
int sc_layer_token_is_current(void *token);

/* S65: dedicated opaque root layer on Android's per-buffer release API (36+).
 * The caller supplies an fd owned by this function; -1 means proven ready.
 * It must retain its own producer obligation even if Android drops a frame.
 * Serial identifies a single submission, never the address/ID of storage.
 * 1 = submitted, 2 = same active submission (geometry only), 0 = not applied,
 * -1 = bounded backpressure, retry when an actual release becomes available.
 * No release is synthesized on a failure. All calls except the internal
 * Binder callback run on the compositor's thread. */
int sc_root_present(AHardwareBuffer *ahb, int w, int h, int scene_w, int scene_h,
                    int owned_acquire_fd, uint64_t serial);
/* Optional frame-callback pacing signal. If armed != NULL, ask for a real
 * OnCommit event for this new submission; *armed is 1 only when installed.
 * OnCommit is NOT a display-latch or buffer-release event. */
int sc_root_present_track_commit(AHardwareBuffer *ahb, int w, int h, int scene_w, int scene_h,
                                 int owned_acquire_fd, uint64_t serial, int *armed);
int sc_root_active(void);
uint64_t sc_root_serial(void);
int sc_root_hide(void);
const char *sc_root_last_refusal(void);
uint64_t sc_root_window_epoch(void);
/* After disconnecting all Wayland clients: detach output and discard old
 * callback notifications without ever advertising buffer reuse. */
void sc_root_end_epoch(void);
/* Presenter owns the readiness fd. Call sc_root_ack_events once on wake, then
 * drain BOTH queues; pop operations never consume the other queue's wake.
 * take_release: 1 = serial and owned fd (-1 only for ready); 0 = empty.
 * Values below -1 are invalid payloads and must quarantine the use. */
int sc_root_event_fd(void);
void sc_root_ack_events(void);
int sc_root_take_release(uint64_t *serial, int *release_fd);
int sc_root_take_commit(uint64_t *serial, int64_t *callback_mono_ns);
void sc_root_cancel_commit(uint64_t serial);
struct sc_root_stats {
    uint64_t submits, callbacks, fenced, immediate, invalid, retries, hides;
    unsigned outstanding, high_water;
    uint64_t commit_armed,commit_arrived,commit_taken,commit_unavailable,commit_canceled,commit_late;
    uint64_t commit_latency_ns,commit_latency_max_ns;
    unsigned commit_pending,commit_high_water;
    uint64_t rate_vote_updates;
    unsigned rate_vote_mhz;
};
void sc_root_get_stats(struct sc_root_stats *out);

unsigned sc_layer_frames_take(void);
unsigned sc_layer_drops_take(void);

#endif
