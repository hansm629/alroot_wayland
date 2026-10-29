#ifndef ALROOT_VULKAN_IMPORT_FD_H
#define ALROOT_VULKAN_IMPORT_FD_H
#include <stdint.h>
/* Diagnostic import of an EGL-exported fd in the actual Android system
 * Vulkan loader. No pixel copy/Wayland presentation. FD is borrowed. */
int alroot_probe_vulkan_import_fd(int fd, uint32_t drm_fourcc,
                                  int width, int height);
#endif
