/*
 * Alroot Wayland AHB GPU-copy WSI backend.  Vendor-neutral GPU buffer memory:
 * the Vulkan device copies its private optimal-tiled swapchain image into
 * an Android gralloc AHardwareBuffer using VkCmdCopyImageToBuffer.
 *
 * The server receives precisely that same AHB handle, never a fake DMA-BUF,
 * after a conservative CPU fence wait (until an explicit sync wire protocol
 * exists). AHB -> VkImage host GPU compositing is still a separate GPU copy.
 *
 * This module is opt-in and compiled only for the private S1.5 Turnip build;
 * the API is not enabled in arbitrary system Mesa packages or Xwayland.
 *
 * Importable external-buffer capability queries can be inaccurate with the
 * current Termux Vulkan loader; real FD properties, actual allocation and
 * five vendor EGL pixels are verified by the dedicated preflight program.
 * No claim of generic AHB import is based on a GPU brand or extension name.
 */
#include "wsi_common_private.h"
#include "vk_util.h"
#include "vk_device.h"
#include "util/macros.h"

#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

struct alroot_native_handle {
   int version;
   int numFds;
   int numInts;
   int data[];
};

struct ahb_api {
   void *handle;
   int (*allocate)(const AHardwareBuffer_Desc *, AHardwareBuffer **);
   void (*describe)(const AHardwareBuffer *, AHardwareBuffer_Desc *);
   void (*release)(AHardwareBuffer *);
   int (*send)(const AHardwareBuffer *, int);
   const struct alroot_native_handle *(*get_native)(const AHardwareBuffer *);
};
static struct ahb_api api;
static once_flag ahb_once = ONCE_FLAG_INIT;

static void
load_ahb_api(void)
{
   api.handle = dlopen("/system/lib64/libnativewindow.so", RTLD_NOW | RTLD_LOCAL);
   if (!api.handle)
      return;
   api.allocate = dlsym(api.handle, "AHardwareBuffer_allocate");
   api.describe = dlsym(api.handle, "AHardwareBuffer_describe");
   api.release = dlsym(api.handle, "AHardwareBuffer_release");
   api.send = dlsym(api.handle, "AHardwareBuffer_sendHandleToUnixSocket");
   api.get_native = dlsym(api.handle, "AHardwareBuffer_getNativeHandle");
}

static bool
ahb_available(void)
{
   call_once(&ahb_once, load_ahb_api);
   return api.allocate && api.describe && api.release &&
          api.send && api.get_native;
}

/* Match only the formats proven by the host AHB protocol. The Android
 * allocator accepts BGRA8 as a private gralloc format value 5 on S26U;
 * this must be renegotiated on other devices and protocol v1 hosts. */
static uint32_t
alroot_vk_to_ahb_format(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_R8G8B8A8_UNORM: return 1;
   case VK_FORMAT_B8G8R8A8_UNORM: return 5;
   default: return 0;
   }
}

uint32_t
wsi_alroot_ahb_get_format(const AHardwareBuffer *buffer)
{
   if (!ahb_available() || !buffer)
      return 0;
   AHardwareBuffer_Desc desc = {0};
   api.describe(buffer, &desc);
   return desc.format;
}

/* The native_handle_t layout and FD order are not a public NDK contract.
 * Fail closed unless exactly one FD is plausible for an RGBA8 image plane.
 * The caller receives an owned duplicate, which Vulkan owns on successful
 * import; the original AHB retains its own strong reference. */
