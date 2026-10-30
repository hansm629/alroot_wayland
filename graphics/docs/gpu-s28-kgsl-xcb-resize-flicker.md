# GPU-S28 — KGSL Xwayland Vulkan-window resize flicker reduction

**Device:** Galaxy S26 Ultra, Adreno 840, native Termux + Alroot Wayland
Android helper, rootful private KGSL/GLAMOR Xwayland and XFCE4.

**Status:** source-level GPU-pixmap preservation implemented; separate
private binary built; deterministic isolated X11 pixel A/B shows strong
reduction. **Not a verified complete flicker fix for the real user XFCE
screen.** Production `awstart` and all global/system drivers remain intact.

## User acceptance from GPU-S26

The user manually resized the S26 Wrapper + Mesa27 Zink `glxgears` window
and reported **no further client termination**. A new problem remains:
**the demo flickers while the window is resizing**. S26's `GLXBadCurrentWindow`
transient import retry and S24's correctly aligned dedicated AHB remain
unchanged; this phase is about image continuity, not adding another retry
or changing FPS/Present scheduling.

## What the GPU-side tests actually demonstrate

The client X11 window was sampled, never an unrelated user window and
never the physical Android screen. `vkcube` normally fills its corners
with the stable `0x333333` background.

On the then-live private XFCE `:96`, the S26 native Vulkan Wrapper path
showed **0 anomalies in 527 stationary samples**, but **112 anomalous
samples out of 2,169** during 120 controlled resizes; **95** of those
had one or more fully black corners. An independent Turnip + `vkcube`
path on the *same* Xwayland had **0 anomalies in 529 stationary samples**,
but **97 anomalous samples out of 2,188** during resizing. Therefore the
flicker is observable in the X11 drawable and shared across these Vulkan
drivers; blaming only the Wrapper's AHB GPU-copy is unsupported.

When the X server resizes an X11 window, a new backing pixmap can be
exposed before Vulkan Zink or `vkcube` finishes its new swapchain image.
The newly exposed pixels can be black. With xfwm4, additional redirected
window/frame pixmaps are reallocated through the Composite extension.
This is the relevant shared Xwayland preservation problem.

## Isolated S28 patch — exact scope

`graphics/patches/xwayland/kgsl/0005-kgsl-gpu-window-resize-frame-preserve.patch`
modifies only the **private KGSL Xwayland** `hw/xwayland/xwayland-window.c`
file. It applies after the prior 0001–0004 KGSL patches.

- Disabled by default. Requires **`S28_KGSL_RESIZE_PRESERVE=1`**, rootful
  session, KGSL surfaceless GLAMOR and a Vulkan XCB window publishing the
  standard Mesa `_MESA_DRV` WSI marker. Explicitly excludes desktop root,
  generic GTK/Qt windows, Xfwm4 decorations, rootless Xwayland and vendors
  other than validated Adreno KGSL.
- Before a bounded X11 window resize, copies the preceding visible window
  into a temporary server pixmap using Xorg's GPU-accelerated GLAMOR
  `CopyArea` path. After the normal server resize, scales that snapshot to
  the new dimensions through **XRender `PictOpSrc`** with bilinear filter.
- Where the XComposite path changes the *shared backing pixmap before*
  `ResizeWindow`, it also preserves old pixel contents to the new pixmap
  through the same Render operation. This is gated to the Vulkan XCB window.
- If XRender pictures cannot be set up, the original-pixmap GPU `CopyArea`
  fallback fills the target with bounded old-frame tiles. The temporary
  pixmap and pictures are released. The source adds no `XGetImage`,
  `glReadPixels` or explicit CPU pixel memcpy. A GLAMOR fallback to a CPU
  backend was not exhaustively excluded with instrumentation, so do not
  claim end-to-end zero-copy.
- Frame reuse is a **temporary visual placeholder**, never an alternative
  to receiving the next freshly rendered Vulkan Present. It may stretch
  the previous cube/gear image briefly during resize; judge the tradeoff
  on the Android display rather than from FPS alone.

## Real on-device, isolated A/B evidence

Both binaries were tested on the S26U's actual Adreno GPU, but in
**disposable, authenticated rootful Xwayland `:201` servers** on the
existing Android host. The test servers close and remove their own socket
and lock; the real XFCE Xwayland and session never restarted.

