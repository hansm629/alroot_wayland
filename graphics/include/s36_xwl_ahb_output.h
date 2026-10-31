#ifndef S36_XWL_AHB_OUTPUT_H
#define S36_XWL_AHB_OUTPUT_H

#include <wayland-client.h>
#include <EGL/egl.h>
#include <stdint.h>

/* Experimental Xwayland rootful KGSL output: source GL texture -> Android
 * AHB FBO -> alroot_gpu_buffer_v1 -> host Vulkan VkImage. Unlike the
 * previous linear DMA-BUF VkBuffer path, no host VkBuffer->VkImage copy.
 * The caller retains both wl_buffer and this opaque state until the
 * corresponding Xorg pixmap is destroyed. Runtime opt-in only. */
struct s36_ahb_output;

void s36_ahb_output_probe(struct wl_display *display);
/* Xorg may tear down and reinitialize a whole Xwayland generation using
 * a NEW Wayland connection inside the SAME process. Clear the manager
 * proxy before the former connection is destroyed, never reuse its ID. */
void s36_ahb_output_reset(struct wl_display *display);
struct wl_buffer *s36_ahb_output_create(struct wl_display *display,
                                        EGLDisplay egl,
                                        unsigned int source_texture,
                                        int width, int height,
                                        struct s36_ahb_output **out);
int s36_ahb_output_copy(struct s36_ahb_output *out,
                        unsigned int source_texture);
/* Direct rootful mode: GLAMOR owns the same AHB-backed GPU texture as its
 * output pixmap; no extra source->AHB blit is necessary. */
unsigned int s36_ahb_output_texture(struct s36_ahb_output *out);
void s36_ahb_output_glamor_owns_texture(struct s36_ahb_output *out);
struct wl_buffer *s36_ahb_output_recreate_buffer(struct s36_ahb_output *out);
void s36_ahb_output_destroy(struct s36_ahb_output *out);

#endif