static int
ahb_image_fd(const AHardwareBuffer *ahb, uint64_t *size,
             uint32_t *stride, AHardwareBuffer_Desc *desc_out)
{
   if (!ahb_available() || !ahb || !size || !stride)
      return -1;

   AHardwareBuffer_Desc desc = {0};
   api.describe(ahb, &desc);
   if (desc.layers != 1 ||
       (desc.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM &&
        desc.format != 5) ||
       desc.width == 0 || desc.width > 8192 ||
       desc.height == 0 || desc.height > 8192 ||
       desc.stride < desc.width || desc.stride > 16384 ||
       !(desc.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE))
      return -1;

   const struct alroot_native_handle *native = api.get_native(ahb);
   if (!native || native->version < 12 || native->numFds < 1 ||
       native->numFds > 8 || native->numInts < 0 || native->numInts > 1024)
      return -1;
   const uint64_t min_bytes = (uint64_t)desc.stride * desc.height * 4;
   uint64_t allowance = MAX2(min_bytes / 2, 65536);
   uint64_t closest = UINT64_MAX;
   int candidate = -1;
   bool ambiguous = false;
   for (int i = 0; i < native->numFds; i++) {
      struct stat statbuf = {0};
      if (native->data[i] < 0 || fstat(native->data[i], &statbuf) != 0 ||
          statbuf.st_size <= 0)
         continue;
      const uint64_t n = (uint64_t)statbuf.st_size;
      if (n < min_bytes || n - min_bytes > allowance)
         continue;
      if (n - min_bytes < closest) {
         closest = n - min_bytes;
         candidate = i;
         ambiguous = false;
         *size = n;
      } else if (n - min_bytes == closest) {
         ambiguous = true;
      }
   }
   if (candidate < 0 || ambiguous)
      return -1;

   const int fd = fcntl(native->data[candidate], F_DUPFD_CLOEXEC, 3);
   if (fd < 0)
      return -1;
   *stride = desc.stride;
   if (desc_out)
      *desc_out = desc;
   return fd;
}

VkResult
wsi_alroot_ahb_probe(uint32_t width, uint32_t height, VkFormat format,
                     struct wsi_alroot_ahb_image_params *params)
{
   if (!ahb_available() || !alroot_vk_to_ahb_format(format) ||
       width == 0 || height == 0 || width > 8192 || height > 8192)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   const AHardwareBuffer_Desc allocation = {
      .width = width,
      .height = height,
      .layers = 1,
      .format = alroot_vk_to_ahb_format(format),
      .usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
   };
   AHardwareBuffer *buffer = NULL;
   if (api.allocate(&allocation, &buffer) != 0 || !buffer)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   uint64_t size = 0;
   uint32_t stride = 0;
   AHardwareBuffer_Desc result = {0};
   int fd = ahb_image_fd(buffer, &size, &stride, &result);
   if (fd >= 0)
      close(fd);
   api.release(buffer);
   if (fd < 0 || result.width != width || result.height != height ||
       result.format != alroot_vk_to_ahb_format(format) ||
       size > UINT32_MAX || stride > UINT32_MAX / 4 ||
       size < (uint64_t)stride * 4 * height)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   *params = (struct wsi_alroot_ahb_image_params) {
      .base.image_type = WSI_IMAGE_TYPE_ALROOT_AHB,
      .width = width,
      .height = height,
      .row_pitch = stride * 4,
      .allocation_size = size,
   };
   fprintf(stderr, "S15_WSI_AHB_PROBE=PASS %ux%u format=%u stride=%u bytes=%llu\n",
           width, height, result.format, stride, (unsigned long long)size);
   return VK_SUCCESS;
}

