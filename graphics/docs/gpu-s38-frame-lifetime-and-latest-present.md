# GPU-S38 — Latest Present continuity and root-buffer lifetime

**2026-10-31 · Galaxy S26 Ultra / Adreno 840 · private Bionic Xwayland · S36 root AHB output + S37 thin-height import fix.**

**Status:** S38 is built and tested on the S26U as a separate, SHA256-pinned candidate. Two concrete defects were reproduced: an older resize snapshot replaced a newer real Present, and frame recovery lost a buffer-release callback, retaining root AHB resources. S38 fixes both and replaces an iteration-count watchdog with elapsed time. A final source-order review also moved snapshot/source reads before the existing Present release synchronization. Physical Android smoothness is still unaccepted; brief blank/edge samples remain in extreme-resize readback.

The user's live desktop stayed on S36 throughout this work (Xwayland PID 30653, XFCE PID 30762, DISPLAY :97). No automatic session switch, APK installation, global Mesa/Vulkan change or rootfs package/configuration change was performed. This report records that observation at verification time; the explicit launch below changes the project desktop.

## What the recordings actually showed

The earlier recordings mainly resize the lower-right corner while keeping the upper-left corner fixed. The gear fragments occur inside the client window, and the cube keeps an old orientation as its dimensions change. The old S35 test looked for colors left at vacated desktop coordinates, so its passing result did not answer this internal-content failure. Geometry-dependent image hashes also cannot prove animation: recropping a frozen frame changes its hash.

The Vulkan video additionally shows S36 failing AHB DRI3 imports at logical 267×19 and 826×8, followed by exit -11. That is the separate S37 narrow-height FD-tie fix, already committed before this continuation. S38 includes S37; it does not claim that the crash had the same cause as the stale-frame rollback.

## 1. A newer real Present was replaced by an older snapshot

S32 retained a resize snapshot until a client's Present extent matched the current window. During continuous resizing, a client can legitimately submit a newer frame at the previous size. The copy displayed that newer content, but the mismatched-size branch kept the first pre-drag snapshot. The next ResizeWindow replayed the old snapshot over the new content.

`graphics/tests/s38-present-freshness.c` makes the frame generation unambiguous:

1. Present a 400×400 solid blue pixmap, RGB `2040e0`.
2. Resize the window to 500×500.
3. Present a new 420×420 solid green pixmap, RGB `20e040`, and wait for its Present completion.
4. Resize to 560×520 and read the same interior pixel.

| Server | Initial | Newer Present | After next resize | Result |
|---|---|---|---|---|
| Pinned S37 | `2040e0` | `20e040` | `2040e0` | Older frame resurrected |
| Final S38 | `2040e0` | `20e040` | `20e040` | Latest generation retained |

S38 replaces the obsolete snapshot with an independent GPU copy of the actual Present pixmap, even when its extent lags the window. This is restricted to the existing tagged KGSL application path. It neither retains a client-owned pixmap as the cache nor writes a placeholder into that pixmap. A failed snapshot allocation invalidates the obsolete cache instead of reviving an older generation. A matching-size Present still drops the cache.

### Source lifetime: snapshot before client release

The existing call site ran `present_execute_copy()` before the S31/S32 post-copy hooks. That generic function already flushes or exports the release fence and marks the client's pixmap idle. Adding a source-reading snapshot after its return would therefore permit a client-reuse race.

Patch 0014 adds `present_execute_copy_with_callback()`. Its callback runs after the actual region copy and before **either** flush/release-syncobj or flush/idle-fence processing. The S38 callback performs uncovered-strip handling and latest-frame capture there. A requeued copy does not run the callback. The original generic entry point remains a null-callback wrapper, and non-S38 Xwayland keeps its historical path.

The snapshot is therefore included in the existing Present submission/release ordering. No new unconditional CPU wait or `glFinish()` is added. This preserves the original synchronization mechanism; it is not a claim that every external ICD/fence combination has been audited. **Apply 0013 and 0014 together. The preliminary after-idle S38 binary is not the launch target.**

## 2. Frame recovery dropped the newly installed release callback

`xwl_window_attach_buffer()` calls `xwl_window_swap_pixmap(..., TRUE)`, which adds a pending reference and installs the release callback. On forced buffer refresh, `xwl_glamor_kgsl_refresh_wl_buffer()` then deleted that same callback before replacing the Wayland buffer proxy. The later compositor release could no longer drop the pending reference. Root AHBs and their DMA-BUF descriptors accumulated.

S38 preserves the just-armed pixmap release callback while replacing only the protocol object. It does not synthesize an early release or treat a timeout as GPU completion.

The dedicated stress test runs three 1.5-second resize bursts, each followed by two seconds idle, against an authenticated disposable Xwayland and its own Xfwm4/demo. `--storm` adds 128 synchronous X11 round trips per resize step to expose the recovery path; it is deliberate diagnostic event pressure, not a normal-user-speed or FPS benchmark.

