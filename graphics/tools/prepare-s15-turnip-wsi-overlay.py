#!/data/data/com.termux/files/usr/bin/python3
"""Reproducibly create the S1.5 private Mesa 27 WSI source, never edit 6f90e92.

Consumes the pinned/PR25-patched S1.4 source overlay and copies only its WSI
sources. Anchors must match the reviewed Mesa 27 code exactly; no fuzzy apply.
The Alroot WSI edits are not installed into the Termux system prefix.
"""
from __future__ import annotations
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STAGE = ROOT / '.source/gpu-stage27-pr25'
SRC = STAGE / 'mesa-private/source'
DST = STAGE / 's15-turnip/source'
WSI = DST / 'src/vulkan/wsi'
PIN = '6f90e92dfdacc4759d8e9ccb328805d0f0b4439b'
assert subprocess.check_output(['git','-C',str(STAGE/'source'),'rev-parse','HEAD'],text=True).strip() == PIN
assert (SRC/'src/vulkan/wsi/wsi_common_wayland.c').exists()
assert (SRC/'src/egl/drivers/dri2/platform_wayland.c').is_file()

# Rebuilding an unchanged private WSI should be idempotent. Record the
# previously generated files and restore their mtimes only when their final
# bytes are identical; Meson and Ninja otherwise unnecessarily rebuild all
# Turnip C++ objects on every invocation, often timing out on a handset.
_generated_names = (
    'meson.build', 'wsi_common.h', 'wsi_common_private.h',
    'wsi_common.c', 'wsi_common_wayland.c', 'wsi_alroot_ahb.c',
    'alroot-gpu-buffer-v1-client-protocol.h',
    'alroot-gpu-buffer-v1-protocol.c',
)
_previous = {}
for _name in _generated_names:
    _file = WSI / _name
    if _file.is_file() and not _file.is_symlink():
        _stat = _file.stat()
        _previous[_name] = (_file.read_bytes(),
                            _stat.st_atime_ns, _stat.st_mtime_ns)

def substitute(src: str, before: str, after: str, why: str) -> str:
    count = src.count(before)
    if count != 1:
        raise RuntimeError(f'S15_WSI_ANCHOR_MISMATCH {why} count={count}')
    return src.replace(before, after, 1)

if not DST.exists():
    shutil.copytree(SRC,DST,symlinks=True)
if WSI.is_symlink():
    WSI.unlink()
    WSI.mkdir()
for path in (SRC/'src/vulkan/wsi').iterdir():
    dest=WSI/path.name
    if not dest.exists() and not dest.is_symlink():
        dest.symlink_to(path.resolve())

# Never mutate the S1.4 or pinned files through a symlink. The edited
# WSI sources have dedicated real copies in this overlay.
# Important: copy2() preserves the old source mtime, which causes Ninja to
# reuse C++ driver objects built with a previous, incompatible wsi_device
# layout. Only leave the timestamp untouched when the file bytes match.
for name in ('meson.build','wsi_common.h','wsi_common_private.h','wsi_common.c','wsi_common_wayland.c'):
    output=WSI/name
    original=(SRC/'src/vulkan/wsi'/name).read_bytes()
    if output.is_symlink():
        output.unlink()
    elif output.is_file() and output.read_bytes()==original:
        continue
    output.write_bytes(original)

p=WSI/'wsi_common_private.h'
s=p.read_text()
s=substitute(s,'''   WSI_IMAGE_TYPE_CPU,
   WSI_IMAGE_TYPE_DRM,''','''   WSI_IMAGE_TYPE_CPU,
#ifdef ALROOT_WSI_AHB
   WSI_IMAGE_TYPE_ALROOT_AHB,
#endif
   WSI_IMAGE_TYPE_DRM,''','image enum')
s=substitute(s,'''struct wsi_cpu_image_params {
''','''#ifdef ALROOT_WSI_AHB
struct AHardwareBuffer;
struct wsi_alroot_ahb_image_params {
   struct wsi_base_image_params base;
   uint32_t width, height;
   uint32_t row_pitch;
   uint64_t allocation_size;
};
#endif

struct wsi_cpu_image_params {
''','image params')
s=substitute(s,'''struct wsi_image {
   VkImage image;''','''struct wsi_image {
#ifdef ALROOT_WSI_AHB
   struct AHardwareBuffer *alroot_ahb;
#endif
   VkImage image;''','image object')
