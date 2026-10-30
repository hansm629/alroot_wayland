#!/data/data/com.termux/files/usr/bin/bash
# One explicit native-Termux-shell restart to compare the S29 pixel-exact
# KGSL Xwayland against the previous S28 and S16 private binaries.
# No GTK/IME/Android APK or Termux-X11 configuration is modified.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
STATE="$HOME/.alroot-wayland"
S16="$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland"
S28="$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland"
S29="$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland"
S16_HASH=f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701
S28_HASH=cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7
S29_HASH=3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c
mode=""
dry_run=0
cd "$ROOT"

while (($#));do
    case "$1" in
        --restart) [[ -z "$mode" ]] || exit 2; mode=s29 ;;
        --restore-s28) [[ -z "$mode" ]] || exit 2; mode=s28 ;;
        --restore-s16) [[ -z "$mode" ]] || exit 2; mode=s16 ;;
        --dry-run) dry_run=1 ;;
        *) echo 'Usage: bash graphics/tools/start-s29-kgsl-native-resize.sh --restart|--restore-s28|--restore-s16 [--dry-run]' >&2;exit 2 ;;
    esac
    shift
done
[[ -n "$mode" ]] || { echo S29_START=BLOCKED_EXPLICIT_SELECTION_REQUIRED >&2;exit 2; }
[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S29_START=BLOCKED_NOT_VALIDATED_S26_ULTRA >&2;exit 2;
}
[[ -x "$S16" && -x "$S28" && -x "$S29" &&
   "$(sha256sum "$S16" | cut -d' ' -f1)" == "$S16_HASH" &&
   "$(sha256sum "$S28" | cut -d' ' -f1)" == "$S28_HASH" &&
   "$(sha256sum "$S29" | cut -d' ' -f1)" == "$S29_HASH" ]] || {
    echo S29_START=BLOCKED_MISSING_OR_MODIFIED_PRIVATE_XWAYLAND >&2;exit 2;
}

if [[ "${XDG_CONFIG_HOME:-}" == "$STATE/xfce-config" &&
      -r "$STATE/display" && -n "${DISPLAY:-}" &&
      "${DISPLAY%%.*}" == "$(cat "$STATE/display")" ]];then
    echo S29_START=BLOCKED_FROM_XFCE_TERMINAL_USE_INDEPENDENT_NATIVE_TERMUX >&2
    exit 2
fi

case "$mode" in
    s16) target="$S16" ;;
    s28) target="$S28" ;;
    s29) target="$S29" ;;
esac

if ((dry_run));then
    echo "S29_DRY_RUN_SELECTION=$mode"
    echo "S29_DRY_RUN_PRIVATE_XWAYLAND=$target"
    echo S29_DRY_RUN_RESTARTS_USER_XFCE=ONLY_IF_REAL_COMMAND_RUNS
    echo S29_DRY_RUN_USER_SESSION=UNCHANGED
    exit 0
fi

status="$(awstart --status)"
active="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
if [[ "$active" =~ ^[0-9]+$ && -r "/proc/$active/exe" ]] &&
   [[ "$(readlink -f "/proc/$active/exe")" == "$target" ]] &&
   grep -Fq 'x11_desktop: READY' <<< "$status";then
    echo S29_START=ALREADY_RUNNING_SELECTED_PRIVATE_XWAYLAND
    echo "S29_ACTIVE_MODE=$mode PID=$active"
    exit 0
fi

echo "S29_EXPLICIT_SESSION_RESTART=$mode"
echo S29_CPU_PROFILE=NATIVE_TERMUX_SHELL_NOT_ANDROID_TOPAPP

# S27 awstart handles process ownership, lock and complete restart.
# The allowed profile is never changed globally; each environment setting
# applies only to this one newly created project-private Xwayland process.
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
    s29)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            "${common[@]}" S29_KGSL_NATIVE_RESIZE=1 \
            awstart --restart --direct --no-topapp ;;
    s28)
        env -u S29_KGSL_NATIVE_RESIZE -u S28_KGSL_RESIZE_PRESERVE \
            "${common[@]}" S28_KGSL_RESIZE_PRESERVE=1 \
            awstart --restart --direct --no-topapp ;;
    s16)
        env -u S28_KGSL_RESIZE_PRESERVE -u S29_KGSL_NATIVE_RESIZE \
            "${common[@]}" awstart --restart --direct --no-topapp ;;
esac

status="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
[[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/cmdline" ]] || {
    echo S29_START=FAIL_NO_PRIVATE_XWAYLAND_PROCESS >&2;exit 4;
}
[[ "$(readlink -f "/proc/$pid/exe")" == "$target" ]] || {
    echo S29_START=FAIL_PRIVATE_XWAYLAND_EXECUTABLE_MISMATCH >&2;exit 4;
}
grep -Fq 'x11_desktop: READY' <<< "$status" || {
    echo S29_START=FAIL_XFCE_DESKTOP_NOT_READY >&2;exit 4;
}

printf '%s\n' "$status" | grep -E '^host:|^xwayland:|^xfce:|^ime_frontend:|^x11_desktop:|^X DISPLAY:'
echo "S29_RUNNING_PRIVATE_XWAYLAND=$mode PID=$pid"
grep '^Cpus_allowed_list:' "/proc/$pid/status" || true
echo S29_SYSTEM_GRAPHICS_AND_TERMUX_X11=UNCHANGED
echo S29_VULKAN_WINDOW_RESIZE_VISUAL=USER_TEST_REQUIRED
