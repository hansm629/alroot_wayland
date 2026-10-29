#!/data/data/com.termux/files/usr/bin/bash
# Fetch precisely the commit selected in lfdevs/termux-packages PR #25.
# Creates only ignored .source data; never installs a system Mesa package.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
BASE="$ROOT/.source/gpu-stage27-pr25"
SRC="$BASE/source"
URL='https://github.com/lfdevs/mesa-for-android-container.git'
PIN=6f90e92dfdacc4759d8e9ccb328805d0f0b4439b
RECIPE_SHA=d03437f224ce3a85cc7f719bd79bb9e5cc2e6237
sha256sum -c graphics/patches/mesa/pr25/SHA256SUMS >/dev/null || exit 2
mkdir -p "$BASE"
if [[ ! -d "$SRC/.git" ]]; then
  if [[ -e "$SRC" ]]; then
    echo 'S14_MESA27_SOURCE=UNRECOGNIZED_EXISTING_DIRECTORY' >&2
    exit 2
  fi
  git init -q "$SRC"
  git -C "$SRC" remote add origin "$URL"
fi
[[ "$(git -C "$SRC" remote get-url origin)" == "$URL" ]] || {
  echo 'S14_MESA27_SOURCE=WRONG_REMOTE' >&2;exit 2;
}
[[ -z "$(git -C "$SRC" status --porcelain)" ]] || {
  echo 'S14_MESA27_SOURCE=DIRTY_REFUSE_TO_SWITCH' >&2;exit 2;
}
if [[ "$(git -C "$SRC" rev-parse HEAD 2>/dev/null || true)" != "$PIN" ]]; then
  GIT_TERMINAL_PROMPT=0 git -C "$SRC" fetch --depth=1 --filter=blob:none origin "$PIN"
  git -C "$SRC" checkout --detach FETCH_HEAD
fi
[[ "$(git -C "$SRC" rev-parse HEAD)" == "$PIN" &&
   "$(cat "$SRC/VERSION")" == '27.0.0-devel' ]] || exit 2
cat > "$BASE/PR25_PROVENANCE.txt" <<META
User-selected recipe https://github.com/lfdevs/termux-packages/pull/25
Recipe head used for exact patch snapshots: $RECIPE_SHA
Mesa git origin: $URL
Mesa tree: $PIN
Mesa VERSION: 27.0.0-devel
Package patch SHA256 manifest: $ROOT/graphics/patches/mesa/pr25/SHA256SUMS
Private build only; system Mesa 26.3 and Termux-X11 must not be replaced.
META
echo 'S14_MESA27_BOOTSTRAP=PINNED_SOURCE_READY'
echo "S14_MESA27_RECIPE_HEAD=$RECIPE_SHA"
echo "S14_MESA27_PIN=$PIN"