s=substitute(s,'''extern uint64_t WSI_DEBUG;''','''extern uint64_t WSI_DEBUG;

#ifdef ALROOT_WSI_AHB
struct wsi_alroot_ahb_image_params;
struct wsi_image_info;
VkResult wsi_alroot_ahb_probe(uint32_t width, uint32_t height, VkFormat format,
                               struct wsi_alroot_ahb_image_params *params);
VkResult wsi_alroot_ahb_image_create_mem(const struct wsi_swapchain *chain,
                                         const struct wsi_image_info *info,
                                         struct wsi_image *image);
void wsi_alroot_ahb_image_destroy(struct wsi_image *image);
int wsi_alroot_ahb_send(struct AHardwareBuffer *ahb, int socket);
uint32_t wsi_alroot_ahb_get_format(const struct AHardwareBuffer *ahb);
#endif''','provider prototypes')
p.write_text(s)

p=WSI/'wsi_common.c';s=p.read_text()
s=substitute(s,'''   case WSI_IMAGE_TYPE_CPU: {
      const struct wsi_cpu_image_params *cpu_params =
         container_of(params, const struct wsi_cpu_image_params, base);
      return wsi_cpu_image_needs_buffer_blit(wsi, cpu_params) ?''','''#ifdef ALROOT_WSI_AHB
   case WSI_IMAGE_TYPE_ALROOT_AHB:
      return WSI_SWAPCHAIN_BUFFER_BLIT;
#endif
   case WSI_IMAGE_TYPE_CPU: {
      const struct wsi_cpu_image_params *cpu_params =
         container_of(params, const struct wsi_cpu_image_params, base);
      return wsi_cpu_image_needs_buffer_blit(wsi, cpu_params) ?''','blit policy')
s=substitute(s,'''   switch (params->image_type) {
   case WSI_IMAGE_TYPE_CPU: {''','''   switch (params->image_type) {
#ifdef ALROOT_WSI_AHB
   case WSI_IMAGE_TYPE_ALROOT_AHB: {
      const struct wsi_alroot_ahb_image_params *ahb =
         container_of(params, const struct wsi_alroot_ahb_image_params, base);
      VkResult result = wsi_configure_image(chain, pCreateInfo, 0, info);
      if (result != VK_SUCCESS)
         return result;
      /* The Vulkan client still renders to an optimal-tiled VkImage.
       * Mesa's ordinary GPU buffer blit copies into external AHB memory.
       * The host sees the exact Android gralloc buffer, no CPU upload. */
      wsi_configure_buffer_image(chain, pCreateInfo, 4, 4, info);
      info->linear_stride = ahb->row_pitch;
      info->linear_size = ahb->allocation_size;
      info->select_blit_dst_memory_type = wsi_select_device_memory_type;
      info->select_image_memory_type = wsi_select_device_memory_type;
      info->create_mem = wsi_alroot_ahb_image_create_mem;
      return VK_SUCCESS;
   }
#endif
   case WSI_IMAGE_TYPE_CPU: {''','image config')
s=substitute(s,'''#ifdef HAVE_LIBDRM
   result = wsi_drm_init_swapchain_implicit_sync(chain);
   if (result != VK_SUCCESS)
      goto fail;
#endif''','''#ifdef HAVE_LIBDRM
#ifdef ALROOT_WSI_AHB
   /* AHB handle transport is not DRM implicit sync. CPU completion waits
    * are retained at wl_surface_attach until protocol v3 explicit fences. */
   if (image_params->image_type != WSI_IMAGE_TYPE_ALROOT_AHB)
#endif
   {
      result = wsi_drm_init_swapchain_implicit_sync(chain);
      if (result != VK_SUCCESS)
         goto fail;
   }
#endif''','DRM sync isolation')
s=substitute(s,'''   wsi->DestroyBuffer(chain->device, image->blit.buffer, &chain->alloc);
}''','''   wsi->DestroyBuffer(chain->device, image->blit.buffer, &chain->alloc);
#ifdef ALROOT_WSI_AHB
   wsi_alroot_ahb_image_destroy(image);
#endif
}''','AHB lifetime')
s=substitute(s,'''   assert(info->image_type == WSI_IMAGE_TYPE_CPU ||
          info->image_type == WSI_IMAGE_TYPE_DRM);''','''   assert(info->image_type == WSI_IMAGE_TYPE_CPU ||
          info->image_type == WSI_IMAGE_TYPE_DRM
#ifdef ALROOT_WSI_AHB
          || info->image_type == WSI_IMAGE_TYPE_ALROOT_AHB
#endif
          );''','GPU blit image type assertion')
