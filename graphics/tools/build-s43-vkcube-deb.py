#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Build the S43 vkcube AUTO-WSI correction as a vulkan-tools-only upgrade.

The verified S42 deb is retained for rollback and supplies every unchanged
payload/control member. This script never installs or alters installed files.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PREFIX = Path('/data/data/com.termux/files/usr')
PACKAGE, ARCH = 'vulkan-tools', 'aarch64'
BASE_VERSION, VERSION = '1.4.365+alroot.s42.1', '1.4.365+alroot.s42.2'
S42 = ROOT / '.source/gpu-stage1/s42-default-graphics/demos'
BASE_DEB = S42 / 'packages' / f'{PACKAGE}_{BASE_VERSION}_{ARCH}.deb'
BASE_DEB_SHA = 'c6926d52fea7b199d4c2263a34e0abe17972bcf76949c5bc3ace3554f4610dcc'
BASE_ELF_SHA = 'ae4f3ee457da41c25793459cb567a1cc941dc8e25260947d6421cb802f01fe6c'
BASE_SOURCE_SHA = 'b396d52524270fce6791bbc32648eb7ac05104986f0dcce3e264399b18503059'
SOURCE_SHA = 'bf7991b13c280820cd48566150382ca9ddf049ab219d1d4bd0c28fb32087bf47'
PATCH = ROOT / 'graphics/patches/vulkan-tools/1.4.365/0005-vkcube-prefer-x11-session.patch'
PATCH_SHA = 'a3c2db3eebf9e0c9be86dfbe9a7f75cbc2771445669fd3013fbfbf06ebaf75bc'
REF = S42 / 'source/vulkan-tools'
OUT = ROOT / '.source/gpu-stage1/s43-vkcube-default-wsi'
SOURCE, BUILD, STAGE = OUT / 'source', OUT / 'build', OUT / 'stage'
BIN = PREFIX.relative_to('/') / 'bin/vkcube'
DOC = PREFIX.relative_to('/') / 'share/doc/vulkan-tools/alroot-build.json'
CMAKE_OPTIONS = ['-G', 'Ninja', '-DCMAKE_SYSTEM_NAME=Linux', '-DCMAKE_BUILD_TYPE=Release',
                 '-DBUILD_CUBE=ON', '-DBUILD_VULKANINFO=OFF', '-DBUILD_ICD=OFF', '-DBUILD_TESTS=OFF',
                 '-DBUILD_WSI_XCB_SUPPORT=ON', '-DBUILD_WSI_XLIB_SUPPORT=ON',
                 '-DBUILD_WSI_WAYLAND_SUPPORT=ON', '-DBUILD_WSI_DISPLAY_SUPPORT=ON',
                 '-DVULKAN_HEADERS_INSTALL_DIR=' + str(PREFIX)]


def sha(path, algorithm='sha256'):
    result = hashlib.new(algorithm)
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''): result.update(block)
    return result.hexdigest()


def pinned(path, expected):
    if path.is_symlink() or not path.is_file() or sha(path) != expected:
        raise RuntimeError('pinned input/output mismatch: ' + str(path))


def capture(argv):
    return subprocess.check_output([str(x) for x in argv], text=True, timeout=60).strip()


def run(argv, log, env, cwd=ROOT, timeout=1200):
    argv = [str(x) for x in argv]
    log.write('COMMAND ' + json.dumps(argv) + '\n'); log.flush()
    subprocess.run(argv, env=env, cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
                   check=True, timeout=timeout)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.pending')
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
    temporary.chmod(0o644); os.replace(temporary, path)


def control(text):
    fields, key = {}, None
    for line in text.splitlines():
        if line.startswith((' ', '\t')) and key: fields[key] += '\n' + line
        elif ': ' in line: key, value = line.split(': ', 1); fields[key] = value
        elif line: raise RuntimeError('unexpected Debian control syntax')
    return fields


def entry(path):
    status = path.lstat()
    data = {'mode': stat.S_IMODE(status.st_mode)}
    if stat.S_ISLNK(status.st_mode): data.update(type='symlink', target=os.readlink(path))
    elif stat.S_ISREG(status.st_mode): data.update(type='file', size=status.st_size, sha256=sha(path))
    elif stat.S_ISDIR(status.st_mode): data['type'] = 'directory'
    else: raise RuntimeError('unsupported filesystem object: ' + str(path))
    return data


