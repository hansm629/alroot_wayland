#!/data/data/com.termux/files/usr/bin/bash
# Explicit S28 private Xwayland A/B launcher. Restart is NEVER implicit.
# Requires independent native Termux, preserves installed system graphics.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
STATE="$HOME/.alroot-wayland"
BASE="$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland"
CANDIDATE="$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland"
MODE=""
DRY=0
cd "$ROOT"

while (($#)); do
    case "$1" in
        --restart) [[ -z "$MODE" ]] || exit 2; MODE=s28 ;;
        --restore) [[ -z "$MODE" ]] || exit 2; MODE=restore ;;
        --dry-run) DRY=1 ;;
        *) echo 'Usage: bash graphics/tools/start-s28-kgsl-resize-preserve.sh --restart|--restore [--dry-run]' >&2;exit 2 ;;
    esac
    shift
done
[[ -n "$MODE" ]] || { echo 'S28_LAUNCH=BLOCKED_REQUIRES_EXPLICIT_--restart_OR_--restore' >&2;exit 2; }
[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S28_LAUNCH=BLOCKED_S26U_ONLY >&2;exit 2;
}
[[ -x "$BASE" && -x "$CANDIDATE" ]] || {
    echo S28_LAUNCH=BLOCKED_MISSING_PRIVATE_SERVER_BUILD >&2;exit 2;
}
[[ "$(sha256sum "$BASE"|cut -d' ' -f1)" == f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701 ]] || {
    echo S28_LAUNCH=BLOCKED_KGSL_BASELINE_CHANGED >&2;exit 2;
}
[[ "$(sha256sum "$CANDIDATE"|cut -d' ' -f1)" == cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7 ]] || {
    echo S28_LAUNCH=BLOCKED_EXPERIMENTAL_BINARY_NOT_VALIDATED >&2;exit 2;
}
if [[ "${XDG_CONFIG_HOME:-}" == "$STATE/xfce-config" &&
      -n "${DISPLAY:-}" && -r "$STATE/display" &&
      "${DISPLAY%%.*}" == "$(cat "$STATE/display")" ]]; then
    echo 'S28_LAUNCH=BLOCKED_FROM_INSIDE_XFCE_USE_NATIVE_TERMUX_SHELL' >&2
    exit 2
fi
if [[ "$MODE" == restore ]]; then
    target="$BASE"
else
    target="$CANDIDATE"
fi

if ((DRY)); then
    echo "S28_DRY_RUN_MODE=$MODE"
    echo "S28_DRY_RUN_XWAYLAND=$target"
    echo S28_DRY_RUN_WOULD_STOP_EXISTING_XFCE=YES_ONLY_ON_ACTUAL_RESTART
    echo S28_DRY_RUN_ACTUAL_DESKTOP=UNCHANGED
    exit 0
fi

echo "S28_EXPLICIT_XFCE_RESTART_MODE=$MODE"
echo S28_CPU_LANE=NATIVE_TERMUX_SHELL_NOT_ANDROID_TOPAPP

# The S27 awstart command performs stop/start under the project locks,
# including verified PID termination. No separate awstart --stop needed.
if [[ "$MODE" == s28 ]]; then
    env ALROOT_XWAYLAND_BINARY="$target" \
        ALROOT_XWAYLAND_GLAMOR=es \
        ALROOT_XWAYLAND_DUAL_ABI_FENCE=1 \
        S28_KGSL_RESIZE_PRESERVE=1 \
        XWAYLAND_FORCE_KGSL_SURFACELESS=1 \
        FD_KGSL_ENABLE_DMABUF=1 \
        MESA_LOADER_DRIVER_OVERRIDE=kgsl \
        GALLIUM_DRIVER=freedreno \
        awstart --restart --direct --no-topapp
else
    env -u S28_KGSL_RESIZE_PRESERVE \
        ALROOT_XWAYLAND_BINARY="$target" \
        ALROOT_XWAYLAND_GLAMOR=es \
        ALROOT_XWAYLAND_DUAL_ABI_FENCE=1 \
        XWAYLAND_FORCE_KGSL_SURFACELESS=1 \
        FD_KGSL_ENABLE_DMABUF=1 \
        MESA_LOADER_DRIVER_OVERRIDE=kgsl \
        GALLIUM_DRIVER=freedreno \
        awstart --restart --direct --no-topapp
fi

status="$(awstart --status)"
printf '%s\n' "$status" | grep -E '^host:|^xwayland:|^xfce:|^x11_desktop:|^X DISPLAY:'
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
[[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/cmdline" ]] || {
    echo S28_LAUNCH=FAIL_NO_OWNED_XWAYLAND_PID >&2;exit 4;
}
if ! tr '\0' ' ' < "/proc/$pid/cmdline" | grep -Fq "$target";then
    echo S28_LAUNCH=FAIL_WRONG_PRIVATE_XWAYLAND >&2;exit 4
fi
grep -Fq 'x11_desktop: READY ' <<< "$status" || {
    echo S28_LAUNCH=FAIL_DESKTOP_NOT_READY >&2;exit 4;
}
if [[ "$MODE" == s28 ]];then
    echo S28_KGSL_RESIZE_PRESERVE=PRIVATE_OPT_IN_ACTIVE
else
    echo S28_KGSL_RESIZE_PRESERVE=BASELINE_RESTORED
fi
echo "S28_RUNNING_XWAYLAND_PID=$pid"
grep '^Cpus_allowed_list:' "/proc/$pid/status" || true
echo S28_FLICKER_PHYSICAL_ACCEPTANCE=USER_VISUAL_REQUIRED
