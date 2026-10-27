#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

home=/data/data/com.termux/files/home
prefix=/data/data/com.termux/files/usr
project="$home/alroot_wayland"
for name in alroot-wayland awstart; do
    source="$project/termux/$name"
    destination="$prefix/bin/$name"
    [[ -x "$source" ]] || { echo "Missing executable: $source" >&2; exit 1; }
    if [[ -e "$destination" || -L "$destination" ]]; then
        [[ -L "$destination" && "$(readlink "$destination")" == "$source" ]] || {
            echo "BLOCKED: refusing to overwrite existing $destination" >&2
            exit 1
        }
    else
        ln -s "$source" "$destination"
    fi
    echo "PASS: installed project-only command $destination"
done
