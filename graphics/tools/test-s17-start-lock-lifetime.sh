#!/data/data/com.termux/files/usr/bin/bash
# S17 Alroot startup lock regression — READ-ONLY. After a successful desktop
# start, long-lived Xwayland/IME/XFCE/clipboard children MUST NOT inherit the
# parent's exclusive flock fd8; otherwise a crashed/orphaned server prevents
# a new desktop from starting and leaves the Android helper black.
# This test never kills processes, restarts XFCE, or opens applications.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
STATE="$HOME/.alroot-wayland"
LOCK="$STATE/start.lock"
[[ -f "$LOCK" && -r "$STATE/xwayland.pid" ]] || {
    echo 'S17_START_LOCK_LIFETIME=NO_ACTIVE_ALROOT_SESSION';exit 2;
}

# The original fault held the same open-file-description lock from deadlocked
# PID 12829, IME 12880 and clipboard xclip 12976. Acquire the file ourselves
# while the new, live Xwayland is running to verify the lock is short-lived.
flock -n "$LOCK" -c true || {
    echo 'S17_START_LOCK_LIFETIME=FAIL_HELD_BY_CHILD_OR_OTHER_START';exit 4;
}
echo 'S17_START_LOCK_AVAILABLE_WHILE_XFCE_RUNNING=PASS'

lock_id="$(stat -Lc '%d:%i' "$LOCK")"
for role in xwayland xfce clipboard ime labwc;do
    file="$STATE/$role.pid"
    [[ -r "$file" ]] || continue
    read -r pid ticks marker < "$file"
    [[ "$marker" == alroot-wayland && "$pid" =~ ^[0-9]+$ ]] || {
        echo "S17_START_LOCK_BAD_PID_MARKER=$role";exit 5;
    }
    [[ -r "/proc/$pid/cmdline" ]] || {
        echo "S17_START_LOCK_PID_MISSING=$role";exit 5;
    }
    matching=0
    for fd in "/proc/$pid/fd/"*;do
        [[ -e "$fd" ]] || continue
        id="$(stat -Lc '%d:%i' "$fd" 2>/dev/null || true)"
        [[ "$id" == "$lock_id" ]] && ((matching+=1)) || :
    done
    if ((matching));then
        echo "S17_START_LOCK_BACKGROUND_FD_LEAK=$role pid:$pid fd_count:$matching"
        exit 6
    fi
    echo "S17_START_LOCK_BACKGROUND_FD_CLEAN[$role]=PASS"
done

# The launcher source must contain the XFCE self-restart guard as well as
# a close-on-spawn for each project-owned long-running process.
for item in 'xfce.log' 'clipboard.log' 'ime-server.log' 'xwayland.log' 'labwc.log';do
    grep -F '8>&- &' "$ROOT/termux/alroot-wayland" |grep -Fq "$item" || {
        echo "S17_START_LOCK_BACKGROUND_CHILD_CLOSE_MISSING=$item";exit 7;
    }
done
grep -Fq 'Cannot restart from inside the Alroot XFCE session' "$ROOT/termux/awstart" || {
    echo 'S17_START_LOCK_XFCE_SELF_RESTART_GUARD_MISSING';exit 8;
}
echo 'S17_START_LOCK_ALL_BACKGROUND_SPAWNS_CLOSE_FD8=PASS'
echo 'S17_START_LOCK_SELF_RESTART_GUARD=PASS'
echo 'S17_START_LOCK_LIFETIME=PASS'
