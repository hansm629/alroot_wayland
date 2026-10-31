#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Package selected S44 staged drivers without building or installing anything.

The S42 owner archives supply the exact file sets, control members, modes and
rollback artifacts. Only existing ELF files and owner provenance may change.
The staged bytes must be the measured bytes: this tool never strips an ELF.

--selection names a reviewed JSON receipt (see selection.example.json). Its
evidence and input hashes bind packaging to a selected experiment; they do not
replace the performance/resize review. The recorded runtime policy must already
have been exercised against those binaries. This tool changes no source or
launcher default; both optimizations are already compiled on, profiling off.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PREFIX = Path('/data/data/com.termux/files/usr')
NATIVE = PREFIX.relative_to('/')
BASE = ROOT / '.source/gpu-stage1/s42-default-graphics'
OUT = ROOT / '.source/gpu-stage1/s44-sync-overhead'
OWNERS = {
    'vulkan-wrapper-android': ('wrapper', '26.3.0+alroot.s42.1', '26.3.0+alroot.s44.1', 'aarch64',
        '36e1b6fed5970861bd606d8f9721d31a3264388db6f5dbb4c79a33c0f992aa46'),
    'mesa': ('mesa', '27.0.0~devel+alroot.s42.1', '27.0.0~devel+alroot.s44.1', 'aarch64',
        '87b07712d61616ae978bbaaf95f1d690820a3882f9c55b0ff8cfb6a351a345cf'),
    'mesa-dev': ('mesa', '27.0.0~devel+alroot.s42.1', '27.0.0~devel+alroot.s44.1', 'all',
        '6df20b4ab28b44e73a596e482e7dcdc66d756af47f07b35a5384810c1b7db4f7'),
}
COMMITS = {'wrapper': 'b3b32abb82af4d374fd35c7192a7f524a5fc8bd3',
           'mesa': '6f90e92dfdacc4759d8e9ccb328805d0f0b4439b'}
DOCS = {'vulkan-wrapper-android': 'share/doc/vulkan-wrapper-android/alroot-build.json',
        'mesa': 'share/doc/mesa/alroot-s42-build.json'}
PROTECTED = ('vulkan-tools', 'mesa-demos', 'mesa-vulkan-icd-freedreno',
             'mesa-vulkan-icd-swrast', 'libglvnd', 'libglvnd-dev',
             'vulkan-loader', 'vulkan-loader-generic')
MEASURED_MESA = ('lib/libGLX_mesa.so', 'lib/libEGL_mesa.so', 'lib/libgallium-27.0.0-devel.so')
PROFILE_FLAGS = ('S44_WRAPPER_PROFILE', 'S44_WSI_PROFILE', 'S44_ZINK_SUBMIT_STATS')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path, algorithm='sha256'):
    result = hashlib.new(algorithm)
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def pin(path, expected):
    require(isinstance(expected, str) and re.fullmatch('[0-9a-f]{64}', expected),
            'missing SHA-256 pin for ' + str(path))
    require(not path.is_symlink() and path.is_file() and sha(path) == expected,
            'pinned input mismatch: ' + str(path))


def relative_name(name):
    path = Path(name)
    require(isinstance(name, str) and name and not path.is_absolute() and
            '..' not in path.parts and str(path) == name, 'unsafe relative path: ' + str(name))
    return path


def project_file(name):
    path = ROOT / relative_name(name)
    require(path.resolve(strict=True).is_relative_to(ROOT), 'input escapes project: ' + name)
    return path


def owned_output(path):
    require(path.is_relative_to(OUT), 'output escapes S44 root: ' + str(path))
    for candidate in (path, *path.parents):
        require(not candidate.is_symlink(), 'generated path is a symlink: ' + str(candidate))
        if candidate == ROOT:
            break


def capture(argv):
    return subprocess.check_output([str(x) for x in argv], text=True, timeout=60).strip()


def run(argv, log, env, cwd=ROOT, timeout=600):
    args = [str(x) for x in argv]
    log.write('COMMAND ' + json.dumps(args) + '\n'); log.flush()
    subprocess.run(args, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                   check=True, timeout=timeout)


