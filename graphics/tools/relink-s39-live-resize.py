#!/data/data/com.termux/files/usr/bin/python3
"""Build the pinned S39 Xwayland without changing the running desktop.

Replay the combined S39 patch against the verified S38 window source.
Only xwayland-window.c is compiled; all other S38 objects, its pre-idle
Present replacement and the S36 AHB helpers remain unchanged. The output
is published atomically only after it matches the tested binary digest.
--dry-run validates inputs and the session without writing files.
"""
import argparse
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
S38 = STAGE / 's38-frame-continuity/runtime'
S36 = STAGE / 's36-ahb-output/runtime'
S38_OVERLAY = STAGE / 's38-frame-continuity/scratch/overlay'
OUT = STAGE / 's39-live-resize'
RUNTIME = OUT / 'runtime'
SOURCE_INPUT = S38_OVERLAY / 'hw/xwayland/xwayland-window.c'
SOURCE_OUTPUT = OUT / 'xwayland-window.final.c'
OBJECT_OUTPUT = RUNTIME / 'final/xwayland-window.s39.o'
BINARY_FINAL = RUNTIME / 'Xwayland.final'
BINARY_PUBLISHED = RUNTIME / 'Xwayland'
PATCH = ROOT / 'graphics/patches/xwayland/kgsl/0015-kgsl-live-resize-continuity.patch'

INPUT_SHA256 = 'f388c03ef6fd5d27ec5bd4e9f38d05ad5bdf6608a6a9ae74f874e3e1bba63bb4'
OUTPUT_SOURCE_SHA256 = '1a1d39c113999f06cf6a5471bf5c82503d1ee63fe54b78dd955314f539f4dec5'
PATCH_SHA256 = '6af957adb04199c8ec366f2a7cd0d96ab4919c08f2e3b65deddf51e72cc5ac58'
S38_SHA256 = '3f27886fae2bde560d34ca53dedacf4cec09fd9dbcddc591efafc4ec9bc994c2'
S36_SHA256 = 'df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5'
OUTPUT_BINARY_SHA256 = 'c5f6c9a512f84e31f60ec52a9f10e689e0746f831049e57a4223ade2750f3899'
REQUIRED_FLAGS = ['S38_KGSL_FRAME_LIFETIME=1', 'S39_KGSL_RESIZE_ORIGIN=1',
                  'S39_KGSL_KEEP_UNRELATED_ROOT_POOL=1']
S38_OBJECT_NAMES = ('xwayland-glamor-gbm', 'xwayland-screen', 'xwayland-present',
                    'present_execute')
S36_OBJECT_NAMES = ('s36_xwl_ahb_output', 'kgsl_ahb_fd', 'alroot-gpu-buffer-v1-protocol')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_pin(path, expected):
    if path.is_symlink() or not path.is_file() or digest(path) != expected:
        raise RuntimeError('pinned input mismatch: ' + str(path))


def session_status():
    text = subprocess.check_output(['awstart', '--status'], text=True, timeout=8)
    result = {key: value.strip() for line in text.splitlines()
              if ':' in line for key, value in [line.split(':', 1)]
              if key in ('xwayland', 'xfce', 'ime', 'clipboard')}
    if not {'xwayland', 'xfce'}.issubset(result):
        raise RuntimeError('could not identify current project Xwayland/XFCE state')
    return result


def refuse_running_s39():
    """Includes isolated S39 test servers, not only awstart's desktop PID."""
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            target = os.readlink(proc / 'exe')
        except OSError:
            continue
        target = target.removesuffix(' (deleted)')
        executable = Path(target)
        if executable.is_relative_to(RUNTIME) and executable.name.startswith('Xwayland'):
            raise RuntimeError('refusing to rebuild while S39 is running: PID ' + proc.name)


