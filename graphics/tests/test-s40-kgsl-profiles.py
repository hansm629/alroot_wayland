#!/usr/bin/env python3
"""Check launch-profile compatibility without starting a desktop or GPU client.

Usage: python test-kgsl-profile-matrix.py /absolute/path/xwayland-gpu-profile.sh
All executables and libraries below are temporary inert fixtures. The guard
must inspect them, never execute/load them. Intended to cover the S39 launch
regression and ensure its repair preserves the existing S28..S38 profiles.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    guard = Path(sys.argv[1]).resolve(strict=True)
    shell = shutil.which('bash')
    if not shell:
        raise SystemExit('bash is unavailable')
    search_path = os.environ.get('PATH', os.defpath)
    directories = {
        'base': 'xwayland-kgsl/build/hw/xwayland',
        's28': 's28-kgsl-resize-preserve/runtime',
        's29': 's29-native-resize/runtime',
        's30': 's30-min-extent/runtime',
        's31': 's31-resize-continuity/runtime',
        's32': 's32-no-blank-present/runtime',
        's36': 's36-ahb-output/runtime',
        's37': 's37-thin-ahb/runtime',
        's38': 's38-frame-continuity/runtime',
        's39': 's39-live-resize/runtime',
    }
    tiny = {'S30_KGSL_TINY_AHB_FD0': '1'}
    nonblank = {**tiny, 'S32_KGSL_NO_BLANK_PRESENT': '1'}
    ahb = {**nonblank, 'S36_KGSL_ROOT_AHB_OUTPUT': '1'}
    thin = {**ahb, 'S37_KGSL_THIN_AHB_FD0': '1'}
    lifetime = {**thin, 'S38_KGSL_FRAME_LIFETIME': '1'}
    continuity = {**lifetime, 'S39_KGSL_RESIZE_ORIGIN': '1',
                  'S39_KGSL_KEEP_UNRELATED_ROOT_POOL': '1'}
    profiles = {
        'base': {},
        's28': {'S28_KGSL_RESIZE_PRESERVE': '1'},
        's29': {'S29_KGSL_NATIVE_RESIZE': '1'},
        's30': {**tiny, 'S30_KGSL_STABLE_FRAME': '1'},
        's31': {**tiny, 'S31_KGSL_RESIZE_CONTENT_POLICY': '3'},
        's32': nonblank, 's36': ahb, 's37': thin,
        's38': lifetime, 's39': continuity,
    }
    results = []
    with tempfile.TemporaryDirectory(prefix='alroot-profile-matrix-') as temporary:
        project = Path(temporary)
        binaries = {}
        for stage, directory in directories.items():
            binary = project / '.source/gpu-stage1' / directory / 'Xwayland'
            binary.parent.mkdir(parents=True)
            binary.write_text('#!/bin/sh\nexit 99\n')
            binary.chmod(0o700)
            binaries[stage] = binary
        interposer = project / '.source/gpu-stage27-pr25/s16-xwayland-gpu/xshmfence/libalroot_xshmfence_dualabi.so'
        interposer.parent.mkdir(parents=True)
        interposer.write_bytes(b'inert guard-only fixture\n')

        def check(name, stage, flags, expected, glamor='es', binary=None):
            environment = {'PATH': search_path, 'LANG': 'C', **flags}
            result = subprocess.run([shell, str(guard), str(binary or binaries[stage]),
                                     glamor, str(project)], env=environment,
                                    capture_output=True, text=True, timeout=3)
            actual = result.returncode == 0
            results.append({'name': name, 'allowed': actual, 'expected': expected,
                            'exit_code': result.returncode})
            if actual != expected:
                raise AssertionError(name + ': ' + json.dumps(results[-1]) +
                                     ': ' + result.stderr.strip())

        for stage, flags in profiles.items():
            check('valid-' + stage, stage, flags, True)
        for key in continuity:
            flags = continuity.copy()
            del flags[key]
            check('s39-missing-' + key, 's39', flags, False)
        for key in ('S39_KGSL_RESIZE_ORIGIN', 'S39_KGSL_KEEP_UNRELATED_ROOT_POOL'):
            check('s38-mixed-' + key, 's38', {**lifetime, key: '1'}, False)
        check('s39-old-snapshot-policy', 's39',
              {**continuity, 'S31_KGSL_RESIZE_CONTENT_POLICY': '3'}, False)
        check('s39-wrong-glamor', 's39', continuity, False, glamor='off')
        unknown = project / 'unregistered-Xwayland'
        unknown.write_text('#!/bin/sh\nexit 99\n')
        unknown.chmod(0o700)
        check('unknown-executable-path', 's39', continuity, False, binary=unknown)
        binaries['s39'].chmod(0o600)
        check('s39-not-executable', 's39', continuity, False)
        binaries['s39'].chmod(0o700)
        interposer.unlink()
        check('missing-interposer', 's39', continuity, False)
        interposer.touch()
        check('empty-interposer', 's39', continuity, False)

        # Exercise the real rollback dispatch before its Android-only checks.
        # A stub lower-stage starter verifies arguments and inherited flags;
        # no awstart, getprop, Xwayland, or process termination is executed.
        starter = guard.parent.parent / 'graphics/tools/start-s39-live-resize.sh'
        fixture_starter = project / 'graphics/tools/start-s39-live-resize.sh'
        fixture_starter.parent.mkdir(parents=True)
        shutil.copyfile(starter, fixture_starter)
        lower = fixture_starter.with_name('start-s38-frame-lifetime.sh')
        lower.write_text('''#!/bin/bash
set -eu
[[ $# == 2 && "$1" == --restart && "$2" == --dry-run ]] || exit 21
[[ "${S39_KGSL_RESIZE_ORIGIN:-0}" == 0 ]] || exit 22
[[ "${S39_KGSL_KEEP_UNRELATED_ROOT_POOL:-0}" == 0 ]] || exit 23
[[ "${S39_RESIZE_WORK_TRACE:-0}" == 0 ]] || exit 24
''')
        result = subprocess.run([shell, str(fixture_starter), '--restore-s38', '--dry-run'],
                                env={**{key: os.environ[key] for key in ('HOME',)
                                        if key in os.environ},
                                     'PATH': search_path, 'LANG': 'C',
                                     'S39_KGSL_RESIZE_ORIGIN': '1',
                                     'S39_KGSL_KEEP_UNRELATED_ROOT_POOL': '1',
                                     'S39_RESIZE_WORK_TRACE': '1'},
                                capture_output=True, text=True, timeout=3)
        results.append({'name': 'real-s39-rollback-clears-flags-and-forwards-dry-run',
                        'allowed': result.returncode == 0, 'expected': True,
                        'exit_code': result.returncode})
        if result.returncode:
            raise AssertionError('rollback dispatch: ' + result.stderr.strip())
    print(json.dumps({'status': 'PASS', 'cases': len(results),
                      'desktop_started': False, 'results': results}, indent=2))


if __name__ == '__main__':
    main()
