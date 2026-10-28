#!/data/data/com.termux/files/usr/bin/bash
# Private A/B test: original Termux VTE vs scoped patched VTE on Xvfb.
# No user :0/:90 injection, source rewrite, or global package installation.
set -euo pipefail
ROOT=/data/data/com.termux/files/home/alroot_wayland
PREFIX=/data/data/com.termux/files/usr
STATE=/data/data/com.termux/files/home/.alroot-wayland
cd "$ROOT"
bash termux/build-vte-caret.sh > /dev/null
mkdir -p .source
work="$(mktemp -d "$ROOT/.source/p14-caret-test.XXXXXXXX")"
chmod 700 "$work"
xvfb_pid=""
cleanup() {
    [[ -z "$xvfb_pid" ]] || kill -TERM "$xvfb_pid" 2>/dev/null || true
}
trap cleanup EXIT
display=""
for n in 215 216 217 218 219; do
    if [[ ! -S "$PREFIX/tmp/.X11-unix/X$n" ]]; then
        display=":$n"
        break
    fi
done
[[ -n "$display" ]] || { echo 'No free private Xvfb display' >&2; exit 2; }
auth="$work/Xauthority"
touch "$auth"
chmod 600 "$auth"
xauth -f "$auth" add "$display" MIT-MAGIC-COOKIE-1 \
    "$(openssl rand -hex 16)"
Xvfb "$display" -screen 0 900x300x24 -auth "$auth" -nolisten tcp \
    > "$work/xvfb.log" 2>&1 &
xvfb_pid=$!
sleep .5
clang -O1 -Wno-deprecated-declarations tests/p14-vte-caret-probe.c \
    -o "$work/probe" $(pkg-config --cflags --libs vte-2.91)
export DISPLAY="$display" XAUTHORITY="$auth"
export XDG_CONFIG_HOME="$work/config"
export XDG_RUNTIME_DIR="$work/runtime"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export FCITX_DATA_HOME="$STATE/fcitx-data"
export FCITX_ADDON_DIRS="$STATE/fcitx-addon:$PREFIX/lib/fcitx5"
export ALROOT_FCITX_BRIDGE_SOCKET="$work/fcitx.sock"
export GTK_IM_MODULE=fcitx QT_IM_MODULE=fcitx XMODIFIERS=@im=fcitx
dbus-run-session -- bash tests/p14-vte-caret-inner.sh "$work"
python3 - "$work" <<'PY'
from PIL import Image, ImageChops
from pathlib import Path
import sys
folder=Path(sys.argv[1])
def compare(phase):
    original=Image.open(folder/f"original-{phase}.png").convert("RGB")
    patched=Image.open(folder/f"patched-{phase}.png").convert("RGB")
    assert original.size==patched.size, "Patched VTE changed window geometry"
    diff=ImageChops.difference(original,patched)
    count=sum(pixel!=(0,0,0) for pixel in diff.getdata())
    print(f"P14_{phase.upper()}_DIFFERENT_PIXELS={count} bbox={diff.getbbox()}")
    return count
preedit=compare("preedit")
committed=compare("committed")
assert 1 <= preedit <= 1500, "Expected cursor-only difference during Hangul"
assert committed == 0, "Committed glyphs changed unexpectedly"
print("PASS: caret added only during Korean preedit, committed appearance identical")
PY
echo 'PASS: Original Termux VTE unchanged; only Alroot private VTE differs'
