#ifndef ALROOT_VULKAN_AHB_PRODUCER_H
#define ALROOT_VULKAN_AHB_PRODUCER_H
struct AHardwareBuffer;
/* Imports an existing AHB into the selected Vulkan ICD. When clear != 0,
 * submit a Vulkan GPU clear and wait for completion before exporting that
 * AHB to a Wayland host. Stage-1 correctness only; no per-frame path yet.
 * Return 0 on success. Never modifies VK_DRIVER_FILES or global ICDs. */
int alroot_ahb_vulkan_import_and_clear(struct AHardwareBuffer *buf, int clear);
#endif
