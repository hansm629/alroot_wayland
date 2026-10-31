#!/data/data/com.termux/files/usr/bin/python3
"""Reproducible five-case GPU proof for private S41 AHB padding.

Builds a pinned diagnostic from an isolated S39 source copy, then runs one
isolated S39/WM session. Default S41 requires filled-edge row equality; --s26
requires the observed source-padding difference as a negative control.
Paired --icd/--wrapper-sha256 arguments validate a package candidate with the
same filled-edge expectation, reusing the pinned diagnostic without rebuilding.
The original source/build/installed vkcube are read-only inputs. No user
windows are searched or resized; no ICD or installed program is replaced.
"""
import argparse
import fcntl
from contextlib import ExitStack
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
def load_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module
base = load_module('s40_test_helpers', 'test-s40-live-resize.py')
build = load_module('s39_build_helpers', 'build-s39-vkcube.py')
OUT = base.ROOT / '.source/gpu-stage1/s41-ahb-padding'
DIAG = OUT / 'diagnostic'
SHARED_BUILD = base.S39_OUT / 'vkcube/build'
ORIGINAL = base.S39_OUT / 'vkcube/source/cube/cube.c'
SOURCE = DIAG / 'cube-s41-padding-repro.c'
CLIENT = DIAG / 'vkcube-padding-repro'
PATCH = base.ROOT / 'graphics/tests/s41-padding-proof.patch'
PATCH_SHA = 'b9983e10729744e429c81d2769da94ee5f2582b51bccc1e20f4d692d37b968bd'
SOURCE_SHA = '31663d8f2ea4aae81223199d243ce012200e96575a33f799560dfebaeb93c724'
CLIENT_SHA = '976390a415f360f22922a09bd620c1b8594a37e77cae44dc6ed1a99692e6d0c6'
S26_SHA = '5fc1053bfeb549d31f0f748581472bb82d776d5e0ac08e0905b0fdc5fc7fbcf1'
S41_SHA = '21fdc1624cf35f957947e1e73813a6e3fbbcbb168eb18ce6bca2f96c0b03f11a'
CASES = ((301, 304), (302, 304), (303, 304), (300, 301), (301, 302))


def selected_icd(s26, custom_icd=None, wrapper_sha256=None):
    if bool(custom_icd) != bool(wrapper_sha256) or (s26 and custom_icd):
        raise RuntimeError('custom ICD/hash must be paired and cannot be combined with --s26')
    path = (custom_icd or (base.ICD if s26 else OUT / 'runtime/wrapper_icd.private.json')).resolve(strict=True)
    private = (base.ROOT / '.source').resolve()
    if custom_icd:
        if not path.is_relative_to(private) and path != base.PREFIX / 'share/vulkan/icd.d/wrapper_icd.aarch64.json':
            raise RuntimeError('custom ICD must be private project data or the canonical installed Wrapper ICD')
    elif path.parent.name != 'runtime' or not path.is_relative_to((base.ROOT / '.source/gpu-stage1').resolve()):
        raise RuntimeError('ICD must remain inside the private project runtime')
    icd_identity = base.identity(path)
    data = json.loads(path.read_text())
    raw = data.get('ICD', {}).get('library_path')
    if not isinstance(raw, str) or not raw: raise RuntimeError('invalid ICD library_path')
    library = Path(raw)
    wrapper = (library if library.is_absolute() else path.parent / library).resolve(strict=True)
    if custom_icd:
        if not wrapper.is_relative_to(private) and wrapper != base.PREFIX / 'lib/libvulkan_wrapper.so':
            raise RuntimeError('custom Wrapper must be private project data or the canonical installed library')
    elif wrapper != path.parent / 'libvulkan_wrapper.so':
        raise RuntimeError('ICD/library directory mismatch')
    wrapper_identity = base.identity(wrapper, wrapper_sha256 or (S26_SHA if s26 else S41_SHA))
    normalized = None if library.is_absolute() else {**data, 'ICD': {**data['ICD'], 'library_path': str(wrapper)}}
    return path, icd_identity, wrapper_identity, normalized


