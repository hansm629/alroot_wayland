#!/data/data/com.termux/files/usr/bin/bash
# Build a GLAMOR+DRI3-capable Bionic Xwayland in .source only.
# NEVER overwrite $PREFIX/bin/Xwayland or change Termux-X11 DRI3.
# Xwayland GLAMOR starting successfully is a SEPARATE runtime acceptance gate.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
VERSION=24.1.14
SHA=4eb6b98d678299a4b96138d886345b294130695760345ef84596225a8224078a
WORK="$ROOT/.source/gpu-stage1/xwayland"
mkdir -p "$WORK"
tarball="$WORK/xwayland-$VERSION.tar.xz"
if [[ ! -s "$tarball" ]]; then
    curl --fail --location --retry 1 --connect-timeout 12 --max-time 90 \
        "https://xorg.freedesktop.org/releases/individual/xserver/xwayland-$VERSION.tar.xz" \
        -o "$tarball"
fi
echo "$SHA  $tarball" | sha256sum -c
if [[ ! -f "$WORK/source/meson.build" ]]; then
    mkdir -p "$WORK/source"
    tar -xf "$tarball" -C "$WORK/source" --strip-components=1
fi
for fix in "$ROOT/graphics/patches/xwayland/termux"/000*.patch; do
    if (cd "$WORK/source" && patch -s --dry-run -p1 -i "$fix" >/dev/null 2>&1); then
        (cd "$WORK/source" && patch -s -p1 -i "$fix")
        echo "Applied private Android compatibility patch $(basename "$fix")"
    elif (cd "$WORK/source" && patch -s --dry-run -R -p1 -i "$fix" >/dev/null 2>&1); then
        echo "Private Android patch already applied: $(basename "$fix")"
    else
        echo "BLOCKED: $(basename "$fix") no longer matches the pinned Xwayland source" >&2
        exit 2
    fi
done
export CFLAGS='-fcommon -fPIC -DFNDELAY=O_NDELAY -Wno-int-to-pointer-cast -Wno-implicit-function-declaration'
export CPPFLAGS="-I$PREFIX/include/libdrm"
export LDFLAGS='-landroid-shmem'
if [[ ! -f "$WORK/build/build.ninja" ]]; then
    python3 -c 'from mesonbuild import mesonmain; import sys; sys.argv=["meson"]+sys.argv[1:]; sys.exit(mesonmain.main())' \
        setup "$WORK/build" "$WORK/source" --prefix="$WORK/prefix" \
        -Dmitshm=true -Dxres=true -Dxv=true -Dsecure-rpc=false \
        -Dscreensaver=true -Dxdmcp=true -Dglx=true -Ddri3=true \
        -Dxinerama=true -Dxace=true -Dxcsecurity=true \
        -Dxf86bigfont=true -Ddrm=true -Dglamor=true -Dxvfb=false \
        -Dlibunwind=false -Dipv6=true -Dsha1=libcrypto \
        -Ddefault_font_path="$PREFIX/share/fonts"
fi
ninja -C "$WORK/build" -j "${ALROOT_BUILD_JOBS:-2}" hw/xwayland/Xwayland
binary="$WORK/build/hw/xwayland/Xwayland"
[[ -x "$binary" ]] || exit 2
echo "PASS: private GLAMOR+DRI3 Xwayland built at $binary"
echo 'WARNING: no private GPU runtime/DRI3/Wayland buffer export acceptance yet'
echo 'Global Termux Xwayland, Mesa and Vulkan drivers not installed or replaced'
