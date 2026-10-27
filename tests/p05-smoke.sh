#!/data/data/com.termux/files/usr/bin/bash
# Native, read-only source and isolated Alroot display smoke checks.
set -euo pipefail
ROOT=/data/data/com.termux/files/home/alroot_wayland
cd "$ROOT"
for script in termux/alroot-wayland termux/aw-debian \
              termux/alroot-notes.sh termux/alroot-battery-tray.sh \
              termux/build-ime-server.sh; do
    bash -n "$script"
done
python3 - <<'PY'
from pathlib import Path
import xml.etree.ElementTree as ET

root = Path('/data/data/com.termux/files/home/.alroot-wayland')
profile = root / 'xfce-config'
panel = profile / 'xfce4/xfconf/xfce-perchannel-xml/xfce4-panel.xml'
items = ET.parse(panel).getroot()
assert items.find("./property[@name='plugins']") is not None
for entry, script in [('alroot-battery-tray.desktop','alroot-battery-tray.sh'),
                      ('xfce4-notes-autostart.desktop','alroot-notes.sh')]:
    text = (profile / 'autostart' / entry).read_text()
    assert script in text, entry
assert (root / 'notes-data/notes').is_dir()

# The Java/JNI/native output names must use Alroot. Upstream attribution
# remains deliberately present in LICENSING.md / LICENSES/.
paths = list(Path('native/waylandcomp/src').rglob('*.c'))
paths += list(Path('native/waylandcomp/src').rglob('*.h'))
paths += list(Path('app/src/main/java').rglob('*.java'))
for path in paths:
    assert 'droiddeck' not in path.read_text(errors='replace').lower(), path
assert 'DroidDeck' in Path('LICENSING.md').read_text()
assert 'add_library(alrootwayland' in Path('native/waylandcomp/CMakeLists.txt').read_text()
print('PASS: battery/notes private autostart + Alroot runtime rebranding')
PY
test "$(readlink -f /data/data/com.termux/files/usr/bin/debian)" = \
    /data/data/com.termux/files/usr/bin/debian
test "$(readlink -f /data/data/com.termux/files/usr/bin/aw-debian)" = \
    "$ROOT/termux/aw-debian"
echo 'PASS: original Debian launcher preserved; Alroot-only wrapper installed'
if [[ -r "$HOME/.alroot-wayland/display" ]]; then
    display="$(cat "$HOME/.alroot-wayland/display")"
    [[ "$display" != :0 && "$display" =~ ^:[0-9]{1,3}$ ]]
    current="$(timeout 12 aw-debian /usr/bin/xdpyinfo)"
    grep -q "name of display: *$display" <<< "$current"
    echo "PASS: Alroot Debian session uses the isolated X display $display"
fi
echo 'PASS: P05 smoke; real Android menu and Korean keyboard need user verification.'
