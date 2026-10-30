#!/data/data/com.termux/files/usr/bin/python3
"""S16 safe GLX Zink DRI3 stress probe: private Xwayland only.

Never launches a Zink test on the active user desktop. A private X server is
started in the already running Alroot Wayland compositor at :201..:240, then
one Zink client is run with a finite timeout. The script checks the private
Xserver's responsiveness *during* client execution, to distinguish a
client-side failure from an Xserver-wide deadlock, and always terminates ONLY
its own processes/sockets. No changes to Mesa/Termux-X11/APK/global files.
"""
import os
import pathlib
import re
import secrets
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HOME = pathlib.Path('/data/data/com.termux/files/home')
PREFIX = pathlib.Path('/data/data/com.termux/files/usr')
ROOT = HOME / 'alroot_wayland'
STATE = HOME / '.alroot-wayland'
OUT = ROOT / '.source/gpu-stage27-pr25/s16-xwayland-gpu/zink-matrix'
SERVER = pathlib.Path(os.environ.get('S16_ZINK_SERVER_BINARY',
    str(ROOT / '.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland'))).resolve()
if ROOT.resolve() not in SERVER.parents:
    raise SystemExit('S16_ZINK_SERVER_BINARY_OUTSIDE_PROJECT_FORBIDDEN')
CASES = {
    'termux-turnip-glxinfo': 201,
    'termux-turnip-glmark2': 202,
    'termux-wrapper-glxinfo': 203,
    'termux-wrapper-private-glxinfo': 204,
    'debian-turnip-glxinfo': 205,
    'alroot-turnip-glxinfo': 206,
    'termux-wrapper-no-kopper-glxinfo': 207,
    'debian-kgsl-control-glxinfo': 208,
    'debian-turnip-no-dri3-glxinfo': 209,
    'debian-turnip-vkcube': 210,
    'termux-turnip-vkcube': 211,
    'debian-turnip-glmark2': 212,
    'alroot-turnip-glmark2': 213,
    'termux-wrapper-vkcube': 214,
    'termux-wrapper-private-vkcube': 215,
    'termux-wrapper-vkxstage': 216,
    'termux-turnip-vkxstage': 217,
    'termux-wrapper-private-vkxstage': 218,
    'termux-wrapper-glmark2': 219,
}


def probe_x11(env, display, seconds=1.5):
    try:
        r = subprocess.run(['xdpyinfo', '-display', display, '-queryExtensions'],
                           env=env, capture_output=True, text=True, timeout=seconds)
        return r.returncode == 0
    except subprocess.TimeoutExpired:
        return False


