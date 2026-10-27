#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

PROJECT=/data/data/com.termux/files/home/alroot_wayland
cd "$PROJECT"
for script in termux/alroot-wayland termux/awstart termux/alroot-battery-genmon.sh termux/clipboard-broker.sh \
              packages/build-wlroots-termux.sh tools/install-cli.sh tools/sign-local.sh; do
    bash -n "$script"
done
echo 'PASS: Termux shell syntax'

[[ "$(git rev-parse --show-toplevel)" == "$PROJECT" ]]
[[ -r "native/waylandcomp/src/compositor.c" && -r "native/waylandcomp/src/vk_present.c" ]]
[[ -r "native/waylandcomp/src/wl_clipboard.c" && -r "native/waylandcomp/src/wl_text_input.c" ]]
echo 'PASS: actual DroidDeck source extraction'

command -v xclip >/dev/null && command -v xdotool >/dev/null
command -v Xwayland >/dev/null && command -v labwc >/dev/null
LD_LIBRARY_PATH="$PROJECT/packages/prefix/lib" labwc -v
echo 'PASS: pinned private wlroots / Labwc dynamic linking'

doctor_output="$(alroot-wayland doctor)"
status_output="$(alroot-wayland status)"
grep -q 'labwc:' <<< "$doctor_output"
grep -q 'xwayland:' <<< "$status_output"
grep -q '^x11_desktop:' <<< "$status_output"
echo 'PASS: CLI diagnostics without altering pre-existing session'
help_output="$(awstart --help)"
normal_preview="$(awstart --dry-run)"
direct_preview="$(awstart --direct --dry-run)"
grep -q 'Start helper + XFCE4' <<< "$help_output"
grep -q 'profile=xfce-labwc' <<< "$normal_preview"
grep -q 'profile=xfce-shm-direct' <<< "$direct_preview"
if awstart --profile unsupported --dry-run >/dev/null 2>&1; then
    echo 'FAIL: launcher accepted an unapproved profile' >&2; exit 1
fi
echo 'PASS: tx11start-style awstart parsing and no-side-effect previews'

if [[ -r artifacts/alroot-wayland-p01-signed.apk ]]; then
    apk="$(pm path com.termux | sed -n '1s/^package://p')"
    android_cert="$(apksigner verify --print-certs "$apk" |
        sed -nE 's/^V[0-9](\.[0-9]+)? Signer: certificate SHA-256 digest: //p' | head -1)"
    helper_cert="$(apksigner verify --print-certs artifacts/alroot-wayland-p01-signed.apk |
        sed -nE 's/^V[0-9](\.[0-9]+)? Signer: certificate SHA-256 digest: //p' | head -1)"
    [[ -n "$android_cert" && "$android_cert" == "$helper_cert" ]]
    echo 'PASS: helper signing cert matches currently installed Termux'
fi

echo 'P01 smoke complete. Android UI/XFCE/input remains TEST-PENDING until device checks.'
