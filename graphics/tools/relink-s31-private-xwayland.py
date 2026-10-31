#!/data/data/com.termux/files/usr/bin/python3
"""Relink S31 separately; reuse pinned S30 KGSL importer + private Meson graph.

Only the S31 Xwayland Window and Present source objects are recompiled.
Never replace the running XFCE Xwayland, S16/S28/S29/S30 binaries, or the
installed Termux X11/Mesa/Vulkan packages.
"""

import hashlib
import os
from pathlib import Path
import shlex
import subprocess


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / '.source/gpu-stage1/xwayland-kgsl/build'
OUT = ROOT / '.source/gpu-stage1/s31-resize-continuity'
S30_RUNTIME = ROOT / '.source/gpu-stage1/s30-min-extent/runtime'
S30_SHA = '4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31'

(OUT / 'logs').mkdir(parents=True, exist_ok=True)
(OUT / 'runtime').mkdir(parents=True, exist_ok=True)

if hashlib.sha256((S30_RUNTIME / 'Xwayland').read_bytes()).hexdigest() != S30_SHA:
    raise SystemExit('S31_RELINK=BLOCKED_S30_KGSL_SERVER_NOT_PINNED')

commands = subprocess.run(
    ['ninja', '-C', str(BUILD), '-t', 'commands', 'hw/xwayland/Xwayland'],
    check=True, capture_output=True, text=True, timeout=20,
).stdout.splitlines()
link = next((shlex.split(c) for c in reversed(commands)
             if ' -o hw/xwayland/Xwayland ' in c), None)
if not link:
    raise SystemExit('S31_RELINK=BLOCKED_UNRECOGNIZED_MESON_TARGET')

staged = (OUT / 'runtime/Xwayland.staging').resolve()
link[link.index('-o') + 1] = str(staged)
gbm_object = (S30_RUNTIME / 'xwayland-glamor-gbm.s30.o').resolve()
link = [str(gbm_object)
        if arg == 'hw/xwayland/Xwayland.p/xwayland-glamor-gbm.c.o'
        else arg for arg in link]

try:
    with (OUT / 'logs/relink.log').open('w') as logfile:
        for name in ('xwayland-window', 'xwayland-present'):
            filename = '../source/hw/xwayland/' + name + '.c'
            compile_cmd = next((shlex.split(c) for c in commands
                                if filename in c and ' -c ' in c), None)
            if not compile_cmd:
                raise RuntimeError('Missing Meson object for ' + name)
            target = (OUT / 'runtime' / (name + '.s31.o')).resolve()
            compile_cmd[compile_cmd.index('-o') + 1] = str(target)
            if '-MF' in compile_cmd:
                compile_cmd[compile_cmd.index('-MF') + 1] = str(target) + '.d'
            subprocess.run(compile_cmd, cwd=BUILD, stdout=logfile,
                           stderr=subprocess.STDOUT, check=True, timeout=65)
            link = [str(target) if arg ==
                    'hw/xwayland/Xwayland.p/' + name + '.c.o' else arg
                    for arg in link]
        if any(link.count(str(object_path)) != 1 for object_path in (
                gbm_object, OUT / 'runtime/xwayland-window.s31.o',
                OUT / 'runtime/xwayland-present.s31.o')):
            raise RuntimeError('S31 object replacement not exactly one-to-one')
        subprocess.run(link, cwd=BUILD, stdout=logfile,
                       stderr=subprocess.STDOUT, check=True, timeout=90)
    if staged.stat().st_size < 1_000_000:
        raise RuntimeError('S31 binary unexpectedly small')
    os.replace(staged, OUT / 'runtime/Xwayland')
except Exception:
    staged.unlink(missing_ok=True)
    raise

binary = OUT / 'runtime/Xwayland'
print('S31_ISOLATED_KGSL_XWAYLAND_LINK=PASS')
print('S31_PRIVATE_BINARY=' + str(binary))
print('S31_PRIVATE_BINARY_SHA256=' + hashlib.sha256(binary.read_bytes()).hexdigest())
