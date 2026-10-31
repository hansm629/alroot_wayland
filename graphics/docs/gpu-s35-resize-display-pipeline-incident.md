# GPU-S35: why S28–S32 did not solve visible resize/move artifacts

**2026-10-31, S26 Ultra / Adreno 840, Android Wayland helper with XFCE4/Xfwm4.**
**STATUS: DIAGNOSIS ONLY. NO NEW RENDERER CANDIDATE, APK INSTALL, XFCE RESTART,
SYSTEM DRIVER CHANGE, OR DEFAULT PROMOTION.**

## User incident, not a passing acceptance test

The user reports that **S32 is still not acceptable**: `glxgears` leaves
visible colored-gear trails during repeated window movement, while
`vkcube` temporarily stops rotating and shows narrow black bands at the
bottom and right. Previous S28/S29/S30/S31 releases attempted to hide
such artifacts with XRender rescaling, a previous-frame snapshot and/or
whole-window background clearing. Some changes caused **worse regressions**
(S31 made cubes disappear and gears flicker). An X11 demo merely staying
alive or its center pixel changing does not validate the **Android panel**.

## Xfwm4 overlay, not just the application pixmap

`graphics/tests/s35-xcomposite-move-trail.c` is an isolated XComposite
screen test. `graphics/tools/test-s32-nonblank-resize.py` has an explicit
`--audit-composite-move-trails` option. It starts **disposable** authenticated
Xwayland on :201+, disposable Xfwm4, real S26 Wrapper / Mesa27 Zink
`glxgears`, and alternately moves its OWN XID between two far-apart
positions. Instead of reading only the current application pixmap, it
samples the **root XComposite overlay at the VACATED window location**.

Verified on the actual S26U:

```text
S35_XCOMPOSITE_TRAIL_SUMMARY moves=22 traced=22
  ghost_at_15ms=0 ghost_at_50ms=0 ghost_at_100ms=0
  current_window_chroma=22
S35_XCOMPOSITE_AUDIT=PASS_NO_LONG_LIVED_X11_OVERLAY_GHOST
S35_ANDROID_PHYSICAL_DISPLAY=NOT_VERIFIED_BY_X11_PIXEL_READBACK
```

That is strong evidence that Xfwm4's **composited X11 overlay in this
specific move scenario** is already clearing the old gear pixels.
It is NOT proof that the real Android app never displays trailing frames:
the X11 test is not a capture of the APK's SurfaceView or the panel.
The user's physical observation remains authoritative for display quality.

## Rootful Xwayland output-to-host timings

A separate *unshipped* opt-in diagnostic Xwayland, S33, timed
`xwl_window_swap_pixmap()`, `xwl_pixmap_get_wl_buffer()`, and the returned
Wayland frame callback; each stage is aggregated over 2s. The test
passed through the real running Android helper and GPU while isolating
the X11 demo from the user's existing XFCE.

Representative high-frequency resize results:

```text
S33_OUTPUT_2S commits=202 callbacks=202
  swap_avg_us=165 swap_max_us=1562
  export_avg_us=1513 export_max_us=9875
  callback_avg_us=8142 callback_max_us=15508
```

The rootful KGSL Xwayland normally waits for the preceding Wayland
`wl_surface.frame` callback before starting another GPU export/attachment.
The callback interval is approximately one 120Hz refresh, then new
GPU work incurs another ~1–2ms average (with occasional longer stalls).
Some continuous resizing intervals therefore do not meet the 120Hz
output deadline even though the app and Xfwm4 overlay have changed.

At approximately 14:41:30 KST, the actual Android helper's *logcat*
reported **884 scenes shown in the previous 10s (88.4fps)** during an
isolated Xwayland rapid-resize test, against a reported 120Hz display:

```text
884 scenes, 884 on screen (copy 884, zero-copy 0)
render_scene 3.89/11.53 ms
fence wait 1.95/7.39 ms
```

These data are from an overlapping test period and are not a precise
one-to-one frame-ID correlation across the X11 and Android processes.
In particular, this does not establish that the host is *the only*
source of the remaining artifacts.

## Actual transport: GPU-copy, not direct VkImage zero-copy

The Android helper verified its imported Xwayland desktop transport:

```text
LINEAR DMA-BUF -> Vulkan VkBuffer GPU-copy:
1920x1080, pitch 7680, fd 8388608 B;
no CPU pixels, VkImage import not used
```

The native `vk_present.c` source selects the VkBuffer import +
`vkCmdCopyBufferToImage` path when a linear DMA-BUF source lacks a
compatible DRM-modifier-backed VkImage import. The helper also composes
and presents a GPU-owned screen swapchain and waits for a GPU completion
fence. This is not CPU pixel copying, but also **not an end-to-end
zero-copy** desktop pipeline. Native producer synchronization,
imported buffer lifetime, output-buffer recycling and compositor timing
must be addressed as one pipeline rather than hidden by an XRender
snapshot of old application content.

## Tried and rejected mitigations (DO NOT PROMOTE)

1. Disabling S32 snapshot protection altogether: isolated Xfwm4 overlay
   center went black **23/120** samples, vs **0/120** with S32 snapshot
   enabled. Merely deleting last-frame retention is not a full fix.
2. `MESA_VK_WSI_DEBUG=forcesync`: introduced additional client GPU
   waits, slowed output-buffer export (one observed maximum ~16ms), and
   reduced the number of resizes processed in the same test horizon.
   Do not turn this on as a blanket workaround.
3. S34 **unshipped** speculative one-extra-commit-before-frame-callback:
   a 5s accelerated resize test processed **154** window resizes vs
   **161** for the standard path, with no repeatable benefit and some
   reintroduced black-edge readbacks. Motion-only testing gave similarly
   inconclusive results. Not safe to promote on this evidence.
4. Whole-window clearing or scaling every unmatched Present (S31/S28):
   demonstrably hid real cube/gear content or visibly distorted it.
   Never treat a zero-black-pixel score achieved by erasing an object as
   visual correctness.

## Next acceptance criteria — physical frame continuity, not FPS alone

Before another deployment, the isolated tests must measure the same
**content generation** at X11 source, XComposite overlay, Wayland
`wl_buffer`, Android imported VkImage/VkBuffer, and **Android SurfaceView**.
Validate real client content while moving AND resizing, with explicit
comparisons of timestamps, buffer IDs, sizes, acquired/released fences,
output frame generation and black/ghost pixels at *both* old and new
window boundaries. Only then choose among Vulkan WSI resize/present
coalescing, output VkImage/AHB import, or explicit synchronization.

**Existing user session was never restarted during this diagnosis.**
All S33/S34 speculative code was isolated, tested, rejected and the
private Xwayland source was restored to the pinned S32 source state.
System Termux-X11, user APK, installed Mesa/Wrapper and XFCE/IME remain
unchanged. The safest immediate version comparison is an explicitly
user-initiated switch from S32 to the previously less regressed S30,
via a **separate native Termux shell** (this intentionally restarts
the project XFCE and thus requires saving user work):

```bash
cd ~/alroot_wayland
bash graphics/tools/start-s32-kgsl-nonblank-present.sh --restore-s30
```

The result is **NOT resolved**. Do not claim S33 or S34 is a new fix.
