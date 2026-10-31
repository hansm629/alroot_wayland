#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Build pinned private Mesa demos 9.0.0 glxgears variants; never install them.

The candidate batches X events before drawing; Mesa, the ICD and Xwayland are
unchanged. --dry-run validates local inputs and prints the plan without writes.
"""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / '.source/gpu-stage1/s40-demo-events'
SOURCE, RUNTIME = OUT / 'source', OUT / 'runtime'
ARCHIVE = OUT / 'upstream/mesa-demos-9.0.0.tar.xz'
PATCH = ROOT / 'graphics/patches/mesa-demos/9.0.0/0001-glxgears-coalesce-xevents.patch'
MANIFEST = OUT / 'glxgears-build-manifest.json'
INSTALLED = Path('/data/data/com.termux/files/usr/bin/glxgears')
ARCHIVE_URL = 'https://archive.mesa3d.org/demos/mesa-demos-9.0.0.tar.xz'
ARCHIVE_SHA = '3046a3d26a7b051af7ebdd257a5f23bfeb160cad6ed952329cdff1e9f1ed496b'
PATCH_SHA = '5ec33f1017fe6f53e402b148130542734471118a3456f672a00b20a367bffd85'
SOURCE_SHA = {
    'baseline': '344a03aff01708350d90603fd6b841bccd295157670f519b459bbf3874acf847',
    'coalesced': 'd4ca3fe86499fc4618dcba03332351d99e35bcee281c6dac60a0904f0f6ac3a3',
}
BINARY_SHA = {
    'baseline': 'b0a7c8225211cc61467e0ae18db12a99efd01d1c8ba383c887fbd6469eea203f',
    'coalesced': '4f5444726d455d522ebaf37b17760febc028db4f6653dcd237517913c836e086',
}
COMPILE_FLAGS = ['-O2', '-std=gnu11', '-Wall', '-Wextra']
LINK_FLAGS = ['-lGL', '-lX11', '-lm']


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def pinned(path, expected):
    if path.is_symlink() or not path.is_file() or digest(path) != expected:
        raise RuntimeError('pinned input/output mismatch: ' + str(path))


def assert_not_running(paths):
    """Reject running targets, including hardlinks and deleted executables."""
    names, identities = {str(path.absolute()) for path in paths}, set()
    for path in paths:
        if path.is_symlink():
            raise RuntimeError('refusing a private executable symlink: ' + str(path))
        if path.exists():
            st = path.stat()
            identities.add((st.st_dev, st.st_ino))
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            exe = proc / 'exe'
            target = os.readlink(exe).removesuffix(' (deleted)')
            st = exe.stat()
            if target in names or (st.st_dev, st.st_ino) in identities:
                raise RuntimeError('private glxgears is running: pid=' + proc.name)
        except (FileNotFoundError, ProcessLookupError):
            continue
        except OSError as error:
            try:
                same_uid = proc.stat().st_uid == os.getuid()
            except (FileNotFoundError, ProcessLookupError):
                continue
            if same_uid:
                raise RuntimeError('cannot inspect same-UID process: pid=' + proc.name) from error


def atomic_write(destination, data, mode=0o644, executable=False):
    with tempfile.NamedTemporaryFile(prefix=destination.name + '.staging-',
                                     dir=destination.parent, delete=False) as file:
        staging = Path(file.name)
        file.write(data)
    try:
        staging.chmod(mode)
        if executable:
            assert_not_running([destination])
        os.replace(staging, destination)
    finally:
        staging.unlink(missing_ok=True)


def receipt_write(receipt):
    atomic_write(MANIFEST, (json.dumps(receipt, indent=2) + '\n').encode())


def get_archive(given):
    archive = given.resolve() if given else ARCHIVE
    if given and not archive.exists():
        raise RuntimeError('specified archive is missing: ' + str(archive))
    if not archive.exists():
        with tempfile.NamedTemporaryFile(prefix='mesa-demos-download-', dir=ARCHIVE.parent,
                                         delete=False) as file:
            staging = Path(file.name)
            try:
                with urlopen(ARCHIVE_URL, timeout=30) as response:
                    shutil.copyfileobj(response, file)
                file.flush()
                pinned(staging, ARCHIVE_SHA)
                os.replace(staging, archive)
            finally:
                staging.unlink(missing_ok=True)
    pinned(archive, ARCHIVE_SHA)
    return archive


def run(argv, log, timeout=90):
    log.write('COMMAND ' + json.dumps([str(arg) for arg in argv]) + '\n')
    log.flush()
    subprocess.run([str(arg) for arg in argv], cwd=SOURCE, check=True,
                   stdout=log, stderr=subprocess.STDOUT, timeout=timeout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, help='reuse the pinned official release archive')
    parser.add_argument('--dry-run', action='store_true', help='read-only input checks and build plan')
    args = parser.parse_args()
    pinned(PATCH, PATCH_SHA)
    if not INSTALLED.is_file():
        raise RuntimeError('installed Termux glxgears is missing')
    installed_before = digest(INSTALLED)
    targets = {variant: RUNTIME / ('glxgears.' + variant) for variant in SOURCE_SHA}
    protected = [*targets.values(), RUNTIME / 'glxgears']
    assert_not_running(protected)
    for path in (OUT, SOURCE, RUNTIME, ARCHIVE.parent, OUT / 'logs'):
        if path.is_symlink():
            raise RuntimeError('refusing a generated-directory symlink: ' + str(path))
    selected_archive = args.archive.resolve() if args.archive else ARCHIVE
    if args.archive or selected_archive.exists():
        pinned(selected_archive, ARCHIVE_SHA)
    plan = {'component': 'private-glxgears', 'version': '9.0.0',
            'archive': {'url': ARCHIVE_URL, 'path': str(selected_archive), 'sha256': ARCHIVE_SHA},
            'patch': {'path': str(PATCH), 'sha256': PATCH_SHA},
            'source_sha256': SOURCE_SHA, 'expected_binary_sha256': BINARY_SHA,
            'compile_flags': COMPILE_FLAGS, 'link_flags': LINK_FLAGS,
            'targets': {key: str(path) for key, path in targets.items()},
            'installed_glxgears': {'path': str(INSTALLED), 'sha256_before': installed_before}}
    if args.dry_run:
        print(json.dumps({'dry_run': True, 'writes': False, **plan}, indent=2))
        return
    for path in (SOURCE, RUNTIME, ARCHIVE.parent, OUT / 'logs'):
        path.mkdir(parents=True, exist_ok=True)
    with (OUT / 'glxgears-build.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        assert_not_running(protected)
        receipt = {'schema': 1, **plan, 'status': 'building', 'validation': 'build_only',
                   'started_utc': datetime.now(timezone.utc).isoformat(), 'binaries': {}}
        receipt_write(receipt)
        try:
            archive = get_archive(args.archive)
            with tarfile.open(archive, 'r:xz') as tar:
                member = tar.getmember('mesa-demos-9.0.0/src/xdemos/glxgears.c')
                if not member.isfile() or member.size > 1024 * 1024:
                    raise RuntimeError('unexpected upstream glxgears archive member')
                with tar.extractfile(member) as source:
                    upstream = source.read()
            if hashlib.sha256(upstream).hexdigest() != SOURCE_SHA['baseline']:
                raise RuntimeError('upstream glxgears source mismatch')
            sources = {variant: SOURCE / ('glxgears.' + variant + '.c') for variant in SOURCE_SHA}
            for path in sources.values():
                atomic_write(path, upstream)
            with (OUT / 'logs/glxgears-build.log').open('w') as log:
                run(['patch', '--forward', '--batch', '--fuzz=0', sources['coalesced'], '-i', PATCH], log, 15)
                receipt['compiler'] = subprocess.check_output(['clang', '--version'], text=True,
                                                              timeout=10).splitlines()[0]
                with tempfile.TemporaryDirectory(prefix='glxgears-build-', dir=OUT) as temporary:
                    built = {}
                    for variant, source in sources.items():
                        pinned(source, SOURCE_SHA[variant])
                        output = Path(temporary) / ('glxgears.' + variant)
                        run(['clang', *COMPILE_FLAGS, source, *LINK_FLAGS, '-o', output], log)
                        pinned(output, BINARY_SHA[variant])
                        built[variant] = output
                    # Both variants must compile and reproduce before publishing either.
                    assert_not_running(protected)
                    for variant, output in built.items():
                        atomic_write(targets[variant], output.read_bytes(), mode=0o755, executable=True)
                        pinned(targets[variant], BINARY_SHA[variant])
                        receipt['binaries'][variant] = {'path': str(targets[variant]),
                            'sha256': digest(targets[variant]), 'matches_reference': True}
            if digest(INSTALLED) != installed_before:
                raise RuntimeError('installed glxgears changed during private build')
            receipt['status'] = 'success'
        except Exception as error:
            receipt.update(status='failed', error=str(error))
            raise
        finally:
            installed_after = digest(INSTALLED) if INSTALLED.is_file() else None
            receipt['installed_glxgears'].update(sha256_after=installed_after,
                                                unchanged=installed_after == installed_before)
            if installed_after != installed_before:
                receipt['status'] = 'failed'
            receipt['finished_utc'] = datetime.now(timezone.utc).isoformat()
            receipt_write(receipt)
        if receipt['status'] != 'success':
            raise RuntimeError('private build did not preserve installed glxgears')
    print('S40_PRIVATE_GLXGEARS_BUILD=PASS')
    print('S40_INSTALLED_GLXGEARS_UNCHANGED=PASS')
    for variant, data in receipt['binaries'].items():
        print('S40_GLXGEARS_' + variant.upper() + '_SHA256=' + data['sha256'])
    print('S40_GLXGEARS_MANIFEST=' + str(MANIFEST))


if __name__ == '__main__':
    main()
