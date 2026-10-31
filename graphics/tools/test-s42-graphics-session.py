#!/data/data/com.termux/files/usr/bin/python3
"""Validate staged or installed S42 drivers in an owned S39/XFWM session.

Reuse S40's bounded Present, interactive-resize and fixed-animation probes.
--server-only is the early Mesa27 KGSL/EGL compatibility gate. This program
never installs files, changes the live desktop, or restarts its Wayland host.
"""
import argparse
from contextlib import ExitStack
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('s40_helpers', Path(__file__).with_name('test-s40-live-resize.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
S42 = base.ROOT / '.source/gpu-stage1/s42-default-graphics'
OUT = S42 / 'session-tests'
MESA_VERSION = '27.0.0~devel+alroot.s42.1'
WRAPPER_VERSION = '26.3.0+alroot.s42.1'
MESA_COMMIT = '6f90e92dfdacc4759d8e9ccb328805d0f0b4439b'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def read_json(path):
    return json.loads(Path(path).read_text())


def selected_runtime(args):
    staged = args.mode == 'staged'
    package = read_json(args.mesa_manifest) if staged else None
    prefix = Path(package['staged_prefix']).resolve(strict=True) if staged else base.PREFIX
    if staged:
        require(prefix != base.PREFIX and prefix.is_relative_to(S42), 'staged Mesa must be inside S42')
    receipt = read_json(prefix / 'share/doc/mesa/alroot-s42-build.json')
    require(receipt.get('package_version') == MESA_VERSION and receipt.get('mesa_version') == '27.0.0-devel'
            and receipt.get('source_commit') == MESA_COMMIT, 'Mesa27 package provenance mismatch')
    expected = {}
    for name, relative in {'egl': 'lib/libEGL_mesa.so', 'glx': 'lib/libGLX_mesa.so',
            'gallium': 'lib/libgallium-27.0.0-devel.so', 'gbm': 'lib/libgbm.so',
            'dril': 'lib/dri/libdril_dri.so', 'zink': 'lib/dri/zink_dri.so',
            'kgsl': 'lib/dri/kgsl_dri.so'}.items():
        path = (prefix / relative).resolve(strict=True)
        require(path.is_relative_to(prefix), 'Mesa runtime points outside selected prefix: ' + relative)
        digest = receipt['runtime_files'].get(str(path.relative_to(prefix)))
        require(isinstance(digest, str) and len(digest) == 64, 'missing Mesa runtime hash: ' + relative)
        expected[name] = base.identity(path, digest)
    egl = Path(package['preinstall_egl_vendor_json']) if staged else prefix / 'share/glvnd/egl_vendor.d/50_mesa.json'
    if staged:
        require(read_json(egl).get('ICD', {}).get('library_path') == str(prefix / 'lib/libEGL_mesa.so'),
                'staged EGL manifest must select staged Mesa')
    result = {'prefix': str(prefix), 'egl_manifest': base.identity(egl), 'libraries': expected,
              'mesa_version': receipt['mesa_version'], 'package_version': receipt['package_version']}
    if not args.server_only:
        wrapper = read_json(args.wrapper_manifest) if staged else {
            'provenance': read_json(base.PREFIX / 'share/doc/vulkan-wrapper-android/alroot-build.json'),
            'runtime_icd': str(base.PREFIX / 'share/vulkan/icd.d/wrapper_icd.aarch64.json')}
        provenance, icd = wrapper['provenance'], Path(wrapper['runtime_icd'])
        require(provenance.get('package_version') == WRAPPER_VERSION, 'Wrapper package version mismatch')
        raw = Path(read_json(icd)['ICD']['library_path'])
        path = (raw if raw.is_absolute() else icd.parent / raw).resolve(strict=True)
        require((staged and path.is_relative_to(S42)) or
                (not staged and path == base.PREFIX / 'lib/libvulkan_wrapper.so'), 'unexpected Wrapper location')
        expected['wrapper'] = base.identity(path, provenance['library_sha256'])
        result['icd'] = base.identity(icd, None if staged else provenance['icd_sha256'])
    if not staged:
        for package_name, version in [('mesa', MESA_VERSION)] + ([] if args.server_only else [('vulkan-wrapper-android', WRAPPER_VERSION)]):
            actual = base.command(['dpkg-query', '-W', '-f=${Status}\t${Version}', package_name], check=True).stdout
            require(actual == 'install ok installed\t' + version, 'installed package mismatch: ' + package_name)
    return result


def clean_environment():
    env = base.clean_environment()
    for key in list(env):
        if key.startswith(('MESA_', 'GALLIUM_', 'LIBGL_', '__EGL_', '__GLX_', 'EGL_',
                           'GBM_', 'ZINK_', 'VK_', 'TU_', 'FD_', 'WRAPPER_', 'ALROOT_')):
            env.pop(key)
    return env


def mesa_environment(args, selected):
    if args.mode == 'installed':
        return {}
    prefix = Path(selected['prefix'])
    return {'LD_LIBRARY_PATH': str(prefix / 'lib'), 'LIBGL_DRIVERS_PATH': str(prefix / 'lib/dri'),
            '__EGL_VENDOR_LIBRARY_FILENAMES': selected['egl_manifest']['path'],
            '__GLX_VENDOR_LIBRARY_NAME': 'mesa', 'GBM_BACKENDS_PATH': str(prefix / 'lib/gbm')}


def mapped_libraries(pid, name, selected, required, logdir):
    text = Path(f'/proc/{pid}/maps').read_text()
    (logdir / (name + '.maps')).write_text(text)
    paths = set()
    for line in text.splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) == 6 and fields[5].startswith('/'):
            if re.search(r'/lib(?:EGL_mesa|GLX_mesa|gallium|gbm|vulkan_wrapper)|/(?:libdril|zink|kgsl|msm|swrast)_dri\.so', fields[5]):
                require(not fields[5].endswith(' (deleted)'), 'mapped graphics library was deleted: ' + fields[5])
                paths.add(str(Path(fields[5]).resolve(strict=True)))
    expected = selected['libraries']
    allowed = {entry['path']: entry['sha256'] for entry in expected.values()}
    require(not paths - allowed.keys(), name + ' loaded unexpected Mesa/Wrapper: ' + repr(sorted(paths - allowed.keys())))
    for role in required:
        require(expected[role]['path'] in paths, name + ' did not map required ' + role)
    identities = [base.identity(Path(path), allowed[path]) for path in sorted(paths)]
    return {'pid': pid, 'mapped_graphics_library_count': len(identities), 'libraries': identities,
            'required_roles': required, 'status': 'VERIFIED' if identities else 'NO_MESA_OR_WRAPPER_MAPPED'}


def xfwm_pid(group):
    matches = []
    for directory in Path('/proc').glob('[0-9]*'):
        try:
            fields = (directory / 'stat').read_text().rsplit(')', 1)[1].split()
            command = (directory / 'cmdline').read_bytes().split(b'\0')[0].decode()
            if int(fields[2]) == group and Path(command).name == 'xfwm4':
                matches.append(int(directory.name))
        except (OSError, ValueError, IndexError):
            continue
    require(len(matches) == 1, 'could not identify exactly one owned xfwm4 process')
    return matches[0]


def run(args):
    stamp = time.strftime('%Y%m%d-%H%M%S')
    logdir = OUT / 'logs' / f'{args.mode}-{args.demo}-{stamp}-{os.getpid()}'
    logdir.mkdir(parents=True)
    base.OUT = OUT
    (OUT / 'runtime').mkdir(exist_ok=True)
    before = base.user_session()
    summary = {'status': 'RUNNING', 'mode': args.mode, 'demo': args.demo, 'server_only': args.server_only,
               'drag_ms': args.drag_ms, 'step_ms': 8, 'user_session_before': before, 'logdir': str(logdir)}
    processes, temporary, failure = [], None, None
    try:
        selected = selected_runtime(args)
        summary['runtime'] = selected
        summary['server'] = base.identity(base.S39, base.S39_SHA, executable=True)
        summary['interposer'] = base.identity(base.INTERPOSER)
        require((base.STATE / 'runtime/wayland-0').is_socket(), 'existing Wayland host socket is unavailable')
        if not args.server_only:
            default = (base.S39_OUT / 'runtime/vkcube.coalesced' if args.demo == 'vkcube'
                       else base.GLX_RUNTIME / 'glxgears.coalesced')
            summary['client'] = base.identity(args.client_path or default,
                args.client_sha256 or base.CLIENT_SHA[args.demo]['coalesced'], executable=True)
            summary['helpers'] = base.build_helpers(argparse.Namespace(capture=False, thin=True, demo=args.demo), logdir)
        number = next((n for n in range(201, 241) if not any(p.exists() for p in (
            base.PREFIX / f'tmp/.X11-unix/X{n}', base.PREFIX / f'tmp/.X{n}-lock',
            base.PREFIX / f'tmp/.tX{n}-lock'))), None)
        require(number is not None, 'no free isolated display :201..:240')
        display = ':' + str(number)
        summary['display'] = display
        temporary = Path(tempfile.mkdtemp(prefix='alroot-s42-test-', dir=base.PREFIX / 'tmp'))
        auth = temporary / 'Xauthority'
        auth.touch(mode=0o600)
        base.command(['xauth', '-f', str(auth), 'add', display, 'MIT-MAGIC-COOKIE-1', secrets.token_hex(16)], check=True)
        xenv = clean_environment()
        xenv.update(DISPLAY=display, XAUTHORITY=str(auth), TMPDIR=str(base.PREFIX / 'tmp'),
                    XDG_CONFIG_HOME=str(temporary / 'config'), XDG_CACHE_HOME=str(temporary / 'cache'))
        server_env = {**xenv, **mesa_environment(args, selected), **base.SERVER_FLAGS}
        server_env.pop('DISPLAY', None)
        server_env.update(LD_PRELOAD=str(base.INTERPOSER), S16_XSHMFENCE_TRACE='0', EGL_PLATFORM='surfaceless',
            XDG_RUNTIME_DIR=str(base.STATE / 'runtime'), WAYLAND_DISPLAY='wayland-0',
            S39_KGSL_RESIZE_ORIGIN='1', S39_KGSL_KEEP_UNRELATED_ROOT_POOL='1')
        summary['server_environment'] = {k: v for k, v in server_env.items() if k in base.SERVER_FLAGS or
            k in ('LD_PRELOAD', 'EGL_PLATFORM', 'LD_LIBRARY_PATH', 'LIBGL_DRIVERS_PATH', '__EGL_VENDOR_LIBRARY_FILENAMES') or k.startswith('S39_')}
        with ExitStack() as stack:
            def launch(argv, env, filename):
                log = stack.enter_context((logdir / filename).open('w'))
                proc = subprocess.Popen(argv, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                processes.append(proc)
                return proc
            server = launch([str(base.S39), display, '-geometry', '1920x1080', '-dpi', '96',
                '-auth', str(auth), '-glamor', 'es', '-nolisten', 'tcp'], server_env, 'server.log')
            base.ensure_ready(server, ['xdpyinfo'], xenv)
            summary['server_maps'] = mapped_libraries(server.pid, 'server', selected, ['egl', 'gallium'], logdir)
            if args.server_only:
                time.sleep(2)
                require(server.poll() is None, 'owned Mesa27 server exited after readiness')
                summary['server_compatibility'] = 'PASS'
            else:
                client_env = dict(xenv, VK_DRIVER_FILES=selected['icd']['path'], VK_ICD_FILENAMES=selected['icd']['path'],
                                  S24_WRAPPER_XCB_AHB_PAD_HEIGHT='1', S26_WRAPPER_XCB_RETRY_TRANSIENT='1')
                zink_env = {**client_env, **mesa_environment(args, selected), 'MESA_LOADER_DRIVER_OVERRIDE': 'zink',
                            'GALLIUM_DRIVER': 'zink', 'LIBGL_KOPPER_DRI2': '1'}
                wm_env = dict(zink_env, GDK_BACKEND='x11', XDG_SESSION_TYPE='x11')
                wm = launch(['dbus-run-session', '--', 'xfwm4', '--compositor=on', '--display', display], wm_env, 'xfwm.log')
                base.ensure_ready(wm, ['xprop', '-root', '_NET_SUPPORTING_WM_CHECK'], xenv, 'window id #')
                info = base.command(['glxinfo', '-B'], zink_env, timeout=15)
                (logdir / 'glxinfo-B.log').write_text(info.stdout + info.stderr)
                renderer = re.search(r'^OpenGL renderer string:\s*(.+)$', info.stdout, re.M)
                require(info.returncode == 0 and 'Mesa 27.0.0-devel' in info.stdout and renderer and
                        'zink' in renderer[1].lower() and 'adreno' in renderer[1].lower() and
                        'direct rendering: Yes' in info.stdout, 'glxinfo did not confirm direct Mesa27/Zink/Adreno')
                summary['glxinfo'] = {'renderer': renderer[1], 'direct_mesa27_zink_adreno': 'PASS'}
                env = zink_env if args.demo == 'zink' else client_env
                summary['client_environment'] = {k: v for k, v in env.items() if k not in xenv}
                summary.update(client_ld_preload=env.get('LD_PRELOAD'), wm_ld_preload=wm_env.get('LD_PRELOAD'))
                existing = base.own_windows(xenv)
                app = launch([summary['client']['path']] + (['--wsi', 'xcb'] if args.demo == 'vkcube' else []), env, 'client.log')
                deadline, xid = time.monotonic() + 6, None
                while app.poll() is None and time.monotonic() < deadline:
                    xid = base.find_window(xenv, existing, app.pid)
                    if xid: break
                    time.sleep(.1)
                require(xid, 'owned client did not map a unique managed window')
                summary.update(xid=xid, app_pid=app.pid)
                required = ['glx', 'gallium', 'wrapper'] if args.demo == 'zink' else ['wrapper']
                summary['client_maps'] = mapped_libraries(app.pid, 'client', selected, required, logdir)
                summary['wm_maps'] = mapped_libraries(xfwm_pid(wm.pid), 'xfwm', selected, [], logdir)
                observer = launch([str(OUT / 'runtime/s39-present-observer'), xid, str(args.drag_ms / 1000 + 4)], xenv, 'present.log')
                deadline = time.monotonic() + 5
                while 'S39_PRESENT_READY ' not in (logdir / 'present.log').read_text():
                    require(observer.poll() is None and time.monotonic() < deadline, 'Present observer did not become ready')
                    time.sleep(.02)
                time.sleep(1.5)
                stress = launch([str(OUT / 'runtime/s40-interactive-resize'), xid, str(args.drag_ms), '8', 'mixed'], xenv, 'resize.log')
                stress.wait(timeout=args.drag_ms / 1000 + 5)
                require(stress.returncode == 0, 'interactive resize helper failed')
                timing = re.search(r'S39_RESIZE_TIMING start_ms=(\d+) end_ms=(\d+)', (logdir / 'resize.log').read_text())
                require(timing, 'missing actual resize timestamps')
                observer.wait(timeout=7)
                require(observer.returncode == 0, 'Present observer failed')
                metrics = base.present_metrics((logdir / 'present.log').read_text(), *map(int, timing.groups()))
                summary['present'] = metrics
                require(metrics['total_configure'] >= 10 and metrics['core_zero_copy_bins'] == 0 and
                        metrics['max_complete_receive_gap_ms'] < 1000, 'continuous Present COPY gate failed')
                summary['thin'] = base.thin_check(xid, xenv, app, server, args.demo, logdir)
                base.command(['xdotool', 'key', '--window', xid, 'Escape'], xenv, check=True)
                app.wait(timeout=5)
                summary['close_client_exit'] = app.returncode
                require(app.returncode == 0, 'owned client did not exit normally after Escape')
                require(wm.poll() is None, 'owned WM exited before cleanup')
            require(server.poll() is None, 'owned server exited before cleanup')
            faults = (logdir / 'server.log').read_text(errors='replace')
            require(not re.search(r'invalid object|failed to dispatch Wayland events|Fatal server error|Segmentation fault', faults),
                    'owned Xwayland fault in log')
            summary['status'] = 'PASS'
    except BaseException as exc:
        summary.update(status='FAIL', error=f'{type(exc).__name__}: {exc}')
        failure = exc
    finally:
        for proc in reversed(processes):
            try:
                base.stop_owned(proc)
            except Exception as exc:
                summary.setdefault('cleanup_errors', []).append(str(exc))
                summary['status'], failure = 'FAIL', failure or exc
        if temporary is not None:
            shutil.rmtree(temporary, ignore_errors=True)
        try:
            after = base.user_session()
            summary.update(user_session_after=after, user_session_unchanged=(after == before))
            require(after == before, 'live user session changed')
        except Exception as exc:
            summary.update(status='FAIL', user_session_error=str(exc))
            failure = failure or exc
        (logdir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        print('S42_SESSION=' + summary['status'], flush=True)
        print('S42_SESSION_SUMMARY=' + str(logdir / 'summary.json'), flush=True)
    if failure:
        raise failure


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('staged', 'installed'), default='staged')
    parser.add_argument('--mesa-manifest', type=Path, default=S42 / 'mesa/package-manifest.json')
    parser.add_argument('--wrapper-manifest', type=Path, default=S42 / 'wrapper/build-manifest.json')
    parser.add_argument('--demo', choices=('vkcube', 'zink'), default='vkcube')
    parser.add_argument('--server-only', action='store_true')
    parser.add_argument('--drag-ms', type=int, default=6000)
    parser.add_argument('--client-path', type=Path)
    parser.add_argument('--client-sha256')
    args = parser.parse_args()
    if bool(args.client_path) != bool(args.client_sha256):
        parser.error('--client-path and --client-sha256 must be supplied together')
    if args.client_sha256 and not re.fullmatch(r'[0-9a-f]{64}', args.client_sha256):
        parser.error('--client-sha256 must be 64 lowercase hexadecimal characters')
    if not 6000 <= args.drag_ms <= 12000:
        parser.error('--drag-ms must be 6000..12000')
    return args


if __name__ == '__main__':
    try:
        run(parse_args())
    except Exception as exc:
        print('S42_SESSION_ERROR=' + str(exc)[:500], file=sys.stderr)
        raise SystemExit(2)
