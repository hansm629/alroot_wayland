#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
src="/data/data/com.termux/files/home/alroot_wayland/termux/alroot-wayland"
dst="/data/data/com.termux/files/usr/bin/alroot-wayland"
[[ -x "$src" ]] || { echo "CLI source missing: $src" >&2; exit 1; }
if [[ -e "$dst" || -L "$dst" ]]; then
    [[ -L "$dst" && "$(readlink "$dst")" == "$src" ]] || {
        echo "BLOCKED: $dst exists, refusing to overwrite another launcher" >&2
        exit 1
    }
else
    ln -s "$src" "$dst"
fi
echo "PASS: project-only launcher: $dst"
