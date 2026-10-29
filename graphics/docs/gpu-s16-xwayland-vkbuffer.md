# GPU-S1.6 — Isolated Xwayland GLAMOR via Vulkan `VkBuffer` GPU-copy

**Status:** Host implementation and GPU stage-copy acceptance: **PASS at source/isolated GPU proof level**. **Installed Android compositor / Xwayland visible GLX presentation: NOT TESTED with this code**. Source branch: `feature/gpu-s16-xwayland-glamor-vkbuffer`. Existing signed helper APK, XFCE `:90`, Termux-X11 `:0`, system Mesa and S1.5 Wayland Vulkan/AHB paths remain untouched.

## Reason for this path

On S26U (Adreno 840), the previously compiled private 24.1.14 Xwayland already creates a KGSL surfaceless GLAMOR EGL context, exposes DRI3, GLX and Present, and produces Wayland `linux-dmabuf` LINEAR `wl_buffer`s. Both default Mesa26 and private Mesa27 initialize GLAMOR. However *the current installed Android compositor* rejects the LINEAR FD as a Vulkan `VkImage`, logging `VK_ERROR_INVALID_EXTERNAL_HANDLE`. Termux GLX clients time out. The private headless/non-glamor control exposes GLX without actual accelerated frame output and is not an acceptance success. The external Vulkan-image import capability test separately determined the vendor driver does not advertise `VK_EXT_image_drm_format_modifier` nor a supported DMA-BUF image import contract.

Prior S18 testing proved **real** Android Vulkan `VkBuffer` direct DMA-BUF import and GPU compute reads (even full-frame hash) from glibc Turnip; S16 now specifically probes *the same origin as a KGSL GLAMOR pixmap*: a Termux Mesa KGSL EGL texture exported through `EGL_MESA_image_dma_buf_export`. That real exported FD is readable as an Android vendor Vulkan `VkBuffer` without copying pixels on CPU. This is not a Bionic *memory broker/server*. All tests are one-shot programs, and the producer uses its own GPU allocation.

## Implementation

`native/waylandcomp/src/vk_present.c` implements a **capability-gated, single-plane, 32-bit LINEAR-only** `VkBuffer` fallback when the compositor's Vulkan driver lacks an external DRM modifier VkImage path. It:

1. Enforces supported ARGB/XRGB/ABGR/XBGR fourcc, LINEAR modifier, zero offset, valid four-byte stride and physical FD size; rejects multi-plane/unsupported layouts.
2. Checks the **exact** `VK_BUFFER_USAGE_TRANSFER_SRC_BIT` `VkPhysicalDeviceExternalBufferProperties`, requiring `IMPORTABLE`; duplicates and imports the *same* kernel DMA-BUF FD via `VkImportMemoryFdInfoKHR` using intersected memory types, checking `VkMemoryDedicatedRequirements.requiresDedicatedAllocation==0` and `VkBuffer` requirement `<= fstat(fd).st_size`. No Bionic AHB allocator or Vulkan image external-import workaround.
3. Allocates a **host-owned, normal Vulkan optimal-tiled `VkImage`** for the Stage 1 GPU-copy composition path; imports source FD as a `VkBuffer` only. `vkCmdCopyBufferToImage` honors EGL producer's real row pitch, performed in the compositor's command buffer ahead of scene/blit. No CPU pixel mapping or intermediate CPU copy.
4. Keeps existing AHB `VkImage` GPU-copy first and does not reinterpret AHB buffers or multi-plane formats as generic VkBuffers. Preserves old Vulkan DRM `VkImage` import for drivers that advertise it.
5. Manages imported `VkBuffer` and memory with the existing `vkp_image` lifetime. The source is not marked a scanout-ready external `VkImage`; no direct layer or zero-copy scanout claim.

### Important EGL/Vulkan synchronization distinction

