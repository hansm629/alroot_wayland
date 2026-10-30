#!/data/data/com.termux/files/usr/bin/python3
"""S29 vkcube edge-pixel/GLX stability A/B vs S28 on isolated Xwayland.

S28 previously stretched the last GPU frame over the new window size.
S29 retains 1:1 pixels and fills new right/bottom areas with border color.
This test observes known-background pixels only in its own vkcube client.

Neither the Android helper, user's live XFCE, nor installed Termux-X11 is
stopped or modified. Only the benchmark's own mapped window is resized;
the optional XFWM4 is also spawned exclusively on the temporary X display.
XGetImage samples nine *known* vkcube background/edge positions, including
the center right/bottom strips that S28's four-corner test failed to catch.
"""

import argparse
from contextlib import ExitStack
import os
from pathlib import Path
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
STATE = HOME / '.alroot-wayland'
OUT = ROOT / '.source/gpu-stage1/s29-native-resize'
LOGS = OUT / 'logs'
BASE = ROOT / '.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland'
CANDIDATE = OUT / 'runtime/Xwayland'
ICD = ROOT / '.source/gpu-stage1/s26-wrapper-resize-retry/runtime/wrapper_icd.private.json'
MESA_GLX = ROOT / '.source/gpu-stage27-pr25/mesa-private/glx-runtime/lib'
SIZES = ((900, 649), (901, 650), (777, 673), (650, 490),
         (1200, 701), (500, 503), (640, 480), (900, 700),
         (900, 650), (500, 500))


def command(args, env, timeout=4):
    return subprocess.run(args, env=env, capture_output=True,
                          text=True, timeout=timeout)


def client_windows(env):
    p = command(['xprop', '-root', '_NET_CLIENT_LIST'], env)
    if p.returncode:
        raise RuntimeError('X11 WM returned no authenticated client list')
    return set(re.findall(r'0x[0-9a-fA-F]+', p.stdout))


def mapped_xid(env, app_pid, with_wm, tree, before):
    if with_wm:
        for xid in client_windows(env) - before:
            prop = command(['xprop', '-id', xid, 'WM_STATE', '_NET_WM_PID'], env)
            if 'window state: Normal' not in prop.stdout:
                continue
            m = re.search(r'_NET_WM_PID\(CARDINAL\) = (\d+)', prop.stdout)
            if m is None or int(m.group(1)) == app_pid:
                return xid
    else:
        p = command([str(tree)], env)
        new = [line for line in p.stdout.splitlines()
               if re.search(r'^0x[0-9a-f]+ map=2 ', line)]
        if len(new) == 1:
            return re.search(r'0x[0-9a-f]+', new[0]).group(0)
    return None


def end_process(proc, group=False):
    if proc is None or proc.poll() is not None:
        return
    try:
        if group:
            os.killpg(proc.pid, signal.SIGTERM)
        else:
            proc.terminate()
        proc.wait(timeout=4)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=3)


