# GPU-S1.7 — Android black-screen after `awstart --restart`: startup lock leaked into children

**S26U, 2026-10-30;** `feature/gpu-s16-live-regression-fixes`. This fix is **Termux launcher-only**, requires **no APK update**, and does **not** change GLAMOR/Zink/Turnip, Vulkan swapchains, DRI3 AHB or the installed global Termux-X11.

## Real black-screen reproduction and evidence

After the previous live `Xwayland :92` hung while evaluating glibc 4-byte Xshmfence vs Bionic 96-byte pthread fences, the user ran `awstart --restart --direct --no-topapp`. The Android Wayland host remained READY but the helper displayed a **black screen**. On-device the project status showed `xwayland: DOWN`, `xfce: DOWN`, and `ime: DOWN` even as the old orphaned `Xwayland :92` process (PID 12829) continued to exist, blocked in `futex_wait`. The old X11 socket and lock file were not removed by the previous stop. The new start returned:

```
[awstart] Shell-spawn mode does NOT ensure top-app CPU scheduling.
another Alroot startup is active
```

`fuser ~/.alroot-wayland/start.lock` and `/proc/PID/fd/8` proved that **three orphaned long-lived children** inherited the script's startup `flock`: old private Xwayland (12829), old native IME server (12880), and clipboard `xclip` (12976). The original `termux/alroot-wayland` script opened `exec 8>"$STATE/start.lock"` during startup and spawned long-lived tasks without `8>&-`. The Bash parent subsequently exited/was stopped, **but the shared kernel open-file-description flock survived in inherited file descriptor 8**. An unrelated new launcher could not acquire the lock, so the Android helper had nothing to display. Merely recompiling Xwayland or installing an APK could not fix this.

## Implemented fix

`termux/alroot-wayland`: all *long-lived child launch commands* (Xwayland both with/without dual-ABI shim, Labwc, XFCE, clipboard broker, IME server) explicitly close the lock descriptor on spawn with `8>&-`. The parent explicitly `flock -u 8` / closes fd 8 on successful completion; lock contention refuses with the fd closed. The descriptor is now **startup-only**, not a lease that persists throughout the desktop's lifetime. The Xwayland process may become unresponsive later without blocking another startup through this inherited lock.

`termux/awstart`: `--restart` is rejected when invoked **from inside the very same Alroot XFCE X11 desktop** (`XDG_CONFIG_HOME=$STATE/xfce-config`, matching active DISPLAY). Otherwise, `--stop` may terminate the calling graphical terminal/session before reaching the new `--start`, also leaving a black helper screen. Independent native Termux shells remain supported.

`graphics/tools/test-s17-start-lock-lifetime.sh` is a **read-only, on-device** regression. During a LIVE private Xwayland/XFCE session it requires all of the following:

```
S17_START_LOCK_AVAILABLE_WHILE_XFCE_RUNNING=PASS
S17_START_LOCK_BACKGROUND_FD_CLEAN[xwayland]=PASS
S17_START_LOCK_BACKGROUND_FD_CLEAN[xfce]=PASS
S17_START_LOCK_BACKGROUND_FD_CLEAN[clipboard]=PASS
S17_START_LOCK_BACKGROUND_FD_CLEAN[ime]=PASS
S17_START_LOCK_ALL_BACKGROUND_SPAWNS_CLOSE_FD8=PASS
S17_START_LOCK_SELF_RESTART_GUARD=PASS
S17_START_LOCK_LIFETIME=PASS
```

## Device recovery performed

The hung old `:92` session's PID markers were already removed. The session's original XFCE terminal (with a bash child) was **no longer running** when recovery was attempted. A scoped cleanup verified each orphan process's exact executable, PID and inherited `start.lock` inode before sending TERM and, where needed, KILL. The cleanup touched **only** the three verified orphaned Alroot processes (12829, 12880, 12976); it did not use a global `pkill`, did not touch `:0` or the Android host, and removed only the obsolete `:92` X11 lock/socket belonging to the terminated PID.

Before cleanup, a fresh `awstart --direct --no-topapp` was independently tested and returned `another Alroot startup is active`. After cleanup:

```
S17_RECOVERY_START_LOCK_AVAILABLE=PASS
[Alroot GPU] PRIVATE Xwayland ... (GLAMOR=es)
[Alroot GPU] PRIVATE Xwayland xshmfence Bionic96/glibc4 compatibility enabled
Xwayland :92 ready at 1920x1080 96DPI
Alroot xfce-shm-direct started on :92
xwayland: RUNNING 28815
xfce: RUNNING 28908
ime: RUNNING 28857
x11_desktop: READY
S17_START_LOCK_RELEASE_AFTER_START=PASS
```

Android `AlrootWayland` log for the new session showed Xwayland's **1920×1080 LINEAR DMA-BUF → VkBuffer→VkImage GPU-copy**, an open and presenting Xwayland window, new on-screen frames, and a non-black `PixelProbe SurfaceView` sample (about 10,962 non-black pixels out of 14,400). The fresh X11 `xdpyinfo` returned exit code **0**, with GLX/DRI3/Present advertised; `/proc/<new Xwayland PID>/maps` confirmed the private dual-ABI shim was loaded. The controller's lock is obtainable while new XFCE/IME/clipboard run. **These checks establish that the former empty-black session is no longer missing its desktop; visible UX correctness, input gestures and ongoing Zink stability still require user observation.**

## Safe subsequent use

Run `awstart --status` and `bash graphics/tools/test-s17-start-lock-lifetime.sh` from a separate native Termux shell. Restarting from inside XFCE itself is explicitly disallowed; use an independent native Termux shell after saving application work. The S1.7 KGSL/dual-ABI backend is still opt-in; the universal `awstart` default has **not** been switched. Never remove active X server locks or kill unrelated Xwayland/Termux-X11 sessions to work around a blocked startup.
