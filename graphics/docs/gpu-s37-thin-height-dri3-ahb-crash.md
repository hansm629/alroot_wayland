# GPU-S37 — Fix S36 `vkcube` DRI3/AHB BadMatch on thin-height resize

**2026-10-31 · Galaxy S26 Ultra / Adreno 840 · Bionic private Xwayland (S36 root AHB output) · S26 Vulkan Wrapper XCB WSI.**

**Status:** The *specific* AHB DRI3 import failure triggered by video-demonstrated `267×19` and `826×8` Vulkan window resizes was reproduced, fixed and verified on an isolated real S26U Xwayland. This is **not** a verified fix for visible `glxgears` gear trails, general vkcube stutter or physical Android panel latency. S37 is opt-in only, not the `awstart` default. Running user XFCE and all old binaries remain unchanged.

## User video and real device evidence

The uploaded recordings `Screen_Recording_20261031_155520_Alroot Wayland.mp4` (vkcube) and `Screen_Recording_20261031_155316_Alroot Wayland.mp4` (glxgears) show that S36 **did not fix** the physical Android compositor/resize symptoms. In the Vulkan recording, the window disappears after repeated resizing; the terminal shows:

```text
S26_XCB_DRI3_AHB_IMPORT_FAILURE code=11 forced=0 extent=267x19
S26_XCB_DRI3_AHB_RETRY attempt=1 image=0
S26_XCB_DRI3_AHB_RETRY attempt=2 image=0
S24_VISUAL_CLIENT_EXIT_CODE=-11
```

The **live S36 user's private Xwayland log** independently recorded six `S16 AHB DRI3 no unambiguous linear-sized DMA-BUF fd` failures. An isolated Xfwm4 `vkcube` test with real 267×19/826×8/350×12 dimensions reproduced `BadMatch 11` and three failed image attempts on unmodified S36.

We then added device-local instrumentation and obtained the real native AHB handle data:

```text
logical=267x20; stride=320; expected=25,600 bytes; nfd=2
fd[0]: st_size=28,672, inode=524226, mode=0600
fd[1]: st_size=28,672, inode=524227, mode=0600
```

`267×20` is the physically padded DRI3/AHB extent used for the logical `267×19` client swapchain. The two native gralloc FDs have **equal lengths but distinct inodes**. The S16 size-only selection rejects this tie. S30 had a corrective `fd[0]` override, but only when **width ≤64**, not when **height** was tiny (e.g. 267×19 or 826×8). The Vulkan wrapper then retried BadMatch and could exit with code -11.

An initial S37 diagnostic unnecessarily required `S_ISREG(st_mode)`. The S26U vendor DMA-BUF reported `mode=0600`, so this test wrongly rejected both handles. We removed that invalid type assumption; the final S37 requires independently grounded size/inode and geometry evidence instead. **Do not reintroduce `S_ISREG` for this Android gralloc handle.**

## Actual S37 correction

`graphics/patches/xwayland/kgsl/0011-kgsl-thin-ahb-fd0.patch` applies **after S36's 0010** and modifies ONLY the private Xwayland native AHB DRI3 socket → EGL import adapter.

The new override `S37_KGSL_THIN_AHB_FD0=1` selects fd[0] only when all conditions are met:

- S26U isolated KGSL GLAMOR and Android private AHB-socket DRI3 modifier 1255, already validated by the unchanged server.
- BGRA8 image (`desc.format==5`), width over 64, physical height 1–128, expected backing bytes no more than 2MiB.
- Exactly **two** native handle FDs, both stat-able with identical file lengths, **different inode numbers**, and fd[0] large enough for the advertised pitch/height within the bounded vendor padding tolerance.
- The explicit `S37_KGSL_THIN_AHB_FD0=1` launch flag. No global libnativewindow hack, other-vendor override or direct modification to imported client images.

This preserves S30's old narrow-**width** import exception, the S26 Wrapper's fault-retry behavior, and S36's AHB-backed rootful GLAMOR output. It does not change glibc applications, installed Vulkan/Mesa or the Android APK.

## S26U on-device result