def inventory(root, payload=False):
    return {str(p.relative_to(root)): entry(p) for p in sorted(root.rglob('*'))
            if not payload or p.relative_to(root).parts[0] != 'DEBIAN'}


def installed_snapshot():
    if capture(['dpkg', '--audit']) or capture(['dpkg', '--verify', PACKAGE]):
        raise RuntimeError('installed package database or vulkan-tools verification is not clean')
    fields = control(capture(['dpkg-query', '-s', PACKAGE]))
    if (fields.pop('Status', None) != 'install ok installed' or fields.get('Package') != PACKAGE
            or fields.get('Version') != BASE_VERSION or fields.get('Architecture') != ARCH):
        raise RuntimeError('requires the accepted installed S42 vulkan-tools package')
    files = {}
    for name in capture(['dpkg-query', '-L', PACKAGE]).splitlines():
        path = Path(name)
        if not path.is_absolute() or '..' in path.parts: raise RuntimeError('unsafe installed package path')
        data = entry(path)
        if data['type'] == 'directory':
            if not path.is_relative_to(PREFIX) and path not in PREFIX.parents:
                raise RuntimeError('unexpected package directory outside the native prefix')
        else:
            if not path.is_relative_to(PREFIX): raise RuntimeError('package payload outside the native prefix')
            files[str(path.relative_to('/'))] = data
    info = PREFIX / 'var/lib/dpkg/info'
    metadata = sorted(set(info.glob(PACKAGE + '.*')) | set(info.glob(PACKAGE + ':*.*')))
    return {'control': fields, 'files': files, 'dpkg_info': {p.name: entry(p) for p in metadata}}


