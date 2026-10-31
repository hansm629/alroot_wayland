#!/usr/bin/env python3
"""Exercise actual awstart dispatch against an inert project CLI.

Usage: test-awstart-preflight.py OLD_AWSTART NEW_AWSTART SHARED_GUARD
Only the fixed PROJECT assignment is redirected to a temporary fixture.
HOME, PREFIX and launcher flow are not rewritten. The CLI stub records its
first argument then exits immediately; no desktop or Android helper can start.
The old launcher must reach the stop stub for a malformed profile, proving
the new launcher's negative check distinguishes the original regression.
"""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    old, new, guard = [Path(arg).resolve(strict=True) for arg in sys.argv[1:]]
    shell = shutil.which('bash')
    if not shell:
        raise SystemExit('bash unavailable')
    search_path = os.environ.get('PATH', os.defpath)
    results = []
    with tempfile.TemporaryDirectory(prefix='alroot-awstart-preflight-') as temporary:
        project = Path(temporary)
        termux = project / 'termux'
        termux.mkdir()
        calls = project / 'cli-calls.txt'
        cli = termux / 'alroot-wayland'
        cli.write_text('#!' + shell + '\nset -eu\n'
                       'printf "%s\\n" "$1" >> "$ALROOT_TEST_CLI_CALLS"\n'
                       'exit 79\n')
        cli.chmod(0o700)
        shutil.copyfile(guard, termux / 'xwayland-gpu-profile.sh')
        binary = project / '.source/gpu-stage1/s38-frame-continuity/runtime/Xwayland'
        binary.parent.mkdir(parents=True)
        binary.write_text('#!' + shell + '\nexit 99\n')
        binary.chmod(0o700)
        interposer = project / '.source/gpu-stage27-pr25/s16-xwayland-gpu/xshmfence/libalroot_xshmfence_dualabi.so'
        interposer.parent.mkdir(parents=True)
        interposer.write_bytes(b'inert profile-guard fixture\n')
        launchers = {}
        for name, source in (('old', old), ('new', new)):
            text = source.read_text()
            assignment = 'PROJECT="$HOME/alroot_wayland"'
            if text.count(assignment) != 1:
                raise AssertionError('unexpected launcher project assignment')
            target = project / ('awstart-' + name)
            target.write_text(text.replace(assignment, 'PROJECT=' + shlex.quote(str(project))))
            launchers[name] = target
        base = {'PATH': search_path, 'LANG': 'C', 'ALROOT_TEST_CLI_CALLS': str(calls)}
        profile = {
            'ALROOT_XWAYLAND_BINARY': str(binary), 'ALROOT_XWAYLAND_GLAMOR': 'es',
            'ALROOT_XWAYLAND_DUAL_ABI_FENCE': '1', 'S30_KGSL_TINY_AHB_FD0': '1',
            'S32_KGSL_NO_BLANK_PRESENT': '1', 'S36_KGSL_ROOT_AHB_OUTPUT': '1',
            'S37_KGSL_THIN_AHB_FD0': '1', 'S38_KGSL_FRAME_LIFETIME': '1',
        }
        malformed = {**profile, 'S39_KGSL_RESIZE_ORIGIN': '1'}

        def check(name, version, flags, args, expected_code, expected_calls):
            calls.unlink(missing_ok=True)
            result = subprocess.run([shell, str(launchers[version]), *args],
                                    env={**base, **flags}, capture_output=True,
                                    text=True, timeout=4)
            observed = calls.read_text().splitlines() if calls.exists() else []
            row = {'name': name, 'exit_code': result.returncode, 'cli_calls': observed}
            results.append(row)
            if result.returncode != expected_code or observed != expected_calls:
                raise AssertionError(json.dumps(row) + ': ' + result.stderr.strip())

        check('negative-control-old-stops-before-reject', 'old', malformed,
              ['--restart', '--direct', '--no-topapp'], 79, ['stop'])
        check('new-mixed-profile-blocked-before-stop', 'new', malformed,
              ['--restart', '--direct', '--no-topapp'], 1, [])
        check('new-mixed-profile-start-blocked', 'new', malformed,
              ['--start', '--direct', '--no-topapp'], 1, [])
        check('new-valid-profile-still-dispatches-restart', 'new', profile,
              ['--restart', '--direct', '--no-topapp'], 79, ['stop'])
        check('new-valid-profile-dry-run', 'new', profile,
              ['--restart', '--direct', '--no-topapp', '--dry-run'], 0, [])
        check('new-default-topapp-dry-run', 'new', {},
              ['--restart', '--dry-run'], 0, [])
        check('new-help-ignores-invalid-profile', 'new', malformed, ['--help'], 0, [])
        interposer.unlink()
        check('new-missing-interposer-blocked-before-stop', 'new', profile,
              ['--restart', '--direct', '--no-topapp'], 1, [])
    print(json.dumps({'status': 'PASS', 'cases': len(results),
                      'actual_desktop_stopped': False, 'results': results}, indent=2))


if __name__ == '__main__':
    main()
