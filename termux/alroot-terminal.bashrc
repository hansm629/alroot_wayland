# S42 interactive Bash startup only. Source user settings at top level so
# their aliases, functions and declare commands retain normal Bash semantics.
alroot_terminal_rc_prefix=/data/data/com.termux/files/usr
alroot_terminal_rc_state=/data/data/com.termux/files/home/.alroot-wayland
alroot_terminal_rc_project=/data/data/com.termux/files/home/alroot_wayland
alroot_terminal_rc_display="${DISPLAY:-}"
alroot_terminal_rc_auth="${XAUTHORITY:-}"
alroot_terminal_rc_target=
alroot_terminal_rc_active=0

if [[ "$-" == *i* && "${BASH:-}" -ef "$alroot_terminal_rc_prefix/bin/bash" &&
      "${ALROOT_CLIENT_GRAPHICS:-}" == wrapper-zink &&
      "${XDG_CONFIG_HOME:-}" == "$alroot_terminal_rc_state/xfce-config" &&
      "$alroot_terminal_rc_auth" == "$alroot_terminal_rc_state/Xauthority" &&
      -r "$alroot_terminal_rc_auth" && -r "$alroot_terminal_rc_state/display" ]]; then
    IFS= read -r alroot_terminal_rc_target < "$alroot_terminal_rc_state/display"
    if [[ "$alroot_terminal_rc_target" =~ ^:[0-9]{1,3}$ &&
          "$alroot_terminal_rc_target" != :0 &&
          ( "$alroot_terminal_rc_display" == "$alroot_terminal_rc_target" ||
            "$alroot_terminal_rc_display" == "$alroot_terminal_rc_target".* ) ]]; then
        alroot_terminal_rc_active=1
    fi
fi

[[ ! -r "$HOME/.bashrc" ]] || source "$HOME/.bashrc"

if [[ "$alroot_terminal_rc_active" == 1 ]]; then
    source "$alroot_terminal_rc_project/termux/graphics-defaults.sh"
    alroot_s42_apply_client "$alroot_terminal_rc_prefix"
    export DISPLAY="$alroot_terminal_rc_display" XAUTHORITY="$alroot_terminal_rc_auth"
fi

unset alroot_terminal_rc_prefix alroot_terminal_rc_state alroot_terminal_rc_project
unset alroot_terminal_rc_display alroot_terminal_rc_auth alroot_terminal_rc_target
unset alroot_terminal_rc_active
