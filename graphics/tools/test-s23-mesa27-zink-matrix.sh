#!/data/data/com.termux/files/usr/bin/bash
# Mesa 27 (PR #25, pinned 6f90e92) + wrapper / Turnip Xwayland regression.
# Every case runs on its OWN short-lived private Xwayland DISPLAY, not the
# running XFCE or the system Termux-X11. No APK install or Mesa replacement.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
BASE="$ROOT/.source/gpu-stage27-pr25"
NATIVE="$BASE/mesa-private/glx-runtime/lib"
NATIVE_ICD="$BASE/s15-turnip/private-runtime/turnip_alroot_icd.private.json"
GLIBC="$BASE/mesa-glibc27/private-runtime"
SHIM="$BASE/s16-xwayland-gpu/xshmfence/libalroot_xshmfence_dualabi.so"
OUTPUT="$BASE/s16-xwayland-gpu/zink-matrix/acceptance-mesa27"
PIN=6f90e92dfdacc4759d8e9ccb328805d0f0b4439b
mkdir -p "$OUTPUT"

[[ "$(git -C "$BASE/source" rev-parse HEAD)" == "$PIN" ]] || {
    echo S23_MESA27_SOURCE=FAIL_UNEXPECTED_PIN; exit 2;
}
[[ -s "$NATIVE/libGLX_mesa.so" && -s "$NATIVE/libgallium-27.0.0-devel.so" &&
   -s "$NATIVE_ICD" && -s "$SHIM" ]] || {
    echo S23_NATIVE_RUNTIME=BLOCKED_MISSING_MESA27_BUILD; exit 2;
}
[[ -s "$GLIBC/lib/libGLX_mesa.so" && -s "$GLIBC/lib/libgallium-27.0.0-devel.so" &&
   -s "$GLIBC/lib/libvulkan_freedreno.so" && -s "$GLIBC/turnip_icd.private.json" ]] || {
    echo S23_GLIBC_RUNTIME=BLOCKED_MISSING_MESA27_BUILD; exit 2;
}
readelf -d "$NATIVE/libGLX_mesa.so" | grep -q 'libgallium-27.0.0-devel.so'
readelf -d "$GLIBC/lib/libGLX_mesa.so" | grep -q 'libgallium-27.0.0-devel.so'
echo S23_MESA27_GALLIUM_AND_GLX_ABI=PINNED_6F90E92
# A KGSL-only Mesa configure can omit the DRM WSI implementation while
# keeping the X11 DRI3 image type, aborting at get_blit_type(). The Debian
# glibc profile must explicitly retain libdrm AND KGSL in this build.
if ! readelf -d "$GLIBC/lib/libvulkan_freedreno.so" | grep -q 'Shared library: \[libdrm.so'; then
    echo S23_GLIBC_VULKAN_DRM_WSI=FAIL_MISSING_LIBDRM
    exit 2
fi
echo S23_GLIBC_VULKAN_DRM_WSI=LINKED_LIBDRM_WITH_KGSL

active_before="$(awstart --status | sed -n 's/^xwayland: RUNNING \([0-9]*\).*/\1/p')"
[[ "$active_before" =~ ^[0-9]+$ ]] || { echo S23_DESKTOP=BLOCKED_NO_ACTIVE_XWAYLAND;exit 2; }