def atomic_copy(source, target):
    if target.is_symlink():
        raise RuntimeError('refusing symlink output: ' + str(target))
    staging = target.with_name(target.name + '.publish-' + str(os.getpid()))
    if staging.exists() or staging.is_symlink():
        raise RuntimeError('unexpected staging path: ' + str(staging))
    try:
        shutil.copyfile(source, staging)
        shutil.copymode(source, staging)
        os.replace(staging, target)
    finally:
        staging.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    model = subprocess.check_output(['/system/bin/getprop', 'ro.product.model'],
                                    text=True, timeout=3).strip()
    if not model.startswith('SM-S948'):
        raise RuntimeError('S39 validation is limited to S26 Ultra')
    pins = {SOURCE_INPUT: INPUT_SHA256, PATCH: PATCH_SHA256,
            S38 / 'Xwayland': S38_SHA256, S36 / 'Xwayland': S36_SHA256}
    for path, expected in pins.items():
        verify_pin(path, expected)
    protected = list(pins) + [S38 / (name + '.s38.o') for name in S38_OBJECT_NAMES]
    protected += [S36 / (name + '.s36.o') for name in S36_OBJECT_NAMES]
    for path in protected:
        if path.is_symlink() or not path.is_file():
            raise RuntimeError('missing or symlinked protected input: ' + str(path))
    protected_before = {str(path): digest(path) for path in protected}
    before = session_status()
    refuse_running_s39()

    commands = subprocess.check_output(
        ['ninja', '-C', str(BUILD), '-t', 'commands', 'hw/xwayland/Xwayland'],
        text=True, timeout=25).splitlines()
    link = next(shlex.split(c) for c in reversed(commands)
                if ' -o hw/xwayland/Xwayland ' in c)
    compile_commands = {}
    for name in ('xwayland-glamor-gbm.c', 'xwayland-screen.c',
                 'xwayland-present.c', 'xwayland-window.c'):
        original = '../source/hw/xwayland/' + name
        command = next(shlex.split(c) for c in commands
                       if original in c and ' -c ' in c)
        if original not in command:
            raise RuntimeError('unexpected compile source: ' + name)
        compile_commands[name] = command
    if args.dry_run:
        if session_status() != before:
            raise RuntimeError('user session changed during dry run')
        print('S39_BUILD_DRY_RUN=PASS')
        print('S39_BUILD_SOURCE=' + str(SOURCE_OUTPUT))
        print('S39_BUILD_OBJECT=' + str(OBJECT_OUTPUT))
        print('S39_BUILD_BINARY=' + str(BINARY_PUBLISHED))
        print('S39_BUILD_EXPECTED_SHA256=' + OUTPUT_BINARY_SHA256)
        print('S39_DRY_RUN_NO_FILES_WRITTEN=PASS')
        return

    replay = OUT / 'replay/hw/xwayland'
    for directory in (OUT, RUNTIME, OBJECT_OUTPUT.parent, replay, OUT / 'logs'):
        if directory.is_symlink():
            raise RuntimeError('refusing symlink build directory: ' + str(directory))
        directory.mkdir(parents=True, exist_ok=True)
    for target in (SOURCE_OUTPUT, OBJECT_OUTPUT, BINARY_FINAL, BINARY_PUBLISHED,
                   OUT / 'build-manifest.json'):
        if target.is_symlink():
            raise RuntimeError('refusing symlink output: ' + str(target))
    replay_source = replay / 'xwayland-window.c'
    if replay_source.is_symlink():
        raise RuntimeError('refusing symlink replay source')
    shutil.copyfile(SOURCE_INPUT, replay_source)
    with (OUT / 'logs/relink-s39.log').open('w') as log:
        subprocess.run(['patch', '--forward', '--batch', '--fuzz=0', '-p1', '-i', str(PATCH)],
                       cwd=OUT / 'replay', check=True, stdout=log,
                       stderr=subprocess.STDOUT, timeout=15)
        verify_pin(replay_source, OUTPUT_SOURCE_SHA256)
        atomic_copy(replay_source, SOURCE_OUTPUT)
        replacements = {}
        for name, command in compile_commands.items():
            command = command.copy()
            original_object = command[command.index('-o') + 1]
            if name == 'xwayland-window.c':
                # Preserve the exact source/object paths and compiler argument
                # order of the tested generic S39 final build.
                command[command.index('-c') + 1] = str(SOURCE_OUTPUT)
                command[command.index('-o') + 1] = str(OBJECT_OUTPUT)
                if '-MF' in command:
                    command[command.index('-MF') + 1] = str(OBJECT_OUTPUT) + '.d'
                command += ['-I' + str(S38_OVERLAY / 'present'),
                            '-I' + str(ROOT / 'graphics/include'),
                            '-I' + str(ROOT / 'native/waylandcomp/generated')]
                subprocess.run(command, cwd=BUILD, check=True, stdout=log,
                               stderr=subprocess.STDOUT, timeout=100)
                replacement = OBJECT_OUTPUT
            else:
                replacement = S38 / (Path(name).stem + '.s38.o')
            replacements[original_object] = str(replacement)
        refuse_running_s39()
        link[link.index('-o') + 1] = str(BINARY_FINAL)
        link = [replacements.get(arg, arg) for arg in link]
        # This object must precede libpresent's archive so the S38 pre-idle
        # hook replaces the archive member without editing that archive.
        link[1:1] = [str(S38 / 'present_execute.s38.o')]
        link += [str(S36 / (name + '.s36.o')) for name in S36_OBJECT_NAMES]
        link += ['-lEGL', '-lGLESv2']
        subprocess.run(link, cwd=BUILD, check=True, stdout=log,
                       stderr=subprocess.STDOUT, timeout=100)

    verify_pin(BINARY_FINAL, OUTPUT_BINARY_SHA256)
    verify_pin(SOURCE_OUTPUT, OUTPUT_SOURCE_SHA256)
    for path in protected:
        verify_pin(path, protected_before[str(path)])
    if session_status() != before:
        raise RuntimeError('user session changed; S39 output was not published')
    refuse_running_s39()
    atomic_copy(BINARY_FINAL, BINARY_PUBLISHED)
    verify_pin(BINARY_PUBLISHED, OUTPUT_BINARY_SHA256)
    receipt = {
        'binary': str(BINARY_PUBLISHED), 'binary_sha256': OUTPUT_BINARY_SHA256,
        'linked_binary': str(BINARY_FINAL), 'linked_binary_sha256': digest(BINARY_FINAL),
        'source': str(SOURCE_OUTPUT), 'source_sha256': OUTPUT_SOURCE_SHA256,
        'source_input_sha256': INPUT_SHA256, 'patch': str(PATCH), 'patch_sha256': PATCH_SHA256,
        'new_object': str(OBJECT_OUTPUT), 'new_object_sha256': digest(OBJECT_OUTPUT),
        'protected_inputs_sha256': protected_before, 'session_before': before,
        'session_unchanged': True, 'required_flags': REQUIRED_FLAGS,
        'diagnostic_trace_included': False,
        'scope': 'resize origin consistency and root output pool reuse; no vkcube event-loop fix',
    }
    manifest = OUT / 'build-manifest.json'
    staging = manifest.with_name(manifest.name + '.publish-' + str(os.getpid()))
    if staging.exists() or staging.is_symlink():
        raise RuntimeError('unexpected manifest staging path')
    staging.write_text(json.dumps(receipt, indent=2) + '\n')
    os.replace(staging, manifest)
    print('S39_PRIVATE_XWAYLAND_LINK=PASS')
    print('S39_PRIVATE_BINARY_SHA256=' + OUTPUT_BINARY_SHA256)
    print('S39_PROTECTED_INPUTS_AND_USER_SESSION_UNCHANGED=PASS')
    print('S39_PRIVATE_BINARY=' + str(BINARY_PUBLISHED))
    print('S39_MANIFEST=' + str(manifest))


if __name__ == '__main__':
    main()
