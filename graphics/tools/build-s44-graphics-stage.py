#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Reproduce the S44 Wrapper/Mesa source, native build and private stage.

Default action is recipe: verify recipe inputs and print commands, with no
build or output changes. Other actions never install or select a driver.
Accepted S42 sources are read-only. Existing candidates are never reset.
Source/options reproducibility is asserted; byte-identical builds are not.
"""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
ROOT = Path('/data/data/com.termux/files/home/alroot_wayland')
PREFIX = Path('/data/data/com.termux/files/usr')
NATIVE = PREFIX.relative_to('/')
BASE = ROOT / '.source/gpu-stage1/s42-default-graphics'
OUT = ROOT / '.source/gpu-stage1/s44-sync-overhead'
PINS = {'wrapper': 'b3b32abb82af4d374fd35c7192a7f524a5fc8bd3',
        'mesa': '6f90e92dfdacc4759d8e9ccb328805d0f0b4439b'}
EPOCHS = {'wrapper': 1793464502, 'mesa': 1792935002}
VERSIONS = {'wrapper': '26.3.0+alroot.s42.1', 'mesa': '27.0.0~devel+alroot.s42.1'}
MESON_SHA = '6f9d113615557b1230d74dc537953fece3d3504f9446be76a5ebb5fa0bdd3ab0'
HELPERS = {
    'wrapper': ('graphics/tools/build-s42-wrapper-deb.py', 'aead25e48ececd320c4c3a1de0ef87b5a45c50260bda77f0f4a82e454775b6db'),
    'mesa': ('graphics/tools/build-s42-mesa-debs.py', 'bb53dab1d6359f27a308a1fe38d97aeca25d48747d0de5e586b2035566acc2da'),
}
PATCHES = {
    'wrapper': {
        'graphics/patches/mesa/wrapper/0006-s44-submit-stack.patch': 'd9f5ae06e1d477ffc1b026996ed49f81a891094fc97ddd1cd2f85abcffb2e85b',
        'graphics/patches/mesa/wrapper/0007-s44-wsi-profile.patch': '8476c2d503a7385e42e81b0dd8d4e03ba742f8b5a352aa64eb92fa319d1086c1',
        'graphics/patches/mesa/wrapper/0008-wrapper-small-multi-submit.patch': '7453bf479d49d33ec0a1f177fc3cce40a80d0e0787e6e295db936118c3057d83',
    },
    'mesa': {
        'graphics/patches/mesa/zink/0001-s44-submit-compaction-profile.patch': '74aa4b476abdb8d64bf86abe25b36690334330de28395746a0dac2ccac245708',
        'graphics/patches/mesa/zink/0002-zink-submit-default-on.patch': '15796e2212c6ad264168fbabc3c6d944847136de35eada5573c5703e2dd97420',
    },
}
MESA_OPTIONS = [
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


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def stamp(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def pin(path, expected):
    require(isinstance(expected, str) and re.fullmatch(r'[0-9a-f]{64}', expected), 'missing SHA256 pin')
    require(not path.is_symlink() and path.is_file() and sha(path) == expected,
            'pinned input mismatch: ' + str(path))


def read_json(path):
    return json.loads(Path(path).read_text())


def write_json(path, value, preserve=False):
    if path.exists() and read_json(path) == value:
        return
    require(not preserve or not path.exists(), 'existing receipt differs: ' + str(path))
    pending = path.with_name(path.name + '.pending')
    require(not pending.exists() and not pending.is_symlink(), 'pending receipt already exists')
    pending.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
    os.replace(pending, path)


def owned(path):
    require(path.is_relative_to(OUT), 'output escapes S44 root: ' + str(path))
    for parent in (path, *path.parents):
        require(not parent.is_symlink(), 'output path is a symlink: ' + str(parent))
        if parent == ROOT:
            break


def capture(argv, env=None):
    return subprocess.check_output([str(x) for x in argv], env=env, text=True, timeout=60).strip()


def run(argv, log, env, cwd=ROOT, timeout=None):
    args = [str(x) for x in argv]
    log.write('COMMAND ' + json.dumps(args) + '\n'); log.flush()
    subprocess.run(args, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                   check=True, timeout=timeout)


def inventory(root, source=False):
    result = {}
    for directory, dirs, files in os.walk(root, followlinks=False):
        dirs[:] = sorted(name for name in dirs if not source or name not in ('.git', '__pycache__'))
        for name in sorted([*dirs, *files]):
            if source and (name in ('.git', '__pycache__') or name.endswith('.pyc')):
                continue
            path = Path(directory) / name
            relative = str(path.relative_to(root))
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode):
                require(path.resolve(strict=True).is_relative_to(root.resolve()),
                        'input symlink escapes its independent tree: ' + str(path))
                result[relative] = {'type': 'symlink', 'target': os.readlink(path)}
            elif stat.S_ISREG(info.st_mode):
                result[relative] = {'type': 'file', 'sha256': sha(path),
                                    'mode': stat.S_IMODE(info.st_mode) & (0o111 if source else 0o777)}
            elif not stat.S_ISDIR(info.st_mode):
                raise RuntimeError('unsupported tree member: ' + str(path))
    return result


def helper(component):
    name, expected = HELPERS[component]
    path = ROOT / name
    pin(path, expected)
    spec = importlib.util.spec_from_file_location('s44_build_' + component, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_recipe(args, wrapper):
    component = args.component
    directory = OUT / 'revisions' / args.revision / component if args.revision else OUT / component
    owned(directory)
    patches = read_json(args.patch_manifest) if args.patch_manifest else PATCHES[component]
    require(isinstance(patches, dict) and patches and list(patches) == sorted(patches),
            'patch manifest must contain an ordered path:SHA256 map')
    namespace = 'wrapper' if component == 'wrapper' else 'zink'
    for name, expected in patches.items():
        require(name.startswith('graphics/patches/mesa/' + namespace + '/') and
                '..' not in Path(name).parts and name.endswith('.patch'), 'unexpected patch path: ' + name)
        pin(ROOT / name, expected)
    launcher_input = (ROOT / '.source/gpu-stage1/wrapper-meson/meson' if component == 'wrapper'
                      else BASE / 'mesa/meson')
    pin(launcher_input, MESON_SHA)
    options = wrapper.CONFIGURE if component == 'wrapper' else MESA_OPTIONS
    result = {'component': component, 'directory': str(directory),
        'baseline_source': str(BASE / component / 'source'), 'baseline_git': PINS[component],
        'source': str(directory / 'source'), 'build': str(directory / 'build'),
        'stage': str(directory / 'stage'), 'runtime': str(directory / 'runtime'),
        'options': options, 'patches': patches, 'launcher_input': str(launcher_input),
        'launcher_sha256': MESON_SHA,
        'meson': str(launcher_input if component == 'wrapper' else directory / 'meson'),
        'build_environment': {'CC': 'cc', 'CXX': 'c++', 'MESA_GIT_SHA1_OVERRIDE': PINS[component],
            'SOURCE_DATE_EPOCH': str(EPOCHS[component]), 'LD_PRELOAD': str(PREFIX / 'lib/libtermux-exec.so'),
            'CMAKE_PREFIX_PATH': str(PREFIX), 'LLVM_CONFIG': str(PREFIX / 'bin/llvm-config')},
        'historical_source_date_epoch': 'NOT_RECORDED',
        'reproducibility': 'pinned source, patches and configure arguments; exact binary identity must be remeasured',
        'installs_packages': False, 'selects_default_driver': False}
    targets = ['src/vulkan/wrapper/libvulkan_wrapper.so', 'src/vulkan/wrapper/wrapper_icd.aarch64.json'] if component == 'wrapper' else []
    result['commands'] = {
        'configure_new_build': [result['meson'], 'setup', result['build'], result['source'], *options],
        'build': ['ninja', '-C', result['build'], '-j', str(args.jobs), *targets],
        'stage': [result['meson'], 'install', '-C', result['build'], '--destdir',
                  '<fresh temporary directory inside ' + str(directory) + '>', '--no-rebuild', '--strip'],
    }
    return result


def check_baseline(recipe, wrapper):
    component = recipe['component']
    source = Path(recipe['baseline_source'])
    require(source.is_dir() and not source.is_symlink(), 'accepted S42 source is missing or linked')
    if component == 'wrapper':
        receipt = read_json(BASE / component / 'source-inputs.json')
        require(receipt['baseline_commit'] == PINS[component], 'Wrapper baseline commit mismatch')
        for name, expected in receipt['overlay_files'].items():
            pin(source / name, wrapper.COMMON_AFTER if name == 'src/vulkan/wsi/wsi_common.c' else expected)
        pin(source / 'src/vulkan/wsi/wsi_common.c', wrapper.COMMON_AFTER)
        pin(source / 'src/vulkan/wsi/wsi_common_x11.c', wrapper.X11_SHA)
    else:
        receipt = read_json(BASE / component / 'source-receipt.json')
        require(receipt['source_commit'] == PINS[component]
                and (source / 'VERSION').read_text().strip() == '27.0.0-devel', 'Mesa baseline mismatch')
        for name, expected in receipt['modified_sources'].items():
            pin(source / name, expected)
        require(capture(['git', '-C', source, 'rev-parse', 'HEAD']) == PINS[component], 'Mesa worktree HEAD changed')
        require(set(capture(['git', '-C', source, 'diff', '--name-only']).splitlines()) == set(receipt['modified_sources']),
                'unrecorded tracked changes in accepted Mesa source')
    return inventory(source, source=True)


def patch_inputs(recipe):
    names = set()
    allowed = ('src/vulkan/wrapper/', 'src/vulkan/wsi/') if recipe['component'] == 'wrapper' else ('src/gallium/drivers/zink/',)
    for patch in recipe['patches']:
        for line in (ROOT / patch).read_text().splitlines():
            if not line.startswith(('--- ', '+++ ')):
                continue
            name = line[4:].split('\t', 1)[0]
            if name == '/dev/null':
                continue
            require(name.startswith(('a/', 'b/')), 'unexpected unified patch header')
            name = name[2:]
            require(name.startswith(allowed) and '..' not in Path(name).parts,
                    'patch touches a path outside this S44 component: ' + name)
            names.add(name)
    require(names, 'patches contain no source paths')
    return names


def apply_patches(recipe, tree, log, env):
    for name in recipe['patches']:
        run(['patch', '--batch', '--forward', '--fuzz=0', '--no-backup-if-mismatch', '--reject-file=-',
             '-p1', '-i', ROOT / name], log, env, cwd=tree, timeout=30)


def prepare_source(recipe, baseline, log, env, verify_only=False):
    directory, source, original = map(Path, (recipe['directory'], recipe['source'], recipe['baseline_source']))
    touched = patch_inputs(recipe)
    with tempfile.TemporaryDirectory(prefix='patch-proof-', dir=directory) as temporary:
        proof = Path(temporary)
        for name in touched:
            path = original / name
            if path.exists():
                require(path.is_file() and not path.is_symlink(), 'patched baseline member is not a regular file')
                target = proof / name; target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
        apply_patches(recipe, proof, log, env)
        patched = inventory(proof, source=True)
        require(set(patched) == touched, 'unexpected patch additions/deletions/backups')
        expected = {**baseline, **patched}
        if source.exists():
            require(inventory(source, source=True) == expected,
                    'existing S44 source differs from accepted S42 + selected patches; use --revision')
        else:
            require(not verify_only, 'verify requires an existing S44 source tree')
            with tempfile.TemporaryDirectory(prefix='source-copy-', dir=directory) as copy:
                tree = Path(copy) / 'source'
                shutil.copytree(original, tree, symlinks=True,
                                ignore=shutil.ignore_patterns('.git', '__pycache__', '*.pyc'))
                apply_patches(recipe, tree, log, env)
                require(inventory(tree, source=True) == expected, 'independent patched source copy mismatch')
                tree.rename(source)
    state = {'baseline_inventory_sha256': stamp(baseline), 'source_inventory_sha256': stamp(expected),
             'patches': recipe['patches'], 'modified_sources': {name: patched[name]['sha256'] for name in sorted(touched)}}
    if verify_only:
        return state
    receipt = {key: recipe[key] for key in ('baseline_source', 'source', 'build', 'baseline_git', 'options')}
    receipt['installed_files_changed'] = False
    write_json(directory / 'prepare-receipt.json', receipt, preserve=True)
    # Never silently replace the selected patch manifest used by measurements.
    write_json(directory / 'patches.json', recipe['patches'], preserve=True)
    write_json(directory / 'source-state.json', state)
    return state


def configured_options(recipe):
    build = Path(recipe['build'])
    values = {item['name']: item['value'] for item in read_json(build / 'meson-info/intro-buildoptions.json')}
    for option in recipe['options']:
        name, value = option.removeprefix('--').removeprefix('-D').split('=', 1)
        if option.startswith('--'):
            name = name.replace('-', '_')
        require(name in values, 'configured option is absent: ' + name)
        actual = values[name]
        if isinstance(actual, list):
            expected = shlex.split(value) if name.endswith(('_args', '_link_args')) else (value.split(',') if value else [])
        elif isinstance(actual, bool):
            require(value in ('true', 'false'), 'unexpected boolean configure syntax')
            expected = value == 'true'
        else:
            expected = value
        require(actual == expected, 'configured option differs: ' + name)
    info = read_json(build / 'meson-info/meson-info.json')
    directories = info['directories']
    require(Path(directories['source']).resolve() == Path(recipe['source']).resolve()
            and Path(directories['build']).resolve() == build.resolve(), 'Meson source/build ownership differs')
    return values


def configure(recipe, log, env):
    directory, build, meson = map(Path, (recipe['directory'], recipe['build'], recipe['meson']))
    if recipe['component'] == 'mesa':
        if meson.exists():
            pin(meson, MESON_SHA)
        else:
            shutil.copy2(recipe['launcher_input'], meson); meson.chmod(0o755)
    owned(build)
    existing = (build / 'build.ninja').is_file()
    if build.exists() and not existing:
        raise RuntimeError('existing build directory has no completed Meson setup; preserving it, use --revision')
    if not existing:
        run([meson, 'setup', build, recipe['source'], *recipe['options']], log, env, timeout=300)
    values = configured_options(recipe)
    receipt = {'options': recipe['options'], 'source': recipe['source'], 'build': recipe['build'],
               'launcher_sha256': MESON_SHA, 'git_sha1_override': recipe['baseline_git']}
    write_json(directory / 'configure-state.json', receipt, preserve=True)
    # The historical commands did not record SOURCE_DATE_EPOCH. Do not assert
    # that old objects were built with the new repeatable environment.
    history = directory / 'build-origin.json'
    if not history.exists():
        write_json(history, {'origin': 'verified_preexisting' if existing else 'created_by_this_builder',
            'source_date_epoch_at_original_configure': None if existing else env['SOURCE_DATE_EPOCH']})
    return values


def build(recipe, source_state, args, log, env):
    directory, build_dir = map(Path, (recipe['directory'], recipe['build']))
    targets = ['src/vulkan/wrapper/libvulkan_wrapper.so', 'src/vulkan/wrapper/wrapper_icd.aarch64.json'] if args.component == 'wrapper' else []
    run(['ninja', '-C', build_dir, '-j', args.jobs, *targets], log, env)
    require('(git-' + recipe['baseline_git'][:10] + ')' in (build_dir / 'src/git_sha1.h').read_text(),
            'generated Mesa git identity differs from baseline')
    write_json(directory / 'build-state.json', {'source_state_sha256': stamp(source_state),
        'configure_state_sha256': sha(directory / 'configure-state.json'),
        'environment_for_this_invocation': recipe['build_environment'],
        'compiler': capture(['cc', '--version'], env).splitlines()[0],
        'meson_version': capture([recipe['meson'], '--version'], env),
        'action': 'incremental_ninja', 'binary_identity_requires_staged_hashes': True})


def ensure_stage_compatible(stage, new_inventory, receipt):
    if not stage.exists():
        return False
    old = inventory(stage)
    if old == new_inventory:
        return True
    require(receipt.is_file() and read_json(receipt).get('stage_inventory_sha256') == stamp(old),
            'existing stage differs and has no matching inventory receipt; preserved, use --revision')
    return False


def runtime_plan(recipe, library):
    runtime = Path(recipe['runtime']); owned(runtime)
    desired = {'library_sha256': sha(library), 'icd': {'file_format_version': '1.0.1', 'ICD': {
        'library_path': str(runtime / 'libvulkan_wrapper.so'), 'api_version': '1.3.359'}}}
    replace = False
    if runtime.exists():
        require(runtime.is_dir() and set(p.name for p in runtime.iterdir()) ==
                {'libvulkan_wrapper.so', 'wrapper_icd.private.json'}, 'private runtime has unrecognized members')
        require(not (runtime / 'libvulkan_wrapper.so').is_symlink()
                and not (runtime / 'wrapper_icd.private.json').is_symlink(), 'private runtime members must be regular files')
        old = {'library_sha256': sha(runtime / 'libvulkan_wrapper.so'),
               'icd': read_json(runtime / 'wrapper_icd.private.json')}
        replace = old != desired
        if replace:
            receipt = Path(recipe['directory']) / 'runtime-state.json'
            require(receipt.is_file() and read_json(receipt) == old,
                    'private runtime changed or has no matching receipt; preserved, use --revision')
    return desired, replace


def publish_runtime(recipe, library, desired, replace):
    directory, runtime = Path(recipe['directory']), Path(recipe['runtime'])
    if replace:
        archive = directory / 'history' / ('runtime-' + time.strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
        archive.parent.mkdir(parents=True, exist_ok=True)
        runtime.rename(archive)
        shutil.copy2(directory / 'runtime-state.json', archive / 'runtime-state.json')
        # Preserve the original ICD bytes and provide a separate usable loader
        # manifest; its original absolute path now names the new candidate.
        archived_icd = read_json(archive / 'wrapper_icd.private.json')
        archived_icd['ICD']['library_path'] = str(archive / 'libvulkan_wrapper.so')
        write_json(archive / 'wrapper_icd.archived.json', archived_icd)
    if not runtime.exists():
        runtime.mkdir()
        shutil.copy2(library, runtime / 'libvulkan_wrapper.so')
        (runtime / 'libvulkan_wrapper.so').chmod(0o755)
        write_json(runtime / 'wrapper_icd.private.json', desired['icd'])
    write_json(directory / 'runtime-state.json', desired)


def stage(recipe, source_state, mesa, wrapper, log, env):
    directory, build_dir, destination = map(Path, (recipe['directory'], recipe['build'], recipe['stage']))
    recorded = read_json(directory / 'build-state.json')
    require(recorded['source_state_sha256'] == stamp(source_state)
            and recorded['configure_state_sha256'] == sha(directory / 'configure-state.json'),
            'build receipt does not match verified source/configuration; run --action build first')
    with tempfile.TemporaryDirectory(prefix='stage-copy-', dir=directory) as temporary:
        fresh = Path(temporary) / 'stage'
        run([recipe['meson'], 'install', '-C', build_dir, '--destdir', fresh, '--no-rebuild', '--strip'],
            log, env, timeout=180)
        staged_prefix = fresh / NATIVE
        if recipe['component'] == 'mesa':
            mesa.STAGE, mesa.STAGED_PREFIX = fresh, staged_prefix
            mesa.normalize_stage()
        else:
            wrapper.OUT = directory
            dynamic, needed, owners = wrapper.check_dynamic(staged_prefix / 'lib/libvulkan_wrapper.so', 'readelf')
            (directory / 'staged-dynamic.txt').write_text(dynamic + '\n')
        # All ELF checks and identities happen after Meson's strip install.
        for path in sorted(staged_prefix.rglob('*')):
            if path.is_symlink() or not path.is_file():
                continue
            with path.open('rb') as stream:
                elf = stream.read(4) == b'\x7fELF'
            if elf:
                text = capture(['readelf', '-d', path], env)
                require(not any(value in text for value in (str(ROOT), '/.source/', 'android_stub')),
                        'private path in staged ELF dynamic section: ' + str(path))
        current = inventory(fresh)
        receipt = directory / 'stage-state.json'
        identical = ensure_stage_compatible(destination, current, receipt)
        runtime = runtime_plan(recipe, staged_prefix / 'lib/libvulkan_wrapper.so') if recipe['component'] == 'wrapper' else None
        if not identical:
            if destination.exists():
                archive = directory / 'history' / (time.strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
                archive.mkdir(parents=True)
                destination.rename(archive / 'stage')
                shutil.copy2(receipt, archive / 'stage-state.json')
            fresh.rename(destination)
        prefix = destination / NATIVE
        files, links = mesa.inventory(prefix)
        result = {'prefix': str(prefix), 'runtime_files': files, 'runtime_symlinks': links,
                  'baseline_version': VERSIONS[recipe['component']], 'stripped_before_hashing': True,
                  'source_state_sha256': stamp(source_state)}
        if recipe['component'] == 'wrapper':
            library = prefix / 'lib/libvulkan_wrapper.so'
            publish_runtime(recipe, library, *runtime)
            result.update(runtime_icd=str(Path(recipe['runtime']) / 'wrapper_icd.private.json'), library_sha256=sha(library))
        write_json(receipt, {'stage_inventory_sha256': stamp(inventory(destination)),
            'source_state_sha256': stamp(source_state), 'build_state_sha256': sha(directory / 'build-state.json'),
            'patches': recipe['patches'], 'baseline_git': recipe['baseline_git'],
            'strip': True, 'reused_identical_stage': identical})
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--component', choices=('wrapper', 'mesa'), required=True)
    parser.add_argument('--action', choices=('recipe', 'verify', 'prepare', 'build', 'stage', 'all'), default='recipe')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--revision', help='new safe leaf under S44/revisions; never replaces the measured candidate')
    parser.add_argument('--patch-manifest', type=Path, help='explicit ordered path:SHA256 JSON instead of final embedded S44 pins')
    args = parser.parse_args()
    require(1 <= args.jobs <= 8, '--jobs must be 1..8')
    require(args.revision is None or re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}', args.revision), 'invalid revision')
    wrapper, mesa = helper('wrapper'), helper('mesa')
    recipe = make_recipe(args, wrapper)
    if args.action == 'recipe':
        print(json.dumps(recipe, indent=2))
        print('S44_BUILD_ACTION=RECIPE_ONLY')
        return
    require(Path(os.environ.get('PREFIX', '')) == PREFIX
            and capture(['dpkg', '--print-architecture']) == 'aarch64', 'requires native aarch64 Termux')
    require(capture(['llvm-config', '--version']).startswith('21.'), 'accepted Mesa configuration requires LLVM21')
    directory = Path(recipe['directory']); directory.mkdir(parents=True, exist_ok=True)
    exec_shim = mesa.build_exec_shim()
    env = os.environ.copy()
    for name in ('LD_PRELOAD', 'LD_LIBRARY_PATH', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS',
                 'CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH',
                 'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR', 'PKG_CONFIG_SYSROOT_DIR', 'DESTDIR'):
        env.pop(name, None)
    env.update(recipe['build_environment'], LC_ALL='C')
    require(env['LD_PRELOAD'] == exec_shim['path'], 'build preload differs from the package-owned exec shim')
    mesa.SOFTWARE_PACKAGES += ('vulkan-wrapper-android', 'vulkan-tools', 'mesa-demos')
    with (directory / 'reproduce.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        baseline = check_baseline(recipe, wrapper)
        before = mesa.installed_snapshot()
        result = None
        logpath = directory / ('reproduce-' + time.strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()) + '.log')
        try:
            with logpath.open('w') as log:
                source_state = prepare_source(recipe, baseline, log, env, verify_only=args.action == 'verify')
                if args.action == 'verify':
                    configured_options(recipe)
                else:
                    configure(recipe, log, env)
                if args.action in ('build', 'all'):
                    build(recipe, source_state, args, log, env)
                if args.action in ('stage', 'all'):
                    result = stage(recipe, source_state, mesa, wrapper, log, env)
        finally:
            require(mesa.installed_snapshot() == before, 'installed graphics/build inputs changed')
            require(check_baseline(recipe, wrapper) == baseline, 'accepted S42 source changed')
            for name, expected in recipe['patches'].items():
                pin(ROOT / name, expected)
            for name, expected in HELPERS.values():
                pin(ROOT / name, expected)
        if result is not None:
            result['installed_unchanged'] = True
            write_json(directory / 'runtime-identity.json', result)
        if args.action != 'verify':
            write_json(directory / 'reproduce-recipe.json', recipe)
        print('S44_BUILD_ACTION=' + args.action.upper() + '_COMPLETE')
        print('S44_BUILD_LOG=' + str(logpath))
        if result is not None:
            print('S44_RUNTIME_IDENTITY=' + str(directory / 'runtime-identity.json'))


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('S44_BUILD_ERROR=' + str(exc)[:500], file=sys.stderr)
        raise SystemExit(2)
