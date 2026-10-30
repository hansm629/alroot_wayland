#!/data/data/com.termux/files/usr/bin/python3
"""Link S29 to an isolated binary, leaving accepted S16/S28 objects intact.

Rebuild only the edited hw/xwayland/xwayland-window.c object from the
existing pinned Termux Meson command line. The original executable and
the running user Xwayland are neither linked over nor restarted.
"""
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / '.source/gpu-stage1/xwayland-kgsl/build'
OUT = ROOT / '.source/gpu-stage1/s29-native-resize'
(OUT / 'logs').mkdir(parents=True, exist_ok=True)
(OUT / 'runtime').mkdir(parents=True, exist_ok=True)

commands = subprocess.run(
    ['ninja', '-C', str(BUILD), '-t', 'commands', 'hw/xwayland/Xwayland'],
    capture_output=True, text=True, check=True, timeout=18,
).stdout.splitlines()
compile_cmd = next((shlex.split(s) for s in commands
                    if '../source/hw/xwayland/xwayland-window.c' in s
                    and ' -c ' in s), None)
link_cmd = next((shlex.split(s) for s in reversed(commands)
                 if ' -o hw/xwayland/Xwayland ' in s), None)
if not compile_cmd or not link_cmd:
    raise SystemExit('S29_RELINK=BLOCKED_UNRECOGNIZED_MESON_BUILD')

obj = (OUT / 'runtime/xwayland-window.s29.o').resolve()
staged = (OUT / 'runtime/Xwayland.staging').resolve()
existing_obj = 'hw/xwayland/Xwayland.p/xwayland-window.c.o'
compile_cmd[compile_cmd.index('-o') + 1] = str(obj)
if '-MF' in compile_cmd:
    compile_cmd[compile_cmd.index('-MF') + 1] = str(obj) + '.d'
link_cmd[link_cmd.index('-o') + 1] = str(staged)
link_cmd = [str(obj) if arg == existing_obj else arg for arg in link_cmd]
if link_cmd.count(str(obj)) != 1:
    raise SystemExit('S29_RELINK=BLOCKED_EDITED_OBJECT_NOT_LINKED_ONCE')

try:
    with (OUT / 'logs/relink.log').open('w') as out:
        subprocess.run(compile_cmd, cwd=BUILD, stdout=out,
                       stderr=subprocess.STDOUT, check=True, timeout=45)
        subprocess.run(link_cmd, cwd=BUILD, stdout=out,
                       stderr=subprocess.STDOUT, check=True, timeout=90)
    if staged.stat().st_size < 1000000:
        raise RuntimeError('S29 executable unexpectedly small')
    os.replace(staged, OUT / 'runtime/Xwayland')
except Exception:
    staged.unlink(missing_ok=True)
    raise

print('S29_PRIVATE_KGSL_XWAYLAND_LINK=PASS')
print('S29_PRIVATE_XWAYLAND_PATH=' + str(OUT / 'runtime/Xwayland'))