- The Xwayland KGSL EGL texture exporter calls `glFinish()` before `wl_buffer` is submitted. This provides initial correctness but costs CPU blocking and is **not** the final explicit-fence design.
- An initial isolated copy proof erroneously attempted Vulkan `VK_QUEUE_FAMILY_EXTERNAL` acquire for an EGL-originated buffer, which has **no matching Vulkan release**, and the GPU copied zeros. Switching to `VK_QUEUE_FAMILY_IGNORED` for this specific EGL path made the GPU-copy and independent direct SSBO re-read both pass. This is a **device-tested implicit synchronization assumption**, not proof of vendor-independent explicit synchronization. When the producer is native Vulkan it may use a different queue-family transfer contract.
- `vkQueueWaitIdle` exists in the one-shot standalone proof (to validate pixels); the *host implementation* records source GPU-copy in the normal scene command buffer instead of synchronously submitting and waiting for an extra copy.

### Proven functional GPU copy

`graphics/tools/test-s16-xwayland-gpu-copy.sh` builds tracked `graphics/tests/s16-kgsl-to-vkbuffer-gpucopy.c`, which creates a genuine Mesa KGSL GL texture, GPU clear (not CPU upload), EGL DMA-BUF export and directly imports this very FD in Android Vulkan `VkBuffer`. It executes `vkCmdCopyBufferToImage` into an optimal VkImage and verifies output GPU pixels using a *small readback for diagnosis only*.

| GL texture size | EGL DMA-BUF modifier | VkBuffer import | GPU VkBuffer→VkImage copy | GPU pixel check |
|---|---|---|---|---|
| 640×360 | LINEAR | PASS | PASS | 5/5 PASS |
| 800×600 | LINEAR | PASS | PASS | 5/5 PASS |
| 1280×720 | LINEAR | PASS | PASS | 5/5 PASS |
| 1920×1080 | LINEAR | PASS | PASS | 5/5 PASS |

**Known size failure:** for a native 1024×768 EGL GL texture, KGSL returned `fstat(fd)=3,145,728 B`, while Android vendor `VkBuffer` requires `3,151,028 B`. The importer refuses insufficient buffers, rather than overreading or violating Vulkan allocation size. `graphics/tests/s16-kgsl-to-vkbuffer-gpucopy.c` at 1024×768 returns failure as expected. Future generalized texture producer allocation/padding or an entirely different output buffer strategy is needed for such sizes. This is **not resolved** by simply relaxing the check.

### Existing live Xwayland diagnostics (old host, before installing S16)

Isolated opt-in Xwayland at `:241`, attached to Alroot `wayland-0`, no impact on `:90`:

- `XWAYLAND_FORCE_KGSL_SURFACELESS=1`, `FD_KGSL_ENABLE_DMABUF=1`, `MESA_LOADER_DRIVER_OVERRIDE=kgsl`, `-glamor es`, private patched Xwayland 24.1.14: **DRI3/GLX/Present all enabled**, real `linux-dmabuf` `wl_buffer` created, Android host import FAIL (`INVALID_EXTERNAL_HANDLE`), `glxinfo -B` and `glxgears -info` time out. Mesa26 and private Mesa27 both reproduce.
- Generic Xwayland `-glamor off` control: GLX present but DRI3 absent, `glxinfo -B` reports Zink (not on-screen proof), glxgears fails `GLXBadCurrentWindow`.
- The running user's `:90` uses global Termux Xwayland without DRI3. **These are not yet a live S16 host A/B test**.

## Run and optional integration gates

Source/one-shot GPU acceptance only (S26U Termux, independently creates/destroys its own resources):

```bash
cd ~/alroot_wayland
ALROOT_S16_TEST_RESOLUTIONS=full bash graphics/tools/test-s16-xwayland-gpu-copy.sh
```

