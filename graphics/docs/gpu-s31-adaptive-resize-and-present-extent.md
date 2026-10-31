# GPU-S31 — adaptive resize continuity and stale Present extent rejection

**Date:** 2026-10-31 · **Device:** Galaxy S26 Ultra, Adreno 840.
**Scope:** project-private KGSL/GLAMOR Xwayland, XFCE4/Xfwm4, S26 private
Vulkan Wrapper + Mesa27 Zink. **Status:** isolated source, pixel and
live-render tests PASS; **physical Android resize quality still requires
the user's manual acceptance**. S31 is not the `awstart` default.

## Source: two actual S30 user screen recordings

Reviewed the supplied files:

- `Screen_Recording_20261031_131808_Alroot Wayland.mp4`, **glxgears**, ~37.6s.
  The blue gear can be clipped at the upper border while the red gear
  briefly appears at an enormous scale after the window shrinks. Gear
  positions then bounce back when the GLX/Vulkan client submits a new
  buffer corresponding to the updated window extent.
- `Screen_Recording_20261031_132003_Alroot Wayland.mp4`, **vkcube**, ~25.3s.
  On repeated resizing, the image can look temporarily frozen and a
  thin horizontal black strip appears at the previous buffer boundary
  inside the resized gray client area. The cube itself survives and
  becomes correctly aligned again after the client renders a matching
  new frame.

S30's last-frame 1:1 GPU snapshot preserved previous pixels but could
not guarantee that a **DRI3 Present pixmap** already matches the latest
Xfwm4 window geometry. These symptoms are not an argument to restore S28
bilinear scaling (which independently caused blur and distortion).

## Server-instrumented root cause

On the actual S26U in an authenticated *isolated* rootful Xwayland +
Xfwm4 test, instrumented real `vkcube` and `glxgears` Present copies:

```text
S31_PRESENT_EXTENT win=0x400000 window=900x720 source=350x252 stale=1
S31_PRESENT_EXTENT win=0x600002 window=290x290 source=1000x700 stale=1
```

The **X11 window's current size** and the **client's last submitted
pixmap extent** can diverge dramatically during rapid mouse dragging.
Xorg Present's normal copy then clips an old giant gear when the window
shrinks, or leaves black/stale right/bottom strips when the window grows.
The original S30 centered-pixel placeholder could also show incorrectly
located old gear/cube content until the new image arrived.

## S31 opt-in response, no renderer change

Patch `graphics/patches/xwayland/kgsl/0008-kgsl-adaptive-resize-present-extent.patch`
is applied **after** S30's 0007 in only the private KGSL Xwayland source.

1. **Keep all S30 protections.** Tiny AHB two-FD selection, S26 Wrapper
   transient-import recovery, no writes into client-owned DRI3 images,
   and snapshot invalidation after a successful real Present are unchanged.
2. **Bounded snapshot policy.** The existing GPU-only last-good-frame
   copy is shown at native 1:1 pixels only when the old snapshot width
   and height are each within a bounded ~10% of the updated window size
   (minimum tolerance 12px). Otherwise it fills using one GPU-sampled
   last-known background pixel instead of placing a huge clipped gear or
   a frozen, incorrectly sized cube on the new window. It does not
   bilinearly scale, repeat old image tiles or read pixels to the CPU.
3. **Real Present geometry check.** When Xorg's Present `copy` executes
   with a client pixmap smaller or larger than the new window by over the
   threshold, the server *after copying* fills the **Xorg-owned client
   window** with a repeatable background color, without modifying the
   client DRI3 source. A correctly sized new client Present replaces the
   placeholder normally. Small mismatches leave the genuinely rendered
   area intact and fill **only the exposed right/bottom strips**.
4. **Guarded applicability.** The physical S31 binary and
   `S31_KGSL_RESIZE_CONTENT_POLICY=3` must be selected explicitly,
   alongside `S30_KGSL_TINY_AHB_FD0=1`. Only rootful KGSL surfaceless
   GLAMOR and Mesa Vulkan XCB-marked (`_MESA_DRV`) windows receive this
   policy. No generic system Termux-X11, Android APK, other-vendor
   driver, desktop root window or XFCE panel changes. The private source
   still offers policy 1/2 for isolated diagnostic comparisons, but the
   **user launcher accepts only policy 3**.

The background-only choice for dramatic mismatches is intentional:
when a GPU client is recreating its swapchain and not yet submitting
matching frames, showing the background briefly is less geometrically
misleading than resizing an old cube or cutting a large gear. This is
**not a zero-latency or no-flicker guarantee**, and the eventual smooth
solution may require coalescing client/Wm size changes and improving
Vulkan WSI Present timing in a later phase.

