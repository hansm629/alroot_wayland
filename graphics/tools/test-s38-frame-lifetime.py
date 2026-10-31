#!/data/data/com.termux/files/usr/bin/python3
"""S37/S38 root-frame lifetime regression on an authenticated isolated Xwayland.

Three short resize bursts touch only a spawned demo's XID. Optional --storm
adds diagnostic synchronous X11 round trips. FD counts after each two-second
idle expose retained root AHBs. This does not prove physical-screen smoothness.
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
import tempfile
import time

ROOT = Path('/data/data/com.termux/files/home/alroot_wayland')
TERMUX_HOME = ROOT.parent
PREFIX = Path('/data/data/com.termux/files/usr')
STATE = TERMUX_HOME / '.alroot-wayland'
OUT = ROOT / '.source/gpu-stage1/s38-frame-continuity'
BASE = ROOT / '.source/gpu-stage1/s37-thin-ahb/runtime/Xwayland'
BASE_SHA256 = '193f43e03f0e754c39cfb73e04ac1e0032f8f632e6d06b5d1b675f67c834a594'
FINAL_SHA256 = '3f27886fae2bde560d34ca53dedacf4cec09fd9dbcddc591efafc4ec9bc994c2'
PROBE_SHA256 = '521c727ef10f270e70d548ac1a4e7951c802702cf25850066165b2d3e49502f5'
ICD = ROOT / '.source/gpu-stage1/s26-wrapper-resize-retry/runtime/wrapper_icd.private.json'
MESA_GLX = ROOT / '.source/gpu-stage27-pr25/mesa-private/glx-runtime/lib'


def command(args, env=None, timeout=4, check=False):
    return subprocess.run(args, env=env, capture_output=True, text=True,
                          timeout=timeout, check=check)


def own_windows(env):
    p = command(['xprop', '-root', '_NET_CLIENT_LIST'], env, check=True)
    return set(re.findall(r'0x[0-9a-fA-F]+', p.stdout))


def find_window(env, before, app_pid):
    candidates = []
    for xid in own_windows(env) - before:
        p = command(['xprop', '-id', xid, 'WM_STATE', '_NET_WM_PID'], env)
        if p.returncode or 'window state: Normal' not in p.stdout:
            continue
        pid = re.search(r'_NET_WM_PID\(CARDINAL\) = (\d+)', p.stdout)
        if pid and int(pid.group(1)) != app_pid:
            continue
        candidates.append(xid)
    return candidates[0] if len(candidates) == 1 else None


def stop_owned(proc):
    if proc is None:
        return
    # Every process here was created with its own session. Kill only that
    # session's process group, including our dbus-run-session children.
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


def user_session():
    p = command(['awstart', '--status'], timeout=8, check=True)
    return {key: re.search(r'^' + key + r':\s*(.+)$', p.stdout, re.M).group(1)
            for key in ('xwayland', 'xfce')
            if re.search(r'^' + key + r':\s*(.+)$', p.stdout, re.M)}


def fd_sample(pid):
    descriptors = list(Path(f'/proc/{pid}/fd').iterdir())
    status = Path(f'/proc/{pid}/status').read_text()
    rss = re.search(r'^VmRSS:\s*(\d+)', status, re.M)
    dmabuf = 0
    for descriptor in descriptors:
        try:
            dmabuf += 'dmabuf' in os.readlink(descriptor)
        except OSError:
            pass
    return {'fd': len(descriptors), 'dmabuf': dmabuf, 'rss_kib': int(rss.group(1)) if rss else None}


def ensure_ready(proc, args, env, marker=None, attempts=60):
    for _ in range(attempts):
        if proc.poll() is not None:
            raise RuntimeError('owned server/WM exited before ready')
        try:
            p = command(args, env, timeout=2)
            if p.returncode == 0 and (marker is None or marker in p.stdout):
                return
        except subprocess.TimeoutExpired:
            pass
        time.sleep(.10)
    raise RuntimeError('owned server/WM readiness timeout')


def run(args, binary, digest, stress, animation):
    prefix_tmp = PREFIX / 'tmp'
    xdir = prefix_tmp / '.X11-unix'
    display_no = next((n for n in range(201, 241) if not any(p.exists() for p in (
        xdir / f'X{n}', prefix_tmp / f'.X{n}-lock', prefix_tmp / f'.tX{n}-lock'))), None)
    if display_no is None:
        raise RuntimeError('no free isolated :201..:240 display')
    display = f':{display_no}'
    temporary = Path(tempfile.mkdtemp(prefix='alroot-s38-', dir=prefix_tmp))
    auth = temporary / 'Xauthority'
    auth.touch(mode=0o600)
    command(['xauth', '-f', str(auth), 'add', display, 'MIT-MAGIC-COOKIE-1',
             secrets.token_hex(16)], timeout=5, check=True)
    stamp = time.strftime('%Y%m%d-%H%M%S')
    logdir = OUT / 'logs' / f'lifetime-{args.mode}-{args.demo}-{stamp}-{os.getpid()}'
    logdir.mkdir(parents=True)
    old_session = user_session()
    server = wm = app = None
    summary = {'mode': args.mode, 'demo': args.demo, 'binary_sha256': digest,
               'storm': args.storm, 'display': display, 'bursts': []}
    server_env = os.environ.copy()
    for key in list(server_env):
        if key in ('DISPLAY', 'LD_PRELOAD', 'DBUS_SESSION_BUS_ADDRESS', 'SESSION_MANAGER') or \
                re.match(r'^S(?:28|29|30|31|32|35|36|37|38)_', key):
            server_env.pop(key, None)
    server_env.update(XDG_RUNTIME_DIR=str(STATE / 'runtime'), WAYLAND_DISPLAY='wayland-0',
        XAUTHORITY=str(auth), TMPDIR=str(prefix_tmp), XWAYLAND_FORCE_KGSL_SURFACELESS='1',
        FD_KGSL_ENABLE_DMABUF='1', MESA_LOADER_DRIVER_OVERRIDE='kgsl',
        GALLIUM_DRIVER='freedreno', EGL_PLATFORM='surfaceless',
        LD_LIBRARY_PATH=str(ROOT / 'packages/prefix/lib'),
        S30_KGSL_TINY_AHB_FD0='1', S32_KGSL_NO_BLANK_PRESENT='1',
        S36_KGSL_ROOT_AHB_OUTPUT='1', S37_KGSL_THIN_AHB_FD0='1')
    if args.mode == 'candidate':
        server_env['S38_KGSL_FRAME_LIFETIME'] = '1'
    client_env = os.environ.copy()
    for key in ('WAYLAND_DISPLAY', 'LD_PRELOAD', 'LD_LIBRARY_PATH', 'VK_DRIVER_FILES',
                'VK_ICD_FILENAMES', 'MESA_LOADER_DRIVER_OVERRIDE', 'GALLIUM_DRIVER',
                'LIBGL_DRIVERS_PATH', 'LIBGL_ALWAYS_SOFTWARE', 'DBUS_SESSION_BUS_ADDRESS'):
        client_env.pop(key, None)
    client_env.update(DISPLAY=display, XAUTHORITY=str(auth), VK_DRIVER_FILES=str(ICD),
        VK_ICD_FILENAMES=str(ICD), S24_WRAPPER_XCB_AHB_PAD_HEIGHT='1',
        S26_WRAPPER_XCB_RETRY_TRANSIENT='1', LIBGL_KOPPER_DRI2='1')
    if args.demo == 'zink':
        client_env.update(LD_LIBRARY_PATH=str(MESA_GLX),
            LIBGL_DRIVERS_PATH=str(MESA_GLX / 'dri'),
            MESA_LOADER_DRIVER_OVERRIDE='zink', GALLIUM_DRIVER='zink')
        app_cmd = [str(PREFIX / 'bin/glxgears')]
    else:
        app_cmd = [str(PREFIX / 'bin/vkcube'), '--wsi', 'xcb']
    try:
        with ExitStack() as stack:
            def log(name):
                return stack.enter_context((logdir / name).open('w'))
            server = subprocess.Popen([str(binary), display, '-geometry', '1920x1080',
                '-dpi', '96', '-auth', str(auth), '-glamor', 'es', '-nolisten', 'tcp'],
                env=server_env, stdout=log('server.log'), stderr=subprocess.STDOUT,
                start_new_session=True)
            ensure_ready(server, ['xdpyinfo'], client_env)
            wm_env = server_env.copy()
            wm_env.update(DISPLAY=display, GDK_BACKEND='x11', XDG_SESSION_TYPE='x11',
                XDG_CONFIG_HOME=str(temporary / 'config'), XDG_CACHE_HOME=str(temporary / 'cache'))
            wm_env.pop('WAYLAND_DISPLAY', None)
            wm = subprocess.Popen(['dbus-run-session', '--', 'xfwm4', '--compositor=on',
                '--display', display], env=wm_env, stdout=log('xfwm.log'),
                stderr=subprocess.STDOUT, start_new_session=True)
            ensure_ready(wm, ['xprop', '-root', '_NET_SUPPORTING_WM_CHECK'], client_env, 'window id #')
            before = own_windows(client_env)
            app = subprocess.Popen(app_cmd, env=client_env, stdout=log('client.log'),
                                   stderr=subprocess.STDOUT, start_new_session=True)
            xid = None
            for _ in range(60):
                if app.poll() is not None:
                    break
                xid = find_window(client_env, before, app.pid)
                if xid:
                    break
                time.sleep(.10)
            if not xid:
                raise RuntimeError(f'no unique owned mapped demo window: code={app.poll()}')
            summary.update(server_pid=server.pid, own_xid=xid)
            time.sleep(.5)
            summary['initial'] = fd_sample(server.pid)
            for burst in range(1, 4):
                if app.poll() is not None or server.poll() is not None:
                    raise RuntimeError('owned demo/server exited before stress')
                before_fd = fd_sample(server.pid)
                result = command([str(stress), xid, str(args.burst_ms), str(args.step_ms),
                                  '128' if args.storm else '0'], client_env,
                                 timeout=args.burst_ms / 1000 + 5)
                (logdir / f'burst-{burst}.log').write_text(result.stdout + result.stderr)
                immediate = fd_sample(server.pid)
                time.sleep(2)
                settled = fd_sample(server.pid)
                alive = app.poll() is None and server.poll() is None
                row = {'burst': burst, 'before': before_fd, 'after': immediate,
                       'after_idle_2s': settled, 'client_server_alive': alive,
                       'stress_exit': result.returncode}
                summary['bursts'].append(row)
                print('S38_FD_BURST=' + json.dumps(row, sort_keys=True), flush=True)
                if result.returncode or not alive:
                    raise RuntimeError('stress error or owned demo/server exit')
            settled = [row['after_idle_2s']['fd'] for row in summary['bursts']]
            growth = settled[-1] - settled[0]
            total = settled[-1] - summary['initial']['fd']
            summary.update(retained_growth_after_warmup=growth, retained_growth_total=total)
            if args.mode == 'baseline':
                summary['baseline_leak'] = 'OBSERVED' if growth >= 15 else 'NOT_REPRODUCED'
                print('S38_BASELINE_LEAK=' + summary['baseline_leak'], flush=True)
            else:
                bounded = growth <= 10 and total <= 30
                summary['candidate_fd_bounded'] = bounded
                print('S38_CANDIDATE_FD_BOUNDED=' + ('PASS' if bounded else 'FAIL'), flush=True)
                if not bounded:
                    raise RuntimeError(f'retained FD growth: tail={growth}, total={total}')
            if args.demo == 'vkcube':
                # Helper restores its original extent. No resizing occurs during
                # this probe: geometry-changing hashes cannot count as animation.
                command(['xdotool', 'windowsize', xid, '500', '500'], client_env, check=True)
                time.sleep(.6)
                result = command([str(animation), xid], client_env, timeout=8)
                (logdir / 'fixed-size-animation.log').write_text(result.stdout + result.stderr)
                summary['fixed_size_animation_exit'] = result.returncode
                print('S38_FIXED_SIZE_AFTER_STRESS=' + result.stdout.strip(), flush=True)
                if result.returncode:
                    raise RuntimeError('fixed-size cube animation failed after stress')
            else:
                command(['xdotool', 'windowsize', xid, '500', '500'], client_env, check=True)
                time.sleep(.6)
                result = command([str(OUT / 'runtime/s32-gears-color-probe'), xid, '2'],
                                 client_env, timeout=8)
                (logdir / 'fixed-size-gears.log').write_text(result.stdout + result.stderr)
                values = re.search(r'SAMPLES=(\d+) COLORED=(\d+).*CHANGED=(\d+) '
                                   r'INVALID=(\d+) ERRORS=(\d+)', result.stdout)
                if not values or result.returncode:
                    raise RuntimeError('missing fixed-size GLX content measurement')
                samples, colored, changed, invalid, errors = map(int, values.groups())
                advancing = samples >= 15 and colored * 100 >= samples * 85 and \
                    changed >= 4 and invalid == 0 and errors == 0
                summary['fixed_size_gears_advancing'] = advancing
                print('S38_FIXED_SIZE_AFTER_STRESS=' + result.stdout.strip(), flush=True)
                if not advancing:
                    raise RuntimeError('fixed-size gears frozen or missing after stress')
            faults = (logdir / 'server.log').read_text(errors='replace')
            summary['server_fault'] = bool(re.search(
                r'invalid object|failed to dispatch Wayland events|Fatal server error|Segmentation fault', faults))
            if summary['server_fault']:
                raise RuntimeError('owned Xwayland fault recorded')
            if args.freshness:
                fresh = OUT / 'runtime/s38-present-freshness'
                result = command([str(fresh)], client_env, timeout=12)
                (logdir / 'present-freshness.log').write_text(result.stdout + result.stderr)
                summary['present_freshness_exit'] = result.returncode
                print('S38_PRESENT_FRESHNESS=' + result.stdout.strip()[-900:], flush=True)
                if result.returncode:
                    raise RuntimeError('optional exact Present freshness failed')
    except Exception as exc:
        summary['error'] = str(exc)
        raise
    finally:
        stop_owned(app)
        stop_owned(wm)
        stop_owned(server)
        shutil.rmtree(temporary, ignore_errors=True)
        summary['user_session_unchanged'] = user_session() == old_session
        (logdir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        print('S38_SUMMARY=' + str(logdir / 'summary.json'), flush=True)
        print('S38_PHYSICAL_ANDROID_SMOOTHNESS=NOT_TESTED', flush=True)
        if not summary['user_session_unchanged']:
            raise RuntimeError('existing user Xwayland/XFCE state changed')
        print('S38_USER_XFCE_XWAYLAND_UNCHANGED=PASS', flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mode', choices=('baseline', 'candidate'), default='candidate')
    p.add_argument('--demo', choices=('vkcube', 'zink'), default='vkcube')
    p.add_argument('--storm', action='store_true', help='diagnostic 128 round trips per resize step')
    p.add_argument('--burst-ms', type=int, default=1500)
    p.add_argument('--step-ms', type=int, default=16)
    p.add_argument('--candidate', type=Path, default=OUT / 'runtime/Xwayland')
    p.add_argument('--candidate-sha256', help='exact reviewed digest for an explicitly selected diagnostic candidate')
    p.add_argument('--freshness', action='store_true', help='also run exact Present freshness probe')
    args = p.parse_args()
    if not 250 <= args.burst_ms <= 2000 or not 10 <= args.step_ms <= 20:
        p.error('three bursts must total <=6s; each 250..2000ms, step 10..20ms')
    binary = BASE if args.mode == 'baseline' else args.candidate
    if not binary.is_file() or not ICD.is_file():
        raise RuntimeError('missing private Xwayland or S26 wrapper')
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    expected = BASE_SHA256 if args.mode == 'baseline' else args.candidate_sha256
    if expected is None and binary == OUT / 'runtime/Xwayland':
        expected = FINAL_SHA256
    if expected is None and binary.name == 'Xwayland.root-probe':
        expected = PROBE_SHA256
    if expected is None or digest != expected:
        raise RuntimeError('candidate needs an exact reviewed --candidate-sha256; digest mismatch or missing')
    if not (STATE / 'runtime/wayland-0').is_socket():
        raise RuntimeError('Alroot Wayland host not running')
    runtime = OUT / 'runtime'
    runtime.mkdir(parents=True, exist_ok=True)
    stress = runtime / 's38-frame-release-stress'
    animation = runtime / 's30-vkcube-animation'
    sources = [('s38-frame-release-stress.c', stress)]
    if args.demo == 'vkcube':
        sources.append(('s30-vkcube-animation.c', animation))
    else:
        sources.append(('s32-gears-color-probe.c', runtime / 's32-gears-color-probe'))
    if args.freshness:
        sources.append(('s38-present-freshness.c', runtime / 's38-present-freshness'))
    for source, target in sources:
        command(['clang', '--target=aarch64-linux-android35', '-O2', '-std=gnu11',
                 '-Wall', '-Wextra', '-Werror', str(ROOT / 'graphics/tests' / source),
                 '-lX11', *(['-lXpresent'] if source == 's38-present-freshness.c' else []),
                 '-o', str(target)], timeout=30, check=True)
    run(args, binary, digest, stress, animation)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:
        print('S38_FRAME_LIFETIME_ERROR=' + str(exc)[:300], flush=True)
        raise SystemExit(2)