def write_json(path, value):
    temporary = path.with_name(path.name + '.pending')
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
    temporary.chmod(0o644)
    os.replace(temporary, path)


def control(text):
    fields, key = {}, None
    for line in text.splitlines():
        if line.startswith((' ', '\t')) and key:
            fields[key] += '\n' + line
        elif ': ' in line:
            key, value = line.split(': ', 1); fields[key] = value
        elif line:
            raise RuntimeError('unexpected Debian control syntax')
    return fields


def entry(path):
    status = path.lstat()
    result = {'mode': stat.S_IMODE(status.st_mode)}
    if stat.S_ISLNK(status.st_mode):
        result.update(type='symlink', target=os.readlink(path))
    elif stat.S_ISREG(status.st_mode):
        result.update(type='file', size=status.st_size, sha256=sha(path))
    elif stat.S_ISDIR(status.st_mode):
        result['type'] = 'directory'
    else:
        raise RuntimeError('unsupported filesystem member: ' + str(path))
    return result


def inventory(root, payload=False):
    result = {}
    for path in sorted(root.rglob('*')):
        name = str(path.relative_to(root))
        if payload and path.relative_to(root).parts[0] == 'DEBIAN':
            continue
        result[name] = entry(path)
    return result


def leaves(items):
    return {name: data for name, data in items.items() if data['type'] != 'directory'}


def installed(package, optional=False):
    result = subprocess.run(['dpkg-query', '-s', package], text=True, capture_output=True, timeout=30)
    if result.returncode and optional:
        return None
    require(result.returncode == 0, 'package is missing: ' + package)
    fields = control(result.stdout)
    require(fields.pop('Status', None) == 'install ok installed', 'package is not configured: ' + package)
    files = {}
    for name in capture(['dpkg-query', '-L', package]).splitlines():
        path = Path(name)
        require(path.is_absolute() and '..' not in path.parts, 'unsafe dpkg path: ' + name)
        data = entry(path)
        if data['type'] == 'directory':
            require(path.is_relative_to(PREFIX) or path in PREFIX.parents, 'unexpected package directory')
        else:
            require(path.is_relative_to(PREFIX), 'package payload escapes native prefix: ' + name)
            files[str(path.relative_to('/'))] = data
    return {'control': fields, 'files': files}


def elf_dynamic(path, origin=PREFIX / 'lib'):
    with path.open('rb') as stream:
        header = stream.read(20)
    if not header.startswith(b'\x7fELF'):
        return None
    require(len(header) == 20 and header[4:6] == b'\x02\x01' and
            int.from_bytes(header[18:20], 'little') == 183, 'ELF is not native AArch64: ' + str(path))
    text = capture(['readelf', '-d', path])
    paths = re.findall(r'\((?:RUNPATH|RPATH)\).*\[([^]]*)\]', text)
    for group in paths:
        for value in group.split(':'):
            expanded = value.replace('${ORIGIN}', str(origin)).replace('$ORIGIN', str(origin))
            require(expanded and Path(expanded).is_absolute() and Path(expanded).resolve() == PREFIX / 'lib',
                    'noncanonical ELF runtime path: ' + value)
    require(not any(value in text for value in (str(ROOT), '/.source/', 'android_stub')),
            'private build path in ELF dynamic section: ' + str(path))
    return {'needed': sorted(re.findall(r'\(NEEDED\).*\[([^]]+)\]', text)),
            'soname': re.findall(r'\(SONAME\).*\[([^]]+)\]', text)}


def overlay_file(source, destination, origin=PREFIX / 'lib'):
    before, candidate = entry(destination), entry(source)
    require(before['type'] == candidate['type'], 'staged file type changed: ' + str(source))
    if before['type'] == 'symlink':
        require(before['target'] == candidate['target'], 'runtime symlink changed: ' + str(source))
        return
    require(before['type'] == 'file', 'overlay is not a payload file')
    old_dynamic, new_dynamic = elf_dynamic(destination, origin), elf_dynamic(source, origin)
    if old_dynamic is None:
        require(before['sha256'] == candidate['sha256'], 'unrelated non-ELF payload changed: ' + str(source))
    else:
        require(new_dynamic == old_dynamic, 'ELF dependencies or SONAME changed: ' + str(source))
    # copyfile keeps the exact S42 destination mode; it does not inherit a
    # private staging umask or chmod the surrounding installed-directory tree.
    shutil.copyfile(source, destination, follow_symlinks=False)
    require(stat.S_IMODE(destination.lstat().st_mode) == before['mode'], 'payload mode changed')


