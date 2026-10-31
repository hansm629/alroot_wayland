# Xwayland GPU patch slots (nothing is installed by this directory)

**Generic backend** (future `generic/`): keep GLAMOR and DRI3 build-enabled
in an Alroot-only Xwayland binary. Select DRM/GBM, surfaceless EGL+DMA-BUF,
or an explicitly implemented AHB-present backend from actual capabilities.
No hard-coded KGSL node, Adreno IDs, or assumed ANGLE extensions.

**KGSL adapter** (future `kgsl/`): port `lfdevs/xwayland` PR #1 as a
distinct, opt-in backend. Its `XWAYLAND_FORCE_KGSL_SURFACELESS=1` behavior
is explicitly **not** the Alroot default. Debian 24.1.6 patch hunks must
be rebased onto the selected Termux Xwayland upstream source before build.

**Xclipse ANGLE/Vulkan** (future): EGL/GLES may be implemented by ANGLE over
Vulkan, so an EGL context is not evidence of DMA-BUF export/import. Inspect
`EGL_MESA_image_dma_buf_export`, EGLImage support, AHB handling and actual
buffer ownership/synchronization. Implement a separate bridge where the
required EGL export contract is absent. PowerVR/Mali follow the same rule.

No new global Xwayland, Termux-X11 DRI3 patch or runtime auto-configuration
exists yet. The current P20 Xwayland remains pixman and undisturbed.

The isolated Android/Bionic fix at
termux/0001-android-bigfont-pagesize-fallback.patch uses sysconf for the
actual kernel page size when SHMLBA is undefined. It does not alter
global packages. On S26U the GLAMOR+DRI3 Xwayland 24.1.14 binary
compiled successfully after this patch. Headless runtime DRI3 and
GPU frame import remain NOT ACCEPTED, so the executable must not be
automatically selected by Alroot yet.

The second Bionic-specific patch,
termux/0002-android-unprivileged-popen.patch, handles Android seccomp:
setgid(getgid()) is denied even for non-root Termux children, which
crashed the XKB helper and aborted Xwayland. Android only, we first
require matching real/effective UID and GID, then execute the child
under the already-restricted app identity without these syscalls.
Ordinary Linux builds retain the original Xorg privilege-drop code.

With the original two Android patches, private rootful Xwayland starts and accepts
X clients through an explicitly supplied listen FD against a private
headless Labwc. GLX and Present initialize in -glamor off and es
modes. The headless server cannot offer linux-dmabuf-v4 or wl_drm,
so GLAMOR falls back to pixman and DRI3 is not yet presentable.
Use graphics/tools/test-xwayland-private.py to reproduce safely; it
does not create clients on the user's actual :0 or :90 display.

## S26U opt-in KGSL port (never the universal backend)

The retained patch
kgsl/0001-optional-kgsl-surfaceless-dri3-24.1.14.patch ports the
lfdevs/xwayland PR #1 surfaceless GLAMOR/DRI3 commits to pinned
Xwayland 24.1.14. It is an **Adreno-only optional adapter** and is
never enabled by the generic Xwayland launcher on other GPUs.
Its isolated build is reproducible:

    ALROOT_XWAYLAND_VARIANT=kgsl bash graphics/tools/build-xwayland-private.sh

On S26U, an explicitly activated copy initialized GLAMOR through Mesa
Zink/Vulkan-wrapper and advertised DRI3, GLX and Present on disposable
DISPLAY :232. But glxinfo timed out and Xwayland reported "Error getting
buffer", so this is **not** a passed visible-screen GPU test.
For Xclipse/RADV-Xclipse, Mali and PowerVR the common DRM/GBM or
AHB-backed Vulkan route remains separate. Never make /dev/kgsl-3d0 a
default device or bypass DRI3 regressions for those GPUs.


## GPU-S1.6 — fix Android Xwayland rootful display locks (2026-10-30)

Unlike a `-listenfd`-based isolated smoke test, the real Alroot launcher
starts Xwayland rootful **without `-listenfd`**. The pinned upstream Xorg
`os/utils.c` then enters `LockServer()`. On S26U Android 17 the actual
`/tmp` directory is owned by `shell` and unwritable by Termux's unprivileged
app UID, producing:

