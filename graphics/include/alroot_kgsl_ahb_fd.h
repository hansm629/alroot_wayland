#ifndef ALROOT_KGSL_AHB_FD_H
#define ALROOT_KGSL_AHB_FD_H

#include <android/hardware_buffer.h>
#include <stdint.h>

/* Experimental KGSL-only access to the Android VNDK native handle.
 * Returns an owned fd; caller must close it or give it to a successful
 * GL_EXT_memory_object_fd import. Unsupported/multiple plausible planes
 * cause a hard failure instead of guessing the vendor's handle ordering. */
int alroot_kgsl_ahb_dup_image_fd(AHardwareBuffer *buffer,
                                 uint64_t *allocation_size,
                                 uint32_t *row_stride);

#endif
