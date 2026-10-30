# GPU-S1.6.3 — Xwayland GLAMOR right-half corruption: exact on-device GLES precision reproduction

**Date:** 2026-10-30. **Device:** S26 Ultra, Adreno 840, Termux Mesa Freedreno KGSL, private Xwayland 24.1.14, Android Vulkan Alroot host. **Branch:** `feature/gpu-s16-live-regression-fixes`.

## User symptom and decisive new clue

The Xwayland GLAMOR desktop is **1920×1080**. The **left half stays sharp**, while approximately x≥960 on the **right half** intermittently shows doubled, broken, aliased glyphs and window buttons (like poor-quality video encoding, though there is no video encoder in this rendering path). The user's screenshot shows UI corruption particularly in the terminal text and XFCE button glyphs. This is **not** adequately explained by a generic DMA-BUF memory-import failure: 1920×1080 import and complete gradient copies succeed on the same phone.

The pinned Xorg 24.1.14 private KGSL Xwayland's shader header `glamor/glamor_priv.h` provides:

```c
#define GLAMOR_DEFAULT_PRECISION \
    "#ifdef GL_ES\n" \
    "precision mediump float;\n" \
    "#endif\n"
```

This `GLAMOR_DEFAULT_PRECISION` is included in **GLAMOR XRender compositing fragment shaders** (`glamor_render.c`) and primitive fill/texturing shaders (`glamor_program.c`). On Adreno 840, actual GLES reports **10 bits mediump float** and **23 bits highp float**. When a 1920-pixel-wide texture is sampled with **mediump-normalized texture coordinates**, coordinate resolution becomes insufficient as UV x grows beyond ~0.5 (x ≈ 960). This produces the observed **left-clear, right-corrupt** split. It is a rendering precision issue *before the correctly functioning DMA-BUF VkBuffer→VkImage GPU copy*.

## Actual GPU and XRender A/B — source bug reproduced and corrected

1. `graphics/tests/s16-glamor-precision-half.c` creates a 1920×8 alternating 1-pixel RGB texture and samples it into a GPU FBO using GLES3 fragment shader texture coordinates at **mediump vs highp**. It reads back the diagnostic target for **every** pixel (no real application/screen capturing):

   | GLES source shader | 960px left half (7,680 pixels) | 960px right half (7,680 pixels) | First bad x |
   | --- | ---: | ---: | ---: |
   | mediump | **0 wrong** | **3,584 wrong** | **961** |
   | highp | **0 wrong** | **0 wrong** | none |

2. Stronger: `graphics/tests/s16-glamor-xrender-wide.c` creates a synthetic 1920×64 X11 picture, draws an **XRender PictOpOver texture composite** through the **real GLAMOR-accelerated private Xwayland**, then reads the *generated test pixmap* with `XGetImage`. On the same S26U, we built two private Xwayland binaries that differ only in the single shader precision line above. Both have the identical prior KGSL DMA-BUF, full-pixmap/damage, and rootful-lock patches:

   | Private Xwayland 24.1.14 shader | 960px left half (61,440 pixels) | 960px right half (61,440 pixels) | First bad x |
   | --- | ---: | ---: | ---: |
   | **mediump** (original) | **0 wrong** | **28,672 wrong** | **961** |
   | **highp** (patched) | **0 wrong** | **0 wrong** | none |

   Both tests use actual Adreno 840 GLES, real XRender, and real GLAMOR, **not a synthetic software precision simulation**. Repeating 1920×1080 private rootful Xwayland also passed DRI3, GLX, actual Android host Vulkan `VkBuffer` GPU-copy logs, and isolated X socket cleanup. The actual running XFCE `:92` was not restarted for this proof.

3. **Negative control on Vulkan copy**: `S16_GRADIENT_TEST=1 S16_GPU_SYNC_FD_WAIT=1 ALROOT_S16_TEST_RESOLUTIONS=full` with `test-s16-xwayland-gpu-copy.sh` renders a GPU-originated **nonuniform spatial gradient** through EGL DMA-BUF → Android Vulkan VkBuffer → VkImage. All **2,073,600 pixels** of 1920×1080 matched exactly (top orientation; 0 wrong). Therefore that standalone copy's linear pitch and GPU memory import are sound; the corruption is introduced in GLAMOR's medium-precision texture sampling, not from the frame-wide buffer-copy byte layout in the tested path.

## Applied source fix

`graphics/patches/xwayland/kgsl/0003-kgsl-glamor-highp-texcoords.patch` changes **only** `GLAMOR_DEFAULT_PRECISION` to `"precision highp float;\n"` inside the **private KGSL build**. It does not patch the global Termux `Xwayland`, generic Xwayland build, Android Vulkan vendor driver, Turnip ICD, AHB WSI, or app code. On this tested Adreno 840/GLES3 renderer, highp is confirmed supported. Other GPU families must use their own validated policy rather than assuming every GLES2 GPU supports fragment highp.

**Full private acceptance** (Termux native, does not restart user XFCE):

```bash
cd ~/alroot_wayland
ALROOT_XWAYLAND_VARIANT=kgsl bash graphics/tools/build-xwayland-private.sh
bash graphics/tools/test-s16-kgsl-highp.sh
```

The test compiles the 1920×8 mediump/highp GLES proof and 1920×64 real XRender proof, starts a disposable rootful `:201..:240` server with screen size 1920×1080 on the running Alroot Wayland host, and checks the Android Vulkan host copy log. If an independently saved old mediump binary exists in the ignored local test scratch area, it also performs the deliberately failing old-code XRender negative control. No user windows are captured. Without a saved old binary, that optional control is skipped.

## Installation, rollback and remaining gates

**The shader fix is in the Termux-private Xwayland binary, not the Android APK.** Rebuild the private KGSL Xwayland once (already built on the current S26U). The **existing active `:92` Xwayland process still has its old GLES shader programs**; the new binary only takes effect after a user-approved desktop restart. Do not mistakenly reinstall an APK to apply only this Xwayland shader fix. The staged newer Android APK is an independent mouse-coordinate and producer sync change and can be tested separately, so that two variables are not changed in one A/B attempt.

When restarting with the user's consent and after saving open files, use the existing **private KGSL** `ALROOT_XWAYLAND_BINARY` startup profile. Compare static terminal/Blender glyphs and title buttons on **both sides of x=960**, scroll/move windows, and monitor drift. **On-screen XFCE visual PASS remains pending the user's physical observation**; desktop-session pointer drift is a separate issue (the d7f5833 Android code changes).

A performance optimization may later revisit the separate full-pixmap damage preservation patch. This shader highp A/B was controlled with that patch identically present in both builds; highp alone is the decisive source variable.
