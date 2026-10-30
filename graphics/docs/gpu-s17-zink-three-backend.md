# GPU-S1.7 — Xwayland GLAMOR Zink/Turnip/wrapper cross-libc compatibility

**Date:** 2026-10-30. **Device:** S26 Ultra, Adreno 840; **branch:** `feature/gpu-s16-live-regression-fixes` (starting from `14b2a9d`). **Status:** three isolated GLX/Zink `glmark2` backends PASS on device; **default `awstart` promotion NOT yet authorized**. The previously running XFCE4 `:92` is still hung and belongs to the old Xwayland process. No automatic restart was performed because a user `xfce4-terminal` remains alive with a shell.

The Stage-S1.6.3 private KGSL Xwayland already fixed right-half corruption by upgrading `GLAMOR_DEFAULT_PRECISION` from mediump to highp. This S1.7 stage adds two distinct opt-in DRI3 compatibility repairs. It does **not** modify Android's system Vulkan driver, Termux global Xwayland `:0`, the installed helper APK, Mesa/Zink packages or user XFCE settings.

## 1. Debian glibc → Bionic Xwayland DRI3 Xshmfence

Real failing trace before correction: `vkcube` and Debian Zink hang the private X11 server, with an Xorg FORTIFY abort: `pthread_mutex_lock called on a destroyed mutex` in `libxshmfence.so` (Xorg `xshmfence_trigger`). An isolated memfd/ABI test on S26U identified the exact cross-libc incompatibility:

| Process shared-fence producer | Shared buffer size | Representation |
| --- | ---: | --- |
| Termux Bionic libxshmfence | **96 bytes** | Bionic process-shared pthread synchronization |
| Debian 13 glibc libxshmfence | **4 bytes** | Linux shared futex word |

Prior Xwayland Bionic code mapped the 4-byte glibc FD as a pthread mutex, which is invalid. Source `graphics/compat/s16-xshmfence-dualabi.c` is an isolated, **Xwayland-only** interposer that recognizes the memfd size at `xshmfence_map_shm`. It delegates all 96-byte fences to the native Termux library; maps 4-byte fences with `mmap(MAP_SHARED)` and implements real atomic trigger, futex wait/wake, query and reset; unknown sizes **fail closed**. It does **not** forge an always-signaled fence and does **not** preload into glibc apps or replace the Termux libxshmfence. A separate `_Atomic` diagnostic guard prevents data races in its once-per-format logger.

`bash graphics/tools/build-s16-xshmfence-dualabi.sh` builds an Android arm64 private `.so` and runs a **real forked-waiter** test for both layouts:

```
S16_DUALABI_bionic96_NATIVE_STATE_MACHINE=PASS
S16_DUALABI_bionic96_CROSS_PROCESS_WAKE=PASS
S16_DUALABI_glibc4_NATIVE_STATE_MACHINE=PASS
S16_DUALABI_glibc4_CROSS_PROCESS_WAKE=PASS
```

The `termux/alroot-wayland` launcher accepts a deliberately opt-in `ALROOT_XWAYLAND_DUAL_ABI_FENCE=1` **only if** the selected executable is the exact Alroot private KGSL GLAMOR `es` binary. This runs it with a process-scoped `LD_PRELOAD` and does not touch the user's shell, distro, installed Android helper or other X11 servers. The `.so` may also be inherited by helper processes spawned by that test server; it must not be used as a system/global preload.

## 2. Termux vulkan-wrapper-android XCB WSI → KGSL Xwayland AHB DRI3 socket

The wrapper's special XCB WSI sets DRI3 modifier **1255** (`AHARDWAREBUFFER_SOCKET_FD`): it does **not** pass a real DMA-BUF FD. It sends an Android AHardwareBuffer over one end of an AF_UNIX socket, passes the other socket FD to Xorg `DRI3 PixmapFromBuffers`, then **blocks waiting for one-byte acknowledgment**. The original private KGSL Xwayland only accepted true LINEAR DMA-BUFs, and the wrapper stalled inside `vkCreateSwapchainKHR` on the first WSI image. Exact private wrapper WSI instrumentation showed:

```
CREATE_INSTANCE=0; SURFACE_SUPPORT=0; CREATE_DEVICE=0
CREATE_SWAPCHAIN: blocked
WSI_IMAGE_INIT_BEGIN=0
WSI_CREATE_IMAGE rc:0, AHardwareBuffer:yes, modifier:1255
AHB_SEND_HANDLE_RC=0
BEFORE_AHB_SOCKET_ACK_READ ... blocked
```

`graphics/patches/xwayland/kgsl/0004-kgsl-ahb-socket-dri3-24.1.14.patch` adds a **private KGSL-only, modifier-1255** branch to `glamor_pixmap_from_fds`: validate the socket, emit the wrapper ACK, receive AHardwareBuffer through Android nativewindow, check its descriptor (S26U vendor BGRA8 format 5, dimensions and row pitch), derive the **actual gralloc DMA-BUF FD** from its native handle, and import that FD using the existing KGSL `EGL_LINUX_DMA_BUF_EXT` pixmap route. This is not a universal AHB-to-DMA-BUF contract; other devices/formats need specific validation. The ordinary DRM LINEAR paths and generic Xwayland are unchanged.

