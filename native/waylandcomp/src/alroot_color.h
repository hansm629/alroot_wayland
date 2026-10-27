#ifndef ALROOT_COLOR_H
#define ALROOT_COLOR_H
#include <stdint.h>
#include <wayland-server.h>

#define ALROOT_ADATASPACE_UNKNOWN    0
#define ALROOT_ADATASPACE_BT2020_PQ  163971072
#define ALROOT_ADATASPACE_BT2020_HLG 168165376

struct alroot_color {
    uint32_t identity;
    uint32_t primaries, tf;
    int32_t dataspace;
    int has_st2086;
    float red[2], green[2], blue[2], white[2];
    float max_lum, min_lum;
    int has_cta861;
    float max_cll, max_fall;
    char text[256];
};

void alroot_color_set_request(int mode, const char *source, int dxvk_hdr, int zero_copy_forced);
void alroot_color_set_display(int id, const char *name, const char *formats, int hdr10, float max_lum,
                              float max_avg, float min_lum, int ratio_available, float ratio, int api);
void alroot_color_ratio_sample(float ratio, int listener);
int alroot_color_last_frame_age_ms(void);
int alroot_color_gate_state(void);
void alroot_color_session_end(void);
int alroot_color_hdr_on_screen(void);
int alroot_color_hdr_state(void);
void alroot_color_set_sdr_white(float nits);
float alroot_color_sdr_white(void);
int alroot_color_requested(void);
void alroot_color_set_output(int on);
int alroot_color_output(void);
int alroot_color_tonemapped_on_screen(void);
void alroot_color_env_sample(int thermal, float headroom, int brightness, int bmode);

float alroot_color_desired_headroom(const struct alroot_color *c, char *why, size_t n);
void alroot_color_note_headroom_request(float ratio);
void alroot_color_set_highest_ratio(float ratio);
float alroot_color_screen_headroom(char *why, size_t n);

void alroot_color_init(struct wl_display *display);
int alroot_color_hdr_open(void);
void alroot_color_commit(struct wl_resource *surface);
const struct alroot_color *alroot_color_of(struct wl_resource *surface);
void alroot_color_client_gone(struct wl_client *client);
void alroot_color_stats_tick(void);
void alroot_color_frame_copied(const char *who, const char *reason);

enum alroot_hdr_path {
    ALROOT_HDR_ZERO_COPY = 0,
    ALROOT_HDR_LAYER_COPY,
    ALROOT_HDR_COMPOSED,
    ALROOT_HDR_SWAPCHAIN,
    ALROOT_HDR_TONEMAPPED,

};
void alroot_color_frame_shown(const struct alroot_color *c, int path, uint32_t ahb_format);
const char *alroot_ahb_format_name(uint32_t format);

#endif
