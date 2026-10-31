#!/data/data/com.termux/files/usr/bin/python3
"""Bounded S40 demo resize regression on a disposable Xwayland/WM.

Defaults: pinned S39 server, unchanged S26 ICD, private coalesced clients for
both vkcube and glxgears, twelve seconds of WM dragging at 8 ms steps. Only
Xwayland receives the live-session fence interposer. Counts are X Present COPY
completions; they do not measure Android physical frames or prove rotation.
Every helper is built before any test process starts. No installed binaries,
user desktop settings, or user XIDs are modified.
"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path('/data/data/com.termux/files/home/alroot_wayland')
PREFIX = Path('/data/data/com.termux/files/usr')
STATE = ROOT.parent / '.alroot-wayland'
S39_OUT = ROOT / '.source/gpu-stage1/s39-live-resize'
OUT = ROOT / '.source/gpu-stage1/s40-resize-diagnostics'
DIAG_SOURCE = ROOT / 'graphics/tests'
GLX_RUNTIME = ROOT / '.source/gpu-stage1/s40-demo-events/runtime'
INTERPOSER = ROOT / '.source/gpu-stage27-pr25/s16-xwayland-gpu/xshmfence/libalroot_xshmfence_dualabi.so'
S39 = S39_OUT / 'runtime/Xwayland'
S39_SHA = 'c5f6c9a512f84e31f60ec52a9f10e689e0746f831049e57a4223ade2750f3899'
S38 = ROOT / '.source/gpu-stage1/s38-frame-continuity/runtime/Xwayland'
S38_SHA = '3f27886fae2bde560d34ca53dedacf4cec09fd9dbcddc591efafc4ec9bc994c2'
CLIENT_SHA = {
    'vkcube': {
        'baseline': '2a4c55dc54b14ede27acdb9e88eace5d00c64c20580187ea8e02ceaf2c5cd56b',
        'coalesced': '0050e7126c8d096cd14a7e0b1026b7063f898e33eb7be5b80116991cee57c208',
    },
    'zink': {
        'baseline': 'b0a7c8225211cc61467e0ae18db12a99efd01d1c8ba383c887fbd6469eea203f',
        'coalesced': '4f5444726d455d522ebaf37b17760febc028db4f6653dcd237517913c836e086',
    },
}
ICD = ROOT / '.source/gpu-stage1/s26-wrapper-resize-retry/runtime/wrapper_icd.private.json'
MESA = ROOT / '.source/gpu-stage27-pr25/mesa-private/glx-runtime/lib'
SERVER_FLAGS = {
    'ALROOT_XWAYLAND_DUAL_ABI_FENCE': '1',
    'XWAYLAND_FORCE_KGSL_SURFACELESS': '1', 'FD_KGSL_ENABLE_DMABUF': '1',
    'MESA_LOADER_DRIVER_OVERRIDE': 'kgsl', 'GALLIUM_DRIVER': 'freedreno',
    'S30_KGSL_TINY_AHB_FD0': '1', 'S32_KGSL_NO_BLANK_PRESENT': '1',
    'S36_KGSL_ROOT_AHB_OUTPUT': '1', 'S37_KGSL_THIN_AHB_FD0': '1',
    'S38_KGSL_FRAME_LIFETIME': '1',
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def identity(path, expected=None, executable=False):
    path = path.resolve(strict=True)
    if not path.is_file() or (executable and not os.access(path, os.X_OK)):
        raise RuntimeError('missing or non-executable file: ' + str(path))
    digest = sha256(path)
    if expected is not None and digest != expected:
        raise RuntimeError('pinned hash mismatch: ' + str(path))
    return {'path': str(path), 'sha256': digest}


def command(args, env=None, timeout=4, check=False):
    result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=timeout)
    if check and result.returncode:
        raise RuntimeError(f'command failed ({result.returncode}): {args[0]}: ' +
                           result.stderr.strip()[-500:])
    return result


def user_session():
    status = command(['awstart', '--status'], timeout=8, check=True).stdout
    return {key: match.group(1) for key in ('xwayland', 'xfce')
            if (match := re.search(r'^' + key + r':\s*(.+)$', status, re.M))}


def own_windows(env):
    result = command(['xprop', '-root', '_NET_CLIENT_LIST'], env, check=True)
    return set(re.findall(r'0x[0-9a-fA-F]+', result.stdout))


def find_window(env, before, app_pid):
    found = []
    for xid in own_windows(env) - before:
        result = command(['xprop', '-id', xid, 'WM_STATE', '_NET_WM_PID'], env)
        if result.returncode or 'window state: Normal' not in result.stdout:
            continue
        owner = re.search(r'_NET_WM_PID\(CARDINAL\) = (\d+)', result.stdout)
        if owner and int(owner.group(1)) != app_pid:
            continue
        found.append(xid)
    return found[0] if len(found) == 1 else None


def stop_owned(proc):
    if proc is None:
        return
    # All supplied Popen objects were created here with start_new_session=True.
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        proc.wait(timeout=3)


def ensure_ready(proc, args, env, marker=None):
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError('owned server/WM exited before ready')
        try:
            result = command(args, env, timeout=2)
            if result.returncode == 0 and (marker is None or marker in result.stdout):
                return
        except subprocess.TimeoutExpired:
            pass
        time.sleep(.10)
    raise RuntimeError('owned server/WM readiness timeout')


def clean_environment():
    env = os.environ.copy()
    exact = {'DISPLAY', 'WAYLAND_DISPLAY', 'LD_PRELOAD', 'LD_LIBRARY_PATH',
             'DBUS_SESSION_BUS_ADDRESS', 'SESSION_MANAGER', 'VK_DRIVER_FILES',
             'VK_ICD_FILENAMES', 'MESA_LOADER_DRIVER_OVERRIDE', 'GALLIUM_DRIVER',
             'LIBGL_DRIVERS_PATH', 'LIBGL_ALWAYS_SOFTWARE', 'LIBGL_KOPPER_DRI2',
             'MESA_VK_WSI_DEBUG', 'MESA_VK_WSI_PRESENT_MODE',
             'WRAPPER_BLIT', 'WRAPPER_MAX_IMAGE_COUNT'}
    for key in list(env):
        if key in exact or re.match(r'^S(?:1\d|2\d|3\d|4\d)_', key):
            env.pop(key, None)
    return env


def build_helpers(args, logdir):
    sources = {
        's39-present-origin': ['-lxcb', '-lxcb-present'],
        's39-present-observer': ['-lxcb', '-lxcb-present'],
        's40-interactive-resize': ['-lX11', '-lXtst'],
        's39-direct-resize': ['-lX11'],
    }
    if args.capture:
        sources['s40-content-pair'] = ['-lX11', '-lXcomposite']
    if args.thin:
        sources['s30-vkcube-animation' if args.demo == 'vkcube'
                else 's32-gears-color-probe'] = ['-lX11']
    artifacts = {}
    for name, libraries in sources.items():
        source = DIAG_SOURCE / (name + '.c')
        output = OUT / 'runtime' / name
        temporary_output = output.with_name(output.name + f'.build-{os.getpid()}')
        try:
            result = command(['clang', '--target=aarch64-linux-android35', '-O2', '-std=gnu11',
                              '-Wall', '-Wextra', '-Werror', str(source), *libraries,
                              '-o', str(temporary_output)], timeout=45)
            (logdir / (name + '.build.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError('helper build failed: ' + name)
            temporary_output.replace(output)
        finally:
            temporary_output.unlink(missing_ok=True)
        artifacts[name] = {'source': identity(source), 'binary': identity(output, executable=True)}
    return artifacts


def values(line):
    return dict(re.findall(r'([a-zA-Z_][a-zA-Z_0-9]*)=([^\s]+)', line))


def present_metrics(text, resize_start_ms, resize_end_ms):
    ready = next((values(line) for line in text.splitlines()
                  if line.startswith('S39_PRESENT_READY ')), None)
    tail = next((values(line) for line in reversed(text.splitlines())
                 if line.startswith('S39_PRESENT_SUMMARY ')), None)
    if not ready or 'start_monotonic_ms' not in ready or not tail:
        raise RuntimeError('missing observer timestamps or complete summary')
    if int(tail['observer_error']):
        raise RuntimeError('observer reported an X11 error')
    origin = int(ready['start_monotonic_ms'])
    previous_elapsed = 0
    bins = []
    for line in text.splitlines():
        if not line.startswith('S39_PRESENT '):
            continue
        fields = values(line)
        elapsed = int(fields['elapsed_ms'])
        if elapsed < previous_elapsed:
            raise RuntimeError('observer timestamps are not monotonic')
        if elapsed == previous_elapsed:
            if int(fields['completes']) or int(fields['configure']):
                raise RuntimeError('nonempty zero-duration observer bin')
            continue
        row = {'start_ms': origin + previous_elapsed, 'end_ms': origin + elapsed,
               'span_ms': elapsed - previous_elapsed,
               **{key: int(fields[key]) for key in
                  ('completes', 'copy', 'flip', 'skip', 'other', 'configure',
                   'since_last_complete_ms')}}
        row['copy_fps'] = row['copy'] * 1000.0 / row['span_ms']
        bins.append(row)
        previous_elapsed = elapsed
    lower, upper = resize_start_ms + 500, resize_end_ms - 100
    core = [row for row in bins if row['start_ms'] >= lower and row['end_ms'] <= upper]
    if len(core) < 3:
        raise RuntimeError('insufficient complete middle resize observation bins')
    span = sum(row['span_ms'] for row in core)
    copies = sum(row['copy'] for row in core)
    return {
        'measurement': 'X Present COPY completion, not Android physical FPS or rotation proof',
        'observer_start_ms': origin, 'core_start_bound_ms': lower, 'core_end_bound_ms': upper,
        'core_bins': core, 'all_bins': bins, 'core_copy_count': copies,
        'core_span_ms': span, 'core_copy_fps': copies * 1000.0 / span,
        'core_zero_copy_bins': sum(row['copy'] == 0 for row in core),
        'core_configure_count': sum(row['configure'] for row in core),
        'total_configure': int(tail['configure']), 'total_complete': int(tail['completes']),
        'max_complete_receive_gap_ms': int(tail['max_receive_gap_ms']),
        'pre_resize_copy_count': sum(row['copy'] for row in bins if row['end_ms'] <= resize_start_ms),
        'post_resize_copy_count': sum(row['copy'] for row in bins if row['start_ms'] >= resize_end_ms),
    }


def thin_check(xid, env, app, server, demo, logdir):
    results = []
    for width, height in ((200, 1), (200, 2), (400, 1), (500, 500)):
        command(['xdotool', 'windowsize', xid, str(width), str(height)], env, check=True)
        time.sleep(.08)
        if app.poll() is not None or server.poll() is not None:
            raise RuntimeError('owned client/server exited during thin-size trial')
        geometry = command(['xdotool', 'getwindowgeometry', '--shell', xid], env, check=True).stdout
        actual = {key: int(value) for key, value in re.findall(r'^(WIDTH|HEIGHT)=(\d+)$', geometry, re.M)}
        results.append({'requested': [width, height], 'actual': actual})
        if actual != {'WIDTH': width, 'HEIGHT': height}:
            raise RuntimeError('requested thin size was not applied to the owned client')
    time.sleep(.5)
    probe = OUT / 'runtime' / ('s30-vkcube-animation' if demo == 'vkcube' else 's32-gears-color-probe')
    result = command([str(probe), xid] + (['2'] if demo == 'zink' else []), env, timeout=8)
    (logdir / 'thin-fixed-size-animation.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError('fixed-size content advancement failed after thin sizes')
    if demo == 'zink':
        match = re.search(r'SAMPLES=(\d+) COLORED=(\d+).*CHANGED=(\d+) INVALID=(\d+) ERRORS=(\d+)', result.stdout)
        if not match:
            raise RuntimeError('missing fixed-size Zink probe counters')
        samples, colored, changed, invalid, errors = map(int, match.groups())
        if samples < 15 or colored * 100 < samples * 85 or changed < 4 or invalid or errors:
            raise RuntimeError('fixed-size Zink content did not advance after thin sizes')
    if app.poll() is not None or server.poll() is not None:
        raise RuntimeError('owned client/server exited after thin-size observation')
    return {'sizes': results, 'fixed_size_probe_exit': result.returncode,
            'fixed_size_advancement': 'PASS', 'during_resize_rotation': 'NOT_PROVEN_BY_THIS_PROBE'}


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo', choices=('vkcube', 'zink'), default='vkcube')
    parser.add_argument('--client', choices=('installed', 'baseline', 'coalesced'),
                        help='default: pinned private coalesced client for either demo')
    parser.add_argument('--server', type=Path, default=S39)
    parser.add_argument('--server-sha256', help='required for a noncanonical server path')
    parser.add_argument('--mode', choices=('interactive', 'direct'), default='interactive')
    parser.add_argument('--step-ms', type=int, default=8)
    parser.add_argument('--origin', action='store_true', help='own-window origin probe; S38 is a negative control')
    parser.add_argument('--expect-starvation', action='store_true',
                        help='negative control only: require zero-copy middle bins from unmodified vkcube')
    parser.add_argument('--thin', action='store_true', help='after observation, test thin sizes and fixed-size advancement')
    parser.add_argument('--close-client', action='store_true', help='send Escape to the owned coalesced client; require exit 0')
    parser.add_argument('--drag-ms', type=int, default=12000)
    parser.add_argument('--trajectory', choices=('aspect','mixed','original'), default='aspect')
    parser.add_argument('--capture', action='store_true', help='diagnostic named-pixmap and compositor readback; no motion verdict')
    parser.add_argument('--client-path', type=Path)
    parser.add_argument('--client-sha256')
    args = parser.parse_args()
    if bool(args.client_path) != bool(args.client_sha256):
        parser.error('--client-path and --client-sha256 are required together')
    if args.client_sha256 and not re.fullmatch(r'[0-9a-fA-F]{64}', args.client_sha256):
        parser.error('--client-sha256 must be an exact SHA-256 digest')
    if not 4000 <= args.drag_ms <= 15000:
        parser.error('--drag-ms must be 4000..15000')
    if args.mode != 'interactive':
        parser.error('this diagnostic requires --mode interactive')
    args.client = args.client or 'coalesced'
    if not 2 <= args.step_ms <= 200:
        parser.error('--step-ms must be 2..200')
    if args.expect_starvation and (args.demo != 'vkcube' or args.client == 'coalesced'):
        parser.error('--expect-starvation requires installed or baseline vkcube')
    if args.close_client and args.client != 'coalesced':
        parser.error('--close-client requires the coalesced client')
    if args.origin and (args.thin or args.close_client or args.expect_starvation):
        parser.error('--origin cannot combine with client resize/close checks')
    if args.server_sha256 and not re.fullmatch(r'[0-9a-fA-F]{64}', args.server_sha256):
        parser.error('--server-sha256 must be an exact SHA-256 digest')
    return args


def run(args):
    server_path = args.server.resolve()
    expected = args.server_sha256 or ({S39.resolve(): S39_SHA, S38.resolve(): S38_SHA}.get(server_path))
    if not expected:
        raise RuntimeError('an explicit server path requires --server-sha256')
    server_identity = identity(server_path, expected.lower(), executable=True)
    icd_identity = identity(ICD)
    interposer_identity = identity(INTERPOSER)
    library = Path(json.loads(ICD.read_text())['ICD']['library_path'])
    wrapper = library if library.is_absolute() else ICD.parent / library
    if wrapper.resolve() != (ICD.parent / 'libvulkan_wrapper.so').resolve():
        raise RuntimeError('S26 ICD points outside its unchanged private wrapper')
    wrapper_identity = identity(wrapper)
    client_identity = None
    if not args.origin:
        if args.client_path:
            client, client_sha = args.client_path, args.client_sha256.lower()
        elif args.client == 'installed':
            client = PREFIX / 'bin' / ('vkcube' if args.demo == 'vkcube' else 'glxgears')
            client_sha = None
        else:
            client = (S39_OUT / 'runtime' / ('vkcube.' + args.client) if args.demo == 'vkcube'
                      else GLX_RUNTIME / ('glxgears.' + args.client))
            client_sha = CLIENT_SHA[args.demo][args.client]
        client_identity = identity(client, client_sha, executable=True)
    if not (STATE / 'runtime/wayland-0').is_socket():
        raise RuntimeError('existing Alroot Wayland host socket is unavailable')
    runtime = OUT / 'runtime'
    runtime.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime('%Y%m%d-%H%M%S')
    logdir = OUT / 'logs' / f'formal-{args.demo}-{args.client}-{args.mode}-{args.step_ms}ms-{stamp}-{os.getpid()}'
    logdir.mkdir(parents=True)
    before = user_session()
    summary = {'status': 'RUNNING', 'demo': args.demo, 'client_choice': 'custom' if args.client_path else args.client,
               'mode': args.mode, 'step_ms': args.step_ms, 'server': server_identity,
               'client': client_identity, 'icd': icd_identity, 'wrapper': wrapper_identity,
               'icd_used': not args.origin, 'expected_starvation': args.expect_starvation,
               'interposer': interposer_identity, 'drag_ms': args.drag_ms, 'trajectory': args.trajectory,
               'content_readback': args.capture, 'content_animation': 'NOT_AUTOMATICALLY_CLASSIFIED',
               'android_physical_frames': 'NOT_MEASURED', 'user_session_before': before,
               'logdir': str(logdir)}
    temporary = None
    server = wm = app = observer = stress = capture = None
    failure = None
    try:
        summary['helpers'] = build_helpers(args, logdir)
        prefix_tmp = PREFIX / 'tmp'
        number = next((n for n in range(201, 241) if not any(path.exists() for path in (
            prefix_tmp / f'.X11-unix/X{n}', prefix_tmp / f'.X{n}-lock', prefix_tmp / f'.tX{n}-lock'))), None)
        if number is None:
            raise RuntimeError('no free isolated display :201..:240')
        display = f':{number}'
        summary['display'] = display
        temporary = Path(tempfile.mkdtemp(prefix='alroot-s40-formal-', dir=prefix_tmp))
        auth = temporary / 'Xauthority'
        auth.touch(mode=0o600)
        command(['xauth', '-f', str(auth), 'add', display, 'MIT-MAGIC-COOKIE-1', secrets.token_hex(16)], check=True)
        server_env = clean_environment()
        server_env.update(SERVER_FLAGS)
        server_env.update(LD_PRELOAD=str(INTERPOSER), S16_XSHMFENCE_TRACE='0')
        if server_identity['sha256'] != S38_SHA:
            server_env.update(S39_KGSL_RESIZE_ORIGIN='1', S39_KGSL_KEEP_UNRELATED_ROOT_POOL='1')
        summary['server_flags'] = {key: value for key, value in server_env.items()
                                   if key in SERVER_FLAGS or key.startswith(('S39_', 'S40_')) or key in ('LD_PRELOAD','S16_XSHMFENCE_TRACE')}
        server_env.update(XDG_RUNTIME_DIR=str(STATE / 'runtime'), WAYLAND_DISPLAY='wayland-0',
                          XAUTHORITY=str(auth), TMPDIR=str(prefix_tmp), EGL_PLATFORM='surfaceless',
                          LD_LIBRARY_PATH=str(ROOT / 'packages/prefix/lib'))
        client_env = clean_environment()
        client_env.update(DISPLAY=display, XAUTHORITY=str(auth), VK_DRIVER_FILES=str(ICD),
                          VK_ICD_FILENAMES=str(ICD), S24_WRAPPER_XCB_AHB_PAD_HEIGHT='1',
                          S26_WRAPPER_XCB_RETRY_TRANSIENT='1', LIBGL_KOPPER_DRI2='1')
        if args.demo == 'zink':
            client_env.update(LD_LIBRARY_PATH=str(MESA), LIBGL_DRIVERS_PATH=str(MESA / 'dri'),
                              MESA_LOADER_DRIVER_OVERRIDE='zink', GALLIUM_DRIVER='zink')
        with ExitStack() as stack:
            def log(name):
                return stack.enter_context((logdir / name).open('w'))
            server = subprocess.Popen([str(server_path), display, '-geometry', '1920x1080', '-dpi', '96',
                '-auth', str(auth), '-glamor', 'es', '-nolisten', 'tcp'], env=server_env,
                stdout=log('server.log'), stderr=subprocess.STDOUT, start_new_session=True)
            ensure_ready(server, ['xdpyinfo'], client_env)
            wm_env = server_env.copy()
            wm_env.pop('WAYLAND_DISPLAY', None)
            wm_env.pop('LD_PRELOAD', None)
            summary.update(wm_ld_preload=wm_env.get('LD_PRELOAD'),
                           client_ld_preload=client_env.get('LD_PRELOAD'))
            wm_env.update(DISPLAY=display, GDK_BACKEND='x11', XDG_SESSION_TYPE='x11',
                          XDG_CONFIG_HOME=str(temporary / 'config'), XDG_CACHE_HOME=str(temporary / 'cache'))
            wm = subprocess.Popen(['dbus-run-session', '--', 'xfwm4', '--compositor=on', '--display', display],
                env=wm_env, stdout=log('xfwm.log'), stderr=subprocess.STDOUT, start_new_session=True)
            ensure_ready(wm, ['xprop', '-root', '_NET_SUPPORTING_WM_CHECK'], client_env, 'window id #')
            summary.update(server_pid=server.pid, wm_pid=wm.pid)
            if args.origin:
                negative = server_identity['sha256'] == S38_SHA
                result = command([str(runtime / 's39-present-origin')] +
                                 (['--expect-centered'] if negative else []), client_env, timeout=15)
                (logdir / 'origin.log').write_text(result.stdout + result.stderr)
                summary['origin'] = {'negative_s38_control': negative, 'exit_code': result.returncode}
                if result.returncode:
                    raise RuntimeError('origin probe did not match its required signature')
                summary['status'] = 'PASS_EXPECTED_CENTERED_ORIGIN' if negative else 'PASS'
            else:
                existing = own_windows(client_env)
                app_cmd = [client_identity['path']] + (['--wsi', 'xcb'] if args.demo == 'vkcube' else [])
                app = subprocess.Popen(app_cmd, env=client_env, stdout=log('client.log'),
                                       stderr=subprocess.STDOUT, start_new_session=True)
                xid = None
                for _ in range(60):
                    if app.poll() is not None:
                        break
                    xid = find_window(client_env, existing, app.pid)
                    if xid:
                        break
                    time.sleep(.1)
                if not xid:
                    raise RuntimeError('owned client did not map a unique window')
                summary.update(xid=xid, app_pid=app.pid)
                observe_seconds = args.drag_ms / 1000 + 4
                observer = subprocess.Popen([str(runtime / 's39-present-observer'), xid, str(observe_seconds)], env=client_env,
                    stdout=log('present.log'), stderr=subprocess.STDOUT, start_new_session=True)
                ready_deadline = time.monotonic() + 5
                while 'S39_PRESENT_READY ' not in (logdir / 'present.log').read_text():
                    if observer.poll() is not None or time.monotonic() >= ready_deadline:
                        raise RuntimeError('Present observer did not become ready')
                    time.sleep(.02)
                if args.capture:
                    (logdir / 'images').mkdir()
                    capture = subprocess.Popen([str(runtime / 's40-content-pair'), xid, str(logdir / 'images'), str(observe_seconds)], env=client_env, stdout=log('content.log'), stderr=subprocess.STDOUT, start_new_session=True)
                    capture_deadline = time.monotonic() + 5
                    while 'S40_CONTENT_READY ' not in (logdir / 'content.log').read_text():
                        if capture.poll() is not None or time.monotonic() >= capture_deadline:
                            raise RuntimeError('named-pixmap diagnostic did not become ready')
                        time.sleep(.02)
                time.sleep(1.5)
                helper = runtime / ('s39-direct-resize' if args.mode == 'direct' else 's40-interactive-resize')
                stress = subprocess.Popen([str(helper), xid, str(args.drag_ms), str(args.step_ms), args.trajectory], env=client_env,
                    stdout=log('resize.log'), stderr=subprocess.STDOUT, start_new_session=True)
                stress.wait(timeout=args.drag_ms / 1000 + 5)
                if stress.returncode:
                    raise RuntimeError('owned resize helper failed')
                timing = re.search(r'S39_RESIZE_TIMING start_ms=(\d+) end_ms=(\d+)',
                                   (logdir / 'resize.log').read_text())
                if not timing:
                    raise RuntimeError('missing actual resize timestamps')
                resize_start, resize_end = map(int, timing.groups())
                summary.update(resize_start_ms=resize_start, resize_end_ms=resize_end)
                observer.wait(timeout=7)
                if observer.returncode:
                    raise RuntimeError('Present observer failed')
                if capture is not None:
                    capture.wait(timeout=7)
                    summary['content_capture_exit'] = capture.returncode
                    if capture.returncode:
                        raise RuntimeError('named-pixmap content capture failed')
                summary.update(client_alive=app.poll() is None, server_alive=server.poll() is None)
                if not summary['client_alive'] or not summary['server_alive']:
                    raise RuntimeError('owned client/server exited during resize observation')
                metrics = present_metrics((logdir / 'present.log').read_text(), resize_start, resize_end)
                summary['present'] = metrics
                enough_configures = metrics['total_configure'] >= 10
                continuous = (metrics['core_zero_copy_bins'] == 0 and
                              metrics['max_complete_receive_gap_ms'] < 1000 and enough_configures)
                summary['continuous_gate_pass'] = continuous
                if args.expect_starvation:
                    reproduced = (metrics['core_zero_copy_bins'] > 0 and enough_configures and
                                  metrics['pre_resize_copy_count'] > 0 and metrics['post_resize_copy_count'] > 0)
                    summary['starvation_control_reproduced'] = reproduced
                    if not reproduced:
                        raise RuntimeError('expected baseline starvation was not reproduced')
                elif not continuous:
                    raise RuntimeError('continuous resize gate failed: zero COPY bin, >=1000ms gap, or insufficient Configure events')
                if args.thin:
                    summary['thin'] = thin_check(xid, client_env, app, server, args.demo, logdir)
                if args.close_client:
                    command(['xdotool', 'key', '--window', xid, 'Escape'], client_env, check=True)
                    app.wait(timeout=5)
                    summary['close_client_exit'] = app.returncode
                    if app.returncode != 0:
                        raise RuntimeError('coalesced client did not exit normally after its Escape')
                if server.poll() is not None or wm.poll() is not None:
                    raise RuntimeError('owned server/WM exited before cleanup')
                summary['status'] = 'PASS_EXPECTED_STARVATION' if args.expect_starvation else 'PASS'
            if server.poll() is not None or wm.poll() is not None:
                raise RuntimeError('owned server/WM exited before cleanup')
            faults = (logdir / 'server.log').read_text(errors='replace')
            if re.search(r'invalid object|failed to dispatch Wayland events|Fatal server error|Segmentation fault', faults):
                raise RuntimeError('owned Xwayland fault in log')
    except BaseException as exc:
        summary.update(status='FAIL', error=f'{type(exc).__name__}: {exc}')
        failure = exc
    finally:
        for proc in (stress, capture, observer, app, wm, server):
            try:
                stop_owned(proc)
            except Exception as exc:
                summary.setdefault('cleanup_errors', []).append(str(exc))
                failure = failure or exc
                summary['status'] = 'FAIL'
        if temporary is not None:
            shutil.rmtree(temporary, ignore_errors=True)
        try:
            after = user_session()
            summary['user_session_after'] = after
            summary['user_session_unchanged'] = after == before
            if after != before:
                raise RuntimeError('user session changed')
        except Exception as exc:
            summary.update(user_session_unchanged=False, status='FAIL', user_session_error=str(exc))
            failure = failure or exc
        (logdir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        print('S40_LIVE_RESIZE=' + summary['status'], flush=True)
        print('S40_SUMMARY=' + str(logdir / 'summary.json'), flush=True)
        if 'present' in summary:
            metrics = summary['present']
            print(json.dumps({key: metrics[key] for key in
                              ('core_copy_count', 'core_copy_fps', 'core_zero_copy_bins',
                               'total_configure', 'max_complete_receive_gap_ms')}, sort_keys=True), flush=True)
    if failure:
        raise failure
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(run(parse_args()))
    except Exception as exc:
        print('S40_TEST_ERROR=' + str(exc)[:400], file=sys.stderr)
        raise SystemExit(2)
