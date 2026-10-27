#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
HOME=/data/data/com.termux/files/home
STATE="$HOME/.alroot-wayland"
PREFIX=/data/data/com.termux/files/usr
export GDK_BACKEND=x11
export XDG_CONFIG_HOME="$STATE/xfce-config"
# The live user's notes in ~/.local/share/notes are NEVER opened/written by
# the Alroot instance. Data was snapshot-copied on the first startup.
export XDG_DATA_HOME="$STATE/notes-data"
exec "$PREFIX/bin/xfce4-notes"
