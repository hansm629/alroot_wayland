# GPU-S25 — screen stutter and desktop-session termination diagnostic

**Date:** 2026-10-30. **Device:** S26U, Adreno 840, 120 Hz on-device display.
**Prior state:** S24 private Vulkan Wrapper XCB/GPU-copy resize correctness
passed, but the user reported that `vkcube` and Mesa 27 Zink `glxgears`
rotated with small intermittent pauses. During a persistent `--visual zink`
test the Android helper showed a dead desktop.

## What actually stopped

The Android Wayland host foreground service continued to run and its
`wayland-0` socket was READY. The private rootful Xwayland :94, XFCE4,
clipboard and IME processes all disappeared. At **18:45:56.638** the Android
log recorded `[program] disconnected: Xwayland (pid 24814)` and the
compositor closed `Xwayland on :94`. We did not find a matching native
Xwayland SIGSEGV tombstone in the available crash log. Therefore this is
**not proven to be a native Wayland host crash or a specific Xwayland
segfault**; OS process-killing, loss of X11 parent process, Wayland protocol
errors, and client/server failure remain separate possible explanations.
Do not claim the release fix below alone conclusively prevents every kind
of Android session termination.

The original logs were backed up under the ignored local diagnostic folder
`.source/gpu-stage1/s25-host-pacing/logs/` before reopening XFCE.
The original XFCE config, cursor, IME and KGSL Xwayland flags were preserved.
The restarted private session is **:95** (not the old dead :94), with Xwayland
and XFCE both reporting RUNNING and a READY X11 desktop.

## Confirmed host pacing and logging behavior

Immediately before the disconnect, the Android Wayland host reported:

| Metric, 18:45:45–18:45:55 | Observed |
| --- | --- |
| Display tick rate | ~120 Hz |
| Displayed scenes in ten seconds | 1132 (113.2 fps) |
| Incoming game GPU frames in ten seconds | 1303 (~130.3 fps) |
| Render-scene CPU wall time avg/peak | 3.00 / 55.50 ms |
| Host Vulkan fence wait avg/peak | 1.57 / 17.04 ms |
| `wl_buffer.release` latency avg/peak | **389.06 / 988.73 ms** |
| Releases counted / >=1 ms | 1188 / 1188 |

The host's existing `release_buffer` scheduler serialized *each* release
at one refresh interval after the last one, with the max deadline itself
growing as `interval * (releases_pending + 1)`. A producer running above
the 120 Hz release rate could accumulate hundreds of milliseconds of
queued buffer-release latency. This mechanism is confirmed from the
source and is consistent with the observed latency, although the exact
Xwayland process-exit cause has not been established.

`native/waylandcomp/src/vk_present.c` also logged `LINEAR DMA-BUF -> Vulkan
VkBuffer GPU-copy` unconditionally on **each** new 1920x1080 DMA-BUF.
For this Xwayland root-pixmap update path, that produced high-frequency
Android logcat plus file writes, adding avoidable host CPU/IO contention.

An Android dynamic physical refresh-rate change between 60 and 120 Hz was
also observed **after** the Xwayland session had disconnected, while the
user moved between Android activities. That is **not sufficient evidence**
that physical refresh switching caused the earlier in-game micro-stutter.

## Scoped S25 candidate changes

- `native/waylandcomp/src/alroot_release_pacing.h`: pure testable function
  for scheduling the release of a **no-longer-in-use** client buffer.
- `native/waylandcomp/src/compositor.c`: when the user has not set an explicit
  FPS limit (`g_fps_limit == 0`), use the advertised Android physical
  `wl_output` refresh period instead of the noisy host event-loop wakeup
  interval, and cap a new release deadline to **one display interval after
  now**. Example: maximum scheduled-ahead latency 8.33 ms at 120 Hz.
  Preserve the existing deferred GPU-ownership/release-fence logic and
  user-defined positive FPS caps. Preserve `fps=-1` immediate-release mode.
  Extend 10-second diagnostics with `auto release caps` and `peak pending`.
- `native/waylandcomp/src/vk_present.c`: log successful LINEAR GPU-copy
  initialization on first import, dimension/stride changes, or every 30 s,
  rather than hundreds of times per second. VkImage, GPU copy, semaphore,
  fence, resource lifetime and device selection logic are unchanged.

This changes host Java/JNI-bundled native code, so source-only tests cannot
verify improved physical animation. The APK **must be built and installed
as a separate diagnostic update** before on-device A/B verification.

## Offline validation

```sh
cd ~/alroot_wayland
bash graphics/tools/test-s25-host-release-pacing.sh
bash graphics/tools/test-s22-xfce-startup-cursor.sh
```

The C test includes the exact production scheduling helper. On a modeled
130 FPS producer at a 120 Hz display the deadline is bounded at 8.333 ms;
150 FPS / 60 Hz is bounded at 16.667 ms. Under a slower producer there are
no unnecessary caps. The previous explicit positive FPS limiter behavior
remains unchanged (and may still deliberately queue buffers). Both modified
native C source files pass Android ARM64 syntax-only compilation, and
S20/S21/S22 pointer/cursor regression tests passed.

## Test/update and acceptance gates

The versionName is `0.1.29-gpu-s25-release-pacing-candidate` while the
versionCode stays **29**, matching the installed S22 development APK, so
reinstalling the original same-signature S22 APK remains a potential
rollback path. Do **not** overwrite or remove the saved S22 signed APK.
Android installation still requires the user's installer approval.

After installing the candidate on S26U, check the private XFCE session with
the same supported S24 client paths. Do **not** enable any unverified default
renderer/profile. Compare the new ten-second `[perf]` reports under Zink
against the above **389 ms release delay** and confirm Xwayland stays
running. The real target is stable 120 Hz animation without intermittent
halts, not merely high FPS or successful `vkcube` window creation.

**Not yet verified:** new candidate APK on Android hardware, physical
smoothness, sustained GPU/CPU clocks, long-duration Xwayland uptime, or
zero-copy. Even when the tests improve, preserve the user's visual gate and
keep the global `awstart` default unchanged.
