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
