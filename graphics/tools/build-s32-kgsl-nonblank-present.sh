#!/data/data/com.termux/files/usr/bin/bash
# S32 Qualcomm KGSL opt-in: keep actual content through stale XCB Presents
# clearing, with S30's proven tiny-AHB importer. Never touch system drivers
# or the running user's XFCE. The previous S30 binary remains available.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
SOURCE="$ROOT/.source/gpu-stage1/xwayland-kgsl/source/hw/xwayland"
BUILD="$ROOT/.source/gpu-stage1/xwayland-kgsl/build"
OUT="$ROOT/.source/gpu-stage1/s32-no-blank-present"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0009-kgsl-nonblank-present-continuity.patch"
S32_BINARY="$OUT/runtime/Xwayland"
S32_SHA=b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455

[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S32_BUILD=BLOCKED_ONLY_TESTED_S26_ULTRA >&2;exit 2;
}
[[ -s "$PATCH" && -f "$BUILD/build.ninja" ]] || {
    echo S32_BUILD=BLOCKED_REQUIRES_PREPARED_PRIVATE_S31_KGSL_SOURCE >&2;exit 2;
}
declare -a older=(
    "$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland|f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701"
    "$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland|cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7"
    "$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland|3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c"
    "$ROOT/.source/gpu-stage1/s30-min-extent/runtime/Xwayland|4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31"
    "$ROOT/.source/gpu-stage1/s31-resize-continuity/runtime/Xwayland|73acf8472faf1e5b91b5f4e998c32ed3322d30fc68e7b0683eef62b6a9c1fe90"
)
for item in "${older[@]}";do
    file="${item%%|*}"
    hash="${item#*|}"
    [[ -f "$file" && -x "$file" && ! -L "$file" &&
       "$(sha256sum "$file"|cut -d' ' -f1)" == "$hash" ]] || {
        echo "S32_BUILD=BLOCKED_OLDER_PRIVATE_BINARY_CHANGED: $file" >&2;exit 2;
    }
done

declare -a names=(xwayland-window.c xwayland-window.h xwayland-present.c)
declare -a before_sha=(
    9d422b27e55d9848f3e8341402a9bb966d7a448d2ce5e38b8cf8e61ba466ac77
    2e0751cd815130255f72fb0fc709370ef38b6ac8f6db477f2ac131bfda844809
    49195bcf51e8a9fff9f0ef17e85e9e459518b982bd1ee9ea1984162da20a3575
)
declare -a after_sha=(
    85b3d7cda893a679ca92fc5958100e837d6ab6f6870049792076a03f6d3ed9ea
    378bdbefb30e96515079e6e29251dc769ce2b03dcac2a0353ea859d7dac1e7fd
    fae115e07784e39685ead8da8152ffc5af7c51b26e2fa1f4ddc2d4fcdcb3c73b
)
baseline=1
patched=1
for i in "${!names[@]}";do
    file="$SOURCE/${names[$i]}"
    [[ -f "$file" && ! -L "$file" ]] || {
        echo S32_BUILD=BLOCKED_PRIVATE_SOURCE_MISSING_OR_SYMLINK >&2;exit 2;
    }
    sha="$(sha256sum "$file"|cut -d' ' -f1)"
    [[ "$sha" == "${before_sha[$i]}" ]] || baseline=0
    [[ "$sha" == "${after_sha[$i]}" ]] || patched=0
done

mkdir -p "$OUT/logs" "$OUT/runtime"
before_status="$(awstart --status)"
before_pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$before_status")"
if [[ "$before_pid" =~ ^[0-9]+$ && -r "/proc/$before_pid/exe" ]] &&
   [[ "$(readlink -f "/proc/$before_pid/exe")" == "$S32_BINARY" ]];then
    echo S32_BUILD=BLOCKED_RELINKING_ACTIVE_XFCE_XWAYLAND >&2;exit 2
fi

if ((patched));then
    echo S32_PRIVATE_SOURCE=ALREADY_PATCHED
elif ((baseline));then
    (cd "$ROOT/.source/gpu-stage1/xwayland-kgsl/source" &&
        patch --batch --silent --dry-run -p1 < "$PATCH") || {
        echo S32_BUILD=BLOCKED_PATCH_DOES_NOT_APPLY_TO_S31 >&2;exit 3;
    }
    (cd "$ROOT/.source/gpu-stage1/xwayland-kgsl/source" &&
        patch --batch --silent -p1 < "$PATCH")
    echo S32_PRIVATE_SOURCE=PATCHED_S31_OVERLAY
else
    echo S32_BUILD=BLOCKED_UNKNOWN_OR_PARTIAL_PRIVATE_SOURCE >&2;exit 3
fi

for i in "${!names[@]}";do
    [[ "$(sha256sum "$SOURCE/${names[$i]}"|cut -d' ' -f1)" == "${after_sha[$i]}" ]] || {
        echo S32_BUILD=FAILED_PRIVATE_SOURCE_HASH >&2;exit 4;
    }
done
system_before="$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)"
python3 graphics/tools/relink-s32-private-xwayland.py || {
    tail -40 "$OUT/logs/relink.log" >&2;exit 4;
}
[[ "$(sha256sum "$S32_BINARY"|cut -d' ' -f1)" == "$S32_SHA" &&
   "$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)" == "$system_before" ]] || {
    echo S32_BUILD=FAILED_BINARY_SIGNATURE_OR_SYSTEM_XWAYLAND_CHANGED >&2;exit 5;
}
for item in "${older[@]}";do
    file="${item%%|*}";sha="${item#*|}"
    [[ "$(sha256sum "$file"|cut -d' ' -f1)" == "$sha" ]] || {
        echo S32_BUILD=FAILED_OLDER_XWAYLAND_CHANGED >&2;exit 5;
    }
done
after_pid="$(awstart --status|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
[[ "$before_pid" == "$after_pid" ]] || {
    echo S32_BUILD=FAILED_USER_XFCE_RESTARTED_UNEXPECTEDLY >&2;exit 5;
}
sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$OUT/logs/installed-driver-integrity.log"
echo S32_NONBLANK_STALE_PRESENT_CONTINUITY=PRIVATE_BUILT
echo S32_ALL_OLDER_PRIVATE_XWAYLAND_AND_SYSTEM_DRIVERS=UNCHANGED
echo S32_USER_XFCE_PROCESS=UNCHANGED
echo S32_NOT_PROMOTED_AS_AWSTART_DEFAULT=PASS
echo S32_PHYSICAL_ANDROID_VISUAL=USER_ACCEPTANCE_PENDING
