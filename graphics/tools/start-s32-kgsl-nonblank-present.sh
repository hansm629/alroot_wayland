#!/data/data/com.termux/files/usr/bin/bash
# Explicit private Xwayland S32 A/B; do not start this inside the XFCE
# session it will restart. Neither global Termux-X11 nor drivers are changed.
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
declare -a approved=(
    "$S16|f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701"
    "$S28|cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7"
    "$S29|3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c"
    "$S30|4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31"
    "$S31|73acf8472faf1e5b91b5f4e998c32ed3322d30fc68e7b0683eef62b6a9c1fe90"
    "$S32|b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455"
)
mode=""
dry_run=0
while (($#));do
    case "$1" in
        --restart) [[ -z "$mode" ]] || exit 2;mode=s32 ;;
        --restore-s30) [[ -z "$mode" ]] || exit 2;mode=s30 ;;
        --restore-s16) [[ -z "$mode" ]] || exit 2;mode=s16 ;;
        --dry-run) dry_run=1 ;;
        *) echo 'Usage: bash graphics/tools/start-s32-kgsl-nonblank-present.sh --restart|--restore-s30|--restore-s16 [--dry-run]' >&2;exit 2 ;;
    esac
    shift
done
[[ -n "$mode" ]] || { echo S32_START=BLOCKED_EXPLICIT_SELECTION_REQUIRED >&2;exit 2; }
[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S32_START=BLOCKED_UNTESTED_GPU_OR_DEVICE >&2;exit 2;
}

# Never terminate the compositor from inside a terminal owned by it.
if [[ "${XDG_CONFIG_HOME:-}" == "$STATE/xfce-config" &&
      -n "${DISPLAY:-}" && -r "$STATE/display" &&
      "${DISPLAY%%.*}" == "$(cat "$STATE/display")" ]];then
    echo 'S32_START=BLOCKED_INSIDE_XFCE_OPEN_INDEPENDENT_NATIVE_TERMUX' >&2
    exit 2
fi
for item in "${approved[@]}";do
    path="${item%%|*}"
    digest="${item#*|}"
    [[ -f "$path" && -x "$path" && ! -L "$path" &&
       "$(sha256sum "$path"|cut -d' ' -f1)" == "$digest" ]] || {
        echo "S32_START=BLOCKED_PRIVATE_BINARY_CHANGED: $path" >&2;exit 2;
    }
done
case "$mode" in
    s16) target="$S16" ;;
    s30) target="$S30" ;;
    s32) target="$S32" ;;
esac

if ((dry_run));then
    echo "S32_DRY_RUN_MODE=$mode"
    echo "S32_DRY_RUN_PRIVATE_XWAYLAND=$target"
    echo S32_DRY_RUN_USER_XFCE_UNCHANGED=PASS
    exit 0
fi

status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
if [[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" ]] &&
   [[ "$(readlink -f "/proc/$pid/exe")" == "$target" ]] &&
   grep -Fq 'x11_desktop: READY' <<< "$status";then
    echo "S32_START=ALREADY_RUNNING_SELECTED_PRIVATE_BINARY mode=$mode pid=$pid"
    exit 0
fi

echo "S32_EXPLICIT_PROJECT_XFCE_RESTART=$mode"
echo S32_CPU_MODE=NATIVE_TERMUX_SHELL_NOT_ANDROID_TOPAPP
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
    s32)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S30_KGSL_STABLE_FRAME -u S31_KGSL_RESIZE_CONTENT_POLICY \
            -u S32_KGSL_NO_BLANK_PRESENT -u S30_KGSL_TINY_AHB_FD0 \
            -u S30_DIAG_TINY_AHB -u S30_TRACE_STABLE -u S31_RESIZE_DIAG \
            -u S32_KGSL_SKIP_REPLAY \
            "${common[@]}" S32_KGSL_NO_BLANK_PRESENT=1 \
            S30_KGSL_TINY_AHB_FD0=1 \
            awstart --restart --direct --no-topapp ;;
    s30)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S31_KGSL_RESIZE_CONTENT_POLICY -u S32_KGSL_NO_BLANK_PRESENT \
            -u S30_KGSL_TINY_AHB_FD0 -u S30_DIAG_TINY_AHB \
            -u S30_TRACE_STABLE -u S31_RESIZE_DIAG -u S32_KGSL_SKIP_REPLAY \
            "${common[@]}" S30_KGSL_STABLE_FRAME=1 \
            S30_KGSL_TINY_AHB_FD0=1 \
            awstart --restart --direct --no-topapp ;;
    s16)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            -u S30_KGSL_STABLE_FRAME -u S30_KGSL_TINY_AHB_FD0 \
            -u S31_KGSL_RESIZE_CONTENT_POLICY -u S32_KGSL_NO_BLANK_PRESENT \
            -u S30_DIAG_TINY_AHB -u S30_TRACE_STABLE -u S31_RESIZE_DIAG \
            -u S32_KGSL_SKIP_REPLAY \
            "${common[@]}" awstart --restart --direct --no-topapp ;;
esac

status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
[[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/exe" &&
   "$(readlink -f "/proc/$pid/exe")" == "$target" ]] || {
    echo S32_START=FAIL_NOT_SELECTED_PROJECT_PRIVATE_XWAYLAND >&2;exit 4;
}
grep -Fq 'x11_desktop: READY' <<< "$status" || {
    echo S32_START=FAIL_PROJECT_XFCE_NOT_READY >&2;exit 4;
}
printf '%s\n' "$status" |grep -E '^host:|^xwayland:|^xfce:|^ime_frontend:|^x11_desktop:|^X DISPLAY:'
echo "S32_ACTIVE_PRIVATE_SERVER=$mode PID=$pid"
echo S32_GLOBAL_TERMUX_X11_AND_GPU_PACKAGES=UNCHANGED
echo S32_PHYSICAL_ANDROID_VISUAL_TEST=USER_ACCEPTANCE_PENDING