## On-device isolated tests and remaining tradeoff

All tests used the actual Adreno 840 and S26 private Wrapper/Mesa27 Zink,
but on **disposable authenticated Xwayland :201** with separately spawned
Xfwm4 and test-owned X11 demo windows. The normal user XFCE PID and
Termux-X11 :0 were not touched.

| Test | S30 baseline | S31 private candidate |
| --- | --- | --- |
| Gradual Vulkan window drag (~45 resizes), client alive | PASS | **PASS** |
| Gradual drag, visible center scene (~215 samples) | 213/213 | **215/215** |
| Gradual drag, right/bottom-edge black samples | at least one each in matched trial | **0** |
| Abrupt aspect-ratio changes, Vulkan (~39 resizes) | survived | **survived** |
| Abrupt changes, old-image/background-only samples | fewer (retains stale content) | **17/191**, max one consecutive 40ms sample |
| Abrupt changes, right/bottom-edge black samples | observed previously | **0** |
| Abrupt Mesa27 Zink `glxgears` (~39 resizes) | survived | **survived** |
| Extreme Vulkan drag (width **1px, 8px, 40px**; ~39 resizes) | S30 stabilized crash | **survived** |
| Vulkan after return to **500×500** | animation had resumed | **22 samples; 21 differing hashes; 22 showing cube pixels** |
| User XFCE/IME/clipboard and system GPU | running | **unchanged** |

Sampling is not synchronized to the Android display and cannot confirm
what the *physical panel* showed. Very narrow frames cannot be sampled
by the current motion detector, so extreme-size edge quality is **not
proven perfect**. Abrupt large resizes can briefly show a blank client
background; this is a known tradeoff vs the S30 frozen/cropped content.

## Build, preflight and isolated validation (no XFCE restart)

```bash
cd ~/alroot_wayland
bash graphics/tools/build-s31-kgsl-resize-continuity.sh
bash graphics/tools/test-s31-build-and-scope.sh
python3 graphics/tools/test-s31-resize-continuity.py --pattern gentle --demo vkcube --wm --seconds 8
python3 graphics/tools/test-s31-resize-continuity.py --pattern abrupt --demo vkcube --wm --seconds 8
python3 graphics/tools/test-s31-resize-continuity.py --pattern abrupt --demo zink --wm --seconds 8
```

**Note:** the isolated test program's Vulkan demo selector is
`--demo vkcube`. The existing *user-visible S26 launcher* instead uses
`--visual vulkan`; these interfaces intentionally remain separate.

The pinned S31 executable is
`.source/gpu-stage1/s31-resize-continuity/runtime/Xwayland`, SHA256
`73acf8472faf1e5b91b5f4e998c32ed3322d30fc68e7b0683eef62b6a9c1fe90`.
Original private S16/S27, S28, S29, S30 binaries, S24/S26 Wrapper,
system Mesa/Vulkan libraries, Android APK and desktop config remain intact.
The ordinary private Xwayland builder blocks S31 source overlays **before
applying any old KGSL patches**. The S26 visual runner admits the S31
binary only by exact executable/PID/UID/DISPLAY/Xauthority/SHA256 checks.

## Physical XFCE visual A/B — intentional user restart only

Save open XFCE work. From a **separate native Termux shell**, never a
terminal within the XFCE session about to be restarted:

```bash
cd ~/alroot_wayland
bash graphics/tools/start-s31-kgsl-resize-continuity.sh --restart --dry-run
bash graphics/tools/start-s31-kgsl-resize-continuity.sh --restart
```

When the Alroot Wayland **Android helper** desktop returns READY, open
its new XFCE terminal and run the two demos **one at a time**. Close the
first demo (or press `Ctrl+C`) before invoking the next:

```bash
cd ~/alroot_wayland
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual zink
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual vulkan
```

The tests use the unchanged S26 Vulkan Wrapper and its DRI3 retry; they
target the running authenticated private Xfwm4 desktop and never use
Termux-X11 `:0`. **Do not call `awstart --stop` separately.**

To **rollback to the previously working S30** from the native Termux
shell (this intentionally restarts only Alroot's XFCE session):

```bash
bash graphics/tools/start-s31-kgsl-resize-continuity.sh --restore-s30
```

S31 remains **opt-in and not promoted** until user inspection confirms
the blue gear no longer clips/bounces and the `vkcube` horizontal black
strip no longer persists during real continuous drags. Adreno-specific
logic must not be enabled for Mali/Xclipse or the future generalized
Alroot Box backend.
