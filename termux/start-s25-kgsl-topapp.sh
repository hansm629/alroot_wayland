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
[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo 'S25_TOPAPP=BLOCKED_NOT_S26_ULTRA' >&2;exit 2;
}
status="$(awstart --status)"
grep -Fqx '[awstart] Android helper APK: INSTALLED (matches latest locally signed APK)' <<< "$status" || {
    echo 'S25_TOPAPP=BLOCKED_INSTALL_NEW_SIGNED_S25_APK_FIRST' >&2;exit 2;
}
for role in xwayland xfce;do
    grep -Fxq "$role: DOWN" <<< "$status" || {
        echo "S25_TOPAPP=BLOCKED_ALREADY_RUNNING_$role" >&2;exit 2;
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
awstart --direct --topapp
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
[[ ! -f "$MARKER" ]] || { echo 'S25_TOPAPP=ERROR_MARKER_NOT_CONSUMED';exit 3; }
echo S25_TOPAPP_PRIVATE_KGSL_MARKER=CONSUMED_ONCE
