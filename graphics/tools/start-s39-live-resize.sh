#!/data/data/com.termux/files/usr/bin/bash
# Explicit project-XFCE switch. This selects the S39 server only; a private
# vkcube event-loop test binary is selected separately by its demo runner.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
STATE="$HOME/.alroot-wayland"
TARGET="$ROOT/.source/gpu-stage1/s39-live-resize/runtime/Xwayland"
EXPECTED=c5f6c9a512f84e31f60ec52a9f10e689e0746f831049e57a4223ade2750f3899
mode="${1:-}"
[[ $# -gt 0 ]] && shift
[[ $# -eq 0 || ( $# -eq 1 && "$1" == --dry-run ) ]] || {
    echo 'Usage: start-s39-live-resize.sh --restart|--restore-s38 [--dry-run]' >&2
    exit 2
}
case "$mode" in
    --restore-s38)
        exec env -u S39_KGSL_RESIZE_ORIGIN -u S39_KGSL_KEEP_UNRELATED_ROOT_POOL \
            -u S39_RESIZE_WORK_TRACE bash \
            "$ROOT/graphics/tools/start-s38-frame-lifetime.sh" --restart "$@" ;;
    --restart) ;;
    *) echo 'Usage: start-s39-live-resize.sh --restart|--restore-s38 [--dry-run]' >&2; exit 2 ;;
esac
[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S39_START=BLOCKED_NON_S26_ULTRA >&2; exit 2;
}
[[ -x "$TARGET" && -f "$TARGET" && ! -L "$TARGET" &&
   "$(sha256sum "$TARGET" | cut -d' ' -f1)" == "$EXPECTED" ]] || {
    echo S39_START=BLOCKED_UNPINNED_PRIVATE_BINARY >&2; exit 2;
}
if [[ "${1:-}" == --dry-run ]]; then
    echo "S39_PRIVATE_BINARY=$TARGET"
    echo "S39_PRIVATE_BINARY_SHA256=$EXPECTED"
    echo S39_REQUIRED_FLAGS=S39_KGSL_RESIZE_ORIGIN=1,S39_KGSL_KEEP_UNRELATED_ROOT_POOL=1
    echo S39_DRY_RUN_XFCE_NOT_RESTARTED=PASS
    exit 0
fi
if [[ "${XDG_CONFIG_HOME:-}" == "$STATE/xfce-config" &&
      -n "${DISPLAY:-}" && -r "$STATE/display" &&
      "${DISPLAY%%.*}" == "$(cat "$STATE/display")" ]]; then
    echo S39_START=OPEN_SEPARATE_NATIVE_TERMUX_SHELL >&2; exit 2
fi

active_has_flags() {
    python3 - "$1" <<'PY'
from pathlib import Path
import sys
try:
    entries = Path('/proc/' + sys.argv[1] + '/environ').read_bytes().split(b'\0')
except OSError:
    raise SystemExit(1)
required = (b'S38_KGSL_FRAME_LIFETIME=1', b'S39_KGSL_RESIZE_ORIGIN=1',
            b'S39_KGSL_KEEP_UNRELATED_ROOT_POOL=1')
raise SystemExit(0 if all(item in entries for item in required) else 1)
PY
}

status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
if [[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" &&
      "$(readlink -f "/proc/$pid/exe")" == "$TARGET" ]] &&
      grep -Fq 'x11_desktop: READY' <<< "$status" && active_has_flags "$pid"; then
    echo "S39_START=ALREADY_SELECTED PID=$pid"
    exit 0
fi
echo S39_EXPLICIT_PROJECT_XFCE_RESTART=STARTING
env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
    -u S30_KGSL_STABLE_FRAME -u S31_KGSL_RESIZE_CONTENT_POLICY \
    -u S30_DIAG_TINY_AHB -u S37_DIAG_THIN_AHB -u S30_TRACE_STABLE \
    -u S31_RESIZE_DIAG -u S32_KGSL_SKIP_REPLAY -u S39_RESIZE_WORK_TRACE \
    ALROOT_XWAYLAND_BINARY="$TARGET" ALROOT_XWAYLAND_GLAMOR=es \
    ALROOT_XWAYLAND_DUAL_ABI_FENCE=1 XWAYLAND_FORCE_KGSL_SURFACELESS=1 \
    FD_KGSL_ENABLE_DMABUF=1 MESA_LOADER_DRIVER_OVERRIDE=kgsl \
    GALLIUM_DRIVER=freedreno S30_KGSL_TINY_AHB_FD0=1 \
    S32_KGSL_NO_BLANK_PRESENT=1 S36_KGSL_ROOT_AHB_OUTPUT=1 \
    S37_KGSL_THIN_AHB_FD0=1 S38_KGSL_FRAME_LIFETIME=1 \
    S39_KGSL_RESIZE_ORIGIN=1 S39_KGSL_KEEP_UNRELATED_ROOT_POOL=1 \
    awstart --restart --direct --no-topapp
status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
[[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" &&
   "$(readlink -f "/proc/$pid/exe")" == "$TARGET" ]] &&
   grep -Fq 'x11_desktop: READY' <<< "$status" && active_has_flags "$pid" || {
    echo S39_START=FAILED_PRIVATE_DESKTOP_OR_FLAGS_NOT_READY >&2; exit 4;
}
printf '%s\n' "$status" | grep -E '^host:|^xwayland:|^xfce:|^ime_frontend:|^x11_desktop:|^X DISPLAY:'
echo "S39_ACTIVE_PRIVATE_XWAYLAND=PASS PID=$pid"
echo S39_SERVER_SCOPE=ORIGIN_CONSISTENCY_AND_ROOT_POOL_REUSE
echo S39_VKCUBE_EVENT_LOOP_FIX=SELECT_PRIVATE_DEMO_SEPARATELY
echo S39_ANDROID_SCREEN_VISUAL_CHECK=REQUIRED
