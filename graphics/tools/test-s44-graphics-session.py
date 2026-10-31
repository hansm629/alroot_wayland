#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Adapt the unchanged S42/S43 gates for explicitly pinned S44 driver bytes.

Run only in native Termux. Creates an owned Xwayland/XFWM session; never
installs packages, restarts the live desktop, or operates on its windows.
The installed S43 vkcube runs with no WSI argument and closes through XRes.
Present counts remain X11 COPY completions, not physical display FPS.
"""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from types import SimpleNamespace

sys.dont_write_bytecode = True
ROOT = Path('/data/data/com.termux/files/home/alroot_wayland')
PREFIX = Path('/data/data/com.termux/files/usr')
S44 = ROOT / '.source/gpu-stage1/s44-sync-overhead'
HISTORICAL = {
    'graphics/tools/test-s42-graphics-session.py': 'd89dca83086d8f0604a81a9a37dc6325233ed0e4c445ae3c2fb1f422f2063e7a',
    'graphics/tools/test-s40-live-resize.py': '0d6211a08f758c3cafa43480c77916e14960c156e9d536e91604f9297ca6317f',
    'graphics/tools/test-s43-vkcube-default.py': '6287ad16a42c613f3f0bd867dcca09de28e677055069cc4aa3200e9014fd2eca',
    'graphics/tests/s43-close-owned-window.c': '003fa43866a6ea886352309f19562dd83775efe35e36ecc8a5bfa7f7661f0539',
}
CLIENT_SHA = {
    'vkcube': 'f848eae71659918458a2ea6c17a01f9e0551939a4bf4d324c774831c09bac64c',
    'zink': '4f5444726d455d522ebaf37b17760febc028db4f6653dcd237517913c836e086',
}
MESA_ROLES = {
    'egl': 'lib/libEGL_mesa.so', 'glx': 'lib/libGLX_mesa.so',
    'gallium': 'lib/libgallium-27.0.0-devel.so', 'gbm': 'lib/libgbm.so',
    'dril': 'lib/dri/libdril_dri.so', 'zink': 'lib/dri/zink_dri.so',
    'kgsl': 'lib/dri/kgsl_dri.so',
}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def identity(path, expected=None):
    path = Path(path).resolve(strict=True)
    require(path.is_file(), 'not a file: ' + str(path))
    value = digest(path)
    require(expected is None or value == expected, 'pinned hash mismatch: ' + str(path))
    return {'path': str(path), 'sha256': value}


def read_json(path):
    return json.loads(Path(path).read_text())


def relative_file(prefix, name):
    relative = Path(name)
    require(not relative.is_absolute() and '..' not in relative.parts,
            'invalid Mesa identity relative path: ' + str(name))
    path = (prefix / relative).resolve(strict=True)
    require(path.is_relative_to(prefix), 'Mesa identity escapes selected prefix: ' + str(name))
    return path


def selected_runtime(args, run_dir):
    manifest = read_json(args.mesa_identity)
    prefix = Path(manifest['prefix']).resolve(strict=True)
    if args.mode == 'staged':
        require(prefix.is_relative_to(S44.resolve()) and prefix != PREFIX,
                'private Mesa prefix must be inside the S44 work area')
    else:
        require(prefix == PREFIX, 'installed Mesa must use the canonical Termux prefix')
    files = manifest.get('runtime_files')
    require(isinstance(files, dict) and files, 'Mesa identity has no runtime_files')
    canonical = {}
    for relative, expected in files.items():
        require(isinstance(expected, str) and re.fullmatch(r'[0-9a-f]{64}', expected),
                'invalid Mesa SHA256 for ' + relative)
        path = relative_file(prefix, relative)
        entry = identity(path, expected)
        require(str(path) not in canonical or canonical[str(path)] == entry,
                'conflicting hashes for a Mesa alias')
        canonical[str(path)] = entry
    links = {}
    for relative, target in manifest.get('runtime_symlinks', {}).items():
        relative_file(prefix, relative)
        path = prefix / relative
        require(path.is_symlink() and os.readlink(path) == target,
                'Mesa symlink mismatch: ' + relative)
        links[relative] = target
    libraries = {}
    for role, relative in MESA_ROLES.items():
        path = relative_file(prefix, relative)
        require(str(path) in canonical, 'missing pinned resolved Mesa runtime: ' + relative)
        libraries[role] = canonical[str(path)]
        alias = prefix / relative
        links[relative] = os.readlink(alias) if alias.is_symlink() else None
    icd = Path(args.wrapper_icd).resolve(strict=True)
    library_name = Path(read_json(icd)['ICD']['library_path'])
    require(library_name.is_absolute(), 'selected Wrapper ICD must use an absolute library path')
    library = library_name.resolve(strict=True)
    if args.mode == 'staged':
        require(icd.is_relative_to(S44.resolve()) and library.is_relative_to(S44.resolve()),
                'private Wrapper ICD/library must be inside the S44 work area')
    else:
        require(icd == PREFIX / 'share/vulkan/icd.d/wrapper_icd.aarch64.json'
                and library == PREFIX / 'lib/libvulkan_wrapper.so',
                'installed Wrapper must use canonical paths')
    libraries['wrapper'] = identity(library, args.wrapper_sha256)
    if args.mode == 'staged':
        egl = run_dir / 'egl-vendor.private.json'
        content = json.dumps({'file_format_version': '1.0.0',
                             'ICD': {'library_path': str(prefix / MESA_ROLES['egl'])}}, indent=2) + '\n'
        if egl.exists():
            require(egl.read_text() == content, 'private EGL manifest changed')
        else:
            egl.write_text(content)
            egl.chmod(0o600)
    else:
        egl = prefix / 'share/glvnd/egl_vendor.d/50_mesa.json'
        egl_library = read_json(egl)['ICD']['library_path']
        require(egl_library in ('libEGL_mesa.so', 'libEGL_mesa.so.0', str(prefix / MESA_ROLES['egl'])),
                'installed EGL vendor manifest does not select Mesa')
    return {'prefix': str(prefix), 'libraries': libraries, 'all_pinned_files': canonical,
            'runtime_links': links, 'icd': identity(icd), 'egl_manifest': identity(egl),
            'mesa_identity': identity(args.mesa_identity), 'mesa_version': '27.0.0-devel',
            'package_version': manifest.get('package_version'),
            'baseline_version': manifest.get('baseline_version')}


def historical_snapshot():
    return {relative: identity(ROOT / relative, expected)
            for relative, expected in HISTORICAL.items()}


def installed_snapshot(base):
    packages = base.command(['dpkg-query', '-W', '-f=${Package}\t${Status}\t${Version}\t${Architecture}\n',
                             'mesa', 'vulkan-wrapper-android', 'vulkan-tools'], check=True).stdout
    require(len(packages.splitlines()) == 3 and packages.count('install ok installed') == 3,
            'required installed graphics packages are unavailable')
    paths = [PREFIX / name for name in MESA_ROLES.values()]
    paths += [PREFIX / name for name in ('lib/libvulkan_wrapper.so',
        'share/vulkan/icd.d/wrapper_icd.aarch64.json', 'share/glvnd/egl_vendor.d/50_mesa.json',
        'bin/vkcube', 'bin/glxgears', 'share/doc/vulkan-wrapper-android/alroot-build.json',
        'share/doc/vulkan-tools/alroot-build.json')]
    paths += list((PREFIX / 'share/doc/mesa').glob('alroot-*-build.json'))
    paths += [ROOT / 'termux/awstart', ROOT / 'termux/graphics-defaults.sh']
    return {'packages': packages, 'files': {str(path): {
        **identity(path), 'symlink': os.readlink(path) if path.is_symlink() else None}
        for path in paths}}


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def adapt(s42, s43, args, selected, run_dir, summary):
    base = s42.base
    original_clean, original_command = s42.clean_environment, base.command
    original_build, original_find, original_stop = base.build_helpers, base.find_window, base.stop_owned
    helper = run_dir / 'runtime/s43-close-owned-window'
    state = {}
    gates = {}
    for key, value in (('S44_WRAPPER_SUBMIT_STACK', args.wrapper_submit_stack),
                       ('S44_ZINK_COMPACT_SUBMITS', args.zink_compact_submits)):
        if value != 'default':
            gates[key] = value
    if args.profile:
        gates.update(S44_WRAPPER_PROFILE='1', S44_WSI_PROFILE='1', S44_ZINK_SUBMIT_STATS='1')

    def clean_environment():
        env = original_clean()
        env.pop('WAYLAND_DEBUG', None)
        # Both transports are reachable, so plain S43 vkcube must honor X11
        # session AUTO preference instead of succeeding due to an absent socket.
        env.update(XDG_SESSION_TYPE='x11', XDG_RUNTIME_DIR=str(base.STATE / 'runtime'),
                   WAYLAND_DISPLAY='wayland-0', **gates)
        return env

    def build_helpers(options, logdir):
        artifacts = original_build(options, logdir)
        if args.demo == 'vkcube':
            source = ROOT / 'graphics/tests/s43-close-owned-window.c'
            result = original_command(['clang', '--target=aarch64-linux-android35', '-O2', '-std=gnu11',
                '-Wall', '-Wextra', '-Werror', str(source), '-lxcb', '-lxcb-res', '-o', str(helper)],
                clean_environment(), timeout=45)
            (logdir / 's43-close-owned-window.build.log').write_text(result.stdout + result.stderr)
            require(result.returncode == 0, 'S43 XRes helper build failed')
            artifacts['s43-close-owned-window'] = {'source': identity(source), 'binary': identity(helper)}
        return artifacts

    def launch(argv, **kwargs):
        if str(argv[0]) == str(args.client_path):
            env = dict(kwargs['env'])
            require(not env.get('LD_PRELOAD'), 'server interposer leaked into client')
            if args.demo == 'vkcube':
                require(list(argv[1:]) == ['--wsi', 'xcb'], 'historical client invocation changed')
                argv = [str(args.client_path)]
                env['WAYLAND_DEBUG'] = 'client'
            kwargs['env'] = env
            state['client_log'] = Path(kwargs['stdout'].name)
            summary['actual_client_arguments'] = list(argv[1:])
            summary['s44_client_environment'] = {key: env.get(key) for key in (
                'DISPLAY', 'XDG_SESSION_TYPE', 'WAYLAND_DISPLAY', 'XDG_RUNTIME_DIR',
                'S44_WRAPPER_SUBMIT_STACK', 'S44_ZINK_COMPACT_SUBMITS',
                'S44_WRAPPER_PROFILE', 'S44_WSI_PROFILE', 'S44_ZINK_SUBMIT_STATS')}
            proc = subprocess.Popen(argv, **kwargs)
            state['app'] = proc
            return proc
        return subprocess.Popen(argv, **kwargs)

    def find_window(env, before, pid):
        if args.demo != 'vkcube':
            return original_find(env, before, pid)
        found = []
        for xid in base.own_windows(env) - before:
            probe = original_command([str(helper), '--inspect', xid, str(pid)], env)
            if probe.returncode == 0:
                found.append((xid, json.loads(probe.stdout)))
        if len(found) != 1:
            return None
        xid, inspection = found[0]
        require(inspection.get('ownership') == 'xres_local_client_pid'
                and inspection.get('client_pid') == pid, 'XRes ownership result mismatch')
        summary['s43_xres_inspection'] = inspection
        state.update(xid=xid, pid=pid)
        return xid

    def command(argv, env=None, timeout=4, check=False):
        if args.demo == 'vkcube' and list(argv[:3]) == ['xdotool', 'key', '--window']:
            require(len(argv) == 5 and argv[3] == state.get('xid') and argv[4] == 'Escape',
                    'unexpected mutating window request')
            app = state['app']
            require(app.pid == state['pid'] and app.poll() is None, 'owned client already exited')
            text = state['client_log'].read_text(errors='replace')
            require('Selected WSI platform: xcb' in text and 'get_xdg_surface' not in text,
                    'plain S43 vkcube AUTO no longer selects only XCB')
            result = original_command([str(helper), state['xid'], str(app.pid)], env, timeout=timeout, check=True)
            close = json.loads(result.stdout)
            require(close.get('ownership') == 'xres_local_client_pid'
                    and close.get('close_request') == 'SENT', 'invalid XRes close result')
            app.wait(timeout=6)
            require(app.returncode == 0 and state['xid'] not in base.own_windows(env),
                    'WM close failed to exit normally and remove the owned window')
            summary['s43_default_wsi_close'] = {'status': 'PASS', 'selected_wsi': 'xcb',
                'arguments': [], 'close': close, 'exit_code': app.returncode,
                'wayland_surface_created': False}
            return result
        return original_command(argv, clean_environment() if env is None else env, timeout, check)

    def stop_owned(proc):
        # Do not signal an already-reaped process group after PID reuse.
        if proc.poll() is None:
            original_stop(proc)

    s42.OUT = run_dir
    s42.selected_runtime = lambda unused: selected
    s42.clean_environment = clean_environment
    # A local module proxy avoids replacing subprocess.Popen process-wide.
    s42.subprocess = SimpleNamespace(Popen=launch, STDOUT=subprocess.STDOUT)
    base.build_helpers, base.find_window = build_helpers, find_window
    base.command, base.stop_owned = command, stop_owned
    summary['s44_overrides'] = gates
    summary['profile_enabled'] = args.profile


def run(args):
    histories = historical_snapshot()
    s42 = load_module('s44_s42_helpers', ROOT / 'graphics/tools/test-s42-graphics-session.py')
    s43 = load_module('s44_s43_helpers', ROOT / 'graphics/tools/test-s43-vkcube-default.py')
    require(s42.base.ROOT == ROOT and s42.base.PREFIX == PREFIX, 'historical environment constants changed')
    out = S44 / 'regression-tests'
    out.mkdir(parents=True, exist_ok=True)
    with (out / 'runner.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        run_dir = out / f'{args.mode}-{args.demo}-{time.strftime("%Y%m%d-%H%M%S")}-{os.getpid()}'
        run_dir.mkdir()
        summary = {'status': 'RUNNING', 'mode': args.mode, 'demo': args.demo, 'historical_inputs': histories,
                   'physical_display': 'NOT_MEASURED', 'session_window_scope': 'owned isolated Xwayland only'}
        failure = None
        selected = before = installed = None
        try:
            before = s43.session_stamp()
            summary['session_before'] = before
            installed = installed_snapshot(s42.base)
            summary['installed_before'] = installed
            selected = selected_runtime(args, run_dir)
            summary['runtime'] = selected
            summary['client'] = s42.base.identity(args.client_path, args.client_sha256, executable=True)
            if args.verify_only:
                summary['verification_scope'] = 'identity and session preflight only; GPU gates not run'
                summary['status'] = 'PREFLIGHT_PASS'
            else:
                adapt(s42, s43, args, selected, run_dir, summary)
                s42.run(argparse.Namespace(mode=args.mode, demo=args.demo, server_only=False,
                    drag_ms=args.drag_ms, client_path=args.client_path, client_sha256=args.client_sha256))
                receipts = list((run_dir / 'logs').glob('*/summary.json'))
                require(len(receipts) == 1, 'missing or ambiguous S42 session result')
                session = read_json(receipts[0])
                require(session['status'] == 'PASS', 'inherited S42 regression failed')
                summary['session_gates'] = session
                summary['session_summary'] = str(receipts[0])
                if args.demo == 'vkcube':
                    require(summary.get('s43_default_wsi_close', {}).get('status') == 'PASS',
                            'S43 default WSI/XRes-close gate was not executed')
                summary['status'] = 'PASS'
        except BaseException as exc:
            failure = exc
            summary.update(status='FAIL', error=f'{type(exc).__name__}: {exc}')
        finally:
            receipts = list((run_dir / 'logs').glob('*/summary.json'))
            if len(receipts) == 1:
                summary['session_summary'] = str(receipts[0])
                summary['session_gates'] = read_json(receipts[0])
            for name, verify in (
                ('historical_inputs_unchanged', lambda: historical_snapshot() == histories),
                ('session_unchanged', lambda: before is not None and s43.session_stamp() == before),
                ('installed_unchanged', lambda: installed is not None and installed_snapshot(s42.base) == installed),
                ('selected_runtime_unchanged', lambda: selected is not None and selected_runtime(args, run_dir) == selected)):
                try:
                    require(verify(), name + ' failed')
                    summary[name] = True
                except Exception as exc:
                    summary[name] = False
                    summary.setdefault('verification_errors', []).append(str(exc))
                    summary['status'], failure = 'FAIL', failure or exc
            (run_dir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
            print('S44_SESSION=' + summary['status'], flush=True)
            print('S44_SESSION_SUMMARY=' + str(run_dir / 'summary.json'), flush=True)
        if failure:
            raise failure


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('staged', 'installed'), required=True)
    parser.add_argument('--mesa-identity', type=Path, required=True,
                        help='JSON with absolute prefix and runtime_files relative-path:SHA256 pins')
    parser.add_argument('--wrapper-icd', type=Path, required=True)
    parser.add_argument('--wrapper-sha256', required=True)
    parser.add_argument('--demo', choices=('vkcube', 'zink'), required=True)
    parser.add_argument('--drag-ms', type=int, default=6000)
    parser.add_argument('--client-path', type=Path)
    parser.add_argument('--client-sha256')
    parser.add_argument('--wrapper-submit-stack', choices=('0', '1', 'default'), default='1')
    parser.add_argument('--zink-compact-submits', choices=('0', '1', 'default'), default='1')
    parser.add_argument('--profile', action='store_true', help='enable aggregate diagnostic counters; normally off')
    parser.add_argument('--verify-only', action='store_true', help='read-only identity/session preflight; no GPU processes')
    args = parser.parse_args()
    if bool(args.client_path) != bool(args.client_sha256):
        parser.error('--client-path and --client-sha256 must be supplied together')
    args.client_path = args.client_path or PREFIX / 'bin' / ('vkcube' if args.demo == 'vkcube' else 'glxgears')
    args.client_sha256 = args.client_sha256 or CLIENT_SHA[args.demo]
    args.client_path = args.client_path.resolve(strict=True)
    for value in (args.wrapper_sha256, args.client_sha256):
        if not re.fullmatch(r'[0-9a-f]{64}', value):
            parser.error('SHA256 arguments must be 64 lowercase hexadecimal characters')
    if not 6000 <= args.drag_ms <= 12000:
        parser.error('--drag-ms must be 6000..12000')
    return args


if __name__ == '__main__':
    try:
        run(parse_args())
    except Exception as exc:
        print('S44_SESSION_ERROR=' + str(exc)[:500], file=sys.stderr)
        raise SystemExit(2)
