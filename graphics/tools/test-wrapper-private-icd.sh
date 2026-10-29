#!/data/data/com.termux/files/usr/bin/bash
# Verify the private wrapper WSI and real GPU without installing this ICD.
# Uses no X11 window and never changes Termux-X11 :0 or Alroot XFCE :90.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
ICD="$ROOT/.source/gpu-stage1/wrapper-private-runtime/wrapper_icd.private.json"
if [[ ! -s "$ICD" ]]; then
    echo 'Private ICD missing; build it with:' >&2
    echo ' ALROOT_BUILD_WRAPPER_FULL=1 ALROOT_VERIFY_WRAPPER=1 bash graphics/tools/build-wrapper-wayland-private.sh' >&2
    exit 2
fi
test "$(python3 - "$ICD" <<'PY'
import json,pathlib,sys
manifest=json.loads(pathlib.Path(sys.argv[1]).read_text())
lib=pathlib.Path(manifest["ICD"]["library_path"])
assert lib.is_file() and lib.name=="libvulkan_wrapper.so"
assert lib.resolve().is_relative_to(pathlib.Path.cwd()/".source/gpu-stage1/wrapper-private-runtime")
print("VALID")
PY
)" = VALID
run="$ROOT/.source/gpu-stage1/wrapper-private-runtime"
env VK_DRIVER_FILES="$ICD" timeout 12 vulkaninfo --summary > "$run/vulkaninfo-summary.log" 2>&1
grep -q 'deviceName' "$run/vulkaninfo-summary.log"
grep -q 'VK_KHR_wayland_surface' "$run/vulkaninfo-summary.log"
grep -E 'apiVersion|deviceName|driverID|VK_KHR_wayland_surface' "$run/vulkaninfo-summary.log" | tail -8
echo 'PASS: private wrapper ICD real GPU + Wayland surface extension'
echo 'GPU AHB import and GPU command execution:'
bash graphics/tools/test-vulkan-client-ahb.sh wrapper-private |
    grep -E 'VK_AHB_IMPORT=PASS|VK_AHB_CLEAR=GPU_SUBMITTED_AND_COMPLETED|AHB_IPC_CHILD_EXIT=0'
bash graphics/tools/snapshot-termux-x11.sh verify | tail -2
echo 'PASS: Termux-X11 DRI3 unchanged; no GPU display frames claimed'
