#!/data/data/com.termux/files/usr/bin/bash
# Verify opt-in S26U helper Activity launch contract WITHOUT starting a new
# desktop, changing an installed APK, or creating a session marker.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
bash -n termux/alroot-wayland termux/start-s25-kgsl-topapp.sh
python3 - <<'PY'
from pathlib import Path
start=Path('termux/start-s25-kgsl-topapp.sh').read_text()
guest=Path('termux/alroot-wayland').read_text()
java=Path('app/src/main/java/com/alroot/wayland/TermuxSession.java').read_text()
assert 'env.clear();' in java
assert 's25-kgsl-topapp-once' in start and 's25-kgsl-topapp-once' in guest
assert 'S25_KGSL_TOPAPP_ONCE' in guest and 'S25_KGSL_TOPAPP_ONCE' in start
assert 'awstart --direct --topapp' in start
assert 'getprop ro.product.model' in start and 'getprop ro.product.model' in guest
assert '$(id -u):600' in guest and '$(id -u):700' in start
assert 's25_age' in guest and 's25_age" -gt 90' in guest
assert 'rm -f -- "$s25_marker"' in guest
assert 'ALROOT_XWAYLAND_BINARY="$s25_xwayland"' in guest
assert 'ALROOT_XWAYLAND_DUAL_ABI_FENCE=1' in guest
assert 'XWAYLAND_FORCE_KGSL_SURFACELESS=1' in guest
assert 'FD_KGSL_ENABLE_DMABUF=1' in guest
assert 'ALROOT_XWAYLAND_GLAMOR=es' in guest
assert 'ALROOT_XWAYLAND_BINARY' in start or 'S25_TOPAPP_PRIVATE_KGSL_XWAYLAND=PASS' in start
assert 'S25_TOPAPP=BLOCKED_ALREADY_RUNNING_' in start
assert 'INSTALLED (matches latest locally signed APK)' in start
print('S25_TOPAPP_ONE_SHOT_MARKER_SOURCE=PASS')
print('S25_TOPAPP_S26U_GPU_CAPABILITY_ISOLATION=PASS')
print('S25_TOPAPP_SANITIZED_PROCESSBUILDER_COMPAT=PASS')
PY
marker="$HOME/.alroot-wayland/control/s25-kgsl-topapp-once"
[[ ! -e "$marker" && ! -L "$marker" ]] || {
    echo S25_TOPAPP_TEST=BLOCKED_USER_MARKER_ALREADY_EXISTS >&2;exit 2;
}
# While the signed APK has not been approved, this command must fail before
# mutating ANY process, file or service. The code test does not install it.
if [[ "$(awstart --status | head -1)" != *'(matches latest locally signed APK)' ]];then
    output="$(bash termux/start-s25-kgsl-topapp.sh 2>&1)" && {
        echo S25_TOPAPP_GUARD=UNEXPECTED_LAUNCH >&2;exit 2;
    }
    grep -Fq 'S25_TOPAPP=BLOCKED_INSTALL_NEW_SIGNED_S25_APK_FIRST' <<< "$output"
    [[ ! -e "$marker" && ! -L "$marker" ]] || exit 2
    echo S25_TOPAPP_REJECTS_UNINSTALLED_CANDIDATE=PASS
fi
echo S25_TOPAPP_EXISTING_WAYLAND_HOST=NOT_TOUCHED
echo S25_TOPAPP_ACTUAL_CPUSET=NOT_MEASURED_BY_SOURCE_ONLY_TEST
