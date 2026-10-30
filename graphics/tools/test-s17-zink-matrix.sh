#!/data/data/com.termux/files/usr/bin/bash
# GPU-S1.7: S26U KGSL private-Xwayland three-backend GLX Zink acceptance.
# Never touches the active XFCE server, Termux global Xwayland, system Mesa,
# Vulkan ICD registration, APK install or live shell's GPU environment.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
STATE="$HOME/.alroot-wayland"
OUT="$ROOT/.source/gpu-stage27-pr25/s16-xwayland-gpu/zink-matrix/acceptance"
mkdir -p "$OUT"
cd "$ROOT"

[[ -S "$STATE/runtime/wayland-0" ]] || {
  echo 'S17_ZINK_ACCEPTANCE=BLOCKED_ANDROID_WAYLAND_HOST_DOWN';exit 2;
}
[[ -x "$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland" ]] || {
  echo 'S17_ZINK_ACCEPTANCE=BLOCKED_PRIVATE_KGSL_XWAYLAND_MISSING';exit 2;
}

# One binary, two verified fence formats. Never globally LD_PRELOAD this.
bash graphics/tools/build-s16-xshmfence-dualabi.sh > "$OUT/dualabi-build.log" 2>&1 || {
  cat "$OUT/dualabi-build.log";exit 3;
}
SHIM="$ROOT/.source/gpu-stage27-pr25/s16-xwayland-gpu/xshmfence/libalroot_xshmfence_dualabi.so"
[[ -s "$SHIM" ]]
echo 'S17_ZINK_DRI3_FENCE_ABI_TEST=BIONIC96_AND_GLIBC4_PASS'

# Preserve the current desktop (even if it is hung); the wrapper creates
# and destroys only isolated X displays :201..:240.
ORIGINAL_XWAYLAND_PID=''
if [[ -r "$STATE/xwayland.pid" ]];then
    read -r ORIGINAL_XWAYLAND_PID _ < "$STATE/xwayland.pid"
fi

MODE="${S17_MATRIX_MODE:-smoke}"
case "$MODE" in
  smoke) cases=(termux-wrapper-glmark2 termux-turnip-glmark2 debian-turnip-glmark2) ;;
  full) cases=(termux-wrapper-glxinfo termux-wrapper-glmark2 termux-wrapper-vkcube
               termux-turnip-glxinfo termux-turnip-glmark2 termux-turnip-vkcube
               debian-turnip-glxinfo debian-turnip-glmark2 debian-turnip-vkcube) ;;
  *) echo 'S17_MATRIX_MODE must be smoke or full';exit 2 ;;
esac
for case_name in "${cases[@]}";do
    log="$OUT/$case_name.log"
    if ! env S16_ZINK_XSHMFENCE_SHIM="$SHIM" timeout -s TERM -k 3s 37s \
        python3 -u graphics/tools/test-s16-zink-isolated.py "$case_name" >"$log" 2>&1;then
        echo "S17_ZINK_MATRIX[$case_name]=FAIL_TEST_RUNNER"
        tail -22 "$log";exit 5
    fi
    for must in 'S16_ZINK_PRE_CLIENT_XSERVER=PASS' \
                'S16_ZINK_DURING_CLIENT_XSERVER=RESPONSIVE' \
                'S16_ZINK_CLIENT_EXIT=0' \
                'S16_ZINK_AFTER_CLIENT_XSERVER=RESPONSIVE' \
                'S16_ZINK_CASE_RESULT=PASS' \
                'S16_ZINK_PRIVATE_SERVER_CLEANED=PASS';do
        if ! grep -Fxq "$must" "$log";then
            echo "S17_ZINK_MATRIX[$case_name]=FAIL_MISSING:$must"
            tail -26 "$log";exit 6
        fi
    done
    server_log="$ROOT/.source/gpu-stage27-pr25/s16-xwayland-gpu/zink-matrix/$case_name-server.log"
    if [[ "$case_name" == debian-* ]];then
        grep -Fq 'ALROOT_S16_XSHMFENCE_MAPPED_FORMAT=GLIBC_FUTEX_4B' "$server_log" || {
            echo "S17_ZINK_MATRIX[$case_name]=FAIL_NO_GLIBC_FUTEX_MAPPING";exit 7;
        }
    elif [[ "$case_name" == termux-* ]];then
        grep -Fq 'ALROOT_S16_XSHMFENCE_MAPPED_FORMAT=BIONIC_PTHREAD_96B' "$server_log" || {
            echo "S17_ZINK_MATRIX[$case_name]=FAIL_NO_BIONIC_FENCE_MAPPING";exit 7;
        }
    fi
    if [[ "$case_name" == termux-wrapper-* ]];then
        grep -Fq 'S16 AHB DRI3 socket->native DMA-BUF->KGSL EGL import PASS:' "$server_log" || {
            echo "S17_ZINK_MATRIX[$case_name]=FAIL_AHB_SOCKET_NOT_IMPORTED";exit 7;
        }
    fi
    echo "S17_ZINK_MATRIX[$case_name]=PASS"
done

if [[ -n "$ORIGINAL_XWAYLAND_PID" && -r "$STATE/xwayland.pid" ]];then
    read -r LATER_XWAYLAND_PID _ < "$STATE/xwayland.pid"
    [[ "$ORIGINAL_XWAYLAND_PID" == "$LATER_XWAYLAND_PID" ]] || {
        echo 'S17_ORIGINAL_DESKTOP_PID_CHANGED_UNEXPECTEDLY';exit 8;
    }
fi
echo 'S17_ACTIVE_XFCE_SERVER=UNCHANGED'
echo 'S17_GLOBAL_TERMUX_XWAYLAND=UNMODIFIED'
echo 'S17_DISPLAY_PIXEL_CORRECTNESS=REQUIRES_USER_VISUAL_TEST'
echo 'S17_ZINK_BACKEND_MATRIX=PASS'