On S26U native buffer import was checked with a real GPU EGL diagnostic: 320×240 BGRA format 5, row stride 320, two native FDs (one matching pixel buffer size 307200 bytes, one metadata FD); `EGL_LINUX_DMA_BUF_EXT` image import/GL texture binding succeeded. With the new private Xorg patch:

```
S16 AHB DRI3 socket->native DMA-BUF->KGSL EGL import PASS
vkCreateSwapchainKHR = VK_SUCCESS
```

After that, the wrapper Vulkan `vkcube --wsi xcb --c 10`, Zink `glxinfo -B` and windowed `glmark2` all exited cleanly with the private Xwayland remaining responsive. Neither global wrapper ICD nor global X server was modified.

## 3. Three Zink routes — integrated S26U acceptance

`graphics/tools/test-s16-zink-isolated.py` creates one private X11 display per test (`:201..:240`) with `-nolisten tcp`, a temporary Xauthority cookie and the **private** KGSL Xwayland only. The ABI shim is preloaded into that server alone. Termux clients use scoped Vulkan ICD JSON and Mesa environment; Debian glibc clients receive the explicit Turnip/Zink environment **inside** the distro command. A negative control without the shim reproduced unresponsive Xorg; with the shim the client and server remained healthy. `graphics/tools/test-s17-zink-matrix.sh` provides the reproducible smoke suite and strict outcome/ABI/AHB import assertions. The default smoke runs just three lightweight GLmark scenes to avoid unnecessary mobile heat.

| Route | Renderer | Windowed GLmark scene | Exit | Xorg after |
| --- | --- | --- | --- | --- |
| **Termux wrapper + Zink** | zink Vulkan 1.4 / Qualcomm proprietary Adreno 840 | **2,908** (latest integrated run) | **0** | responsive |
| **Termux Turnip + Zink** | zink Vulkan 1.4 / MESA_TURNIP Adreno 840 | **795** | **0** | responsive |
| **Debian glibc Turnip + Zink** | zink Vulkan 1.4 / MESA_TURNIP Adreno 840 | **436** | **0** | responsive |

Benchmarks are **one scene, non-comparable thermal/runtime settings**; do not report these as on-screen Android FPS or a strict driver performance comparison. Real shader execution and X11 windowed GLX are confirmed. Each test also validates a real shared-fence ABI, correct AHB import on the wrapper route, X11 response **during and after** and cleanup of the temporary X11 socket.

```
cd ~/alroot_wayland
bash graphics/tools/build-s16-xshmfence-dualabi.sh
S17_MATRIX_MODE=smoke bash graphics/tools/test-s17-zink-matrix.sh
# Optional slower extended matrix of GLX/Vulkan scenarios:
S17_MATRIX_MODE=full bash graphics/tools/test-s17-zink-matrix.sh
```

The actual Vulkan XCB swapchain was also tested: **Termux wrapper** and **Debian Turnip** `vkcube` completed with return code 0 using the two compatibility repairs. This is Vulkan WSI client success; neither the physical Android display's final pixels nor full XFCE stability is established merely from these command-line results.

## 4. Live hung XFCE :92 and safe recovery

The pre-fix private `Xwayland :92` remains blocked in `futex_wait`; authenticated `xdpyinfo` times out. The X server cannot be hot-patched by recompiling its on-disk executable; an **Alroot-only session restart is required**. The preflight also found an existing `xfce4-terminal` (PID 17242) with a bash child; restarting before the user saves their work would close that interactive shell. As a result, **we deliberately did not kill or restart it**.

The read-only diagnostic:

```
bash graphics/tools/check-s17-desktop-recovery.sh
```

prints `S17_RECOVERY_X11_SERVER=UNRESPONSIVE`, any known open editor/terminal processes, and that a manual restart is required. It **never restarts or kills anything**.

After personally saving/accepting the loss of any ongoing desktop terminal work, execute in a **separate Termux native shell**:

```
cd ~/alroot_wayland
ALROOT_XWAYLAND_BINARY="$PWD/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland" \
ALROOT_XWAYLAND_GLAMOR=es \
ALROOT_XWAYLAND_DUAL_ABI_FENCE=1 \
XWAYLAND_FORCE_KGSL_SURFACELESS=1 \
FD_KGSL_ENABLE_DMABUF=1 \
MESA_LOADER_DRIVER_OVERRIDE=kgsl \
GALLIUM_DRIVER=freedreno \
awstart --restart --direct --no-topapp
```

No APK reinstallation is required to enable these two Xwayland-only compatibility additions. This remains **opt-in on Adreno 840**. Afterwards, check `awstart --status`, the actual X11 health diagnostic and the `~/.alroot-wayland/logs/xwayland.log` Bionic/glibc shim indicators. Compare mouse clicks, long-duration XFCE, Blender 2D glyphs, partial vs full pixmap updates, `vkcube` visible window content, stable 60/120 Hz and multi-app behavior before declaring a production default.

### Default `awstart` promotion decision

**Do not silently make KGSL the universal default.** The three driver **execution** paths now PASS, but the user's actual XFCE session has not yet been restarted/observed with both repairs, cursor-drift confirmation is still pending, the private glibc Xauth handling needs production verification, and Xclipse/ANGLE/Mali/PowerVR have distinct GPU export contracts. Once S26U's whole desktop usage passes, it is reasonable to add a capability-gated `awstart` default **for validated KGSL devices**, retaining the current generic fallback and preserving the Xclipse path separately. Further zero-copy performance/fence optimization is distinct from correctness.
