#!/data/data/com.termux/files/usr/bin/python3
"""Build an isolated S36 Xwayland executable from an S32-pinned Meson graph.

Compiles only experimental GLAMOR AHB output+helper+protocol objects.
Does not rewrite the original Xwayland Meson object, user process,
system GLES/Vulkan, or any S16-S32 pinned project server.
"""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / '.source/gpu-stage1/xwayland-kgsl/build'
OUT = ROOT / '.source/gpu-stage1/s36-ahb-output'
S32 = ROOT / '.source/gpu-stage1/s32-no-blank-present/runtime'
S32_HASH = 'b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455'
if hashlib.sha256((S32 / 'Xwayland').read_bytes()).hexdigest() != S32_HASH:
    raise SystemExit('S36_RELINK=BLOCKED_UNTRUSTED_S32_BASELINE')

(OUT / 'runtime').mkdir(parents=True, exist_ok=True)
(OUT / 'logs').mkdir(parents=True, exist_ok=True)
commands = subprocess.run(
    ['ninja', '-C', str(BUILD), '-t', 'commands', 'hw/xwayland/Xwayland'],
    check=True, capture_output=True, text=True, timeout=20,
).stdout.splitlines()
link = next((shlex.split(c) for c in reversed(commands)
             if ' -o hw/xwayland/Xwayland ' in c), None)
if not link:
    raise SystemExit('S36_RELINK=BLOCKED_BAD_MESON_TARGET')
staging = (OUT / 'runtime/Xwayland.staging').resolve()
link[link.index('-o') + 1] = str(staging)

source = OUT / 'scratch/xwayland-glamor-gbm.s36.c'
if not source.exists():
    raise SystemExit('S36_RELINK=BLOCKED_MISSING_PRIVATE_OVERLAY_SOURCE')
with (OUT / 'logs/relink.log').open('w') as log:
    try:
        source_compile = next((shlex.split(c) for c in commands
                               if '../source/hw/xwayland/xwayland-glamor-gbm.c'
                               in c and ' -c ' in c), None)
        if not source_compile:
            raise RuntimeError('missing pinned KGSL GBM target')
        replacement = (OUT / 'runtime/xwayland-glamor-gbm.s36.o').resolve()
        source_compile[source_compile.index('-o') + 1] = str(replacement)
        # Replace the source path with the isolated copy.
        source_compile = [str(source.resolve()) if arg ==
                          '../source/hw/xwayland/xwayland-glamor-gbm.c'
                          else arg for arg in source_compile]
        source_compile += ['-I' + str(ROOT / 'graphics/include'),
                           '-I' + str(ROOT / 'native/waylandcomp/generated')]
        if '-MF' in source_compile:
            source_compile[source_compile.index('-MF') + 1] = str(replacement) + '.d'
        subprocess.run(source_compile, cwd=BUILD, check=True, stdout=log,
                       stderr=subprocess.STDOUT, timeout=65)

        # Preserve the previous S32 window+Present byte-exact objects;
        # only replace GLAMOR export code, not compositor or X11 semantics.
        replacements = {
            'hw/xwayland/Xwayland.p/xwayland-glamor-gbm.c.o': str(replacement),
            'hw/xwayland/Xwayland.p/xwayland-window.c.o':
                str((S32 / 'xwayland-window.s32.o').resolve()),
            'hw/xwayland/Xwayland.p/xwayland-present.c.o':
                str((S32 / 'xwayland-present.s32.o').resolve()),
        }
        link = [replacements.get(arg, arg) for arg in link]
        for object_path in replacements.values():
            if link.count(object_path) != 1:
                raise RuntimeError('S36 replacement object not linked exactly once: ' + object_path)

        extra = (
            ('s36_xwl_ahb_output', ROOT / 'graphics/src/s36_xwl_ahb_output.c'),
            ('kgsl_ahb_fd', ROOT / 'graphics/src/kgsl_ahb_fd.c'),
            ('alroot-gpu-buffer-v1-protocol',
             ROOT / 'native/waylandcomp/generated/alroot-gpu-buffer-v1-protocol.c'),
        )
        for name, filename in extra:
            obj = (OUT / 'runtime' / (name + '.s36.o')).resolve()
            cmd = ['cc', '-c', '-std=gnu11', '-O2', '-fPIC', '-Wall', '-Wextra',
                   '-I' + str(ROOT / 'graphics/include'),
                   '-I' + str(ROOT / 'native/waylandcomp/generated'),
                   '-I/data/data/com.termux/files/usr/include',
                   str(filename), '-o', str(obj)]
            subprocess.run(cmd, cwd=BUILD, check=True, stdout=log,
                           stderr=subprocess.STDOUT, timeout=65)
            link.append(str(obj))
        # The base Xwayland uses libepoxy wrappers and does not directly
        # link to eglGetProcAddress / GLES3 symbols. Our isolated module
        # uses native EGL/GLES calls to blit to AHB-backed external memory.
        link += ['-lEGL', '-lGLESv2']
        subprocess.run(link, cwd=BUILD, check=True, stdout=log,
                       stderr=subprocess.STDOUT, timeout=90)
        if staging.stat().st_size < 1_000_000:
            raise RuntimeError('unexpected private Xwayland binary size')
        os.replace(staging, OUT / 'runtime/Xwayland')
    except Exception:
        staging.unlink(missing_ok=True)
        raise

binary = OUT / 'runtime/Xwayland'
print('S36_PRIVATE_KGSL_AHB_OUTPUT_XWAYLAND_BUILT=PASS')
print('S36_PRIVATE_BINARY=' + str(binary))
print('S36_PRIVATE_BINARY_SHA256=' + hashlib.sha256(binary.read_bytes()).hexdigest())
