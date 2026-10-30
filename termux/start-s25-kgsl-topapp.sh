#!/data/data/com.termux/files/usr/bin/bash
# Opt-in, S26U-only, one-shot KGSL profile across helper Activity ProcessBuilder.
# Never modifies installed GPU drivers or the default awstart profile.
set -euo pipefail
HOME=/data/data/com.termux/files/home
PREFIX=/data/data/com.termux/files/usr
PROJECT="$HOME/alroot_wayland"
STATE="$HOME/.alroot-wayland"
CONTROL="$STATE/control"
MARKER="$CONTROL/s25-kgsl-topapp-once"
cd "$PROJECT"
# A single, explicit restart replaces the previous unsafe two-command
# sequence. A normal call never tears down a healthy desktop. Do not
# attempt to stop a desktop from its own XFCE terminal.
restart=0
use_shell=0
while (($#));do
    case "$1" in
        --restart) restart=1 ;;
        --shell) use_shell=1 ;;
        *)
            echo 'Usage: bash termux/start-s25-kgsl-topapp.sh [--restart] [--shell]' >&2
            exit 2 ;;
    esac
    shift
done
if [[ "${XDG_CONFIG_HOME:-}" == "$STATE/xfce-config" &&
      -r "$STATE/display" && -n "${DISPLAY:-}" &&
      "${DISPLAY%%.*}" == "$(cat "$STATE/display")" ]];then
    echo 'S25_TOPAPP=BLOCKED_RUN_FROM_NATIVE_TERMUX_NOT_XFCE_TERMINAL' >&2
    exit 2
fi
[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo 'S25_TOPAPP=BLOCKED_NOT_S26_ULTRA' >&2;exit 2;
}
status="$(awstart --status)"
grep -Fqx '[awstart] Android helper APK: INSTALLED (matches latest locally signed APK)' <<< "$status" || {
    echo 'S25_TOPAPP=BLOCKED_INSTALL_NEW_SIGNED_S25_APK_FIRST' >&2;exit 2;
}
if ((restart));then
    echo S25_TOPAPP=REQUESTED_EXPLICIT_SINGLE_COMMAND_RESTART
    awstart --stop
elif grep -Fqx 'xfce: DOWN' <<< "$status" &&
     grep -Eq '^xwayland: RUNNING [0-9]+$' <<< "$status";then
    # Recover a half-stopped project session only; never stop an active
    # XFCE, never kill anything outside the project PID markers.
    pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$status")"
    expected="$PROJECT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland"
    if [[ ! -r "/proc/$pid/cmdline" ]] ||
       ! tr '\0' ' ' < "/proc/$pid/cmdline" | grep -Fq "$expected";then
        echo S25_TOPAPP=BLOCKED_UNRECOGNIZED_ORPHAN_XWAYLAND >&2;exit 2
    fi
    echo S25_TOPAPP=RECOVERING_PARTIALLY_STOPPED_PROJECT_XWAYLAND
    awstart --stop
fi
status="$(awstart --status)"
for role in xwayland xfce;do
    grep -Fxq "$role: DOWN" <<< "$status" || {
        echo "S25_TOPAPP=BLOCKED_ALREADY_RUNNING_$role_USE_EXPLICIT_--restart" >&2;exit 2;
    }
done
# A successful stop must also retire the clipboard, IME and nested
# compositor markers. Do not start another copy on top of old helpers.
for role in clipboard ime labwc;do
    grep -Fxq "$role: DOWN" <<< "$status" || {
        echo "S25_TOPAPP=BLOCKED_STALE_$role" >&2;exit 2;
    }
done
[[ -x "$PROJECT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland" &&
   -s "$PROJECT/.source/gpu-stage27-pr25/s16-xwayland-gpu/xshmfence/libalroot_xshmfence_dualabi.so" ]] || {
    echo S25_TOPAPP=BLOCKED_MISSING_PRIVATE_GPU_BINARY >&2;exit 2;
}
[[ -d "$CONTROL" && ! -L "$CONTROL" &&
   "$(stat -c '%u:%a' "$CONTROL")" == "$(id -u):700" ]] || {
    echo S25_TOPAPP=BLOCKED_UNSAFE_CONTROL_DIR >&2;exit 2;
}
[[ ! -e "$MARKER" && ! -L "$MARKER" ]] || {
    echo S25_TOPAPP=BLOCKED_PREEXISTING_MARKER >&2;exit 2;
}
launch_shell() {
    # Known-good recovery route: explicitly scoped to one invocation.
    # This is NOT top-app CPU scheduling, and does not alter awstart default.
    echo S25_TOPAPP=SHELL_LAUNCH_KGSL_EXPLICIT_FALLBACK
    env \
        ALROOT_XWAYLAND_BINARY="$PROJECT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland" \
        ALROOT_XWAYLAND_GLAMOR=es \
        ALROOT_XWAYLAND_DUAL_ABI_FENCE=1 \
        XWAYLAND_FORCE_KGSL_SURFACELESS=1 \
        FD_KGSL_ENABLE_DMABUF=1 \
        MESA_LOADER_DRIVER_OVERRIDE=kgsl \
        GALLIUM_DRIVER=freedreno \
        awstart --direct --no-topapp
}
route=TOPAPP
if ((use_shell));then
    route=SHELL_EXPLICIT
    launch_shell
