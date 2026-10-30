# GPU-S24 — Qualcomm Vulkan Wrapper XCB resize and VkImageView lifetime

**Date:** 2026-10-30. **Test GPU:** Galaxy S26 Ultra / Adreno 840
(Qualcomm Android Vulkan 1.4.295). **Mesa source baseline:**
`b3b32abb82af4d374fd35c7192a7f524a5fc8bd3` from the existing
`~/alroot-vk-lab/vulkan-wrapper-android` reference. The installed system
wrapper, Termux-X11, running XFCE and Android helper APK are untouched.

## Reproduced failure before S24

Real XFCE4 Xwayland `:94`, private KGSL GLAMOR and AHB DRI3 modifier 1255.
`vkcube --wsi xcb` created a 500x500 swapchain successfully, but a resize
to 900x650 failed while importing the replacement AHB to KGSL EGL.
`eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` returned **EGL_BAD_ALLOC 0x3003**.
The failed DRI3 pixmap returned `VK_ERROR_INITIALIZATION_FAILED` (-3),
destroyed the old images, and vkcube subsequently attempted `vkCreateImageView`
on a destroyed 500x500 image. The wrapper's image table reported
`tracked=0`; Qualcomm `vulkan.adreno.so::vkCreateImageView` faulted with
SIGSEGV at address 0x70. `glxgears` with Wrapper + Zink also failed at
`X_GLXSwapBuffers` with `GLXBadCurrentWindow`. The same GLX bug appeared
with system Mesa26.3, not just Mesa27.

An **independent on-device AHB→EGL probe** (`graphics/tests/s24-wrapper-ahb-egl-import.c`)
confirmed the actual source of the failed import on S26U:

| BGRA8 AHB size | KGSL `eglCreateImageKHR` |
| --- | --- |
| 900×648 | PASS |
| 900×649 | FAIL 0x3003 |
| 900×650 | FAIL 0x3003 |
| 900×651 | FAIL 0x3003 |
| 900×652 | PASS |
| 1200×700 | PASS |

This dimension rule was reproduced with several widths and AHB usage flags.
On this driver the imported BGRA8 LINEAR buffer needs a **physical height
multiple of 4**, not just a suitable row stride. Do not generalize this rule
to Turnip, Mali, PowerVR or Xclipse without driver-specific proof.

## Final S24 architecture — correctly sized Vulkan objects

The initial quick workaround allocated AHB 900x652 but bound it to a Vulkan
image with a declared height of 650. Although GPU pixels appeared correct,
that **violates Vulkan's Android hardware buffer dedicated-import image
dimension contract**, and is NOT the final fix. The final implementation
uses Mesa WSI's existing **`WSI_SWAPCHAIN_IMAGE_BLIT`** path, with two
separate Vulkan images. The example physical allocation is:

```text
vkcube / Zink / GLX: XCB window 900x650 (logical, unchanged)
      │
      ▼
VkImage render: B8G8R8A8, 900x650, ordinary GPU memory
      │ vkCmdCopyImage (900x650 rectangle, GPU-only transfer)
      ▼
VkImage blit: BGRA8, 900x652, dedicated imported AHB 900x652
      │
      ▼
XCB DRI3 pixmap: 900x652 physical BGRA8, AHB IPC modifier 1255
      │
      ▼
Private Xwayland KGSL EGL DMA-BUF importer → XFCE visible 900x650
```

The AHB's physical dimensions **match the actual Vulkan image it is bound
to**; `vkCmdCopyImage` operates on only the requested logical rectangle.
Extra physical rows are outside the X11 window and not exposed as guest
content. The source-only assertions additionally check the blit path and
correct use of `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`.

Implementation details in private Mesa wrapper source:

1. Scope to `VK_ICD_WSI_PLATFORM_XCB` and **exactly**
   `S24_WRAPPER_XCB_AHB_PAD_HEIGHT=1`, and only select the GPU image-blit
   path for heights not divisible by 4; aligned dimensions keep the
   previous direct AHB path.
2. Preserve `info->image_type=WSI_IMAGE_TYPE_ANDROID` after
   `wsi_configure_image` zeroes `struct wsi_image_info`, and maintain
   transfer-source usage in both legacy and usage2 flags.
3. Build the AHB-bound destination `VkImage` with its own physical height,
   with matching dedicated-import dimensions and BGRA8 format; retain the
   original logical dimensions for the application's render VkImage and
   GPU copy region. The blit command transitions the destination from
   `UNDEFINED`, then copies only logical pixels.
4. Advertise the physical height to the validated XCB AHB DRI3 socket
   importer, without changing the X11 window dimensions or Android output
   transform. This is **not** an end-to-end zero-copy path.
