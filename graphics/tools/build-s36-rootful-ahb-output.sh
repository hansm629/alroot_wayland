#!/data/data/com.termux/files/usr/bin/bash
# S36 private Adreno KGSL output = direct GLAMOR AHB FBO -> native host VkImage.
# Replays the opt-in patch into an ISOLATED overlay and relinks a separate
# Xwayland binary. Never recompiles/replaces any current XFCE, system
# Termux-X11, existing S16-S32 private binaries, Android APK or driver.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
OUT="$ROOT/.source/gpu-stage1/s36-ahb-output"
SOURCE="$ROOT/.source/gpu-stage1/xwayland-kgsl/source/hw/xwayland/xwayland-glamor-gbm.c"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0010-kgsl-rootful-ahb-direct-output.patch"
OVERLAY="$OUT/scratch/overlay"
STAGED="$OUT/scratch/xwayland-glamor-gbm.s36.c"
EXPECTED_SOURCE=c1c7d46d7b4825f40906cbe7562e1f5031fa9ac74c203c2dc879714ba9c228d8
EXPECTED_BINARY=df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5

[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S36_BUILD=BLOCKED_ONLY_VALIDATED_S26_ULTRA >&2;exit 2;
}
[[ -s "$SOURCE" && -s "$PATCH" &&
    "$(sha256sum "$SOURCE"|cut -d' ' -f1)" == 1f1bd939d026eaa5e1e695488a63c75dcde5af382a4ff3c9f6d1d2642f0aa487 ]] || {
    echo S36_BUILD=BLOCKED_S32_SOURCE_NOT_PINNED >&2;exit 2;
}

declare -a previous=(
    "$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland|f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701"
    "$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland|cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7"
    "$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland|3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c"
    "$ROOT/.source/gpu-stage1/s30-min-extent/runtime/Xwayland|4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31"
    "$ROOT/.source/gpu-stage1/s31-resize-continuity/runtime/Xwayland|73acf8472faf1e5b91b5f4e998c32ed3322d30fc68e7b0683eef62b6a9c1fe90"
    "$ROOT/.source/gpu-stage1/s32-no-blank-present/runtime/Xwayland|b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455"
)
for approved in "${previous[@]}";do
    file="${approved%%|*}"
    digest="${approved#*|}"
    [[ -x "$file" && ! -L "$file" &&
        "$(sha256sum "$file"|cut -d' ' -f1)" == "$digest" ]] || {
        echo "S36_BUILD=BLOCKED_PREVIOUS_PRIVATE_SERVER_CHANGED: $file" >&2;exit 2;
    }
done

live="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$live")"
xfce="$(sed -nE 's/^xfce: RUNNING ([0-9]+)$/\1/p' <<< "$live")"
[[ "$pid" =~ ^[0-9]+$ && "$xfce" =~ ^[0-9]+$ ]] || {
    echo S36_BUILD=BLOCKED_CURRENT_XFCE_NOT_HEALTHY >&2;exit 2;
}
if [[ -r "/proc/$pid/exe" &&
      "$(readlink -f "/proc/$pid/exe")" == "$OUT/runtime/Xwayland" ]];then
    echo S36_BUILD=BLOCKED_CANNOT_RELINK_RUNNING_S36_SERVER >&2;exit 2;
fi

mkdir -p "$OVERLAY/hw/xwayland" "$OUT/logs" "$OUT/runtime"
cp -p "$SOURCE" "$OVERLAY/hw/xwayland/xwayland-glamor-gbm.c"
(cd "$OVERLAY" && patch --silent --batch -p1 < "$PATCH")
[[ "$(sha256sum "$OVERLAY/hw/xwayland/xwayland-glamor-gbm.c"|cut -d' ' -f1)" == "$EXPECTED_SOURCE" ]] || {
    echo S36_BUILD=FAILED_S36_SOURCE_REPLAY_MISMATCH >&2;exit 3;
}
cp -p "$OVERLAY/hw/xwayland/xwayland-glamor-gbm.c" "$STAGED"
echo S36_PRIVATE_SOURCE_PATCH_REPLAY=PASS

# Fail closed without S1.3's pixel-correct full-HD external GPU memory.
ALROOT_S13_RESOLUTION=1920x1080 \
    ALROOT_S13_TEXTURE_STRIDE_WIDTH=1 \
    ALROOT_S13_TEXTURE_HEIGHT=1080 \
    ALROOT_S13_SAMPLE_HEIGHT=1080 \
    bash graphics/tools/test-s13-kgsl-ahb-capabilities.sh \
    > "$OUT/logs/1920x1080-ahb-gpu-pixel-preflight.log"
grep -Fxq 'S13_KGSL_AHB_GL_FD0_CROSS_DRIVER_PIXELS=PASS' \
    "$OUT/logs/1920x1080-ahb-gpu-pixel-preflight.log"
echo S36_FULLHD_FREEDRENO_AHB_GPU_PIXEL_IDENTITY=PASS

system_before="$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)"
python3 graphics/tools/relink-s36-rootful-ahb-xwayland.py
[[ "$(sha256sum "$OUT/runtime/Xwayland"|cut -d' ' -f1)" == "$EXPECTED_BINARY" &&
    "$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)" == "$system_before" ]] || {
    echo S36_BUILD=FAILED_PRIVATE_BINARY_SIGNATURE_OR_SYSTEM_CHANGED >&2;exit 4;
}
for approved in "${previous[@]}";do
    file="${approved%%|*}";digest="${approved#*|}"
    [[ "$(sha256sum "$file"|cut -d' ' -f1)" == "$digest" ]] || exit 4
done
after="$(awstart --status)"
grep -Fxq "xwayland: RUNNING $pid" <<< "$after"
grep -Fxq "xfce: RUNNING $xfce" <<< "$after"
grep -Fq 'x11_desktop: READY' <<< "$after"
sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$OUT/logs/protected-global-gpu.sha256.log"

echo S36_DIRECT_ROOTFUL_AHB_BACKING_XWAYLAND=PRIVATE_BUILT
echo S36_PREVIOUS_S16_S28_S29_S30_S31_S32_SYSTEM_DRIVERS=UNCHANGED
echo S36_USER_XFCE=UNCHANGED
echo S36_DEFAULT_AWSTART=NOT_PROMOTED
echo S36_ANDROID_PHYSICAL_VISUAL_ACCEPTANCE=USER_REQUIRED