run_one() {
    local name="$1" kind="$2" log="$OUTPUT/$1.log" server_log client_log
    server_log="$BASE/s16-xwayland-gpu/zink-matrix/$name-server.log"
    client_log="$BASE/s16-xwayland-gpu/zink-matrix/$name-client.log"
    local -a env_args=(S16_ZINK_XSHMFENCE_SHIM="$SHIM")
    if [[ "$name" == termux-* ]]; then
        env_args+=(LIBGL_DRIVERS_PATH="$NATIVE/dri" LD_LIBRARY_PATH="$NATIVE${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}")
        if [[ "$name" == termux-turnip-* ]]; then
            env_args+=(S16_ZINK_TEST_TURNIP_ICD="$NATIVE_ICD")
        fi
    else
        env_args+=(S16_ZINK_TEST_GLIBC27_RUNTIME="$GLIBC")
    fi
    local rc=0
    timeout -s TERM -k 3s 42s env "${env_args[@]}" python3 -u \
        graphics/tools/test-s16-zink-isolated.py "$name" >"$log" 2>&1 || rc=$?
    if ((rc)) || ! grep -qx S16_ZINK_CASE_RESULT=PASS "$log" ||
         ! grep -qx S16_ZINK_DURING_CLIENT_XSERVER=RESPONSIVE "$log" ||
         ! grep -qx S16_ZINK_AFTER_CLIENT_XSERVER=RESPONSIVE "$log" ||
         ! grep -qx S16_ZINK_PRIVATE_SERVER_CLEANED=PASS "$log"; then
        echo "S23[$name]=FAIL_EXECUTION_OR_XSERVER"
        tail -18 "$log"
        return 3
    fi
    if [[ "$kind" == gl ]]; then
        grep -Fq 'Mesa 27.0.0-devel (git-6f90e92dfd)' "$client_log" || {
            echo "S23[$name]=FAIL_DID_NOT_LOAD_MESA27";return 4;
        }
        if [[ "$name" == termux-wrapper-* ]]; then
            grep -Fq QUALCOMM_PROPRIETARY "$client_log" || return 5
        else
            grep -Fq MESA_TURNIP "$client_log" || return 5
        fi
    fi
    if [[ "$name" == termux-wrapper-* ]]; then
        grep -Fq 'S16 AHB DRI3 socket->native DMA-BUF->KGSL EGL import PASS:' "$server_log" || {
            echo "S23[$name]=FAIL_AHB_DRI3_IMPORT"; return 6;
        }
    elif [[ "$name" == debian-* ]]; then
        grep -Fq 'ALROOT_S16_XSHMFENCE_MAPPED_FORMAT=GLIBC_FUTEX_4B' "$server_log" || {
            echo "S23[$name]=FAIL_GLIBC_DRI3_FENCE"; return 7;
        }
    fi
    echo "S23[$name]=PASS"
}

MODE="${S23_MATRIX_MODE:-smoke}"
case "$MODE" in
  smoke)
    names=(termux-wrapper-glxinfo termux-wrapper-glmark2
           termux-turnip-glxinfo termux-turnip-glmark2
           debian-turnip-glxinfo debian-turnip-glmark2)
    ;;
  full)
    names=(termux-wrapper-glxinfo termux-wrapper-glmark2 termux-wrapper-vkcube
           termux-turnip-glxinfo termux-turnip-glmark2 termux-turnip-vkcube
           debian-turnip-glxinfo debian-turnip-glmark2 debian-turnip-vkcube)
    ;;
  *) echo S23_MATRIX_MODE=INVALID_EXPECTED_SMOKE_OR_FULL; exit 2 ;;
esac
for name in "${names[@]}"; do
    kind=gl
    [[ "$name" != *-vkcube ]] || kind=vulkan
    run_one "$name" "$kind"
done
active_after="$(awstart --status | sed -n 's/^xwayland: RUNNING \([0-9]*\).*/\1/p')"
[[ "$active_before" == "$active_after" ]] || {
    echo S23_LIVE_XFCE=FAIL_SESSION_PID_CHANGED;exit 8;
}
sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" > "$OUTPUT/protected-hashes.log" || {
    echo S23_SYSTEM_TERMUX_GRAPHICS=FAIL_MODIFIED;exit 9;
}
echo S23_LIVE_XFCE_SESSION=UNCHANGED
echo S23_SYSTEM_TERMUX_GRAPHICS=UNCHANGED
echo S23_ANDROID_VISIBLE_PIXEL_CORRECTNESS=USER_CHECK_REQUIRED
echo S23_AWSTART_DEFAULT_PROMOTION=NOT_AUTOMATIC
echo S23_MESA27_ZINK_MATRIX=PASS