```
Fatal server error:
Could not create lock file in /tmp/.tX92-lock
```

Two *Android-only*, generic (not KGSL-specific) private patches address it:

- `termux/0003-android-termux-tmpdir-server-lock.patch`: read an **absolute**
  `TMPDIR` at `LockServer()` time. The Alroot shell launcher already exports
  `TMPDIR=$PREFIX/tmp`, matching Termux Xtrans's prebuilt
  `$PREFIX/tmp/.X11-unix/X`. Non-Android Xorg still locks in `/tmp`.
- `termux/0004-android-atomic-no-replace-lock.patch`: Android denies `link()`
  with `EACCES` even within the app-private directory. Use
  `renameat2(RENAME_NOREPLACE)` as a fallback (only for `EACCES`/`EPERM`),
  publishing the fully-written PID lock **atomically** without overwriting
  another active display's lock. Preserve the original `EEXIST` process/PID
  collision handling. Never use plain `rename()` or globally disable X11
  locking, and never change Android's `/tmp` permissions.

Both patches apply through the existing private `build-xwayland-private.sh`
`000*.patch` loop for **both generic and KGSL variants**. Neither patches
Termux's installed Xwayland nor changes a system renderer or APK.

```
ALROOT_XWAYLAND_VARIANT=kgsl bash graphics/tools/build-xwayland-private.sh
python3 graphics/tools/test-s16-xwayland-rootful-lock.py
```

On the S26U the test started a disposable `:201` private rootful server
without `-listenfd`; created `$PREFIX/tmp/.X201-lock`; exposed GLAMOR,
DRI3/GLX/Present; ran KGSL `glxinfo -B` (`Adreno (TM) 840`); rejected a
second server on **the same display** (`Server is already active`) while
preserving the first lock's inode; and cleaned its lock/socket at exit.
Live **Android S1.6 host logs** freshly reported this private Xwayland's
LINEAR DMA-BUF → VkBuffer → VkImage GPU-copy path as imported. User GUI
visual/pixel correctness, resizing and full XFCE remain separate gates.
Do not auto-enable KGSL surfaceless on Mali/PowerVR/Xclipse.

## GPU-S1.7 — wrapper AHB DRI3 + cross-libc shared fences (S26U)

The optional private KGSL patch `kgsl/0004-kgsl-ahb-socket-dri3-24.1.14.patch`
accepts vendor Vulkan-wrapper X11 WSI's nonstandard DRI3 modifier **1255**,
which represents an **AHardwareBuffer socket**, not a linear DMA-BUF. It
acknowledges the socket transfer, receives the AHB via Android nativewindow,
validates BGRA8/pitch and imports the actual gralloc DMA-BUF through KGSL
EGL. On Adreno 840 the wrapper Zink GLX context, 800×600 GLmark rendering
and Vulkan swapchain passed with this adapter. Standard LINEAR DMA-BUF DRI3
and the generic/global Xwayland remain unchanged.

The private launcher can additionally opt in to `ALROOT_XWAYLAND_DUAL_ABI_FENCE=1`
for the tested **Bionic 96-byte pthread vs Debian glibc 4-byte futex**
Xshmfence incompatibility. The shim is only preloaded into the explicitly
selected private KGSL Xwayland; it does not replace any system library.

### S28 private resize-frame preservation (S26U, candidate only)

`kgsl/0005-kgsl-gpu-window-resize-frame-preserve.patch` is **not** applied
by the ordinary `build-xwayland-private.sh`. On the pinned KGSL private
source it adds explicit `S28_KGSL_RESIZE_PRESERVE=1` handling for
_Mesa Vulkan XCB WSI-tagged_ resizable windows only. X11 `vkcube`
black-frame flicker during resizing was observed in both Vulkan Wrapper
and Turnip; the existing GPU framebuffer is captured to a private pixmap
before reallocating the X window, then scaled back to the new window
through GLAMOR/XRender to reduce the black gap before new Vulkan Present.

S28 is built into a **separate private Xwayland binary** rather than
overwriting the running S16/S27 build. The regular builder refuses to
rebuild the original binary while S28 source is active. The independent
S28 test uses only disposable rootful Xwayland `:201..:240`.

