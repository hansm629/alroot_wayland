#ifndef BLEND_PASS_H
#define BLEND_PASS_H
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>

/* Prepared as an all-or-nothing scene: no commands are emitted if a view,
 * shader, framebuffer or descriptor could not be created. */
#define BLENDP_MAX_SCENE 32
struct blendp_item {
    VkImage img;
    VkFormat fmt;
    int width, height, blend, nearest, ahb;
    VkImageBlit rect;
    VkDescriptorSet descriptor;
};
int blendp_scene_prepare(VkImage dst, VkFormat fmt, int w, int h,
                         struct blendp_item *items, int n);
void blendp_scene_draw(VkCommandBuffer cmd, int w, int h,
                       const struct blendp_item *items, int n);
void blendp_bind_device(VkDevice dev);
void blendp_forget_image(VkImage img);
void blendp_begin_frame(void);

int blendp_draw(VkCommandBuffer cmd, VkImage src, VkFormat src_fmt, int src_w, int src_h, int src_general,
                VkImage dst, VkFormat dst_fmt, int dst_w, int dst_h, const VkImageBlit *blit);

#endif
