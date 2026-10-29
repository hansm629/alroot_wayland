#!/data/data/com.termux/files/usr/bin/bash
# Compiles a real native Wayland EGL window client, then runs the selected
# driver under scoped environment only. Does NOT touch Termux-X11 :0.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
mode="${1:-zink}"
frames="${2:-90}"
case "$mode" in
zink)
    export MESA_LOADER_DRIVER_OVERRIDE=zink GALLIUM_DRIVER=zink
    export VK_DRIVER_FILES="$ROOT/.source/gpu-stage1/wrapper-private-runtime/wrapper_icd.private.json"
    [[ -s "$VK_DRIVER_FILES" ]] || { echo 'Missing private Vulkan wrapper ICD' >&2; exit 2; }
    if [[ "${ALROOT_S12_PRIVATE_EGL:-0}" == 1 ]]; then
        runtime="$ROOT/.source/gpu-stage2/mesa-egl/private-runtime"
        [[ -s "$runtime/egl_vendor.private.json" &&
           -s "$runtime/lib/libEGL_mesa.so" &&
           -s "$runtime/lib/libgallium-26.3.0-devel.so" ]] || {
            echo 'Private Mesa EGL/Gallium missing; build with graphics/tools/build-s12-zink-egl-private.sh' >&2
            exit 2
        }
        build="$ROOT/.source/gpu-stage2/mesa-egl/build"
        if ! cmp -s "$build/src/egl/libEGL_mesa.so" "$runtime/lib/libEGL_mesa.so" ||
           ! cmp -s "$build/src/gallium/targets/dri/libgallium-26.3.0-devel.so" \
                    "$runtime/lib/libgallium-26.3.0-devel.so"; then
            echo 'S12_PRIVATE_EGL_STAGING=STALE_OR_MISMATCHED' >&2
            echo 'Rebuild/restage with: bash graphics/tools/build-s12-zink-egl-private.sh' >&2
            exit 2
        fi
        patch_dir="$ROOT/graphics/patches/mesa/egl"
        source_hash="$(sha256sum \
            "$patch_dir/0001-zink-kopper-ahb-wayland-no-rendernode.patch" \
            "$patch_dir/0002-bionic-glapi-emutls-symbols.patch" \
            "$patch_dir/0003-zink-optional-driconf-kopper.patch" | sha256sum | cut -d' ' -f1)"
        stored_hash="$(cat "$ROOT/.source/gpu-stage2/mesa-egl/patch.sha256" 2>/dev/null || true)"
        if [[ "$source_hash" != "$stored_hash" ]]; then
            echo 'S12_PRIVATE_EGL_PATCHES=CHANGED_SINCE_BUILD' >&2
            echo 'Rebuild/restage with: bash graphics/tools/build-s12-zink-egl-private.sh' >&2
            exit 2
        fi
        export __EGL_VENDOR_LIBRARY_FILENAMES="$runtime/egl_vendor.private.json"
        export LIBGL_DRIVERS_PATH="$runtime/lib/dri"
        export LD_LIBRARY_PATH="$runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        export ALROOT_EGL_WAYLAND_KOPPER=1
    fi
    ;;
kgsl)
    export MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno
    unset VK_DRIVER_FILES
    ;;
*)
    echo "usage: $0 zink|kgsl [1..600 frames]" >&2
    exit 2
    ;;
esac
[[ "$frames" =~ ^[0-9]+$ ]] && ((frames>0&&frames<=600)) || exit 2
if [[ "${ALROOT_S12_PRIVATE_EGL:-0}" == 1 &&
      "${ALROOT_S12_ALLOW_PROTOCOL_V1_DIAG:-0}" != 1 ]]; then
    # Native Kopper often chooses BGRA8. An old v1 Alroot host can
    # render RGBA8 but cannot receive that BGRA8 AHB as an wl_buffer.
    # Avoid triggering a known-bad DMA-BUF fallback on a live S1.1 host.
    capabilities="$(bash graphics/tools/check-stage1-accel.sh)"
    if ! grep -qx 'ALROOT_GPU_AHB_ONLY_VERSION=2' <<< "$capabilities"; then
        echo 'S12_ZINK_KOPPER=HOST_PROTOCOL_V2_REQUIRED'
        echo 'Install a verified Alroot S1.2 GPU host first; existing APK/session is unchanged.' >&2
        exit 2
    fi