```sh
bash graphics/tools/build-s28-kgsl-resize-preserve.sh
bash graphics/tools/test-s28-build-and-scope.sh
python3 graphics/tools/test-s28-kgsl-resize-preserve.py --wm
```

The private `start-s28-kgsl-resize-preserve.sh --restart` command performs
an **explicit** user-initiated XFCE restart, and `--restore` returns to
the original private Xwayland. Real-world flicker with XFWM4 and the
Android screen is still **USER_VISUAL_TEST_REQUIRED**, as isolated XFWM4
sampling showed improvement but **not zero black events**. See
`graphics/docs/gpu-s28-kgsl-xcb-resize-flicker.md`.

### S29 native-pixel Vulkan window preservation (Adreno 840, candidate)

`kgsl/0006-kgsl-native-pixel-resize-preserve.patch` applies **after**
S28's 0005 only in the separate private KGSL source. It replaces S28's
bilinear stretch and repeated old-image tiles with an exact **1:1
centered previous frame** and a GPU-sampled 1x1 background color fill
for exposed right/bottom pixels. Gate: `S29_KGSL_NATIVE_RESIZE=1` and
Mesa Vulkan XCB `_MESA_DRV` marker on the private rootful KGSL server.
No CPU pixel readback or system driver replacement is introduced.

```sh
bash graphics/tools/build-s29-kgsl-native-resize.sh
bash graphics/tools/test-s29-build-and-scope.sh
python3 graphics/tools/test-s29-native-geometry.py
python3 graphics/tools/test-s29-vulkan-resize-edges.py --wm --seconds 8
```

The S16/S27 and S28 private binaries are retained with fixed SHA256;
the new S29 executable is linked in a different project-private
directory. Session switch is **only** via explicit
`start-s29-kgsl-native-resize.sh --restart`, with `--restore-s28`
rollback. In Xfwm4 live Vulkan, right/bottom black pixels were greatly
reduced but not eradicated; synthetic no-Present Xfwm4 still exposes a
lower black region. **Physical Android visual testing is required.**
See `graphics/docs/gpu-s29-pixel-exact-vulkan-resize.md`.

### S30 tiny AHB & genuine Present-safe KGSL frame (candidate only)

`kgsl/0007-kgsl-tiny-ahb-stable-xcb-present.patch` is applied **after**
S29's 0006 patch, exclusively for a separately linked S30 private
Xwayland. It addresses two different real S29 regressions: 1×98/40×98
AHB DRI3 `BadMatch` when both Qualcomm gralloc native FDs have identical
rounded size, and a stale Xwayland resize placeholder overriding genuine
client DRI3 Present pixmaps or being repeatedly cropped from the
full-screen rootful pixmap.

The size-tie FD[0] import is opt-in (`S30_KGSL_TINY_AHB_FD0=1`) and only
permitted for BGRA8, width 1–64, **two distinct FDs with the same valid
size** on the tested S26U KGSL path. For Vulkan-resize placeholders,
`S30_KGSL_STABLE_FRAME=1` enables a single GPU-sampled **window-local**
frame that survives multiple resize notifications, never paints over a
real imported DRI3 image, and is discarded only after actual client
flip/copy Present. GPU copy remains; this is not zero-copy.

```sh
bash graphics/tools/build-s30-kgsl-stable-resize.sh
bash graphics/tools/test-s30-build-and-scope.sh
python3 graphics/tools/test-s30-native-stable-frame.py
python3 graphics/tools/test-s30-extreme-resize.py --mode baseline --wm --expect-baseline-failure
python3 graphics/tools/test-s30-extreme-resize.py --mode candidate --wm --seconds 8
```

The ordinary private KGSL builder rejects S28/S29/S30 overlays **before
any patching**. The S30 binary is SHA256-pinned, retains all older
project-private servers unchanged, and is selected only with an
**explicit** native Termux `start-s30-kgsl-stable-resize.sh --restart`.
Use `--restore-s29` to roll back. Isolated Xfwm4+Vulkan and Zink
survival, animation after extreme resizes, and native-pixel geometry
PASS; a never-Presenting synthetic window under Xfwm4 and rare black
edges are still unresolved. User physical-screen acceptance is pending.
See `graphics/docs/gpu-s30-tiny-ahb-and-stable-present.md`.