def source_receipt(component, expected, baseline_provenance, log, env):
    directory = OUT / component
    prepare, patches_path = directory / 'prepare-receipt.json', directory / 'patches.json'
    pin(prepare, expected['prepare_receipt_sha256'])
    pin(patches_path, expected['patches_manifest_sha256'])
    receipt, patches = json.loads(prepare.read_text()), json.loads(patches_path.read_text())
    require(receipt['baseline_source'] == str(BASE / component / 'source') and
            receipt['source'] == str(directory / 'source') and receipt['build'] == str(directory / 'build') and
            receipt['baseline_git'] == COMMITS[component] and receipt['installed_files_changed'] is False,
            'S44 independent-source receipt mismatch: ' + component)
    options = baseline_provenance['configure_options' if component == 'wrapper' else 'meson_options']
    require(receipt['options'] == options, 'S44 configure options changed from accepted S42: ' + component)
    require(isinstance(patches, dict) and patches, 'S44 tracked patch inventory is empty')
    touched = set()
    for name, digest in patches.items():
        patch = project_file(name); pin(patch, digest)
        require(name.startswith('graphics/patches/mesa/' + ('wrapper/' if component == 'wrapper' else 'zink/')),
                'unexpected component patch path: ' + name)
        for line in patch.read_text().splitlines():
            if line.startswith(('--- ', '+++ ')):
                value = line[4:].split('\t', 1)[0]
                if value == '/dev/null':
                    continue
                require(value.startswith(('a/', 'b/')), 'unrecognized patch path')
                touched.add(str(relative_name(value[2:])))
    # Reapply only the touched files in a disposable tree. This checks patch
    # order and exact selected source bytes without modifying either source.
    with tempfile.TemporaryDirectory(prefix=component + '-patch-check-', dir=OUT) as temporary:
        check = Path(temporary)
        for name in touched:
            original = BASE / component / 'source' / name
            if original.exists():
                require(original.is_file() and not original.is_symlink(), 'patched baseline is not a regular file')
                target = check / name; target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(original, target)
        for name in sorted(patches):
            run(['patch', '--batch', '--forward', '--fuzz=0', '-p1', '-i', project_file(name)], log, env, cwd=check)
        source_files = {}
        for name in sorted(touched):
            candidate = directory / 'source' / name
            require((check / name).is_file(), 'S44 packaging does not accept source deletions')
            pin(candidate, sha(check / name)); source_files[name] = sha(candidate)
    default_checks = ({
        'src/vulkan/wrapper/wrapper_device.c': (
            's44_submit_stack = !stack || strcmp(stack, "1") == 0;',
            's44_submit_profile = profile && strcmp(profile, "1") == 0;'),
        'src/vulkan/wsi/wsi_common.c': (
            'chain->s44_profile.enabled = s44_profile && !strcmp(s44_profile, "1");',)
    } if component == 'wrapper' else {
        'src/gallium/drivers/zink/zink_screen.c': (
            'screen->s44_compact_submits = debug_get_bool_option("S44_ZINK_COMPACT_SUBMITS", true);',
            'screen->s44_submit_stats = debug_get_bool_option("S44_ZINK_SUBMIT_STATS", false);')
    })
    for name, checks in default_checks.items():
        text = (directory / 'source' / name).read_text()
        require(all(text.count(value) == 1 for value in checks),
                'compiled optimization/profile defaults differ from selected policy: ' + name)
    return {'prepare': receipt, 'prepare_receipt_sha256': sha(prepare), 'patches': patches,
            'patches_manifest_sha256': sha(patches_path), 'modified_sources': source_files}


