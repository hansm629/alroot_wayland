# GPU-S32 — undo S31 whole-window blanking on stale Present

**2026-10-31 · S26 Ultra / Adreno 840 · private rootful KGSL/GLAMOR
Xwayland + Xfwm4 · S26 private Vulkan Wrapper / Mesa27 Zink.**

**Status: isolated S32 opt-in implementation tested. The user has not
visually accepted this build on the physical Android screen. Not the
`awstart` default. S30 and all earlier private server binaries remain
available as unchanged rollback targets.**

## Actual S31 user regression

The user reported that S31 *worsened* both demos: `glxgears` flickered
during continuous window resizing, and the `vkcube` cube disappeared.
This is a specific regression caused by the S31 patch, not merely
preexisting GPU scheduling or S26 transient AHB import behavior.

In the S31 source, `s29_preserve_native_pixmap()` fills the whole new
window with an old corner-pixel color, then **does not** copy the old
client content when geometry differs by more than ~10%. Worse,
`xwl_window_s31_fill_uncovered_present()` runs **after a real client
Present copy**, and if the source pixmap has a greatly mismatched extent,
it calls `CompositePicture(PictOpSrc, fill, ..., 0,0,w,h)` for the **entire
window**. This erases correctly rendered cube/gear pixels as well as the
stale area. Preventing a clipped gear does not justify hiding the entire
live GPU window. The S31 design decision was incorrect.

## S32 changes: content first, not background replacement

`graphics/patches/xwayland/kgsl/0009-kgsl-nonblank-present-continuity.patch`
applies **after S31's 0008**, and is linked into a separate private
Xwayland binary. Only `S32_KGSL_NO_BLANK_PRESENT=1` selects the S32
policy, in conjunction with the S30 Qualcomm
`S30_KGSL_TINY_AHB_FD0=1` importer fix.

1. **No whole-window erasure after real Present.** Keep the genuinely
   rendered client area even if the Xfwm4 window is larger or smaller
   than the source image. If the source is smaller, GPU-fill **only the
   newly exposed right/bottom strips**. Do not scale old images, repeat
   obsolete gear pixels, or cover the entire client window with a flat
   color. If the source is larger, retain the actual copied content;
   a temporary geometric clip is preferable to deleting all content.
2. **Preserve last valid frame for actual ResizeWindow gaps.** S32 uses
   S30's window-local, GPU-only 1:1 snapshot while the client creates a
   new Vulkan swapchain, instead of S31's >10% background-only policy.
   It does not replay the stable snapshot **after** a real Present:
   experiments showed that doing so increased black-edge artifacts.
3. **Retire snapshots only when the real client buffer matches.** S31
   dropped the last stable frame on every Present, including when a
   350×252 source was being copied into a 900×720 X window. S32 retains
   the snapshot until a source with exactly the window width and logical
   height (allowing 0–3 additional S24 padded AHB rows) has actually
   been presented. Flip and copy paths both supply the actual source
   pixmap to this check.
4. **Do not modify imported DRI3 client images.** Before GPU strip fill,
   check whether the Xorg destination pixmap is an actual KGSL-imported
   client image. If so, skip the copy; the X server must not overwrite
   an application's shared Vulkan/GLX buffer.
5. **No broad platform effects.** Only rootful Adreno KGSL GLAMOR and
   Mesa XCB Vulkan-window-marker (`_MESA_DRV`) clients are selected.
   Original S24/S26 Vulkan Wrapper, S30 tiny-AHB importer, global
   Termux-X11 `:0`, Android helper APK, XFCE input/IME, other GPU vendors
   and original private S16/S28/S29/S30/S31 binaries are unchanged.

## Measured S26U real-GPU results on *isolated* Xwayland

Tests run an authenticated throwaway Xwayland `:201..:240` and optional
disposable Xfwm4; only the tests' own demo windows are resized. The
motion probe reads center pixels from the known test-owned Vulkan XID;
the color probe independently detects red, green and blue GLX gears.
The readings are **not** actual Android display frames.

