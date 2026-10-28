#!/data/data/com.termux/files/usr/bin/bash
# P11 private Xvfb/D-Bus regression, never original Termux-X11 :0.
set -euo pipefail
PROJECT=/data/data/com.termux/files/home/alroot_wayland
cd "$PROJECT"
echo 'P11: build native Fcitx addon and test focus-specific routing'
bash termux/build-fcitx-bridge.sh
bash -n termux/alroot-wayland termux/awstart \
    termux/build-fcitx-bridge.sh tests/p11-native-integration.sh
bash tests/p11-native-integration.sh
echo 'P11: verify P10 legacy XTEST behavior when native IM is unavailable'
bash tests/p10-ime-smoothing.sh
echo 'P11: prior Android battery and wallpaper fixes remain intact'
bash tests/p08-battery-source.sh
python3 tests/p08-profile-battery.py
echo 'PASS: Native GTK/terminal preedit; English and raw Xlib XTEST fallback'
echo 'NOTE: physical Samsung keyboard behavior still needs acceptance testing'
