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
