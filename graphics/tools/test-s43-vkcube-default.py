#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Check plain vkcube and its WM close action in the running Bionic XFCE.

Only windows proven by XRes to belong to this script's child can be closed.
The optional finite Wayland probes verify explicit selection and native AUTO.
No desktop restart, mouse/keyboard input, or user settings are involved.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
PREFIX = Path('/data/data/com.termux/files/usr')
STATE = ROOT.parent / '.alroot-wayland'
OUT = ROOT / '.source/gpu-stage1/s43-vkcube-default-wsi'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(argv, env=None, timeout=5):
    return subprocess.run([str(x) for x in argv], env=env, capture_output=True,
                          text=True, timeout=timeout)


def session_stamp():
    result = {}
    for name in ('xfce', 'xwayland'):
        value = (STATE / (name + '.pid')).read_text().split()
        fields = Path('/proc', value[0], 'stat').read_text().rsplit(')', 1)[1].split()
        require(fields[19] == value[1], 'live session identity changed: ' + name)
        result[name] = {'pid': int(value[0]), 'start_ticks': value[1]}
    return result


def desktop_environment(stamp):
    matches = []
    for directory in Path('/proc').glob('[0-9]*'):
        try:
            fields = (directory / 'stat').read_text().rsplit(')', 1)[1].split()
            argv0 = (directory / 'cmdline').read_bytes().split(b'\0')[0].decode()
            if int(fields[2]) == stamp['xfce']['pid'] and Path(argv0).name == 'xfce4-session':
                matches.append(directory)
        except (OSError, ValueError, IndexError):
            continue
    require(len(matches) == 1, 'expected one xfce4-session in the live desktop group')
    env = dict(item.decode(errors='surrogateescape').split('=', 1)
               for item in (matches[0] / 'environ').read_bytes().split(b'\0') if b'=' in item)
    require(env.get('XDG_SESSION_TYPE') == 'x11' and env.get('DISPLAY'), 'requires the real X11 desktop')
    require(env.get('ALROOT_CLIENT_GRAPHICS') == 'wrapper-zink', 'requires the accepted graphics policy')
    require(not env.get('LD_PRELOAD') and not env.get('LD_LIBRARY_PATH'), 'unexpected runtime library override')
    runtime = Path(env['XDG_RUNTIME_DIR'])
    require((runtime / env.get('WAYLAND_DISPLAY', 'wayland-0')).is_socket(), 'host Wayland socket must also be reachable')
    env['WAYLAND_DEBUG'] = 'client'
    return env


def windows(env):
    result = command(['xprop', '-root', '_NET_CLIENT_LIST'], env)
    require(result.returncode == 0, 'cannot read the live managed window list')
    return set(re.findall(r'0x[0-9a-fA-F]+', result.stdout))


def stop_owned(proc):
    if proc.poll() is None:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait(timeout=3)


def plain_case(client, helper, env, logdir):
    before = windows(env)
    logfile = logdir / 'plain.log'
    with logfile.open('w') as log:
        app = subprocess.Popen([str(client)], env=env, stdout=log, stderr=subprocess.STDOUT,
                               start_new_session=True)
        try:
            deadline, found = time.monotonic() + 8, []
            while app.poll() is None and time.monotonic() < deadline:
                found = []
                for xid in windows(env) - before:
                    title = command(['xprop', '-id', xid, '_NET_WM_NAME'], env)
                    if title.returncode or '"Vkcube X11"' not in title.stdout:
                        continue
                    probe = command([helper, '--inspect', xid, app.pid], env)
                    if probe.returncode == 0:
                        found.append((xid, json.loads(probe.stdout)))
                if found:
                    break
                time.sleep(.1)
            require(len(found) == 1, 'plain vkcube did not create one decorated window owned by its PID')
            xid, inspection = found[0]
            text = logfile.read_text(errors='replace')
            require('Selected WSI platform: xcb' in text and 'get_xdg_surface' not in text,
                    'plain vkcube did not select only XCB')
            maps = Path(f'/proc/{app.pid}/maps').read_text()
            require(str(PREFIX / 'lib/libvulkan_wrapper.so') in maps, 'plain vkcube did not load the installed Wrapper')
            close = command([helper, xid, app.pid], env)
            require(close.returncode == 0, 'owned WM close request failed: ' + close.stderr[-500:])
            app.wait(timeout=6)
            require(app.returncode == 0, 'WM close did not produce normal vkcube exit')
            require(xid not in windows(env), 'closed vkcube remains in the managed window list')
            return {'status': 'PASS', 'arguments': [], 'pid': app.pid, 'xid': xid,
                    'selected_wsi': 'xcb', 'inspection': inspection,
                    'close': json.loads(close.stdout), 'exit_code': app.returncode,
                    'wayland_surface_created': False}
        finally:
            stop_owned(app)


