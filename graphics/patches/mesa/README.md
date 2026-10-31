# Per-driver WSI patch slots

Current installed evidence is in [S44](../../docs/gpu-s44-submit-overhead.md)
and [S42](../../docs/gpu-s42-installed-defaults.md). The original slot plan
below is historical; patch presence alone is not evidence of runtime support.

- `generic/`: vendor-neutral `wl_buffer` transport negotiation and stable
  present semantics (resize, FIFO/MAILBOX, frame callbacks, release).
- `wrapper/`: scoped Vulkan-wrapper Android AHB WSI adaptation, referencing
  S26U `~/alroot-vk-lab/vulkan-wrapper-android`; do not touch X11 WSI/DRI3.
- `turnip/`: use lfdevs PR #25 Mesa 27 pin; DMA-BUF first if importable,
  AHB bridge only after the client can actually allocate/import and present
  suitable buffers. Preserve existing `wsi_common_x11.c` unchanged.
- `egl/`: GL, GLES, Zink, MobileGL/virgl or ANGLE Vulkan-backed EGL pathways
  require explicit evidence of exportable render targets; no assumption that
  `EGL_ANDROID_image_native_buffer` or any DMA-BUF extension is universal.
- `zink/`: S44 empty submit-header compaction, opt-in aggregate profiling and
  the validated compiled default, applied to the accepted S42 Mesa source.

A directory label is not proof of support. The accepted S42/S44 records identify
the actually applied sources, built packages and private/installed validation.
New changes still require their own compatibility and runtime evidence.