### S31 adaptive GPU frame retention and Present size-gap clearing (opt-in)

`kgsl/0008-kgsl-adaptive-resize-present-extent.patch` applies after
S30's `0007` **only** to a separate private Adreno KGSL Xwayland.
`S31_KGSL_RESIZE_CONTENT_POLICY=3` permits native-pixel reuse only if
client/window dimensions are sufficiently close (~10%). It does not
bilinearly stretch a previous frame or tile old gear pixels. On an XCB
Present **copy** with a stale source pixmap, it GPU-clears the uncovered
right/bottom region; a grossly mismatched source is instead replaced by
the client's last-known background color until the next valid Present.
The Xorg-owned window is the only destination; the client's imported
DRI3 GPU image is **never modified**. Existing S30 tiny Qualcomm AHB
2-FD fallback is retained. This is not end-to-end zero-copy.

```bash
bash graphics/tools/build-s31-kgsl-resize-continuity.sh
bash graphics/tools/test-s31-build-and-scope.sh
python3 graphics/tools/test-s31-resize-continuity.py --pattern gentle --demo vkcube --wm
python3 graphics/tools/test-s31-resize-continuity.py --pattern abrupt --demo zink --wm
```

S31 is **not** promoted into the normal `awstart` or regular private
Xwayland builder, which refuses experimental source overlays early.
The pinned S30 binary and all earlier private/system binaries remain
available. The native Termux `start-s31-kgsl-resize-continuity.sh
--restart` explicitly switches the project XFCE, while `--restore-s30`
rolls back. Physical user-screen acceptance is still required: abrupt
resizes may momentarily show the background rather than stretched/cropped
old client content. See
`graphics/docs/gpu-s31-adaptive-resize-and-present-extent.md`.

### S32 — undo S31 whole-window blanking after a real Present

`kgsl/0009-kgsl-nonblank-present-continuity.patch` applies **only after
0008**, to a separately linked S32 private Adreno KGSL Xwayland. S31
wrongly filled the entire already-rendered Vulkan/GLX client window with
a background color when a source pixmap extent was stale; physical user
testing exposed a missing cube and gear flickering. S32
(`S32_KGSL_NO_BLANK_PRESENT=1`) instead preserves the client's real
rendered region and initializes only newly exposed right/bottom strips.
The window-local last real GPU snapshot is kept until an **extent-matched
actual Present**; the server does not replay a snapshot after Present,
which proved to increase black-edge artifacts. A runtime guard prevents
writing into imported client DRI3 images. S30's tiny AHB FD correction
remains required (`S30_KGSL_TINY_AHB_FD0=1`). Other GPUs, Android helper,
system Termux-X11 and global Mesa/Vulkan remain untouched.

```bash
bash graphics/tools/build-s32-kgsl-nonblank-present.sh
bash graphics/tools/test-s32-build-and-scope.sh
python3 graphics/tools/test-s32-nonblank-resize.py --mode candidate --pattern abrupt --demo vkcube --wm
python3 graphics/tools/test-s32-nonblank-resize.py --mode candidate --pattern abrupt --demo zink --wm
```

The actual XFCE is not restarted by any of those build/tests. Only an
**explicit native Termux**
`start-s32-kgsl-nonblank-present.sh --restart` switches to the pinned
S32 server, and `--restore-s30` returns to the previous S30 private
baseline. **Physical Android quality is not yet accepted.** There are
still intermittent edge artifacts; do not claim pixel-perfect or
zero-flicker gameplay. See
`graphics/docs/gpu-s32-nonblank-present-continuity.md`.

### S36 — KGSL root GLAMOR directly backed by Android AHB (candidate)

`kgsl/0010-kgsl-rootful-ahb-direct-output.patch` is **not applied by
the ordinary KGSL Xwayland builder**. A separate S36 build applies it to
an isolated copy of the pinned S32-era private GLAMOR source, then links
that isolated Xwayland executable with the S32 window/Present objects,
the AHB image-FD validator and the existing Alroot Wayland custom
`alroot_gpu_buffer_v1` client protocol. Global GLES/Vulkan and system
Termux-X11 binaries are never rebuilt or overwritten.

