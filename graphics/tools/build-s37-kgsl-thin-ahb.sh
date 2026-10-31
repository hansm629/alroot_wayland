#!/data/data/com.termux/files/usr/bin/bash
# S37: narrow-HEIGHT Qualcomm AHB DRI3 BadMatch correction. S36 rootful
# AHB output is unchanged. Never mutates the installed X11/GPU stack or
# relinks the user's running S36 XFCE Xwayland; fail closed on all hashes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
OUT="$ROOT/.source/gpu-stage1/s37-thin-ahb"
S36="$ROOT/.source/gpu-stage1/s36-ahb-output"
SOURCE="$S36/scratch/xwayland-glamor-gbm.s36.c"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0011-kgsl-thin-ahb-fd0.patch"
STAGED="$OUT/scratch/xwayland-glamor-gbm.s37.c"
REPLAY="$OUT/scratch/replay/hw/xwayland/xwayland-glamor-gbm.c"
S37_HASH=193f43e03f0e754c39cfb73e04ac1e0032f8f632e6d06b5d1b675f67c834a594

[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S37_BUILD=BLOCKED_ONLY_S26_ULTRA >&2;exit 2;
}
[[ -s "$PATCH" && -f "$SOURCE" && ! -L "$SOURCE" &&
   "$(sha256sum "$SOURCE"|cut -d' ' -f1)" == c1c7d46d7b4825f40906cbe7562e1f5031fa9ac74c203c2dc879714ba9c228d8 ]] || {
    echo S37_BUILD=BLOCKED_S36_AHB_SOURCE_NOT_PINNED >&2;exit 2;
}

declare -a previous=(
    "$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland|f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701"
    "$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland|cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7"
    "$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland|3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c"
    "$ROOT/.source/gpu-stage1/s30-min-extent/runtime/Xwayland|4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31"
    "$ROOT/.source/gpu-stage1/s31-resize-continuity/runtime/Xwayland|73acf8472faf1e5b91b5f4e998c32ed3322d30fc68e7b0683eef62b6a9c1fe90"
    "$ROOT/.source/gpu-stage1/s32-no-blank-present/runtime/Xwayland|b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455"
    "$S36/runtime/Xwayland|df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5"
)
for item in "${previous[@]}";do
    file="${item%%|*}";expected="${item#*|}"
    [[ -f "$file" && ! -L "$file" && -x "$file" &&
       "$(sha256sum "$file"|cut -d' ' -f1)" == "$expected" ]] || {
        echo "S37_BUILD=BLOCKED_EXISTING_PRIVATE_BINARY_CHANGED:$file" >&2;exit 2;
    }
done

before="$(awstart --status)"
xpid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
xfce="$(sed -nE 's/^xfce: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
[[ "$xpid" =~ ^[0-9]+$ && "$xfce" =~ ^[0-9]+$ ]] || {
    echo S37_BUILD=BLOCKED_USER_XFCE_NOT_READY >&2;exit 2;
}
if [[ -r "/proc/$xpid/exe" &&
      "$(readlink -f "/proc/$xpid/exe")" == "$OUT/runtime/Xwayland" ]];then
    echo S37_BUILD=BLOCKED_WOULD_RELINK_ACTIVE_S37_SERVER >&2;exit 2;
fi

mkdir -p "$(dirname "$REPLAY")" "$OUT/logs" "$OUT/runtime"
cp -p "$SOURCE" "$REPLAY"
(cd "$OUT/scratch/replay" && patch -s --batch -p1 < "$PATCH")
[[ "$(sha256sum "$REPLAY"|cut -d' ' -f1)" == \
    b2a84ec1927f2a7154c1689fa223b531d5d30c395cfa6d0036318cd701a10570 ]] || {
    echo S37_BUILD=FAILED_S36_TO_S37_PATCH_SOURCE_HASH >&2;exit 3;
}
cp -p "$REPLAY" "$STAGED"
echo S37_S36_SOURCE_PATCH_BYTE_REPLAY=PASS

system_before="$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)"
python3 graphics/tools/relink-s37-kgsl-thin-ahb.py
[[ "$(sha256sum "$OUT/runtime/Xwayland"|cut -d' ' -f1)" == "$S37_HASH" &&
   "$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)" == "$system_before" ]] || {
    echo S37_BUILD=FAILED_PRIVATE_HASH_OR_SYSTEM_XWAYLAND_CHANGED >&2;exit 4;
}
for item in "${previous[@]}";do
    file="${item%%|*}";expected="${item#*|}"
    [[ "$(sha256sum "$file"|cut -d' ' -f1)" == "$expected" ]] || {
        echo S37_BUILD=FAILED_PREVIOUS_XWAYLAND_CHANGED >&2;exit 4;
    }
done
after="$(awstart --status)"
grep -Fxq "xwayland: RUNNING $xpid" <<< "$after"
grep -Fxq "xfce: RUNNING $xfce" <<< "$after"
grep -Fq 'x11_desktop: READY' <<< "$after"
sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$OUT/logs/protected-system-x11-and-gpu.log"
echo S37_S26U_THIN_HEIGHT_AHB_DRI3_FIX=PRIVATE_BUILT
echo S37_S16_TO_S36_SYSTEM_X11_AND_GPU_BINARIES=UNCHANGED
echo S37_USER_XFCE_AND_IME=UNCHANGED
echo S37_AWSTART_DEFAULT=NOT_PROMOTED
echo S37_ANDROID_PHYSICAL_VISUAL_ACCEPTANCE=PENDING
