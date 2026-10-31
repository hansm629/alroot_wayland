#!/data/data/com.termux/files/usr/bin/python3
"""Build S37 Xwayland with a narrow-height Qualcomm DRI3 AHB fix.

Only a separate private GLAMOR object and executable are produced. All
S36 AHB direct-output helper/protocol objects and S32 X11 Present/window
objects are reused byte-for-byte; a running user's Xwayland is untouched.
"""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / '.source/gpu-stage1/xwayland-kgsl/build'
OUT = ROOT / '.source/gpu-stage1/s37-thin-ahb'
S32 = ROOT / '.source/gpu-stage1/s32-no-blank-present/runtime'
S36 = ROOT / '.source/gpu-stage1/s36-ahb-output/runtime'
S32_SHA = 'b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455'
S36_SHA = 'df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5'

for baseline, digest in [(S32, S32_SHA), (S36, S36_SHA)]:
    if hashlib.sha256((baseline / 'Xwayland').read_bytes()).hexdigest() != digest:
        raise SystemExit('S37_RELINK=BLOCKED_PREVIOUS_BINARY_CHANGED')

source = OUT / 'scratch/xwayland-glamor-gbm.s37.c'
if not source.is_file() or source.is_symlink():
    raise SystemExit('S37_RELINK=BLOCKED_MISSING_PRIVATE_SOURCE')
(OUT / 'runtime').mkdir(parents=True, exist_ok=True)
(OUT / 'logs').mkdir(parents=True, exist_ok=True)

commands = subprocess.run(
    ['ninja', '-C', str(BUILD), '-t', 'commands', 'hw/xwayland/Xwayland'],
    capture_output=True, text=True, check=True, timeout=25,
).stdout.splitlines()
link = next((shlex.split(cmd) for cmd in reversed(commands)
             if ' -o hw/xwayland/Xwayland ' in cmd), None)
compile_cmd = next((shlex.split(cmd) for cmd in commands
                    if '../source/hw/xwayland/xwayland-glamor-gbm.c' in cmd
                    and ' -c ' in cmd), None)
if not link or not compile_cmd:
    raise SystemExit('S37_RELINK=BLOCKED_MESON_OBJECT_GRAPH_INVALID')

stage = (OUT / 'runtime/Xwayland.staging').resolve()
dst_obj = (OUT / 'runtime/xwayland-glamor-gbm.s37.o').resolve()
compile_cmd[compile_cmd.index('-o') + 1] = str(dst_obj)
compile_cmd = [str(source.resolve()) if arg ==
               '../source/hw/xwayland/xwayland-glamor-gbm.c'
               else arg for arg in compile_cmd]
compile_cmd += ['-I' + str(ROOT / 'graphics/include'),
                '-I' + str(ROOT / 'native/waylandcomp/generated')]
if '-MF' in compile_cmd:
    compile_cmd[compile_cmd.index('-MF') + 1] = str(dst_obj) + '.d'

link[link.index('-o') + 1] = str(stage)
objects = {
    'hw/xwayland/Xwayland.p/xwayland-glamor-gbm.c.o': str(dst_obj),
    'hw/xwayland/Xwayland.p/xwayland-window.c.o':
        str((S32 / 'xwayland-window.s32.o').resolve()),
    'hw/xwayland/Xwayland.p/xwayland-present.c.o':
        str((S32 / 'xwayland-present.s32.o').resolve()),
}
link = [objects.get(arg, arg) for arg in link]
extra = [S36 / (name + '.s36.o') for name in (
    's36_xwl_ahb_output', 'kgsl_ahb_fd', 'alroot-gpu-buffer-v1-protocol')]
for path in [str((S32 / 'xwayland-window.s32.o').resolve()),
             str((S32 / 'xwayland-present.s32.o').resolve())] + \
            [str(p) for p in extra]:
    if not Path(path).is_file():
        raise SystemExit('S37_RELINK=BLOCKED_REQUIRED_OBJECT_MISSING:' + path)
for path in objects.values():
    if link.count(path) != 1:
        raise SystemExit('S37_RELINK=BLOCKED_OBJECT_NOT_SINGLE_REPLACEMENT')
link += [str(p.resolve()) for p in extra] + ['-lEGL', '-lGLESv2']

with (OUT / 'logs/relink.log').open('w') as log:
    try:
        subprocess.run(compile_cmd, cwd=BUILD, check=True,
                       stdout=log, stderr=subprocess.STDOUT, timeout=70)
        subprocess.run(link, cwd=BUILD, check=True,
                       stdout=log, stderr=subprocess.STDOUT, timeout=95)
        if stage.stat().st_size < 1_000_000:
            raise RuntimeError('unexpected Xwayland executable size')
        os.replace(stage, OUT / 'runtime/Xwayland')
    except Exception:
        stage.unlink(missing_ok=True)
        raise

binary = OUT / 'runtime/Xwayland'
print('S37_PRIVATE_XWAYLAND_LINK=PASS')
print('S37_PRIVATE_BINARY=' + str(binary))
print('S37_PRIVATE_BINARY_SHA256=' + hashlib.sha256(binary.read_bytes()).hexdigest())
