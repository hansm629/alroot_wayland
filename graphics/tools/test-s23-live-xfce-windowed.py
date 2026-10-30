#!/data/data/com.termux/files/usr/bin/python3
"""GPU-S23: bounded on-screen windowed Zink/Turnip smoke on live private XFCE.

Tests only owned demo windows, never moves the mouse, restarts a session,
modifies a desktop setting, or reads another user's window pixels.
Default suite has 3 GLmark demos; --vulkan adds 3 Vulkan cube demos.
This cannot establish what the Android physical display actually showed.
"""

import argparse
import os
import pathlib
import re
import signal
import subprocess
import sys
import time

HOME = pathlib.Path('/data/data/com.termux/files/home')
PREFIX = pathlib.Path('/data/data/com.termux/files/usr')
ROOT = HOME / 'alroot_wayland'
BASE = ROOT / '.source/gpu-stage27-pr25'
STATE = HOME / '.alroot-wayland'
NATIVE = BASE / 'mesa-private/glx-runtime/lib'
GLIBC = BASE / 'mesa-glibc27/private-runtime'
TURNIP = BASE / 's15-turnip/private-runtime/turnip_alroot_icd.private.json'
OUTPUT = BASE / 's16-xwayland-gpu/zink-matrix/live-s23-acceptance'
SOURCE_HASH = '6f90e92dfdacc4759d8e9ccb328805d0f0b4439b'


def run_quick(argv, env, timeout=3):
    return subprocess.run(argv, env=env, capture_output=True, text=True, timeout=timeout)


def current_xids(env):
    result = run_quick(['xprop', '-root', '_NET_CLIENT_LIST'], env)
    if result.returncode:
        raise RuntimeError('authenticated _NET_CLIENT_LIST unavailable')
    return set(re.findall(r'0x[0-9a-fA-F]+', result.stdout))


def require_private_session():
    status = subprocess.check_output(['awstart', '--status'], text=True, timeout=7)
    match = re.search(r'^xwayland: RUNNING (\d+)$', status, re.M)
    display_match = re.search(r'^X DISPLAY: (:\d{2,3})$', status, re.M)
    if not match or not display_match or 'x11_desktop: READY' not in status:
        raise RuntimeError('private XFCE Xwayland is not fully ready')
    display, pid = display_match.group(1), int(match.group(1))
    if display == ':0' or not (STATE / 'Xauthority').is_file():
        raise RuntimeError('unsafe display or missing authenticated Xauthority')
    cmdline = pathlib.Path(f'/proc/{pid}/cmdline').read_bytes().replace(b'\x00', b' ')
    if b'/alroot_wayland/.source/gpu-stage1/xwayland-kgsl/' not in cmdline:
        raise RuntimeError('running Xwayland is not the validated private KGSL server')
    if not re.search(rf'Xwayland {re.escape(display)} ', cmdline.decode(errors='replace')):
        raise RuntimeError('Xwayland PID/actual DISPLAY mismatch')
    return pid, display


