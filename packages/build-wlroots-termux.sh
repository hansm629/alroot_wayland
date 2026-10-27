#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

PROJECT=/data/data/com.termux/files/home/alroot_wayland
SOURCE="$PROJECT/.source/wlroots-0.18.2"
BUILD="$PROJECT/.source/wlroots-build"
PREFIX="$PROJECT/packages/prefix"
PATCH="$PROJECT/packages/patches/wlroots-0.18.2-bionic-shm-xwayland-sockets.patch"
EXPECTED=cda69b696d65a53d5d5e75dfed059a3803e0d700
UPSTREAM=https://gitlab.freedesktop.org/wlroots/wlroots.git

if [[ ! -d "$SOURCE/.git" ]]; then
    mkdir -p "$PROJECT/.source"
    git clone --depth 1 --branch 0.18.2 "$UPSTREAM" "$SOURCE"
fi
[[ "$(git -C "$SOURCE" rev-parse HEAD)" == "$EXPECTED" ]] || {
    echo "BLOCKED: wlroots source commit differs from pinned $EXPECTED" >&2
    exit 1
}
if git -C "$SOURCE" apply --reverse --check "$PATCH" >/dev/null 2>&1; then
    echo 'Android scoped SHM patch already present.'
else
    git -C "$SOURCE" apply --check "$PATCH"
    git -C "$SOURCE" apply "$PATCH"
fi

python3 -c 'import mesonbuild' || {
    echo 'Install pinned Meson (python3 -m pip install --user meson==1.11.2)' >&2
    exit 1
}
if [[ -e "$BUILD/build.ninja" ]]; then
    python3 -m mesonbuild.mesonmain configure "$BUILD" -Dxwayland=enabled
else
    python3 -m mesonbuild.mesonmain setup "$BUILD" "$SOURCE" \
        --prefix="$PREFIX" --libdir=lib \
        -Dauto_features=disabled -Dbackends=[] -Drenderers=[] \
        -Dsession=disabled -Dxwayland=enabled -Dexamples=false -Dwerror=false
fi
ninja -C "$BUILD" -j2
ninja -C "$BUILD" install
echo "PASS: project-private wlroots at $PREFIX/lib/libwlroots-0.18.so"
echo 'Run: LD_LIBRARY_PATH=$HOME/alroot_wayland/packages/prefix/lib labwc -h'
