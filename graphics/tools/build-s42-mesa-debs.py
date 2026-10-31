#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Build coherent Mesa 27 runtime/development debs for the S26U Bionic prefix.

Uses an owned detached source worktree, a fresh Meson build and DESTDIR.
Never installs packages or changes an existing desktop. The staged runtime is
available for the required S39 Xwayland compatibility check before installation.
"""
import argparse
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PREFIX = Path('/data/data/com.termux/files/usr')
REF = ROOT / '.source/gpu-stage27-pr25/source'
PIN = '6f90e92dfdacc4759d8e9ccb328805d0f0b4439b'
VERSION = '27.0.0~devel+alroot.s42.1'
OUT = ROOT / '.source/gpu-stage1/s42-default-graphics/mesa'
SOURCE, BUILD, STAGE = OUT / 'source', OUT / 'build', OUT / 'stage'
STAGED_PREFIX = STAGE / PREFIX.relative_to('/')
PATCHDIR = ROOT / 'graphics/patches/mesa'
PR25 = PATCHDIR / 'pr25'
EGL_PATCHES = (
    PATCHDIR / 'egl/0001-zink-kopper-ahb-wayland-no-rendernode.patch',
    PATCHDIR / 'egl/0003-zink-optional-driconf-kopper.patch',
)
RECIPE_PINS = {
    'graphics/patches/mesa/pr25/SHA256SUMS': '447824c264cc0e8e4215ef6ae2b158a7b365433e9aebc163ff4846bb401c7d0e',
    'graphics/patches/mesa/egl/0001-zink-kopper-ahb-wayland-no-rendernode.patch': 'f5c8f1d449a9d5681997603cb96dae2762c82774a7f650e41f3009e254f1a859',
    'graphics/patches/mesa/egl/0003-zink-optional-driconf-kopper.patch': 'c4d3dfca804cadc381999dc2c8e6ee605e1041fca2c8ffc1c00b4fbecd1a0d30',
}
SOFTWARE_PACKAGES = ('mesa', 'mesa-dev', 'libglvnd', 'libglvnd-dev',
                     'mesa-vulkan-icd-freedreno', 'mesa-vulkan-icd-swrast', 'termux-exec')


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''): h.update(b)
    return h.hexdigest()


def output(argv, cwd=ROOT):
    return subprocess.check_output([str(x) for x in argv], cwd=cwd, text=True, timeout=60).strip()


def run(argv, log, cwd=ROOT, env=None, timeout=None):
    args = [str(x) for x in argv]
    log.write('COMMAND ' + json.dumps(args) + '\n'); log.flush()
    subprocess.run(args, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                   check=True, timeout=timeout)


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    stage = path.with_name(path.name + '.tmp')
    stage.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')
    os.replace(stage, path)


def inventory(prefix):
    files, links = {}, {}
    for p in sorted(prefix.rglob('*')):
        name = str(p.relative_to(prefix))
        if p.is_symlink(): links[name] = os.readlink(p)
        elif p.is_file(): files[name] = sha(p)
    return files, links


def installed_snapshot():
    snapshot = {}
    for package in SOFTWARE_PACKAGES:
        result = subprocess.run(['dpkg-query', '-L', package], capture_output=True, text=True)
        if result.returncode: continue
        for name in result.stdout.splitlines():
            p = Path(name)
            if not p.is_relative_to(PREFIX): continue
            if p.is_symlink(): snapshot[str(p)] = {'link': os.readlink(p)}
            elif p.is_file(): snapshot[str(p)] = {'sha256': sha(p)}
    return snapshot


def check_snapshot(snapshot):
    for name, entry in snapshot.items():
        p = Path(name)
        if 'link' in entry:
            if not p.is_symlink() or os.readlink(p) != entry['link']:
                raise RuntimeError('installed input symlink changed: ' + name)
        elif not p.is_file() or sha(p) != entry['sha256']:
            raise RuntimeError('installed input changed: ' + name)


def build_exec_shim():
    # Mesa invokes some Python generators by their /usr/bin/env shebang.
    # Native Termux needs its installed exec-path compatibility shim for that
    # operation. Whitelist this package-owned build helper, never inherit an
    # arbitrary caller preload or the graphics fence interposer.
    path = PREFIX / 'lib/libtermux-exec.so'
    resolved = path.resolve(strict=True)
    if not resolved.is_file() or not resolved.is_relative_to(PREFIX / 'lib'):
        raise RuntimeError('Termux build exec shim is outside the installed native library directory')
    owners = {line.split(': ', 1)[0].split(':', 1)[0]
              for line in output(['dpkg-query', '-S', str(path)]).splitlines()}
    if owners != {'termux-exec'}:
        raise RuntimeError('build exec shim is not owned by the expected termux-exec package')
    return {'path': str(path), 'sha256': sha(resolved), 'package': 'termux-exec'}


def recipe():
    if output(['git', '-C', REF, 'rev-parse', 'HEAD']) != PIN:
        raise RuntimeError('Mesa reference commit differs from the reviewed PR25 pin')
    if output(['git', '-C', REF, 'status', '--porcelain']):
        raise RuntimeError('Mesa reference source is modified')
    if (REF / 'VERSION').read_text().strip() != '27.0.0-devel':
        raise RuntimeError('unexpected Mesa source version')
    for name, expected in RECIPE_PINS.items():
        p = ROOT / name
        if not p.is_file() or sha(p) != expected:
            raise RuntimeError('recipe input mismatch: ' + name)
    subprocess.run(['sha256sum', '-c', str(PR25 / 'SHA256SUMS')], cwd=ROOT,
                   stdout=subprocess.DEVNULL, check=True, timeout=30)
    patches = sorted(PR25.glob('*.patch'))
    if len(patches) != 14: raise RuntimeError('expected exactly 14 pinned PR25 patches')
    return {str(p.relative_to(ROOT)): sha(p) for p in (*patches, *EGL_PATCHES)}


def prepare_source(patches, log):
    receipt_path = OUT / 'source-receipt.json'
    if SOURCE.exists():
        if not receipt_path.is_file():
            raise RuntimeError('existing source has no completed S42 receipt; refusing to reset it')
        receipt = json.loads(receipt_path.read_text())
        if receipt['patches'] != patches or output(['git', '-C', SOURCE, 'rev-parse', 'HEAD']) != PIN:
            raise RuntimeError('existing S42 source belongs to another recipe')
        for name, expected in receipt['modified_sources'].items():
            if sha(SOURCE / name) != expected: raise RuntimeError('S42 patched source changed: ' + name)
        names = set(output(['git', '-C', SOURCE, 'diff', '--name-only']).splitlines())
        if names != set(receipt['modified_sources']):
            raise RuntimeError('unexpected tracked modifications in S42 source')
        handle = SOURCE / 'include/cutils/native_handle.h'
        if not handle.is_symlink() or os.readlink(handle) != '../android_stub/cutils/native_handle.h':
            raise RuntimeError('private Android header link changed')
        return receipt
    run(['git', '-C', REF, 'worktree', 'add', '--detach', SOURCE, PIN], log, timeout=120)
    for patch in EGL_PATCHES:
        run(['patch', '--batch', '--forward', '--fuzz=0', '-p1', '-i', patch], log, cwd=SOURCE, timeout=30)
    for patch in sorted(PR25.glob('*.patch')):
        actual = patch
        if patch.name.startswith('0012-'):
            text = patch.read_text()
            delimiter = '--- a/src/egl/drivers/dri2/platform_wayland.c'
            if text.count(delimiter) != 1: raise RuntimeError('unexpected PR25 Wayland conflict hunk')
            actual = OUT / 'pr25-0012-x11-only.patch'
            actual.write_text(text.split(delimiter, 1)[0])
        run(['patch', '--batch', '--forward', '--fuzz=2', '-p1', '-i', actual], log, cwd=SOURCE, timeout=30)
    wayland = SOURCE / 'src/egl/drivers/dri2/platform_wayland.c'
    text = wayland.read_text()
    old = '''   if (!dri2_setup_device(disp, disp->Options.ForceSoftware ||
                         (dri2_dpy->kopper && dri2_dpy->fd_render_gpu < 0))) {'''
    new = '''#ifdef __ANDROID__
   if (!dri2_setup_device(disp, true)) {
#else
   if (!dri2_setup_device(disp, disp->Options.ForceSoftware ||
                         (dri2_dpy->kopper && dri2_dpy->fd_render_gpu < 0))) {
#endif'''
    if text.count(old) != 1: raise RuntimeError('reviewed PR25/Alroot Wayland merge no longer applies')
    wayland.write_text(text.replace(old, new))
    handle = SOURCE / 'include/cutils/native_handle.h'
    handle.parent.mkdir(parents=True, exist_ok=True)
    handle.symlink_to('../android_stub/cutils/native_handle.h')
    modified = output(['git', '-C', SOURCE, 'diff', '--name-only']).splitlines()
    receipt = {'source_commit': PIN, 'mesa_version': '27.0.0-devel', 'patches': patches,
               'modified_sources': {name: sha(SOURCE / name) for name in modified},
               'wayland_merge': 'PR25 Android software device + Alroot DRM-less Kopper',
               'tls': 'Android35 native ELF TLS; old emutls export patch intentionally absent'}
    write_json(receipt_path, receipt)
    return receipt


def check_accepted_overlay():
    """Compare every physical source override in the accepted S14/S23 tree.

    Symlinked paths point to the pinned reference. Only tracked source names
    are compared, so patch backups and generated private helper files cannot
    become source inputs accidentally. A difference is recorded before failing
    for inspection; no later accepted modification is silently discarded.
    """
    overlay = ROOT / '.source/gpu-stage27-pr25/mesa-private/source'
    if not overlay.is_dir(): raise RuntimeError('accepted Mesa27 private source overlay is missing')
    tracked = set(subprocess.check_output(['git', '-C', str(REF), 'ls-files', '-z']).decode().split('\0'))
    compared, differences = {}, []
    for directory, dirs, names in os.walk(overlay, followlinks=False):
        dirs[:] = [name for name in dirs if not (Path(directory) / name).is_symlink() and name != '.git']
        for name in names:
            accepted = Path(directory) / name
            relative = str(accepted.relative_to(overlay))
            if accepted.is_symlink() or relative not in tracked or name.endswith(('.orig', '.rej')):
                continue
            expected = sha(accepted)
            current = sha(SOURCE / relative)
            compared[relative] = expected
            if expected != current:
                differences.append({'path': relative, 'accepted_sha256': expected,
                                    'fresh_sha256': current, 'reference_sha256': sha(REF / relative)})
    if not compared: raise RuntimeError('accepted source overlay contained no physical tracked overrides')
    write_json(OUT / 'accepted-overlay-parity.json', {'compared_sources': compared, 'differences': differences})
    if differences:
        raise RuntimeError('fresh source differs from accepted Mesa27 overrides; inspect accepted-overlay-parity.json: ' +
                           ', '.join(item['path'] for item in differences))
    return compared


def configure(env, log):
    meson = OUT / 'meson'
    meson.write_text('#!' + str(PREFIX / 'bin/python3') + '\nfrom mesonbuild.mesonmain import main\nimport sys\nsys.exit(main())\n')
    meson.chmod(0o755)
    options = [
        '--prefix=' + str(PREFIX), '--libdir=lib', '--sysconfdir=etc', '--datadir=share',
        '--buildtype=debugoptimized', '-Dgallium-drivers=zink,freedreno,softpipe,llvmpipe',
        '-Dvulkan-drivers=', '-Dfreedreno-kmds=msm,kgsl', '-Dplatforms=x11,wayland',
        '-Degl=enabled', '-Dglx=dri', '-Dglvnd=enabled', '-Dgles1=enabled', '-Dgles2=enabled',
        '-Dopengl=true', '-Dgbm=enabled', '-Dllvm=enabled', '-Dshared-llvm=enabled',
        '-Dandroid-stub=false', '-Dbuild-tests=false', '-Dgallium-va=disabled',
        '-Dgallium-rusticl=false', '-Dvideo-codecs=', '-Dmesa-clc=auto',
        '-Ddri-drivers-path=' + str(PREFIX / 'lib/dri'),
        '-Dc_args=--target=aarch64-linux-android35', '-Dcpp_args=--target=aarch64-linux-android35',
        '-Dc_link_args=-Wl,--no-as-needed -landroid-shmem -Wl,--as-needed',
        '-Dcpp_link_args=-Wl,--no-as-needed -landroid-shmem -Wl,--as-needed',
    ]
    command = [meson, 'setup', BUILD, SOURCE, *options]
    if (BUILD / 'build.ninja').exists(): command.insert(2, '--reconfigure')
    run(command, log, env=env, timeout=240)
    configured = json.loads((BUILD / 'meson-info/intro-buildoptions.json').read_text())
    values = {x['name']: x['value'] for x in configured}
    for name, expected in {'prefix': str(PREFIX), 'libdir': 'lib', 'glx': 'dri', 'egl': 'enabled',
                           'gbm': 'enabled', 'glvnd': 'enabled', 'llvm': 'enabled', 'android-stub': False,
                           'vulkan-drivers': []}.items():
        if values.get(name) != expected: raise RuntimeError('unexpected Meson option: ' + name)
    return meson, options


def normalize_stage():
    if not STAGED_PREFIX.is_dir(): raise RuntimeError('Meson did not stage the configured Termux prefix')
    for p in STAGE.rglob('*'):
        if (p.is_file() or p.is_symlink()) and not p.is_relative_to(STAGED_PREFIX):
            raise RuntimeError('Meson staged a file outside the Termux prefix: ' + str(p))
    for p in STAGED_PREFIX.rglob('*'):
        if not p.is_symlink(): continue
        target = Path(os.readlink(p))
        if target.is_absolute():
            if not target.is_relative_to(PREFIX): raise RuntimeError('symlink points outside prefix: ' + str(p))
            installed = PREFIX / p.relative_to(STAGED_PREFIX)
            relative = os.path.relpath(target, installed.parent)
            p.unlink(); p.symlink_to(relative)
        if not p.resolve().is_relative_to(STAGED_PREFIX) or not p.exists():
            raise RuntimeError('invalid or out-of-stage runtime link: ' + str(p))
    for name in ('libEGL_mesa.so', 'libGLX_mesa.so'):
        path = STAGED_PREFIX / 'lib' / (name + '.0')
        if not path.exists(): path.symlink_to(name)
    required = ['lib/libEGL_mesa.so', 'lib/libGLX_mesa.so', 'lib/libgallium-27.0.0-devel.so',
                'lib/libgbm.so', 'lib/dri/libdril_dri.so', 'lib/gbm/dri_gbm.so',
                'lib/dri/zink_dri.so', 'lib/dri/kgsl_dri.so', 'lib/dri/swrast_dri.so',
                'lib/dri/kms_swrast_dri.so', 'share/glvnd/egl_vendor.d/50_mesa.json',
                'include/gbm.h', 'include/gbm_backend_abi.h', 'include/EGL/eglmesaext.h',
                'include/EGL/eglext_angle.h', 'include/GL/internal/dri_interface.h']
    for name in required:
        if not (STAGED_PREFIX / name).is_file(): raise RuntimeError('missing Mesa package member: ' + name)
    for p in STAGED_PREFIX.rglob('*'):
        if p.is_file() and (p.name.startswith('libvulkan_') or p.is_relative_to(STAGED_PREFIX / 'share/vulkan')):
            raise RuntimeError('unexpected Vulkan ICD in Mesa-only package: ' + str(p))
        if p.name in ('libnativewindow.so', 'liblog.so', 'libhardware.so', 'libsync.so'):
            raise RuntimeError('Android build stub must never be packaged: ' + str(p))
    return required


def package_tree(name):
    path = OUT / 'package-roots' / name
    if path.exists(): shutil.rmtree(path)
    (path / 'DEBIAN').mkdir(parents=True)
    return path


def write_control(tree, name, architecture):
    depends = ('libandroid-shmem, libc++, libdrm, libglvnd, libllvm (<< 22), libwayland, '
               'libx11, libxext, libxfixes, libxshmfence, libxxf86vm, ncurses, '
               'vulkan-loader, zlib, zstd, libxcb, libexpat') if name == 'mesa' else 'libglvnd-dev'
    fields = {'Package': name, 'Version': VERSION, 'Architecture': architecture,
              'Maintainer': 'Alroot S26U development', 'Depends': depends,
              'Homepage': 'https://www.mesa3d.org', 'Section': 'graphics', 'Priority': 'optional'}
    if name == 'mesa':
        fields.update(Replaces='libmesa, osmesa', Breaks='osmesa, osmesa-demos',
                      Conflicts='libmesa, ndk-sysroot (<= 25b), osmesa', Suggests='mesa-dev',
                      Description='Mesa 27 EGL, GLX, Zink, KGSL and software rendering for Alroot Bionic')
    else:
        fields.update(Replaces='mesa (<< 22.3.3-2), ndk-sysroot (<< 25b-3)',
                      Breaks='mesa (<< 22.3.3-2), ndk-sysroot (<< 25b-3)',
                      Description='Mesa 27 development headers for Alroot Bionic')
    size = sum(p.stat().st_size for p in tree.rglob('*') if p.is_file() and 'DEBIAN' not in p.parts)
    fields['Installed-Size'] = str(math.ceil(size / 1024))
    (tree / 'DEBIAN/control').write_text(''.join(k + ': ' + v + '\n' for k, v in fields.items()))
    sums = []
    for p in sorted(tree.rglob('*')):
        if p.is_file() and not p.is_symlink() and not p.is_relative_to(tree / 'DEBIAN'):
            sums.append(hashlib.md5(p.read_bytes()).hexdigest() + '  ' + str(p.relative_to(tree)))
    (tree / 'DEBIAN/md5sums').write_text('\n'.join(sums) + '\n')


def create_packages(source_receipt, options, env, log):
    required = normalize_stage()
    dynamic = {}
    for p in sorted(STAGED_PREFIX.rglob('*')):
        if not p.is_file() or p.is_symlink(): continue
        with p.open('rb') as f: elf = f.read(4) == b'\x7fELF'
        if not elf: continue
        run(['llvm-strip', '--strip-unneeded', p], log, timeout=60)
        text = output(['readelf', '-d', p])
        for line in text.splitlines():
            if '(RPATH)' in line or '(RUNPATH)' in line:
                if any(s in line for s in (str(ROOT), '/.source/', 'android_stub', str(OUT))):
                    raise RuntimeError('private build path leaked into runtime search path: ' + str(p))
        dynamic[str(p.relative_to(STAGED_PREFIX))] = text
    vendor = json.loads((STAGED_PREFIX / 'share/glvnd/egl_vendor.d/50_mesa.json').read_text())
    if vendor.get('ICD', {}).get('library_path') not in ('libEGL_mesa.so', 'libEGL_mesa.so.0', str(PREFIX / 'lib/libEGL_mesa.so')):
        raise RuntimeError('unexpected installed EGL vendor library path')
    for p in STAGED_PREFIX.rglob('*'):
        if p.is_file() or p.is_symlink():
            result = subprocess.run(['dpkg-query', '-S', str(PREFIX / p.relative_to(STAGED_PREFIX))], capture_output=True, text=True)
            if result.returncode == 0:
                owners = {line.split(': ', 1)[0].split(':', 1)[0] for line in result.stdout.splitlines()}
                if owners - {'mesa', 'mesa-dev'}: raise RuntimeError('payload overlaps another installed package: ' + str(p))
    doc = STAGED_PREFIX / 'share/doc/mesa'
    doc.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(SOURCE / 'docs/license.rst', doc / 'license.rst')
    files, links = inventory(STAGED_PREFIX)
    development_files = {name: value for name, value in files.items()
                         if name.startswith('include/') and name != 'include/gbm_backend_abi.h'}
    files = {name: value for name, value in files.items() if name not in development_files}
    receipt = {'package_version': VERSION, 'mesa_version': '27.0.0-devel', 'source_commit': PIN,
               'source_date_epoch': int(env['SOURCE_DATE_EPOCH']), 'prefix': str(PREFIX),
               'patches': source_receipt['patches'], 'modified_sources': source_receipt['modified_sources'],
               'accepted_overlay_sources': source_receipt['accepted_overlay_sources'],
               'meson_options': options, 'runtime_files': files, 'runtime_symlinks': links,
               'development_files': development_files,
               'driver_scope': 'EGL/GLX Zink, Freedreno KGSL and software; Vulkan ICD packages unchanged',
               'build_only_exec_shim': build_exec_shim(),
               'llvm_version': output(['llvm-config', '--version']),
               'compiler': output(['clang', '--version']).splitlines()[0]}
    # This installed manifest excludes its own digest to avoid a hash cycle.
    write_json(doc / 'alroot-s42-build.json', receipt)
    runtime, development = package_tree('mesa'), package_tree('mesa-dev')
    for p in sorted(STAGED_PREFIX.rglob('*')):
        if not (p.is_file() or p.is_symlink()): continue
        relative = p.relative_to(STAGED_PREFIX)
        is_dev = relative.parts[0] == 'include' and relative.name != 'gbm_backend_abi.h'
        destination = (development if is_dev else runtime) / PREFIX.relative_to('/') / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if p.is_symlink(): destination.symlink_to(os.readlink(p))
        else: shutil.copy2(p, destination)
    write_control(runtime, 'mesa', 'aarch64'); write_control(development, 'mesa-dev', 'all')
    packages = OUT / 'packages'; packages.mkdir(exist_ok=True)
    artifacts = []
    for tree, name, architecture in ((runtime, 'mesa', 'aarch64'), (development, 'mesa-dev', 'all')):
        # The device caller may use umask 0077. Package access modes must not
        # inherit it, and chmod must never follow our relative runtime links.
        tree.chmod(0o755)
        for p in tree.rglob('*'):
            if not p.is_symlink():
                if p.is_dir():
                    p.chmod(0o755)
                elif p.is_file():
                    with p.open('rb') as stream: elf = stream.read(4) == b'\x7fELF'
                    p.chmod(0o755 if elf else 0o644)
            os.utime(p, (int(env['SOURCE_DATE_EPOCH']),) * 2, follow_symlinks=False)
        target = packages / f'{name}_{VERSION}_{architecture}.deb'
        pending = target.with_name(target.name + '.tmp')
        run(['dpkg-deb', '--root-owner-group', '-Zxz', '-z6', '--build', tree, pending], log, env=env, timeout=300)
        run(['dpkg-deb', '--info', pending], log, timeout=30)
        os.replace(pending, target)
        artifacts.append({'package': name, 'version': VERSION, 'path': str(target), 'sha256': sha(target)})
    preflight = OUT / 'preinstall-egl.json'
    write_json(preflight, {'file_format_version': '1.0.0', 'ICD': {'library_path': str(STAGED_PREFIX / 'lib/libEGL_mesa.so')}})
    write_json(OUT / 'runtime-dynamic.json', dynamic)
    write_json(OUT / 'package-manifest.json', {'source': source_receipt, 'packages': artifacts,
        'staged_prefix': str(STAGED_PREFIX), 'preinstall_egl_vendor_json': str(preflight),
        'required_paths': required, 'promotion': 'requires isolated S39 and client validation before installation'})
    return artifacts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8: parser.error('--jobs must be 1..8')
    if output(['dpkg', '--print-architecture']) != 'aarch64': raise RuntimeError('requires native Termux aarch64')
    if int(output(['getprop', 'ro.build.version.sdk'])) < 35: raise RuntimeError('requires Android API35 or newer')
    if not output(['llvm-config', '--version']).startswith('21.'): raise RuntimeError('reviewed recipe requires LLVM21')
    patches = recipe()
    exec_shim = build_exec_shim()
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / 'build.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        before = installed_snapshot(); write_json(OUT / 'installed-inputs-before.json', before)
        env = os.environ.copy()
        for name in ('LD_PRELOAD', 'LD_LIBRARY_PATH', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS',
                     'CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH',
                     'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR', 'PKG_CONFIG_SYSROOT_DIR'):
            env.pop(name, None)
        env.update(SOURCE_DATE_EPOCH=output(['git', '-C', REF, 'show', '-s', '--format=%ct', PIN]),
                   MESA_GIT_SHA1_OVERRIDE=PIN, CMAKE_PREFIX_PATH=str(PREFIX),
                   LLVM_CONFIG=str(PREFIX / 'bin/llvm-config'), CC='cc', CXX='c++',
                   LD_PRELOAD=exec_shim['path'])
        print('S42_MESA_STAGE=SOURCE', flush=True)
        try:
            with (OUT / 'build.log').open('w') as log:
                source_receipt = prepare_source(patches, log)
                source_receipt['accepted_overlay_sources'] = check_accepted_overlay()
                print('S42_MESA_STAGE=CONFIGURE', flush=True)
                meson, options = configure(env, log)
                print('S42_MESA_STAGE=BUILD', flush=True)
                run(['ninja', '-C', BUILD, '-j', args.jobs], log, env=env)
                if STAGE.exists(): shutil.rmtree(STAGE)
                print('S42_MESA_STAGE=DESTDIR', flush=True)
                run([meson, 'install', '-C', BUILD, '--no-rebuild', '--destdir', STAGE], log, env=env, timeout=120)
                print('S42_MESA_STAGE=DEB', flush=True)
                artifacts = create_packages(source_receipt, options, env, log)
            check_snapshot(before)
            if output(['git', '-C', REF, 'status', '--porcelain']): raise RuntimeError('reference source changed')
            print('S42_MESA_PACKAGES=BUILT_NOT_INSTALLED', flush=True)
            for artifact in artifacts: print(json.dumps(artifact), flush=True)
            print('S42_MESA_STAGED_PREFIX=' + str(STAGED_PREFIX), flush=True)
        finally:
            check_snapshot(before)


if __name__ == '__main__':
    main()
