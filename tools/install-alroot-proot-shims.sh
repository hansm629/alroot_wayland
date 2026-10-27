#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
HOME=/data/data/com.termux/files/home
STATE="$HOME/.alroot-wayland"
SOURCE="$HOME/alroot_wayland/termux/aw-proot"
[[ -x "$SOURCE" ]] || { echo "Alroot PRoot shim missing" >&2; exit 1; }
mkdir -p "$STATE/bin"
chmod 700 "$STATE/bin"
for name in mpd mprun pdrun; do
    dest="$STATE/bin/$name"
    if [[ -e "$dest" || -L "$dest" ]]; then
        [[ -L "$dest" && "$(readlink "$dest")" == "$SOURCE" ]] || {
            echo "Refusing to overwrite custom Alroot shim $dest" >&2
            exit 1
        }
    else
        ln -s "$SOURCE" "$dest"
    fi
done
echo "Alroot-only PRoot shims ready; original Termux mpd/mprun/pdrun untouched"