def terminate(proc, group=False):
    if proc is None or proc.poll() is not None:
        return
    try:
        if group:
            os.killpg(proc.pid, signal.SIGTERM)
        else:
            proc.terminate()
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        if group:
            os.killpg(proc.pid, signal.SIGKILL)
        else:
            proc.kill()
        proc.wait(timeout=2)


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in CASES:
        print('usage: test-s16-zink-isolated.py ' + '|'.join(CASES))
        return 2
    name = sys.argv[1]
    id_ = CASES[name]
    display = f':{id_}'
    xdir = PREFIX / 'tmp/.X11-unix'
    x_sock = xdir / f'X{id_}'
    lock = PREFIX / f'tmp/.X{id_}-lock'
    temporary_lock = PREFIX / f'tmp/.tX{id_}-lock'
    if not SERVER.is_file() or not (STATE / 'runtime/wayland-0').is_socket():
        print('S16_ZINK_PRIVATE_SERVER_OR_HOST_NOT_RUNNING')
        return 2
    if any(p.exists() for p in (x_sock, lock, temporary_lock)):
        print(f'S16_ZINK_PRIVATE_DISPLAY_ALREADY_USED={display}')
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    token = pathlib.Path(tempfile.mkdtemp(prefix=f'zink-{id_}-', dir=PREFIX / 'tmp'))
    auth = token / 'Xauthority'
    auth.touch(mode=0o600)
    subprocess.run(['xauth', '-f', str(auth), 'add', display, 'MIT-MAGIC-COOKIE-1',
                    secrets.token_hex(16)], check=True, capture_output=True)
    server_env = os.environ.copy()
    server_env.update(XDG_RUNTIME_DIR=str(STATE / 'runtime'),
                      WAYLAND_DISPLAY='wayland-0', TMPDIR=str(PREFIX / 'tmp'),
                      XAUTHORITY=str(auth), XWAYLAND_FORCE_KGSL_SURFACELESS='1',
                      FD_KGSL_ENABLE_DMABUF='1', MESA_LOADER_DRIVER_OVERRIDE='kgsl',
                      GALLIUM_DRIVER='freedreno', EGL_PLATFORM='surfaceless',
                      LD_LIBRARY_PATH=str(ROOT / 'packages/prefix/lib'))
    server_env.pop('DISPLAY', None)
    shim_path = os.environ.get('S16_ZINK_XSHMFENCE_SHIM')
    if shim_path:
        shim = pathlib.Path(shim_path).resolve()
        if ROOT.resolve() not in shim.parents or not shim.is_file():
            raise SystemExit('S16_ZINK_XSHMFENCE_SHIM_OUTSIDE_PROJECT_OR_MISSING')
        # Process-scoped: preload compatibility only in this disposable
        # private Xwayland, never into the user's Termux/PRoot/GLIBC apps.
        server_env['LD_PRELOAD'] = str(shim) + ' ' + server_env.get('LD_PRELOAD', '')
        server_env['S16_XSHMFENCE_TRACE'] = '1'
        print('S16_ZINK_PRIVATE_XSHMFENCE_DUALABI=' + str(shim), flush=True)
    if os.environ.get('S16_ZINK_SERVER_TRACE') == '1':
        server_env['S16_ZINK_DRI3_TRACE'] = '1'
    xclient = os.environ.copy()
    xclient.update(DISPLAY=display, XAUTHORITY=str(auth))
    for k in ('WAYLAND_DISPLAY', 'VK_DRIVER_FILES', 'VK_ICD_FILENAMES',
              'GALLIUM_DRIVER', 'MESA_LOADER_DRIVER_OVERRIDE', 'MESA_LOG_FILE',
              'MESA_DEBUG'):
        xclient.pop(k, None)
    client = xclient.copy()
    client.update(LIBGL_KOPPER_DRI2='1', LIBGL_DEBUG='verbose')
    is_guest = name.startswith('debian-')
    is_alroot = name.startswith('alroot-')
    if is_guest:
        extra = ([f'DISPLAY={display}', f'XAUTHORITY={auth}',
                  'MESA_LOADER_DRIVER_OVERRIDE=zink', 'GALLIUM_DRIVER=zink',
                  'LIBGL_KOPPER_DRI2=1',
                  'VK_DRIVER_FILES=/usr/share/vulkan/icd.d/freedreno_icd.aarch64.json',
                  'VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/freedreno_icd.aarch64.json'])
        if 'kgsl-control' in name:
            extra = [f'DISPLAY={display}', f'XAUTHORITY={auth}',
                     'MESA_LOADER_DRIVER_OVERRIDE=kgsl', 'GALLIUM_DRIVER=freedreno']
        if 'no-dri3' in name:
            extra.append('LIBGL_DRI3_DISABLE=1')
        guest_program = (['/usr/bin/vkcube','--wsi','xcb','--c','10']
                         if name.endswith('vkcube') else
                         ['/usr/bin/glmark2','-b','build:use-vbo=false']
                         if name.endswith('glmark2') else
                         ['/usr/bin/glxinfo','-B'])
        command = ['bash', str(ROOT / 'termux/aw-debian'), '/usr/bin/env',
                   *extra, *guest_program]
    elif is_alroot:
        guest_icd = '/usr/share/vulkan/icd.d/freedreno_icd.aarch64.json'
        client.update(MESA_LOADER_DRIVER_OVERRIDE='zink', GALLIUM_DRIVER='zink',
                      ALROOT_MESA_DRIVER='zink', ALROOT_VK_ICD=guest_icd,
                      ALROOT_KEEP_GPU_ENV='1', ALROOT_SVC_PATCH='1',
                      VK_ICD_FILENAMES=guest_icd, VK_DRIVER_FILES=guest_icd)
        alroot_prog = (['/usr/bin/glmark2','-b','build:use-vbo=false']
                       if name.endswith('glmark2') else ['/usr/bin/glxinfo','-B'])
        command = ['bash',str(HOME / '다운로드/alroot-runtime/scripts/alroot_run.sh'),
                   '--raw', *alroot_prog]
    else:
        client.update(MESA_LOADER_DRIVER_OVERRIDE='zink', GALLIUM_DRIVER='zink')
        if 'no-kopper' in name:
            client['LIBGL_KOPPER_DRI2'] = '0'
        icd = (ROOT / '.source/gpu-stage1/wrapper-private-runtime/wrapper_icd.private.json'
               if 'wrapper-private' in name
               else PREFIX / 'share/vulkan/icd.d' /
               ('wrapper_icd.aarch64.json' if 'wrapper' in name else 'freedreno_icd.aarch64.json'))
        if not icd.is_file():
            print('S16_ZINK_SELECTED_ICD_NOT_FOUND', icd)
            return 2
        client['VK_DRIVER_FILES'] = str(icd)
        command = ([str(PREFIX / 'bin/glmark2'), '-b', 'build:use-vbo=false']
                   if name.endswith('glmark2') else
                   [str(OUT / 'vulkan-xcb-stage')]
                   if name.endswith('vkxstage') else
                   [str(PREFIX / 'bin/vkcube'), '--wsi', 'xcb', '--c', '10']
                   if name.endswith('vkcube') else
                   [str(PREFIX / 'bin/glxinfo'), '-B'])
    server_log = OUT / f'{name}-server.log'
    client_log = OUT / f'{name}-client.log'
    sproc = cproc = None
    try:
        with server_log.open('w') as log:
            server_args = [str(SERVER), display, '-geometry', '800x600',
                           '-dpi', '96', '-auth', str(auth), '-nolisten',
                           'tcp', '-glamor', 'es', '-verbose', '2']
            if is_alroot:
                # The standalone Alroot glibc Xau path currently does not
                # forward private FamilyLocal cookies. For THIS disposable
                # UNIX-only test server only, disable X11 auth while probing
                # actual GLIBC/driver compatibility; never production :92.
                server_args.append('-ac')
                print('S16_ALROOT_PRIVATE_AUTH_DISABLED_TEST_ONLY=YES', flush=True)
            sproc = subprocess.Popen(server_args,
                                     env=server_env, stdout=log,
                                     stderr=subprocess.STDOUT)
        good = False
        for i in range(50):
            if sproc.poll() is not None:
                break
            if probe_x11(xclient, display, .8):
                good = True
                break
            time.sleep(.08)
        print('S16_ZINK_CASE=' + name, flush=True)
        print('S16_ZINK_PRIVATE_DISPLAY=' + display, flush=True)
        print('S16_ZINK_PRE_CLIENT_XSERVER=' + ('PASS' if good else 'FAIL'), flush=True)
        if not good:
            print('\n'.join(server_log.read_text(errors='replace').splitlines()[-8:]), flush=True)
            return 3
        with client_log.open('w') as log:
            cproc = subprocess.Popen(command, env=client, stdout=log,
                                     stderr=subprocess.STDOUT, start_new_session=True)
        time.sleep(1.5)
        during = probe_x11(xclient, display, 2)
        print('S16_ZINK_DURING_CLIENT_XSERVER=' + ('RESPONSIVE' if during else 'UNRESPONSIVE'), flush=True)
        try:
            code = cproc.wait(timeout=19 if name.endswith('glmark2') else 6)
            completed = True
        except subprocess.TimeoutExpired:
            completed = False
            code = 124
        print('S16_ZINK_CLIENT_EXIT=' + str(code), flush=True)
        terminate(cproc, group=True)
        after = probe_x11(xclient, display, 2)
        print('S16_ZINK_AFTER_CLIENT_XSERVER=' + ('RESPONSIVE' if after else 'UNRESPONSIVE'), flush=True)
        log = client_log.read_text(errors='replace')
        print('S16_ZINK_CLIENT_LOG_BYTES=' + str(len(log)), flush=True)
        for line in log.splitlines():
            if any(term in line for term in ('OpenGL renderer string:', 'zink Vulkan', 'ERROR', 'Error',
                                             'failed', 'Failed', 'Score:', 'GL_RENDERER')):
                print('S16_ZINK_CLIENT_LOG=' + line[:175], flush=True)
        if not log and not completed:
            print('S16_ZINK_STALL_BEFORE_STDOUT=YES', flush=True)
        if not after:
            print('S16_ZINK_VIOLATES_AWSTART_DEFAULT_SAFETY=YES', flush=True)
        if name.endswith('vkcube') or name.endswith('vkxstage'):
            # Vulkan clients don't display an OpenGL Zink renderer. Require
            # their own Vulkan success markers in addition to exit 0.
            expected = ('S16_VKXCB_STAGE=DONE' in log if name.endswith('vkxstage')
                        else 'Selected GPU 0:' in log)
        elif name.endswith('glmark2'):
            expected = 'zink Vulkan' in log and 'glmark2 Score:' in log
        elif 'kgsl-control' in name:
            expected = 'OpenGL renderer string: Adreno (TM) 840' in log
        else:
            expected = 'OpenGL renderer string: zink Vulkan' in log
        outcome = completed and code == 0 and after and expected
        print('S16_ZINK_CASE_RESULT=' + ('PASS' if outcome else 'FAIL'), flush=True)
        return 0 if outcome else 5
    finally:
        terminate(cproc, group=True)
        terminate(sproc)
        # SIGKILL cannot invoke Xorg's UnlockServer() and may leave its
        # socket/lock. Only unlink files belonging to this exact private
        # test PID, never any global/user display or an unrelated process.
        if sproc is not None and sproc.poll() is not None and lock.is_file():
            try:
                recorded = int(lock.read_text().strip())
            except (ValueError, OSError):
                recorded = -1
            if recorded == sproc.pid and not pathlib.Path(f'/proc/{recorded}').exists():
                lock.unlink()
                if x_sock.is_socket():
                    x_sock.unlink()
                print('S16_ZINK_SIGKILL_PRIVATE_XSERVER_STALE_LOCK_CLEANED=YES', flush=True)
        shutil.rmtree(token, ignore_errors=True)
        print('S16_ZINK_PRIVATE_SERVER_CLEANED=' +
              ('PASS' if not any(p.exists() for p in (x_sock, lock, temporary_lock)) else 'FAIL'), flush=True)


if __name__ == '__main__':
    sys.exit(main())
