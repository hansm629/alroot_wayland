#!/data/data/com.termux/files/usr/bin/python3
"""Rebuild three S30 private Xwayland objects and relink a separate binary.

Never writes the running/accepted S16, S28, or S29 executable or their
Meson objects. Reuse the pinned S16 private Meson graph for all other .o
files; S30 source changes are confined to window/present/KGSL AHB import.
"""
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / '.source/gpu-stage1/xwayland-kgsl/build'
OUT = ROOT / '.source/gpu-stage1/s30-min-extent'
(OUT / 'logs').mkdir(parents=True, exist_ok=True)
(OUT / 'runtime').mkdir(parents=True, exist_ok=True)

commands = subprocess.run(
    ['ninja', '-C', str(BUILD), '-t', 'commands', 'hw/xwayland/Xwayland'],
    check=True, capture_output=True, text=True, timeout=20,
).stdout.splitlines()
link = next((shlex.split(command) for command in reversed(commands)
             if ' -o hw/xwayland/Xwayland ' in command), None)
if not link:
    raise SystemExit('S30_RELINK=BLOCKED_UNKNOWN_MESON_LINK_TARGET')

temporary = (OUT / 'runtime/Xwayland.staging').resolve()
link[link.index('-o') + 1] = str(temporary)
replaced = {}
try:
    with (OUT / 'logs/relink.log').open('w') as out:
        for name in ('xwayland-glamor-gbm', 'xwayland-window',
                     'xwayland-present'):
            needle = '../source/hw/xwayland/' + name + '.c'
            compile_cmd = next((shlex.split(s) for s in commands
                                if needle in s and ' -c ' in s), None)
            if not compile_cmd:
                raise RuntimeError('Missing Meson source: ' + name)
            obj = (OUT / 'runtime' / (name + '.s30.o')).resolve()
            compile_cmd[compile_cmd.index('-o') + 1] = str(obj)
            if '-MF' in compile_cmd:
                compile_cmd[compile_cmd.index('-MF') + 1] = str(obj) + '.d'
            subprocess.run(compile_cmd, cwd=BUILD, stdout=out,
                           stderr=subprocess.STDOUT, check=True, timeout=65)
            replaced['hw/xwayland/Xwayland.p/' + name + '.c.o'] = str(obj)
        link = [replaced.get(arg, arg) for arg in link]
        if any(link.count(path) != 1 for path in replaced.values()):
            raise RuntimeError('A S30 replacement object was not linked once')
        subprocess.run(link, cwd=BUILD, stdout=out, stderr=subprocess.STDOUT,
                       check=True, timeout=90)
    if temporary.stat().st_size < 1_000_000:
        raise RuntimeError('S30 linked executable unexpectedly small')
    os.replace(temporary, OUT / 'runtime/Xwayland')
except Exception:
    temporary.unlink(missing_ok=True)
    raise

print('S30_ISOLATED_KGSL_XWAYLAND_LINK=PASS')
print('S30_PRIVATE_XWAYLAND=' + str(OUT / 'runtime/Xwayland'))