def client_command(route, scene, display, auth):
    env = os.environ.copy()
    for key in ('WAYLAND_DISPLAY', 'LD_PRELOAD', 'VK_ICD_FILENAMES',
                'VK_DRIVER_FILES', 'MESA_LOADER_DRIVER_OVERRIDE',
                'GALLIUM_DRIVER', 'LIBGL_DRIVERS_PATH', 'LIBGL_ALWAYS_SOFTWARE'):
        env.pop(key, None)
    env.update(DISPLAY=display, XAUTHORITY=str(auth), LIBGL_KOPPER_DRI2='1')
    if scene == 'glmark2':
        executable = ('/usr/bin/glmark2' if route == 'debian' else str(PREFIX / 'bin/glmark2'))
        args = [executable, '-b', 'build:use-vbo=false']
    elif scene == 'glxgears':
        executable = ('/usr/bin/glxgears' if route == 'debian' else str(PREFIX / 'bin/glxgears'))
        args = [executable, '-info']
    else:
        executable = ('/usr/bin/vkcube' if route == 'debian' else str(PREFIX / 'bin/vkcube'))
        args = [executable, '--wsi', 'xcb']  # bounded by this harness, not --c FPS

    if route == 'debian':
        env['VK_DRIVER_FILES'] = str(GLIBC / 'turnip_icd.private.json')
        env['VK_ICD_FILENAMES'] = str(GLIBC / 'turnip_icd.private.json')
        extra = ['DISPLAY=' + display, 'XAUTHORITY=' + str(auth),
                 'VK_DRIVER_FILES=' + env['VK_DRIVER_FILES'],
                 'VK_ICD_FILENAMES=' + env['VK_ICD_FILENAMES'],
                 'LD_LIBRARY_PATH=' + str(GLIBC / 'lib'),
                 'LIBGL_DRIVERS_PATH=' + str(GLIBC / 'lib/dri'),
                 'MESA_LOADER_DRIVER_OVERRIDE=zink', 'GALLIUM_DRIVER=zink',
                 'LIBGL_KOPPER_DRI2=1']
        return env, ['bash', str(ROOT / 'termux/aw-debian'), '/usr/bin/env', *extra, *args]

    if route == 'wrapper-system26':
        # Diagnostic negative/control path only: existing system Mesa26 GLX
        # and Zink, with the same Qualcomm wrapper ICD. Never installs Mesa.
        env['LIBGL_DRIVERS_PATH'] = str(PREFIX / 'lib/dri')
        env['GALLIUM_DRIVER'] = 'zink'
        env['MESA_LOADER_DRIVER_OVERRIDE'] = 'zink'
        env['VK_DRIVER_FILES'] = str(PREFIX / 'share/vulkan/icd.d/wrapper_icd.aarch64.json')
        return env, args

    env['LD_LIBRARY_PATH'] = str(NATIVE) + (':' + os.environ['LD_LIBRARY_PATH']
                                          if os.environ.get('LD_LIBRARY_PATH') else '')
    env['LIBGL_DRIVERS_PATH'] = str(NATIVE / 'dri')
    env['GALLIUM_DRIVER'] = 'zink'
    env['MESA_LOADER_DRIVER_OVERRIDE'] = 'zink'
    env['VK_DRIVER_FILES'] = (
        str(ROOT / '.source/gpu-stage1/s24-wrapper-xcb-resize/runtime/wrapper_icd.private.json')
        if route == 'wrapper-s24' else
        str(ROOT / '.source/gpu-stage1/wrapper-private-runtime/wrapper_icd.private.json')
        if route == 'wrapper-private' else
        str(PREFIX / 'share/vulkan/icd.d/wrapper_icd.aarch64.json')
        if route == 'wrapper' else str(TURNIP))
    return env, args


def check_renderer(route, text, scene):
    vendor = ('QUALCOMM_PROPRIETARY' if route.startswith('wrapper') else 'MESA_TURNIP')
    if scene in ('glmark2', 'glxgears'):
        version = ('Mesa 26.3.0-devel' if route == 'wrapper-system26'
                   else 'Mesa 27.0.0-devel (git-6f90e92dfd)')
        return (version in text and
                'zink Vulkan' in text and vendor in text and
                (scene == 'glxgears' or 'glmark2 Score:' in text))
    return 'Selected GPU 0: Adreno (TM) 840' in text


