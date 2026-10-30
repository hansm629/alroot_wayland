# GPU-S26 — XCB/DRI3 transient AHB pixmap import recovery

**Status:** opt-in S26U private Vulkan Wrapper candidate; offline fault-injection
and live windowed GPU regression **PASS**. **Manual Android visual acceptance
pending**, and the exact cause of the original non-injected manual failure
has not been independently captured. Never promote to `awstart` by default.

## Reported failure and scope

The user saw Mesa 27 Zink `glxgears` rotating more smoothly under the S25
host build, but after approximately **44.3 seconds** of manual window
resizing, `glxgears` alone exited with code 1:

```
X Error of failed request: GLXBadCurrentWindow
  Major opcode: 152 (GLX)
  Minor opcode: 11 (X_GLXSwapBuffers)
```

The original Xwayland `:96` and XFCE4 were **still running** after this
specific client failure, as confirmed by the reported `awstart --status`
and the independent live device check. It is wrong to classify this
`glxgears` error as Xwayland crashing. The available Xwayland log included
nonfatal `xkbcomp` warnings and an isolated `S16 AHB DRI3 no unambiguous
linear-sized DMA-BUF fd` report; neither proves the cause of the client
failure.

The user also observed lower frame rates than Termux-X11. That comparison
is not yet an apples-to-apples benchmark, and this S26 patch **does not**
attempt GPU-copy, CPU-fence, vsync or Present performance optimization.

## Actual source failure chain

`Mesa 27 src/glx/drisw_glx.c::driswSwapBuffers()` →
`src/gallium/frontends/dri/kopper.c::kopperSwapBuffers()` →
`src/gallium/drivers/zink/zink_kopper.c` → Vulkan Wrapper XCB WSI →
`wsi_common_x11.c::x11_image_init()`.

When `xcb_request_check()` reports an error for a DRI3 imported AHB
pixmap, the S24 wrapper returns `VK_ERROR_INITIALIZATION_FAILED (-3)`
while creating the replacement swapchain. Zink can invalidate its
displaytarget, after which `kopperSwapBuffers()` returns `-1`.
`src/glx/glxcmds.c::__glXSwapBuffers()` maps that to `GLXBadCurrentWindow`
even if the underlying X11 window **continues to exist**. This is distinct
from the earlier, already fixed S24 stale-`VkImageView` SIGSEGV.

**Important proof limit:** the original client did not include the detailed
WSI trace. We have conclusively reproduced this **exact GLX client error**
by deliberately failing the 10th DRI3 AHB pixmap creation during resizing;
we cannot yet assert that this same `BadAlloc` was spontaneously returned
in the user's 44-second manual case. Alternative error paths such as
swapchain capability acquisition, actual window destruction and Vulkan
driver failures still require their own evidence, not ignored errors.

## Isolated patch implementation

Patch `graphics/patches/mesa/wrapper/0004-s26-xcb-dri3-transient-retry.patch`
applies **after** the S24 padded-height AHB GPU-copy patch, only in the
project-private Mesa-wrapper overlay and only to `wsi_common_x11.c`.

- Requires both explicit `S24_WRAPPER_XCB_AHB_PAD_HEIGHT=1` and
  `S26_WRAPPER_XCB_RETRY_TRANSIENT=1` for the retry. **S24 stays the default
  when `--s26` is not supplied**, and normal Termux/system Wrapper, Mesa,
  global Termux-X11, other vendors and Wayland WSI are untouched.
- Recognizes only `XCB_ALLOC` (BadAlloc) or `XCB_MATCH` (BadMatch) from
  an **AHB-backed DRI3 pixmap creation**. A thread-local flag prevents an
  unrelated image/driver error from being retried as a pixmap import.
- Before retrying a new AHB/pixmap, checks actual XCB window existence via
  `xcb_get_geometry`. If missing, aborts with `VK_ERROR_SURFACE_LOST_KHR`.
- The failed Vulkan image is destroyed by the original failure path;
  the candidate clears its image metadata before a fresh
  `wsi_create_image` and new XCB XID. It never binds a logical image to
  an AHB with a different physical extent; S24's paired-image copy remains.
