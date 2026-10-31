# GPU-S30 — tiny AHB DRI3 BadMatch and real-Present frame preservation

**2026-10-31 · S26 Ultra (Adreno 840) · private rootful Xwayland + Xfwm4.**

**Status:** build and reproducible *isolated* Vulkan/Zink stability tests PASS;
the real Alroot Android screen has **not** been restarted into S30 or
manually accepted. This is a **separate opt-in candidate**, not a new
default `awstart`, GPU driver, Mesa runtime or Android APK.

## User's real S29 regression

The supplied video `Screen_Recording_20261031_013045_Alroot Wayland.mp4`
shows `vkcube` progressively freezing/clipping during repeated window
resizing and then crashing. The screen-recorded terminal shows:

```text
S26_XCB_DRI3_AHB_IMPORT_FAILURE code=11 forced=0 extent=1x98
S26_XCB_DRI3_AHB_RETRY attempt=1 image=0 extent=1x98
S26_XCB_DRI3_AHB_RETRY attempt=2 image=0 extent=1x98
S24_VISUAL_CLIENT_EXIT_CODE=-11
```

`code=11` is **XCB BadMatch** for the AHB-backed DRI3 pixmap. The user's
`glxgears` reportedly did not run. A separate bounded test on the *actual*
S29 desktop later opened `glxgears` and ran at ~100 FPS; this does **not**
invalidate the user's failure or establish consistent manual GUI behavior.

## Failure 1 — size-only AHB native FD selection at tiny extents

In the S16 private Xwayland AHB socket adapter, native handle FDs are
selected using `st_size` proximity to `desc.stride*desc.height*4`.
At tiny widths on the S26U, `AHardwareBuffer` exposes **two different
FDs with the same rounded `st_size`**; their tie is intentionally rejected
by the old size-only heuristic. An *unchanged* S29 private server reproduces
the user's `BadMatch` and client exit in a disposable Xwayland `:201`:

```text
S30_AHB_TINY_DIAG logical=40x100 physical=40x100 stride=64 expected=25600 nfd=2
S30_AHB_FD_DIAG fdindex=0 fsize=28672 ino=306003
S30_AHB_FD_DIAG fdindex=1 fsize=28672 ino=306004
S16 AHB DRI3 no unambiguous linear-sized DMA-BUF fd
S26_XCB_DRI3_AHB_IMPORT_FAILURE code=11 forced=0 extent=40x98
```

The first controlled diagnostic of width **40×98** exits after only
two resizes. S26's bounded retry cannot fix an unchanged *deterministic*
importer rejection: all three attempts encounter the same tied-FD handle.

S30 adds an opt-in, narrow fallback when the original selector has no
result: `S30_KGSL_TINY_AHB_FD0=1`, BGRA8, width **1–64**, exactly two
native FDs, both nonnegative, matching size but **different inode**,
image allocation at least as large as `stride*height*4` and bounded
padding. Only in that observed Qualcomm KGSL case does it select FD[0].
The resulting KGSL EGL import succeeded for widths 1, 4, 8, 17, 40.
Never use this assumption on Mali/Xclipse/PowerVR, unknown AHB handles,
three-plus FDs or other layouts. The ordinary selector and server remain
unchanged otherwise. This is not a general gralloc metadata parser.

## Failure 2 — old-frame preservation overwrote new DRI3 content

S29 used an exact-pixel snapshot after S28's bilinear stretch was
rejected, but the previous-frame copying was still hooked into **both**
Xwayland window resizing and `SetWindowPixmap`. The latter is also used
by the X11 **Present flip**, whose new pixmap already contains the
client's *genuine* Vulkan render. Painting a previous image over this
can freeze a visibly rotating cube. Furthermore the old snapshot could
come from the shared **1920×1080 rootful backing pixmap**, instead of
the client's own 200×140 window, clipping/recropping the wrong region.

S30 changes the private KGSL GLAMOR window path to:

- Take a GPU-only snapshot in **window-local drawable coordinates**, not
  from the rootful full-screen pixmap. The stored buffer is stable over
  consecutive resizes, even when the client window temporarily becomes
  1×98. Do not recapture an already cropped resize placeholder.
- Distinguish **actual KGSL DRI3 imported GPU pixmaps** using
  `xwl_pixmap`'s real `kgsl_dri3_import` flag rather than the XID's owner
  (which is not sufficient for all Present/Composite cases).
- **Never overwrite an imported client DRI3 image** with old content.
  Temporary placeholders are for Xorg-owned drawable space only.
- Drop the stable snapshot when a genuine client Present **flip or copy**
  succeeds, as well as on window destruction/unrealize. Preserve S24's
  padded-AHB copy and S26's transient-import retry unchanged.
