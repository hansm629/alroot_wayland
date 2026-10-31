#!/data/data/com.termux/files/usr/bin/python3
"""S30 stable-last-real-frame proof across 25 extreme/no-repaint resizes.

The unmodified S29 negative control loses its own test window graphic when
repeatedly resized to width 1/8; S30 must restore the original 20px bar
and remove right/bottom ghosts at the final 400x280 size. All Xwayland
instances are authenticated disposable :201+: user XFCE remains untouched.
"""
from pathlib import Path
import argparse
import os
import re
import secrets
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path('/data/data/com.termux/files/home/alroot_wayland')
HOME = Path('/data/data/com.termux/files/home')
PREFIX = Path('/data/data/com.termux/files/usr')
RUNTIME = HOME / '.alroot-wayland/runtime'
S29 = ROOT / '.source/gpu-stage1/s29-native-resize/runtime/Xwayland'
S30 = ROOT / '.source/gpu-stage1/s30-min-extent/runtime/Xwayland'
LOGS = ROOT / '.source/gpu-stage1/s30-min-extent/logs'
TREE = PREFIX / 'tmp/.X11-unix'
PROG = ROOT / '.source/gpu-stage1/s30-min-extent/runtime/s30-native-stable-frame'


def stop(proc):
    if proc is None or proc.poll() is not None:
        return
    os.killpg(proc.pid, signal.SIGTERM)
    try:
        proc.wait(timeout=4)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)


