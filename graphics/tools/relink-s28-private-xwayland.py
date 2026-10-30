#!/data/data/com.termux/files/usr/bin/python3
"""Relink S28 into its own runtime without modifying live Xwayland objects.

Reuse the exact existing S16 KGSL private Meson object graph and replace
only the independently compiled xwayland-window.c object. Other build
outputs, the running XFCE server and the Termux Xwayland remain untouched.
"""
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / '.source/gpu-stage1/xwayland-kgsl/build'
OUT = ROOT / '.source/gpu-stage1/s28-kgsl-resize-preserve'
(OUT / 'logs').mkdir(parents=True, exist_ok=True)
(OUT / 'runtime').mkdir(parents=True, exist_ok=True)

result = subprocess.run(
    ['ninja', '-C', str(BUILD), '-t', 'commands', 'hw/xwayland/Xwayland'],
    capture_output=True, text=True, check=True, timeout=18,
)
commands = result.stdout.splitlines()
compile_cmd = next((shlex.split(line) for line in commands
                    if '../source/hw/xwayland/xwayland-window.c' in line
                    and ' -c ' in line), None)
link_cmd = next((shlex.split(line) for line in reversed(commands)
                 if ' -o hw/xwayland/Xwayland ' in line), None)
if not compile_cmd or not link_cmd:
    raise SystemExit('S28_RELINK=BLOCKED_UNKNOWN_MESON_TARGET')

obj = (OUT / 'runtime/xwayland-window.s28.o').resolve()
staged = (OUT / 'runtime/Xwayland.staging').resolve()
real_input = 'hw/xwayland/Xwayland.p/xwayland-window.c.o'

compile_cmd[compile_cmd.index('-o') + 1] = str(obj)
if '-MF' in compile_cmd:
    compile_cmd[compile_cmd.index('-MF') + 1] = str(obj) + '.d'
link_cmd[link_cmd.index('-o') + 1] = str(staged)
link_cmd = [str(obj) if arg == real_input else arg for arg in link_cmd]
if link_cmd.count(str(obj)) != 1:
    raise SystemExit('S28_RELINK=BLOCKED_EDITED_OBJECT_NOT_LINKED_ONCE')

try:
    with (OUT / 'logs/relink.log').open('w') as log:
        subprocess.run(compile_cmd, cwd=BUILD, stdout=log,
                       stderr=subprocess.STDOUT, check=True, timeout=45)
        subprocess.run(link_cmd, cwd=BUILD, stdout=log,
                       stderr=subprocess.STDOUT, check=True, timeout=90)
    if staged.stat().st_size < 1_000_000:
        raise RuntimeError('S28 linked executable unexpectedly small')
    os.replace(staged, OUT / 'runtime/Xwayland')
except Exception:
    staged.unlink(missing_ok=True)
    raise

print('S28_PRIVATE_KGSL_XWAYLAND_RELINK=PASS')
print('S28_PRIVATE_XWAYLAND_PATH=' + str(OUT / 'runtime/Xwayland'))
