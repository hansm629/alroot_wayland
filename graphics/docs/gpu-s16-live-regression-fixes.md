# GPU-S1.6 live XFCE regressions and candidate repairs (S26U, 2026-10-30)

**Branch:** `feature/gpu-s16-live-regression-fixes` on top of `e06a002`.
**Policy:** experimental opt-in Alroot Wayland S1.6 host and private KGSL
Xwayland only. The installed APK, Termux-X11 `:0`, global Mesa, kernel and
existing user's :92 desktop remain unchanged until an explicitly authorized
APK upgrade. For Xclipse/ANGLE/Mali/PowerVR these S26U results are not a
compatibility guarantee. The existing AHB Wayland Vulkan WSI and launcher's
default nested Labwc/pixman remain intact.

## Actual live symptoms (user report)

1. Mouse click hit-test differs from the Android pointer's position.
2. GLAMOR accelerated XFCE desktop visibly shimmers or looks like unstable
   low-quality video. This is not an assertion about image compression: the
   GPU path uses no video encode or network stream.
3. The native Vulkan `vkcube` demonstration does not appear on the Android
   display.
4. Default `glxinfo -B` and `glmark2` launch paths hang or fail.

## GPU client selection and Vulkan rendering: independently reproduced

The live private KGSL `Xwayland :92` exposes DRI3, GLX and Present. Mesa's
automatic GLX device detection produces `failed to get driver name for fd`,
`No driver found`, and (with Zink) `CreateSwapchainKHR failed with
VK_ERROR_INITIALIZATION_FAILED`.  These are **client-driver/WSI selection
failures**, not a failed KGSL GLAMOR compositor initialisation.

With *explicit, subprocess-scoped* drivers on the actual running `:92`:

| Test | Real result |
| --- | --- |
| Termux GLX `glxinfo -B` (KGSL + freedreno) | exit 0, Adreno 840 OpenGL 4.6 |
| Termux GLX `glmark2 -b build:use-vbo=false` (KGSL) | exit 0, application score 4214 |
| Debian 13 `aw-debian /usr/bin/glxinfo -B` (KGSL) | exit 0, Adreno 840 OpenGL 4.6 |
| Termux native Turnip `vkcube --wsi xcb --c 20` | exit 0, Turnip ICD 1.4.359 |
| Native vkcube default / wrapper ICD | still hangs in the live X11 test |
| Automatic Zink GLX | fails swapchain creation |

**Important:** `awstart` changes its own descendants' environment, NOT the
parent Termux terminal; the latter may still have `DISPLAY=:0`. The
`debian` command in a regular Termux window can invoke the user's global
PRoot login rather than the project `aw-debian` wrapper. Use a strictly
scoped test helper when debugging Alroot's X server:

```
cd ~/alroot_wayland
bash graphics/tools/run-s16-x11-client.sh glxinfo -B
bash graphics/tools/run-s16-x11-client.sh glmark2 -b 'build:use-vbo=false'
bash graphics/tools/run-s16-x11-client.sh debian-glxinfo -B
bash graphics/tools/run-s16-x11-client.sh vkcube --wsi xcb --c 20
```

The wrapper validates that the live Xwayland is the private KGSL variant,
reads its own Alroot display/auth, and chooses KGSL Mesa or installed native
Turnip **only for these test child processes**. No global or generic
launcher policy hard-codes KGSL. Full independent test:
`bash graphics/tools/test-s16-live-x11-clients.sh`.

### Vulkan X11 content vs Android present

A further test used `_NET_CLIENT_LIST` before/after starting the *native
Turnip XCB* `vkcube`. One new X11 window appeared, named `Vkcube X11` (not
the lower-case `vkcube` initially searched for), with `WM_STATE=Normal`,
500×500. `XGetImage` against **that newly created X11 window** returned
5/5 readable pixels, with different colors such as `0x333333`, `0x505050`,
`0x124a53`. Thus the X11 client has a mapped window with rendered pixels.

```
python3 graphics/tools/test-s16-vkcube-xcb-window.py
```

**Still not confirmed:** those colored X11 pixels were presented intact to
the **Android screen**. X11 `XGetImage` does not prove the Wayland
`wl_buffer`→Android Vulkan compositor transfer and display. The user's
report of an invisible demo window remains an active issue. Do not infer
success from vkcube process exit, X11 window existence, or renderer names.

## Mouse click offset: event routing bug and isolated source fix

