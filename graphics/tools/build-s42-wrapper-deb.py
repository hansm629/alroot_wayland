#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Build a fresh Termux Wrapper deb from the accepted S41 source overlay.

Build/stage/package only: this script never installs packages or restarts a
desktop. The clean lab checkout and previous private builds are read-only.
"""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PREFIX = Path('/data/data/com.termux/files/usr')
REF = ROOT.parent / 'alroot-vk-lab/vulkan-wrapper-android'
PRIVATE = ROOT / '.source/gpu-stage1/wrapper-meson'
OUT = ROOT / '.source/gpu-stage1/s42-default-graphics/wrapper'
SOURCE, BUILD, STAGE = OUT / 'source', OUT / 'build', OUT / 'stage'
PACKAGE, VERSION, ARCH = 'vulkan-wrapper-android', '26.3.0+alroot.s42.1', 'aarch64'
PIN = 'b3b32abb82af4d374fd35c7192a7f524a5fc8bd3'
PATCH = ROOT / 'graphics/patches/mesa/wrapper/0005-s41-ahb-padding-edge-copy.patch'
PATCH_SHA = 'a077c1aae0df79daef60ab782a64490dc62cd8d5d54af38419b60281e4525a6e'
COMMON_BEFORE = 'f132324c5f892c86e98a3cf4d68a752598455b49129a7ddc825c211ce3abf5d0'
COMMON_AFTER = 'dcffb2d95a8b493580856ae1ca0726d8a15996ad7f53530cfe7e85fa4af9af26'
X11_SHA = '49a887ba8fbd34fa40ad518086e82f89aef26b1c996863ba3d97823737ad39db'
S41_SHA = '21fdc1624cf35f957947e1e73813a6e3fbbcbb168eb18ce6bca2f96c0b03f11a'
DEPENDS = ('libandroid-shmem', 'libc++', 'libdrm', 'libwayland', 'libx11',
           'libxcb', 'libxshmfence', 'xcb-util-keysyms', 'libdisplay-info',
           'vulkan-loader-generic', 'zlib', 'zstd', 'libexpat')
PLATFORM_LIBS = {'libc.so', 'libdl.so', 'libm.so', 'libnativewindow.so',
                 'libhardware.so', 'liblog.so', 'libsync.so', 'libandroid.so'}
CONFIGURE = [
    '--prefix=' + str(PREFIX), '--libdir=lib', '--buildtype=debugoptimized',
    '--wrap-mode=nofallback', '-Dvulkan-drivers=wrapper', '-Dplatforms=x11,wayland',
    '-Dgallium-drivers=', '-Dglx=disabled', '-Degl=disabled', '-Dgbm=disabled',
    '-Dllvm=disabled', '-Dbuild-tests=false', '-Dgallium-va=disabled', '-Dvideo-codecs=',
    '-Dc_args=--target=aarch64-linux-android35', '-Dc_link_args=-landroid-shmem',
    '-Dcpp_link_args=-landroid-shmem',
]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def pinned(path, expected):
    if not path.is_file() or digest(path) != expected:
        raise RuntimeError('pinned input mismatch: ' + str(path))


def capture(argv, **kwargs):
    return subprocess.check_output(argv, text=True, timeout=30, **kwargs).strip()


def run(argv, log, env, cwd=None, timeout=1800):
    log.write('COMMAND ' + json.dumps([str(x) for x in argv]) + '\n'); log.flush()
    subprocess.run(argv, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                   check=True, timeout=timeout)


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')
    path.chmod(0o644)


def check_reference():
    if capture(['git', '-C', str(REF), 'rev-parse', 'HEAD']) != PIN:
        raise RuntimeError('wrapper reference is not at the accepted baseline')
    if capture(['git', '-C', str(REF), 'status', '--porcelain']):
        raise RuntimeError('wrapper reference has local changes')


def overlay_inputs():
    result = {}
    base = PRIVATE / 'source'
    # The private tree mirrors unchanged directories/files using symlinks.
    # Only real overlay files replace baseline files in the new archive tree.
    for directory, dirs, files in os.walk(base, followlinks=False):
        dirs[:] = [d for d in dirs if d not in ('.git', '__pycache__') and
                   not (Path(directory) / d).is_symlink()]
        for name in files:
            path = Path(directory) / name
            if path.is_symlink() or not path.is_file() or name.endswith(('.orig', '.rej', '.pyc')):
                continue
            result[str(path.relative_to(base))] = digest(path)
    if not result:
        raise RuntimeError('private wrapper overlay is empty')
    return result


def installed_snapshot():
    version = capture(['dpkg-query', '-W', '-f=${Version}', PACKAGE])
    files = capture(['dpkg-query', '-L', PACKAGE]).splitlines()
    return {'version': version, 'files': {name: digest(Path(name)) for name in files
                                        if Path(name).is_file()}}


def prepare_source(inputs, log, env):
    identity = {'baseline_commit': PIN, 'overlay_files': inputs, 's41_patch_sha256': PATCH_SHA}
    receipt = OUT / 'source-inputs.json'
    if SOURCE.exists():
        if SOURCE.is_symlink() or not receipt.is_file() or json.loads(receipt.read_text()) != identity:
            raise RuntimeError('S42 source already exists with different inputs; use a new build revision')
        pinned(SOURCE / 'src/vulkan/wsi/wsi_common.c', COMMON_AFTER)
        for name, expected in inputs.items():
            if name != 'src/vulkan/wsi/wsi_common.c':
                pinned(SOURCE / name, expected)
        return identity
    with tempfile.TemporaryDirectory(prefix='source-export-', dir=OUT) as temporary:
        temporary = Path(temporary)
        archive, tree = temporary / 'source.tar', temporary / 'tree'
        tree.mkdir()
        run(['git', '-C', str(REF), 'archive', '--format=tar', '-o', str(archive), PIN], log, env)
        # This archive is produced from the verified local Git commit. The
        # data filter additionally excludes paths escaping the export root.
        with tarfile.open(archive) as source_tar:
            source_tar.extractall(tree, filter='data')
        for name in sorted(inputs):
            original, target = PRIVATE / 'source' / name, tree / name
            pinned(original, inputs[name])
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.is_symlink():
                target.unlink()
            shutil.copy2(original, target)
        pinned(tree / 'src/vulkan/wsi/wsi_common.c', COMMON_BEFORE)
        run(['patch', '--batch', '--forward', '--fuzz=0', '-p1', '-i', str(PATCH)], log, env, cwd=tree)
        pinned(tree / 'src/vulkan/wsi/wsi_common.c', COMMON_AFTER)
        pinned(tree / 'src/vulkan/wsi/wsi_common_x11.c', X11_SHA)
        if not (tree / 'src/vulkan/wsi/alroot-gpu-buffer-v1-protocol.c').is_file():
            raise RuntimeError('Wayland AHB protocol source is missing')
        os.rename(tree, SOURCE)
    write_json(receipt, identity)
    return identity


def check_dynamic(library, readelf):
    text = capture([readelf, '-d', str(library)])
    needed = re.findall(r'\(NEEDED\).*\[([^]]+)\]', text)
    paths = re.findall(r'\((?:RUNPATH|RPATH)\).*\[([^]]*)\]', text)
    if not needed or 'libnativewindow.so' not in needed or 'libwayland-client.so' not in needed:
        raise RuntimeError('packaged wrapper has unexpected runtime dependencies')
    for group in paths:
        for entry in group.split(':'):
            expanded = entry.replace('${ORIGIN}', str(PREFIX / 'lib')).replace('$ORIGIN', str(PREFIX / 'lib'))
            if not expanded or not Path(expanded).is_absolute() or Path(expanded).resolve() != PREFIX / 'lib':
                raise RuntimeError('noncanonical or build-only wrapper RUNPATH: ' + entry)
    if 'android_stub' in text or str(OUT) in text or str(PRIVATE) in text:
        raise RuntimeError('build path leaked into the packaged ELF dynamic section')
    owners = {}
    for soname in needed:
        if soname in PLATFORM_LIBS:
            continue
        path = PREFIX / 'lib' / soname
        if not path.is_file():
            raise RuntimeError('runtime dependency is missing: ' + str(path))
        owner_lines = capture(['dpkg-query', '-S', str(path)]).splitlines()
        owner_names = {line.split(': ', 1)[0].split(':', 1)[0] for line in owner_lines if ': ' in line}
        if not owner_names or not owner_names.issubset(set(DEPENDS)):
            raise RuntimeError('runtime dependency is absent from Depends: ' + soname + ' ' + repr(owner_names))
        owners[soname] = sorted(owner_names)
    return text, needed, owners


def package(library, source_identity, dynamic, needed, owners, log, env, readelf):
    pkgroot = OUT / 'deb-root'
    if pkgroot.is_symlink():
        raise RuntimeError('package root is a symlink')
    if pkgroot.exists():
        shutil.rmtree(pkgroot)
    installed = pkgroot / PREFIX.relative_to('/')
    control_dir = pkgroot / 'DEBIAN'
    doc = installed / 'share/doc' / PACKAGE
    icd = installed / 'share/vulkan/icd.d/wrapper_icd.aarch64.json'
    lib = installed / 'lib/libvulkan_wrapper.so'
    for directory in (control_dir, doc, icd.parent, lib.parent):
        directory.mkdir(parents=True, exist_ok=True)
    shutil.copy2(library, lib); lib.chmod(0o755)
    write_json(icd, {'file_format_version': '1.0.1', 'ICD': {
        'library_path': str(PREFIX / 'lib/libvulkan_wrapper.so'), 'api_version': '1.3.359'}})
    shutil.copy2(SOURCE / 'docs/license.rst', doc / 'LICENSE'); (doc / 'LICENSE').chmod(0o644)
    provenance = {
        'package': PACKAGE, 'package_version': VERSION, 'version': VERSION, 'architecture': ARCH,
        'baseline_commit': PIN, 'source_inputs': source_identity,
        'accepted_private_wrapper_sha256': S41_SHA, 'wsi_common_sha256': COMMON_AFTER,
        'library_sha256': digest(lib), 'icd_sha256': digest(icd),
        'needed': needed, 'dependency_owners': owners, 'configure_options': CONFIGURE,
        'source_policy': 'Accepted S41 overlay; no additional legacy lab performance experiments merged',
    }
    write_json(doc / 'alroot-build.json', provenance)
    size = (sum(p.stat().st_size for p in installed.rglob('*') if p.is_file()) + 1023) // 1024
    control = '\n'.join([
        'Package: ' + PACKAGE, 'Version: ' + VERSION, 'Architecture: ' + ARCH,
        'Maintainer: Local Termux Build', 'Installed-Size: ' + str(size),
        'Section: x11', 'Priority: optional', 'Homepage: https://github.com/lfdevs/mesa-for-android-container',
        'Depends: ' + ', '.join(DEPENDS), 'Provides: vulkan-icd',
        'Description: Android Vulkan Wrapper with Alroot Wayland and XCB AHB WSI',
        ' Fresh native build of the accepted S41 sources, including Wayland AHB',
        ' presentation, bounded XCB import recovery, and initialized physical',
        ' AHB padding rows. Installs the canonical Termux Wrapper ICD.', '',
    ])
    (control_dir / 'control').write_text(control); (control_dir / 'control').chmod(0o644)
    md5s = []
    for file in sorted(installed.rglob('*')):
        if file.is_file():
            md5s.append(hashlib.md5(file.read_bytes()).hexdigest() + '  ' + str(file.relative_to(pkgroot)))
    (control_dir / 'md5sums').write_text('\n'.join(md5s) + '\n'); (control_dir / 'md5sums').chmod(0o644)
    for directory in [pkgroot] + [p for p in pkgroot.rglob('*') if p.is_dir()]:
        directory.chmod(0o755)
    # Only these four owned payload files enter the package. In particular,
    # Android stubs and Mesa's shared drirc defaults cannot enter it.
    payload = sorted(str(p.relative_to(installed)) for p in installed.rglob('*') if p.is_file())
    expected = sorted(['lib/libvulkan_wrapper.so', 'share/vulkan/icd.d/wrapper_icd.aarch64.json',
                       'share/doc/' + PACKAGE + '/LICENSE', 'share/doc/' + PACKAGE + '/alroot-build.json'])
    if payload != expected:
        raise RuntimeError('unexpected package payload')
    packages = OUT / 'packages'; packages.mkdir(exist_ok=True)
    deb = packages / f'{PACKAGE}_{VERSION}_{ARCH}.deb'
    pending = deb.with_suffix('.deb.pending')
    run(['dpkg-deb', '--root-owner-group', '--build', str(pkgroot), str(pending)], log, env)
    if capture(['dpkg-deb', '-f', str(pending), 'Package']) != PACKAGE or capture(['dpkg-deb', '-f', str(pending), 'Version']) != VERSION:
        raise RuntimeError('built package identity mismatch')
    os.replace(pending, deb)
    runtime = OUT / 'runtime'; runtime.mkdir(exist_ok=True)
    runtime_lib = runtime / 'libvulkan_wrapper.so'
    # Atomic copy keeps any already-loaded validation inode intact.
    pending_lib = runtime / 'libvulkan_wrapper.so.pending'
    shutil.copy2(lib, pending_lib); os.replace(pending_lib, runtime_lib)
    write_json(runtime / 'wrapper_icd.private.json', {'file_format_version': '1.0.1', 'ICD': {
        'library_path': str(runtime_lib), 'api_version': '1.3.359'}})
    return deb, provenance, runtime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--prepare-only', action='store_true', help='export source and configure the fresh build only')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error('--jobs must be 1..4')
    if Path(os.environ.get('PREFIX', '')).resolve() != PREFIX or capture(['dpkg', '--print-architecture']) != ARCH:
        raise RuntimeError('this builder requires native S26U aarch64 Termux')
    check_reference()
    pinned(PATCH, PATCH_SHA)
    pinned(PRIVATE / 'source/src/vulkan/wsi/wsi_common.c', COMMON_BEFORE)
    pinned(PRIVATE / 'source/src/vulkan/wsi/wsi_common_x11.c', X11_SHA)
    pinned(ROOT / '.source/gpu-stage1/s41-ahb-padding/runtime/libvulkan_wrapper.so', S41_SHA)
    meson = PRIVATE / 'meson'
    readelf = shutil.which('llvm-readelf') or shutil.which('readelf')
    if not meson.is_file() or not os.access(meson, os.X_OK) or not readelf:
        raise RuntimeError('existing private Meson and readelf are required')
    if OUT.is_symlink():
        raise RuntimeError('S42 output directory is a symlink')
    OUT.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    for name in ('CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'LD_PRELOAD', 'LD_LIBRARY_PATH'):
        env.pop(name, None)
    env.update(CC='cc', CXX='c++', MESA_GIT_SHA1_OVERRIDE=PIN)
    inputs, before = overlay_inputs(), installed_snapshot()
    with (OUT / 'build.lock').open('a') as lock, (OUT / 'build.log').open('w') as log:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        identity = prepare_source(inputs, log, env)
        setup_receipt = OUT / 'configure.json'
        if not (BUILD / 'build.ninja').is_file():
            run([str(meson), 'setup', str(BUILD), str(SOURCE), *CONFIGURE], log, env)
            write_json(setup_receipt, {'options': CONFIGURE, 'git_sha1_override': PIN})
        elif not setup_receipt.is_file() or json.loads(setup_receipt.read_text()) != {'options': CONFIGURE, 'git_sha1_override': PIN}:
            raise RuntimeError('existing S42 build uses a different configuration')
        if args.prepare_only:
            print('S42_WRAPPER=SOURCE_AND_BUILD_PREPARED'); print('S42_BUILD=' + str(BUILD)); return
        run(['ninja', '-C', str(BUILD), '-j', str(args.jobs),
             'src/vulkan/wrapper/libvulkan_wrapper.so',
             'src/vulkan/wrapper/wrapper_icd.aarch64.json'], log, env)
        git_header = BUILD / 'src/git_sha1.h'
        if '(git-' + PIN[:10] + ')' not in git_header.read_text():
            raise RuntimeError('Mesa build recorded the wrong source repository')
        if STAGE.is_symlink():
            raise RuntimeError('staging directory is a symlink')
        if STAGE.exists():
            shutil.rmtree(STAGE)
        # DESTDIR is private. Meson's install step removes the uninstalled
        # target's Android-stub RUNPATH; only a strict whitelist is packaged.
        run([str(meson), 'install', '-C', str(BUILD), '--destdir', str(STAGE), '--no-rebuild', '--strip'], log, env)
        library = STAGE / PREFIX.relative_to('/') / 'lib/libvulkan_wrapper.so'
        dynamic, needed, owners = check_dynamic(library, readelf)
        (OUT / 'staged-dynamic.txt').write_text(dynamic + '\n')
        deb, provenance, runtime = package(library, identity, dynamic, needed, owners, log, env, readelf)
        smoke_env = env.copy()
        for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'VK_DRIVER_FILES', 'VK_ICD_FILENAMES', 'LIBGL_ALWAYS_SOFTWARE',
                     'MESA_LOADER_DRIVER_OVERRIDE', 'GALLIUM_DRIVER', 'LIBGL_DRIVERS_PATH'):
            smoke_env.pop(name, None)
        smoke_env.update(VK_DRIVER_FILES=str(runtime / 'wrapper_icd.private.json'),
                         VK_ICD_FILENAMES=str(runtime / 'wrapper_icd.private.json'))
        with (OUT / 'vulkaninfo-summary.log').open('w') as smoke:
            run(['vulkaninfo', '--summary'], smoke, smoke_env, timeout=25)
        summary = (OUT / 'vulkaninfo-summary.log').read_text()
        if not re.search(r'deviceName\s*=\s*Adreno', summary):
            raise RuntimeError('staged Wrapper did not enumerate the real Adreno GPU')
        check_reference()
        if overlay_inputs() != inputs or installed_snapshot() != before:
            raise RuntimeError('reference overlay or installed Wrapper changed during build')
        result = {'built_at_utc': datetime.now(timezone.utc).isoformat(), 'package': str(deb),
                  'package_sha256': digest(deb), 'provenance': provenance,
                  'runtime_icd': str(runtime / 'wrapper_icd.private.json'),
                  'installed_wrapper_unchanged': True, 'reference_unchanged': True,
                  'staged_real_gpu_enumeration': 'PASS', 'desktop_validation': 'REQUIRED_BEFORE_PROMOTION'}
        write_json(OUT / 'build-manifest.json', result)
        print('S42_WRAPPER_DEB=BUILT')
        print('S42_WRAPPER_PACKAGE=' + str(deb))
        print('S42_WRAPPER_LIBRARY_SHA256=' + provenance['library_sha256'])
        print('S42_WRAPPER_ICD_SHA256=' + provenance['icd_sha256'])
        print('S42_WRAPPER_TEST_ICD=' + str(runtime / 'wrapper_icd.private.json'))


if __name__ == '__main__':
    main()
