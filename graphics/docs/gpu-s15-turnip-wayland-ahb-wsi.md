# GPU-S1.5 — Mesa 27 Turnip Wayland AHardwareBuffer WSI

## Pinned source / isolation

- Mesa `27.0.0-devel`, exactly lfdevs PR #25 `6f90e92dfdacc4759d8e9ccb328805d0f0b4439b`.
- PR #25's 14 source patches and S1.4 private Android API 35 build are retained.
- New S1.5 sources are generated in **ignored** `.source/gpu-stage27-pr25/s15-turnip/source` from the previously patched S1.4 private overlay. Never patch the pinned Git source, installed Termux Mesa 26.3, Vulkan ICDs, Xwayland, or APK.
- Build: `bash graphics/tools/build-s15-turnip27-ahb-wsi.sh`.
- Test: `bash graphics/tools/test-s15-turnip-wayland-wsi.sh 120`, followed by `bash graphics/tools/test-s15-zink27-turnip-ahb.sh 120` (both require the private ICD).

## Architecture and expected cost

```text
Vulkan application (Turnip 27, private ICD)
  → application's optimal VkImage (normal Vulkan swapchain image)
  → Mesa WSI CmdCopyImageToBuffer GPU copy
  → AHB's real gralloc GPU buffer memory (DMA-BUF memory imported in the same Vulkan process)
  → alroot_gpu_buffer_v1 Wayland wl_buffer (public NDK handle socket transfer)
  → Alroot host imports same AHB as VkImage
  → host Vulkan GPU composition/copy to Android surface
```

This is a **GPU-copy** implementation with TWO copies: client VkImage → AHB memory, then host AHB → surface. No CPU-pixel staging or fake DRM buffer. CPU fence completion waits remain deliberately until the AHB protocol gains explicit synchronization. There is **no end-to-end zero-copy or direct scanout** claim.

`graphics/patches/mesa/turnip/wsi_alroot_ahb.c` implements the AHB allocator/FD selection/import for WSI, while `graphics/tools/prepare-s15-turnip-wsi-overlay.py` applies the Vulkan WSI changes to a *private* Mesa source overlay. The Vulkan WSI code uses the shared AHB handle protocol and Vulkan `VK_EXT_external_memory_dma_buf`, not any Qualcomm-specific submit command or layout. A future Xclipse/RADV implementation can reuse the WSI *design*, but glibc needs an Android/Bionic-side AHB allocator/handle transport bridge; it cannot simply `dlopen` Android's Bionic `libnativewindow.so` from glibc.

The driver must support the actual memory import and the gralloc format/stride; an extension's name alone does not guarantee that. The current S26U test has a dynamic `AHardwareBuffer_getNativeHandle` lookup and conservative image-FD size selection. This is **not** a stable public NDK ABI and is intentionally opt-in, single-plane RGBA8, S26U-tested only. BGRA8, differing gralloc allocation formats, multisampling, device loss, swapchain resize, and other GPUs require separate validation. No impact to existing Termux-X11 DRI3 should occur.

## Proven pre-WSI memory experiment

A standalone physical cross-driver experiment using the **unmodified pinned Turnip 27 ICD** successfully:

- Allocated real AHB RGBA8 of 480×270, 480×320, 640×360, 800×600 on S26U;
- Imported its exact GPU memory FD using `VK_KHR_external_memory_fd` + `VK_EXT_external_memory_dma_buf`;
- Bound Vulkan buffer memory and submitted `vkCmdFillBuffer`;
- Waited a GPU completion fence;
- Compared five tiny diagnostic pixels in Android vendor EGL: **5/5 MATCH** at each tested resolution.

One surprising incompatibility remains: `vkGetPhysicalDeviceExternalBufferProperties` reports non-importable for that handle type (0 feature bits), yet the actual Vulkan FD import and writes work on this device. The direct diagnostic must use `ALROOT_S15_DIAGNOSTIC_TRY_UNADVERTISED_IMPORT=1` and **must not be treated as universal Vulkan-conformant import support**. Production WSI negotiation must have a reliable support probe or driver fix; do not silently enable the path on arbitrary hardware.

## WSI acceptance criteria

