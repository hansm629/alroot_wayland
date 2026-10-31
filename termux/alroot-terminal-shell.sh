#!/data/data/com.termux/files/usr/bin/bash
# Default command in Alroot's private Xfce Terminal channel. Explicit
# terminal --command/--execute selections bypass this upstream preference.
set -eu
alroot_terminal_prefix=/data/data/com.termux/files/usr
alroot_terminal_project=/data/data/com.termux/files/home/alroot_wayland
alroot_terminal_config=/data/data/com.termux/files/home/.alroot-wayland/xfce-config
alroot_terminal_shell="${SHELL:-$alroot_terminal_prefix/bin/bash}"

if (( $# == 0 )) && [[ "$alroot_terminal_shell" -ef "$alroot_terminal_prefix/bin/bash" &&
     "${ALROOT_CLIENT_GRAPHICS:-}" == wrapper-zink &&
     "${XDG_CONFIG_HOME:-}" == "$alroot_terminal_config" ]]; then
    exec "$alroot_terminal_shell" --rcfile \
        "$alroot_terminal_project/termux/alroot-terminal.bashrc" -i
fi
# In a non-S42 session, with a non-Bash SHELL, or with explicit arguments,
# preserve the selected shell and its normal startup behavior.
exec "$alroot_terminal_shell" "$@"