| Variant / demo | DMA-BUF FDs after idle 1 | After idle 2 | After idle 3 |
|---|---:|---:|---:|
| S37 baseline / vkcube | 343 | 618 | 908 |
| Release-callback-only diagnostic, old 64-loop watchdog / vkcube | 50 | 50 | 50 |
| **Final S38 / vkcube** | **44** | **39** | **44** |
| **Final S38 / Zink glxgears** | **45** | **45** | **44** |

The release-only diagnostic separates the lifetime fix from the watchdog change: retaining the callback removes the observed accumulation even when the old timeout remains. The final harness also checks total FD growth, client/server survival, server errors and the live user's unchanged session. These are short bounded regressions, not a long-duration leak certification. FD counts are resource indicators, not exact GPU physical-memory accounting.

## 3. Input volume is not a frame timeout

The old watchdog triggered after 64 Xorg event-loop visits while a Wayland frame callback was pending. Resize/input pressure could reach that threshold before an ordinary display frame had elapsed. S38 records the first blocked monotonic timestamp and only recovers after 100ms.

The existing field is reused without changing structure layout; unsigned CARD32 subtraction handles timer wrap, and callback/repost paths reset the field. Non-S38 retains the original behavior. This reduces false recovery triggers and is distinct from fixing the lost release callback.

## Final binary validation

Final executable: `.source/gpu-stage1/s38-frame-continuity/runtime/Xwayland`

SHA256:

```text
3f27886fae2bde560d34ca53dedacf4cec09fd9dbcddc591efafc4ec9bc994c2
```

| Check on final S38 | Observation |
|---|---|
| Exact Present generation regression | Blue → green → green, zero X errors |
| vkcube after stress at fixed 500×500 | 22 samples, 21 content changes, 22 non-grey frames, zero geometry changes/errors |
| Zink glxgears after stress at fixed 500×500 | 49/49 colored samples, 48 content changes, zero blanks/errors |
| Thin-height vkcube sequence including 267×19 / 826×8 | 34 resizes; client survived |
| Thin sequence, valid center readbacks | 78/81 drawn; 3 blank, maximum blank run 1; 73 same-geometry comparisons changed |
| Thin sequence after restoring 500×500 | 22 samples, 21 changes, zero geometry changes/errors |
| Thin sequence edge readback | Nonzero black/off-grey edge observations remain; no pixel-perfect claim |
| Previous private binaries | All nine registered profile hashes verified, including final S38 |
| Protected installed graphics files | All eight manifest checks passed, including system Xwayland, Vulkan and Mesa libraries |
| Native glibc GLX through final S38 | Debian glxinfo exit 0, Adreno 840; planned seven-second glxgears run completed |
| Live user desktop | Same Xwayland/XFCE PIDs and DISPLAY; READY |
| Physical Android panel / user's drag sequence on S38 | **Not tested** |

The thin-window sampler skipped 90 iterations with too-small or unreadable sampling geometry. Skips are not successful frames and are not included in the 81 valid samples. The 3 blank samples and edge observations must not be hidden behind a generic PASS. Screenshot/readback cadence also cannot establish Android presentation latency or absence of stutter.

The S30/S31/S32 probes now compare hashes only between samples with unchanged width and height. The fixed-size cube probe rejects any geometry change. This prevents geometry alone from being counted as new animation, but still does not turn X11 readback into physical-display evidence.

### Device-local evidence

All paths below are relative to `.source/gpu-stage1/s38-frame-continuity/`:

- `logs/s37-freshness-negative.log`: S37 exact-color rollback.
- `logs/s37-lifetime-baseline.log`: S37 three-burst descriptor growth.
- `logs/s38-release-only-proof.log`: callback preservation with old watchdog.
- `logs/s38-final-lifetime-vkcube.log`: final S38 lifetime, animation and exact-generation results.
- `logs/s38-final-lifetime-zink.log`: final S38 lifetime and fixed-size gears.
- `logs/s38-release-ordered-vkcube-thin.log`: final S38 thin-height sequence and residual samples.
- `logs/s38-final-session-guard.log`: all nine pinned binaries and session-isolation guard checks.
- `logs/s37-glibc-output.log`: native glibc basic GPU execution through S37.
- `logs/s38-final-glibc-output.log`: the same native glibc smoke check on the final pinned S38.
- `build-manifest.json` and `logs/relink.log`: exact source and executable build receipt.

Earlier `s38-lifetime-*`, `s38-real-*` and `s38-final-vkcube-thin.log` runs predate the final pre-idle ordering fix. They are retained as investigation evidence, not substituted for the final results above. The release-only diagnostic is not an allowlisted desktop profile.

## glibc: which part is supported?

The actual Xwayland ELF interpreter is `/system/bin/linker64`. Both that display server and the Android helper are Bionic processes. The tested architecture is:

**native glibc client/Mesa → X11/GLX/DRI3 with shared GPU handles → Bionic Xwayland → Xwayland's own output AHB → Android helper Vulkan memory / VkImage import.**

