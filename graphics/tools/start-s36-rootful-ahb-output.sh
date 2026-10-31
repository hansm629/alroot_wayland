#!/data/data/com.termux/files/usr/bin/bash
# Deliberately opt in to S36 on the S26 Ultra, or roll back to the pinned
# S32 / S30 project-private Xwayland. This never installs a new Android APK
# or changes global Termux-X11/Mesa/Vulkan and requires a native Termux shell.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
STATE="$HOME/.alroot-wayland"
cd "$ROOT"
S16="$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland"
S28="$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland"
S29="$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland"
S30="$ROOT/.source/gpu-stage1/s30-min-extent/runtime/Xwayland"
S31="$ROOT/.source/gpu-stage1/s31-resize-continuity/runtime/Xwayland"
S32="$ROOT/.source/gpu-stage1/s32-no-blank-present/runtime/Xwayland"
S36="$ROOT/.source/gpu-stage1/s36-ahb-output/runtime/Xwayland"
declare -a approved=(
    "$S16|f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701"
    "$S28|cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7"
    "$S29|3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c"
    "$S30|4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31"
    "$S31|73acf8472faf1e5b91b5f4e998c32ed3322d30fc68e7b0683eef62b6a9c1fe90"
    "$S32|b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455"
    "$S36|df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5"
)
mode=""
dry_run=0
while (($#));do
    case "$1" in
        --restart) [[ -z "$mode" ]] || exit 2;mode=s36 ;;
        --restore-s32) [[ -z "$mode" ]] || exit 2;mode=s32 ;;
        --restore-s30) [[ -z "$mode" ]] || exit 2;mode=s30 ;;
        --restore-s16) [[ -z "$mode" ]] || exit 2;mode=s16 ;;
        --dry-run) dry_run=1 ;;
        *) echo 'Usage: bash graphics/tools/start-s36-rootful-ahb-output.sh --restart|--restore-s32|--restore-s30|--restore-s16 [--dry-run]' >&2;exit 2 ;;
    esac
    shift
done
[[ -n "$mode" ]] || { echo S36_START=BLOCKED_EXPLICIT_SELECTION_REQUIRED >&2;exit 2; }
[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S36_START=BLOCKED_NON_S26_ULTRA >&2;exit 2;
}

# Prevent killing the very XFCE terminal from which this script is run.
if [[ "${XDG_CONFIG_HOME:-}" == "$STATE/xfce-config" &&
      -n "${DISPLAY:-}" && -r "$STATE/display" &&
      "${DISPLAY%%.*}" == "$(cat "$STATE/display")" ]];then
    echo 'S36_START=BLOCKED_FROM_INSIDE_XFCE_OPEN_SEPARATE_NATIVE_TERMUX_SHELL' >&2
    exit 2
fi
for item in "${approved[@]}";do
    path="${item%%|*}";digest="${item#*|}"
    [[ -x "$path" && -f "$path" && ! -L "$path" &&
       "$(sha256sum "$path"|cut -d' ' -f1)" == "$digest" ]] || {
        echo "S36_START=BLOCKED_UNPINNED_PRIVATE_BINARY: $path" >&2;exit 2;
    }
done
case "$mode" in
    s16) target="$S16" ;;
    s30) target="$S30" ;;
    s32) target="$S32" ;;
    s36) target="$S36" ;;
esac
if ((dry_run));then
    echo "S36_DRY_RUN_MODE=$mode"
    echo "S36_DRY_RUN_PRIVATE_BINARY=$target"
    echo S36_DRY_RUN_XFCE_NOT_RESTARTED=PASS
    exit 0
fi

status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
if [[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" ]] &&
   [[ "$(readlink -f "/proc/$pid/exe")" == "$target" ]] &&
   grep -Fq 'x11_desktop: READY' <<< "$status";then
    echo "S36_START=ALREADY_SELECTED_PRIVATE_XWAYLAND mode=$mode pid=$pid"
    exit 0
fi

echo "S36_EXPLICIT_PROJECT_XFCE_RESTART_MODE=$mode"
echo S36_CPU_MODE=NATIVE_TERMUX_NOT_ANDROID_TOPAPP
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
    s36)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S30_KGSL_STABLE_FRAME -u S31_KGSL_RESIZE_CONTENT_POLICY \
            -u S30_KGSL_TINY_AHB_FD0 -u S32_KGSL_NO_BLANK_PRESENT \
            -u S36_KGSL_ROOT_AHB_OUTPUT -u S30_DIAG_TINY_AHB \
            -u S30_TRACE_STABLE -u S31_RESIZE_DIAG -u S32_KGSL_SKIP_REPLAY \
            "${common[@]}" S30_KGSL_TINY_AHB_FD0=1 \
            S32_KGSL_NO_BLANK_PRESENT=1 S36_KGSL_ROOT_AHB_OUTPUT=1 \
            awstart --restart --direct --no-topapp ;;
    s32)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S30_KGSL_STABLE_FRAME -u S31_KGSL_RESIZE_CONTENT_POLICY \
            -u S36_KGSL_ROOT_AHB_OUTPUT -u S30_DIAG_TINY_AHB \
            -u S30_TRACE_STABLE -u S31_RESIZE_DIAG -u S32_KGSL_SKIP_REPLAY \
            "${common[@]}" S30_KGSL_TINY_AHB_FD0=1 \
            S32_KGSL_NO_BLANK_PRESENT=1 \
            awstart --restart --direct --no-topapp ;;
    s30)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S31_KGSL_RESIZE_CONTENT_POLICY -u S32_KGSL_NO_BLANK_PRESENT \
            -u S36_KGSL_ROOT_AHB_OUTPUT -u S30_DIAG_TINY_AHB \
            -u S30_TRACE_STABLE -u S31_RESIZE_DIAG -u S32_KGSL_SKIP_REPLAY \
            "${common[@]}" S30_KGSL_STABLE_FRAME=1 \
            S30_KGSL_TINY_AHB_FD0=1 \
            awstart --restart --direct --no-topapp ;;
    s16)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S30_KGSL_STABLE_FRAME -u S30_KGSL_TINY_AHB_FD0 \
            -u S31_KGSL_RESIZE_CONTENT_POLICY -u S32_KGSL_NO_BLANK_PRESENT \
            -u S36_KGSL_ROOT_AHB_OUTPUT -u S30_DIAG_TINY_AHB \
            -u S30_TRACE_STABLE -u S31_RESIZE_DIAG -u S32_KGSL_SKIP_REPLAY \
            "${common[@]}" awstart --restart --direct --no-topapp ;;
esac

status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
[[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" &&
   "$(readlink -f "/proc/$pid/exe")" == "$target" ]] || {
    echo S36_START=FAILED_PRIVATE_XWAYLAND_PROCESS_MISMATCH >&2;exit 4;
}
grep -Fq 'x11_desktop: READY' <<< "$status" || {
    echo S36_START=FAILED_PRIVATE_XFCE_NOT_READY >&2;exit 4;
}
printf '%s\n' "$status" |grep -E '^host:|^xwayland:|^xfce:|^ime_frontend:|^x11_desktop:|^X DISPLAY:'
echo "S36_ACTIVE_PRIVATE_XWAYLAND=$mode PID=$pid"
echo S36_INSTALLED_GPU_AND_TERMUX_X11=UNCHANGED
echo S36_ANDROID_PHYSICAL_VISUAL=USER_TEST_REQUIRED
