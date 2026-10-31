#!/data/data/com.termux/files/usr/bin/bash
# Shared, side-effect-free validation for private KGSL startup and preflight.
# This file is sourced by the session launcher and executed by stage starters.
alroot_validate_kgsl_profile() {
    local xwayland_binary="$1" glamor="$2" project="$3"
    local required="$project/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland"
    local s28_candidate="$project/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland"
    local s29_candidate="$project/.source/gpu-stage1/s29-native-resize/runtime/Xwayland"
    local s30_candidate="$project/.source/gpu-stage1/s30-min-extent/runtime/Xwayland"
    local s31_candidate="$project/.source/gpu-stage1/s31-resize-continuity/runtime/Xwayland"
    local s32_candidate="$project/.source/gpu-stage1/s32-no-blank-present/runtime/Xwayland"
    local s36_candidate="$project/.source/gpu-stage1/s36-ahb-output/runtime/Xwayland"
    local s37_candidate="$project/.source/gpu-stage1/s37-thin-ahb/runtime/Xwayland"
    local s38_candidate="$project/.source/gpu-stage1/s38-frame-continuity/runtime/Xwayland"
    local s39_candidate="$project/.source/gpu-stage1/s39-live-resize/runtime/Xwayland"
    local dualabi="$project/.source/gpu-stage27-pr25/s16-xwayland-gpu/xshmfence/libalroot_xshmfence_dualabi.so"
    if [[ ( "$xwayland_binary" != "$required" && "$xwayland_binary" != "$s28_candidate" && "$xwayland_binary" != "$s29_candidate" && "$xwayland_binary" != "$s30_candidate" && "$xwayland_binary" != "$s31_candidate" && "$xwayland_binary" != "$s32_candidate" && "$xwayland_binary" != "$s36_candidate" && "$xwayland_binary" != "$s37_candidate" && "$xwayland_binary" != "$s38_candidate" && "$xwayland_binary" != "$s39_candidate" ) ||
          ( "$xwayland_binary" == "$s28_candidate" && "${S28_KGSL_RESIZE_PRESERVE:-0}" != 1 ) ||
          ( "$xwayland_binary" != "$s28_candidate" && "${S28_KGSL_RESIZE_PRESERVE:-0}" == 1 ) ||
          ( "$xwayland_binary" == "$s29_candidate" && "${S29_KGSL_NATIVE_RESIZE:-0}" != 1 ) ||
          ( "$xwayland_binary" != "$s29_candidate" && "${S29_KGSL_NATIVE_RESIZE:-0}" == 1 ) ||
          ( "$xwayland_binary" == "$s30_candidate" &&
            ( "${S30_KGSL_STABLE_FRAME:-0}" != 1 || "${S30_KGSL_TINY_AHB_FD0:-0}" != 1 ) ) ||
          ( "$xwayland_binary" != "$s30_candidate" &&
            ( "${S30_KGSL_STABLE_FRAME:-0}" == 1 ||
              ( "$xwayland_binary" != "$s31_candidate" && "$xwayland_binary" != "$s32_candidate" && "$xwayland_binary" != "$s36_candidate" && "$xwayland_binary" != "$s37_candidate" && "$xwayland_binary" != "$s38_candidate" && "$xwayland_binary" != "$s39_candidate" && "${S30_KGSL_TINY_AHB_FD0:-0}" == 1 ) ) ) ||
          ( "$xwayland_binary" == "$s31_candidate" &&
            ( "${S31_KGSL_RESIZE_CONTENT_POLICY:-0}" != 3 || "${S30_KGSL_TINY_AHB_FD0:-0}" != 1 ) ) ||
          ( "$xwayland_binary" != "$s31_candidate" && "${S31_KGSL_RESIZE_CONTENT_POLICY:-0}" != 0 ) ||
          ( "$xwayland_binary" == "$s32_candidate" &&
            ( "${S32_KGSL_NO_BLANK_PRESENT:-0}" != 1 || "${S30_KGSL_TINY_AHB_FD0:-0}" != 1 ) ) ||
          ( "$xwayland_binary" != "$s32_candidate" && "$xwayland_binary" != "$s36_candidate" && "$xwayland_binary" != "$s37_candidate" && "$xwayland_binary" != "$s38_candidate" && "$xwayland_binary" != "$s39_candidate" && "${S32_KGSL_NO_BLANK_PRESENT:-0}" != 0 ) ||
          ( ( "$xwayland_binary" == "$s36_candidate" || "$xwayland_binary" == "$s37_candidate" || "$xwayland_binary" == "$s38_candidate" || "$xwayland_binary" == "$s39_candidate" ) &&
            ( "${S36_KGSL_ROOT_AHB_OUTPUT:-0}" != 1 || "${S32_KGSL_NO_BLANK_PRESENT:-0}" != 1 || "${S30_KGSL_TINY_AHB_FD0:-0}" != 1 ) ) ||
          ( "$xwayland_binary" != "$s36_candidate" && "$xwayland_binary" != "$s37_candidate" && "$xwayland_binary" != "$s38_candidate" && "$xwayland_binary" != "$s39_candidate" && "${S36_KGSL_ROOT_AHB_OUTPUT:-0}" != 0 ) ||
          ( ( "$xwayland_binary" == "$s37_candidate" || "$xwayland_binary" == "$s38_candidate" || "$xwayland_binary" == "$s39_candidate" ) && "${S37_KGSL_THIN_AHB_FD0:-0}" != 1 ) ||
          ( "$xwayland_binary" != "$s37_candidate" && "$xwayland_binary" != "$s38_candidate" && "$xwayland_binary" != "$s39_candidate" && "${S37_KGSL_THIN_AHB_FD0:-0}" != 0 ) ||
          ( ( "$xwayland_binary" == "$s38_candidate" || "$xwayland_binary" == "$s39_candidate" ) && "${S38_KGSL_FRAME_LIFETIME:-0}" != 1 ) ||
          ( "$xwayland_binary" != "$s38_candidate" && "$xwayland_binary" != "$s39_candidate" && "${S38_KGSL_FRAME_LIFETIME:-0}" != 0 ) ||
          ( "$xwayland_binary" == "$s39_candidate" &&
            ( "${S39_KGSL_RESIZE_ORIGIN:-0}" != 1 || "${S39_KGSL_KEEP_UNRELATED_ROOT_POOL:-0}" != 1 ) ) ||
          ( "$xwayland_binary" != "$s39_candidate" &&
            ( "${S39_KGSL_RESIZE_ORIGIN:-0}" != 0 || "${S39_KGSL_KEEP_UNRELATED_ROOT_POOL:-0}" != 0 ) ) ||
          "$glamor" != es || ! -s "$dualabi" ]];then
        echo '[Alroot GPU] Cannot enable dual-ABI fence outside private KGSL GLAMOR' >&2
        return 1
    fi
    [[ -x "$xwayland_binary" ]] || {
        echo '[Alroot GPU] Private Xwayland executable is missing' >&2
        return 1
    }
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    set -euo pipefail
    [[ $# == 3 ]] || exit 2
    alroot_validate_kgsl_profile "$@"
fi
