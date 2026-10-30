# GPU-S23 — Xwayland Mesa 27 Zink/Turnip 3-stack validation

## Scope and protected baseline

- Host: Galaxy S26 Ultra (Adreno 840), Alroot Wayland Android host and **private**
  GPU Xwayland 24.1.14 (`GLAMOR=es`, dual Bionic/glibc xshmfence shim).
- Source: pinned `lfdevs/mesa-for-android-container` commit
  `6f90e92dfdacc4759d8e9ccb328805d0f0b4439b` (27.0.0-devel);
  packaging patches are documented in GPU-S1.4.
- Live XFCE, Android APK and original Termux-X11 `:0` are protected. Test
  sessions use isolated displays `:201..:240`, authenticated UNIX sockets,
  and per-client Vulkan and GLX overrides. **No default `awstart` promotion.**

## Distinguish pre-existing success from Mesa 27

The S17 original 9-case full matrix passes with the system GLX Mesa
26.3.0-devel. That is **not** evidence that the pinned Mesa 27 Zink runs.
The original private Mesa 27 S14 build provided Wayland EGL but explicitly
disabled `-Dglx`, so `LIBGL_DRIVERS_PATH` alone does not switch X11 GLX.

This continuation builds independent GLVND `libGLX_mesa.so`, matching
`libgallium-27.0.0-devel.so` and Turnip ICD without replacing global packages:

| Environment | Private runtime | GLX + Vulkan backend |
| --- | --- | --- |
| Termux Bionic | `.source/gpu-stage27-pr25/mesa-private/glx-runtime` | Mesa 27 Zink; wrapper or Mesa 27 Turnip |
| Debian 13 glibc PRoot | `.source/gpu-stage27-pr25/mesa-glibc27/private-runtime` | Mesa 27 Zink and Mesa 27 Turnip |

`graphics/tools/test-s23-mesa27-zink-matrix.sh` checks a real Mesa 27
version string, selected vendor/Turnip, server responsiveness, actual
wrapper AHB DRI3 import, correct dual ABI fences and cleanup. It does not
claim physical pixel correctness merely from `glxinfo` or a score.

## WSI glibc build defect and fix

An initial private glibc build used `-Dfreedreno-kmds=kgsl` alone.
Mesa's `meson.build` intentionally set `system_has_kms_drm=false`, removing
`HAVE_LIBDRM` and `wsi_common_drm.c`, **but** retained X11 DRI3
(`HAVE_X11_DRM`). X11 then submitted `WSI_IMAGE_TYPE_DRM` into
`wsi_common.c:get_blit_type()` without the corresponding case; the first
`glxinfo` aborted on `Invalid image type` at line 449. This is **not** an
Xwayland hang or validation of GPU failure.

For Debian with libdrm available, the proper Meson configuration is
`-Dfreedreno-kmds=msm,kgsl`, retaining KGSL support **and** building the
real DRM WSI path. Confirm the compiled WSI command line now contains both
`HAVE_LIBDRM` and `HAVE_X11_DRM`. This links libdrm and must not be
silently generalized to platforms without a compatible libdrm runtime.
Never suppress `UNREACHABLE` or silently force CPU-copy as an "optimization".

## Alroot LD_PRELOAD live Xauthority: separate unresolved gate

The Debian PRoot runtime authenticates to the running private XFCE `:94`.
The Alroot **native glibc** LD_PRELOAD runtime can read the same 55-byte
Xauthority file and manually query `FamilyLocal=localhost` successfully,
but ordinary `xcb_connect()` fails with `Authorization required`.
The *same process* invoking
`xcb_connect_to_display_with_auth_info()` using the existing in-memory
MIT-MAGIC-COOKIE record succeeds. Therefore the endpoint and credential
itself are valid; default authorization **discovery/forwarding** under
the native runtime remains a separate compatibility gap. `-ac` was used
only in an isolated diagnostic X server, **never** in the live XFCE profile.
Do not promote to the universal `awstart` default until this authentication
path is corrected without disabling X11 authorization.

## Acceptance gates

- Bionic wrapper + Zink 27: GLX, GLmark2 and Vulkan XCB, including AHB socket
  ACK/import, Xwayland responsive after each case.
- Bionic Turnip 27 + Zink 27: real Zink 27, Vulkan XCB, private ICD and GPU
  presentation, no system library overrides.
- Debian glibc Turnip 27 + Zink 27: **newly built** glibc GLX, Vulkan,
  correct DRM WSI and 4-byte futex ABI, no assertions.
- Native Alroot glibc: authenticated session (do not rely on `-ac`), own ICD,
  real GUI, stable window handling.
- Android live user checks: XFCE desktop, correct 2D pixel output, moving
  windows while GL/Vulkan scenes render, mouse hit-testing, PiP and resize,
  stability under repeated application launches.
- Later vendors (Xclipse/ANGLE, Mali, PowerVR) must not be implicitly marked
  PASS from Adreno 840-only KGSL tests; preserve generic fallbacks.

Performance scores from the short mobile one-scene GLmark runs are **not**
head-to-head thermal-controlled comparisons. CPU-fence removal and
zero-copy optimization remain separate from correctness acceptance.

## 2026-10-30 — resumed real XFCE4 live-window acceptance

The Android helper APK S22 was already installed, with rootful Xwayland
`:94` (PID 24814) and XFCE4 (PID 24951) on S26U. Both remained alive
through this resumed test. No app, driver, desktop, or global graphics package
was installed or replaced during acceptance.

