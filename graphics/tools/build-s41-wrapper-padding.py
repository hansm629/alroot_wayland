#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Relink the pinned private S26 wrapper with initialized AHB padding rows.

The shared Mesa source, thin archive, S26 runtime and installed drivers are
read-only inputs. Only the private S41 copy and runtime are written.
"""
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / '.source/gpu-stage1'
BUILD = BASE / 'wrapper-meson/build'
ORIGINAL = BASE / 'wrapper-meson/source/src/vulkan/wsi/wsi_common.c'
OUT = BASE / 's41-ahb-padding'
PATCH = ROOT / 'graphics/patches/mesa/wrapper/0005-s41-ahb-padding-edge-copy.patch'
S26 = BASE / 's26-wrapper-resize-retry/runtime'
SOURCE_SHA = 'f132324c5f892c86e98a3cf4d68a752598455b49129a7ddc825c211ce3abf5d0'
CANDIDATE_SHA = 'dcffb2d95a8b493580856ae1ca0726d8a15996ad7f53530cfe7e85fa4af9af26'
PATCH_SHA = 'a077c1aae0df79daef60ab782a64490dc62cd8d5d54af38419b60281e4525a6e'
S26_SHA = '5fc1053bfeb549d31f0f748581472bb82d776d5e0ac08e0905b0fdc5fc7fbcf1'
REFERENCE_BINARY_SHA = '21fdc1624cf35f957947e1e73813a6e3fbbcbb168eb18ce6bca2f96c0b03f11a'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pinned(path, expected):
    if path.is_symlink() or not path.is_file() or digest(path) != expected:
        raise RuntimeError('pinned input mismatch: ' + str(path))


def assert_not_mapped(path):
    target = str(path.resolve())
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if proc.stat().st_uid != os.getuid():
                continue
            maps = (proc / 'maps').read_text()
        except (FileNotFoundError, ProcessLookupError):
            continue
        if any(line.split(maxsplit=5)[-1].removesuffix(' (deleted)') == target
               for line in maps.splitlines()):
            raise RuntimeError('S41 wrapper is in use; close its client first: pid=' + proc.name)


def atomic_write(path, data, mode=0o644):
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + '.', delete=False) as f:
        stage = Path(f.name)
        try:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
            stage.chmod(mode)
            os.replace(stage, path)
        finally:
            stage.unlink(missing_ok=True)


def main():
    pinned(ORIGINAL, SOURCE_SHA)
    pinned(PATCH, PATCH_SHA)
    pinned(S26 / 'libvulkan_wrapper.so', S26_SHA)
    pinned(BUILD / 'src/vulkan/wrapper/libvulkan_wrapper.so', S26_SHA)
    pinned(ORIGINAL.with_name('wsi_common_x11.c'),
           '49a887ba8fbd34fa40ad518086e82f89aef26b1c996863ba3d97823737ad39db')
    pinned(BUILD / 'src/vulkan/wsi/libvulkan_wsi.a.p/wsi_common.c.o',
           '1469a36f611642794addea46c972654d565205a6985d7c7dc55742c5ded6f3d5')
    pinned(BUILD / 'src/vulkan/wsi/libvulkan_wsi.a.p/wsi_common_x11.c.o',
           'd37a0a612d056c2d945b65e9447e325ee743608dc8f01acf16d18d261824f9e8')
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / 'build.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        runtime = OUT / 'runtime'
        runtime.mkdir(exist_ok=True)
        target = runtime / 'libvulkan_wrapper.so'
        assert_not_mapped(target)
        source_root = OUT / 'source'
        source = source_root / 'src/vulkan/wsi/wsi_common.c'
        source.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ORIGINAL, source)
        subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(PATCH)],
                       cwd=source_root, check=True, timeout=15)
        pinned(source, CANDIDATE_SHA)
        obj = OUT / 'object/wsi_common.c.o'
        obj.parent.mkdir(exist_ok=True)
        archive = OUT / 'libvulkan_wsi.a'
        original_archive = BUILD / 'src/vulkan/wsi/libvulkan_wsi.a'
        commands = subprocess.check_output(
            ['ninja', '-C', str(BUILD), '-t', 'commands', 'src/vulkan/wrapper/libvulkan_wrapper.so'],
            text=True, timeout=15).splitlines()
        compile_cmd = next(shlex.split(c) for c in commands
                           if '../source/src/vulkan/wsi/wsi_common.c' in c and ' -c ' in c)
        compile_cmd[compile_cmd.index('-c') + 1] = str(source)
        compile_cmd[compile_cmd.index('-o') + 1] = str(obj)
        for flag in ('-MF', '-MQ'):
            if flag in compile_cmd:
                compile_cmd[compile_cmd.index(flag) + 1] = str(obj) + ('.d' if flag == '-MF' else '')
        link = next(shlex.split(c) for c in reversed(commands)
                    if ' -o src/vulkan/wrapper/libvulkan_wrapper.so ' in c)
        inputs = {ORIGINAL, ORIGINAL.with_name('wsi_common_x11.c'), original_archive,
                  S26 / 'libvulkan_wrapper.so', S26 / 'wrapper_icd.private.json',
                  BUILD / 'src/vulkan/wrapper/libvulkan_wrapper.so'}
        inputs.update((BUILD / arg).resolve(strict=True) for arg in link
                      if arg.endswith(('.a', '.o', '.so')) and (BUILD / arg).is_file())
        # Hash the thin archive's actual members too: the archive itself only
        # contains paths, so hashing it alone would not protect its inputs.
        members = subprocess.check_output(['ar', 't', str(original_archive)], text=True, timeout=15).splitlines()
        for member in members:
            path = Path(member)
            if not path.is_absolute():
                path = original_archive.parent / path
            inputs.add(path.resolve(strict=True))
        before = {str(path): digest(path) for path in inputs}
        candidate = OUT / 'libvulkan_wrapper.pending.so'
        link[link.index('-o') + 1] = str(candidate)
        if link.count('src/vulkan/wsi/libvulkan_wsi.a') != 1:
            raise RuntimeError('unexpected WSI link structure')
        link = [str(archive) if arg == 'src/vulkan/wsi/libvulkan_wsi.a' else arg for arg in link]
        with (OUT / 'build.log').open('w') as log:
            subprocess.run(['sha256sum', '-c', str(ROOT / '.source/gpu-audit/termux-x11-protected.sha256')],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
            subprocess.run(compile_cmd, cwd=BUILD, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=120)
            archive.unlink(missing_ok=True)
            # Materialize the thin archive before replacing exactly one member.
            mri = f'CREATE {archive}\nADDLIB {original_archive}\nSAVE\nEND\n'
            subprocess.run(['ar', '-M'], input=mri, text=True, stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=30)
            subprocess.run(['ar', 'r', str(archive), str(obj)], stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=30)
            names = subprocess.check_output(['ar', 't', str(archive)], text=True, timeout=15).splitlines()
            if names.count('wsi_common.c.o') != 1:
                raise RuntimeError('private archive must contain exactly one WSI common implementation')
            subprocess.run(link, cwd=BUILD, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=120)
            subprocess.run(['sha256sum', '-c', str(ROOT / '.source/gpu-audit/termux-x11-protected.sha256')],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
        for path, expected in before.items():
            pinned(Path(path), expected)
        if REFERENCE_BINARY_SHA is not None:
            pinned(candidate, REFERENCE_BINARY_SHA)
        candidate_sha = digest(candidate)
        data = json.loads((S26 / 'wrapper_icd.private.json').read_text())
        data['ICD']['library_path'] = str(target)
        assert_not_mapped(target)
        atomic_write(target, candidate.read_bytes(), 0o755)
        atomic_write(runtime / 'wrapper_icd.private.json', (json.dumps(data, indent=2) + '\n').encode())
        receipt = {
            'built_at': datetime.now(timezone.utc).isoformat(),
            'source_sha256': CANDIDATE_SHA, 'patch_sha256': PATCH_SHA,
            'baseline_wrapper_sha256': S26_SHA, 'binary_sha256': candidate_sha,
            'binary': str(target), 'icd': str(runtime / 'wrapper_icd.private.json'),
            'compile': compile_cmd, 'link': link, 'unchanged_inputs': before,
            'protected_drivers': 'PASS',
            'scope': 'initialize physical Android AHB padding in the existing GPU image copy',
        }
        atomic_write(OUT / 'build-manifest.json', (json.dumps(receipt, indent=2, sort_keys=True) + '\n').encode())
        print('S41_PRIVATE_WRAPPER=BUILT')
        print('S41_WRAPPER_SHA256=' + candidate_sha)
        print('S41_ICD=' + str(runtime / 'wrapper_icd.private.json'))
        print('S41_SOURCE_S26_SYSTEM_INPUTS=UNCHANGED')


if __name__ == '__main__':
    main()
