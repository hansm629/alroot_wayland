# GPU-S1.4 — pinned Mesa 27.0.0-devel PR #25 revalidation

## Non-negotiable source contract

- User-selected Termux recipe: `lfdevs/termux-packages` PR #25.
- The inspected recipe branch HEAD was `d03437f224ce3a85cc7f719bd79bb9e5cc2e6237`.
- The recipe pins `lfdevs/mesa-for-android-container`, `dev/adreno-main`,
  `_COMMIT=6f90e92dfdacc4759d8e9ccb328805d0f0b4439b`.
- Mesa's pinned `VERSION` file contains `27.0.0-devel`.
- All **14** currently selected package `.patch` files are copied verbatim
  from that PR head into `graphics/patches/mesa/pr25/`, with fixed hashes in
  `SHA256SUMS`. The original 26.3 Alroot Zink/Kopper patches remain separate.

## What is (and is not) reproduced

Build uses a private **source overlay** in
`./.source/gpu-stage27-pr25/mesa-private/source`, with the exact pinned
Git tree retained unmodified under `.source/gpu-stage27-pr25/source`.
Nested source symlinks are replaced with private copies *before* applying
package/Alroot patches. No `/data/data/com.termux/files/usr` package, ICD,
EGL, GBM, DRI, Termux-X11 server, helper APK or XFCE session is changed.

The package's Android `0012-always-use-software-for-swrast.patch` conflicts
with the already validated Alroot DRM-less Kopper path in
`platform_wayland.c`. Its X11 hunk is applied verbatim; on Android the
Wayland device setup is `dri2_setup_device(..., true)` as in PR #25,
while the non-Android Kopper fallback keeps `ForceSoftware || (Kopper &&
render_fd < 0)`. This merge is explicit in the build script and **must**
be reviewed whenever the pinned source commit changes.

This is a **minimal device-local GPU validation build**, not a byte-for-byte
Termux `.deb` rebuild. It uses `-Dgallium-drivers=zink,freedreno`,
`-Dvulkan-drivers=freedreno`, `-Dfreedreno-kmds=msm,kgsl` and disables
unrelated VA/LLVM-based/Rusticl components. In particular the PR's host
`mesa_clc` build for full packages is not performed. This S26U runs
Android API 37; the private test uses Android API 35 to compile
`sync_merge`, avoiding the Termux compiler's default API 24. No public
APK minSdk or system toolchain flag is modified.

## Commands (non-destructive)

    cd ~/alroot_wayland
    bash graphics/tools/check-s14-mesa27-pr25.sh
    bash graphics/tools/build-s14-mesa27-private.sh
    bash graphics/tools/build-s14-mesa27-turnip.sh

Then verify each GPU driver separately:

    bash graphics/tools/test-s14-mesa27-wayland-egl.sh zink 90 wrapper
    bash graphics/tools/test-s14-mesa27-wayland-egl.sh zink 90 turnip
    bash graphics/tools/test-s14-mesa27-wayland-egl.sh kgsl 24 none
    ALROOT_S14_USE_KGSL_SHIM=1 bash graphics/tools/test-s14-mesa27-wayland-egl.sh kgsl 90 none
    bash graphics/tools/test-s14-mesa27-turnip.sh wayland 60
    bash graphics/tools/test-s14-mesa27-turnip.sh xcb 60
    bash graphics/tools/test-s14-mesa27-xwayland.sh
    bash graphics/tools/test-s14-mesa27-acceptance.sh

The KGSL+AHB shim is strictly opt-in and still does an internal **GPU blit**;
the 27-specific five-point GPU-memory identity preflight must pass before
submitting to the host. Missing AHB export, unsupported Vulkan WSI,
missing DRI3, or failed live host GPU import are `NOT_CONFIRMED`/`FAIL`,
never quietly upgraded to a PASS based solely on `eglSwapBuffers` or
`vulkaninfo`. Ordinary unmodified KGSL EGL and GLX may still choose
Linux DMA-BUF, which the Android display driver may reject. The test
separates Vulkan/Zink private wrapper vs pinned Turnip to avoid
attributing wrapper successes to Mesa 27 Turnip.

Static source audit: at the pinned commit, stock Mesa 27's
`src/vulkan/wsi/wsi_common_wayland.c` uses `zwp_linux_dmabuf_v1` and
`wl_shm`; there is no `alroot_gpu_buffer_v1` implementation in its
Vulkan WSI, EGL or Gallium tree. Therefore native Turnip 27 or Zink
using Turnip 27 is **not** automatically compatible with Alroot's AHB
transport. An actual host AHB registration and VkImage import must be
observed independently. Success with the separately patched private
Vulkan-wrapper WSI must never be attributed to native Turnip 27.

### Initial Xwayland observation

