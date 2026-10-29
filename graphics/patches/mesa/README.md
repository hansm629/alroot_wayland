# Per-driver WSI patch slots

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

**No driver patches have been copied or applied here yet.** A directory
label is not proof of support. Everything must be separately rebased,
built and tested on a private Alroot session, with Termux-X11 DRI3 intact.