p.write_text(s)

p=WSI/'wsi_common_wayland.c';s=p.read_text()
s=substitute(s,'''#include <sys/types.h>
''','''#include <sys/types.h>
#ifdef ALROOT_WSI_AHB
#include <sys/socket.h>
#include "alroot-gpu-buffer-v1-client-protocol.h"
#endif
''','wayland headers')
s=substitute(s,'''   struct zwp_linux_dmabuf_v1 *wl_dmabuf;''','''   struct zwp_linux_dmabuf_v1 *wl_dmabuf;
#ifdef ALROOT_WSI_AHB
   struct alroot_gpu_buffer_v1 *alroot_ahb_manager;
#endif''','manager field')
s=substitute(s,'''   WSI_WL_BUFFER_SHM_MEMCPY,
};''','''   WSI_WL_BUFFER_SHM_MEMCPY,
#ifdef ALROOT_WSI_AHB
   WSI_WL_BUFFER_ALROOT_AHB,
#endif
};''','buffer enum')
s=substitute(s,'''   } else {
      if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0 && version >= 3) {''','''   } else {
#ifdef ALROOT_WSI_AHB
      if (strcmp(interface, alroot_gpu_buffer_v1_interface.name) == 0 &&
          version >= 1 && !display->alroot_ahb_manager &&
          os_get_option("ALROOT_TURNIP_WAYLAND_AHB") &&
          !strcmp(os_get_option("ALROOT_TURNIP_WAYLAND_AHB"), "1")) {
         display->alroot_ahb_manager =
            wl_registry_bind(registry, name, &alroot_gpu_buffer_v1_interface,
                             MIN2(version, 2));
      } else
#endif
      if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0 && version >= 3) {''','AHB registry opt-in')
s=substitute(s,'''   if (display->wl_dmabuf)
      zwp_linux_dmabuf_v1_destroy(display->wl_dmabuf);''','''   if (display->wl_dmabuf)
      zwp_linux_dmabuf_v1_destroy(display->wl_dmabuf);
#ifdef ALROOT_WSI_AHB
   if (display->alroot_ahb_manager)
      alroot_gpu_buffer_v1_destroy(display->alroot_ahb_manager);
#endif''','manager destruction')
s=substitute(s,'''   if (wsi_wl->wsi->force_bgra8_unorm_first) {
      /* Find BGRA8_UNORM''','''#ifdef ALROOT_WSI_AHB
   if (display->alroot_ahb_manager && !display->sw) {
      struct wsi_wl_format *f = wsi_wl_display_add_vk_format(
         display, &display->formats, VK_FORMAT_R8G8B8A8_UNORM,
         WSI_WL_FMT_ALPHA | WSI_WL_FMT_OPAQUE);
      if (f) {
         struct wsi_wl_format *first = u_vector_tail(&display->formats);
         struct wsi_wl_format tmp = *first;
         *first = *f;
         *f = tmp;
      }
      /* Append BGRA only after the RGBA priority swap: u_vector may
       * reallocate and invalidate the `f` pointer when it grows. */
      if (alroot_gpu_buffer_v1_get_version(display->alroot_ahb_manager) >= 2)
         wsi_wl_display_add_vk_format(
            display, &display->formats, VK_FORMAT_B8G8R8A8_UNORM,
            WSI_WL_FMT_ALPHA | WSI_WL_FMT_OPAQUE);
      fprintf(stderr, "S15_WSI_AHB_FORMAT=RGBA8_FIRST_BGRA8_IF_HOST_V2\\n");
   } else
#endif
   if (wsi_wl->wsi->force_bgra8_unorm_first) {
      /* Find BGRA8_UNORM''','RGBA first on AHB host')
