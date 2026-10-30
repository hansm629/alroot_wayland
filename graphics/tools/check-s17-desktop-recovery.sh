#!/data/data/com.termux/files/usr/bin/bash
# Read-only health diagnostic for an Alroot XFCE Xwayland that stopped handling
# X11 requests after an unsupported cross-libc DRI3 shared-fence operation.
# This script NEVER sends signals, kills apps, restarts XFCE, or edits files.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
STATE="$HOME/.alroot-wayland"
[[ -r "$STATE/display" && -r "$STATE/Xauthority" && -r "$STATE/xwayland.pid" ]] || {
    echo 'S17_RECOVERY_CHECK=NO_ACTIVE_ALROOT_DISPLAY';exit 2;
}
IFS= read -r display < "$STATE/display"
read -r pid _ < "$STATE/xwayland.pid"
[[ "$display" =~ ^:[0-9]+$ && "$display" != :0 && "$pid" =~ ^[0-9]+$ ]] || {
    echo 'S17_RECOVERY_CHECK=INVALID_OR_GLOBAL_DISPLAY';exit 2;
}
[[ -r "/proc/$pid/cmdline" ]] || {
    echo 'S17_RECOVERY_CHECK=XWAYLAND_PROCESS_NOT_FOUND';exit 3;
}
cmdline="$(tr '\0' ' ' <"/proc/$pid/cmdline")"
if [[ "$cmdline" != *"Xwayland $display "* || "$cmdline" != *"$ROOT/.source/gpu-stage1/xwayland-kgsl/"* ]];then
    echo 'S17_RECOVERY_CHECK=NOT_PRIVATE_KGSL_XWAYLAND';exit 3
fi
printf 'S17_RECOVERY_MAIN_DISPLAY=%s\nS17_RECOVERY_XWAYLAND_PID=%s\n' "$display" "$pid"
if timeout -s TERM -k 1s 3s \
     env DISPLAY="$display" XAUTHORITY="$STATE/Xauthority" \
     xdpyinfo -display "$display" >/dev/null 2>&1;then
    echo 'S17_RECOVERY_X11_SERVER=RESPONSIVE'
    echo 'S17_RECOVERY_RESTART=NOT_REQUIRED'
    exit 0
fi
echo 'S17_RECOVERY_X11_SERVER=UNRESPONSIVE'
# Print only process IDs and names, not shell command lines/history or any
# X11 window contents. Even an apparently idle terminal may have unsaved work.
for app in xfce4-terminal blender gimp inkscape mousepad soffice.bin okular kate;do
    # Android's `ps -o comm` truncates long executable paths, hiding even
    # xfce4-terminal. Match the actual executable path prefix instead.
    for path_prefix in "$PREFIX/bin/" /usr/bin/ /usr/local/bin/;do
        while IFS= read -r app_pid;do
            [[ "$app_pid" =~ ^[0-9]+$ ]] || continue
            echo "S17_RECOVERY_USER_APP=$app pid:$app_pid"
        done < <(pgrep -f "^${path_prefix}${app}([[:space:]]|$)" 2>/dev/null || :)
    done
done
echo 'S17_RECOVERY_USER_WORK=SAVE_BEFORE_RESTART'
echo 'S17_RECOVERY_ACTION=MANUAL_ALROOT_ONLY_RESTART_REQUIRED'
echo 'S17_RECOVERY_NEW_BINARY=READY_IF_ALREADY_BUILT'
echo 'S17_RECOVERY_GLOBAL_TERMUX_X11=DO_NOT_TOUCH'
exit 4