def wayland_case(client, env, logdir, explicit):
    name = 'explicit-wayland' if explicit else 'wayland-session-auto'
    test_env = dict(env)
    if not explicit:
        # Keep DISPLAY reachable too: a blanket XCB-first change would fail here.
        test_env['XDG_SESSION_TYPE'] = 'wayland'
    args = ['--wsi', 'wayland'] if explicit else []
    args += ['--c', '120']
    logfile = logdir / (name + '.log')
    with logfile.open('w') as log:
        app = subprocess.Popen([str(client), *args], env=test_env, stdout=log,
                               stderr=subprocess.STDOUT, start_new_session=True)
        try:
            app.wait(timeout=15)
            text = logfile.read_text(errors='replace')
            require(app.returncode == 0 and 'get_xdg_surface' in text
                    and re.search(r'alroot_gpu_buffer_v1#\d+\.create_buffer', text),
                    name + ' did not finish through native Wayland/AHB')
            if not explicit:
                require('Selected WSI platform: wayland' in text, 'native session AUTO preference changed')
            return {'status': 'PASS', 'arguments': args, 'exit_code': app.returncode,
                    'session_type': test_env['XDG_SESSION_TYPE'], 'display_still_set': bool(test_env.get('DISPLAY')),
                    'wayland_surface_created': True, 'ahb_buffer_created': True}
        finally:
            stop_owned(app)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, default=PREFIX / 'bin/vkcube')
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--label', choices=('candidate', 'installed'), required=True)
    parser.add_argument('--wayland-checks', action='store_true')
    args = parser.parse_args()
    require(re.fullmatch(r'[0-9a-f]{64}', args.sha256), 'invalid client hash')
    client = args.client.resolve(strict=True)
    require(client.is_file() and os.access(client, os.X_OK) and sha(client) == args.sha256, 'client identity mismatch')
    logdir = OUT / 'probes' / (args.label + '-' + time.strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
    logdir.mkdir(parents=True)
    summary = {'status': 'RUNNING', 'label': args.label, 'client': str(client), 'sha256': args.sha256}
    failure = None
    try:
        before = session_stamp()
        summary['session_before'] = before
        env = desktop_environment(before)
        summary['environment'] = {k: env.get(k) for k in ('XDG_SESSION_TYPE', 'DISPLAY', 'WAYLAND_DISPLAY',
            'XDG_RUNTIME_DIR', 'ALROOT_CLIENT_GRAPHICS', 'VK_DRIVER_FILES', 'VK_ICD_FILENAMES')}
        source = ROOT / 'graphics/tests/s43-close-owned-window.c'
        helper = logdir / 'close-owned-window'
        build_env = os.environ.copy()
        build_env.pop('LD_PRELOAD', None)
        build_env.pop('LD_LIBRARY_PATH', None)
        build = command(['clang', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', source,
                         '-lxcb', '-lxcb-res', '-o', helper], build_env, timeout=45)
        (logdir / 'helper-build.log').write_text(build.stdout + build.stderr)
        require(build.returncode == 0, 'owned-window helper build failed')
        summary['helper'] = {'source_sha256': sha(source), 'binary_sha256': sha(helper)}
        summary['plain'] = plain_case(client, helper, env, logdir)
        if args.wayland_checks:
            summary['explicit_wayland'] = wayland_case(client, env, logdir, True)
            summary['wayland_auto'] = wayland_case(client, env, logdir, False)
        summary['session_after'] = session_stamp()
        require(summary['session_after'] == before, 'live desktop identity changed')
        summary['status'] = 'PASS'
    except Exception as exc:
        failure = exc
        summary.update(status='FAIL', error=f'{type(exc).__name__}: {exc}')
    finally:
        (logdir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        print('S43_DEFAULT_WSI=' + summary['status'], flush=True)
        print('S43_DEFAULT_WSI_SUMMARY=' + str(logdir / 'summary.json'), flush=True)
    if failure:
        raise failure


if __name__ == '__main__':
    main()