fi
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-s12-egl.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
clang --target=aarch64-linux-android35 -std=c11 -Wall -Wextra -Werror \
    -Inative/waylandcomp/generated \
    graphics/tests/wayland-egl-swaps.c \
    native/waylandcomp/generated/xdg-shell-protocol.c \
    -lwayland-client -lwayland-egl -lEGL -lGLESv2 \
    -o "$TMP/wayland-egl"
echo "S12_DRIVER=$mode"
timestamp="$(date +'%m-%d %H:%M:%S.000')"
if [[ "${ALROOT_S12_KOPPER_EXPERIMENT:-0}" == 1 && "$mode" == zink ]]; then
    # Never set LIBGL_ALWAYS_SOFTWARE. That incorrectly makes Zink seek
    # a CPU Vulkan GPU instead of Adreno, Mali, PowerVR or RADV-Xclipse.
    export ALROOT_EGL_WAYLAND_KOPPER=1
fi
set +e
OUT="$ROOT/.source/gpu-stage2/egl-results"
mkdir -p "$OUT"
if [[ "${ALROOT_S12_WIRE_VERIFY:-0}" == 1 ]]; then
    WIRE_LOG="$OUT/wire-${mode}-${frames}.log"
    timeout -s TERM 18s env -u DISPLAY \
        WAYLAND_DEBUG=client \
        XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" \
        WAYLAND_DISPLAY=wayland-0 \
        EGL_PLATFORM=wayland \
        "$TMP/wayland-egl" "$frames" > "$WIRE_LOG" 2>&1
    rc=$?
    grep '^S12_' "$WIRE_LOG" || :
else
    timeout -s TERM 18s env -u DISPLAY \
        XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" \
        WAYLAND_DISPLAY=wayland-0 \
        EGL_PLATFORM=wayland \
        "$TMP/wayland-egl" "$frames"
    rc=$?
fi
set -e
HOST_LOG="$OUT/host-${mode}-${frames}.log"
logcat -d -t "$timestamp" -s AlrootWayland > "$HOST_LOG" 2>&1 || :
if ((rc!=0)); then
    echo "S12_NATIVE_EGL_TEST=FAILED code=$rc"
    exit "$rc"
fi
if [[ "${ALROOT_S12_WIRE_VERIFY:-0}" == 1 && "$mode" == zink ]]; then
    ahb_count="$(grep -Ec ' -> alroot_gpu_buffer_v1#[0-9]+\.create_buffer(_with_format)?\(' "$WIRE_LOG" || true)"
    shm_count="$(grep -Ec ' -> wl_shm_pool#[0-9]+\.create_buffer\(' "$WIRE_LOG" || true)"
    echo "S12_WIRE_AHB_BUFFERS=$ahb_count"
    echo "S12_WIRE_SHM_BUFFERS=$shm_count"
    echo "S12_WIRE_CLIENT_LOG=$WIRE_LOG"
    if ((ahb_count==0 || shm_count!=0)); then
        echo 'S12_WIRE_GPU_TRANSPORT=FAIL (AHB absent or wl_shm fallback)' >&2
        exit 6
    fi
    echo 'S12_WIRE_GPU_TRANSPORT=AHB_ONLY'
fi
if grep -q 'AHB -> VkImage GPU-copy:' "$HOST_LOG" &&
   grep -q '"Alroot S1.2 Wayland EGL/GLES swap test".*presenting GPU frames through Wayland' "$HOST_LOG"; then
    echo 'S12_EGL_WAYLAND_GPU_COPY=HOST_IMPORT_CONFIRMED'
else
    echo 'S12_EGL_WAYLAND_GPU_COPY=NOT_CONFIRMED'
    echo 'eglSwapBuffers may succeed while the host cannot import GPU pixels.'
    grep -Ei 'could not import GPU|dmabuf import failed|S1\.2 Wayland EGL|AHB -> VkImage' "$HOST_LOG" | tail -8 || :
    if [[ "${ALROOT_S12_REQUIRE_GPU_COPY:-1}" == 1 ]]; then
        echo 'S12_GPU_COPY_ACCEPTANCE=FAIL' >&2
        exit 5
    fi
fi
echo "S12_HOST_LOG=$HOST_LOG"
