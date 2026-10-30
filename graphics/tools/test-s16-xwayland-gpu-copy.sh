#!/data/data/com.termux/files/usr/bin/bash
# GPU-S1.6 isolated Xwayland GLAMOR export -> Android Vulkan VkBuffer GPU-copy
# acceptance (one-shot EGL+Vulkan probe, not a live Wayland APK present test).
# No device-driver install, no broker, no libhybris, no live X server teardown.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
OUT="$ROOT/.source/gpu-stage27-pr25/s16-xwayland-gpu"
mkdir -p "$OUT"
cd "$ROOT"

for src in vk_loader vk_present compositor; do
    clang --target=aarch64-linux-android35 -std=gnu11 -fsyntax-only \
        -Wno-unused-parameter -Wno-unused-function \
        -I native/waylandcomp/src -I native/waylandcomp/generated \
        -I native/waylandcomp/prebuilt/include "native/waylandcomp/src/$src.c"
done
echo 'S16_HOST_VKBUFFER_GPU_COPY_SYNTAX=PASS'

clang --target=aarch64-linux-android35 -std=gnu11 -O2 -Wall -Wextra -Werror \
    graphics/tests/s16-kgsl-to-vkbuffer-gpucopy.c \
    -lEGL -lGLESv2 -ldl -o "$OUT/s16-kgsl-to-vkbuffer-gpucopy"
echo 'S16_PRIVATE_GPU_PROOF_BUILD=PASS'

# GLAMOR KGSL is Adreno-specific.  The host source itself has no vendor name
# gate and uses standard Vulkan capabilities; Xclipse is not claimed here.
if [[ "${ALROOT_S16_TEST_RESOLUTIONS:-short}" == full ]]; then
    sizes=(640x360 800x600 1280x720 1920x1080)
else
    sizes=(640x360)
fi
for size in "${sizes[@]}"; do
    log="$OUT/s16-real-kgsl-vkbuffer-copy-$size.log"
    if ! env -u FD_MESA_DEBUG \
        MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno \
        FD_KGSL_ENABLE_DMABUF=1 EGL_PLATFORM=surfaceless \
        S16_EGL_SIZE="$size" timeout 25s \
        "$OUT/s16-kgsl-to-vkbuffer-gpucopy" >"$log" 2>&1; then
        echo "S16_KGSL_VKBUFFER_GPU_COPY[$size]=FAIL"
        tail -20 "$log"
        exit 1
    fi
    # Spatial pattern must survive the REAL EGL GPU texture export and the
    # Android Vulkan VkBuffer->VkImage stage-copy without tiled/swizzle error.
    if [[ -n "${S16_GRADIENT_TEST:-}" ]]; then
        expected_pixels=$(( ${size%x*} * ${size#*x} ))
        correct_frame="S16_SPATIAL_GRADIENT_GPU_COPY=checked:${expected_pixels} top_mismatch:0 "
        valid_frame=0
        grep -Fq "$correct_frame" "$log" && valid_frame=1
    else
        valid_frame=0
        grep -q '^S16_COPY_VKBUFFER_TO_VKIMAGE_GPU_PIXELS=5/5$' "$log" && valid_frame=1
    fi
    if [[ "$valid_frame" != 1 ]] ||
       ! grep -q '^S16_KGSL_TO_ANDROID_VKIMAGE_GPU_COPY=PASS$' "$log" ||
       { [[ -n "${S16_GPU_SYNC_FD_WAIT:-}" ]] &&
         ! grep -q '^S16_GPU_WAIT_IMPORT_SYNC_FD=0$' "$log"; }; then
        echo "S16_KGSL_VKBUFFER_GPU_COPY[$size]=FAIL_PIXELS"
        tail -20 "$log"
        exit 1
    fi
    echo "S16_KGSL_VKBUFFER_GPU_COPY[$size]=PASS (pattern=${S16_GRADIENT_TEST:+spatial-gradient}; syncfd=${S16_GPU_SYNC_FD_WAIT:+Vulkan-GPU-wait})"
done
if [[ -x "$OUT/kgsl-egl-to-android-vkbuffer" ]]; then
    echo 'S16_KGSL_SSBO_DIRECT_READER=PROVEN_IN_SEPARATE_S16_PROBE'
fi
echo 'S16_XWAYLAND_SERVER_GPU_WAYLAND_PRESENT=NOT_VERIFIED'
echo 'S16_ACTIVE_XFCE_APK=UNCHANGED'
echo 'S16_INTERMEDIATE_CPU_PIXEL_COPY=NONE_IN_TEST'
echo 'S16_STAGE1_FRAME_PATH=VULKAN_VKBUFFER_TO_VKIMAGE_GPU_COPY'