def test_one(route, scene, pid, display, sampler, resize_own_window=False,
             resize_sizes=None, edge_sampler=None):
    env, command = client_command(route, scene, display, STATE / 'Xauthority')
    label = route + '-' + scene
    logfile = OUTPUT / (label + '.log')
    before = current_xids(env)
    process = None
    new_ids = set()
    window = None
    sample = None
    x_health = []
    rc = None
    try:
        with logfile.open('w') as out:
            process = subprocess.Popen(command, env=env, stdout=out,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        started = time.monotonic()
        # Allow a window to be mapped. Only examine new XIDs from this case.
        while time.monotonic() - started < 8:
            if process.poll() is not None:
                break
            new_ids = current_xids(env) - before
            for wid in new_ids:
                prop = run_quick(['xprop', '-id', wid, 'WM_CLASS', 'WM_STATE'], env)
                cls = prop.stdout.lower()
                # glmark2 on this Termux/Xwayland build creates a real
                # WM-managed 800x600 window but DOES NOT publish WM_CLASS.
                # A single newly mapped XID is still attributable to our
                # only just-launched test process in this serialized test.
                has_name = 'glmark2' in cls or 'vkcube' in cls or 'glxgears' in cls
                uniquely_mapped = len(new_ids) == 1 and 'window state: normal' in cls
                if 'window state: normal' in cls and (has_name or uniquely_mapped):
                    window = wid
                    if not has_name:
                        print('S23_LIVE[' + label + ']_WM_CLASS=MISSING_UNIQUE_MAPPED_XID', flush=True)
                    break
            if window:
                break
            time.sleep(.13)
        print('S23_LIVE[' + label + ']_WM_MAPPED=' + ('PASS' if window else 'NOT_CONFIRMED'), flush=True)
        if window:
            # XGetImage samples only OUR window and prints five small pixel
            # values. This isn't a physical Android screen capture.
            # PRoot Vulkan shader compilation can leave the very first
            # XGetImage frame all-black. Recheck for up to 2 seconds rather
            # than falsely failing a later fully drawn Vulkan window.
            sample_wait_start = time.monotonic()
            while True:
                result = run_quick([str(sampler), window], env, timeout=6)
                colors = re.findall(r'S16_X11_GETIMAGE_SAMPLE_\d+=0x([0-9a-fA-F]+)', result.stdout)
                varied = len(colors) == 5 and len(set(colors)) >= 2 and any(
                    int(value, 16) != 0 for value in colors)
                sample = (result.returncode == 0 and
                          'S16_X11_GETIMAGE_READBACK=5/5' in result.stdout and varied)
                if sample or time.monotonic() - sample_wait_start >= 2 or process.poll() is not None:
                    break
                time.sleep(.24)
            (OUTPUT / (label + '-pixels.log')).write_text(result.stdout + result.stderr)
            print('S23_LIVE[' + label + ']_X11_PIXMAP=' + ('PASS' if sample else 'NOT_CONFIRMED'), flush=True)
        resized = True
        if resize_own_window and window:
            # Move and resize ONLY the demo XID, never the user's terminal,
            # desktop, pointer, output configuration or XFCE settings.
            geometry_list = resize_sizes or ((900, 650), (1200, 700), (500, 500))
            for index, (width, height) in enumerate(geometry_list, 1):
                px = 120 + (index % 4) * 26
                py = 100 + (index % 4) * 18
                move = run_quick(['xdotool', 'windowmove', window,
                                  str(px), str(py)], env, timeout=4)
                size = run_quick(['xdotool', 'windowsize', window,
                                  str(width), str(height)], env, timeout=4)
                time.sleep(.35)
                snap = run_quick([str(sampler), window], env, timeout=6)
                dimension = re.search(r'S16_X11_GETIMAGE_WINDOW=.*?width:(\d+) height:(\d+)', snap.stdout)
                geometry_ok = (dimension is not None and
                               (int(dimension.group(1)), int(dimension.group(2))) == (width, height))
                colors = re.findall(r'S16_X11_GETIMAGE_SAMPLE_\d+=0x([0-9a-fA-F]+)', snap.stdout)
                content_ok = (len(colors) == 5 and len(set(colors)) >= 2 and
                              any(int(value, 16) != 0 for value in colors) and
                              'S16_X11_GETIMAGE_READBACK=5/5' in snap.stdout)
                edges_ok = True
                if edge_sampler is not None:
                    edges_ok = False
                    for attempt in range(4):
                        edge = run_quick([str(edge_sampler), window], env, timeout=5)
                        (OUTPUT / (label + '-resize-' + str(index) + '-edges.log')).write_text(
                            edge.stdout + edge.stderr)
                        if edge.returncode == 0:
                            edges_ok = True
                            break
                        time.sleep(.22)
                alive = run_quick(['xdpyinfo'], env, timeout=3).returncode == 0
                step_ok = (move.returncode == 0 and size.returncode == 0 and
                           geometry_ok and content_ok and edges_ok and alive and
                           process.poll() is None)
                (OUTPUT / (label + '-resize-' + str(index) + '.log')).write_text(
                    snap.stdout + snap.stderr)
                resized = resized and step_ok
                print('S23_LIVE[' + label + ']_RESIZE_' + str(index) + '=' +
                      ('PASS' if step_ok else 'FAIL') +
                      ' geometry=' + (str(dimension.group(1)) + 'x' + dimension.group(2)
                                      if dimension else 'unknown'), flush=True)
                if edge_sampler is not None:
                    print('S24_LIVE[' + label + ']_PADDED_ROW_' + str(index) + '=' +
                          ('PASS' if edges_ok else 'FAIL'), flush=True)
                if not step_ok:
                    print('S23_LIVE[' + label + ']_RESIZE_DIAG=' +
                          'move_rc=' + str(move.returncode) +
                          ' size_rc=' + str(size.returncode) +
                          ' xserver=' + str(alive) +
                          ' client_rc=' + str(process.poll()) +
                          ' new_xids=' + str(sorted(current_xids(env) - before)), flush=True)
                    print('S23_LIVE[' + label + ']_WINDOW_RESIZE_ERROR=' +
                          (move.stderr + size.stderr + snap.stderr + snap.stdout)[-360:], flush=True)
                    break
        elif resize_own_window:
            resized = False
        # At least one liveness check WHILE this actual GL/Vulkan app runs.
        x_health.append(run_quick(['xdpyinfo'], env, timeout=3).returncode == 0)
        if scene == 'glmark2':
            try:
                rc = process.wait(timeout=17)
            except subprocess.TimeoutExpired:
                rc = 124
        else:
            # Unbounded vkcube / glxgears gives enough time to verify a mapped window.
            # Successful GPU selection + mapped/readback are the Vulkan gate;
            # bounded termination is intentional rather than a natural exit.
            time.sleep(.65)
            rc = process.poll()
        contents = logfile.read_text(errors='replace')
        actual = check_renderer(route, contents, scene)
        print('S23_LIVE[' + label + ']_RENDERER=' + ('PASS' if actual else 'FAIL'), flush=True)
        if scene == 'glmark2':
            print('S23_LIVE[' + label + ']_CLIENT_EXIT=' + str(rc), flush=True)
        else:
            print('S23_LIVE[' + label + ']_CLIENT_STATUS=' +
                  ('ALIVE_UNTIL_INTENTIONAL_STOP' if process.poll() is None
                   else 'EXITED_' + str(process.poll())), flush=True)
        ok = (window is not None and sample and resized and actual and all(x_health) and
              (rc == 0 if scene == 'glmark2' else process.poll() is None))
        print('S23_LIVE[' + label + ']=' + ('PASS' if ok else 'FAIL'), flush=True)
        if not ok:
            for line in contents.splitlines()[-11:]:
                print('S23_LIVE_CLIENT_LOG=' + line[:180], flush=True)
        return ok
    finally:
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=3)
        print('S23_LIVE[' + label + ']_CLEANED=PASS', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--route', choices=('wrapper', 'wrapper-private', 'wrapper-s24', 'wrapper-system26',
                                            'turnip', 'debian', 'all'), default='all')
    parser.add_argument('--vulkan', action='store_true')
    parser.add_argument('--only-vulkan', action='store_true')
    parser.add_argument('--only-gears', action='store_true')
    parser.add_argument('--resize-own-window', action='store_true',
                        help='Move/resize only the newly created demo window')
    parser.add_argument('--resize-sizes', default='',
                        help='Comma-separated demo-only sizes e.g. 900x650,1200x700,500x500')
    parser.add_argument('--edge-check', action='store_true',
                        help='Check vkcube background through the last logical X11 row')
    opts = parser.parse_args()
    if opts.resize_own_window and not (opts.only_vulkan or opts.only_gears):
        parser.error('--resize-own-window applies only to resizable vkcube or glxgears')
    if opts.edge_check and not opts.only_vulkan:
        parser.error('--edge-check requires --only-vulkan')
    sizes = None
    if opts.resize_sizes:
        try:
            sizes = tuple(tuple(int(value) for value in pair.split('x'))
                          for pair in opts.resize_sizes.split(','))
        except ValueError:
            parser.error('--resize-sizes requires WIDTHxHEIGHT comma-separated pairs')
        if (not 1 <= len(sizes) <= 16 or
                any(len(pair) != 2 or pair[0] < 96 or pair[1] < 96 or
                    pair[0] > 1800 or pair[1] > 960 for pair in sizes)):
            parser.error('--resize-sizes must contain 1-16 bounded sizes')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    if subprocess.check_output(['git', '-C', str(BASE / 'source'), 'rev-parse', 'HEAD'],
                               text=True).strip() != SOURCE_HASH:
        raise RuntimeError('Mesa 27 source provenance mismatch')
    pid, display = require_private_session()
    env = dict(os.environ, DISPLAY=display, XAUTHORITY=str(STATE / 'Xauthority'))
    env.pop('WAYLAND_DISPLAY', None)
    if run_quick(['xdpyinfo'], env).returncode:
        raise RuntimeError('X11 preflight failed')
    sampler = OUTPUT / 's23-x11-pixmap-sample'
    subprocess.run(['clang', '--target=aarch64-linux-android35', '-O2', '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror',
                    str(ROOT / 'graphics/tests/s16-x11-pixmap-sample.c'),
                    '-lX11', '-o', str(sampler)], check=True, timeout=25)
    edge_sampler = None
    if opts.edge_check:
        edge_sampler = OUTPUT / 's24-xcb-resize-pixel-edges'
        subprocess.run(['clang', '--target=aarch64-linux-android35', '-O2', '-std=gnu11',
                        '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'graphics/tests/s24-xcb-resize-pixel-edges.c'),
                        '-lX11', '-o', str(edge_sampler)], check=True, timeout=25)
    routes = ('wrapper', 'turnip', 'debian') if opts.route == 'all' else (opts.route,)
    scenes = ('glxgears',) if opts.only_gears else ('vkcube',) if opts.only_vulkan else (
        ('glmark2', 'vkcube') if opts.vulkan else ('glmark2',))
    outcomes = []
    for route in routes:
        for scene in scenes:
            if require_private_session() != (pid, display):
                raise RuntimeError('live Xwayland session changed unexpectedly')
            outcomes.append(test_one(route, scene, pid, display, sampler,
                                     resize_own_window=opts.resize_own_window,
                                     resize_sizes=sizes, edge_sampler=edge_sampler))
            if run_quick(['xdpyinfo'], env).returncode:
                raise RuntimeError('X11 server became unresponsive after ' + route + '-' + scene)
            if not outcomes[-1]:
                print('S23_LIVE_REMAINING_CASES=SKIPPED_AFTER_FAILURE')
                return 3
    if require_private_session() != (pid, display):
        raise RuntimeError('live XFCE server PID changed')
    print('S23_LIVE_XFCE_EXISTING_SERVER=UNCHANGED')
    print('S23_LIVE_ANDROID_PHYSICAL_PIXELS=REQUIRES_USER_VISUAL_ACCEPTANCE')
    print('S23_LIVE_MESA27_WINDOWED_MATRIX=' + ('PASS' if all(outcomes) else 'FAIL'))
    return 0 if all(outcomes) else 4


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print('S23_LIVE_TEST_ERROR=' + str(error)[:250], flush=True)
        sys.exit(2)
