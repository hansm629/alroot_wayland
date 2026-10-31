#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Run one bounded S44 benchmark; root schedules thermal gates and paired runs.
Scene FPS differs from process-lifetime profiling/wall/CPU counters, which
include setup and teardown; neither is GPU time or physical Android FPS.
"""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time
ROOT = Path(__file__).resolve().parents[2]
PREFIX = Path('/data/data/com.termux/files/usr')
OUT = ROOT / '.source/gpu-stage1/s44-sync-overhead/benchmarks'
spec = importlib.util.spec_from_file_location('s43_session', Path(__file__).with_name('test-s43-vkcube-default.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def identity(path, expected=None):
    path = path.resolve(strict=True)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    require(expected is None or digest == expected, 'hash mismatch: ' + str(path))
    return {'path': str(path), 'sha256': digest}

def snapshot(pid):
    result = {'monotonic_ns': time.monotonic_ns(), 'scope': 'main_thread_schedstat'}
    for name in ('cpuset', 'schedstat'):
        try:
            result[name] = Path(f'/proc/{pid}/{name}').read_text().strip()
        except OSError:
            result[name] = None
    return result

def map_check(pid, expected):
    text = Path(f'/proc/{pid}/maps').read_text()
    matches = set()
    for line in text.splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) != 6 or not fields[5].startswith('/'):
            continue
        if re.search(r'/lib(?:vulkan_wrapper|GLX_mesa|EGL_mesa|gallium)[^/]*\.so', fields[5]):
            require(not fields[5].endswith(' (deleted)'), 'deleted runtime library mapping')
            matches.add(str(Path(fields[5]).resolve(strict=True)))
    wanted = {entry['path'] for entry in expected.values()}
    require(not matches - wanted, 'unexpected selected-driver mapping: ' + repr(sorted(matches - wanted)))
    return wanted.issubset(matches), text

def stop_owned(proc):
    if proc is None or proc.returncode is not None:
        return None
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
        if pid:
            proc.returncode = os.waitstatus_to_exitcode(status)
            return usage
        time.sleep(.05)
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    _, status, usage = os.wait4(proc.pid, 0)
    proc.returncode = os.waitstatus_to_exitcode(status)
    return usage

def parse_log(text):
    parsed = {'fps': [], 'wsi': [], 'zink': [], 'wrapper': []}
    for line in text.splitlines():
        if 'S44_EGL_BENCH ' in line:
            values = dict(re.findall(r'(\w+)=([^\s]+)', line.split('S44_EGL_BENCH ', 1)[1]))
            parsed['fps'].append({'fps': float(values['fps']), 'frames': int(values['frames']),
                                 'seconds': float(values['seconds']), 'interval': int(values['interval']), 'line': line})
        if match := re.search(r'\bFPS:\s*([0-9]+(?:\.[0-9]+)?)', line, re.I):
            parsed['fps'].append({'fps': float(match[1]), 'line': line})
        for prefix, key in [('S44_WSI_PROFILE ', 'wsi'), ('S44_ZINK_STATS ', 'zink')]:
            if prefix in line:
                parsed[key].append(json.loads(line.split(prefix, 1)[1]))
        if 'S44_WRAPPER_SUBMITS ' in line:
            values = dict(re.findall(r'(\w+)=([^\s]+)', line.split('S44_WRAPPER_SUBMITS ', 1)[1]))
            parsed['wrapper'].append({k: int(v) if v.isdigit() else v for k, v in values.items()})
    return parsed

def check_profile(parsed, variant, need_zink, egl, probe):
    require(parsed['wrapper'], 'requested Wrapper profiling records missing')
    chains = [r for r in parsed['wsi'] if r.get('event') == 'chain']
    if not egl:
        require(parsed['wsi'], 'requested Vulkan WSI profiling records missing')
    if need_zink:
        require(parsed['zink'], 'requested Zink profiling records missing')
    if probe:
        require(chains and all(r.get('backend') == 'wayland' for r in chains), 'native Wayland WSI chain missing')
    if variant == 'default':
        require(all(r.get('enabled') == 1 for r in parsed['wrapper']), 'compiled Wrapper default is not enabled')
        if need_zink:
            require(all(r.get('compact_submits') == 1 for r in parsed['zink']), 'compiled Zink default is not enabled')
    return len(chains)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wrapper-icd', type=Path, required=True)
    parser.add_argument('--wrapper-sha256', required=True)
    parser.add_argument('--mesa-prefix', type=Path)
    parser.add_argument('--case', choices=('vk-xcb-light', 'vk-wayland-light', 'glx-light',
                                          'vk-xcb-heavy', 'vk-wayland-heavy', 'glx-heavy', 'egl-wayland-light',
                                          'vkcube-wayland-probe'), required=True)
    parser.add_argument('--variant', choices=('baseline', 'wrapper', 'zink', 'both', 'default'), required=True)
    parser.add_argument('--profile', action='store_true')
    parser.add_argument('--duration', type=int, default=4)
    parser.add_argument('--frames', type=int, default=2400)
    parser.add_argument('--label', required=True)
    args = parser.parse_args()
    require(2 <= args.duration <= 8, 'duration must be 2..8 seconds')
    require(1 <= args.frames <= 20000, 'frames must be 1..20000')
    egl = args.case == 'egl-wayland-light'
    probe = args.case == 'vkcube-wayland-probe'
    require(not egl or args.mesa_prefix, 'egl-wayland-light requires --mesa-prefix')
    require(not probe or args.profile, 'vkcube-wayland-probe requires --profile for wait attribution')
    require(re.fullmatch(r'[A-Za-z0-9_-]{1,64}', args.label), 'unsafe label')
    require(re.fullmatch(r'[0-9a-f]{64}', args.wrapper_sha256), 'invalid Wrapper SHA256')
    logdir = OUT / (args.label + '-' + time.strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
    logdir.mkdir(parents=True)
    summary = {'status': 'RUNNING', 'case': args.case, 'variant': args.variant,
               'profile': args.profile, 'duration': args.duration,
               'scene_fps_applicable': not probe,
               'scope': ('finite Vulkan wait attribution; no scene FPS; ' if probe else 'scene FPS; ') +
                        'process-lifetime profiling, wall time and CPU usage'}
    proc, usage, failure, before, lock = None, None, None, None, None
    try:
        lock = (OUT / 'benchmark.lock').open('a')
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        before = base.session_stamp()
        summary['session_before'] = before
        env = base.desktop_environment(before)
        env.pop('WAYLAND_DEBUG', None)
        for name in list(env):
            if name.startswith('S44_'):
                env.pop(name)
        require(not env.get('LD_PRELOAD'), 'runtime LD_PRELOAD must be absent')
        icd = args.wrapper_icd.resolve(strict=True)
        raw = Path(json.loads(icd.read_text())['ICD']['library_path'])
        expected = {'wrapper': identity(raw if raw.is_absolute() else icd.parent / raw, args.wrapper_sha256)}
        summary['icd'] = identity(icd)
        mesa = (args.mesa_prefix or PREFIX).resolve(strict=True)
        glx = args.case.startswith('glx-')
        if glx or args.mesa_prefix:
            mesa_paths = {name: (mesa / relative).resolve(strict=True) for name, relative in
                          {'glx': 'lib/libGLX_mesa.so', 'gallium': 'lib/libgallium-27.0.0-devel.so',
                           'egl': 'lib/libEGL_mesa.so', 'dri': 'lib/dri', 'gbm': 'lib/gbm'}.items()}
            require(all(p.is_relative_to(mesa) for p in mesa_paths.values()), 'Mesa path escapes selected prefix')
            if glx or egl:
                expected.update({name: identity(mesa_paths[name]) for name in ('egl' if egl else 'glx', 'gallium')})
        if args.mesa_prefix and mesa != PREFIX.resolve(strict=True):
            egl_json = logdir / 'egl-vendor.json'
            egl_json.write_text(json.dumps({'file_format_version': '1.0.0', 'ICD': {'library_path': str(mesa_paths['egl'])}}))
            env.update(LD_LIBRARY_PATH=str(mesa / 'lib'), LIBGL_DRIVERS_PATH=str(mesa_paths['dri']),
                       __GLX_VENDOR_LIBRARY_NAME='mesa', __EGL_VENDOR_LIBRARY_FILENAMES=str(egl_json),
                       GBM_BACKENDS_PATH=str(mesa_paths['gbm']))
        env.update(VK_DRIVER_FILES=str(icd), VK_ICD_FILENAMES=str(icd), MESA_VK_WSI_PRESENT_MODE='mailbox',
                   MESA_LOADER_DRIVER_OVERRIDE='zink', GALLIUM_DRIVER='zink', LIBGL_KOPPER_DRI2='1')
        if args.variant != 'default':
            env.update(S44_WRAPPER_SUBMIT_STACK='1' if args.variant in ('wrapper', 'both') else '0',
                       S44_ZINK_COMPACT_SUBMITS='1' if args.variant in ('zink', 'both') else '0')
        if args.profile:
            env.update(S44_WSI_PROFILE='1', S44_WRAPPER_PROFILE='1', S44_ZINK_SUBMIT_STATS='1')
        if egl or probe:
            argv = ([str(ROOT / '.source/gpu-stage1/s44-sync-overhead/egl-probe/wayland-egl-bench'), str(args.frames)] if egl else
                    [str(PREFIX / 'bin/vkcube'), '--wsi', 'wayland', '--present_mode', '1', '--c', str(args.frames)])
            env.update(XDG_SESSION_TYPE='wayland', WAYLAND_DISPLAY=env.get('WAYLAND_DISPLAY') or 'wayland-0')
            if egl:
                env['EGL_PLATFORM'] = 'wayland'
            summary['requested_frames'] = args.frames
        elif glx:
            scene = 'terrain' if args.case.endswith('heavy') else 'build:use-vbo=true'
            argv = [str(PREFIX / 'bin/glmark2'), '-b', scene + ':duration=' + str(args.duration),
                    '--size', '800x600', '--frame-end', 'swap', '--results', 'fps:cpu']
        else:
            winsys = 'wayland' if 'wayland' in args.case else 'xcb'
            scene = 'effect2d:kernel=blur:background-resolution=1920x1080' if args.case.endswith('heavy') else 'vertex'
            argv = [str(PREFIX / 'bin/vkmark'), '--winsys', winsys, '-p', 'mailbox', '-s', '800x600',
                    '-b', scene + ':duration=' + str(args.duration)]
            if winsys == 'wayland':
                env.update(XDG_SESSION_TYPE='wayland', WAYLAND_DISPLAY=env.get('WAYLAND_DISPLAY') or 'wayland-0')
        summary.update(argv=argv, executable=identity(Path(argv[0])), expected_libraries=expected,
                       mesa_prefix=str(mesa), environment={k: v for k, v in env.items() if k.startswith('S44_') or k in {
                       'DISPLAY', 'XDG_SESSION_TYPE', 'XDG_RUNTIME_DIR', 'WAYLAND_DISPLAY', 'VK_DRIVER_FILES',
                       'VK_ICD_FILENAMES', 'MESA_VK_WSI_PRESENT_MODE', 'MESA_LOADER_DRIVER_OVERRIDE', 'GALLIUM_DRIVER',
                       'LIBGL_KOPPER_DRI2', 'LD_LIBRARY_PATH', 'LIBGL_DRIVERS_PATH', '__GLX_VENDOR_LIBRARY_NAME',
                       '__EGL_VENDOR_LIBRARY_FILENAMES', 'GBM_BACKENDS_PATH', 'EGL_PLATFORM'}})
        summary['environment'].update({k: env.get(k) for k in ('vblank_mode', 'GALLIUM_THREAD', 'ZINK_DEBUG', 'mesa_glthread',
                                                             'S44_WRAPPER_SUBMIT_STACK', 'S44_ZINK_COMPACT_SUBMITS')})
        if args.case.startswith('vk-wayland-') and not (PREFIX / 'lib/vkmark/wayland.so').is_file():
            raise NotImplementedError('vkmark Wayland backend unavailable: ' + str(PREFIX / 'lib/vkmark/wayland.so'))
        start, mapped, sample_at = time.monotonic(), False, 0
        with (logdir / 'client.log').open('w') as log:
            proc = subprocess.Popen(argv, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            summary.update(pid=proc.pid, scheduling_start=snapshot(proc.pid))
            while True:
                pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
                if pid:
                    proc.returncode = os.waitstatus_to_exitcode(status)
                    break
                now = time.monotonic()
                require(now - start < args.duration + 20, 'owned benchmark exceeded its timeout')
                if now >= sample_at:
                    summary['scheduling_last_alive'] = snapshot(proc.pid)
                    try:
                        verified, maps = map_check(proc.pid, expected)
                        if verified:
                            (logdir / 'client.maps').write_text(maps)
                            mapped = True
                    except FileNotFoundError:
                        pass
                    sample_at = now + (1 if mapped else .1)
                time.sleep(.05)
        summary.update(process_wall_seconds=time.monotonic() - start, exit_code=proc.returncode,
                       loaded_library_identity_verified=mapped)
        require(proc.returncode == 0 and mapped, 'benchmark failed or selected libraries were not observed')
        for entry in expected.values():
            identity(Path(entry['path']), entry['sha256'])
        summary['parsed'] = parse_log((logdir / 'client.log').read_text(errors='replace'))
        require(probe or summary['parsed']['fps'], 'benchmark did not report a scene FPS result')
        if egl:
            require(any(r.get('frames') == args.frames and r.get('interval') == 0 and r.get('seconds', 0) > 0
                        for r in summary['parsed']['fps']), 'EGL probe did not complete the requested frames at interval 0')
        if args.profile:
            summary['vulkan_wsi_swapchains'] = check_profile(summary['parsed'], args.variant,
                glx or egl, egl, probe)
        for row in summary['parsed']['wsi']:
            if row.get('event') == 'stat':
                require(row['clock_errors'] == 0 and row['calls'] == row['samples'], 'invalid WSI timing samples')
        summary['status'] = 'PASS'
    except BaseException as exc:
        failure = exc
        summary.update(status='UNAVAILABLE' if isinstance(exc, NotImplementedError) else 'FAIL', error=f'{type(exc).__name__}: {exc}')
    finally:
        try:
            usage = stop_owned(proc) or usage
        except Exception as exc:
            summary.update(status='FAIL', cleanup_error=str(exc))
            failure = failure or exc
        if usage is not None:
            summary['rusage_process_lifetime'] = {k: getattr(usage, k) for k in
                ('ru_utime', 'ru_stime', 'ru_maxrss', 'ru_minflt', 'ru_majflt', 'ru_nvcsw', 'ru_nivcsw')}
        if proc is not None:
            summary['exit_code'] = proc.returncode
        try:
            summary['session_after'] = base.session_stamp()
            require(before == summary['session_after'], 'live desktop identity changed')
        except Exception as exc:
            summary.update(status='FAIL', session_error=str(exc))
            failure = failure or exc
        (logdir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        print('S44_BENCH=' + summary['status'], flush=True)
        print('S44_BENCH_SUMMARY=' + str(logdir / 'summary.json'), flush=True)
        if lock is not None:
            lock.close()
    return 2 if failure else 0

if __name__ == '__main__':
    raise SystemExit(main())
