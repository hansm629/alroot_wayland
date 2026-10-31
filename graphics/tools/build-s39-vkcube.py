#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: Apache-2.0
"""Build private baseline/coalesced vkcube 1.4.365 clients; never install globally.

The candidate fixes vkcube's XCB event loop, not the Vulkan ICD or Xwayland.
Source and build paths are fixed to retain the S39 build's reproducibility.
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
OUT = ROOT / '.source/gpu-stage1/s39-live-resize'
WORK = OUT / 'vkcube'
SOURCE = WORK / 'source'
BUILD = WORK / 'build'
RUNTIME = OUT / 'runtime'
PATCHDIR = ROOT / 'graphics/patches/vulkan-tools/1.4.365'
MANIFEST = OUT / 'vkcube-build-manifest.json'
INSTALLED = Path('/data/data/com.termux/files/usr/bin/vkcube')
ARCHIVE_URL = 'https://github.com/KhronosGroup/Vulkan-Tools/archive/refs/tags/v1.4.365.tar.gz'
ARCHIVE_SHA = 'c55db29b4b2a4d222053ddcc7f925b8cad7f16f5640a05d4ee16d44a9dc02aac'
TERMUX_COMMIT = 'a882692cbdfb101ab2b677b5d9f7623380ede5f3'
PATCHES = (
    ('0001-no-android.patch', '536127d4f61c3d9104275b0b9b764ea0020f3e42d342150dc8ac5d6bf6d15c56'),
    ('0002-do-not-search-KHR_display-automatically-on-Termux.patch',
     '58c790ba111cbade12f19e23489c22ae1b991c1f124daea12d2bb8fdb615b135'),
    ('0003-revert-vk-khr-display.patch', '0f462e7d7082ecbdae385471206465b20d2902b844e1cd295c3f0eff7d5edc06'),
    ('0004-xcb-configure-coalesce.patch', '037cf8859d6d3b1288b88389fa0da221eb0f79ec5c7aef91e60736ee818a0216'),
)
SOURCE_SHA = {
    'upstream': 'c609168cdc3c42dd03a011950c971c2b6b926d02a882e340a1311f842e0e4a59',
    'baseline': 'aa9f90b24cfdb5ad98fd0c0dfd412f4d9abcc8df829f56b769d8ac64f7dba319',
    'coalesced': 'b396d52524270fce6791bbc32648eb7ac05104986f0dcce3e264399b18503059',
}
REFERENCE_BINARIES = {
    'baseline': '2a4c55dc54b14ede27acdb9e88eace5d00c64c20580187ea8e02ceaf2c5cd56b',
    'coalesced': '0050e7126c8d096cd14a7e0b1026b7063f898e33eb7be5b80116991cee57c208',
}
CMAKE_OPTIONS = (
    '-G', 'Ninja', '-DCMAKE_SYSTEM_NAME=Linux', '-DCMAKE_BUILD_TYPE=Release',
    '-DBUILD_CUBE=ON', '-DBUILD_VULKANINFO=OFF', '-DBUILD_ICD=OFF', '-DBUILD_TESTS=OFF',
    '-DBUILD_WSI_XCB_SUPPORT=ON', '-DBUILD_WSI_XLIB_SUPPORT=ON',
    '-DBUILD_WSI_WAYLAND_SUPPORT=OFF', '-DBUILD_WSI_DISPLAY_SUPPORT=OFF',
)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda: f.read(1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()


def pinned(path, expected):
    if path.is_symlink() or not path.is_file() or digest(path) != expected:
        raise RuntimeError('pinned input mismatch: ' + str(path))


def assert_not_running(paths):
    """Also recognize hardlinks and executables unlinked after their launch."""
    names = {str(p.absolute()) for p in paths}
    identities = set()
    for path in paths:
        if path.is_symlink():
            raise RuntimeError('refusing a private executable symlink: ' + str(path))
        if path.exists():
            st = path.stat()
            identities.add((st.st_dev, st.st_ino))
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        exe = proc / 'exe'
        try:
            target = os.readlink(exe).removesuffix(' (deleted)')
            if target in names:
                raise RuntimeError('private vkcube is running; finish its test first: pid=' + proc.name)
            st = exe.stat()
        except (FileNotFoundError, ProcessLookupError):
            continue
        except OSError as error:
            try:
                same_uid = proc.stat().st_uid == os.getuid()
            except (FileNotFoundError, ProcessLookupError):
                continue
            if same_uid:
                raise RuntimeError('cannot inspect same-UID process; refusing private binary replacement: pid='
                                   + proc.name) from error
            continue
        if (st.st_dev, st.st_ino) in identities:
            raise RuntimeError('private vkcube is running; finish its test first: pid=' + proc.name)


def atomic_copy(source, destination):
    assert_not_running([destination])
    with tempfile.NamedTemporaryFile(prefix=destination.name + '.staging-',
                                     dir=destination.parent, delete=False) as f:
        staging = Path(f.name)
    try:
        shutil.copyfile(source, staging)
        staging.chmod(0o755)
        if digest(staging) != digest(source):
            raise RuntimeError('private binary copy mismatch')
        assert_not_running([destination])
        os.replace(staging, destination)
    finally:
        staging.unlink(missing_ok=True)


def atomic_manifest(receipt):
    with tempfile.NamedTemporaryFile(mode='w', prefix='vkcube-manifest-',
                                     dir=OUT, delete=False) as f:
        path = Path(f.name)
        json.dump(receipt, f, indent=2)
        f.write('\n')
    try:
        os.replace(path, MANIFEST)
    finally:
        path.unlink(missing_ok=True)


def get_archive(given):
    archive = given.resolve() if given else WORK / 'Vulkan-Tools-v1.4.365.tar.gz'
    if not archive.exists() and given:
        raise RuntimeError('specified source archive does not exist: ' + str(archive))
    if not archive.exists():
        with tempfile.NamedTemporaryFile(prefix='vulkan-tools-download-', dir=WORK,
                                         delete=False) as f:
            staging = Path(f.name)
            try:
                with urlopen(ARCHIVE_URL, timeout=30) as response:
                    shutil.copyfileobj(response, f)
                f.flush()
                pinned(staging, ARCHIVE_SHA)
                os.replace(staging, archive)
            finally:
                staging.unlink(missing_ok=True)
    pinned(archive, ARCHIVE_SHA)
    return archive


def fresh_source(archive):
    # These two directories contain only generated S39 build inputs/outputs.
    for path in (SOURCE, BUILD):
        if path.is_symlink():
            raise RuntimeError('refusing a generated-directory symlink: ' + str(path))
    with tempfile.TemporaryDirectory(prefix='unpack-', dir=WORK) as temp:
        temp = Path(temp)
        with tarfile.open(archive, 'r:gz') as tar:
            tar.extractall(temp, filter='data')
        unpacked = temp / 'Vulkan-Tools-1.4.365'
        pinned(unpacked / 'cube/cube.c', SOURCE_SHA['upstream'])
        if len(list(temp.iterdir())) != 1:
            raise RuntimeError('unexpected source archive layout')
        for path in (SOURCE, BUILD):
            if path.exists():
                shutil.rmtree(path)
        os.replace(unpacked, SOURCE)


def run(argv, log, timeout=600):
    log.write('COMMAND ' + json.dumps([str(a) for a in argv]) + '\n')
    log.flush()
    subprocess.run([str(a) for a in argv], cwd=SOURCE, check=True,
                   stdout=log, stderr=subprocess.STDOUT, timeout=timeout)


def tool_version(name):
    result = subprocess.check_output([name, '--version'], text=True, timeout=10)
    return result.splitlines()[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, help='reuse the pinned official release archive')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--activate', action='store_true',
                        help='atomically copy the candidate to the private runtime/vkcube path')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error('--jobs must be between 1 and 32')
    for name, sha in PATCHES:
        pinned(PATCHDIR / name, sha)
    if not INSTALLED.is_file():
        raise RuntimeError('installed Termux vkcube is missing')
    installed_before = digest(INSTALLED)
    protected = [BUILD / 'cube/vkcube'] + [RUNTIME / name for name in
                                          ('vkcube', 'vkcube.baseline', 'vkcube.coalesced')]
    assert_not_running(protected)
    WORK.mkdir(parents=True, exist_ok=True)
    RUNTIME.mkdir(parents=True, exist_ok=True)
    (OUT / 'logs').mkdir(parents=True, exist_ok=True)
    with (WORK / 'build.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        assert_not_running(protected)
        receipt = {
            'schema': 1, 'component': 'private-vkcube', 'version': '1.4.365',
            'license': 'Apache-2.0', 'scope': 'XCB event-loop coalescing in vkcube only',
            'status': 'building', 'validation': 'build_only',
            'started_utc': datetime.now(timezone.utc).isoformat(),
            'source_dir': str(SOURCE), 'build_dir': str(BUILD),
            'archive': {'url': ARCHIVE_URL, 'sha256': ARCHIVE_SHA},
            'termux_package_commit': TERMUX_COMMIT,
            'patches': [{'file': name, 'sha256': sha} for name, sha in PATCHES],
            'cube_source_sha256': SOURCE_SHA,
            'cmake_options': list(CMAKE_OPTIONS),
            'tools': {name: tool_version(name) for name in ('cc', 'c++', 'cmake', 'ninja')},
            'installed_vkcube': {'path': str(INSTALLED), 'sha256_before': installed_before},
            'binaries': {}, 'canonical_activated': False,
        }
        atomic_manifest(receipt)
        try:
            archive = get_archive(args.archive)
            receipt['archive']['local_path'] = str(archive)
            assert_not_running(protected)
            fresh_source(archive)
            with (OUT / 'logs/vkcube-build.log').open('w') as log:
                for name, _ in PATCHES[:3]:
                    run(['patch', '--forward', '--batch', '--fuzz=0', '-p1', '-i', PATCHDIR / name], log, 30)
                pinned(SOURCE / 'cube/cube.c', SOURCE_SHA['baseline'])
                run(['cmake', '-S', SOURCE, '-B', BUILD, *CMAKE_OPTIONS], log)
                for variant in ('baseline', 'coalesced'):
                    if variant == 'coalesced':
                        run(['patch', '--forward', '--batch', '--fuzz=0', '-p1', '-i', PATCHDIR / PATCHES[3][0]], log, 30)
                        pinned(SOURCE / 'cube/cube.c', SOURCE_SHA['coalesced'])
                    assert_not_running([BUILD / 'cube/vkcube'])
                    run(['cmake', '--build', BUILD, '--target', 'vkcube', '--parallel', str(args.jobs)], log)
                    binary = BUILD / 'cube/vkcube'
                    target = RUNTIME / ('vkcube.' + variant)
                    if binary.is_symlink() or not binary.is_file():
                        raise RuntimeError('expected private build output is missing')
                    atomic_copy(binary, target)
                    sha = digest(target)
                    receipt['binaries'][variant] = {
                        'path': str(target), 'sha256': sha,
                        'reference_sha256': REFERENCE_BINARIES[variant],
                        'matches_reference': sha == REFERENCE_BINARIES[variant],
                    }
            if digest(INSTALLED) != installed_before:
                raise RuntimeError('installed vkcube changed during private build')
            if args.activate:
                atomic_copy(RUNTIME / 'vkcube.coalesced', RUNTIME / 'vkcube')
                receipt['canonical_activated'] = True
                receipt['canonical_path'] = str(RUNTIME / 'vkcube')
                receipt['canonical_sha256'] = digest(RUNTIME / 'vkcube')
            receipt['status'] = 'success'
        except Exception as error:
            receipt['status'] = 'failed'
            receipt['error'] = str(error)
            raise
        finally:
            installed_after = digest(INSTALLED) if INSTALLED.is_file() else None
            receipt['installed_vkcube']['sha256_after'] = installed_after
            receipt['installed_vkcube']['unchanged'] = installed_after == installed_before
            if installed_after != installed_before:
                receipt['status'] = 'failed'
            receipt['finished_utc'] = datetime.now(timezone.utc).isoformat()
            atomic_manifest(receipt)
        if receipt['status'] != 'success':
            raise RuntimeError('private build did not preserve installed vkcube')
    print('S39_PRIVATE_VKCUBE_BUILD=PASS')
    print('S39_INSTALLED_VKCUBE_UNCHANGED=PASS')
    for variant, data in receipt['binaries'].items():
        print('S39_' + variant.upper() + '_SHA256=' + data['sha256'])
        print('S39_' + variant.upper() + '_MATCHES_REFERENCE=' + str(data['matches_reference']))
    print('S39_VKCUBE_MANIFEST=' + str(MANIFEST))


if __name__ == '__main__':
    main()
