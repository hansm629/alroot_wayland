#!/data/data/com.termux/files/usr/bin/bash
# Alroot-only VTE 0.84.1 native composing cursor repair.
# VTE is LGPL-3.0-or-later; do NOT modify Termux global libraries or :0.
set -euo pipefail
PREFIX=/data/data/com.termux/files/usr
HOME=/data/data/com.termux/files/home
PROJECT="$HOME/alroot_wayland"
STATE="$HOME/.alroot-wayland"
VTE_RELEASE=0.84.1
VTE_REV=f2223e45d7f04c5383c59e76772501f0ed4215bc
SRC="$PROJECT/.source/p14-vte-src"
BUILD="$PROJECT/.source/p14-vte-build"
PATCH="$PROJECT/termux/patches/vte-0.84.1-caret.patch"
DEST="$STATE/vte-private"
WRAPPER="$PROJECT/termux/alroot-terminal.sh"
LINK="$STATE/bin/xfce4-terminal"
cd "$PROJECT"
[[ "$(pkg-config --modversion vte-2.91)" == "$VTE_RELEASE" ]] || {
    echo "Unsupported Termux VTE version; declining private ABI override" >&2
    exit 2
}
for tool in git clang++ ninja pkg-config python3; do
    command -v "$tool" >/dev/null || {
        echo "Missing private VTE build tool: $tool" >&2
        exit 2
    }
done
python3 -c "import mesonbuild" || {
    echo "Meson Python module missing; no global dependencies changed" >&2
    exit 2
}
mkdir -p "$PROJECT/.source" "$DEST" "$STATE/bin"
chmod 700 "$DEST" "$STATE/bin"
if [[ ! -d "$SRC/.git" ]]; then
    git clone --depth 1 --branch "$VTE_RELEASE" \
        https://github.com/GNOME/vte.git "$SRC"
fi
[[ "$(git -C "$SRC" rev-parse HEAD)" == "$VTE_REV" ]] || {
    echo "VTE pinned SHA mismatch; refusing to patch unreviewed source" >&2
    exit 2
}
if git -C "$SRC" apply --reverse --check "$PATCH" >/dev/null 2>&1; then
    echo 'Private VTE cursor patch already applied'
else
    git -C "$SRC" apply --check "$PATCH"
    git -C "$SRC" apply "$PATCH"
fi
if [[ ! -f "$BUILD/build.ninja" ]]; then
    python3 -m mesonbuild.mesonmain setup "$BUILD" "$SRC" \
      --prefix="$DEST" \
      -Dgtk3=true -Dgtk4=false \
      -Da11y=true -Dgnutls=true -Dicu=true -Dfribidi=true \
      -Dapp=false -Ddocs=false -Dgir=false -Dvapi=false \
      -Dglade=false -Dterminfo=false
else
    python3 -m mesonbuild.mesonmain configure "$BUILD" \
      -Dgtk3=true -Dgtk4=false \
      -Da11y=true -Dgnutls=true -Dicu=true -Dfribidi=true \
      -Dapp=false -Ddocs=false -Dgir=false -Dvapi=false \
      -Dglade=false -Dterminfo=false
fi
ninja -C "$BUILD" -j2 src/libvte-2.91.so
lib="$BUILD/src/libvte-2.91.so"
[[ -s "$lib" ]] || exit 2
readelf -Ws "$lib" > "$BUILD/p14-vte-symbols.txt"
grep -q 'vte_terminal_spawn_async' "$BUILD/p14-vte-symbols.txt" || {
    echo "Private VTE missing GTK3 ABI symbol" >&2
    exit 2
}
temp="$DEST/.libvte-2.91.so.0.$$"
trap 'rm -f "$temp"' EXIT
cp "$lib" "$temp"
chmod 700 "$temp"
mv -f "$temp" "$DEST/libvte-2.91.so.0"
ln -sfn libvte-2.91.so.0 "$DEST/libvte-2.91.so"
if [[ -e "$LINK" || -L "$LINK" ]]; then
    [[ -L "$LINK" && "$(readlink "$LINK")" == "$WRAPPER" ]] || {
        echo "Refusing to overwrite unrelated XFCE terminal launcher" >&2
        exit 2
    }
else
    ln -s "$WRAPPER" "$LINK"
fi
chmod 700 "$WRAPPER"
echo "PASS: private VTE $VTE_RELEASE caret patch installed for Alroot XFCE terminal"
echo "Private lib: $DEST/libvte-2.91.so.0"
echo "Scoped launcher: $LINK"