| 8s abrupt Xfwm4 resize (separate sample runs) | S31 | S32 |
| --- | ---: | ---: |
| `vkcube` center samples | 172 | 214 |
| `vkcube` samples retaining non-background cube content | 129 | **207** |
| `vkcube` blank/background-only samples | **43** | **7** |
| `glxgears` sampled frames | 216 | 215 |
| `glxgears` frames with colored gear pixels | 203 | **213** |
| `glxgears` frames lacking colored gear pixels | **13** | **2** |
| Vulkan/Zink client survives repeated resizes | Yes | **Yes** |

The different sample counts reflect timing/scheduling differences. Do
not interpret these as measured percentages of **physical** display
flicker. In final tracked S32 tests, `glxgears` still had two no-color
samples, and `vkcube` still had seven blank samples: **not a perfect
zero-flicker result**. S31 had fewer *black edge* samples in some tests
only because it erased the **whole** drawable, which is unacceptable.

Additional candidate checks:

- Gentle continuous drag: Vulkan cube content present in **214/214**
  sampled center frames; right/bottom-edge black samples **0**.
- Extreme 1×98, 8×98, 40×98 drag: Vulkan client remained alive through
  39 resizes. The tiny-frame sampler cannot inspect every small image.
- After restoring 500×500: 22 Vulkan samples, 21 content-hash changes;
  the cube animates again rather than remaining frozen.
- S32 content acceptance tests enforce a real Vulkan-during-resize
  presence threshold and independent red/green/blue gear presence, not
  merely `CLIENT_ALIVE=True`.

**Remaining work:** Some black-edge samples and brief image clipping
remain in isolated tests, and Xfwm4/WSI size updates can still arrive
asynchronously. This change is a **correction of S31's destructive
regression**, not proof that native gaming window resize is fully smooth.
Avoid promoting it as an `awstart` default until physical user testing.

## Build and test without restarting XFCE

```bash
cd ~/alroot_wayland
bash graphics/tools/build-s32-kgsl-nonblank-present.sh
bash graphics/tools/test-s32-build-and-scope.sh
python3 graphics/tools/test-s32-nonblank-resize.py --mode baseline --pattern abrupt --demo vkcube --wm --seconds 8
python3 graphics/tools/test-s32-nonblank-resize.py --mode candidate --pattern abrupt --demo vkcube --wm --seconds 8
python3 graphics/tools/test-s32-nonblank-resize.py --mode candidate --pattern abrupt --demo zink --wm --seconds 8
```

These commands start **only disposable private test servers** and
terminate those servers at completion. They do not stop the user's
actual Xwayland, XFCE, IME or Android helper. The S32 binary path is
`.source/gpu-stage1/s32-no-blank-present/runtime/Xwayland`; SHA256
`b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455`.
The regular private server builder detects this experimental source
overlay early and refuses to overwrite the accepted S16/S27 server.

## Optional physical S32 vs S30 test

**S31 is acknowledged as a regression.** First save all ongoing work.
Use a **separate native Termux shell**, never an XFCE terminal:

```bash
cd ~/alroot_wayland
bash graphics/tools/start-s32-kgsl-nonblank-present.sh --restart --dry-run
bash graphics/tools/start-s32-kgsl-nonblank-present.sh --restart
```

Once the **Alroot Wayland Android helper** XFCE session says READY,
open its XFCE terminal. Run one demo at a time, close the first before
the second:

```bash
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual zink
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual vulkan
```

On any regression, **restore the previous S30** from a native Termux
shell (intentionally restarts the project XFCE, does not reinstall
drivers or modify Termux-X11):

```bash
bash graphics/tools/start-s32-kgsl-nonblank-present.sh --restore-s30
```

The shell fallback still **does not prove Android `top-app` CPU
scheduling**. Keep the S30 user observations as a comparison baseline.
