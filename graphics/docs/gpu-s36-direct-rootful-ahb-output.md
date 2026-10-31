# GPU-S36 — Direct AHB-backed rootful Xwayland output (Adreno-only candidate)

**2026-10-31 · Galaxy S26 Ultra, Adreno 840 · GLAMOR/KGSL with real S26 private Vulkan Wrapper, Mesa27 Zink, Xfwm4.**

**Status:** An isolated, opt-in private Xwayland binary was built, ran Vulkan/Zink, and was confirmed imported into the existing Android Wayland helper as a Vulkan **VkImage**. **Physical Android screen smoothness, frame continuity, and disappearance of the user's right/bottom black bars and colored gear trails have not been verified.** Do not promote this to normal `awstart`. All installed system graphics components, APK, original private server binaries and the user's live XFCE remain unchanged.

## Why S28–S32 did not solve the user-visible problem

Repeated `vkcube` resize still temporarily freezes and shows narrow black bottom/right strips; repeated `glxgears` moving leaves colored gear ghosts on the Android screen. The S28–S32 changes tried stretching/copying/centering/clearing an *old* application frame. They were unreliable and sometimes made the picture worse (S31 blanked the whole client window).

S35 instead inspected **Xfwm4's XComposite overlay**, not only an application-owned pixmap. During 22 major `glxgears` moves there were **0 colored ghost observations at the vacated position after 15/50/100ms**, and all 22 new positions had gear pixels. The Android panel may still present ghosts, which requires checking the later producer/consumer/output buffer chain.

The live host identified the previous Xwayland output transport as:

```text
1920×1080 Xorg GLAMOR root GPU texture
  → EGL linear DMA-BUF export
  → Android Vulkan VkBuffer DMA-BUF import
  → VkCmdCopyBufferToImage
  → Android Vulkan desktop composition
  → Android screen swapchain
```

This has a full-screen intermediate GPU copy, and the KGSL Xwayland producer uses `glFinish()` for implicit cross-process correctness. Previous Xwayland rootful output timing measurements found approximately 8ms Wayland frame callbacks plus 1–2ms average producer/export work, sometimes longer. Forcing extra sync or speculative extra Wayland commits did not produce a safe improvement.

## S36 implementation: allocate the *GLAMOR root pixmap* on AHB

S1.3's standalone Adreno AHB proof already demonstrated renderable, GPU-writable Android buffers. The first S36 prototype made an extra `GL source → AHB` blit, which traded one copy for another. **That is not the selected final S36 design.** S36 now creates an AHB-backed `GL_EXT_memory_object_fd` texture and installs **that same texture as Xwayland's GLAMOR rootful backing pixmap**. Xfwm4's normal XRender/XComposite output renders to it directly.

```text
Xfwm4/XComposite
  → Xwayland root GLAMOR texture (already on AHB external GPU memory)
  → alroot_gpu_buffer_v1 wl_buffer (AHardwareBuffer_sendHandleToUnixSocket)
  → Android existing AHB VkImage importer
  → Android Vulkan desktop composition / GPU-copy into screen swapchain
  → Android display
```

**Removed in this candidate:** separate GLAMOR → AHB pixel blit, intermediate host VkBuffer and host VkBuffer → VkImage copy. **Still present:** cross-API producer `glFinish()` for correctness, Android Vulkan GPU composition and output swapchain GPU-copy. This is **not an end-to-end zero-copy/direct-scanout implementation** and no frame-rate improvement is guaranteed.

Key files:

- `graphics/patches/xwayland/kgsl/0010-kgsl-rootful-ahb-direct-output.patch` — opt-in KGSL-only hooks in private GLAMOR root pixmap creation, wl_buffer lifecycle, Wayland generation cleanup.
- `graphics/src/s36_xwl_ahb_output.c` / `graphics/include/s36_xwl_ahb_output.h` — AHB allocation + verified native image FD + GPU memory-object import + Wayland custom `wl_buffer` creation; preserves GL state and source texture ownership.
- `graphics/tools/relink-s36-rootful-ahb-xwayland.py` — compile **only** the private GLAMOR source and helper/Wayland protocol objects; link an isolated Xwayland, reuse pinned S32 window/Present objects.
- `graphics/tools/build-s36-rootful-ahb-output.sh` — fail-closed S26U/full-HD AHB pixel-identity, source diff/hash, user PID and protected driver/binary checks.
- `graphics/tools/test-s36-rootful-ahb-output.py`, `graphics/tools/test-s36-build-and-scope.sh` — isolated real GPU smoke and source/binary/ABI guarantees.
- `graphics/tools/start-s36-rootful-ahb-output.sh` — manual opt-in launch and unchanged private S32/S30 rollback.

The S26U-only opt-in is `S36_KGSL_ROOT_AHB_OUTPUT=1` alongside `S30_KGSL_TINY_AHB_FD0=1` and `S32_KGSL_NO_BLANK_PRESENT=1`. The AHB pixmap is enabled only for rootful KGSL, **exact 1920×1080 / 24bpp** backing pixmaps. Other resolutions (including physical panel orientation changes), rootless displays, Mali/Xclipse and unsupported gralloc layouts fall back to the original DMA-BUF path. Neither the Android helper APK nor S24/S26 Vulkan Wrapper is patched. Existing Xwayland DRI3 AHB pixmap imports remain intact.

