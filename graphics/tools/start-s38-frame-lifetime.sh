#!/data/data/com.termux/files/usr/bin/bash
# Explicit switch of the project XFCE; run in native Termux after saving
# desktop work. The APK, system GPU packages and Termux-X11 are unchanged.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
STATE="$HOME/.alroot-wayland"
TARGET="$ROOT/.source/gpu-stage1/s38-frame-continuity/runtime/Xwayland"
EXPECTED=3f27886fae2bde560d34ca53dedacf4cec09fd9dbcddc591efafc4ec9bc994c2
mode="${1:-}"
[[ $# -gt 0 ]] && shift
case "$mode" in
    --restore-s37)
        exec env -u S38_KGSL_FRAME_LIFETIME bash \
            "$ROOT/graphics/tools/start-s37-kgsl-thin-ahb.sh" --restart "$@" ;;
    --restore-s36|--restore-s32)
        exec env -u S38_KGSL_FRAME_LIFETIME bash \
            "$ROOT/graphics/tools/start-s37-kgsl-thin-ahb.sh" "$mode" "$@" ;;
    --restart) ;;
    *) echo 'Usage: start-s38-frame-lifetime.sh --restart|--restore-s37|--restore-s36|--restore-s32 [--dry-run]' >&2; exit 2 ;;
esac
[[ $# -eq 0 || ( $# -eq 1 && "$1" == --dry-run ) ]] || exit 2
[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S38_START=BLOCKED_NON_S26_ULTRA >&2; exit 2;
}
if [[ "${XDG_CONFIG_HOME:-}" == "$STATE/xfce-config" &&
      -n "${DISPLAY:-}" && -r "$STATE/display" &&
      "${DISPLAY%%.*}" == "$(cat "$STATE/display")" ]]; then
    echo S38_START=OPEN_SEPARATE_NATIVE_TERMUX_SHELL >&2; exit 2
fi
[[ -x "$TARGET" && ! -L "$TARGET" &&
   "$(sha256sum "$TARGET" | cut -d' ' -f1)" == "$EXPECTED" ]] || {
    echo S38_START=BLOCKED_UNPINNED_PRIVATE_BINARY >&2; exit 2;
}
if [[ "${1:-}" == --dry-run ]]; then
    echo "S38_PRIVATE_BINARY=$TARGET"
    echo S38_DRY_RUN_XFCE_NOT_RESTARTED=PASS
    exit 0
fi
status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
if [[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" &&
      "$(readlink -f "/proc/$pid/exe")" == "$TARGET" ]] &&
      grep -Fq 'x11_desktop: READY' <<< "$status"; then
    echo "S38_START=ALREADY_SELECTED PID=$pid"
    exit 0
fi
echo S38_EXPLICIT_PROJECT_XFCE_RESTART=STARTING
env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
    -u S30_KGSL_STABLE_FRAME -u S31_KGSL_RESIZE_CONTENT_POLICY \
    -u S30_DIAG_TINY_AHB -u S37_DIAG_THIN_AHB -u S30_TRACE_STABLE \
    -u S31_RESIZE_DIAG -u S32_KGSL_SKIP_REPLAY \
    ALROOT_XWAYLAND_BINARY="$TARGET" ALROOT_XWAYLAND_GLAMOR=es \
    ALROOT_XWAYLAND_DUAL_ABI_FENCE=1 XWAYLAND_FORCE_KGSL_SURFACELESS=1 \
    FD_KGSL_ENABLE_DMABUF=1 MESA_LOADER_DRIVER_OVERRIDE=kgsl \
    GALLIUM_DRIVER=freedreno S30_KGSL_TINY_AHB_FD0=1 \
    S32_KGSL_NO_BLANK_PRESENT=1 S36_KGSL_ROOT_AHB_OUTPUT=1 \
    S37_KGSL_THIN_AHB_FD0=1 S38_KGSL_FRAME_LIFETIME=1 \
    awstart --restart --direct --no-topapp
status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
[[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" &&
   "$(readlink -f "/proc/$pid/exe")" == "$TARGET" ]] &&
   grep -Fq 'x11_desktop: READY' <<< "$status" || {
    echo S38_START=FAILED_PRIVATE_DESKTOP_NOT_READY >&2; exit 4;
}
printf '%s\n' "$status" | grep -E '^host:|^xwayland:|^xfce:|^ime_frontend:|^x11_desktop:|^X DISPLAY:'
echo "S38_ACTIVE_PRIVATE_XWAYLAND=PASS PID=$pid"
echo S38_ANDROID_SCREEN_VISUAL_CHECK=REQUIRED