| Test | S36 negative control | S37 candidate |
|---|---|---|
| 267×19 repeated (server receives padded 267×20) | FD tie → `BadMatch 11` | FD0 selected; X11 GPU image imported |
| 826×8 | FD tie → `BadMatch 11` | FD0 selected; X11 GPU image imported |
| Thin-height repeated resize | Imported image attempts failed | **39 resizes, client alive** |
| Restore Vulkan window to 500×500 | Not reliably reached | 22 frame samples, **21 changed** |
| Legacy width 1/8/40 AHB resize | S30 previously fixed | **38 resizes, no crash** |
| Vulkan non-extreme gradual resize | Earlier S36 passed | **39 resizes, no crash** |
| Mesa27 Zink/GLX abrupt resize | Existing visual problem persists | **36 resizes, client alive, RGB gear content measured** |
| Isolated Xfwm4 composited gear move | Previously no leftover RGB on old X11 coordinates | **22 moves, 0 long-lived XComposite trails** |

The X11 pixel readback excludes too-small windows where the sampler cannot safely inspect a meaningful center area. It does **not** prove physical-frame continuity or zero flicker on Android. The `glxgears` recording still shows clipping/trails during continuous movement/resizing; S37 does **not** claim to solve that visual symptom. Android host frame pacing/cross-API synchronization may be separate bottlenecks.

## Important distinction: is this AHB VkImage pipeline compatible with glibc?

The rootful Xwayland executable's actual ELF program interpreter is **`/system/bin/linker64`**: S36/S37 Xwayland runs as native **Bionic**, and the Android helper uses Android/Bionic Vulkan. The Android host imports `AHardwareBuffer` into `VkImage` **on its own Bionic side**. Therefore **there is no direct glibc-to-Bionic `AHardwareBuffer*` ABI transfer in the S36/S37 output stage**.

glibc Debian/Ubuntu/Arch clients communicate with the Bionic Xwayland via **X11/GLX/DRI3 + GPU memory handles**. The shared memory object and X11 wire protocol are different from passing a Bionic pointer. The existing private dual-ABI `xshmfence` compatibility interposer is still required for the 96-byte Bionic pthread vs 4-byte glibc futex fence formats. That incompatibility is independent of AHB output.

**Do not extrapolate:** native glibc Mesa/ICD combinations have different Vulkan/GLX DRI3 memory-export and synchronization support. Not all combinations have been exercised with S37. The follow-up investigation verified Debian native-glibc `glxinfo` and a seven-second `glxgears` run through both S37 and final S38 with root AHB output enabled; this establishes basic GPU execution, not continuous-resize or every Vulkan ICD's compatibility. Those runs did not independently correlate Android host import/panel output.

The user's requirement excludes a Bionic allocation broker and libhybris workaround. Keep glibc clients on their native GPU/DMA-BUF route; Bionic Xwayland allocating its own final desktop output is a different role. A **glibc-native Xwayland or Wayland compositor directly allocating/importing Android AHB** is not implemented or validated by this work. S36's `libnativewindow` allocator cannot simply be moved unchanged into glibc. A Linux ICD does not automatically inherit `VK_ANDROID_external_memory_android_hardware_buffer` merely because the Android host supports it. The Vulkan extension defines AHB import semantics, not cross-libc ABI compatibility. See the S38 report for the tested boundary and remaining limits.

## Reproducible source/pinned private binary and acceptance

S37 executable: `.source/gpu-stage1/s37-thin-ahb/runtime/Xwayland`, SHA256 `193f43e03f0e754c39cfb73e04ac1e0032f8f632e6d06b5d1b675f67c834a594`. S36 original SHA256 `df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5`. Both are immutable to the regular Xwayland builder.

From native Termux, **no XFCE restart for these tests**:

```bash
cd ~/alroot_wayland
bash graphics/tools/build-s37-kgsl-thin-ahb.sh
bash graphics/tools/test-s37-build-and-scope.sh
python3 graphics/tools/test-s36-rootful-ahb-output.py --s37-thin-ahb --mode candidate --pattern thin --demo vkcube --wm --seconds 7
python3 graphics/tools/test-s36-rootful-ahb-output.py --s37-thin-ahb --mode candidate --pattern abrupt --demo zink --wm --seconds 7
```

After saving open XFCE work, an **explicit** native Termux command (never XFCE terminal) switches the real private desktop to S37; this intentionally restarts XFCE, but not the Android helper, system Termux-X11, or GPU drivers:

```bash
cd ~/alroot_wayland
bash graphics/tools/start-s37-kgsl-thin-ahb.sh --restart
```

Run the original real visual tests one demo at a time in new XFCE:

```bash
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual vulkan
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual zink
```

**Rollback to the user's original S36:**

```bash
bash graphics/tools/start-s37-kgsl-thin-ahb.sh --restore-s36
```

Or `--restore-s32` for the earlier Xwayland. Do not promote S37 as default until the **real** Android screen has been manually checked for crashes and visible gear/cube artifacts.
