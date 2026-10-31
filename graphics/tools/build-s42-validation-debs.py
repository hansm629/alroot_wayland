#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Repack verified rollback packages and build the accepted coalesced demos.

All outputs stay below the S42 workspace. No package installation, launcher
changes, or writes to the installed prefix or previous source/build trees.
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
OUT = ROOT / '.source/gpu-stage1/s42-default-graphics'
ROLLBACK, DEMOS = OUT / 'rollback', OUT / 'demos'
PACKAGES = {'mesa': '26.3.0', 'mesa-dev': '26.3.0',
            'vulkan-wrapper-android': '26.3.0', 'vulkan-tools': '1.4.365',
            'mesa-demos': '9.0.0-7'}
CUBE_REF = ROOT / '.source/gpu-stage1/s39-live-resize/vkcube/source'
GLX_REF = ROOT / '.source/gpu-stage1/s40-demo-events/source/glxgears.coalesced.c'
CUBE_SHA = 'b396d52524270fce6791bbc32648eb7ac05104986f0dcce3e264399b18503059'
GLX_SHA = 'd4ca3fe86499fc4618dcba03332351d99e35bcee281c6dac60a0904f0f6ac3a3'
GLX_ELF_SHA = '4f5444726d455d522ebaf37b17760febc028db4f6653dcd237517913c836e086'
CMAKE_OPTIONS = ['-G', 'Ninja', '-DCMAKE_SYSTEM_NAME=Linux', '-DCMAKE_BUILD_TYPE=Release',
                 '-DBUILD_CUBE=ON', '-DBUILD_VULKANINFO=OFF', '-DBUILD_ICD=OFF',
                 '-DBUILD_TESTS=OFF', '-DBUILD_WSI_XCB_SUPPORT=ON',
                 '-DBUILD_WSI_XLIB_SUPPORT=ON', '-DBUILD_WSI_WAYLAND_SUPPORT=ON',
                 '-DBUILD_WSI_DISPLAY_SUPPORT=ON', '-DVULKAN_HEADERS_INSTALL_DIR=' + str(PREFIX)]


def sha(path, algorithm='sha256'):
    h = hashlib.new(algorithm)
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''): h.update(block)
    return h.hexdigest()


def capture(argv):
    return subprocess.check_output([str(x) for x in argv], text=True, timeout=60).strip()


def run(argv, log, env, timeout=1200):
    argv = [str(x) for x in argv]
    log.write('COMMAND ' + json.dumps(argv) + '\n'); log.flush()
    subprocess.run(argv, env=env, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                   check=True, timeout=timeout)


def write_json(path, data):
    missing = []
    directory = path.parent
    while not directory.exists():
        missing.append(directory); directory = directory.parent
    for directory in reversed(missing):
        directory.mkdir(); directory.chmod(0o755)
    temp = path.with_name(path.name + '.pending')
    temp.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')
    temp.chmod(0o644); os.replace(temp, path)


def entry(path):
    st = path.lstat()
    value = {'mode': stat.S_IMODE(st.st_mode)}
    if stat.S_ISLNK(st.st_mode): value.update(type='symlink', target=os.readlink(path))
    elif stat.S_ISREG(st.st_mode): value.update(type='file', size=st.st_size, sha256=sha(path))
    elif stat.S_ISDIR(st.st_mode): value['type'] = 'directory'
    else: raise RuntimeError('unsupported package/source object: ' + str(path))
    return value


def installed_package(package):
    fields, key = {}, None
    for line in capture(['dpkg-query', '-s', package]).splitlines():
        if line.startswith((' ', '\t')) and key: fields[key] += '\n' + line
        elif ': ' in line: key, value = line.split(': ', 1); fields[key] = value
        elif line: raise RuntimeError('unexpected package control syntax: ' + package)
    if (fields.get('Status') != 'install ok installed' or fields.get('Package') != package or
            fields.get('Version') != PACKAGES[package]):
        raise RuntimeError('installed package does not match rollback baseline: ' + package)
    del fields['Status']
    info = PREFIX / 'var/lib/dpkg/info'
    metadata = sorted(set(info.glob(package + '.*')) | set(info.glob(package + ':*.*')))
    if not metadata or any(p.suffix not in ('.list', '.md5sums') or not p.is_file()
                           or p.is_symlink() for p in metadata):
        raise RuntimeError('unexpected maintainer scripts/conffiles or dpkg metadata: ' + package)
    payload = {}
    for name in capture(['dpkg-query', '-L', package]).splitlines():
        path = Path(name)
        if not path.is_absolute() or '..' in path.parts:
            raise RuntimeError('unsafe dpkg-owned path: ' + name)
        value = entry(path)
        if not path.is_relative_to(PREFIX):
            if path not in PREFIX.parents or value['type'] != 'directory':
                raise RuntimeError('package payload outside the native prefix: ' + name)
            # Standard archive ancestors must not inherit Android app-private modes.
            value['mode'] = 0o755
        payload[str(path)] = value
    links = {Path(name) for name, value in payload.items() if value['type'] == 'symlink'}
    if any(parent in links for name in payload for parent in Path(name).parents):
        raise RuntimeError('package owns descendants through a symlink: ' + package)
    return {'control': fields, 'files': payload,
            'dpkg_info_sha256': {p.name: sha(p) for p in metadata}}