| Same class of resize test | Previous private Xwayland | S28 candidate |
| --- | ---: | ---: |
| Without xfwm4, 6 seconds | 34 resizes, 27 black events / 840 samples | 34 resizes, **0** black / 836 samples |
| With isolated xfwm4, 7 seconds | 39 resizes, 21 black / 630 samples | 39 resizes, **11** black / 623 samples |
| Mesa27 Zink `glxgears` under isolated xfwm4 | not a pixel acceptance | 34 resizes, client stayed alive |

These are X11 `XGetImage` *sampling events*, **not displayed Android
flicker counts** or a controlled frame-by-frame capture. Sample counts
vary slightly with scheduling. The WM test still has some black frames,
so **the full xfwm4 flicker bug is not yet proven resolved**. The isolated
xfwm4 instance also reported that another compositing manager was already
running; it does not certify behavior identical to the user's XFCE config.

## Reproducible build and test

The original S16/S27 KGSL private Xwayland binary remains at
`.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland` with pinned
SHA256 `f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701`.
The new S28 Xwayland is **separately linked** at
`.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland`. The builder
recompiles only the modified window source into a new object and links it
against the pinned existing Meson object files; it never overwrites the
running server or system Xwayland. The wrapper S24/S26 ICDs also remain
unchanged. The normal private Xwayland build explicitly excludes the
S28 patch and refuses to overwrite the original binary while the
experimental private source overlay is active.

From a native Termux shell:

```bash
cd ~/alroot_wayland
bash graphics/tools/build-s28-kgsl-resize-preserve.sh
bash graphics/tools/test-s28-build-and-scope.sh
python3 graphics/tools/test-s28-s26-visual-session-guard.py
python3 graphics/tools/test-s28-kgsl-resize-preserve.py --demo vkcube --seconds 10
python3 graphics/tools/test-s28-kgsl-resize-preserve.py --demo vkcube --wm --seconds 10
python3 graphics/tools/test-s28-kgsl-resize-preserve.py --mode candidate --demo zink --wm --seconds 6
```

The tests use only `:201..:240` and leave the live XFCE and `:0` alone.

## Optional live XFCE visual A/B — intentional restart only

With the current XFCE running, **don't run `awstart --stop` just to run a
GPU demo**. To try the private S28 GPU-preservation server in the existing
XFCE profile, one **intentional** restart is needed from an independent
native Termux shell (not from the XFCE terminal):

```bash
cd ~/alroot_wayland
bash graphics/tools/start-s28-kgsl-resize-preserve.sh --restart --dry-run
bash graphics/tools/start-s28-kgsl-resize-preserve.sh --restart
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual zink
```

Open the **Alroot Wayland helper Android app**, not Termux-X11 `:0`.
The S28 candidate is now recognized by the S24/S26 persistent visual
runner, the S26 fault-injection runner and the S23 live X11 matrix via
an **exact executable allowlist** (running PID, authenticated DISPLAY,
actual /proc executable, native Termux UID and pinned binary SHA256).
Previously these scripts hard-coded only the older `xwayland-kgsl`
path and misleadingly rejected the correct S28 process as an
untrusted Termux-X11 server. The fix only extends the approved
private-binary allowlist; it does not weaken the system-X11 exclusion.
**No second XFCE restart is required** after this guard fix.
Drag a Zink `glxgears` window repeatedly through the old failure window,
looking for black flashes, stretched previous frames, painting corruption,
client errors and general Xfwm4/desktop regressions. Then repeat with
`--visual vulkan` to test `vkcube` directly. Current phase does not make
the candidate a default renderer and does not install any APK.

To rollback the *session-selected Xwayland* to the previous private KGSL
binary, again from an independent native Termux shell:

```bash
bash graphics/tools/start-s28-kgsl-resize-preserve.sh --restore
```

Both commands intentionally stop/restart the project XFCE process group;
the S27 start/stop lock and owned PID verification apply. They do not
modify global graphics packages, the original Xwayland executable,
or the GPU driver. The shell-based launcher does not claim Android
`top-app` CPU scheduling; One UI may restrict Xwayland to CPU 0–5.

**Remaining:** user visual acceptance, rootful xfwm4 residual black
samples, verifying there are no stale/duplicated frames, and later FPS,
GPU-copy, fence and Present performance optimization. Do not promote
S28 to `awstart` default before those gates pass.