def one_run(mode, demo, seconds, with_wm, tree, sampler, status_before):
    prefix_tmp = PREFIX / 'tmp'
    xdir = prefix_tmp / '.X11-unix'
    available = next((n for n in range(201, 241)
                      if not (xdir / f'X{n}').exists()
                      and not (prefix_tmp / f'.X{n}-lock').exists()
                      and not (prefix_tmp / f'.tX{n}-lock').exists()), None)
    if available is None:
        raise RuntimeError('isolated :201..:240 X display slots all occupied')
    display = f':{available}'
    tmp = Path(tempfile.mkdtemp(prefix='alroot-s28-', dir=prefix_tmp))
    auth = tmp / 'Xauthority'
    auth.touch(mode=0o600)
    subprocess.run(['xauth', '-f', str(auth), 'add', display,
                    'MIT-MAGIC-COOKIE-1', secrets.token_hex(16)],
                   check=True, capture_output=True, timeout=5)
    binary = CANDIDATE if mode == 'candidate' else BASE
    server_env = os.environ.copy()
    server_env.update(XDG_RUNTIME_DIR=str(STATE / 'runtime'),
                      WAYLAND_DISPLAY='wayland-0', XAUTHORITY=str(auth),
                      XWAYLAND_FORCE_KGSL_SURFACELESS='1',
                      FD_KGSL_ENABLE_DMABUF='1',
                      MESA_LOADER_DRIVER_OVERRIDE='kgsl',
                      GALLIUM_DRIVER='freedreno', EGL_PLATFORM='surfaceless',
                      LD_LIBRARY_PATH=str(ROOT / 'packages/prefix/lib'),
                      TMPDIR=str(prefix_tmp))
    for key in ('DISPLAY', 'LD_PRELOAD', 'DBUS_SESSION_BUS_ADDRESS'):
        server_env.pop(key, None)
    if mode == 'candidate':
        server_env['S29_KGSL_NATIVE_RESIZE'] = '1'
        server_env.pop('S29_KGSL_RESIZE_PRESERVE', None)
    else:
        server_env['S29_KGSL_RESIZE_PRESERVE'] = '1'
        server_env.pop('S29_KGSL_NATIVE_RESIZE', None)

    client_env = os.environ.copy()
    for key in ('WAYLAND_DISPLAY', 'LD_PRELOAD', 'VK_DRIVER_FILES',
                'VK_ICD_FILENAMES', 'MESA_LOADER_DRIVER_OVERRIDE',
                'GALLIUM_DRIVER', 'LIBGL_DRIVERS_PATH', 'LIBGL_ALWAYS_SOFTWARE'):
        client_env.pop(key, None)
    client_env.update(DISPLAY=display, XAUTHORITY=str(auth),
                      VK_DRIVER_FILES=str(ICD), VK_ICD_FILENAMES=str(ICD),
                      S24_WRAPPER_XCB_AHB_PAD_HEIGHT='1',
                      S26_WRAPPER_XCB_RETRY_TRANSIENT='1',
                      LIBGL_KOPPER_DRI2='1')
    if demo == 'zink':
        client_env.update(LD_LIBRARY_PATH=str(MESA_GLX),
                          LIBGL_DRIVERS_PATH=str(MESA_GLX / 'dri'),
                          MESA_LOADER_DRIVER_OVERRIDE='zink',
                          GALLIUM_DRIVER='zink')
        app_cmd = [str(PREFIX / 'bin/glxgears')]
    else:
        app_cmd = [str(PREFIX / 'bin/vkcube'), '--wsi', 'xcb']

    label = f'{mode}-{demo}' + ('-xfwm' if with_wm else '-rootful')
    server = wm = app = observer = None
    error = None
    LOGS.mkdir(parents=True, exist_ok=True)
    try:
        with ExitStack() as stack:
            server_log = stack.enter_context((LOGS / (label + '-server.log')).open('w'))
            app_log = stack.enter_context((LOGS / (label + '-client.log')).open('w'))
            server = subprocess.Popen(
                [str(binary), display, '-geometry', '1920x1080', '-dpi', '96',
                 '-auth', str(auth), '-glamor', 'es', '-nolisten', 'tcp'],
                env=server_env, stdout=server_log, stderr=subprocess.STDOUT,
                start_new_session=True)
            for _ in range(65):
                if server.poll() is not None:
                    raise RuntimeError('private test Xwayland exited during startup')
                if command(['xdpyinfo'], client_env, timeout=2).returncode == 0:
                    break
                time.sleep(.12)
            else:
                raise RuntimeError('private test Xwayland did not start')

            if with_wm:
                wm_env = server_env.copy()
                wm_env.update(DISPLAY=display, GDK_BACKEND='x11',
                              XDG_CONFIG_HOME=str(tmp / 'isolated-xfce-config'),
                              XDG_CACHE_HOME=str(tmp / 'cache'),
                              XDG_SESSION_TYPE='x11')
                wm_env.pop('WAYLAND_DISPLAY', None)
                wm_env.pop('SESSION_MANAGER', None)
                wm_log = stack.enter_context((LOGS / (label + '-xfwm.log')).open('w'))
                wm = subprocess.Popen(
                    ['dbus-run-session', '--', 'xfwm4', '--compositor=on', '--display', display],
                    env=wm_env, stdout=wm_log, stderr=subprocess.STDOUT,
                    start_new_session=True)
                for _ in range(45):
                    if wm.poll() is not None:
                        raise RuntimeError('isolated XFWM4 exited during startup')
                    ready = command(['xprop', '-root', '_NET_SUPPORTING_WM_CHECK'],
                                    client_env)
                    if 'window id #' in ready.stdout:
                        break
                    time.sleep(.12)
                else:
                    raise RuntimeError('isolated XFWM4 did not publish a WM')
                print('S29_ISOLATED_WM=XFWM4', flush=True)
            baseline = client_windows(client_env) if with_wm else set()
            app = subprocess.Popen(app_cmd, env=client_env, stdout=app_log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
            xid = None
            for _ in range(60):
                if app.poll() is not None:
                    break
                xid = mapped_xid(client_env, app.pid, with_wm, tree, baseline)
                if xid:
                    break
                time.sleep(.13)
            if not xid:
                raise RuntimeError(f'no mapped {demo} window, code={app.poll()}')
            print(f'S29_{mode.upper()}_{demo.upper()}_DISPLAY={display} OWN_XID={xid}',
                  flush=True)
            marker = command(['xprop', '-id', xid, '_MESA_DRV'], client_env)
            if '_MESA_DRV(STRING)' not in marker.stdout:
                raise RuntimeError('private XCB Vulkan WSI marker not advertised')

            if demo == 'vkcube':
                sample_log = stack.enter_context((LOGS / (label + '-corners.log')).open('w'))
                observer = subprocess.Popen(
                    [str(sampler), xid, str(seconds + 1), '8'],
                    env=client_env, stdout=sample_log, stderr=subprocess.STDOUT)
            start = time.monotonic()
            count = 0
            while time.monotonic() - start < seconds and app.poll() is None:
                width, height = SIZES[count % len(SIZES)]
                count += 1
                p = command(['xdotool', 'windowsize', xid, str(width), str(height)],
                            client_env, timeout=3)
                if p.returncode:
                    raise RuntimeError('test-owned XCB window resize failed: ' + p.stderr[-180:])
                time.sleep(.17)
            app_alive = app.poll() is None
            if observer:
                observer.wait(timeout=7)
            print(f'S29_{mode.upper()}_{demo.upper()}_RESIZES={count} CLIENT_ALIVE={app_alive}',
                  flush=True)
            if not app_alive:
                raise RuntimeError('test client exited while resizing')
            black_events = -1
            if demo == 'vkcube':
                contents = (LOGS / (label + '-corners.log')).read_text()
                lines = {}
                for side in ('tl','tr','bl','br','right','right_in','bottom','bottom_in','bottom_deep'):
                    match = re.search(r'S29_EDGE_' + side + r' samples=(\d+) black=(\d+) off_grey=(\d+)', contents)
                    if not match:
                        raise RuntimeError('missing edge sample: '+side)
                    lines[side] = tuple(map(int, match.groups()))
                print('S29_' + mode.upper() + '_EDGE_BLACK: ' +
                      ' '.join(side+'='+str(lines[side][1]) for side in lines), flush=True)
                print('S29_' + mode.upper() + '_EDGE_OFF_GREY: ' +
                      ' '.join(side+'='+str(lines[side][2]) for side in lines), flush=True)
                black_events = lines['right'][1] + lines['bottom'][1]
            return black_events
    except Exception as exc:
        error = exc
        raise
    finally:
        end_process(observer)
        end_process(app, group=True)
        end_process(wm, group=True)
        end_process(server, group=True)
        shutil.rmtree(tmp, ignore_errors=True)
        if (xdir / f'X{available}').exists():
            print('S29_PRIVATE_X_SERVER_SOCKET_NOT_CLEANED', flush=True)
        same = subprocess.run(['awstart', '--status'], capture_output=True,
                              text=True, timeout=8).stdout
        if (status_before.split('xwayland: ')[1].splitlines()[0] !=
                same.split('xwayland: ')[1].splitlines()[0]):
            raise RuntimeError('running user Xwayland PID changed') from error
        print('S29_USER_XFCE_XWAYLAND_UNCHANGED=PASS', flush=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--mode', choices=('baseline', 'candidate', 'compare'),
                   default='compare')
    p.add_argument('--demo', choices=('vkcube', 'zink'), default='vkcube')
    p.add_argument('--wm', action='store_true', help='start isolated XFWM4')
    p.add_argument('--seconds', type=int, default=10)
    options = p.parse_args()
    if not 3 <= options.seconds <= 35:
        p.error('duration must be 3..35 seconds per case')
    if not CANDIDATE.is_file() or not BASE.is_file() or not ICD.is_file():
        raise RuntimeError('missing S28/S29 private Xwayland or S26 Wrapper')
    if not (STATE / 'runtime/wayland-0').is_socket():
        raise RuntimeError('Alroot Wayland host not running')

    import hashlib
    for path, digest in (
        (BASE, 'cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7'),
        (CANDIDATE, '3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c'),
    ):
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError('private Xwayland binary not pinned: ' + str(path))
    LOGS.mkdir(parents=True, exist_ok=True)
    sampler = OUT / 'runtime/s29-vkcube-edge-sampler'
    tree = OUT / 'runtime/s28-window-tree'
    for source, output in (('s29-vkcube-edge-sampler.c', sampler),
                           ('s28-x11-window-tree.c', tree)):
        subprocess.run(['clang', '--target=aarch64-linux-android35', '-O2',
                        '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'graphics/tests' / source), '-lX11',
                        '-o', str(output)], check=True, timeout=30)

    status = subprocess.check_output(['awstart', '--status'], text=True, timeout=8)
    modes = ('baseline', 'candidate') if options.mode == 'compare' else (options.mode,)
    outcomes = {}
    for mode in modes:
        outcomes[mode] = one_run(mode, options.demo, options.seconds,
                                 options.wm, tree, sampler, status)
    if options.demo == 'vkcube' and len(outcomes) == 2:
        before, after = outcomes['baseline'], outcomes['candidate']
        print(f'S29_COMPARE_X11_BLACK_EVENTS={before} -> {after}', flush=True)
        print('S29_COMPARE_FLICKER=' +
              ('NO_BLACK_IN_CANDIDATE' if not after
               else 'IMPROVED' if 0 <= after < before
               else 'NEEDS_FURTHER_WORK'), flush=True)
    print('S29_PHYSICAL_ANDROID_FLICKER=USER_VISUAL_TEST_REQUIRED', flush=True)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:
        print('S29_ISOLATED_TEST_ERROR=' + str(exc)[:260], flush=True)
        raise SystemExit(2)
