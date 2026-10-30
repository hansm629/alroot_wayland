# GPU-S29 — native-pixel resize preservation instead of S28 frame stretching

**2026-10-31, S26 Ultra Adreno 840.** Private Termux KGSL/GLAMOR
Xwayland, XFCE4/Xfwm4, S26 Vulkan Wrapper and Mesa27 Zink.
**Candidate built and isolated regression tested. Physical Android screen
manual-drag acceptance pending. Not promoted to the `awstart` default.**

## Source: user's two screen recordings

The videos `Screen_Recording_20261031_005054_Alroot Wayland.mp4`
(glxgears, ~22.7 s) and
`Screen_Recording_20261031_005329_Alroot Wayland.mp4`
(vkcube, ~27.3 s) were reviewed. While dragging the window edges, the
cube becomes blurry and stretches with the aspect ratio, then sharpens
when resizing stops. The gears change apparent size/position abruptly;
previous red content remains near the right and lower window edges.

This is **not** S26's former `GLXBadCurrentWindow` client crash. S28's
source explains the geometric distortion: it applies
`PictTransform(old_width/new_width, old_height/new_height)` and XRender
`bilinear` to stretch the previous *already rendered* image across a new
viewport. Multiple consecutive resizes can resample the temporary image
again, compounding blur and moving old colored edges into newly created
regions. Four-corner black-pixel checks were insufficient to certify this.

## S29 scoped native-pixel treatment

`graphics/patches/xwayland/kgsl/0006-kgsl-native-pixel-resize-preserve.patch`
applies **after** S28's 0005 patch in the project-private KGSL Xwayland
source. Select only via `S29_KGSL_NATIVE_RESIZE=1` on the exact new
private S29 binary, not with S28's opt-in.

1. Restrict to rootful Adreno KGSL surfaceless GLAMOR and a Mesa Vulkan
   XCB WSI-marked (`_MESA_DRV`) window; exclude the screen root, XFWM4
   decorations, ordinary GTK/Qt apps, rootless and other vendor drivers.
2. GPU-copy the **old frame's (2,2) border pixel** into a 1×1 GPU pixmap.
   XRender repeated `PictOpSrc` initializes the *whole* new drawable with
   that frame-derived background color, including previously stale right
   and bottom regions. This is a heuristic for the current graphics demos;
   arbitrary clients with artwork at (2,2) may need a broader solution.
3. Copy the old rendered image into the center of the resized drawable in
   **exact 1:1 pixels**, integer offsets and bounded clipping. No scaling
   transform, bilinear blur, duplicated border tiles or software pixel
   resampling. The client's *next actual* Vulkan Present replaces this
   temporary frame; no fake GLX SwapBuffers success or altered Zink shader.
4. Preserve S24's Vulkan logical/physical AHB height contract and S26's
   bounded transient DRI3 import recovery; those are separate unchanged
   client-side driver components. No XServer `glReadPixels`/`XGetImage`,
   and no new CPU pixel copy. This **does not imply end-to-end zero-copy**;
   existing GPU-copy and fence costs remain.

## Real GPU and synthetic correctness results

`graphics/tests/s29-native-pixel-exact.c` creates a synthetic Vulkan
WSI-tagged X11 test window. It draws a **sharp 20px red bar** and different
red/blue strips on the old right/bottom edges, then changes aspect ratio
**25 times with no client repaint**. It tests the server's temporary
previous-frame rendering rather than a new application frame.

| Synthetic no-XFWM4 test | S28 | S29 |
| --- | ---: | ---: |
| Original red object width | 20px | 20px |
| Width after 25 resizes | **36px** | **20px** |
| Rightmost new pixel | ghost red `0xd60000` | BG `0x333333` |
| Bottom-center new pixel | ghost blue `0x0000cf` | BG `0x333333` |
| Native geometry contract | expected FAIL | **PASS** |

When the synthetic test is instead hosted by isolated Xfwm4, the original
20px shape and cleared *right* edge are verified in S29, **but the lower
center can still be black if the synthetic client never sends any new
Present**. The optional `test-s29-native-geometry.py --wm` deliberately
reports this as a failure; do not hide this residual behavior.

For a real continuously presenting Vulkan `vkcube`, the nine-point
`graphics/tests/s29-vkcube-edge-sampler.c` samples the client's own known
background, including right-center and bottom-center (which S28's original
four-corner test missed):

| Isolated Xfwm4 + Vulkan, ~8 s | S28 | S29 |
| --- | ---: | ---: |
| Right-edge black observations | 21 | **1** |
| Bottom-center black observations | 23 | **1** |
| Combined | **44** | **2** |

Longer S29-only Xfwm4 run: **83 resizes in 15 seconds**, `vkcube`
survived; four right and three bottom-center black observations remained.
These are **X11 pixel readback observations, not actual visible Android
display frames**. The isolated Xfwm4 instance is not the full user XFCE
profile. Thus the patch is **improved, not a complete 0-flicker PASS**.

With Mesa27 Zink on the isolated Xfwm4 test server, `glxgears` survived
39 repeated resizes. The user must still check whether the previous
gear motion/edge artifacts are gone on the physical Android screen.

## Build, integrity and tests (do not restart the user's XFCE)

The S16/S27 private original (SHA256 `f52749b8…`) and S28 private
server (`cd107168…`) are preserved. S29 has a **different private
executable** at `.source/gpu-stage1/s29-native-resize/runtime/Xwayland`
(SHA256 `3e2d56bc…`). Normal Xwayland build scripts exclude S28/S29 and
refuse to overwrite older private binaries while the S29 source overlay
is present. Installed Termux-X11, Vulkan drivers, Android APK, XFCE/IME
settings, and active desktop are untouched by these build/tests.

```bash
cd ~/alroot_wayland
bash graphics/tools/build-s29-kgsl-native-resize.sh
bash graphics/tools/test-s29-build-and-scope.sh
python3 graphics/tools/test-s29-native-geometry.py
python3 graphics/tools/test-s29-vulkan-resize-edges.py --wm --seconds 8
python3 graphics/tools/test-s29-vulkan-resize-edges.py --wm --mode candidate --demo zink --seconds 8
```

The tests start only disposable authenticated Xwayland `:201..:240`
processes and resize their own graphics windows. They do **not** move or
close the user's apps. The source test includes a negative S28 control.

## Optional physical XFCE visual A/B — explicit native Termux shell

**Only after saving work in the running XFCE**, use an **independent
native Termux shell**, not an XFCE terminal:

```bash
cd ~/alroot_wayland
bash graphics/tools/start-s29-kgsl-native-resize.sh --restart --dry-run
bash graphics/tools/start-s29-kgsl-native-resize.sh --restart
```

Wait until `awstart --status` reports private Xwayland/XFCE `READY`,
then open the **Alroot Wayland Android helper** (not Termux-X11 `:0`).
Test the S26 private Wrapper **one demo at a time**. Close/stop the
first process before starting the second:

```bash
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual zink
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual vulkan
```

The S26 test runners use the shared exact executable/PID/display/UID/SHA
allowlist, now including S29. Do not separately call `awstart --stop`.
The S29 launcher is an **explicit** private XFCE restart; it does not
use Android `top-app` CPU scheduling and cannot guarantee equal FPS.

**Rollback to S28** (from the independent native Termux shell):

```bash
bash graphics/tools/start-s29-kgsl-native-resize.sh --restore-s28
```

Or `--restore-s16` for the original private KGSL server. All versions
remain project-private. **Do not promote S29 to the default** before
the user's manual resize acceptance, remaining rare Xfwm4 edge-black
samples and the later GPU-copy/Present/fence performance evaluation.
