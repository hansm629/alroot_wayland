#!/data/data/com.termux/files/usr/bin/bash
# Isolated S1.6 KGSL recycled-pixmap/full-damage GPU-copy acceptance.
# Does not install APK, change global Xwayland/Termux, or stop the user XFCE.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
OUT="$ROOT/.source/gpu-stage27-pr25/s16-xwayland-gpu/full-frame-acceptance"
mkdir -p "$OUT"

# Ensure no accidental second application of context-free Xorg patches,
# e.g. repeated TMPDIR declarations or duplicate hardlink fallbacks.
source="$ROOT/.source/gpu-stage1/xwayland-kgsl/source/os/utils.c"
if [[ -r "$source" ]]; then
    for pattern in 'Android Xwayland needs an absolute TMPDIR' \
                   'Android app data can forbid hard links'; do
        count="$(grep -Fc "$pattern" "$source" || :)"
        [[ "$count" == 1 ]] || { echo "S16_PRIVATE_XORG_REAPPLY_REGRESSION=$pattern count:$count";exit 2; }
    done
fi
ALROOT_XWAYLAND_VARIANT=kgsl bash graphics/tools/build-xwayland-private.sh >"$OUT/build-first.log" 2>&1
ALROOT_XWAYLAND_VARIANT=kgsl bash graphics/tools/build-xwayland-private.sh >"$OUT/build-repeat.log" 2>&1
if ! grep -q 'ninja: no work to do' "$OUT/build-repeat.log";then
    echo 'S16_KGSL_REBUILD_IDEMPOTENCE=FAIL'
    tail -20 "$OUT/build-repeat.log"
    exit 2
fi
echo 'S16_KGSL_REBUILD_IDEMPOTENCE=PASS'
python3 graphics/tools/test-s16-xwayland-rootful-lock.py >"$OUT/rootful-live.log" 2>&1
for line in 'S16_ROOTFUL_START=PASS' \
            'S16_TERMUX_TMP_ATOMIC_LOCK=PASS' \
            'S16_LOCK_COLLISION_REJECTED=1' \
            'S16_ROOTFUL_DRI3=1' \
            'S16_ROOTFUL_GLX=1' \
            'S16_ANDROID_HOST_VKBUFFER_GPU_COPY=LOG_CONFIRMED'; do
    if ! grep -Fxq "$line" "$OUT/rootful-live.log";then
        echo "S16_KGSL_FULL_FRAME_INTEGRATION=FAIL_MISSING:$line"
        exit 2
    fi
done
log="$(sed -n 's/^S16_ROOTFUL_XWAYLAND_LOG=//p' "$OUT/rootful-live.log" |head -1)"
[[ -r "$log" ]] || { echo 'S16_KGSL_PRODUCER_LOG=NOT_FOUND';exit 2; }
if ! grep -q 'Xwayland KGSL: full output-pixmap GPU preservation enabled' "$log";then
    echo 'S16_KGSL_FULL_PIXMAP_REUSE_PATH=NOT_EXECUTED'
    exit 2
fi
echo 'S16_KGSL_FULL_PIXMAP_REUSE_PATH=PASS_ACTUAL_XWAYLAND'
echo 'S16_GUEST_VKBUFFER_GPU_COPY=PASS_ACTUAL_ANDROID_HOST_LOG'
echo 'S16_TERMUX_GLOBAL_XWAYLAND=UNMODIFIED'
echo 'S16_ANDROID_SCREEN_TEXT_STABILITY=REQUIRES_VISUAL_A_B'
