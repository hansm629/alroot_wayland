#!/data/data/com.termux/files/usr/bin/python3
"""S32 vs S31 nonblank last-valid-frame acceptance (disposable GPU Xwayland).

Compare the pinned S31 server against opt-in S32 on disposable :201..:240
Xwayland (optionally an isolated xfwm4). Never restart the running XFCE,
never touch Termux-X11 :0, system graphics libraries or an Android APK.
The test samples only its own vkcube frames and three colored GLX gears,
and requires content continuity for candidate acceptance.
It does not establish what the physical Android screen displays.
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
OUT = ROOT / '.source/gpu-stage1/s36-ahb-output'
LOGS = OUT / 'logs'
BASE = ROOT / '.source/gpu-stage1/s32-no-blank-present/runtime/Xwayland'
CANDIDATE = OUT / 'runtime/Xwayland'
S37_THIN_CANDIDATE = False
ICD = ROOT / '.source/gpu-stage1/s26-wrapper-resize-retry/runtime/wrapper_icd.private.json'
MESA_GLX = ROOT / '.source/gpu-stage27-pr25/mesa-private/glx-runtime/lib'
SIZE_PATTERNS = {
    'abrupt': ((350,250),(900,720),(550,400),(280,620),(1000,700),
               (290,290),(700,500),(210,570),(860,550),(550,320)),
    'gentle': tuple((480+(i if i<=45 else 90-i)*11,
                    380+(i if i<=45 else 90-i)*6) for i in range(91)),
    'extreme': ((300,250),(40,98),(8,98),(1,98),(4,101),
                (300,250),(17,99),(500,500),(1,98),(300,250)),
    # Exact S36 user video BadMatch 11 extents: 267x19, 826x8. Under
    # S24's padded AHB WSI, server may see 267x20. Previous test patterns
    # exercised only small *width*, missing the small-*height* failure.
    'thin': ((300,300),(267,19),(826,8),(300,300),(826,8),
             (267,19),(500,500),(350,12),(750,16),(300,300)),
}  # All geometry updates are restricted to the spawned demo's own XID.



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


def one_run(mode, demo, seconds, with_wm, tree, sampler, status_before,
            pattern="gentle", audit_composite_move=False):
    sizes = SIZE_PATTERNS[pattern]
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
    for opt in ('S37_KGSL_THIN_AHB_FD0', 'S37_DIAG_THIN_AHB',
                'S36_KGSL_ROOT_AHB_OUTPUT', 'S28_KGSL_RESIZE_PRESERVE','S29_KGSL_NATIVE_RESIZE',
                'S30_KGSL_STABLE_FRAME','S30_KGSL_TINY_AHB_FD0',
                'S31_KGSL_RESIZE_CONTENT_POLICY','S30_DIAG_TINY_AHB',
                'S30_TRACE_STABLE','S32_KGSL_NO_BLANK_PRESENT'):
        server_env.pop(opt, None)
    server_env['S30_KGSL_TINY_AHB_FD0'] = '1'
    server_env.pop('S31_RESIZE_DIAG', None)
    if mode == 'candidate':
        server_env['S32_KGSL_NO_BLANK_PRESENT'] = '1'
        server_env['S36_KGSL_ROOT_AHB_OUTPUT'] = '1'
        if S37_THIN_CANDIDATE:
            server_env['S37_KGSL_THIN_AHB_FD0'] = '1'
            server_env['S37_DIAG_THIN_AHB'] = '1'
    else:
        server_env['S32_KGSL_NO_BLANK_PRESENT'] = '1'
        if S37_THIN_CANDIDATE:
            # Baseline is S36 direct AHB with NO narrow-height FD fix.
            # An expected negative control may return BadMatch 11.
            server_env['S36_KGSL_ROOT_AHB_OUTPUT'] = '1'

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
    server = wm = app = observer = probe = None
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
                print('S32_ISOLATED_WM=XFWM4', flush=True)
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
            print(f'S32_{mode.upper()}_{demo.upper()}_DISPLAY={display} OWN_XID={xid}',
                  flush=True)
            marker = command(['xprop', '-id', xid, '_MESA_DRV'], client_env)
            for retry in range(25):
                if '_MESA_DRV(STRING)' in marker.stdout: break
                time.sleep(.08)
                marker = command(['xprop', '-id', xid, '_MESA_DRV'], client_env)
            if '_MESA_DRV(STRING)' not in marker.stdout:
                raise RuntimeError('private XCB Vulkan WSI marker not advertised')

            if audit_composite_move:
                # Entirely separate 1920x1080 rootful Xwayland + xfwm4,
                # with the SAME project-private KGSL executable/Wrapper.
                # Only this demo's XID is moved between two far-apart
                # positions: the ROOT COMPOSITOR's vacated old position,
                # not the app center, is checked for old gear pixels.
                program = OUT / 'runtime/s35-xcomposite-move-trail'
                result = subprocess.run([str(program), xid, '22'],
                                        env=client_env, capture_output=True,
                                        text=True, timeout=18)
                print(result.stdout.strip(), flush=True)
                if result.stderr:
                    print('S35_COMPOSITE_AUDIT_STDERR=' + result.stderr[-240:])
                if result.returncode:
                    raise RuntimeError('S35 composite trail audit did not pass')
                return 0

            if demo == 'vkcube':
                sample_log = stack.enter_context((LOGS / (label + '-corners.log')).open('w'))
                observer = subprocess.Popen(
                    [str(sampler), xid, str(seconds + 1), '8'],
                    env=client_env, stdout=sample_log, stderr=subprocess.STDOUT)
            if demo == 'vkcube':
                mlog = stack.enter_context((LOGS / (label + '-motion.log')).open('w'))
                probe = subprocess.Popen([str(OUT / 'runtime/s31-motion-probe'), xid, str(seconds+1)],
                                         env=client_env, stdout=mlog, stderr=subprocess.STDOUT)
            elif demo == 'zink':
                mlog = stack.enter_context((LOGS / (label + '-motion.log')).open('w'))
                probe = subprocess.Popen([str(OUT / 'runtime/s32-gears-color-probe'), xid, str(seconds+1)],
                                         env=client_env, stdout=mlog, stderr=subprocess.STDOUT)
            start = time.monotonic()
            count = 0
            while time.monotonic() - start < seconds and app.poll() is None:
                width, height = sizes[count % len(sizes)]
                count += 1
                p = command(['xdotool', 'windowsize', xid, str(width), str(height)],
                            client_env, timeout=3)
                if p.returncode:
                    raise RuntimeError('test-owned XCB window resize failed: ' + p.stderr[-180:])
                time.sleep(.17)
            app_alive = app.poll() is None
            if observer:
                observer.wait(timeout=7)
            if probe:
                probe.wait(timeout=7)
                content = (LOGS / (label + '-motion.log')).read_text()
                print(content.strip(), flush=True)
                if mode == 'candidate' and demo == 'vkcube':
                    observed = re.search(r'S31_MOTION frames=(\d+) geometry_changes=\d+ '
                                         r'drawn=(\d+) blank=(\d+) black=\d+ '
                                         r'hash_changes=(\d+) max_blank_samples=(\d+)', content)
                    if not observed:
                        raise RuntimeError('S32 missing real Vulkan content measurement')
                    frames, drawn, blank, changed, blank_run = map(int, observed.groups())
                    if frames < 15 or drawn * 100 < frames * 85 or changed < 8 or blank_run > 3:
                        raise RuntimeError('S32 Vulkan blank/frozen-frame regression')
                    print('S32_VKCUBE_RENDERED_CONTENT_DURING_RESIZE=PASS', flush=True)
                if mode == 'candidate' and demo == 'zink':
                    observed = re.search(r'S32_GLX_COLOR_SAMPLES=(\d+) COLORED=(\d+) '
                                         r'BLANK=(\d+) MAX_BLANK_RUN=(\d+) '
                                         r'RED=(\d+) GREEN=(\d+) BLUE=(\d+) '
                                         r'CHANGED=(\d+)', content)
                    if not observed:
                        raise RuntimeError('S32 missing colored gear content measurement')
                    frames, colored, blank, blank_run, red, green, blue, changed = map(int, observed.groups())
                    if (frames < 15 or colored * 100 < frames * 85 or
                            min(red, green, blue) * 2 < frames or
                            changed < 8 or blank_run > 3):
                        raise RuntimeError('S32 GLX color disappearance/frozen gear regression')
                    print('S32_RGB_GLX_GEARS_DURING_RESIZE=PASS', flush=True)
            if mode == 'candidate':
                logtext = (LOGS / (label + '-server.log')).read_text(errors='replace')
                if 'S36_KGSL_ROOT_AHB_DIRECT_GLAMOR_FBO=ACTIVE' not in logtext or \
                   'direct_AHB_backing=1' not in logtext:
                    raise RuntimeError('S36 AHB direct GLAMOR pixmap not active')
                if 'invalid object' in logtext or 'failed to dispatch Wayland events' in logtext:
                    raise RuntimeError('S36 private AHB Wayland protocol generation regression')
                print('S36_REAL_ROOTFUL_AHB_GLAMOR_BACKING=PASS', flush=True)
            print(f'S32_{mode.upper()}_{demo.upper()}_RESIZES={count} CLIENT_ALIVE={app_alive}',
                  flush=True)
            if not app_alive:
                raise RuntimeError('test client exited while resizing')
            if demo == 'vkcube':
                # Even after 1x98 and 8x98, restoring 500x500 must show
                # newly ADVANCING cube frames, not a static/recropped image.
                resize_back = command(['xdotool', 'windowsize', xid, '500', '500'],
                                      client_env, timeout=3)
                if resize_back.returncode:
                    raise RuntimeError('cannot restore own X window after tiny resize')
                time.sleep(.6)
                anim = subprocess.run(
                    [str(OUT / 'runtime/s30-vkcube-animation'), xid],
                    env=client_env, capture_output=True, text=True, timeout=8)
                print(anim.stdout.strip(), flush=True)
                if anim.returncode:
                    raise RuntimeError('real Vulkan animation did not recover after tiny resize')
            black_events = -1
            if demo == 'vkcube':
                contents = (LOGS / (label + '-corners.log')).read_text()
                lines = {}
                for side in ('tl','tr','bl','br','right','right_in','bottom','bottom_in','bottom_deep'):
                    match = re.search(r'S29_EDGE_' + side + r' samples=(\d+) black=(\d+) off_grey=(\d+)', contents)
                    if not match:
                        raise RuntimeError('missing edge sample: '+side)
                    lines[side] = tuple(map(int, match.groups()))
                print('S32_' + mode.upper() + '_EDGE_BLACK: ' +
                      ' '.join(side+'='+str(lines[side][1]) for side in lines), flush=True)
                print('S32_' + mode.upper() + '_EDGE_OFF_GREY: ' +
                      ' '.join(side+'='+str(lines[side][2]) for side in lines), flush=True)
                black_events = lines['right'][1] + lines['bottom'][1]
            return black_events
    except Exception as exc:
        error = exc
        raise
    finally:
        end_process(observer)
        end_process(probe)
        end_process(app, group=True)
        end_process(wm, group=True)
        end_process(server, group=True)
        shutil.rmtree(tmp, ignore_errors=True)
        if (xdir / f'X{available}').exists():
            print('S32_PRIVATE_X_SERVER_SOCKET_NOT_CLEANED', flush=True)
        same = subprocess.run(['awstart', '--status'], capture_output=True,
                              text=True, timeout=8).stdout
        if (status_before.split('xwayland: ')[1].splitlines()[0] !=
                same.split('xwayland: ')[1].splitlines()[0]):
            raise RuntimeError('running user Xwayland PID changed') from error
        print('S32_USER_XFCE_XWAYLAND_UNCHANGED=PASS', flush=True)


def main():
    global OUT, LOGS, BASE, CANDIDATE, S37_THIN_CANDIDATE
    p = argparse.ArgumentParser()
    p.add_argument('--mode', choices=('baseline', 'candidate'),
                   default='candidate', help='baseline=S32 linear DMA-BUF; candidate=S36 direct AHB')
    p.add_argument('--demo', choices=('vkcube', 'zink'), default='vkcube')
    p.add_argument('--wm', action='store_true', help='start isolated XFWM4')
    p.add_argument('--seconds', type=int, default=8)
    p.add_argument('--pattern', choices=tuple(SIZE_PATTERNS), default='gentle',
                   help='gentle drags, abrupt aspect changes or 1px extreme')
    p.add_argument('--audit-composite-move-trails', action='store_true',
                   help='sample the isolated Xfwm4 COMPOSITED OVERLAY after glxgears movement')
    p.add_argument('--s37-thin-ahb', action='store_true',
                   help='compare S36 and opt-in S37 on thin-height AHB (267x19 / 826x8)')
    options = p.parse_args()
    if options.s37_thin_ahb:
        S37_THIN_CANDIDATE = True
        OUT = ROOT / '.source/gpu-stage1/s37-thin-ahb'
        LOGS = OUT / 'logs'
        BASE = ROOT / '.source/gpu-stage1/s36-ahb-output/runtime/Xwayland'
        CANDIDATE = OUT / 'runtime/Xwayland'
    if not 3 <= options.seconds <= 35:
        p.error('duration must be 3..35 seconds per case')
    if options.audit_composite_move_trails and (options.demo != 'zink' or not options.wm):
        p.error('--audit-composite-move-trails requires --demo zink --wm')
    if not CANDIDATE.is_file() or not BASE.is_file() or not ICD.is_file():
        raise RuntimeError('missing pinned S31/S32 private Xwayland or S26 Wrapper')
    if not (STATE / 'runtime/wayland-0').is_socket():
        raise RuntimeError('Alroot Wayland host not running')

    import hashlib
    pinned = (
        ((BASE, 'df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5'),
         (CANDIDATE, '193f43e03f0e754c39cfb73e04ac1e0032f8f632e6d06b5d1b675f67c834a594'))
        if options.s37_thin_ahb else
        ((BASE, 'b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455'),
         (CANDIDATE, 'df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5'))
    )
    for path, digest in pinned:
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError('private Xwayland binary not pinned: ' + str(path))
    LOGS.mkdir(parents=True, exist_ok=True)
    sampler = OUT / 'runtime/s31-vkcube-edge-sampler'
    tree = OUT / 'runtime/s31-window-tree'
    for source, output in (('s29-vkcube-edge-sampler.c', sampler),
                           ('s28-x11-window-tree.c', tree),
                           ('s30-vkcube-animation.c', OUT / 'runtime/s30-vkcube-animation'),
                           ('s31-resize-motion-probe.c', OUT / 'runtime/s31-motion-probe'),
                           ('s32-gears-color-probe.c', OUT / 'runtime/s32-gears-color-probe')):
        subprocess.run(['clang', '--target=aarch64-linux-android35', '-O2',
                        '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'graphics/tests' / source), '-lX11',
                        '-o', str(output)], check=True, timeout=30)

    if options.audit_composite_move_trails:
        subprocess.run(['clang', '--target=aarch64-linux-android35', '-O2',
                        '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'graphics/tests/s35-xcomposite-move-trail.c'),
                        '-lX11', '-lXcomposite',
                        '-o', str(OUT / 'runtime/s35-xcomposite-move-trail')],
                       check=True, timeout=30)

    status = subprocess.check_output(['awstart', '--status'], text=True, timeout=8)
    modes = (options.mode,)
    outcomes = {}
    for mode in modes:
        outcomes[mode] = one_run(mode, options.demo, options.seconds,
                                 options.wm, tree, sampler, status,
                                 pattern=options.pattern,
                                 audit_composite_move=options.audit_composite_move_trails)
    print('S32_PHYSICAL_ANDROID_FLICKER=USER_VISUAL_TEST_REQUIRED', flush=True)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:
        print('S32_ISOLATED_TEST_ERROR=' + str(exc)[:260], flush=True)
        raise SystemExit(2)
