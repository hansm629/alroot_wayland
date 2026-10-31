#!/data/data/com.termux/files/usr/bin/bash
# Explicit S30 private KGSL Xwayland stable-frame + tiny-AHB launch/rollback.
# Run only from INDEPENDENT native Termux, never inside the XFCE being stopped.
# Does not install an APK, modify global drivers or change default awstart.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
STATE="$HOME/.alroot-wayland"
S16="$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland"
S28="$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland"
S29="$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland"
S30="$ROOT/.source/gpu-stage1/s30-min-extent/runtime/Xwayland"
declare -a checks=(
    "$S16|f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701"
    "$S28|cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7"
    "$S29|3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c"
    "$S30|4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31"
)
mode=""
dry_run=0
cd "$ROOT"
while (($#));do
    case "$1" in
        --restart) [[ -z "$mode" ]] || exit 2;mode=s30 ;;
        --restore-s29) [[ -z "$mode" ]] || exit 2;mode=s29 ;;
        --restore-s28) [[ -z "$mode" ]] || exit 2;mode=s28 ;;
        --restore-s16) [[ -z "$mode" ]] || exit 2;mode=s16 ;;
        --dry-run) dry_run=1 ;;
        *) echo 'Usage: bash graphics/tools/start-s30-kgsl-stable-resize.sh --restart|--restore-s29|--restore-s28|--restore-s16 [--dry-run]' >&2;exit 2 ;;
    esac
    shift
done
[[ -n "$mode" ]] || { echo S30_START=BLOCKED_EXPLICIT_RESTART_OR_ROLLBACK_REQUIRED >&2;exit 2; }
[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S30_START=BLOCKED_ONLY_VALIDATED_S26_ULTRA >&2;exit 2;
}
if [[ "${XDG_CONFIG_HOME:-}" == "$STATE/xfce-config" &&
      -n "${DISPLAY:-}" && -r "$STATE/display" &&
      "${DISPLAY%%.*}" == "$(cat "$STATE/display")" ]];then
    echo S30_START=BLOCKED_RUN_FROM_NATIVE_TERMUX_NOT_XFCE_TERMINAL >&2;exit 2
fi
for entry in "${checks[@]}";do
    file="${entry%%|*}"
    hash="${entry#*|}"
    [[ -x "$file" && ! -L "$file" &&
       "$(sha256sum "$file"|cut -d' ' -f1)" == "$hash" ]] || {
        echo "S30_START=BLOCKED_UNKNOWN_OR_MISSING_PINNED_PRIVATE_BINARY: $file" >&2
        exit 2
    }
done
case "$mode" in
    s16) target="$S16" ;;
    s28) target="$S28" ;;
    s29) target="$S29" ;;
    s30) target="$S30" ;;
esac
if ((dry_run));then
    echo "S30_DRY_RUN_MODE=$mode"
    echo "S30_DRY_RUN_EXECUTABLE=$target"
    echo S30_DRY_RUN_XFCE_NOT_RESTARTED=PASS
    exit 0
fi

current="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$current")"
if [[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" ]] &&
   [[ "$(readlink -f "/proc/$pid/exe")" == "$target" ]] &&
   grep -Fq 'x11_desktop: READY' <<< "$current";then
    echo "S30_START=ALREADY_RUNNING_SELECTED_PRIVATE_SERVER MODE=$mode PID=$pid"
    exit 0
fi

echo "S30_EXPLICIT_PRIVATE_XFCE_RESTART_MODE=$mode"
echo S30_CPU_MODE=NATIVE_TERMUX_SHELL_NOT_ANDROID_TOPAPP

common=(
    ALROOT_XWAYLAND_BINARY="$target"
    ALROOT_XWAYLAND_GLAMOR=es
    ALROOT_XWAYLAND_DUAL_ABI_FENCE=1
    XWAYLAND_FORCE_KGSL_SURFACELESS=1
    FD_KGSL_ENABLE_DMABUF=1
    MESA_LOADER_DRIVER_OVERRIDE=kgsl
    GALLIUM_DRIVER=freedreno
)
case "$mode" in
    s30)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S30_KGSL_STABLE_FRAME -u S30_KGSL_TINY_AHB_FD0 \
            -u S30_DIAG_TINY_AHB -u S30_TRACE_STABLE \
            "${common[@]}" S30_KGSL_STABLE_FRAME=1 S30_KGSL_TINY_AHB_FD0=1 \
            awstart --restart --direct --no-topapp ;;
    s29)
        env -u S28_KGSL_RESIZE_PRESERVE -u S30_KGSL_STABLE_FRAME \
            -u S30_KGSL_TINY_AHB_FD0 -u S30_DIAG_TINY_AHB -u S30_TRACE_STABLE \
            "${common[@]}" S29_KGSL_NATIVE_RESIZE=1 \
            awstart --restart --direct --no-topapp ;;
    s28)
        env -u S29_KGSL_NATIVE_RESIZE -u S30_KGSL_STABLE_FRAME \
            -u S30_KGSL_TINY_AHB_FD0 -u S30_DIAG_TINY_AHB -u S30_TRACE_STABLE \
            "${common[@]}" S28_KGSL_RESIZE_PRESERVE=1 \
            awstart --restart --direct --no-topapp ;;
    s16)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S30_KGSL_STABLE_FRAME -u S30_KGSL_TINY_AHB_FD0 \
            -u S30_DIAG_TINY_AHB -u S30_TRACE_STABLE \
            "${common[@]}" awstart --restart --direct --no-topapp ;;
esac

status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
[[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" &&
   "$(readlink -f "/proc/$pid/exe")" == "$target" ]] || {
    echo S30_START=FAILED_WRONG_PRIVATE_XWAYLAND_PROCESS >&2;exit 4;
}
grep -Fq 'x11_desktop: READY' <<< "$status" || {
    echo S30_START=FAILED_PRIVATE_XFCE_NOT_READY >&2;exit 4;
}
printf '%s\n' "$status" |grep -E '^host:|^xwayland:|^xfce:|^ime_frontend:|^x11_desktop:|^X DISPLAY:'
echo "S30_ACTIVE_PRIVATE_BINARY=$mode PID=$pid"
echo S30_SYSTEM_DRIVERS_AND_TERMUX_X11=UNCHANGED
echo S30_PHYSICAL_ANDROID_VISUAL_TEST=USER_ACCEPTANCE_PENDING
