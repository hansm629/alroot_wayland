#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Reproduce the finite S44 Wayland/EGL probe; never run it or install files.

The original S12 source remains unchanged. --recipe prints the pinned recipe;
--prepare-only creates or verifies only the derived C source. Normal invocation
reuses a verified existing probe or builds a new one inside egl-probe only.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PREFIX = Path('/data/data/com.termux/files/usr')
OUT = ROOT / '.source/gpu-stage1/s44-sync-overhead/egl-probe'
BASE = ROOT / 'graphics/tests/wayland-egl-swaps.c'
SOURCE, BINARY = OUT / 'wayland-egl-bench.c', OUT / 'wayland-egl-bench'
BASE_SHA = 'd6d6d8c330c7a82aa1a3406f7d76d8112c84f8c25d0c40cad7d4df5151ba3cc1'
SOURCE_SHA = '1ea74cb45b77be96ac87fc72ad223f63d0b0d8ff6c0f0e32770525bb765574f9'
ACCEPTED_BINARY_SHA = '3dae63ae22fb2291d3a8439a34a5b7ef2280a5076b36ce0f2871fc2814992104'
ACCEPTED_RECEIPT_SHA = 'a01c09702e6811829efd9469a38187cffcba9201717384f985acd8e99722f153'
TRANSFORMS = [
    ['#include <stdint.h>', '#include <stdint.h>\n#include <time.h>'],
    ['if(frames<1||frames>600)', 'if(frames<1||frames>60000)'],
    ['[1..600 frames]', '[1..60000 frames]'],
    ['struct client c={.width=480,.height=270};', 'struct client c={.width=800,.height=600};'],
    ['    int swapped=0;', '    if (!eglSwapInterval(egl, 0)) return 7;\n'
        '    struct timespec begin, end;\n    clock_gettime(CLOCK_MONOTONIC, &begin);\n    int swapped=0;'],
    ['    printf("S12_EGL_SWAP_SUCCESS_COUNT=%d\\n",swapped);',
        '    clock_gettime(CLOCK_MONOTONIC, &end);\n'
        '    double elapsed=(end.tv_sec-begin.tv_sec)+(end.tv_nsec-begin.tv_nsec)/1e9;\n'
        '    printf("S44_EGL_BENCH frames=%d seconds=%.6f fps=%.3f interval=0\\n", swapped,elapsed,swapped/elapsed);\n'
        '    printf("S12_EGL_SWAP_SUCCESS_COUNT=%d\\n",swapped);'],
]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def pin(path, expected):
    require(not path.is_symlink() and path.is_file() and sha(path) == expected,
            'pinned input mismatch: ' + str(path))


def derived_source():
    pin(BASE, BASE_SHA)
    text = BASE.read_text()
    for old, new in TRANSFORMS:
        require(text.count(old) == 1, 'probe transformation is missing or ambiguous')
        text = text.replace(old, new, 1)
    require(hashlib.sha256(text.encode()).hexdigest() == SOURCE_SHA, 'derived probe source hash mismatch')
    return text


def recipe():
    return {'base': str(BASE), 'base_sha256': BASE_SHA, 'source_sha256': SOURCE_SHA,
        'command': ['clang', '--target=aarch64-linux-android35', '-std=c11', '-O2',
            '-Wall', '-Wextra', '-Werror', '-Inative/waylandcomp/generated', str(SOURCE),
            'native/waylandcomp/generated/xdg-shell-protocol.c',
            '-lwayland-client', '-lwayland-egl', '-lEGL', '-lGLESv2', '-o', str(BINARY)],
        'transformations': TRANSFORMS}


def protocol_inputs():
    names = ('native/waylandcomp/generated/xdg-shell-client-protocol.h',
             'native/waylandcomp/generated/xdg-shell-protocol.c')
    return {name: sha(ROOT / name) for name in names}