else
    umask 077
    temp="$(mktemp "$CONTROL/.s25-kgsl.XXXXXXXX")"
    cleanup() { rm -f -- "$temp" "$MARKER"; }
    trap cleanup EXIT
    printf 'S25_KGSL_TOPAPP_ONCE\n' > "$temp"
    chmod 600 "$temp"
    mv -n -- "$temp" "$MARKER"
    [[ -f "$MARKER" ]] || exit 2
    echo S25_TOPAPP=REQUESTED_FOREGROUND_HELPER_PRIVATE_KGSL
    echo S25_TOPAPP=DOES_NOT_ENABLE_DEFAULT_PROFILE
    if ! awstart --direct --topapp;then
        echo S25_TOPAPP=HELPER_ACKNOWLEDGED_BUT_DESKTOP_NOT_READY >&2
        # The Activity's old REQUESTED acknowledgement proves only that
        # ProcessBuilder.start() was called, not that the CLI ever executed.
        # Clear the one-shot marker before the Termux-shell fallback.
        rm -f "$MARKER"
        status="$(awstart --status)"
        if grep -Eq '^xwayland: RUNNING [0-9]+$' <<< "$status" &&
           grep -Eq '^xfce: RUNNING [0-9]+$' <<< "$status" &&
           grep -Fq 'x11_desktop: READY ' <<< "$status";then
            echo S25_TOPAPP=DESKTOP_BECAME_READY_DURING_TIMEOUT
        else
            echo S25_TOPAPP=RECOVERING_FROM_FAILED_HELPER_SPAWN
            # A failed top-app attempt can leave only a half-started
            # project session. Stop only project-owned children, under
            # the same startup/stop lock; never start a second desktop
            # while one could still be running.
            for role in xwayland xfce clipboard ime labwc;do
                if ! grep -Fxq "$role: DOWN" <<< "$status";then
                    awstart --stop
                    break
                fi
            done
            status="$(awstart --status)"
            for role in xwayland xfce clipboard ime labwc;do
                grep -Fxq "$role: DOWN" <<< "$status" || {
                    echo "S25_TOPAPP=FALLBACK_BLOCKED_STALE_$role" >&2;exit 4;
                }
            done
            route=SHELL_FALLBACK
            launch_shell
        fi
    fi
fi
echo "S25_ACTUAL_LAUNCH_ROUTE=$route"
echo '=== S25_KGSL_TOPAPP_SESSION_CPUSET ==='
report="$(awstart --status)"
printf '%s\n' "$report"|grep -E '^host:|^xwayland:|^xfce:|^profile:|^X DISPLAY:'
pid="$(printf '%s\n' "$report"|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
if [[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/status" ]];then
    expected="$PROJECT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland"
    if ! tr '\0' ' ' < "/proc/$pid/cmdline" | grep -Fq "$expected"; then
        echo S25_TOPAPP_PRIVATE_KGSL_XWAYLAND=FAIL_WRONG_BINARY >&2
        exit 3
    fi
    echo S25_TOPAPP_PRIVATE_KGSL_XWAYLAND=PASS_EXPECTED_BINARY
    printf 'S25_XWAYLAND_CPUSET=%s\n' "$(cat "/proc/$pid/cpuset" 2>/dev/null)"
    grep '^Cpus_allowed_list:' "/proc/$pid/status" || true
else
    echo S25_TOPAPP=FAIL_XWAYLAND_PID_UNVERIFIED >&2
    exit 3
fi
if [[ "$route" == TOPAPP ]];then
    [[ ! -f "$MARKER" ]] || { echo 'S25_TOPAPP=ERROR_MARKER_NOT_CONSUMED';exit 3; }
    echo S25_TOPAPP_PRIVATE_KGSL_MARKER=CONSUMED_ONCE
else
    echo S25_TOPAPP=WARNING_SHELL_ROUTE_MAY_BE_CPUSET_0_TO_5
fi
# Previously the XFCE process disappeared ~26s after launch while the
# Xwayland process survived, leaving a black desktop. A single readiness
# snapshot is insufficient. Keep this command alive long enough to catch
# that delayed failure, with a useful diagnostic and no auto-restart loop.
expected_xwayland="$pid"
expected_xfce="$(printf '%s\n' "$report"|sed -nE 's/^xfce: RUNNING ([0-9]+)$/\1/p')"
[[ "$expected_xfce" =~ ^[0-9]+$ ]] || {
    echo S25_TOPAPP=FAIL_XFCE_NOT_RUNNING_AT_READINESS >&2;exit 3
}
echo S25_TOPAPP=VERIFYING_DESKTOP_STAYS_UP_AFTER_START
for check in $(seq 1 18);do
    sleep 2
    current="$(awstart --status)"
    if ! grep -Fxq "xwayland: RUNNING $expected_xwayland" <<< "$current" ||
       ! grep -Fxq "xfce: RUNNING $expected_xfce" <<< "$current" ||
       ! grep -Fq 'x11_desktop: READY ' <<< "$current";then
        echo "S25_TOPAPP=FAIL_DESKTOP_EXITED_AFTER_START_CHECK_$check" >&2
        echo "$current" | grep -E '^host:|^xwayland:|^xfce:|^x11_desktop:' >&2
        tail -22 "$STATE/logs/xfce.log" >&2 || true
        tail -15 "$STATE/logs/session-control.log" >&2 || true
        exit 4
    fi
    if ((check % 5 == 0));then
        echo "S25_TOPAPP=DESKTOP_STILL_READY_CHECK_$check"
    fi
done
echo "S25_DESKTOP_STABILITY_OBSERVATION=PASS_36_SECONDS_ROUTE_$route"