- Continue to use GPU GLAMOR/XRender copies without introducing
  `XGetImage`/`glReadPixels` into the server. This **does not imply
  end-to-end zero-copy**, absence of CPU-fence cost or final DAC behavior.

The implementation is source-scoped to a private S30 KGSL Xwayland,
selected by `S30_KGSL_STABLE_FRAME=1` plus the tiny-AHB gate. It never
changes the Termux X11 :0, Android host, XFCE keyboard/IME, panel or
Mali/Xclipse paths. `0007-kgsl-tiny-ahb-stable-xcb-present.patch` applies
after private KGSL patches 0001–0006 across five source files.

## Live GPU tests against *disposable* private Xwayland servers

| Test | S29 original | S30 separate candidate |
| --- | --- | --- |
| Vulkan `vkcube`, Xfwm4, 40×98 | exits after 2 resizes, `BadMatch` | survives 37 resizes through 1×98, 8×98, 40×98 |
| Vulkan after return to 500×500 | client already exited | **22 sampled frames, 21 changing hashes, 22 with non-background cube pixels** |
| Mesa27 Zink `glxgears`, Xfwm4, extreme sizes | user's physical failure reported | **38 resizes, client alive**, ~116 FPS in isolated test |
| 25 consecutive resize operations with **no new client repaint**, no Xfwm4 | original 20-pixel red object lost | **20 pixels preserved**, new right/bottom pixels clean |
| Existing user Xwayland/XFCE | not part of tests | **PIDs unchanged**, no restart |

The animation test samples **actual X11 drawable pixels**, not just
client process liveness: the 21 different hashes demonstrate that new
`vkcube` content is visible **after the final resize**. This is not a
frame-by-frame measurement of the physical Android display. The repeated
tiny resize may still cause intermittent black edges, and the isolated
Xfwm4 test of a *never-Presenting* synthetic window continues to show
black regions. **Neither physical smoothness nor zero flicker is proven.**

## Reproducible build/tests without disturbing XFCE

On the S26U native Termux shell, using the existing private source and
already built S16, S28 and S29 binaries:

```bash
cd ~/alroot_wayland
bash graphics/tools/build-s30-kgsl-stable-resize.sh
bash graphics/tools/test-s30-build-and-scope.sh
python3 graphics/tools/test-s30-native-stable-frame.py
python3 graphics/tools/test-s30-extreme-resize.py --mode baseline --wm --expect-baseline-failure
python3 graphics/tools/test-s30-extreme-resize.py --mode candidate --demo vkcube --wm --seconds 8
python3 graphics/tools/test-s30-extreme-resize.py --mode candidate --demo zink --wm --seconds 8
```

The negative control must reproduce the **same** 40×98 DRI3 BadMatch,
not simply any client error. These tests create authenticated throwaway
Xwayland displays `:201..:240`, do not control user mouse or other
windows, and leave the existing XFCE intact. `--wm` for the synthetic
*no-Present* geometry test is a **diagnostic known limitation**, not a
passing acceptance gate.

The pinned SHA256 of the separate S30 executable is
`4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31`.
S16/S27, S28 and S29 private executables remain unchanged. A strict
launcher/visual-test allowlist includes only these SHA-pinned private
binaries. The ordinary `build-xwayland-private.sh` now rejects an
S28/S29/S30 source overlay **before even applying its first old patch**.

## Physical visual acceptance — optional explicit restart only

Save any open XFCE work first. In an **independent native Termux shell**,
not a terminal inside the running XFCE, run:

```bash
cd ~/alroot_wayland
bash graphics/tools/start-s30-kgsl-stable-resize.sh --restart --dry-run
bash graphics/tools/start-s30-kgsl-stable-resize.sh --restart
```

After the Alroot Wayland helper XFCE desktop returns READY, open an XFCE
terminal, then run **one demo at a time**, closing the first before the
second:

```bash
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual zink
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual vulkan
```

Both test commands use the unchanged S26 *private Wrapper*, with the
corresponding S30 Xwayland now authenticated by the exact binary hash.
**Do not separately run `awstart --stop`.** The S30 launcher intentionally
restarts only the project XFCE session; the Android `top-app` CPU lane
is still **not** proven by this native-Termux-shell route.

To return to the previous S29 **from independent native Termux**:

```bash
bash graphics/tools/start-s30-kgsl-stable-resize.sh --restore-s29
```

`--restore-s28` and `--restore-s16` are also provided for additional
private-baseline comparisons. **Remain opt-in until the user confirms
full physical screen rendering, no crash on repeated drag, and acceptable
content continuity.**
