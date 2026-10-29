#!/data/data/com.termux/files/usr/bin/python3
"""S1.6: rootful private Xwayland lock regression on S26U (no -listenfd).

Uses a disposable :201..:240 X display on the already running Alroot
Wayland host. Never replaces/stops an active XFCE, :0, :90..:180 X server,
helper APK, system Mesa, or Termux-X11. Uses local MIT-MAGIC-COOKIE-1;
TCP listening is disabled. Xorg-created lock and X socket are expected
to disappear when the private server exits.
"""
import os
import pathlib
import secrets
import shutil
import subprocess
import sys
import tempfile
import time

HOME = pathlib.Path('/data/data/com.termux/files/home')
PREFIX = pathlib.Path(os.environ.get('PREFIX', '/data/data/com.termux/files/usr'))
PROJECT = HOME / 'alroot_wayland'
RUNTIME = HOME / '.alroot-wayland/runtime'
BINARY = PROJECT / '.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland'
LOG_DIR = PROJECT / '.source/gpu-stage27-pr25/s16-xwayland-gpu'


def stop(proc):
    if proc is None or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)


def main():
    if not BINARY.is_file() or not (RUNTIME / 'wayland-0').is_socket():
        print('S16_ROOTFUL_TEST=BLOCKED_NO_PRIVATE_BUILD_OR_ANDROID_WAYLAND_HOST')
        return 2
    xdir = PREFIX / 'tmp/.X11-unix'
    # Use only diagnostic display numbers, never Alroot XFCE's :90..:180.
    number = next((num for num in range(201, 241)
                   if not (xdir / f'X{num}').exists()
                   and not (PREFIX / f'tmp/.X{num}-lock').exists()
                   and not (PREFIX / f'tmp/.tX{num}-lock').exists()), None)
    if number is None:
        print('S16_ROOTFUL_TEST=BLOCKED_ALL_PRIVATE_X_DISPLAYS_OCCUPIED')
        return 2
    disp = f':{number}'
    lock = PREFIX / f'tmp/.X{number}-lock'
    xsocket = xdir / f'X{number}'
    tempdir = pathlib.Path(tempfile.mkdtemp(prefix='alroot-s16-lock-', dir=PREFIX / 'tmp'))
    auth = tempdir / 'Xauthority'
    auth.touch(mode=0o600)
    proc = None
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    log = LOG_DIR / f'xwayland-rootful-lock-{number}.log'
    try:
        subprocess.run(['xauth', '-f', str(auth), 'add', disp,
                        'MIT-MAGIC-COOKIE-1', secrets.token_hex(16)],
                       check=True, capture_output=True)
        env = os.environ.copy()
        env.update(TMPDIR=str(PREFIX / 'tmp'), XDG_RUNTIME_DIR=str(RUNTIME),
                   WAYLAND_DISPLAY='wayland-0', XAUTHORITY=str(auth),
                   XWAYLAND_FORCE_KGSL_SURFACELESS='1', FD_KGSL_ENABLE_DMABUF='1',
                   MESA_LOADER_DRIVER_OVERRIDE='kgsl', GALLIUM_DRIVER='freedreno',
                   EGL_PLATFORM='surfaceless',
                   LD_LIBRARY_PATH=str(PROJECT / 'packages/prefix/lib'))
        env.pop('DISPLAY', None)
        client_env = os.environ.copy()
        client_env.update(DISPLAY=disp, XAUTHORITY=str(auth),
                          MESA_LOADER_DRIVER_OVERRIDE='kgsl',
                          GALLIUM_DRIVER='freedreno', FD_KGSL_ENABLE_DMABUF='1')
        args = [str(BINARY), disp, '-geometry', '640x360', '-dpi', '96',
                '-auth', str(auth), '-nolisten', 'tcp', '-glamor', 'es', '-verbose', '2']
        print(f'S16_ISOLATED_ROOTFUL_X_DISPLAY={disp}', flush=True)
        print('S16_ROOTFUL_INVOCATION=NO_LISTENFD', flush=True)
        print(f'S16_ROOTFUL_XWAYLAND_LOG={log}', flush=True)
        # Do not clear the Android logcat ring. Capture a baseline and
        # only count newly emitted host logs from this test session.
        before = subprocess.run(['logcat', '-d', '-t', '3500', '-s', 'AlrootWayland'],
                                capture_output=True, text=True, timeout=8)
        baseline = set(before.stdout.splitlines())
        with log.open('w') as output:
            proc = subprocess.Popen(args, env=env,
                                    stdout=output, stderr=subprocess.STDOUT)
            ready = None
            for _ in range(65):
                if proc.poll() is not None:
                    break
                try:
                    result = subprocess.run(['xdpyinfo', '-display', disp, '-queryExtensions'],
                                            env=client_env, capture_output=True,
                                            text=True, timeout=1)
                    if result.returncode == 0:
                        ready = result.stdout
                        break
                except subprocess.TimeoutExpired:
                    pass
                time.sleep(.08)
            if ready is None:
                print('S16_ROOTFUL_START=FAIL', flush=True)
                print('\n'.join(log.read_text(errors='replace').splitlines()[-15:]),
                      flush=True)
                return 3
            print('S16_ROOTFUL_START=PASS', flush=True)
            if not lock.is_file() or not xsocket.is_socket():
                print('S16_ATOMIC_LOCK_OR_UNIX_SOCKET=FAIL', flush=True)
                return 4
            print('S16_TERMUX_TMP_ATOMIC_LOCK=PASS', flush=True)
            print('S16_TERMUX_UNIX_X_SOCKET=PASS', flush=True)
            for ext in ('DRI3', 'GLX', 'Present'):
                present = any(line.strip().startswith(ext + '  (')
                              for line in ready.splitlines())
                print(f'S16_ROOTFUL_{ext}={int(present)}', flush=True)
                if not present:
                    return 4

            # No child may take the same display number while our private
            # Xorg lock is active; test RENAME_NOREPLACE's EEXIST behavior.
            old_inode = lock.stat().st_ino
            with (LOG_DIR / f'xwayland-rootful-collision-{number}.log').open('w') as output2:
                try:
                    duplicate = subprocess.run(args, env=env,
                                               stdout=output2,
                                               stderr=subprocess.STDOUT, timeout=4)
                    dup_code = duplicate.returncode
                except subprocess.TimeoutExpired:
                    dup_code = 124
            same_lock = lock.is_file() and lock.stat().st_ino == old_inode
            survivor = proc.poll() is None
            print(f'S16_LOCK_COLLISION_REJECTED={int(dup_code not in (0, 124) and same_lock and survivor)}',
                  flush=True)
            if dup_code in (0, 124) or not same_lock or not survivor:
                return 5

            info = subprocess.run(['glxinfo', '-B'], env=client_env,
                                  capture_output=True, text=True, timeout=7)
            renderer = next((line for line in info.stdout.splitlines()
                             if 'OpenGL renderer string:' in line), 'NONE')
            print(f'S16_ROOTFUL_GLXINFO_EXIT={info.returncode}', flush=True)
            print(f'S16_ROOTFUL_GLX_RENDERER={renderer}', flush=True)
            if info.returncode != 0 or 'Adreno (TM) 840' not in renderer:
                return 6
            try:
                subprocess.run(['timeout', '3s', 'glxgears', '-info'],
                               env=client_env, capture_output=True,
                               text=True, timeout=5)
            except subprocess.TimeoutExpired:
                print('S16_GLGears_TEST_TIMEOUT=YES', flush=True)
            # Android host already has the S1.6 signed APK installed on the
            # test device. Its logs establish GPU import, not visual pixels.
            host = subprocess.run(['logcat', '-d', '-t', '3500', '-s', 'AlrootWayland'],
                                  capture_output=True, text=True, timeout=8)
            relevant = [line for line in host.stdout.splitlines()
                        if line not in baseline and
                           (f'Xwayland on :{number}' in line or
                            'LINEAR DMA-BUF -> Vulkan VkBuffer GPU-copy:' in line)]
            confirmed = (any(f'Xwayland on :{number}' in line for line in relevant)
                         and any('LINEAR DMA-BUF -> Vulkan VkBuffer GPU-copy:' in line
                                 for line in relevant))
            print('S16_HOST_NEW_GPU_LOG_ENTRIES=' + str(len(relevant)), flush=True)
            print('S16_ANDROID_HOST_VKBUFFER_GPU_COPY=' +
                  ('LOG_CONFIRMED' if confirmed else 'NOT_CONFIRMED'), flush=True)
            print('S16_PHYSICAL_SCREEN_PIXEL_CORRECTNESS=REQUIRES_HUMAN_CHECK', flush=True)
            return 0
    finally:
        stop(proc)
        shutil.rmtree(tempdir, ignore_errors=True)
        print('S16_PRIVATE_XWAYLAND_TERMINATED=' +
              ('YES' if proc is None or proc.poll() is not None else 'NO'),
              flush=True)
        print('S16_PRIVATE_X_LOCK_RELEASED=' +
              ('YES' if not lock.exists() else 'NO'), flush=True)
        print('S16_PRIVATE_X_SOCKET_RELEASED=' +
              ('YES' if not xsocket.exists() else 'NO'), flush=True)


if __name__ == '__main__':
    sys.exit(main())