def prepare_source(log, env):
    pinned(REF / 'cube/cube.c', BASE_SOURCE_SHA); pinned(PATCH, PATCH_SHA)
    reference = inventory(REF)
    for name, data in reference.items():
        if data['type'] == 'symlink':
            path = REF / name
            if Path(data['target']).is_absolute() or not path.resolve(strict=True).is_relative_to(REF):
                raise RuntimeError('source symlink escapes its independent tree: ' + name)
    if SOURCE.is_symlink(): raise RuntimeError('source directory is a symlink')
    if not SOURCE.exists():
        shutil.copytree(REF, SOURCE, symlinks=True)
        run(['patch', '--batch', '--forward', '--fuzz=0', '-p1', '-i', PATCH], log, env, cwd=SOURCE)
    pinned(SOURCE / 'cube/cube.c', SOURCE_SHA)
    candidate = inventory(SOURCE)
    changed = {name for name in reference.keys() | candidate.keys() if reference.get(name) != candidate.get(name)}
    if changed != {'cube/cube.c'}: raise RuntimeError('source changes exceed the one pinned cube.c patch')
    receipt = {'baseline_cube_sha256': BASE_SOURCE_SHA, 'cube_sha256': SOURCE_SHA,
               'patch': str(PATCH.relative_to(ROOT)), 'patch_sha256': PATCH_SHA,
               'reference_files': reference, 'cmake_options': CMAKE_OPTIONS,
               'builder_sha256': sha(Path(__file__).resolve())}
    write_json(OUT / 'source-inputs.json', receipt)
    return reference, receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4: parser.error('--jobs must be 1..4')
    if Path(os.environ.get('PREFIX', '')) != PREFIX or capture(['dpkg', '--print-architecture']) != ARCH:
        raise RuntimeError('requires native S26U aarch64 Termux')
    for path in (OUT, SOURCE, BUILD, STAGE, OUT / 'rollback', OUT / 'packages'):
        if path.is_symlink(): raise RuntimeError('generated directory is a symlink: ' + str(path))
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / 'build.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        before = installed_snapshot()
        pinned(BASE_DEB, BASE_DEB_SHA); pinned(PREFIX / 'bin/vkcube', BASE_ELF_SHA)
        rollback = OUT / 'rollback' / BASE_DEB.name
        rollback.parent.mkdir(parents=True, exist_ok=True)
        if not rollback.exists(): shutil.copy2(BASE_DEB, rollback)
        pinned(rollback, BASE_DEB_SHA); rollback.chmod(0o644)
        if STAGE.exists(): shutil.rmtree(STAGE)
        env = os.environ.copy()
        for key in ('LD_PRELOAD', 'LD_LIBRARY_PATH', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS',
                    'CC', 'CXX', 'CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH',
                    'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR', 'PKG_CONFIG_SYSROOT_DIR'):
            env.pop(key, None)
        env.update(LC_ALL='C', DPKG_DEB_THREADS_MAX=str(args.jobs), CMAKE_PREFIX_PATH=str(PREFIX))
        with (OUT / 'build.log').open('w') as log:
            run(['dpkg-deb', '--raw-extract', rollback, STAGE], log, env)
            baseline = inventory(STAGE, payload=True)
            if {n: d for n, d in baseline.items() if d['type'] != 'directory'} != before['files']:
                raise RuntimeError('rollback archive payload differs from the installed S42 package')
            fields = control((STAGE / 'DEBIAN/control').read_text())
            if fields != before['control']: raise RuntimeError('rollback archive control differs from installed S42')
            control_before = inventory(STAGE / 'DEBIAN')
            old_provenance = json.loads((STAGE / DOC).read_text())
            if (old_provenance.get('package_version') != BASE_VERSION or
                    old_provenance.get('binary_sha256') != BASE_ELF_SHA or
                    old_provenance.get('source_sha256') != BASE_SOURCE_SHA):
                raise RuntimeError('S42 provenance does not identify the accepted baseline')
            epoch = int((STAGE / BIN).stat().st_mtime)
            env['SOURCE_DATE_EPOCH'] = str(epoch)
            shim = PREFIX / 'lib/libtermux-exec.so'
            if (not shim.resolve(strict=True).is_relative_to(PREFIX / 'lib') or
                    capture(['dpkg-query', '-S', shim]).split(': ', 1)[0] != 'termux-exec'):
                raise RuntimeError('build exec shim is not owned by termux-exec in the native prefix')
            env['LD_PRELOAD'] = str(shim)
            reference, source_receipt = prepare_source(log, env)
            run(['cmake', '-S', SOURCE, '-B', BUILD, *CMAKE_OPTIONS], log, env)
            run(['cmake', '--build', BUILD, '--target', 'vkcube', '--parallel', args.jobs], log, env)
            binary = BUILD / 'cube/vkcube'
            runtime_env = env.copy(); runtime_env.pop('LD_PRELOAD', None)
            help_result = subprocess.run([str(binary), '--help'], env=runtime_env, capture_output=True, text=True, timeout=20)
            help_text = help_result.stdout + help_result.stderr
            if help_result.returncode not in (0, 1) or any(w not in help_text.lower() for w in ('xcb', 'xlib', 'wayland', 'display')):
                raise RuntimeError('new vkcube lost a baseline WSI backend: ' + help_text)
            (OUT / 'vkcube-help.txt').write_text(help_text)
            shutil.copy2(binary, STAGE / BIN); (STAGE / BIN).chmod(0o755)
            provenance = dict(old_provenance)
            provenance.update(package=PACKAGE, package_version=VERSION, binary_path=str(PREFIX / 'bin/vkcube'),
                binary_sha256=sha(STAGE / BIN), source_sha256=SOURCE_SHA, baseline_source_sha256=BASE_SOURCE_SHA,
                previous_package_version=BASE_VERSION, previous_binary_sha256=BASE_ELF_SHA,
                previous_provenance_sha256=baseline[str(DOC)]['sha256'], rollback_package_sha256=BASE_DEB_SHA,
                patch=str(PATCH.relative_to(ROOT)), patch_sha256=PATCH_SHA, cmake_options=CMAKE_OPTIONS,
                builder_sha256=source_receipt['builder_sha256'], source_date_epoch=epoch,
                build_exec_shim_sha256=sha(shim),
                scope='AUTO uses X11 in an explicit X11 session; accepted resize handling and explicit WSI selection preserved')
            write_json(STAGE / DOC, provenance)
            fields['Version'] = VERSION
            files = [p for p in STAGE.rglob('*') if p.is_file() and not p.is_symlink()
                     and not p.is_relative_to(STAGE / 'DEBIAN')]
            fields['Installed-Size'] = str((sum(p.stat().st_size for p in files) + 1023) // 1024)
            (STAGE / 'DEBIAN/control').write_text(''.join(k + ': ' + v + '\n' for k, v in fields.items()))
            (STAGE / 'DEBIAN/md5sums').write_text(''.join(sha(p, 'md5') + '  ' + str(p.relative_to(STAGE)) + '\n' for p in sorted(files)))
            # Payload directory modes belong to the archived S42 package; do not
            # broaden this hotfix by normalizing shared-prefix directory modes.
            for directory in [STAGE, STAGE / 'DEBIAN',
                              *[p for p in (STAGE / 'DEBIAN').rglob('*')
                                if p.is_dir() and not p.is_symlink()]]:
                directory.chmod(0o755)
            for path in (STAGE / DOC, STAGE / 'DEBIAN/control', STAGE / 'DEBIAN/md5sums'): path.chmod(0o644)
            candidate = inventory(STAGE, payload=True)
            changed = {n for n in baseline.keys() | candidate.keys() if baseline.get(n) != candidate.get(n)}
            if changed != {str(BIN), str(DOC)}: raise RuntimeError('unexpected package payload changes: ' + repr(changed))
            control_after = inventory(STAGE / 'DEBIAN')
            if {n for n in control_before.keys() | control_after.keys() if control_before.get(n) != control_after.get(n)} != {'control', 'md5sums'}:
                raise RuntimeError('unexpected changes to archived control members')
            for path in [*STAGE.rglob('*'), STAGE]: os.utime(path, (epoch, epoch), follow_symlinks=False)
            packages = OUT / 'packages'; packages.mkdir(exist_ok=True)
            deb = packages / f'{PACKAGE}_{VERSION}_{ARCH}.deb'
            pending = deb.with_suffix('.deb.pending')
            run(['dpkg-deb', '--root-owner-group', '-Zxz', '-z6', '--uniform-compression', '--build', STAGE, pending], log, runtime_env)
            if (capture(['dpkg-deb', '-f', pending, 'Package']) != PACKAGE or
                    capture(['dpkg-deb', '-f', pending, 'Version']) != VERSION):
                raise RuntimeError('new package identity mismatch')
            if installed_snapshot() != before or inventory(REF) != reference:
                raise RuntimeError('installed package or read-only S42 source changed during build')
            pinned(BASE_DEB, BASE_DEB_SHA); pinned(PATCH, PATCH_SHA)
            os.replace(pending, deb)
            write_json(OUT / 'build-manifest.json', {'status': 'BUILT_NOT_INSTALLED', 'package': PACKAGE,
                'package_version': VERSION, 'architecture': ARCH, 'deb': str(deb), 'deb_sha256': sha(deb),
                'staged_binary': str(STAGE / BIN), 'binary_sha256': provenance['binary_sha256'],
                'provenance_sha256': sha(STAGE / DOC), 'source_inputs_sha256': sha(OUT / 'source-inputs.json'),
                'patch_sha256': PATCH_SHA, 'source_sha256': SOURCE_SHA, 'source_date_epoch': epoch,
                'builder_sha256': source_receipt['builder_sha256'], 'cmake_options': CMAKE_OPTIONS,
                'rollback': {'path': str(rollback), 'sha256': BASE_DEB_SHA, 'version': BASE_VERSION},
                'changed_payload_paths': sorted(changed), 'baseline_installed': before,
                'compiler': capture(['clang', '--version']).splitlines()[0],
                'cmake': capture(['cmake', '--version']).splitlines()[0]})
    print('S43_VKCUBE_PACKAGE=BUILT_NOT_INSTALLED')
    print('S43_VKCUBE_MANIFEST=' + str(OUT / 'build-manifest.json'))


if __name__ == '__main__':
    main()