s=substitute(s,'''   if (chain->retired)
      return VK_ERROR_OUT_OF_DATE_KHR;

   struct wsi_wl_surface *wsi_wl_surface = chain->wsi_wl_surface;
   bool mode_fifo = chain->base.present_mode == VK_PRESENT_MODE_FIFO_KHR;''','''   if (chain->retired)
      return VK_ERROR_OUT_OF_DATE_KHR;
#ifdef ALROOT_WSI_AHB
   if (chain->buffer_type == WSI_WL_BUFFER_ALROOT_AHB) {
      /* No acquire fence travels over Alroot AHB protocol v1/v2. Wait for
       * the GPU blit fence before exposing the shared buffer to the host. */
      VkFence fence = chain->base.fences[image_index];
      if (fence == VK_NULL_HANDLE)
         return VK_ERROR_DEVICE_LOST;
      VkResult ready = chain->base.wsi->WaitForFences(
         chain->base.device, 1, &fence, VK_TRUE, UINT64_MAX);
      if (ready != VK_SUCCESS)
         return ready;
   }
#endif

   struct wsi_wl_surface *wsi_wl_surface = chain->wsi_wl_surface;
   bool mode_fifo = chain->base.present_mode == VK_PRESENT_MODE_FIFO_KHR;''','conservative AHB GPU fence')
s=substitute(s,'''   switch (chain->buffer_type) {
   case WSI_WL_BUFFER_GPU_SHM:''','''   switch (chain->buffer_type) {
#ifdef ALROOT_WSI_AHB
   case WSI_WL_BUFFER_ALROOT_AHB: {
      if (!display->alroot_ahb_manager || !image->base.alroot_ahb)
         goto fail_image;
      const uint32_t fmt = wsi_alroot_ahb_get_format(image->base.alroot_ahb);
      const uint32_t protocol =
         alroot_gpu_buffer_v1_get_version(display->alroot_ahb_manager);
      if (fmt != 1 && !(fmt == 5 && protocol >= 2))
         goto fail_image;
      int sockets[2];
      if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0)
         goto fail_image;
      int sent = wsi_alroot_ahb_send(image->base.alroot_ahb, sockets[0]);
      close(sockets[0]);
      if (sent != 0) {
         close(sockets[1]);
         goto fail_image;
      }
      struct wl_buffer *buffer = fmt == 5 ?
         alroot_gpu_buffer_v1_create_buffer_with_format(
            display->alroot_ahb_manager, sockets[1],
            chain->extent.width, chain->extent.height,
            ALROOT_GPU_BUFFER_V1_FORMAT_BGRA8888) :
         alroot_gpu_buffer_v1_create_buffer(
            display->alroot_ahb_manager, sockets[1],
            chain->extent.width, chain->extent.height);
      close(sockets[1]);
      if (!buffer)
         goto fail_image;
      loader_wayland_wrap_buffer(&image->wayland_buffer, buffer);
      fprintf(stderr, "S15_WSI_AHB_WL_BUFFER=CREATED %ux%u format=%u protocol=%u\\n",
              chain->extent.width, chain->extent.height, fmt, protocol);
      break;
   }
#endif
   case WSI_WL_BUFFER_GPU_SHM:''','AHB wl_buffer creation')
s=substitute(s,'''   struct wsi_cpu_image_params cpu_image_params;
   struct wsi_drm_image_params drm_image_params;''','''   struct wsi_cpu_image_params cpu_image_params;
   struct wsi_drm_image_params drm_image_params;
#ifdef ALROOT_WSI_AHB
   struct wsi_alroot_ahb_image_params alroot_ahb_params;
#endif''','swapchain params declaration')
s=substitute(s,'''   } else {
      drm_image_params = (struct wsi_drm_image_params) {
         .base.image_type = WSI_IMAGE_TYPE_DRM,''','''   } else {
#ifdef ALROOT_WSI_AHB
      if (wsi_wl_surface->display->alroot_ahb_manager &&
          (pCreateInfo->imageFormat == VK_FORMAT_R8G8B8A8_UNORM ||
           (pCreateInfo->imageFormat == VK_FORMAT_B8G8R8A8_UNORM &&
            alroot_gpu_buffer_v1_get_version(
               wsi_wl_surface->display->alroot_ahb_manager) >= 2)) &&
          pCreateInfo->imageArrayLayers == 1 &&
          pCreateInfo->imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
         result = wsi_alroot_ahb_probe(pCreateInfo->imageExtent.width,
                                       pCreateInfo->imageExtent.height,
                                       pCreateInfo->imageFormat,
                                       &alroot_ahb_params);
         if (result != VK_SUCCESS)
            goto fail;
         image_params = &alroot_ahb_params.base;
         buffer_type = WSI_WL_BUFFER_ALROOT_AHB;
         fprintf(stderr,"S15_WSI_AHB_SWAPCHAIN=GPU_BUFFER_COPY format=%u\\n",
                 pCreateInfo->imageFormat);
      } else
#endif
      {
#ifdef ALROOT_WSI_AHB
      if (wsi_wl_surface->display->alroot_ahb_manager)
         fprintf(stderr,"S15_WSI_AHB_FALLBACK=IMAGE_FORMAT_%u\\n",
                 pCreateInfo->imageFormat);
#endif
      drm_image_params = (struct wsi_drm_image_params) {
         .base.image_type = WSI_IMAGE_TYPE_DRM,''','AHB image choice')
