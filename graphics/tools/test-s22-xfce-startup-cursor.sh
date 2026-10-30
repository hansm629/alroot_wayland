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

value="$(python3 termux/xfce-cursor-env.py)"
[[ "$value" == $'Layan-white-cursors\t24' ]] || {
    printf 'S22_XFCE_THEME_CHANGED_FROM_S26U_SNAPSHOT=%q\n' "$value" >&2
    exit 1
}
echo 'S22_PRIVATE_XWAYLAND_XCURSOR_THEME=Layan-white-cursors size=24 PASS'
bash -n termux/alroot-wayland
bash graphics/tools/test-s21-pointer-startup-pip.sh > "$root/.source/s22-xcursor/p21-regression.log" 2>&1
tail -4 "$root/.source/s22-xcursor/p21-regression.log"
echo 'S22_EXISTING_S21_POINTER_AND_PIP=PASS'
echo 'S22_REAL_ANDROID_CURSOR_TRANSITION=TEST_PENDING'
