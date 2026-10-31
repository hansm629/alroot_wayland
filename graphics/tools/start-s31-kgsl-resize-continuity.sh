#!/data/data/com.termux/files/usr/bin/bash
# S31 opt-in private KGSL Xwayland resize continuity A/B.
# An intentional XFCE restart is permitted only from native Termux, and
# only after --restart/--restore-* is explicitly given. Never touch :0.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
STATE="$HOME/.alroot-wayland"
S16="$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland"
S28="$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland"
S29="$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland"
S30="$ROOT/.source/gpu-stage1/s30-min-extent/runtime/Xwayland"
S31="$ROOT/.source/gpu-stage1/s31-resize-continuity/runtime/Xwayland"
declare -a approved=(
    "$S16|f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701"
    "$S28|cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7"
    "$S29|3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c"
    "$S30|4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31"
    "$S31|73acf8472faf1e5b91b5f4e998c32ed3322d30fc68e7b0683eef62b6a9c1fe90"
)
mode=""
dry=0
cd "$ROOT"
while (($#));do
    case "$1" in
        --restart) [[ -z "$mode" ]] || exit 2;mode=s31 ;;
        --restore-s30) [[ -z "$mode" ]] || exit 2;mode=s30 ;;
        --restore-s29) [[ -z "$mode" ]] || exit 2;mode=s29 ;;
        --restore-s16) [[ -z "$mode" ]] || exit 2;mode=s16 ;;
        --dry-run) dry=1 ;;
        *) echo 'Usage: bash graphics/tools/start-s31-kgsl-resize-continuity.sh --restart|--restore-s30|--restore-s29|--restore-s16 [--dry-run]' >&2;exit 2 ;;
    esac
    shift
done
[[ -n "$mode" ]] || { echo S31_START=BLOCKED_REQUIRES_EXPLICIT_MODE >&2;exit 2; }
[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S31_START=BLOCKED_ONLY_VALIDATED_S26_ULTRA >&2;exit 2;
}

# Refuse to kill the caller's own running project desktop.
if [[ "${XDG_CONFIG_HOME:-}" == "$STATE/xfce-config" &&
      -r "$STATE/display" && -n "${DISPLAY:-}" &&
      "${DISPLAY%%.*}" == "$(cat "$STATE/display")" ]];then
    echo 'S31_START=BLOCKED_FROM_INSIDE_XFCE_OPEN_A_NATIVE_TERMUX_SHELL' >&2
    exit 2
fi

for item in "${approved[@]}";do
    path="${item%%|*}";hash="${item#*|}"
    [[ -x "$path" && -f "$path" && ! -L "$path" &&
       "$(sha256sum "$path"|cut -d' ' -f1)" == "$hash" ]] || {
        echo "S31_START=BLOCKED_UNTRUSTED_PRIVATE_XWAYLAND: $path" >&2
        exit 2
    }
done
case "$mode" in
    s16)target="$S16" ;;
    s29)target="$S29" ;;
    s30)target="$S30" ;;
    s31)target="$S31" ;;
esac
if ((dry));then
    echo "S31_DRY_RUN_MODE=$mode"
    echo "S31_DRY_RUN_PRIVATE_XWAYLAND=$target"
    echo S31_DRY_RUN_USER_XFCE_UNCHANGED=PASS
    echo S31_DRY_RUN_ACTUAL_RESTART_NOT_EXECUTED=TRUE
    exit 0
fi

status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
if [[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" ]] &&
   [[ "$(readlink -f "/proc/$pid/exe")" == "$target" ]] &&
   grep -Fq 'x11_desktop: READY' <<< "$status";then
    echo "S31_START=ALREADY_RUNNING_SELECTED_PRIVATE_SERVER mode=$mode pid=$pid"
    exit 0
fi

echo "S31_EXPLICIT_PRIVATE_XFCE_RESTART_MODE=$mode"
echo S31_CPU_SCHEDULING=NATIVE_TERMUX_SHELL_NOT_ANDROID_TOPAPP
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
    s31)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S30_KGSL_STABLE_FRAME -u S31_KGSL_RESIZE_CONTENT_POLICY \
            -u S30_DIAG_TINY_AHB -u S30_TRACE_STABLE -u S31_RESIZE_DIAG \
            "${common[@]}" S30_KGSL_TINY_AHB_FD0=1 \
            S31_KGSL_RESIZE_CONTENT_POLICY=3 \
            awstart --restart --direct --no-topapp ;;
    s30)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S31_KGSL_RESIZE_CONTENT_POLICY -u S30_DIAG_TINY_AHB \
            -u S30_TRACE_STABLE -u S31_RESIZE_DIAG \
            "${common[@]}" S30_KGSL_STABLE_FRAME=1 \
            S30_KGSL_TINY_AHB_FD0=1 \
            awstart --restart --direct --no-topapp ;;
    s29)
        env -u S28_KGSL_RESIZE_PRESERVE -u S30_KGSL_STABLE_FRAME \
            -u S30_KGSL_TINY_AHB_FD0 -u S31_KGSL_RESIZE_CONTENT_POLICY \
            -u S30_DIAG_TINY_AHB -u S30_TRACE_STABLE -u S31_RESIZE_DIAG \
            "${common[@]}" S29_KGSL_NATIVE_RESIZE=1 \
            awstart --restart --direct --no-topapp ;;
    s16)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S30_KGSL_STABLE_FRAME -u S30_KGSL_TINY_AHB_FD0 \
            -u S31_KGSL_RESIZE_CONTENT_POLICY -u S30_DIAG_TINY_AHB \
            -u S30_TRACE_STABLE -u S31_RESIZE_DIAG \
            "${common[@]}" awstart --restart --direct --no-topapp ;;
esac

status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
[[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" &&
   "$(readlink -f "/proc/$pid/exe")" == "$target" ]] || {
    echo S31_START=FAIL_UNEXPECTED_XWAYLAND_PID_OR_EXECUTABLE >&2;exit 4;
}
grep -Fq 'x11_desktop: READY' <<< "$status" || {
    echo S31_START=FAIL_USER_XFCE_NOT_READY >&2;exit 4;
}
printf '%s\n' "$status" |grep -E '^host:|^xwayland:|^xfce:|^ime_frontend:|^x11_desktop:|^X DISPLAY:'
echo "S31_ACTIVE_PRIVATE_XWAYLAND=$mode PID=$pid"
echo S31_INSTALLED_TERMUX_X11_AND_GPU_DRIVER=UNCHANGED
echo S31_USER_PHYSICAL_RESIZE_VISUAL=TEST_PENDING