### Correctness fixes made during isolation

An early prototype hit `wl_display.error: invalid object 27`. Xwayland performs multiple Xorg/Wayland initialization generations; the global AHB manager registry proxy cannot be reused across them. The finalized candidate resets the experimental manager at the KGSL EGL generation boundary, and the disposable server completes multiple such transitions with no protocol error. The AHB texture is handed to GLAMOR only after its framebuffer and native handle import succeed; the helper avoids double texture deletion and closes failed DMA-BUF imports. Buffer releases still follow the original Wayland `wl_buffer.release` path.

## On-device verification, 2026-10-31

The following **actually ran on the real Adreno 840** using disposable authenticated rootful Xwayland (:201+) and disposable Xfwm4. They did **not** restart XFCE or change the system drivers/APK.

| Test | Result |
| --- | --- |
| S1.3 GPU-writable AHB at **1920×1080**, 5 tested pixel positions | PASS; 5/5 valid and 1920 stride |
| Full-HD standalone AHB sent to existing Android Vulkan importer | PASS, `AHB -> VkImage GPU-copy` |
| S36 direct AHB GLAMOR backing, actual Xfwm4/Vulkan `vkcube` gentle resize | PASS, 42 resizes; 204/204 visible cube samples; no client crash |
| `vkcube` extreme **1×98/8×98/40×98**, normal-size restoration | PASS, 37 resizes; 22 samples after restoring 500×500, 21 image hashes changed |
| S36 direct AHB GLAMOR backing, Mesa27 Zink `glxgears` abrupt resize | PASS, 43 resizes; 200/207 sampled frames with colored gears |
| Xfwm4 XComposite vacated-window gear trail, 22 alternating moves | PASS, 0 ghosts at 15/50/100ms, new gear colors 22/22 |
| Android helper import while running S36 | `AHB -> VkImage GPU-copy: 1920x1080` confirmed repeatedly |
| Existing user XFCE (Xwayland, IME, clipboard) | PIDs unchanged |

The baseline S32 and S36 Zink demo FPS were close (approximately **112 vs 112fps**, with different individual runs). **Do not assert performance improved from this result.** CPU scheduling, GPU clocks, host foreground activity and refresh presentation state are not controlled enough for a benchmark comparison.

**Crucial limitation:** During these tests the Android helper's statistics said **0 frames on screen** because the Android output surface was not presenting on the physical panel. An attempt to foreground the already running activity had no effect; attempts to inject a screen-on input were denied by Android permissions. Therefore the two user-visible failures are **not yet resolved by demonstrated physical-panel evidence**, even if the GPU path is functional. Preserve the original S32 user observations and test on actual Android UI before accepting S36.

## Reproducible build and isolated test, with no XFCE restart

```bash
cd ~/alroot_wayland
bash graphics/tools/build-s36-rootful-ahb-output.sh
bash graphics/tools/test-s36-build-and-scope.sh
python3 graphics/tools/test-s36-rootful-ahb-output.py --mode candidate --demo zink --pattern abrupt --wm --seconds 8
python3 graphics/tools/test-s36-rootful-ahb-output.py --mode candidate --demo vkcube --pattern extreme --wm --seconds 7
python3 graphics/tools/test-s36-rootful-ahb-output.py --mode candidate --demo zink --wm --audit-composite-move-trails --seconds 5
```

S36 private executable SHA256: `df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5`. The original S16/S28/S29/S30/S31/S32 project binaries remain unchanged, and the generic KGSL Xwayland builder refuses an experimental source overlay. The global Termux-X11 and Mesa drivers are protected by their existing SHA256 manifest.

## Explicit physical user acceptance (restores previous versions if needed)

**Save any open XFCE work first.** In a **separate native Termux shell**, not a terminal inside XFCE, run:

```bash
cd ~/alroot_wayland
bash graphics/tools/start-s36-rootful-ahb-output.sh --restart --dry-run
bash graphics/tools/start-s36-rootful-ahb-output.sh --restart
```

The second command **intentionally restarts only the project XFCE**. It does not update the Android helper APK or system drivers; no separate `awstart --stop` is needed. After the new Alroot Wayland XFCE is READY, run these commands in its new terminal, **one demo at a time**:

```bash
cd ~/alroot_wayland
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual zink
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual vulkan
```

Verify on the actual Android screen whether the old red/green/blue `glxgears` trail persists, whether the cube pauses during continuous resize, and whether thin black right/bottom rectangles still appear. Assess input and desktop responsiveness as well. If S36 is worse or unstable, restore the pinned **S32** from native Termux (requires another explicit XFCE restart):

```bash
bash graphics/tools/start-s36-rootful-ahb-output.sh --restore-s32
```

S30 is also available with `--restore-s30` as an alternative earlier comparison baseline. **Do not promote S36 to `awstart` default without user physical visual acceptance.**
