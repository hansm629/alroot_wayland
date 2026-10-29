#!/data/data/com.termux/files/usr/bin/bash
# Read-only, pinned Mesa 27 provenance and private runtime status.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
SRC="$ROOT/.source/gpu-stage27-pr25/source"
PIN=6f90e92dfdacc4759d8e9ccb328805d0f0b4439b
[[ -d "$SRC/.git" ]] || { echo 'S14_MESA27_SOURCE=NOT_CHECKED_OUT';exit 2; }
ACTUAL="$(git -C "$SRC" rev-parse HEAD)"
VERSION="$(cat "$SRC/VERSION")"
echo "S14_MESA27_PIN_EXPECTED=$PIN"
echo "S14_MESA27_PIN_ACTUAL=$ACTUAL"
echo "S14_MESA27_VERSION=$VERSION"
[[ "$ACTUAL" == "$PIN" && "$VERSION" == 27.0.0-devel ]] || exit 2
sha256sum -c "$ROOT/graphics/patches/mesa/pr25/SHA256SUMS" >/dev/null || exit 2
echo 'S14_MESA27_PR25_PACKAGE_PATCHES=14_HASH_VERIFIED'
echo 'S14_MESA27_PRIVATE_TLS_POLICY=ANDROID35_ELF_TLS_NO_LEGACY_EMUTLS'
for p in "$ROOT"/graphics/patches/mesa/egl/000{1,2,3}-*.patch; do
  if git -C "$SRC" apply --check "$p"; then
    echo "S14_MESA27_PATCH_$(basename "$p" | cut -d- -f1)=APPLIES"
  else
    echo "S14_MESA27_PATCH_$(basename "$p" | cut -d- -f1)=REBASE_REQUIRED"
    exit 2
  fi
done
WORK="$ROOT/.source/gpu-stage27-pr25/mesa-private"
for name in libEGL_mesa.so libgallium-27.0.0-devel.so; do
  path="$WORK/private-runtime/lib/$name"
  if [[ -f "$path" ]]; then
    echo "S14_MESA27_RUNTIME_${name}=PRESENT"
  else
    echo "S14_MESA27_RUNTIME_${name}=NOT_BUILT"
  fi
done
pkgver="$(dpkg-query -W -f='${Version}' mesa 2>/dev/null || true)"
echo "S14_TERMUX_SYSTEM_MESA=$pkgver"
