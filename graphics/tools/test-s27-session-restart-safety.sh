#!/data/data/com.termux/files/usr/bin/bash
# Offline and live read-only safety checks for S27 session-control recovery.
# Must never stop, restart, or install any process.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
bash -n termux/awstart termux/alroot-wayland termux/start-s25-kgsl-topapp.sh

python3 - <<'PY'
from pathlib import Path
a=Path('termux/awstart').read_text()
c=Path('termux/alroot-wayland').read_text()
s=Path('termux/start-s25-kgsl-topapp.sh').read_text()
assert 'Cannot stop from inside the running Alroot XFCE terminal' in a
assert 'flock -w 25 8' in c
assert 'control_event STOP_BEGIN' in c
assert 'control_event STOP_COMPLETE' in c
assert 'control_event START_BEGIN' in c
assert 'control_event START_READY' in c
assert 'owned_pid_active "$role"' in c
assert 'preserving PID record for safe recovery' in c
assert '--restart) restart=1' in s
assert '--shell) use_shell=1' in s
assert 'S25_TOPAPP=HELPER_ACKNOWLEDGED_BUT_DESKTOP_NOT_READY' in s
assert 'S25_TOPAPP=FALLBACK_BLOCKED_STALE_' in s
assert 'S25_ACTUAL_LAUNCH_ROUTE=' in s
assert 'S25_DESKTOP_STABILITY_OBSERVATION=PASS_36_SECONDS_ROUTE_' in s
assert 'awstart --direct --no-topapp' in s
assert 'awstart --direct --topapp' in s
print('S27_SESSION_CONTROL_SOURCE_CONTRACT=PASS')
print('S27_TOPAPP_FAILURE_EXPLICIT_SHELL_FALLBACK=PASS')
print('S27_ORPHANED_XWAYLAND_PID_SAFETY=PASS')
PY

status_before="$(awstart --status)"
before="$(printf '%s\n' "$status_before" | grep -E '^xwayland:|^xfce:|^clipboard:|^ime:')"
display="$(printf '%s\n' "$status_before" | sed -nE 's/^X DISPLAY: (:[0-9]+)$/\1/p')"
if [[ -n "$display" && "$status_before" == *'xfce: RUNNING '* ]];then
    # Simulate an XFCE terminal's scoped environment. This must REFUSE to
    # kill its own desktop, and leave Xwayland/IME/clipboard untouched.
    if env DISPLAY="$display" XDG_CONFIG_HOME="$HOME/.alroot-wayland/xfce-config" \
        bash termux/awstart --stop >/dev/null 2>&1;then
        echo S27_XFCE_TERMINAL_STOP_GUARD=FAIL >&2;exit 2
    fi
    echo S27_XFCE_TERMINAL_STOP_GUARD=PASS
    if bash termux/start-s25-kgsl-topapp.sh --shell > /dev/null 2>&1;then
        echo S27_HEALTHY_SESSION_NO_IMPLICIT_RESTART=FAIL >&2;exit 2
    fi
    echo S27_HEALTHY_SESSION_NO_IMPLICIT_RESTART=PASS
fi
status_after="$(awstart --status)"
after="$(printf '%s\n' "$status_after" | grep -E '^xwayland:|^xfce:|^clipboard:|^ime:')"
[[ "$before" == "$after" ]] || {
    echo S27_EXISTING_SESSION_PID_CHANGED=FAIL >&2;exit 3
}
[[ ! -e "$HOME/.alroot-wayland/control/s25-kgsl-topapp-once" ]] || {
    echo S27_ONE_SHOT_MARKER_UNEXPECTED=FAIL >&2;exit 3
}
echo S27_EXISTING_DESKTOP_UNCHANGED=PASS
echo S27_SHELL_LAUNCH_HEALTH=TESTED_SEPARATELY_ON_DEVICE
