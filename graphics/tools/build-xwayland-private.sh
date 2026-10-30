#!/data/data/com.termux/files/usr/bin/bash
# Build a GLAMOR+DRI3-capable Bionic Xwayland in .source only.
# NEVER overwrite $PREFIX/bin/Xwayland or change Termux-X11 DRI3.
# Xwayland GLAMOR starting successfully is a SEPARATE runtime acceptance gate.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
VERSION=24.1.14
SHA=4eb6b98d678299a4b96138d886345b294130695760345ef84596225a8224078a
case "${ALROOT_XWAYLAND_VARIANT:-generic}" in
generic) WORK="$ROOT/.source/gpu-stage1/xwayland" ;;
kgsl) WORK="$ROOT/.source/gpu-stage1/xwayland-kgsl" ;;
*) echo "ALROOT_XWAYLAND_VARIANT must be generic or kgsl" >&2; exit 2 ;;
esac
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
    # Old no-context Xorg insertion hunks (0003/0004) could silently apply
    # AGAIN during a second build. Probe a stable semantic marker before
    # patch, not only patch(1)'s positional dry run, to keep repeated builds
    # byte-for-byte idempotent. All other patches keep strict dry-run checks.
    if [[ "$fix" == *"0003-android-termux-tmpdir-server-lock.patch" ]] &&
       grep -q 'Android Xwayland needs an absolute TMPDIR' "$WORK/source/os/utils.c"; then
        echo 'Private Termux TMPDIR lock patch already applied (marker verified)'
        continue
    fi
    if [[ "$fix" == *"0004-android-atomic-no-replace-lock.patch" ]] &&
       grep -q 'Android app data can forbid hard links' "$WORK/source/os/utils.c"; then
        echo 'Private Android atomic lock patch already applied (marker verified)'
        continue
    fi
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
if [[ "${ALROOT_XWAYLAND_VARIANT:-generic}" == kgsl ]]; then
    # Optional Adreno adaptation. Never the default for Mali/PowerVR/Xclipse.
    for private in "$ROOT/graphics/patches/xwayland/kgsl"/000*.patch; do
        # The S28 GPU resize-preservation experiment is separately linked,
        # never part of the existing opt-in KGSL server build.
        if [[ "$private" == *"0005-kgsl-gpu-window-resize-frame-preserve.patch" ||
              "$private" == *"0006-kgsl-native-pixel-resize-preserve.patch" ]]; then
            echo 'S28/S29 GPU resize-preserve patches are separate opt-in candidates.'
            continue
        fi
        # The private wrapper AHB import adapter is a zero-context patch
        # to preserve Git whitespace checks. Do not reapply its insertions
        # when rebuilding an already-patched Xwayland source.
        if [[ "$private" == *"0004-kgsl-ahb-socket-dri3-24.1.14.patch" ]] &&
           grep -Fq 'XWL_KGSL_AHB_SOCKET_MODIFIER 1255ULL'                "$WORK/source/hw/xwayland/xwayland-glamor-gbm.c"; then
            echo 'Private KGSL AHB socket DRI3 adapter already applied (marker verified)'
            continue
        fi
        if (cd "$WORK/source" && patch -s --dry-run -p1 -i "$private" >/dev/null 2>&1); then
            (cd "$WORK/source" && patch -s -p1 -i "$private")
            echo "Applied optional private KGSL patch $(basename "$private")"
        elif (cd "$WORK/source" && patch -s --dry-run -R -p1 -i "$private" >/dev/null 2>&1); then
            echo "Optional private KGSL patch already applied $(basename "$private")"
        else
            echo "Private KGSL patch no longer applies to pinned Xwayland 24.1.14: $private" >&2
            exit 2
        fi
    done
fi
if [[ "${ALROOT_XWAYLAND_VARIANT:-generic}" == kgsl ]] &&
   grep -Eq 'S28_KGSL_RESIZE_PRESERVE|S29_KGSL_NATIVE_RESIZE' "$WORK/source/hw/xwayland/xwayland-window.c"; then
    echo 'BLOCKED: S28/S29 private overlay active; use the corresponding separate build helper to protect S16/S27 Xwayland.' >&2
    exit 2
fi
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
if [[ "${ALROOT_XWAYLAND_VARIANT:-generic}" == kgsl ]]; then
    echo 'KGSL remains opt-in; XWAYLAND_FORCE_KGSL_SURFACELESS=1 required'
fi
echo 'Global Termux Xwayland, Mesa and Vulkan drivers not installed or replaced'