def reuse_diagnostic():
    """Keep accepted private artifacts read-only during package validation."""
    manifest = DIAG / 'build-manifest.json'
    receipt = json.loads(manifest.read_text())
    build.pinned(ORIGINAL, 'b396d52524270fce6791bbc32648eb7ac05104986f0dcce3e264399b18503059')
    build.pinned(PATCH, PATCH_SHA)
    build.pinned(SOURCE, SOURCE_SHA)
    receipt['binary'] = base.identity(CLIENT, CLIENT_SHA, executable=True)
    receipt['reused_pinned_diagnostic'] = True
    receipt['original_build_manifest'] = base.identity(manifest)
    inputs = {Path(p) for p in receipt['unchanged_inputs']} | {ORIGINAL, PATCH, SOURCE, CLIENT, manifest}
    receipt['unchanged_inputs'] = {str(p.resolve(strict=True)): base.sha256(p) for p in sorted(inputs)}
    return receipt


def build_diagnostic():
    build.pinned(ORIGINAL, 'b396d52524270fce6791bbc32648eb7ac05104986f0dcce3e264399b18503059')
    build.pinned(PATCH, PATCH_SHA)
    if DIAG.is_symlink(): raise RuntimeError('diagnostic directory is a symlink')
    DIAG.mkdir(parents=True, exist_ok=True)
    obj, dep, linkdep = DIAG / 'cube-s41-padding-repro.o', DIAG / 'cube-s41-padding-repro.o.d', DIAG / 'vkcube-padding-repro.link.d'
    pending = DIAG / 'vkcube-padding-repro.pending'
    for path in (SOURCE, obj, dep, linkdep, pending, CLIENT, DIAG / 'build.lock'):
        if path.is_symlink(): raise RuntimeError('private build output is a symlink: ' + str(path))
    with (DIAG / 'build.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        build.assert_not_running([CLIENT, pending])
        commands = subprocess.check_output(['ninja', '-C', str(SHARED_BUILD), '-t', 'commands', 'cube/vkcube'], text=True, timeout=15).splitlines()
        # Extract actual compiler/linker argv; never execute CMake shell wrappers.
        groups = []
        for line in commands:
            group = []
            for arg in shlex.split(line) + ['&&']:
                if arg == '&&':
                    if group and group != [':']: groups.append(group)
                    group = []
                else: group.append(arg)
        compiles = [c for c in groups if '-c' in c and Path(c[c.index('-c') + 1]).name == 'cube.c']
        links = [c for c in groups if '-o' in c and c[c.index('-o') + 1] == 'cube/vkcube']
        if len(compiles) != 1 or len(links) != 1: raise RuntimeError('unexpected Ninja cube compile/link commands')
        compile_cmd, link = compiles[0].copy(), links[0].copy()
        old_obj = compile_cmd[compile_cmd.index('-o') + 1]
        if link.count(old_obj) != 1: raise RuntimeError('expected exactly one original cube object')
        inputs = [ORIGINAL, SHARED_BUILD / 'cube/vkcube', SHARED_BUILD / old_obj,
                  base.PREFIX / 'bin/vkcube', base.S39_OUT / 'runtime/vkcube.coalesced']
        for path in inputs: base.identity(path)
        if '-MF' in compile_cmd: inputs.append(SHARED_BUILD / compile_cmd[compile_cmd.index('-MF') + 1])
        for i, arg in enumerate(link):
            if arg.startswith('--dependency-file='):
                inputs.append(SHARED_BUILD / arg.split('=', 1)[1]); link[i] = '--dependency-file=' + str(linkdep)
            elif arg == '--dependency-file':
                j = i + 2 if link[i + 1] == '-Xlinker' else i + 1
                inputs.append(SHARED_BUILD / link[j]); link[j] = str(linkdep)
        before = {str(path.resolve()): base.sha256(path) for path in inputs if path.is_file()}
        for flag, target in (('-c', SOURCE), ('-o', obj), ('-MF', dep), ('-MT', obj), ('-MQ', obj)):
            if flag in compile_cmd: compile_cmd[compile_cmd.index(flag) + 1] = str(target)
        # The isolated .c still includes the original cube's quoted headers.
        compile_cmd += ['-I', str(ORIGINAL.parent)]
        link[link.index(old_obj)] = str(obj); link[link.index('-o') + 1] = str(pending)
        link += ['-lxcb', '-lxcb-present']
        try:
            shutil.copyfile(ORIGINAL, SOURCE)
            with (DIAG / 'build.log').open('w') as log:
                subprocess.run(['patch', '--forward', '--batch', '--fuzz=0', str(SOURCE), '-i', str(PATCH)], check=True, stdout=log, stderr=subprocess.STDOUT, timeout=15)
                build.pinned(SOURCE, SOURCE_SHA)
                for argv in (compile_cmd, link):
                    log.write('COMMAND ' + json.dumps(argv) + '\n'); log.flush()
                    subprocess.run(argv, cwd=SHARED_BUILD, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=120)
            build.pinned(pending, CLIENT_SHA)
            build.atomic_copy(pending, CLIENT)
        finally:
            for path, expected in before.items(): base.identity(Path(path), expected)
            pending.unlink(missing_ok=True)
        receipt = {'source_sha256': SOURCE_SHA, 'patch_sha256': PATCH_SHA, 'binary': base.identity(CLIENT, CLIENT_SHA),
                   'compile': compile_cmd, 'link': link, 'unchanged_inputs': before, 'scope': 'diagnostic_client_only'}
        (DIAG / 'build-manifest.json').write_text(json.dumps(receipt, indent=2) + '\n')
        return receipt


def parse_observation(text, logical_h, target_h):
    def one(prefix):
        found = [base.values(line) for line in text.splitlines() if line.startswith(prefix)]
        if len(found) != 1:
            raise RuntimeError('missing or repeated probe marker: ' + prefix)
        return found[0]
    if 'S41_PAD_ERROR ' in text or 'S41_PAD_OBSERVATION=CAPTURED ' not in text:
        raise RuntimeError('probe did not finish its guarded readback')
    window = one('S41_PAD_WINDOW ')
    queued = one('S41_PAD_QUEUE_PRESENT ')
    complete = one('S41_PAD_PRESENT_COMPLETE ')
    captured = one('S41_PAD_CAPTURE_BEGIN ')
    physical_h = (logical_h + 3) & ~3
    if (int(queued['result']) not in (0, 1000001003) or int(complete['mode']) != 0 or
            captured['after'] != 'COPY_AND_IDLE' or captured['window'] != window['xid'] or
            captured['logical'] != f'320x{logical_h}' or captured['physical'] != f'320x{physical_h}' or
            captured['target'] != f'320x{target_h}'):
        raise RuntimeError('unexpected Present mode, completion guard, XID or extent')
    rows = {'source': [], 'window': []}
    for line in text.splitlines():
        if not line.startswith('S41_PAD_ROW '):
            continue
        fields = base.values(line)
        kind = fields['drawable']
        if kind not in rows:
            raise RuntimeError('unexpected readback drawable')
        expected_h = physical_h if kind == 'source' else target_h
        expected_xid = captured['pixmap'] if kind == 'source' else captured['window']
        matches, total = map(int, fields['matches_last_logical_row'].split('/'))
        black, black_total = map(int, fields['black_pixels'].split('/'))
        if (total != 320 or black_total != 320 or not 0 <= matches <= 320 or not 0 <= black <= 320 or
                int(fields['logical_h']) != logical_h or int(fields['actual_h']) != expected_h or
                fields['xid'] != expected_xid):
            raise RuntimeError('invalid row geometry or counters')
        rows[kind].append({'y': int(fields['y']), 'matches': matches, 'black': black,
                          'left_x5_rgb': fields['left_x5_rgb'], 'right_x314_rgb': fields['right_x314_rgb']})
    for kind, height in (('source', physical_h), ('window', target_h)):
        if [row['y'] for row in rows[kind]] != list(range(logical_h - 1, height)):
            raise RuntimeError('missing, duplicated or reordered native rows: ' + kind)
    reference = rows['source'][0]
    rgb = int(reference['left_x5_rgb'], 16)
    r, g, b = rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255
    if (reference['matches'] != 320 or reference['black'] or r != g or g != b or not 3 < r < 252 or
            reference['left_x5_rgb'] != reference['right_x314_rgb']):
        raise RuntimeError('last logical row is not the required uniform nonblack gray reference')
    padding = rows['source'][1:]
    exposed = rows['window'][1:]
    source_matches = all(row['matches'] == 320 and row['black'] == 0 for row in padding)
    window_matches = all(row['matches'] == 320 and row['black'] == 0 for row in rows['window'])
    label = ('NO_SOURCE_PADDING' if not padding else 'SOURCE_PADDING_MATCHES_LOGICAL_EDGE' if source_matches
             else 'SOURCE_PADDING_BLACK' if all(row['black'] == 320 for row in padding)
             else 'SOURCE_PADDING_DIFFERS_FROM_LOGICAL_EDGE')
    return {'logical_h': logical_h, 'target_h': target_h, 'physical_h': physical_h,
            'present_copy_serial': int(complete['serial']), 'present_msc': int(complete['msc']),
            'same_serial_idle': 'verified_by_pinned_probe_guard_before_CAPTURE_BEGIN',
            'source_padding_rows': len(padding), 'window_exposed_rows': len(exposed),
            'source_padding_matches': source_matches, 'window_rows_match': window_matches,
            'measurement': label, 'rows': rows,
            'scope': 'native source/window row values; not proof of memory initialization or physical display'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--s26', action='store_true', help='S26 negative control: require observed differing source padding')
    parser.add_argument('--icd', type=Path, help='private or canonical installed package-candidate ICD')
    parser.add_argument('--wrapper-sha256', help='exact SHA256 of the candidate Wrapper library')
    args = parser.parse_args()
    if bool(args.icd) != bool(args.wrapper_sha256):
        parser.error('--icd and --wrapper-sha256 must be supplied together')
    if args.s26 and args.icd:
        parser.error('--s26 cannot be combined with a custom ICD')
    if args.wrapper_sha256 and not re.fullmatch(r'[0-9a-f]{64}', args.wrapper_sha256):
        parser.error('--wrapper-sha256 must be 64 lowercase hexadecimal characters')
    args.expect_filled, args.expect_uninitialized = not args.s26, args.s26
    server_identity = base.identity(base.S39, base.S39_SHA, executable=True)
    icd, icd_identity, wrapper, normalized = selected_icd(args.s26, args.icd, args.wrapper_sha256)
    build_receipt = reuse_diagnostic() if args.icd else build_diagnostic()
    client = base.identity(CLIENT, CLIENT_SHA, executable=True)
    interposer = base.identity(base.INTERPOSER)
    if not (base.STATE / 'runtime/wayland-0').is_socket():
        raise RuntimeError('existing Wayland host socket unavailable')
    before = base.user_session()
    proof_out = base.ROOT / '.source/gpu-stage1/s42-default-graphics/ahb-padding' if args.icd else OUT
    logdir = proof_out / 'logs' / ('gpu-proof-' + time.strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
    logdir.mkdir(parents=True)
    loader_icd = icd
    if normalized is not None:
        loader_icd = logdir / 'loader-icd.private.json'
        loader_icd.write_text(json.dumps(normalized, indent=2) + '\n')
        loader_icd.chmod(0o600)
    loader_identity = base.identity(loader_icd)
    receipt = {'status': 'RUNNING', 'diagnostic_build': build_receipt, 'server': server_identity, 'client': client, 'icd': icd_identity,
               'wrapper': wrapper, 'interposer': interposer, 'icd_used_by_loader': loader_identity,
               'user_session_before': before, 'cases': [], 'logdir': str(logdir),
               'expectation': 'filled' if args.expect_filled else 'source_padding_difference' if args.expect_uninitialized else 'measurement_only',
               'physical_display': 'NOT_MEASURED'}
    receipt['wrapper_selection'] = {'mode': 'custom_pinned' if args.icd else 's26_control' if args.s26 else 's41_pinned',
        'requested_icd': str(args.icd) if args.icd else None,
        'expected_wrapper_sha256': args.wrapper_sha256 or (S26_SHA if args.s26 else S41_SHA)}
    server = wm = app = None
    temporary = None
    failure = None
    try:
        prefix_tmp = base.PREFIX / 'tmp'
        number = next((n for n in range(201, 241) if not any(path.exists() for path in (
            prefix_tmp / f'.X11-unix/X{n}', prefix_tmp / f'.X{n}-lock', prefix_tmp / f'.tX{n}-lock'))), None)
        if number is None:
            raise RuntimeError('no free isolated X display')
        display = f':{number}'
        temporary = Path(tempfile.mkdtemp(prefix='s41-padding-proof-', dir=prefix_tmp))
        auth = temporary / 'Xauthority'; auth.touch(mode=0o600)
        base.command(['xauth', '-f', str(auth), 'add', display, 'MIT-MAGIC-COOKIE-1', secrets.token_hex(16)], check=True)
        server_env = base.clean_environment()
        server_env.update(base.SERVER_FLAGS)
        server_env.update(S39_KGSL_RESIZE_ORIGIN='1', S39_KGSL_KEEP_UNRELATED_ROOT_POOL='1',
            LD_PRELOAD=str(base.INTERPOSER), S16_XSHMFENCE_TRACE='0', XDG_RUNTIME_DIR=str(base.STATE / 'runtime'),
            WAYLAND_DISPLAY='wayland-0', XAUTHORITY=str(auth), TMPDIR=str(prefix_tmp), EGL_PLATFORM='surfaceless',
            LD_LIBRARY_PATH=str(base.ROOT / 'packages/prefix/lib'))
        client_env = base.clean_environment()
        client_env.update(DISPLAY=display, XAUTHORITY=str(auth), VK_DRIVER_FILES=str(loader_icd),
            VK_ICD_FILENAMES=str(loader_icd), S24_WRAPPER_XCB_AHB_PAD_HEIGHT='1', S26_WRAPPER_XCB_RETRY_TRANSIENT='1')
        wm_env = server_env.copy()
        wm_env.pop('WAYLAND_DISPLAY', None); wm_env.pop('LD_PRELOAD', None)
        wm_env.update(DISPLAY=display, GDK_BACKEND='x11', XDG_SESSION_TYPE='x11',
            XDG_CONFIG_HOME=str(temporary / 'config'), XDG_CACHE_HOME=str(temporary / 'cache'))
        receipt.update(display=display, wm_ld_preload=wm_env.get('LD_PRELOAD'), client_ld_preload=client_env.get('LD_PRELOAD'))
        with ExitStack() as stack:
            def log(name): return stack.enter_context((logdir / name).open('w'))
            server = subprocess.Popen([server_identity['path'], display, '-geometry', '1920x1080', '-dpi', '96',
                '-auth', str(auth), '-glamor', 'es', '-nolisten', 'tcp'], env=server_env,
                stdout=log('server.log'), stderr=subprocess.STDOUT, start_new_session=True)
            base.ensure_ready(server, ['xdpyinfo'], client_env)
            wm = subprocess.Popen(['dbus-run-session', '--', 'xfwm4', '--compositor=on', '--display', display],
                env=wm_env, stdout=log('xfwm.log'), stderr=subprocess.STDOUT, start_new_session=True)
            base.ensure_ready(wm, ['xprop', '-root', '_NET_SUPPORTING_WM_CHECK'], client_env, 'window id #')
            receipt.update(server_pid=server.pid, wm_pid=wm.pid)
            for logical_h, target_h in CASES:
                case = {'logical_h': logical_h, 'target_h': target_h, 'status': 'RUNNING'}
                receipt['cases'].append(case)
                path = logdir / f'gpu-{logical_h}-to-{target_h}.log'
                case['log'] = str(path)
                env = {**client_env, 'S41_PAD_REPRO': '1', 'S41_PAD_LOGICAL_H': str(logical_h), 'S41_PAD_TARGET_H': str(target_h)}
                try:
                    base.identity(Path(wrapper['path']), wrapper['sha256'])
                    base.identity(loader_icd, loader_identity['sha256'])
                    base.identity(Path(client['path']), client['sha256'], executable=True)
                    app = subprocess.Popen([client['path'], '--wsi', 'xcb'], env=env,
                        stdout=log(path.name), stderr=subprocess.STDOUT, start_new_session=True)
                    case['pid'] = app.pid
                    app.wait(timeout=20)
                    case['exit_code'] = app.returncode
                    if app.returncode:
                        raise RuntimeError('GPU diagnostic exited with ' + str(app.returncode))
                    case.update(parse_observation(path.read_text(), logical_h, target_h))
                    if args.expect_filled and not (case['source_padding_matches'] and case['window_rows_match']):
                        raise RuntimeError('required filled-edge row equality was not observed')
                    if args.expect_uninitialized and case['source_padding_rows'] and case['source_padding_matches']:
                        raise RuntimeError('negative-control source padding difference was not observed')
                    case['status'] = 'OBSERVED'
                except Exception as error:
                    case.update(status='FAIL', error=str(error))
                    failure = failure or error
                finally:
                    base.stop_owned(app); app = None
                if server.poll() is not None or wm.poll() is not None:
                    raise RuntimeError('owned server/WM exited between cases')
            faults = (logdir / 'server.log').read_text(errors='replace')
            if re.search(r'invalid object|Fatal server error|Segmentation fault|failed to dispatch Wayland events', faults):
                raise RuntimeError('owned Xwayland fault in log')
            receipt['status'] = 'FAIL' if failure else 'OBSERVATION_COMPLETE'
    except BaseException as error:
        receipt.update(status='FAIL', error=str(error)); failure = failure or error
    finally:
        for proc in (app, wm, server):
            try: base.stop_owned(proc)
            except Exception as error:
                receipt.setdefault('cleanup_errors', []).append(str(error)); failure = failure or error
        if temporary is not None: shutil.rmtree(temporary, ignore_errors=True)
        try:
            after = base.user_session()
            receipt.update(user_session_after=after, user_session_unchanged=after == before)
            if after != before: raise RuntimeError('user desktop session changed')
        except Exception as error:
            receipt.update(user_session_unchanged=False, user_session_error=str(error)); failure = failure or error
        try:
            for path, expected in build_receipt['unchanged_inputs'].items(): base.identity(Path(path), expected)
            receipt['shared_inputs_unchanged'] = True
        except Exception as error:
            receipt['shared_inputs_unchanged'] = False; failure = failure or error
        if failure: receipt['status'] = 'FAIL'
        (logdir / 'summary.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print('S41_GPU_PADDING_PROOF=' + receipt['status'])
        print('S41_SUMMARY=' + str(logdir / 'summary.json'))
        for case in receipt['cases']:
            print(json.dumps({key: case[key] for key in ('logical_h', 'target_h', 'status', 'measurement',
                'source_padding_matches', 'window_rows_match', 'error') if key in case}, sort_keys=True))
    if failure: raise failure


if __name__ == '__main__':
    try: main()
    except Exception as error:
        print('S41_GPU_PADDING_ERROR=' + str(error)[:400], file=sys.stderr)
        raise SystemExit(2)
