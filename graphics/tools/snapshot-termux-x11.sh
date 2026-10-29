#!/data/data/com.termux/files/usr/bin/bash
# Read-only guard: never install, restart or touch the existing Termux-X11 :0.
# snapshot => save hashes / current DRI3 to ignored .source/gpu-audit
# verify   => compare after isolated Alroot GPU patch/build experiments.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
SNAP="$ROOT/.source/gpu-audit/termux-x11-protected.sha256"
DRI3="$ROOT/.source/gpu-audit/termux-x11-dri3.txt"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
mode="${1:-}"
case "$mode" in
snapshot)
    mkdir -p "$(dirname "$SNAP")"
    files=()
    for path in \
      "$PREFIX/lib/libvulkan_wrapper.so" \
      "$PREFIX/lib/libvulkan_freedreno.so" \
      "$PREFIX/lib/libvulkan_lvp.so" \
      "$PREFIX/lib/libEGL_mesa.so" \
      "$PREFIX/lib/libGLX_mesa.so" \
      "$PREFIX/lib/dri/kgsl_dri.so" \
      "$PREFIX/lib/dri/zink_dri.so" \
      "$PREFIX/lib/dri/virgl_dri.so" \
      "$PREFIX/bin/Xwayland"; do
        [[ ! -f "$path" ]] || files+=("$path")
    done
    ((${#files[@]} > 0)) || { echo 'No Termux renderer files found' >&2; exit 2; }
    sha256sum "${files[@]}" > "$SNAP"
    # The user owns a running Termux-X11 display on :0. Query, never inject.
    if DISPLAY=:0 timeout 4 xdpyinfo -queryExtensions > "$DRI3" 2>&1; then
        grep -qE '^[[:space:]]+DRI3[[:space:]]' "$DRI3" || {
            echo 'Termux-X11 baseline is missing DRI3; do NOT proceed' >&2; exit 2;
        }
        echo 'Termux-X11 :0 DRI3 confirmed (read-only)'
    else
        echo 'Termux-X11 :0 unavailable: snapshot hashes retained; DRI3 not verified' >&2
    fi
    echo "PROTECTED baseline written to $SNAP"
    ;;
verify)
    [[ -r "$SNAP" ]] || { echo "Run '$0 snapshot' first" >&2; exit 2; }
    sha256sum -c "$SNAP"
    if [[ -r "$DRI3" ]] && grep -qE '^[[:space:]]+DRI3[[:space:]]' "$DRI3"; then
        now="$(mktemp "${TMPDIR:-/data/data/com.termux/files/usr/tmp}/alroot-dri3.XXXXXXXX")"
        trap 'rm -f "$now"' EXIT
        DISPLAY=:0 timeout 4 xdpyinfo -queryExtensions > "$now" 2>&1 &&
        grep -qE '^[[:space:]]+DRI3[[:space:]]' "$now" || {
            echo 'ERROR: Termux-X11 :0 no longer exposes DRI3' >&2; exit 1;
        }
        echo 'PASS: Termux-X11 :0 still exposes DRI3'
    fi
    echo 'PASS: protected Termux renderer files unchanged'
    ;;
*)
    echo "usage: $0 snapshot|verify" >&2; exit 2 ;;
esac
