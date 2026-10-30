#!/data/data/com.termux/files/usr/bin/bash
# Pure-JVM Xcursor importer and private XFCE configuration regression.
# No input injection, no XFCE restarts, no Termux-X11 global modifications.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$root"
out="$root/.source/s22-xcursor/classes"
mkdir -p "$out"
javac -encoding UTF-8 -d "$out" \
    app/src/main/java/com/alroot/wayland/XcursorThemeLoader.java \
    tests/XcursorThemeLoaderTest.java
java -cp "$out" com.alroot.wayland.XcursorThemeLoaderTest

python3 - <<'PY'
import importlib.util
from pathlib import Path
source=Path('termux/xfce-cursor-env.py')
spec=importlib.util.spec_from_file_location('xfce_cursor_env',source)
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
temp=Path('.source/s22-xcursor/sample.xml')
temp.parent.mkdir(parents=True,exist_ok=True)
temp.write_text('<channel><property name="Gtk"><property name="CursorThemeName" value="XTest"/><property name="CursorThemeSize" value="36"/></property></channel>')
assert mod.cursor_values(temp)==('XTest',36)
temp.write_text('<channel><property name="CursorThemeName" value="../escape"/></channel>')
assert mod.cursor_values(temp) is None
temp.unlink()
print('S22_TERMUX_XCURSOR_XSETTINGS_PARSER=PASS')
PY

python3 - <<'PY'
from pathlib import Path
import importlib.util
import subprocess
root=Path.cwd()
spec=importlib.util.spec_from_file_location('xfce_cursor_env',root/'termux/xfce-cursor-env.py')
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
config=Path.home()/'.config/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml'
expected=mod.cursor_values(config)
assert expected, 'S26U XFCE cursor settings unavailable'
actual=subprocess.check_output(['python3',str(root/'termux/xfce-cursor-env.py')],text=True).strip()
assert actual == f'{expected[0]}\t{expected[1]}', (actual,expected)
print(f'S22_PRIVATE_XWAYLAND_XCURSOR_THEME={expected[0]} size={expected[1]} PASS')
PY
bash -n termux/alroot-wayland
bash graphics/tools/test-s21-pointer-startup-pip.sh > "$root/.source/s22-xcursor/p21-regression.log" 2>&1
tail -4 "$root/.source/s22-xcursor/p21-regression.log"
echo 'S22_EXISTING_S21_POINTER_AND_PIP=PASS'
echo 'S22_REAL_ANDROID_CURSOR_TRANSITION=TEST_PENDING'