For an experimental future installed helper with the new host code, test the **direct** Alroot Wayland output `wayland-0` with a separately built KGSL-surface GLAMOR Xwayland. The normal `xfce-labwc` path currently runs **nested pixman Labwc**, which may not offer `linux-dmabuf` to Xwayland; use the direct compositor profile for the first GPU test, never force GLAMOR into the nested SHM software profile. A private Xwayland binary can be configured via the existing opt-in `ALROOT_XWAYLAND_BINARY` and `ALROOT_XWAYLAND_GLAMOR=es` launch options. Do not enable KGSL flags globally; Xclipse/ANGLE/Mali/PowerVR require their own proven EGL/GBM/Wayland export adapters.

**Before any visible-present acceptance**: build and install the tested S16 Android host (currently no SDK in Termux; CI GitHub Actions can build an opt-in branch), run actual private Xwayland GLAMOR on the new host, show and move glxgears/glmark2 in a real window; confirm GPU imported frame, window content/pixel correctness, resize, desktop session, `:0` DRI3 regression, 60/120Hz frame pacing, GPU fences. Until then label **GPU path integration staged, visible GPU present NOT_VERIFIED**.

**Strict GPU-copy vs zero-copy:** This S1.6 implementation is **GPU-copy from imported shared `VkBuffer` into host `VkImage`**. It removes the broken direct `VkImage` import and CPU pixel upload; it does **not** claim full end-to-end scanout zero-copy. Future Vulkan SSBO fragment composition can remove this *intermediate copy* once real on-screen output has been validated.

## 2026-10-30 continuation: real Debian 13 glibc GLX GPU execution

With the actual Alroot LD_PRELOAD runtime and a **private, never-installed**
Xwayland instance on `:241` connected to the existing `wayland-0` host,
Mesa Freedreno KGSL OpenGL works **inside Debian glibc**, not just in
Termux's Bionic client:

```
S16_PRIVATE_XWAYLAND_DRI3=1
S16_PRIVATE_XWAYLAND_GLX=1
S16_PRIVATE_XWAYLAND_Present=1
S16_GLIBC_GLXINFO_EXIT=0
S16_GLIBC_GL_RENDERER=OpenGL renderer string: Adreno (TM) 840
S16_GLIBC_GLXGEARS_FPS_SAMPLE=6153.501
S16_GLIBC_GLX_GPU_EXECUTION=PASS
S16_ANDROID_HOST_GPU_PRESENT=NOT_VERIFIED_OLD_INSTALLED_HOST
```

**Reproduce** from Termux, without installing the S1.6 APK or altering XFCE
`:90` / Termux-X11 `:0`:

```bash
python3 graphics/tools/test-s16-xwayland-glibc-glx.py
```

The diagnostic opens exactly one free private Unix socket `:241..:270`,
`chmod 0600`, TCP disabled, maps that socket into Debian's **temporary**
`/tmp/.X11-unix` (unlinked at exit), launches opt-in private KGSL GLAMOR,
then runs *actual* Debian `/usr/bin/glxinfo` and `/usr/bin/glxgears` through
`alroot_run.sh --raw` before cleanly terminating **only** the private test
server. It leaves the permanent X displays and users' Xauthority untouched.

**Security: test only.** This diagnostic invokes `-ac` **only on the temporary
private test display**, because glibc/X11 currently fails to send an
acceptable authentication record even when `libXau` can read and locate a
valid private FamilyWild cookie. The same cookie succeeds with Termux-native
`xdpyinfo`; the guest's raw socket path is accessible. **Never use `-ac` on
production `:90` or `:0`**, and do not claim secure production glibc X11
login is solved. The private test closes its socket and releases temporary
rootfs `/tmp` symlink immediately afterward. Investigating glibc libxcb/Xlib
automatic auth selection is a follow-on compatibility task.

The `glxgears` FPS is a diagnostic confirming the GPU GLX client executed,
not a visible screen-rendering FPS or a compositor frame pacing benchmark.
The **old installed** Android helper still refuses Xwayland's plain LINEAR
DMA-BUF as a Vulkan external `VkImage` (`INVALID_EXTERNAL_HANDLE`); signed
S1.6 host APK is staged in a separate local test file but was not installed.
Full GLAMOR → Android Stage-1 frame presentation remains unverified.
