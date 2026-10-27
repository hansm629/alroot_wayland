#!/data/data/com.termux/files/usr/bin/bash
# Compile the persistent X11 input bridge inside the Alroot project only.
set -euo pipefail
PREFIX=/data/data/com.termux/files/usr
HOME=/data/data/com.termux/files/home
project="$HOME/alroot_wayland"
source="$project/termux/alroot-ime-server.c"
output="$project/packages/bin/alroot-ime-server"
mkdir -p "$project/packages/bin"
if [[ -x "$output" && "$output" -nt "$source" ]]; then
    echo 'IME-SERVER binary up to date'
    exit 0
fi
temporary="$project/packages/bin/.alroot-ime-server.$$"
trap 'rm -f "$temporary"' EXIT
"$PREFIX/bin/clang" -O2 -Wall -Wextra -Wno-unused-parameter \
    "$source" -o "$temporary" $(pkg-config --cflags --libs x11 xtst)
chmod 700 "$temporary"
mv -f "$temporary" "$output"
echo "IME-SERVER built at $output"