The client does not load Bionic `libnativewindow` or call an Android AHB allocator through glibc in this route. Raw `AHardwareBuffer*` pointers are not passed between processes. The server allocates its own composited output, and the protocol transfers shareable handles. This is different from adding a Bionic broker that allocates a glibc application's rendering buffers; the user's no-broker/no-libhybris requirement remains in force.

AHB external-memory import binds shared storage to Vulkan objects; import itself is not a pixel-copy conversion. Android documents AHB sharing and EGL/Vulkan binding, and the Vulkan extension specifies importing AHB-backed memory for images/buffers. Final host composition and existing producer synchronization still remain. This work does not establish end-to-end zero-copy or asynchronous fence-only presentation.

| Question | Supported conclusion |
|---|---|
| Can a native glibc app use the current Bionic Xwayland output route? | Basic GLX GPU execution was verified. |
| Does that make glibc itself directly compatible with Android AHB APIs? | No; no such ABI integration is implemented here. |
| Does an Android host's AHB extension enable it in a Linux Vulkan ICD? | No; extension/memory-export support belongs to the selected device/ICD. |
| Is fully glibc-native Xwayland/Wayland AHB allocation validated? | No; preserve the native DMA-BUF route rather than moving S36's `libnativewindow` allocator unchanged. |
| Are all glibc Vulkan WSI, resize and long-run combinations covered? | No; they need their own tests. |

Actual smoke checks on **both S37 and the final pinned S38**: Debian native glibc `glxinfo -B` exited 0 with renderer `Adreno (TM) 840`; GLX, DRI3 and Present were exposed, and `glxgears` ran for the planned seven-second timeout. Both runs enabled S36 root AHB output and the private dual-ABI fence shim. The device-local diagnostics adapt the existing `test-s16-xwayland-glibc-glx.py` to the candidate path/flags; the final S38 diagnostic additionally verifies its SHA256. These runs confirm basic client GPU execution; they did not independently correlate host import or panel output, and their FPS values are not presentation benchmarks. S38 does not change that libc/transport boundary.

The private dual-ABI xshmfence interposer remains part of the display server launch for the existing Bionic/glibc fence-format difference. It is separate from AHB output. Shared formats, stride/modifiers, FD transfer and GPU completion/return ordering still matter across the whole path.

Primary API references:

- [Android NDK AHardwareBuffer](https://developer.android.com/ndk/reference/group/a-hardware-buffer)
- [VK_ANDROID_external_memory_android_hardware_buffer](https://docs.vulkan.org/refpages/latest/refpages/source/VK_ANDROID_external_memory_android_hardware_buffer.html)

## Build and isolated regressions

The builder replays patches 0012/0013/0014 in a separate overlay, verifies six source/header hashes and old S32/S36/S37 executables, recompiles five objects, and links a new private executable. The generic Present replacement object precedes its unchanged archive so the old member is not selected. Shared source/build objects, installed drivers and historical binaries are preserved. Rebuilding an already running S38 desktop is refused.

```bash
cd ~/alroot_wayland
bash graphics/tools/build-s38-frame-lifetime.sh
python3 graphics/tools/test-s38-frame-lifetime.py --mode baseline --demo vkcube --storm
python3 graphics/tools/test-s38-frame-lifetime.py --mode candidate --demo vkcube --storm --freshness
python3 graphics/tools/test-s38-frame-lifetime.py --mode candidate --demo zink --storm
python3 graphics/tools/test-s36-rootful-ahb-output.py --s38-frame-lifetime --mode candidate --pattern thin --demo vkcube --wm --seconds 6
python3 graphics/tools/test-s28-s26-visual-session-guard.py
bash graphics/tools/start-s38-frame-lifetime.sh --restart --dry-run
```

These tests do not restart the user's desktop. The FD stress test's displays, Xauthority, WM/configuration, demo windows and process groups belong to the test and are cleaned up afterward. Avoid running multiple GPU regression scripts at once when comparing timing/resource counts.

## Explicit user session switch and rollback

After saving open XFCE work, run in a **separate native Termux shell**, not in the desktop being restarted:

```bash
cd ~/alroot_wayland
bash graphics/tools/start-s38-frame-lifetime.sh --restart
```

No APK update is required. This deliberately restarts only the project desktop with the pinned S38 executable and required opt-in flags. It does not change the ordinary default profile. A successful launch ends with `S38_ACTIVE_PRIVATE_XWAYLAND=PASS` and the desktop READY status.

In the new XFCE, reproduce the user's actual continuous resize and move sequence, one demo at a time:

```bash
cd ~/alroot_wayland
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual zink
# Close the demo before starting the next:
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual vulkan
```

Check whether gears jump back to an older arrangement, the cube stalls while dimensions change, thin resizing crashes, and either demo leaves transient edge fragments. A continuous Android recording is still the required evidence for the user's visible complaint.

Return to the original S36 from native Termux:

```bash
cd ~/alroot_wayland
bash graphics/tools/start-s38-frame-lifetime.sh --restore-s36
```

`--restore-s37` and `--restore-s32` are also provided. Until physical-screen acceptance, S38 remains an explicit candidate.