Only explicit `S36_KGSL_ROOT_AHB_OUTPUT=1` on S26 Ultra / Adreno KGSL
rootful, 1920×1080 and 24-bit output uses the special allocation.
The **GLAMOR pixmap is already an AHB-backed GPU texture**, so Xfwm4
composes straight into it, and the Android host imports the same storage
into its existing Vulkan VkImage/desktop GPU-composition path. No second
GLES output blit, host VkBuffer staging or CPU pixel copies. Producer
GPU `glFinish()` and final Vulkan GPU composition remain; this is not
direct scanout or end-to-end zero-copy. Other sizes/vendors/rootless
surfaces use the original DMA-BUF transport.

The opt-in S36 has passed full-HD real AHB pixel identity, actual Android
VkImage import and disposable Xfwm4 `vkcube`/Mesa Zink `glxgears`
survival/animated-content tests; **actual Android physical visual quality
is still unverified**. Keep the existing S32/S30 rollback and do not
promote S36 to default.

```bash
bash graphics/tools/build-s36-rootful-ahb-output.sh
bash graphics/tools/test-s36-build-and-scope.sh
python3 graphics/tools/test-s36-rootful-ahb-output.py --mode candidate --demo vkcube --pattern extreme --wm --seconds 7
python3 graphics/tools/test-s36-rootful-ahb-output.py --mode candidate --demo zink --pattern abrupt --wm --seconds 8
```

See `graphics/docs/gpu-s36-direct-rootful-ahb-output.md` for the exact
private binary hash, explicit native Termux session switch, physical
acceptance, and `--restore-s32`/`--restore-s30` rollback commands.

### S37 — S26U thin-height AHB FD tie and Vulkan DRI3 BadMatch 11

`kgsl/0011-kgsl-thin-ahb-fd0.patch` applies **after S36's 0010**
only to a separate S37 Bionic private Xwayland. S36's rootful AHB
output remains intact. The user's `vkcube` 267×19 / 826×8 resize
crashed because S30's gralloc FD0 tie exception covered **narrow width**
but not **narrow height**. Actual S26U BGRA8 fd0 and metadata fd1 both
have `st_size=28672` for 267×20, but distinct inodes, so the original
size-only heuristic rejected them and Vulkan XCB DRI3 returned BadMatch.

`S37_KGSL_THIN_AHB_FD0=1` permits Qualcomm fd0 **only** when width is
over 64, height 1–128, exactly two same-sized but distinct-inode FDs,
BGRA8 format, acceptable stride/padding and backing ≤2MiB. The vendor
DMA-BUF reported `st_mode=0600` and must not be rejected by a generic
`S_ISREG` check. All unsupported cases fail closed, and other vendors,
installed Termux-X11, system Vulkan and the user's existing S36 binary
are not modified.

```bash
bash graphics/tools/build-s37-kgsl-thin-ahb.sh
bash graphics/tools/test-s37-build-and-scope.sh
python3 graphics/tools/test-s36-rootful-ahb-output.py --s37-thin-ahb --mode candidate --pattern thin --demo vkcube --wm --seconds 7
```

The exact failed dimensions were reproduced on the previous S36 and
successfully imported on S37, which survived 39 repeats and resumed
animated Vulkan after restoring 500×500. This does **not** resolve
the physical Android display's gear trails/black borders or prove
complete glibc ICD compatibility. `start-s37-kgsl-thin-ahb.sh` requires
explicit `--restart` from native Termux, with `--restore-s36` and
`--restore-s32` rollback. See
`graphics/docs/gpu-s37-thin-height-dri3-ahb-crash.md`.

`S17_MATRIX_MODE=smoke bash graphics/tools/test-s17-zink-matrix.sh` checks
Termux wrapper + Zink, Termux Turnip + Zink and Debian Turnip + Zink on
isolated Xwayland, validating runtime execution, AHB/ABI branch, X11
responsiveness and cleanup. All three passed on S26U. See
`graphics/docs/gpu-s17-zink-three-backend.md` for the exact validation,
remaining full desktop acceptance gates and safe manual restart.
