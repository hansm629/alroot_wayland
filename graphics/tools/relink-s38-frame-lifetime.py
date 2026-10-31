#!/data/data/com.termux/files/usr/bin/python3
"""Replay S38 into an isolated overlay and build a private Xwayland.

S37 and the running desktop are immutable. Five private objects change;
the S36 AHB output helper keeps its pinned implementation. The Present
snapshot runs before the existing release synchronization. No APK,
installed Mesa or Termux-X11 is modified.
"""
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
STAGE = ROOT / '.source/gpu-stage1'
BUILD = STAGE / 'xwayland-kgsl/build'
SOURCE = STAGE / 'xwayland-kgsl/source/hw/xwayland'
OUT = STAGE / 's38-frame-continuity'
OVERLAY = OUT / 'scratch/overlay'
RUNTIME = OUT / 'runtime'
S32 = STAGE / 's32-no-blank-present/runtime'
S36 = STAGE / 's36-ahb-output/runtime'
S37 = STAGE / 's37-thin-ahb/runtime'
INPUTS = {
    'hw/xwayland/xwayland-glamor-gbm.c': (
        STAGE / 's37-thin-ahb/scratch/xwayland-glamor-gbm.s37.c',
        'b2a84ec1927f2a7154c1689fa223b531d5d30c395cfa6d0036318cd701a10570',
        '21cd304ab8d3eeb32f114d724c68330f66e70f7e5a4d67a354f2608c8ab8293e'),
    'hw/xwayland/xwayland-screen.c': (
        SOURCE / 'xwayland-screen.c',
        '89a2901854c80c2c74ed260ddafef81a10d44219477aae183aa38eb2a92d692d',
        '54f8078fc3b2ff803d394f119834b10e75d0d8f3db44e3e4af7d9fc01c94e211'),
    'hw/xwayland/xwayland-window.c': (
        SOURCE / 'xwayland-window.c',
        '85b3d7cda893a679ca92fc5958100e837d6ab6f6870049792076a03f6d3ed9ea',
        'f388c03ef6fd5d27ec5bd4e9f38d05ad5bdf6608a6a9ae74f874e3e1bba63bb4'),
    'hw/xwayland/xwayland-present.c': (
        STAGE / 's32-no-blank-present/replay/hw/xwayland/xwayland-present.c',
        'fae115e07784e39685ead8da8152ffc5af7c51b26e2fa1f4ddc2d4fcdcb3c73b',
        '72295f53cf36e81d77355096f83369a98ca16a73f6e032b01b7d95844b2731c5'),
    'present/present_execute.c': (
        STAGE / 'xwayland-kgsl/source/present/present_execute.c',
        '0316fb0741a32485eba03c0cbf652754af9f4080eb483a24a8eabe00902176fc',
        '87cd32f236367c37f8381141888eb3061b5cbe773ae79bfb3fc64b4c8e1bca05'),
    'present/present_priv.h': (
        STAGE / 'xwayland-kgsl/source/present/present_priv.h',
        '17ed95ceab4f0e3f632802e356d5e63a35dd33a21a0a1abccdaf156323e79cc4',
        'adec4c029aa3e6d46ed60660f8134f9537ecb4d9b84ee0c2d311eba967f529fb'),
}
PATCHES = ('0012-kgsl-frame-release-lifetime.patch',
           '0013-kgsl-latest-present-resize-snapshot.patch',
           '0014-kgsl-snapshot-before-present-idle.patch')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    model = subprocess.check_output(['/system/bin/getprop', 'ro.product.model'],
                                    text=True, timeout=3).strip()
    if not model.startswith('SM-S948'):
        raise RuntimeError('S38 hardware validation is limited to S26 Ultra')
    pinned = {
        S32 / 'Xwayland': 'b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455',
        S36 / 'Xwayland': 'df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5',
        S37 / 'Xwayland': '193f43e03f0e754c39cfb73e04ac1e0032f8f632e6d06b5d1b675f67c834a594',
    }
    for path, expected in pinned.items():
        if path.is_symlink() or digest(path) != expected:
            raise RuntimeError('old private binary changed: ' + str(path))
    status_before = subprocess.check_output(['awstart', '--status'], text=True, timeout=8)
    status_lines = [line for line in status_before.splitlines()
                    if line.startswith(('xwayland:', 'xfce:', 'ime:', 'clipboard:'))]
    for line in status_lines:
        if line.startswith('xwayland: RUNNING '):
            pid = int(line.rsplit(' ', 1)[1])
            if Path(f'/proc/{pid}/exe').resolve() == RUNTIME / 'Xwayland':
                raise RuntimeError('refusing to rebuild the running S38 desktop')
    RUNTIME.mkdir(parents=True, exist_ok=True)
    (OUT / 'logs').mkdir(parents=True, exist_ok=True)
    target_sources = OVERLAY
    target_sources.mkdir(parents=True, exist_ok=True)
    for name, (path, expected, _) in INPUTS.items():
        if digest(path) != expected:
            raise RuntimeError('input source changed: ' + str(path))
        (target_sources / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target_sources / name)
    with (OUT / 'logs/relink.log').open('w') as log:
        for patch in PATCHES:
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i',
                            str(ROOT / 'graphics/patches/xwayland/kgsl' / patch)],
                           cwd=OVERLAY, check=True, stdout=log, stderr=subprocess.STDOUT,
                           timeout=15)
        for name, (_, _, expected) in INPUTS.items():
            if digest(target_sources / name) != expected:
                raise RuntimeError('patch replay mismatch: ' + name)
        commands = subprocess.run(
            ['ninja', '-C', str(BUILD), '-t', 'commands', 'hw/xwayland/Xwayland'],
            capture_output=True, text=True, check=True, timeout=25).stdout.splitlines()
        link = next(shlex.split(c) for c in reversed(commands)
                    if ' -o hw/xwayland/Xwayland ' in c)
        replacements = {}
        for name in INPUTS:
            if not name.endswith('.c'):
                continue
            original = '../source/' + name
            compile_cmd = next(shlex.split(c) for c in commands
                               if original in c and ' -c ' in c)
            original_object = compile_cmd[compile_cmd.index('-o') + 1]
            obj = RUNTIME / (Path(name).stem + '.s38.o')
            compile_cmd[compile_cmd.index('-o') + 1] = str(obj)
            compile_cmd = [str(target_sources / name) if a == original else a
                           for a in compile_cmd]
            if '-MF' in compile_cmd:
                compile_cmd[compile_cmd.index('-MF') + 1] = str(obj) + '.d'
            compile_cmd.insert(1, '-I' + str(OVERLAY / 'present'))
            compile_cmd += ['-I' + str(ROOT / 'graphics/include'),
                            '-I' + str(ROOT / 'native/waylandcomp/generated')]
            subprocess.run(compile_cmd, cwd=BUILD, check=True, stdout=log,
                           stderr=subprocess.STDOUT, timeout=80)
            replacements[original_object] = str(obj)
        staging = RUNTIME / 'Xwayland.staging'
        link[link.index('-o') + 1] = str(staging)
        # present_execute normally resides in a static archive. Supplying
        # its complete replacement object first keeps the original member
        # unselected without editing the shared archive or build tree.
        extra_objects = [value for key, value in replacements.items() if key not in link]
        link = [replacements.get(a, a) for a in link]
        link[1:1] = extra_objects
        link += [str(S36 / (name + '.s36.o')) for name in (
            's36_xwl_ahb_output', 'kgsl_ahb_fd', 'alroot-gpu-buffer-v1-protocol')]
        link += ['-lEGL', '-lGLESv2']
        subprocess.run(link, cwd=BUILD, check=True, stdout=log,
                       stderr=subprocess.STDOUT, timeout=95)
        os.replace(staging, RUNTIME / 'Xwayland')
    for path, expected in pinned.items():
        if digest(path) != expected:
            raise RuntimeError('previous binary changed during build')
    status_after = subprocess.check_output(['awstart', '--status'], text=True, timeout=8)
    if any(line not in status_after.splitlines() for line in status_lines):
        raise RuntimeError('running user session changed during build')
    receipt = {'binary_sha256': digest(RUNTIME / 'Xwayland'),
               'binary': str(RUNTIME / 'Xwayland'),
               'sources': {name: values[2] for name, values in INPUTS.items()},
               'required_flag': 'S38_KGSL_FRAME_LIFETIME=1'}
    (OUT / 'build-manifest.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('S38_PRIVATE_XWAYLAND_LINK=PASS')
    print('S38_PRIVATE_BINARY_SHA256=' + receipt['binary_sha256'])
    print('S38_PREVIOUS_BINARIES_AND_USER_SESSION_UNCHANGED=PASS')


if __name__ == '__main__':
    main()
