#!/data/data/com.termux/files/usr/bin/bash
# Native Android GLES/ANGLE -> direct AHB image -> Alroot Wayland GPU-copy.
# No modification of system EGL, installed Termux Mesa or DRI3.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
frames="${1:-90}"
format="${2:-rgba8}"
[[ "$frames" =~ ^[0-9]+$ ]] && ((frames>=1&&frames<=360)) || exit 2
case "$format" in
rgba8|bgra8) export ALROOT_S12_AHB_FORMAT="$format" ;;
*) echo "usage: $0 [1..360 frames] [rgba8|bgra8]" >&2; exit 2 ;;
esac
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-s12-gles-ahb.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
clang --target=aarch64-linux-android35 -std=gnu11 -Wall -Wextra -Werror \
    -Igraphics/include -Inative/waylandcomp/generated \
    graphics/tests/s12-android-gles-ahb-present.c \
    graphics/src/ahb_wayland_presenter.c \
    native/waylandcomp/generated/alroot-gpu-buffer-v1-protocol.c \
    native/waylandcomp/generated/xdg-shell-protocol.c \
    -lwayland-client -ldl -o "$TMP/s12-gles-ahb"
timestamp="$(date +'%m-%d %H:%M:%S.000')"
set +e
timeout -s TERM 18s env -u DISPLAY \
    XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" \
    WAYLAND_DISPLAY=wayland-0 \
    "$TMP/s12-gles-ahb" "$frames"
rc=$?
set -e
OUT="$ROOT/.source/gpu-stage2/gles-ahb"
mkdir -p "$OUT"
logcat -d -t "$timestamp" -s AlrootWayland > "$OUT/host.log" 2>&1 || :
if ((rc!=0)); then
    echo "S12_GLES_AHB_EXIT=$rc"
    exit "$rc"
fi
if grep -q 'AHB -> VkImage GPU-copy:' "$OUT/host.log" &&
   grep -q '"Alroot S1.2 Android GLES AHB presenter".*presenting GPU frames through Wayland' "$OUT/host.log"; then
    echo 'S12_GLES_AHB_GPU_COPY=HOST_IMPORT_CONFIRMED'
else
    echo 'S12_GLES_AHB_GPU_COPY=NOT_CONFIRMED'
    grep -E 'import failed|gpu-copy|Alroot S1.2' "$OUT/host.log" | tail -12 || :
    exit 5
fi
echo 'VISUAL_ACCEPTANCE=USER_CHECK_REQUIRED'
