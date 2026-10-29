#!/data/data/com.termux/files/usr/bin/bash
# Physical S26U/S24+/Mali/PowerVR feature probe for optional AHB BGRA8.
# Checks creation, cross-process transfer, VkImage import, GPU clear.
# Does NOT install/change Android APK, Mesa, ICDs, Termux-X11 DRI3.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
for format in rgba8 bgra8; do
    for backend in wrapper-private wrapper; do
        echo "--- AHB $format / $backend ---"
        if ALROOT_TEST_AHB_FORMAT="$format" \
            bash graphics/tools/test-vulkan-client-ahb.sh "$backend"; then
            echo "S12_AHB_${format}_${backend}=PASS"
        else
            echo "S12_AHB_${format}_${backend}=NOT_SUPPORTED"
            if [[ "$format" == rgba8 ]]; then
                exit 1
            fi
        fi
    done
done
echo 'NOTE: only a real import and GPU clear can justify optional v2 BGRA8'
