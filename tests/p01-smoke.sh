#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

PROJECT=/data/data/com.termux/files/home/alroot_wayland
cd "$PROJECT"
for script in termux/alroot-wayland termux/clipboard-broker.sh \
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
grep -q 'xwayland: DOWN' <<< "$status_output"
echo 'PASS: CLI diagnostics without altering pre-existing session'

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