5. In `wrapper_CreateImageView`, fail cleanly with
   `VK_ERROR_INITIALIZATION_FAILED` for a destroyed/untracked VkImage
   instead of forwarding its stale handle to the vendor driver.
6. Restore real Vulkan errors on AHB memory allocation failure rather than
   accidentally returning `VK_SUCCESS` with an unbound image. Keep optional
   image and callback diagnostics behind `S24_TRACE_WRAPPER_RESIZE=1`.

## Durable artifacts and isolated commands

The four-file S24 source diff is
`graphics/patches/mesa/wrapper/0003-s24-xcb-ahb-aligned-gpu-copy.patch`.
It applies **after the existing S16 prepared private wrapper/XCB WSI**;
the build script detects missing prerequisites and refuses to patch a
reference source symlink. A byte-for-byte four-file patch replay against
the pre-S24 source snapshots passed. **Never edit or install the system
Termux wrapper just to test S24.**

```sh
cd ~/alroot_wayland
bash graphics/tools/build-s24-wrapper-xcb-resize.sh
bash graphics/tools/test-s24-wrapper-xcb-resize.sh
S24_RESIZE_STRESS=1 bash graphics/tools/test-s24-wrapper-xcb-resize.sh
```

Private ICD: `.source/gpu-stage1/s24-wrapper-xcb-resize/runtime/wrapper_icd.private.json`.
Launch a demo with this private ICD and the scoped opt-in environment;
the test harness already handles DISPLAY, Xauthority and Mesa27 Zink paths.
No `awstart --restart`, package-manager commands, global library paths or
system Vulkan extension advertisements are needed.

## Real S26U acceptance results

On the existing XFCE Xwayland `:94` with Mesa27 Zink, tests performed on
**only their own** WM-managed windows:

- `vkcube` 500×500 → 900×650 → 1200×700 → 500×500: **PASS**, no SIGSEGV,
  image views valid, client alive through all steps.
- Odd-size Vulkan series: **16/16 PASS** across batches (including
  900×649/650/651, 901×649/651, 1023×653, 1200×701, 1437×811);
  an additional final-build 10-case sequence also PASS.
- The edge tester samples 12 X11 pixels at both sides and in the last
  four logical rows of *each new* `vkcube` size: **12/12 PASS per step**,
  background stable 0x333333, no black cutoff.
- Vulkan Wrapper + **Mesa27 Zink `glxgears`**: **PASS** 900×650,
  1200×701, 777×673, 500×500, no GLXBadCurrentWindow or client exit.
- With the experimental option absent or `=0`, the 500×500 regular Vulkan
  and GLX paths remain functional (smoke PASS). The feature is not silently
  enabled and does not alter Wayland WSI.
- Private-only negative control: `graphics/tests/s24-wrapper-stale-imageview.c`
  creates and then destroys a normal 64×64 VkImage, deliberately calls
  `vkCreateImageView` using the stale handle and receives
  **`VK_ERROR_INITIALIZATION_FAILED` (-3)** rather than SIGSEGV or a vendor
  call. This confirms the lifetime guard is actually executed, not just
  detected by a source-text test. Never run this invalid-use negative control
  with the unpatched system Vulkan driver.
- `S24_RESIZE_STRESS=1 bash graphics/tools/test-s24-wrapper-xcb-resize.sh`:
  GPU demos, the stale-handle negative control, live frame pixel readback,
  server liveness, source contract checks, the option-OFF controls and
  protected Termux graphics hashes **PASS**.
- User XFCE Xwayland PID **24814**, XFCE PID **24951** stayed unchanged.
  The private test applications were cleaned at the end of each run.

These tests establish correct *sampled* logical X11 pixels, and repeated
Vulkan/GLX GPU execution without observed resize crashes. They do **not**
prove every Android display pixel or every compositor timing scenario;
the user must visually check actual resizable windows, cursor alignment,
PiP restore, title bars and the desktop while 3D drawing is active.

## Release scope and performance implications

**S24 is a validated S26U KGSL candidate, not the universal default.**
The odd-height path has an additional per-frame GPU copy by design. Other
drivers, resolutions, Wayland clients and Android compositor modes require
their own correctness checks. The final Android host still currently uses
`LINEAR DMA-BUF → VkBuffer GPU-copy`; **end-to-end zero-copy is not claimed**.

Do not promote `awstart` to the experimental GPU path until the user's
physical display accepts visual quality and the broader driver matrix
is tested. CPU fence and Present optimization may follow correctness,
not replace the required resize acceptance.