- Only **two** additional attempts with ~2 ms backoff. Persistent errors
  propagate; no GLX error suppression, infinite loop or fake `VK_SUCCESS`.
- For deterministic test ONLY,
  `S26_WRAPPER_TEST_FAIL_PIXMAP_ONCE=10` injects one synthetic BadAlloc on
  the **10th** AHB DRI3 import after explicitly freeing the created test
  pixmap. The normal `--visual` runner deletes this environment variable.

## On-device test evidence

In a **running** S26U private KGSL GLAMOR XFCE4 `:96`, original Xwayland
PID 13948, XFCE PID 14054:

1. Baseline S24 automatic tests did **not** spontaneously fail, including
   215 normal, 173 tiny/odd-height, 627 rapid, and 296 xfwm4-requested
   window resizes. This is why a targeted negative fault-injection case
   was necessary; it does not equate to passing human manual interaction.
2. S26 *private binary*, with retry **disabled** and forced BadAlloc at
   import #10 while resizing to 901×649: `glxgears` exited 1 with precisely
   `GLXBadCurrentWindow`, `X_GLXSwapBuffers`, Xwayland still alive.
3. S26 retry **enabled**, same forced error: log shows one fresh AHB import,
   `S26_XCB_DRI3_AHB_RETRY attempt=1 image=0`, then result=0, Zink remains
   running until test-controlled SIGTERM (rc -15).
4. `vkcube --wsi xcb` likewise survives forced import failure at #10 and
   subsequent resizes (no vendor SIGSEGV, no stale VkImageView).
5. Full `S26_RESIZE_STRESS=1` acceptance PASS for both Zink and Vulkan
   (including S24 logical last-row XGetImage checks) and checks existing
   desktop PID consistency and protected system graphics hashes.

The *private* S24 baseline `.source/gpu-stage1/s24-wrapper-xcb-resize/runtime`
binary is preserved byte-for-byte. Because the Meson workspace is shared
between these two private builds, `build-s24-wrapper-xcb-resize.sh` now
**refuses to overwrite the S24 binary** while S26's source overlay is active.
The new S26 library is staged at a different private runtime path.

## Commands and acceptance

```bash
cd ~/alroot_wayland
bash graphics/tools/build-s26-wrapper-xcb-retry.sh
S26_RESIZE_STRESS=1 bash graphics/tools/test-s26-wrapper-xcb-resize.sh

# Interactive persistent windows. Open the Alroot Wayland helper APK,
# NOT Termux-X11 DISPLAY=:0; the script discovers the current Xwayland.
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual zink
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual vulkan
```

The injected failure is **never** enabled in interactive mode. The
window remains open until manually closed or Ctrl+C, so physically drag
the frame repeatedly for at least the prior 44.3-second failure window
and verify smooth continued rendering. **Do not classify as stable from
an automated short PASS alone.** The interactive mode prints
`S24_VISUAL_PRIVATE_WRAPPER=S26_TRANSIENT_RETRY`, confirming its private
ICD selection; it doesn't install the driver or change the Xserver.

If a real failure recurs, capture the full interactive terminal output
with `2>&1 | tee ~/alroot_wayland/.source/gpu-stage1/s26-wrapper-resize-retry/logs/manual-zink.log`
and the fresh Xwayland/Android host status. `glxgears` may fail through
other swapchain errors not covered by the bounded BadAlloc/BadMatch fix.

**Later host session note:** the Android Wayland host was relaunched at
19:49:51 (new Android process PID 15313), and the old Xwayland `:96` logged
`failed to read Wayland events: Broken pipe` before reaching DOWN status;
IME and clipboard still had their original PIDs. This happened *after*
the above tests passed, and is **not proven to be caused by the S26
client patch**, which was not installed into the Android host or Xwayland.
Do not silently restart or attach an X server as part of the private
Wrapper build. When a new GPU session is intentionally started, get the
current display from `awstart --status` rather than assuming `:96`.

**Remaining gates:** manual repeated drag with Zink and `vkcube`, live
frame pacing stability, and later Vulkan Wrapper/Turnip/Alroot Mesa
comparison. No `awstart` default promotion or performance claims yet.
