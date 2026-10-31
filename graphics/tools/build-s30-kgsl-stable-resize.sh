#!/data/data/com.termux/files/usr/bin/bash
# S30 Adreno 840-only, isolated KGSL tiny-AHB + Present-stable frame server.
# No system drivers, original private Xwayland, Android APK, or XFCE restart.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
SOURCE="$ROOT/.source/gpu-stage1/xwayland-kgsl/source"
BUILD="$ROOT/.source/gpu-stage1/xwayland-kgsl/build"
OUT="$ROOT/.source/gpu-stage1/s30-min-extent"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0007-kgsl-tiny-ahb-stable-xcb-present.patch"
BASE16="$BUILD/hw/xwayland/Xwayland"
BASE28="$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland"
BASE29="$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland"
S30_BIN="$OUT/runtime/Xwayland"
S30_HASH=4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31

[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S30_BUILD=BLOCKED_NOT_VALIDATED_S26_ULTRA >&2;exit 2;
}
[[ -s "$PATCH" && -f "$BUILD/build.ninja" && -x "$BASE16" &&
   -x "$BASE28" && -x "$BASE29" ]] || {
    echo S30_BUILD=BLOCKED_MISSING_PINNED_S29_KGSL_BUILD >&2;exit 2;
}

declare -a trusted_paths=("$BASE16" "$BASE28" "$BASE29")
declare -a trusted_hashes=(
    f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701
    cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7
    3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c
)
for i in "${!trusted_paths[@]}";do
    [[ "$(sha256sum "${trusted_paths[$i]}"|cut -d' ' -f1)" == "${trusted_hashes[$i]}" ]] || {
        echo S30_BUILD=BLOCKED_PREVIOUS_PRIVATE_XWAYLAND_CHANGED >&2;exit 2;
    }
done
declare -a names=(
    xwayland-glamor-gbm.c xwayland-glamor-gbm.h
    xwayland-window.c xwayland-window.h xwayland-present.c
)
declare -a base_hashes=(
    24cb16374c80adc6ac865ad7a87e691bd1886f9cad3627dcb41dcad848e89d4f
    b1abd72ff34f6fb4e63ea906425398078561f0e2b79269c5611b97f82fa22380
    d1a3ce07ef84d07b2f06206151a9001d09a69e1adf403d4ab46cd20a22fe31cd
    9a821a1d45e69ceabe00112f3953c5d14ac89014b6c1483ee20ea94606507184
    2b240a1a307f8e5a8530fd6efcc36fa5b037cd3d61126f2f02da6e6ec42d8034
)
declare -a new_hashes=(
    1f1bd939d026eaa5e1e695488a63c75dcde5af382a4ff3c9f6d1d2642f0aa487
    09bee5fafc671cfcd7b23f6989a6fc2698d5e00ee604151cf943227917afd949
    7d0e60471652d6fc7a4076f9d18d182d87772d292c541c63fb06f18c23d4e854
    4328577e7e9dc0295c9261c65cc0ef5a2318f56c70a95f1bd2152d4b24627994
    8226eec16be20a1e1dee199cdaa2559e89649f0958e090eabf773171203f0395
)

base=1
patched=1
for i in "${!names[@]}";do
    f="$SOURCE/hw/xwayland/${names[$i]}"
    [[ -f "$f" && ! -L "$f" ]] || {
        echo S30_BUILD=BLOCKED_MISSING_OR_SYMLINKED_PRIVATE_SOURCE >&2;exit 2;
    }
    checksum="$(sha256sum "$f"|cut -d' ' -f1)"
    [[ "$checksum" == "${base_hashes[$i]}" ]] || base=0
    [[ "$checksum" == "${new_hashes[$i]}" ]] || patched=0
done

mkdir -p "$OUT/runtime" "$OUT/logs"
installed_before="$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)"
previous_pid="$(awstart --status|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
if [[ -n "$previous_pid" && -r "/proc/$previous_pid/exe" &&
      "$(readlink -f "/proc/$previous_pid/exe")" == "$S30_BIN" ]];then
    echo S30_BUILD=BLOCKED_RELINK_OF_CURRENTLY_RUNNING_S30_SERVER >&2;exit 3
fi

if ((patched));then
    echo S30_SOURCE=ALREADY_PATCHED_PRIVATE_ONLY
elif ((base));then
    (cd "$SOURCE" && patch --batch --silent --dry-run -p1 < "$PATCH") || {
        echo S30_BUILD=BLOCKED_PATCH_REPLAY_FAILED >&2;exit 3;
    }
    (cd "$SOURCE" && patch --batch --silent -p1 < "$PATCH")
    echo S30_SOURCE=PATCHED_PRIVATE_ONLY
else
    echo S30_BUILD=BLOCKED_UNEXPECTED_PARTIAL_OR_DIFFERENT_SOURCE >&2;exit 3
fi
for i in "${!names[@]}";do
    f="$SOURCE/hw/xwayland/${names[$i]}"
    [[ "$(sha256sum "$f"|cut -d' ' -f1)" == "${new_hashes[$i]}" ]] || {
        echo S30_BUILD=FAILED_SOURCE_SIGNATURE_CHANGED >&2;exit 4;
    }
done

python3 graphics/tools/relink-s30-private-xwayland.py || {
    tail -35 "$OUT/logs/relink.log" >&2;exit 4;
}
[[ "$(sha256sum "$S30_BIN"|cut -d' ' -f1)" == "$S30_HASH" &&
   "$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)" == "$installed_before" ]] || {
    echo S30_BUILD=FAILED_S30_BINARY_OR_SYSTEM_CHANGED >&2;exit 5;
}
for i in "${!trusted_paths[@]}";do
    [[ "$(sha256sum "${trusted_paths[$i]}"|cut -d' ' -f1)" == "${trusted_hashes[$i]}" ]] || {
        echo S30_BUILD=FAILED_OLDER_PRIVATE_SERVER_CHANGED >&2;exit 5;
    }
done
new_pid="$(awstart --status|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
[[ "$previous_pid" == "$new_pid" ]] || {
    echo S30_BUILD=FAILED_USER_XFCE_PID_CHANGED >&2;exit 5;
}
sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$OUT/logs/protected-termux-gpu.sha256.log"
echo S30_NATIVE_KGSL_TINY_AHB_AND_REAL_PRESENT_STABLE_FRAME=PRIVATE_BUILT
echo S30_PREVIOUS_S16_S28_S29_XWAYLAND_AND_SYSTEM_DRIVERS=UNCHANGED
echo S30_USER_XFCE=UNCHANGED
echo S30_DEFAULT_AWSTART=NOT_PROMOTED
echo S30_PHYSICAL_ANDROID_RESIZE=USER_VISUAL_ACCEPTANCE_PENDING