def reuse(expected):
    receipt_path = OUT / 'build.json'
    require(not receipt_path.is_symlink() and receipt_path.is_file(), 'existing probe has no regular build receipt')
    recorded = json.loads(receipt_path.read_text())
    require(all(recorded.get(key) == value for key, value in expected.items()), 'existing probe recipe differs')
    binary_sha = recorded.get('binary_sha256')
    require(isinstance(binary_sha, str) and len(binary_sha) == 64, 'existing probe has no binary pin')
    pin(BINARY, binary_sha)
    require(os.access(BINARY, os.X_OK), 'existing probe is not executable')
    if recorded.get('generator_schema') == 1:
        require(recorded['protocol_inputs'] == protocol_inputs(), 'generated protocol inputs changed')
    else:
        # Preserve the already-measured probe and its exact historical receipt.
        pin(receipt_path, ACCEPTED_RECEIPT_SHA)
        require(binary_sha == ACCEPTED_BINARY_SHA, 'unrecognized historical probe')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--recipe', action='store_true')
    mode.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    text, expected = derived_source(), recipe()
    if args.recipe:
        print(json.dumps(expected, indent=2))
        print('S44_EGL_PROBE=RECIPE_ONLY')
        return
    for path in (OUT, *OUT.parents):
        require(not path.is_symlink(), 'probe output path is a symlink: ' + str(path))
        if path == ROOT:
            break
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / 'generator.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if SOURCE.exists() or SOURCE.is_symlink():
            pin(SOURCE, SOURCE_SHA)
        else:
            SOURCE.write_text(text)
        if args.prepare_only:
            print('S44_EGL_PROBE=SOURCE_VERIFIED')
            return
        if BINARY.exists() or BINARY.is_symlink():
            reuse(expected)
            print('S44_EGL_PROBE=EXISTING_VERIFIED')
            print('S44_EGL_PROBE_SHA256=' + sha(BINARY))
            return
        require(not (OUT / 'build.json').exists(), 'orphaned build receipt preserved; inspect before rebuilding')
        require(Path(os.environ.get('PREFIX', '')) == PREFIX, 'new probe build requires native Termux')
        env = os.environ.copy()
        for name in ('LD_PRELOAD', 'LD_LIBRARY_PATH', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS',
                     'CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH'):
            env.pop(name, None)
        arch = subprocess.check_output(['dpkg', '--print-architecture'], env=env, text=True, timeout=10).strip()
        require(arch == 'aarch64', 'new probe build requires native aarch64 Termux')
        protocols = protocol_inputs()
        pending = OUT / ('wayland-egl-bench.pending-' + str(os.getpid()))
        require(not pending.exists() and not pending.is_symlink(), 'pending probe output exists')
        command = [*expected['command'][:-1], str(pending)]
        try:
            with (OUT / ('build-' + str(os.getpid()) + '.log')).open('x') as log:
                subprocess.run(command, cwd=ROOT, env=env, check=True, stdout=log,
                               stderr=subprocess.STDOUT, timeout=90)
            pin(BASE, BASE_SHA); pin(SOURCE, SOURCE_SHA)
            require(protocols == protocol_inputs(), 'protocol inputs changed during compilation')
            require(not BINARY.exists() and not BINARY.is_symlink(), 'probe appeared during compilation; preserving it')
            os.replace(pending, BINARY)
            receipt = {**expected, 'binary_sha256': sha(BINARY), 'generator_schema': 1,
                       'protocol_inputs': protocols, 'actual_compile_command': command,
                       'generator_sha256': sha(Path(__file__).resolve()), 'probe_executed': False}
            with (OUT / 'build.json').open('x') as file:
                file.write(json.dumps(receipt, indent=2) + '\n')
        finally:
            pending.unlink(missing_ok=True)
        print('S44_EGL_PROBE=BUILT_NOT_EXECUTED')
        print('S44_EGL_PROBE_SHA256=' + sha(BINARY))


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('S44_EGL_PROBE_ERROR=' + str(exc)[:500], file=sys.stderr)
        raise SystemExit(2)