The live Alroot Xwayland `:90` advertises GLX and Present, but **not DRI3**.
A distinct `:0` Xwayland also advertises GLX/Present without DRI3 at
this test time. This blocks claiming Xwayland DRI3/GLAMOR end-to-end GPU
acceleration. All installed protected Termux renderer file hashes matched
on the first read-only check; the historic DRI3 snapshot expectation no
longer describes this particular running `:0` Xwayland session. Do not
restart a server merely to make the old snapshot pass.

## S26U actual GPU revalidation (2026-10-29)

**Private Mesa 27 build:** The pinned `6f90e92` checkout, the 14 verified
PR #25 packaging patches and the Alroot DRM-less Kopper adjustments compiled
and linked successfully as a separate Mesa 27 EGL/Gallium runtime. The
separate private Turnip Vulkan ICD also compiled and linked successfully.
The *old Mesa 26.3* `__emutls_v` DRI export patch must **not** be used with
this Android API 35 private build, because Clang emits native ELF TLS rather
than `__emutls_v` symbols. The 27 private build therefore retains upstream
ELF TLS symbol names. No installed GL/Vulkan/DRI file was changed.

| Client/driver on Adreno 840 | Actual result | GPU transport verdict |
| --- | --- | --- |
| Mesa 27 Zink + previously patched Vulkan wrapper | 360/360 EGL swaps; 4 AHB wl_buffers; zero SHM/DMA-BUF creations; host AHB imported as VkImage | **Wayland GPU-copy PASS** (wrapper, not Turnip) |
| Mesa 27 Freedreno KGSL + opt-in external EGL AHB shim | 360/360 swaps, 360/360 buffer releases, 3 AHB buffers, zero SHM/DMA-BUF; 5/5 cross-driver pixel samples | **Wayland GPU-copy PASS** (experimental shim) |
| Mesa 27 Freedreno KGSL normal Wayland EGL | 24/24 swaps; 5 DMA-BUF creations, zero AHB; no host Vulkan GPU import | **Not confirmed**; no AHB producer integrated in stock KGSL EGL |
| Mesa 27 Zink + **Mesa 27 Turnip** | 90/90 EGL swaps; 4 DMA-BUF creations, zero AHB; host returns `INVALID_EXTERNAL_HANDLE` | **GPU-copy FAIL**, despite successful EGL swaps |
| Mesa 27 Turnip Vulkan Wayland `vkcube` | `vulkaninfo` identifies Turnip 27 / Adreno 840 / Vulkan 1.4.363; client exits 0 but host rejects DMA-BUF with `INVALID_EXTERNAL_HANDLE` | **Wayland GPU-copy FAIL** |
| Mesa 27 Turnip Vulkan XCB `vkcube`, existing Xwayland :90 | `Invalid physicalDevice` from Vulkan loader, process exit 134 | **XCB FAIL** (exact cause needs investigation) |
| Xwayland :90 and :0 | Both advertise GLX/Present but **DRI3=0**, private GLX renderer not confirmed | **DRI3/GLAMOR GPU NOT VERIFIED** |

The installed Mesa 26.3 package and protected driver/Xwayland binary
hashes remained unchanged. These results are process/transport/log-based:
the user has not yet visually inspected these *Mesa 27* animation windows,
so final on-screen color, crop and smoothness acceptance is still pending.

The matrix script intentionally returns zero after collecting failures,
but **does not mean all GPU tests passed**. Inspect
`S14_MESA27_OVERALL=PARTIAL_VALIDATION_NOT_GENERAL_WAYLAND_XWAYLAND_SUPPORT`
and each `S14_CASE_PROCESS_*=EXIT_*` result. A native Vulkan client exit
code of zero does not prove host GPU image import; the Turnip test now
returns exit 5 when the compositor rejects every frame.

### Remaining work separated by architecture

- **Turnip 27:** implement/port an Alroot Wayland-specific AHB WSI
  swapchain/present path, with capability negotiation, correct release
  synchronization and the current DMA-BUF fallback only where the host
  actually supports the FD import. The Vulkan wrapper already has a
  working AHB protocol adapter but **does not make Turnip 27 compliant**.
- **KGSL 27:** move the restricted `LD_PRELOAD` proof into a private,
  testable Mesa EGL/Gallium integration. Verify geometry/stride, resize,
  swap interval, renderbuffer lifetime, context changes and GPU fences
  across multiple Android gralloc implementations. Never report the
  current fixed-resolution shim as a general Mesa EGL implementation.
- **Xwayland:** independently enable and validate its actual DRI3/GLAMOR
  GPU buffer path on the Alroot Wayland host. Current GLX/Present extension
  advertisement is insufficient, and the live server currently lacks DRI3.
  Do not replace or restart the user's active servers for a diagnostic.