Only mark the Wayland WSI as a **technical GPU-copy PASS** when all are true: exact private Turnip 27 ICD and Adreno 840 GPU are identified; `vkcube` processes frames; Wayland wire shows `alroot_gpu_buffer_v1.create_buffer` and zero `zwp_linux_buffer_params_v1` or `wl_shm_pool` buffer creations; the Alroot host logs actual AHB→VkImage import for the test window; graphics errors, hangs, and leaks are absent. Then request human visual inspection of the animation before calling end-to-end presentation PASS. A successful `vkcube` process exit **alone is not enough**—the stock Turnip WSI submitted unimportable DMA-BUF despite exit code 0 in S1.4.

The WSI is enabled only for a process that specifies `ALROOT_TURNIP_WAYLAND_AHB=1` **and** connects to a compositor advertising `alroot_gpu_buffer_v1`. All other Wayland servers retain their previous DRM/SHM WSI paths.

The host's v1 protocol transfers **RGBA8** via `create_buffer`; v2 can
transfer **BGRA8** via `create_buffer_with_format(format=5)`, but only on
hosts that advertise protocol version 2. The WSI retains RGBA8 as the
first preference for native Vulkan `vkcube`, while Mesa 27 Zink/Kopper
selects `VK_FORMAT_B8G8R8A8_UNORM` and maps it to gralloc BGRA8. Before
enabling this v2 path, a standalone real Turnip GPU fill was verified
against the vendor Android EGL for all five sample points of the same
AHB (640×360); BGRA8 yielded `(229,178,26,255)` after byte-order
conversion. No CPU pixel reading occurs in the actual WSI present path.

### Actual S26U Vulkan/Wayland results (2026-10-29)

The private Turnip 27 AHB WSI was built and linked successfully. An early
private build crashed during Vulkan device creation: its source overlay
preserved an older `wsi_common.h` modification time, so Ninja did not
recompile `tu_autotune.cc`/`tu_cmd_buffer.cc` after a `wsi_device` ABI
change. That field was removed, and the overlay preparation script now
preserves timestamps only when the *bytes* match. A complete rebuild of
the affected Turnip C++ objects restored `vulkaninfo` device enumeration
to Adreno 840 / Vulkan 1.4.363 / Mesa 27 `6f90e92`.

| Test | Client | Wayland wire | Host |
| --- | --- | --- | --- |
| Native Turnip 27 `vkcube` FIFO, **360 frames** | Exit 0 | 8 RGBA8 AHB buffers; 0 DMA-BUF; 0 SHM | **Actual AHB→VkImage import PASS** |
| Native Turnip 27 `vkcube` MAILBOX, **90 frames** | Exit 0 | 8 AHB buffers; 0 DMA-BUF; 0 SHM | **Actual AHB→VkImage import PASS** |
| Mesa 27 Zink+Kopper on private Turnip 27, **360 EGL swaps** | 360/360 PASS | 4 BGRA8 AHB v2 buffers; 0 DMA-BUF; 0 SHM | **Actual AHB→VkImage import PASS** |
| Same ICD, `ALROOT_TURNIP_WAYLAND_AHB` unset | Existing normal Wayland path | 0 AHB; 8 DRM DMA-BUF | Unmodified fallback (not supported by current host import) |
| Prior S1.4 Zink+proprietary wrapper, 12 swaps | PASS | AHB | PASS |
| Prior S1.4 KGSL+AHB shim, 12 swaps | PASS | AHB | PASS |

For the 360-frame native Turnip and Zink tests, `wl_buffer.release` was
observed 358 times each; the remaining in-flight buffers were released
on swapchain/window teardown. The host subsequently reported
`AHB wl_buffers live=0, created=95, freed=95` with no cumulative buffer
accounting leak in this test session. All protected installed Termux
renderer files had unchanged SHA256 hashes, and the pinned source Git
checkout stayed clean. Physical user visual confirmation of the new
Turnip-native animations remains **USER_CHECK_REQUIRED**.

For full automated validation (including FIFO/MAILBOX, Zink+Turnip and
previous Zink+wrapper/KGSL regressions):

    cd ~/alroot_wayland
    bash graphics/tools/test-s15-acceptance.sh

## Stage result

The source and test tools distinguish build, Vulkan renderer, Wayland wire, host GPU import, and human visual verification. Fill actual outcomes only from successful completed runs, not assumptions.