def finish_control(tree, fields, version):
    fields = dict(fields); fields['Version'] = version
    files = [p for p in tree.rglob('*') if p.is_file() and not p.is_symlink()
             and not p.is_relative_to(tree / 'DEBIAN')]
    fields['Installed-Size'] = str((sum(p.stat().st_size for p in files) + 1023) // 1024)
    (tree / 'DEBIAN/control').write_text(''.join(k + ': ' + v + '\n' for k, v in fields.items()))
    (tree / 'DEBIAN/md5sums').write_text(''.join(sha(p, 'md5') + '  ' + str(p.relative_to(tree)) + '\n'
                                             for p in sorted(files)))
    tree.chmod(0o755); (tree / 'DEBIAN').chmod(0o755)
    for name in ('control', 'md5sums'):
        (tree / 'DEBIAN' / name).chmod(0o644)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--selection', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    require(1 <= args.jobs <= 4, '--jobs must be 1..4')
    require(Path(os.environ.get('PREFIX', '')) == PREFIX and capture(['dpkg', '--print-architecture']) == 'aarch64',
            'requires native aarch64 Termux')
    selection_path = args.selection.resolve(strict=True)
    require(selection_path.is_relative_to(OUT) and not args.selection.is_symlink(), 'selection must be a regular S44 receipt')
    selection = json.loads(selection_path.read_text())
    require(selection['status'] == 'MEASURED_SELECTED' and selection['schema'] == 1, 'candidate has not been selected')
    policy = selection['default_policy']
    require(policy['activation'] == 'compiled' and
            policy['runtime_defaults'] == {'S44_WRAPPER_SUBMIT_STACK': '1', 'S44_ZINK_COMPACT_SUBMITS': '1'} and
            isinstance(policy['profiling'], dict) and set(policy['profiling']) == set(PROFILE_FLAGS) and
            all(value is False for value in policy['profiling'].values()), 'unreviewed default/profile policy')
    require(selection['evidence'], 'selected candidate needs measurement/regression evidence')
    for name, digest in selection['evidence'].items():
        pin(project_file(name), digest)
    for path in (OUT, OUT / 'package-roots', OUT / 'rollback', OUT / 'packages'):
        owned_output(path); path.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    for key in ('LD_LIBRARY_PATH', 'LD_PRELOAD'):
        env.pop(key, None)
    env.update(LC_ALL='C', DPKG_DEB_THREADS_MAX=str(args.jobs))
    with (OUT / 'package.lock').open('a') as lock, (OUT / 'package.log').open('w') as log:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        require(not capture(['dpkg', '--audit']) and not capture(['dpkg', '--verify', *OWNERS]),
                'installed dpkg state is not clean')
        before = {name: installed(name) for name in OWNERS}
        protected = {name: installed(name, optional=True) for name in PROTECTED}
        trees, baseline, metadata, original_docs, rollbacks = {}, {}, {}, {}, {}
        for name, (component, base_version, version, arch, digest) in OWNERS.items():
            fields = before[name]['control']
            require(fields['Version'] == base_version and fields['Architecture'] == arch and fields['Package'] == name,
                    'requires the accepted installed S42 owner: ' + name)
            archive = BASE / component / 'packages' / f'{name}_{base_version}_{arch}.deb'
            pin(archive, digest)
            rollback = OUT / 'rollback' / archive.name
            if not rollback.exists():
                shutil.copy2(archive, rollback); rollback.chmod(0o644)
            pin(rollback, digest); rollbacks[name] = {'path': str(rollback), 'sha256': digest, 'version': base_version}
            tree = OUT / 'package-roots' / name; owned_output(tree)
            if tree.exists():
                shutil.rmtree(tree)
            run(['dpkg-deb', '--raw-extract', rollback, tree], log, env)
            baseline[name] = inventory(tree, payload=True)
            require(leaves(baseline[name]) == before[name]['files'], 'S42 archive differs from installed owner: ' + name)
            require(control((tree / 'DEBIAN/control').read_text()) == fields, 'S42 owner control differs')
            metadata[name] = inventory(tree / 'DEBIAN'); trees[name] = tree
            if name in DOCS:
                original_docs[name] = json.loads((tree / NATIVE / DOCS[name]).read_text())
                require(original_docs[name]['package_version'] == base_version, 'S42 provenance version mismatch')
        sources = {component: source_receipt(component, selection[component],
                   original_docs['vulkan-wrapper-android' if component == 'wrapper' else 'mesa'], log, env)
                   for component in ('wrapper', 'mesa')}
        wrapper = OUT / 'wrapper/runtime/libvulkan_wrapper.so'
        pin(wrapper, selection['wrapper']['library_sha256'])
        overlay_file(wrapper, trees['vulkan-wrapper-android'] / NATIVE / 'lib/libvulkan_wrapper.so')
        staged_prefix = OUT / 'mesa/stage' / NATIVE
        owned_output(staged_prefix)
        require(all(Path(name).is_relative_to(NATIVE)
                    for name in leaves(inventory(OUT / 'mesa/stage'))),
                'Mesa staged payload outside the canonical Termux prefix')
        staged = leaves(inventory(staged_prefix))
        require(not (leaves(baseline['mesa']).keys() & leaves(baseline['mesa-dev']).keys()),
                'Mesa owner file sets overlap')
        owned_mesa = {str(Path(name).relative_to(NATIVE)): package
                      for package in ('mesa', 'mesa-dev') for name in leaves(baseline[package])}
        inherited = {DOCS['mesa'], 'share/doc/mesa/license.rst'}
        require(set(staged) == set(owned_mesa) - inherited, 'staged Mesa owner file set differs from S42')
        for name in MEASURED_MESA:
            pin(staged_prefix / name, selection['mesa']['library_sha256'][name])
        for name in staged:
            source = staged_prefix / name
            if source.is_symlink():
                require(not Path(os.readlink(source)).is_absolute() and
                        source.resolve(strict=True).is_relative_to(staged_prefix), 'stage link escapes native prefix')
            overlay_file(source, trees[owned_mesa[name]] / NATIVE / name, (PREFIX / name).parent)
        wrapper_prefix = trees['vulkan-wrapper-android'] / NATIVE
        icd = wrapper_prefix / 'share/vulkan/icd.d/wrapper_icd.aarch64.json'
        require(json.loads(icd.read_text())['ICD']['library_path'] == str(PREFIX / 'lib/libvulkan_wrapper.so'),
                'Wrapper ICD is not canonical')
        mesa_prefix = trees['mesa'] / NATIVE
        vendor = json.loads((mesa_prefix / 'share/glvnd/egl_vendor.d/50_mesa.json').read_text())
        require(vendor['ICD']['library_path'] in ('libEGL_mesa.so', 'libEGL_mesa.so.0', str(PREFIX / 'lib/libEGL_mesa.so')),
                'Mesa EGL vendor path is not canonical')
        shared = {'selection_sha256': sha(selection_path), 'default_policy': policy, 'evidence': selection['evidence'],
                  'packager_sha256': sha(Path(__file__).resolve())}
        for name, component in (('vulkan-wrapper-android', 'wrapper'), ('mesa', 'mesa')):
            doc = dict(original_docs[name]); prefix = trees[name] / NATIVE
            flag = 'S44_WRAPPER_SUBMIT_STACK' if component == 'wrapper' else 'S44_ZINK_COMPACT_SUBMITS'
            doc.update(package_version=OWNERS[name][2], previous_package_version=OWNERS[name][1],
                       previous_provenance_sha256=baseline[name][str(NATIVE / DOCS[name])]['sha256'],
                       rollback_package_sha256=OWNERS[name][4],
                       runtime_defaults={flag: policy['runtime_defaults'][flag]},
                       s44={**shared, **sources[component]})
            if component == 'wrapper':
                doc.update(version=OWNERS[name][2], library_sha256=sha(prefix / 'lib/libvulkan_wrapper.so'),
                           icd_sha256=sha(icd), wsi_common_sha256=sha(OUT / 'wrapper/source/src/vulkan/wsi/wsi_common.c'),
                           source_policy='Accepted installed S42 source plus the selected tracked S44 patches')
            else:
                files = leaves(inventory(prefix)); files.pop(DOCS[name])
                doc.update(runtime_files={p: d['sha256'] for p, d in files.items() if d['type'] == 'file'},
                           runtime_symlinks={p: d['target'] for p, d in files.items() if d['type'] == 'symlink'},
                           development_files={p: d['sha256'] for p, d in leaves(inventory(trees['mesa-dev'] / NATIVE)).items()
                                              if d['type'] == 'file'},
                           patches={**doc['patches'], **sources[component]['patches']},
                           modified_sources={**doc['modified_sources'], **sources[component]['modified_sources']})
            write_json(prefix / DOCS[name], doc)
        packages = []
        epoch = int(original_docs['mesa']['source_date_epoch']); env['SOURCE_DATE_EPOCH'] = str(epoch)
        for name, (_, _, version, arch, _) in OWNERS.items():
            tree = trees[name]
            finish_control(tree, before[name]['control'], version)
            after = inventory(tree, payload=True)
            require(after.keys() == baseline[name].keys(), 'package owner file set changed: ' + name)
            changed = []
            for path, data in after.items():
                old = baseline[name][path]
                require(data['type'] == old['type'] and data['mode'] == old['mode'], 'payload type/mode changed: ' + path)
                if data != old:
                    require(data['type'] == 'file' and (path == str(NATIVE / DOCS.get(name, '')) or
                            elf_dynamic(tree / path, (Path('/') / path).parent) is not None),
                            'unrelated package payload changed: ' + path)
                    changed.append(path)
            meta = inventory(tree / 'DEBIAN')
            require(meta.keys() == metadata[name].keys() and
                    {p for p in meta if meta[p] != metadata[name][p]} <= {'control', 'md5sums'},
                    'archived control members changed beyond version/size/md5sums')
            for path in [tree, *tree.rglob('*')]:
                os.utime(path, (epoch, epoch), follow_symlinks=False)
            package = OUT / 'packages' / f'{name}_{version}_{arch}.deb'
            pending = package.with_suffix('.deb.pending')
            run(['dpkg-deb', '--root-owner-group', '-Zxz', '-z6', '--uniform-compression', '--build', tree, pending], log, env)
            require(capture(['dpkg-deb', '-f', pending, 'Package']) == name and
                    capture(['dpkg-deb', '-f', pending, 'Version']) == version and
                    capture(['dpkg-deb', '-f', pending, 'Architecture']) == arch, 'built package identity differs')
            os.replace(pending, package)
            packages.append({'package': name, 'version': version, 'architecture': arch, 'path': str(package),
                             'sha256': sha(package), 'changed_payload_paths': changed})
        require({name: installed(name) for name in OWNERS} == before and
                {name: installed(name, optional=True) for name in PROTECTED} == protected,
                'installed owner/protected packages changed while packaging')
        pin(wrapper, selection['wrapper']['library_sha256'])
        require(leaves(inventory(staged_prefix)) == staged, 'measured Mesa stage changed while packaging')
        for component, source in sources.items():
            pin(OUT / component / 'prepare-receipt.json', source['prepare_receipt_sha256'])
            pin(OUT / component / 'patches.json', source['patches_manifest_sha256'])
            for name, digest in source['patches'].items():
                pin(project_file(name), digest)
            for name, digest in source['modified_sources'].items():
                pin(OUT / component / 'source' / name, digest)
        write_json(OUT / 'package-manifest.json', {'status': 'BUILT_NOT_INSTALLED', 'packages': packages,
                   'selection': str(selection_path), **shared, 'sources': sources, 'rollback': rollbacks,
                   'baseline_installed': before, 'protected_installed': protected,
                   'source_date_epoch': epoch, 'packaging_restripped_elves': False})
    print('S44_GRAPHICS_PACKAGES=BUILT_NOT_INSTALLED')
    print('S44_GRAPHICS_MANIFEST=' + str(OUT / 'package-manifest.json'))


if __name__ == '__main__':
    main()