def snapshot():
    if capture(['dpkg', '--audit']): raise RuntimeError('dpkg audit is not clean')
    if capture(['dpkg', '--verify', *PACKAGES]): raise RuntimeError('installed package verification failed')
    return {name: installed_package(name) for name in PACKAGES}


def empty_stage(path):
    if path.is_symlink(): raise RuntimeError('staging directory is a symlink: ' + str(path))
    if path.exists(): shutil.rmtree(path)
    path.mkdir(parents=True)


def copy_owned(data, stage, source_root=Path('/')):
    empty_stage(stage)
    for name, expected in sorted(data['files'].items(), key=lambda item: (len(Path(item[0]).parts), item[0])):
        relative = Path(name).relative_to('/')
        src, dest = source_root / relative, stage / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        if expected['type'] == 'directory': dest.mkdir(exist_ok=True); dest.chmod(expected['mode'])
        elif expected['type'] == 'symlink': dest.symlink_to(expected['target'])
        else: shutil.copy2(src, dest); dest.chmod(expected['mode'])
        if entry(dest) != expected: raise RuntimeError('snapshot copy changed: ' + name)


def make_deb(package, data, stage, version, directory, epoch, log, env):
    control = dict(data['control']); control['Version'] = version
    files = [p for p in stage.rglob('*') if not p.is_symlink() and p.is_file()]
    control['Installed-Size'] = str((sum(p.stat().st_size for p in files) + 1023) // 1024)
    # Package-manager state is intentionally absent; maintainers/scripts were guarded above.
    debian = stage / 'DEBIAN'; debian.mkdir(); debian.chmod(0o755)
    (debian / 'control').write_text(''.join(k + ': ' + v + '\n' for k, v in control.items()))
    (debian / 'md5sums').write_text(''.join(sha(p, 'md5') + '  ' + str(p.relative_to(stage)) + '\n'
                                         for p in sorted(files)))
    for p in (debian / 'control', debian / 'md5sums'): p.chmod(0o644)
    for p in [*stage.rglob('*'), stage]: os.utime(p, (epoch, epoch), follow_symlinks=False)
    directory.mkdir(parents=True, exist_ok=True)
    deb = directory / f'{package}_{version}_{control["Architecture"]}.deb'
    pending = deb.with_suffix('.deb.pending')
    run(['dpkg-deb', '--root-owner-group', '-Zxz', '-z6', '--uniform-compression',
         '--build', stage, pending], log, env)
    if capture(['dpkg-deb', '-f', pending, 'Package']) != package or capture(['dpkg-deb', '-f', pending, 'Version']) != version:
        raise RuntimeError('generated package identity mismatch: ' + package)
    os.replace(pending, deb)
    return {'path': str(deb), 'sha256': sha(deb), 'version': version}


def tree_files(root):
    return {str(p.relative_to(root)): entry(p) for p in sorted(root.rglob('*'))}


def pinned(path, expected):
    if path.is_symlink() or not path.is_file() or sha(path) != expected:
        raise RuntimeError('pinned input/output mismatch: ' + str(path))


def build_demos(log, env, jobs):
    pinned(CUBE_REF / 'cube/cube.c', CUBE_SHA); pinned(GLX_REF, GLX_SHA)
    source, build = DEMOS / 'source/vulkan-tools', DEMOS / 'build'
    reference = tree_files(CUBE_REF)
    # An independent source copy allows all four production WSI backends without
    # changing the accepted S39 source or its narrower private build.
    if not source.exists():
        source.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(CUBE_REF, source, symlinks=True)
    if source.is_symlink() or tree_files(source) != reference:
        raise RuntimeError('S42 demo source differs from the read-only S39 reference')
    write_json(DEMOS / 'source-inputs.json', {'vulkan_tools_files': reference, 'glxgears_sha256': GLX_SHA})
    build.mkdir(parents=True, exist_ok=True)
    run(['cmake', '-S', source, '-B', build / 'vulkan-tools', *CMAKE_OPTIONS], log, env)
    run(['cmake', '--build', build / 'vulkan-tools', '--target', 'vkcube', '--parallel', str(jobs)], log, env)
    cube = build / 'vulkan-tools/cube/vkcube'
    glx = build / 'glxgears'
    run(['clang', '-O2', '-std=gnu11', '-Wall', '-Wextra', GLX_REF, '-lGL', '-lX11', '-lm', '-o', glx], log, env)
    pinned(glx, GLX_ELF_SHA)
    runtime_env = env.copy(); runtime_env.pop('LD_PRELOAD', None)
    help_result = subprocess.run([str(cube), '--help'], env=runtime_env, capture_output=True, text=True, timeout=20)
    help_text = help_result.stdout + help_result.stderr
    if help_result.returncode not in (0, 1) or any(name not in help_text.lower() for name in ('xcb', 'xlib', 'wayland', 'display')):
        raise RuntimeError('fresh vkcube lost an installed WSI backend: ' + help_text)
    (DEMOS / 'vkcube-help.txt').write_text(help_text)
    if tree_files(CUBE_REF) != reference: raise RuntimeError('read-only S39 source changed during the build')
    pinned(GLX_REF, GLX_SHA)
    return {'vulkan-tools': (cube, CUBE_SHA), 'mesa-demos': (glx, GLX_SHA)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--rollback-only', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4: parser.error('--jobs must be 1..4')
    if Path(os.environ.get('PREFIX', '')) != PREFIX or capture(['dpkg', '--print-architecture']) != 'aarch64':
        raise RuntimeError('requires native S26U aarch64 Termux')
    ROLLBACK.mkdir(parents=True, exist_ok=True); DEMOS.mkdir(parents=True, exist_ok=True)
    with (OUT / 'validation-debs.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        before = snapshot()
        baseline_path = ROLLBACK / 'baseline.json'
        if baseline_path.exists():
            baseline = json.loads(baseline_path.read_text())
            if baseline['packages'] != before: raise RuntimeError('saved rollback baseline differs; refusing to replace it')
        else:
            baseline = {'schema': 1, 'source_date_epoch': int(capture(['git', '-C', ROOT, 'show', '-s', '--format=%ct', 'HEAD'])),
                        'packages': before}
            write_json(baseline_path, baseline)
        env = os.environ.copy()
        for key in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'CC', 'CXX'):
            env.pop(key, None)
        env.update(SOURCE_DATE_EPOCH=str(baseline['source_date_epoch']), DPKG_DEB_THREADS_MAX=str(args.jobs), LC_ALL='C')
        artifacts = {'baseline_sha256': sha(baseline_path), 'rollback': {}, 'demos': {}}
        with (OUT / 'validation-debs.log').open('a') as log:
            for name, data in before.items():
                stage = ROLLBACK / 'stage' / name
                copy_owned(data, stage)
                artifacts['rollback'][name] = make_deb(name, data, stage, data['control']['Version'],
                                                      ROLLBACK / 'packages', baseline['source_date_epoch'], log, env)
            if snapshot() != before: raise RuntimeError('installed packages changed while saving rollback')
            write_json(ROLLBACK / 'packages.json', artifacts['rollback'])
            if not args.rollback_only:
                shim = PREFIX / 'lib/libtermux-exec.so'
                if not shim.resolve(strict=True).is_relative_to(PREFIX / 'lib') or capture(['dpkg-query', '-S', shim]).split(': ', 1)[0] != 'termux-exec':
                    raise RuntimeError('Termux build exec shim is not owned by termux-exec')
                env['LD_PRELOAD'] = str(shim)
                built = build_demos(log, env, args.jobs)
                for name, (binary, source_sha) in built.items():
                    data, version = before[name], PACKAGES[name] + '+alroot.s42.1'
                    stage = DEMOS / 'stage' / name
                    copy_owned(data, stage, ROLLBACK / 'stage' / name)
                    target = stage / PREFIX.relative_to('/') / 'bin' / binary.name
                    if data['files'].get(str(PREFIX / 'bin' / binary.name), {}).get('type') != 'file':
                        raise RuntimeError('demo executable is not an owned regular file')
                    shutil.copy2(binary, target); target.chmod(0o755)
                    manifest = {'package': name, 'package_version': version, 'binary_path': str(PREFIX / 'bin' / binary.name),
                                'binary_sha256': sha(target), 'source_sha256': source_sha,
                                'original_binary_sha256': data['files'][str(PREFIX / 'bin' / binary.name)]['sha256'],
                                'build_exec_shim_sha256': sha(shim), 'rollback_baseline_sha256': sha(baseline_path),
                                'cmake_options': CMAKE_OPTIONS if name == 'vulkan-tools' else [],
                                'scope': 'Accepted event coalescing; all other installed package payload preserved'}
                    docpath = PREFIX / 'share/doc' / name / 'alroot-build.json'
                    owners = subprocess.run(['dpkg-query', '-S', str(docpath)], capture_output=True, text=True, timeout=20)
                    if owners.returncode != 1: raise RuntimeError('new provenance path already has an owner: ' + str(docpath))
                    write_json(stage / docpath.relative_to('/'), manifest)
                    artifacts['demos'][name] = make_deb(name, data, stage, version, DEMOS / 'packages',
                                                       baseline['source_date_epoch'], log, env)
                    artifacts['demos'][name]['binary_sha256'] = manifest['binary_sha256']
            if snapshot() != before: raise RuntimeError('installed packages changed during demo packaging')
        write_json(OUT / 'validation-debs.json', artifacts)
    print('S42_ROLLBACK_PACKAGES=PASS')
    if not args.rollback_only: print('S42_COALESCED_DEMO_PACKAGES=PASS')
    print('S42_VALIDATION_DEBS_MANIFEST=' + str(OUT / 'validation-debs.json'))


if __name__ == '__main__':
    main()
