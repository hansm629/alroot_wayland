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
timeout -s TERM 18s env -u DISPLAY \
    XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" \
    WAYLAND_DISPLAY=wayland-0 \
    EGL_PLATFORM=wayland \
    "$TMP/wayland-egl" "$frames"
rc=$?
set -e
OUT="$ROOT/.source/gpu-stage2/egl-results"
mkdir -p "$OUT"
HOST_LOG="$OUT/host-${mode}-${frames}.log"
logcat -d -t "$timestamp" -s AlrootWayland > "$HOST_LOG" 2>&1 || :
if ((rc!=0)); then
    echo "S12_NATIVE_EGL_TEST=FAILED code=$rc"
    exit "$rc"
fi
if grep -q 'AHB -> VkImage GPU-copy:' "$HOST_LOG" &&
   grep -q '"Alroot S1.2 Wayland EGL/GLES swap test".*presenting GPU frames through Wayland' "$HOST_LOG"; then
    echo 'S12_EGL_WAYLAND_GPU_COPY=HOST_IMPORT_CONFIRMED'
else
    echo 'S12_EGL_WAYLAND_GPU_COPY=NOT_CONFIRMED'
    echo 'eglSwapBuffers may succeed while the host cannot import GPU pixels.'
    grep -Ei 'could not import GPU|dmabuf import failed|S1\.2 Wayland EGL|AHB -> VkImage' "$HOST_LOG" | tail -8 || :
fi
echo "S12_HOST_LOG=$HOST_LOG"