VkResult
wsi_alroot_ahb_image_create_mem(const struct wsi_swapchain *chain,
                                const struct wsi_image_info *info,
                                struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VK_FROM_HANDLE(vk_device, vkdevice, chain->device);
   if (!ahb_available() || !vkdevice->dispatch_table.GetMemoryFdPropertiesKHR)
      return VK_ERROR_FEATURE_NOT_PRESENT;

   AHardwareBuffer *ahb = NULL;
   const AHardwareBuffer_Desc desc = {
      .width = info->create.extent.width,
      .height = info->create.extent.height,
      .layers = 1,
      .format = alroot_vk_to_ahb_format(info->create.format),
      .usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
   };
   if (api.allocate(&desc, &ahb) != 0 || !ahb)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   image->alroot_ahb = ahb;
   uint64_t fd_size = 0;
   uint32_t stride = 0;
   AHardwareBuffer_Desc real_desc = {0};
   int fd = ahb_image_fd(ahb, &fd_size, &stride, &real_desc);
   if (fd < 0 || fd_size != info->linear_size ||
       real_desc.format != alroot_vk_to_ahb_format(info->create.format) ||
       stride * 4 != info->linear_stride ||
       real_desc.width != info->create.extent.width ||
       real_desc.height != info->create.extent.height) {
      if (fd >= 0)
         close(fd);
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
   }

   const VkExternalMemoryBufferCreateInfo external = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   const VkBufferCreateInfo bci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = &external,
      .size = info->linear_size,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   VkResult result = wsi->CreateBuffer(chain->device, &bci, &chain->alloc,
                                      &image->blit.buffer);
   if (result != VK_SUCCESS) {
      close(fd);
      return result;
   }
   VkMemoryRequirements reqs;
   wsi->GetBufferMemoryRequirements(chain->device, image->blit.buffer, &reqs);
   VkMemoryFdPropertiesKHR fdprops = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
   };
   result = vkdevice->dispatch_table.GetMemoryFdPropertiesKHR(
      chain->device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      fd, &fdprops);
   const uint32_t types = reqs.memoryTypeBits & fdprops.memoryTypeBits;
   if (result != VK_SUCCESS || types == 0 || reqs.size > fd_size) {
      close(fd);
      return result == VK_SUCCESS ? VK_ERROR_FEATURE_NOT_PRESENT : result;
   }

   const VkImportMemoryFdInfoKHR imported_fd = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      .fd = fd,
   };
   const VkMemoryDedicatedAllocateInfo dedicated = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = &imported_fd,
      .buffer = image->blit.buffer,
   };
   const VkMemoryAllocateInfo alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &dedicated,
      .allocationSize = reqs.size,
      .memoryTypeIndex = wsi_select_device_memory_type(wsi, types),
   };
   result = wsi->AllocateMemory(chain->device, &alloc, &chain->alloc,
                                &image->blit.memory);
   if (result != VK_SUCCESS) {
      close(fd);
      return result;
   }
   /* Success transfers ownership of the duplicate FD to Vulkan, while the
    * AHB strong reference remains ours until destruction after GPU idle. */
   result = wsi->BindBufferMemory(chain->device, image->blit.buffer,
                                  image->blit.memory, 0);
   if (result != VK_SUCCESS)
      return result;

   /* Internal optimal VkImage is the application-visible swapchain image;
    * the WSI blit creates a GPU copy into the imported AHB-backed buffer. */
   wsi->GetImageMemoryRequirements(chain->device, image->image, &reqs);
   const VkMemoryDedicatedAllocateInfo image_dedicated = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .image = image->image,
   };
   const VkMemoryAllocateInfo image_alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &image_dedicated,
      .allocationSize = reqs.size,
      .memoryTypeIndex = wsi_select_device_memory_type(wsi,
                                                       reqs.memoryTypeBits),
   };
   result = wsi->AllocateMemory(chain->device, &image_alloc,
                                &chain->alloc, &image->memory);
   if (result != VK_SUCCESS)
      return result;
   image->num_planes = 1;
   image->sizes[0] = (uint32_t)info->linear_size;
   image->row_pitches[0] = info->linear_stride;
   image->offsets[0] = 0;
   fprintf(stderr, "S15_WSI_AHB_IMPORTED_BUFFER=PASS bytes=%llu pitch=%u\n",
           (unsigned long long)fd_size, info->linear_stride);
   return VK_SUCCESS;
}

void
wsi_alroot_ahb_image_destroy(struct wsi_image *image)
{
   if (image->alroot_ahb && ahb_available()) {
      api.release(image->alroot_ahb);
      image->alroot_ahb = NULL;
   }
}

int
wsi_alroot_ahb_send(struct AHardwareBuffer *ahb, int socket)
{
   return ahb && ahb_available() ? api.send(ahb, socket) : -1;
}