Android `InputSurfaceView` previously forwarded physical mouse generic
`ACTION_BUTTON_PRESS` without first synchronising guest pointer position:
clicks could target a previously cached pointer position if no hover move
preceded the click. Android can additionally deliver a hardware mouse as
`onTouchEvent(ACTION_DOWN/UP)`, which previously ran the **finger touchpad**
algorithm, hid the Android cursor and clicked the guest cursor from a
separate cached state. That also permitted duplicated button events.

Candidate Java source changes ensure that hardware mouse coordinates are
updated *before* sending X11 button state, distinguish `SOURCE_MOUSE`
from finger gestures, deduplicate repeated physical button events and
preserve relative pointer-capture behavior. Coordinate FIT inversion was
checked offline against the actual `1920x1080 → 3120x1440` image transform
from host logs (picture 2560×1440 with x offset ~279), plus PiP 752×423 and
ordinary 1920×1080 cases:

```
python3 graphics/tools/test-s16-mouse-coordinate-policy.py
```

Those tests assert source routing and under-3-pixel inverse-coordinate
roundtrip. **Actual Android hardware pointer click correctness requires the
updated APK and a physical device test.** Existing IME, touchpad scrolling,
touchscreen and global `:0` mouse input must not be silently reverted.

## Shimmering: KGSL EGL → Android Vulkan sync-file GPU wait

The S1.6 existing GPU-copy imports a KGSL EGL texture's LINEAR DMA-BUF as
a Vulkan VkBuffer, then `vkCmdCopyBufferToImage` into a compositor-owned
VkImage. It did not carry an explicit EGL-producer wait into the Vulkan
submission. GLAMOR calls `glFinish()` before export, but that is not a full
cross-process buffer acquisition/reuse contract under continuous rendering.
The visual shimmering could reflect this race; it could also involve
buffer lifetime/refresh, interpolation/rescaling, or other issues.

On this S26U the actual EGL-exported KGSL DMA-BUF supports
`DMA_BUF_IOCTL_EXPORT_SYNC_FILE(DMA_BUF_SYNC_READ)` and vendor Vulkan supports
`vkImportSemaphoreFdKHR` with `VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT`
into a temporarily imported binary Vulkan semaphore. A new source-scoped
host Vulkan path snapshots the source's sync_file and waits on its semaphore
in the **same GPU submission** as the buffer→image copy (`TRANSFER` stage).
It allocates one semaphore per imported source VkBuffer and preserves AHB and
non-DMA-BUF routes. If unsupported it logs a diagnostic and retains the
previous glFinish fallback, not fake success. It does not use an additional
host `vkQueueWaitIdle`, CPU pixel copy, or arbitrary queue-family transfer.

**Actual isolated one-shot proof:**

```
S16_FULL_FRAME_VALIDATE=1 S16_GPU_SYNC_FD_WAIT=1 \\
ALROOT_S16_TEST_RESOLUTIONS=full \\
bash graphics/tools/test-s16-xwayland-gpu-copy.sh
```

| EGL source / GPU copy target | Whole-frame diagnostic GPU-copy result |
| --- | --- |
| 640×360 | 230,400 pixels checked, 0 mismatched |
| 800×600 | 480,000 pixels checked, 0 mismatched |
| 1280×720 | 921,600 pixels checked, 0 mismatched |
| 1920×1080 | 2,073,600 pixels checked, 0 mismatched |

All sync-file imports, Vulkan submissions and independent GPU-copy output
checks PASS. This proof uses diagnostic CPU readback **only to validate the
completed destination**; the actual Stage-1 host remains GPU-only. Test
without `S16_GPU_SYNC_FD_WAIT` also passes as a regression control.

**Not yet proven:** visual shimmering disappears on the real XFCE desktop;
producer release/reuse synchronization, multi-frame animation, 60/120Hz
frame pacing and the built Android APK's actual fence path must be examined
after an explicit upgrade. There remains a known 1024×768 KGSL allocation
size mismatch that this sync change does not solve. No full zero-copy or
windowed game scanout is claimed.

## Next live gates after APK build

Build the isolated feature branch's APK in CI, sign with the device's
compatible keystore into a **versioned test-only filename** and do NOT
automatically install or replace `artifacts/alroot-wayland-p01-signed.apk`
or restart the user's desktop. For an approved on-device A/B use the saved
previous APK as a rollback candidate of the same version code; check the
installed certificate first.

After installation confirm all of the following separately: physical mouse
click under the visual cursor, stable XFCE text/picture while scrolling and
moving windows, GLX renderer and `glmark2` under the correct private X11
display, XCB `vkcube` window's **Android visual pixels**, plus GPU source
fence log and absence of new Vk/GL errors. Compare with the previous
stable SHM/pixman path to isolate what is an S1.6 regression.