def run_one(name, exe, flag, with_wm=False):
    number = next((i for i in range(201, 241)
                   if not (TREE / f'X{i}').exists()
                   and not (PREFIX / f'tmp/.X{i}-lock').exists()), None)
    if number is None:
        raise RuntimeError('no isolated X11 display available')
    display = f':{number}'
    tmp = Path(tempfile.mkdtemp(prefix='alroot-s29-exact-', dir=PREFIX / 'tmp'))
    auth = tmp / 'Xauthority'
    auth.touch(mode=0o600)
    subprocess.run(['xauth', '-f', str(auth), 'add', display,
                    'MIT-MAGIC-COOKIE-1', secrets.token_hex(16)],
                   capture_output=True, check=True, timeout=5)
    env = os.environ.copy()
    env.update(XDG_RUNTIME_DIR=str(RUNTIME), WAYLAND_DISPLAY='wayland-0',
               TMPDIR=str(PREFIX / 'tmp'), XAUTHORITY=str(auth),
               XWAYLAND_FORCE_KGSL_SURFACELESS='1', FD_KGSL_ENABLE_DMABUF='1',
               EGL_PLATFORM='surfaceless', MESA_LOADER_DRIVER_OVERRIDE='kgsl',
               GALLIUM_DRIVER='freedreno',
               LD_LIBRARY_PATH=str(ROOT / 'packages/prefix/lib'))
    env.pop('DISPLAY', None)
    env.pop('S28_KGSL_RESIZE_PRESERVE', None)
    env.pop('S29_KGSL_NATIVE_RESIZE', None)
    env[flag] = '1'
    if name == 'candidate':
        env['S30_KGSL_TINY_AHB_FD0'] = '1'
        env.pop('S30_TRACE_STABLE', None)
    client = dict(env, DISPLAY=display)
    client.pop('WAYLAND_DISPLAY', None)
    proc = wm = None
    try:
        with (LOGS / f'geometry-{name}-xwayland.log').open('w') as out:
            proc = subprocess.Popen(
                [str(exe), display, '-glamor', 'es', '-geometry', '1920x1080',
                 '-dpi', '96', '-auth', str(auth), '-nolisten', 'tcp'],
                env=env, stdout=out, stderr=subprocess.STDOUT,
                start_new_session=True)
            for _ in range(70):
                if proc.poll() is not None:
                    raise RuntimeError('test server exited before readiness')
                ping = subprocess.run(['xdpyinfo'], env=client, capture_output=True,
                                      timeout=3)
                if ping.returncode == 0:
                    break
                time.sleep(.1)
            else:
                raise RuntimeError('test server did not become ready')
            if with_wm:
                wm_env = client.copy()
                wm_env.update(GDK_BACKEND='x11', XDG_SESSION_TYPE='x11',
                              XDG_CONFIG_HOME=str(tmp / 'isolated-xfce'),
                              XDG_CACHE_HOME=str(tmp / 'cache'))
                wm_env.pop('SESSION_MANAGER', None)
                wm_env.pop('DBUS_SESSION_BUS_ADDRESS', None)
                with (LOGS / f'geometry-{name}-xfwm.log').open('w') as wm_log:
                    wm = subprocess.Popen(
                        ['dbus-run-session', '--', 'xfwm4', '--compositor=on',
                         '--display', display], env=wm_env,
                        stdout=wm_log, stderr=subprocess.STDOUT,
                        start_new_session=True)
                for _ in range(50):
                    if wm.poll() is not None:
                        raise RuntimeError('isolated XFWM4 exited at startup')
                    p = subprocess.run(['xprop', '-root', '_NET_SUPPORTING_WM_CHECK'],
                                       env=client, capture_output=True, text=True, timeout=3)
                    if 'window id #' in p.stdout:
                        break
                    time.sleep(.10)
                else:
                    raise RuntimeError('isolated XFWM4 did not register')
                print(f'S30_{name.upper()}_ISOLATED_XFWM4=READY', flush=True)
            result = subprocess.run([str(PROG)], env=client, capture_output=True,
                                    text=True, timeout=8)
            print(f'S30_{name.upper()}_CLIENT_RC={result.returncode}', flush=True)
            print(result.stdout.rstrip(), flush=True)
            if result.stderr:
                print('S30_TEST_STDERR=' + result.stderr[-240:], flush=True)
            if name == 'candidate' and result.returncode != 0:
                raise RuntimeError('S30 stable native pixel preservation contract failed')
            if name == 'previous' and result.returncode == 0:
                raise RuntimeError('S29 negative control unexpectedly passed; cannot prove regression fixed')
    finally:
        stop(wm)
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)
        if (TREE / f'X{number}').exists():
            raise RuntimeError('isolated Xserver socket still present')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--wm', action='store_true',
                        help='diagnostic only; no-Present Xfwm4 may leave black regions')
    options = parser.parse_args()
    LOGS.mkdir(parents=True, exist_ok=True)
    if not S29.is_file() or not S30.is_file() or not RUNTIME.joinpath('wayland-0').is_socket():
        raise RuntimeError('S29/S30 or private Android host missing')
    subprocess.run(['clang', '--target=aarch64-linux-android35', '-O2',
                    '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    str(ROOT / 'graphics/tests/s30-native-stable-frame.c'),
                    '-lX11', '-o', str(PROG)], check=True, timeout=25)
    current = subprocess.check_output(['awstart', '--status'], text=True, timeout=8)
    pid = re.search(r'^xwayland: RUNNING (\d+)$', current, re.M)
    if not pid or 'x11_desktop: READY' not in current:
        raise RuntimeError('user XFCE should be active and remain untouched')
    run_one('previous', S29, 'S29_KGSL_NATIVE_RESIZE', options.wm)
    run_one('candidate', S30, 'S30_KGSL_STABLE_FRAME', options.wm)
    end = subprocess.check_output(['awstart', '--status'], text=True, timeout=8)
    if (f'xwayland: RUNNING {pid.group(1)}' not in end or
            'x11_desktop: READY' not in end):
        raise RuntimeError('user desktop changed')
    print('S30_USER_XFCE_UNCHANGED=PASS')


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('S30_NATIVE_GEOMETRY_TEST_ERROR=' + str(exc)[:250], flush=True)
        raise SystemExit(2)