`graphics/tools/test-s23-live-xfce-windowed.py` launches each scoped client
on **that authenticated running desktop**, not a disposable Xwayland. It
requires its new managed XID (this Termux GLX/vkcube does not publish
`WM_CLASS`), reads only that test window with XGetImage, and checks multiple
distinct pixel values, correct GPU identity, and server liveness before/after.
The Vulkan cube runs are intentionally terminated by the harness after the
window and pixel proof, whereas short GLmark2 runs exit normally.

| Client | Termux wrapper + Zink27 | Termux Turnip27 + Zink27 | Debian glibc Turnip27 + Zink27 |
| --- | --- | --- | --- |
| X11 `glxinfo -B` on `:94` | PASS | PASS | PASS |
| `glmark2` mapped 800×600, 5/5 pixel readback | PASS | PASS | PASS |
| `vkcube` mapped 500×500, 5/5 nonblank pixels | PASS | PASS | PASS |
| Xwayland after client exit | responsive | responsive | responsive |

The actual S26U Android host logged `LINEAR DMA-BUF → VkBuffer GPU-copy`
at its 1920×1080 desktop resolution. The current pipeline does **not**
claim AHB zero-copy or `VkImage` direct imported scanout. Physical Android
screen accuracy still needs the user's visual acceptance.

### Blocker found in window resize (do not promote default)

Reproducible with `--only-vulkan --resize-own-window` and with
`--only-gears --resize-own-window`. The test moves/resizes **only its own**
X11 demo XID, never an existing user window or the mouse. Geometry sequence:
900×650 → 1200×700 → 500×500.

| Resizable demo | Bionic installed Vulkan Wrapper | Bionic Mesa27 Turnip | Debian glibc Mesa27 Turnip |
| --- | --- | --- | --- |
| `vkcube`, Vulkan XCB | **FAIL: SIGSEGV (-11) at first resize** | PASS 3/3 | PASS 3/3 |
| `glxgears`, Zink GLX | **FAIL: `GLXBadCurrentWindow`, `X_GLXSwapBuffers`** | PASS 3/3 | PASS 3/3 |

The same Vulkan SIGSEGV also occurred with the project-private
`libvulkan_wrapper.so` ICD, not just the installed system Wrapper. Android's
crash tombstone shows `libvulkan_wrapper.so` → Qualcomm
`vulkan.adreno.so::vkCreateImageView()` (fault address 0x70), suggesting an
invalid/null image or related image-view lifecycle in the resize path. The
vendor is where the crash occurs, **not proven to be the sole root cause**.
`glxgears` also fails with Wrapper + system Mesa26.3, so the GLX resize issue
is **not introduced solely by Mesa27**. The nonresizing GLmark2 benchmark
can pass while the resizable GLX demo fails. `glmark2` fixes its own
800×600 window size and ignores requested X11 resizes, so it is NOT used
as proof of a resizable GLX workload.

Turnip with the same GLX and Vulkan demo windows completes all three resize
steps while returning actual varied X11 pixel values and retaining the
running XFCE4 PID. Debian Vulkan may yield an all-black **first frame**
during shader preparation; the test now waits at most two seconds for varied
pixel content. Later frames and all resize steps succeed.

### Other checks and release decision

- S20 atomic pointer, S21 PiP/viewport mapping, S22 XFCE cursor theme:
  static and JVM regression tests PASS; **real PiP/user cursor acceptance**
  is not asserted by those tests.
- Protected Termux graphics binary hashes: all match; `awstart` default and
  its existing XFCE session remain unchanged.
- Alroot native glibc runtime: authenticated `xcb_connect_to_fd()` fix was
  validated against the live XFCE by loading its **private trial preload**;
  the original runtime source/binary were restored, and portable patch is
  `graphics/patches/alroot-runtime/0001-xcb-authenticated-direct-connection.patch`.
  Alroot native `glxinfo` loaded **Mesa26.3**, not Mesa27, so do not claim
  complete native-Alroot Mesa27 acceptance from the Debian PRoot success.
  This source-specific patch is intended for original
  `alroot_guest_preload_src.c` SHA-256
  `73db4e131c0e9b85c42dff7f575ce3276d54c422a707c0caa30ddaa4023482af`;
  the successful private trial source hash was
  `8cb8a493609073c6140d5e069cd40d0527006be4e47ea6710b4cd3fe2b7d8fe3`.
  Reject an unknown baseline rather than forcing the patch onto unrelated
  versions of the Alroot runtime.
- **Decision: NO DEFAULT PROMOTION** yet. Both Zink and direct Vulkan
  Wrapper resize failures must be fixed or explicitly excluded by a safe,
  justified product capability policy. No zero-copy/performance claims should
  be used to waive correctness and session stability requirements.

Repro commands (native S26U Termux, user XFCE session left running):

```sh
cd ~/alroot_wayland
python3 graphics/tools/test-s23-live-xfce-windowed.py --route all
python3 graphics/tools/test-s23-live-xfce-windowed.py --route turnip --only-vulkan --resize-own-window
python3 graphics/tools/test-s23-live-xfce-windowed.py --route debian --only-gears --resize-own-window
# EXPECTED CURRENT FAILURES, run separately only for wrapper diagnosis:
python3 graphics/tools/test-s23-live-xfce-windowed.py --route wrapper --only-vulkan --resize-own-window
python3 graphics/tools/test-s23-live-xfce-windowed.py --route wrapper --only-gears --resize-own-window
bash graphics/tools/test-s22-xfce-startup-cursor.sh
```

Test logs are under `.source/gpu-stage27-pr25/s16-xwayland-gpu/zink-matrix/`
in `live-s23-acceptance/` and `revalidation-20261030/`. These are local
diagnostics, not a new system Mesa/ICD installation.