s=substitute(s,'''      buffer_type = WSI_WL_BUFFER_NATIVE;
      image_params = &drm_image_params.base;
   }

   result = wsi_swapchain_init''','''      buffer_type = WSI_WL_BUFFER_NATIVE;
      image_params = &drm_image_params.base;
      }
   }

   result = wsi_swapchain_init''','DRM path close')
s=substitute(s,'''   if (buffer_type == WSI_WL_BUFFER_NATIVE) {
      chain->drm_format''','''   if (buffer_type == WSI_WL_BUFFER_NATIVE
#ifdef ALROOT_WSI_AHB
       || buffer_type == WSI_WL_BUFFER_ALROOT_AHB
#endif
       ) {
      chain->drm_format''','alroot buffer native format')
p.write_text(s)

p=WSI/'meson.build';s=p.read_text()
s=substitute(s,'''platform_deps = []''','''platform_deps = []
wsi_alroot_c_args = []''','meson c flags')
s=substitute(s,'''  files_vulkan_wsi += files('wsi_common_wayland.c')''','''  files_vulkan_wsi += files('wsi_common_wayland.c')
  # Turnip private S1.5 WSI: add AHB GPU-copy transport without touching X11.
  files_vulkan_wsi += files('wsi_alroot_ahb.c',
                            'alroot-gpu-buffer-v1-protocol.c')
  wsi_alroot_c_args += '-DALROOT_WSI_AHB=1' ''','WSI compile sources')
s=substitute(s,'''  include_directories : [inc_include, inc_src],
  dependencies : [''','''  include_directories : [inc_include, inc_src],
  c_args : wsi_alroot_c_args,
  dependencies : [''','WSI compiler flag')
p.write_text(s)

provider = ROOT/'graphics/patches/mesa/turnip/wsi_alroot_ahb.c'
shutil.copy2(provider,WSI/'wsi_alroot_ahb.c')
xml = ROOT/'native/waylandcomp/protocols/alroot-gpu-buffer-v1.xml'
subprocess.run(['wayland-scanner','client-header',str(xml),str(WSI/'alroot-gpu-buffer-v1-client-protocol.h')],check=True)
subprocess.run(['wayland-scanner','private-code',str(xml),str(WSI/'alroot-gpu-buffer-v1-protocol.c')],check=True)
_same = 0
for _name, (_bytes, _atime, _mtime) in _previous.items():
    _file = WSI / _name
    if _file.is_file() and not _file.is_symlink() and _file.read_bytes() == _bytes:
        os.utime(_file, ns=(_atime, _mtime))
        _same += 1
assert not any(x.is_symlink() for x in [WSI/'wsi_common.c', WSI/'wsi_common_private.h',
                                        WSI/'wsi_common_wayland.c', WSI/'meson.build'])
assert not subprocess.check_output(['git','-C',str(STAGE/'source'),'status','--porcelain'],text=True).strip()
print(f'S15_WSI_OVERLAY_PREPARED={DST}')
print(f'S15_WSI_OVERLAY_UNCHANGED_FILES={_same}')
print('S15_WSI_PIN=6f90e92')
print('S15_WSI_SOURCE_TRACKED=graphics/patches/mesa/turnip/wsi_alroot_ahb.c')
