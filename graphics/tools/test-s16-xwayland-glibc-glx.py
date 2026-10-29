#!/data/data/com.termux/files/usr/bin/python3
"""S1.6 isolated real Alroot glibc + KGSL Xwayland GLX/DRI3 diagnostic.

IMPORTANT: This is NOT a user/production X server: -ac is only used on an
isolated :241..:270 Unix socket, mode 0600, -nolisten tcp, with immediate
process/socket cleanup. Never disable authentication for a real :0/:90 session.
The live Android helper is not replaced, so host GPU frame presentation is
NOT VERIFIED by this test.
"""
import os
import pathlib
import re
import socket
import subprocess
import sys
import time

HOME = pathlib.Path('/data/data/com.termux/files/home')
PREFIX = pathlib.Path('/data/data/com.termux/files/usr')
PROJECT = HOME / 'alroot_wayland'
ROOTFS = pathlib.Path(os.environ.get('ALROOT_ROOTFS', str(PREFIX / 'var/lib/proot-distro/containers/debian/rootfs')))
WAYLAND = HOME / '.alroot-wayland/runtime'
PRIVATE_XWAYLAND = PROJECT / '.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland'
LAUNCHER = HOME / '다운로드/alroot-runtime/scripts/alroot_run.sh'
LOG_DIR = PROJECT / '.source/gpu-stage27-pr25/s16-xwayland-gpu'


def finish(proc):
    if proc is None or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)


def guest_command(program, args, env, timeout_seconds):
    cmd = ['bash', str(LAUNCHER), '--raw', program, *args]
    try:
        completed = subprocess.run(cmd, env=env, text=True,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, timeout=timeout_seconds)
        return completed.returncode, completed.stdout
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or b''
        if isinstance(output, bytes):
            output = output.decode(errors='replace')
        return 124, output + '\nALROOT_S16_CLIENT_TIMEOUT\n'


def main():
    if not PRIVATE_XWAYLAND.is_file() or not LAUNCHER.is_file():
        print('S16_GLIBC_GLX=BLOCKED_PRIVATE_XWAYLAND_OR_ALROOT_RUNTIME_MISSING')
        return 2
    if not (WAYLAND / 'wayland-0').is_socket():
        print('S16_GLIBC_GLX=BLOCKED_ALROOT_WAYLAND_HOST_NOT_RUNNING')
        return 2
    guest_dir = ROOTFS / 'tmp/.X11-unix'
    if not guest_dir.is_dir():
        print('S16_GLIBC_GLX=BLOCKED_GUEST_TMP_SOCKET_DIR_MISSING')
        return 2
    xdir = PREFIX / 'tmp/.X11-unix'
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    listener = None
    xserver = None
    guest_link = None
    x_socket = None
    log = LOG_DIR / 'xwayland-glibc-standalone.log'
    try:
        for number in range(241, 271):
            path = xdir / f'X{number}'
            lock = PREFIX / f'tmp/.X{number}-lock'
            guest = guest_dir / f'X{number}'
            if not path.exists() and not lock.exists() and not guest.exists() and not guest.is_symlink():
                x_socket = path
                guest_link = guest
                break
        if x_socket is None:
            print('S16_GLIBC_GLX=BLOCKED_NO_ISOLATED_DISPLAY_FREE')
            return 2
        xdir.mkdir(parents=True, exist_ok=True)
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(x_socket))
        listener.listen(16)
        x_socket.chmod(0o600)
        guest_link.symlink_to(x_socket)
        display = f':{number}'
        server_env = os.environ.copy()
        server_env.update(XDG_RUNTIME_DIR=str(WAYLAND), WAYLAND_DISPLAY='wayland-0',
                          XWAYLAND_FORCE_KGSL_SURFACELESS='1',
                          FD_KGSL_ENABLE_DMABUF='1',
                          MESA_LOADER_DRIVER_OVERRIDE='kgsl',
                          GALLIUM_DRIVER='freedreno',
                          EGL_PLATFORM='surfaceless',
                          LD_LIBRARY_PATH=str(PROJECT / 'packages/prefix/lib'))
        server_env.pop('DISPLAY', None)
        args = [str(PRIVATE_XWAYLAND), display, '-listenfd',
                str(listener.fileno()), '-geometry', '640x360', '-dpi', '96',
                '-nolisten', 'tcp', '-ac', '-glamor', 'es', '-verbose', '2']
        print('S16_TEMP_X_SERVER_AUTH=DISABLED_PRIVATE_UNIX_ONLY', flush=True)
        print('S16_TEMP_X_SERVER_TCP=DISABLED', flush=True)
        print('S16_TEMP_X_SERVER_DISPLAY=' + display, flush=True)
        with log.open('w') as output:
            xserver = subprocess.Popen(args, env=server_env,
                                       stdout=output, stderr=subprocess.STDOUT,
                                       pass_fds=(listener.fileno(),))
            client_env = os.environ.copy()
            client_env.update(DISPLAY=display, ALROOT_SVC_PATCH='1',
                              MESA_LOADER_DRIVER_OVERRIDE='kgsl',
                              GALLIUM_DRIVER='freedreno',
                              FD_KGSL_ENABLE_DMABUF='1')
            client_env.pop('XAUTHORITY', None)
            client_env.pop('LD_LIBRARY_PATH', None)
            for _ in range(75):
                if xserver.poll() is not None:
                    break
                try:
                    p = subprocess.run(['xdpyinfo', '-display', display,
                                        '-queryExtensions'], env=server_env,
                                       capture_output=True, text=True, timeout=1)
                except subprocess.TimeoutExpired:
                    p = None
                if p and p.returncode == 0:
                    extensions = p.stdout
                    break
                time.sleep(.06)
            else:
                extensions = ''
            if not extensions:
                print('S16_GLIBC_GLX=FAIL_XWAYLAND_NOT_READY', flush=True)
                return 3
            for ext in ('DRI3', 'GLX', 'Present'):
                visible = any(row.lstrip().startswith(ext + '  (')
                              for row in extensions.splitlines())
                print(f'S16_PRIVATE_XWAYLAND_{ext}={int(visible)}', flush=True)
                if not visible:
                    print('S16_GLIBC_GLX=FAIL_X11_EXTENSION_MISSING', flush=True)
                    return 4
            code, info = guest_command('/usr/bin/glxinfo', ['-B'], client_env, 9)
            (LOG_DIR / 's16-glibc-glxinfo-isolated.log').write_text(info)
            renderer = next((line for line in info.splitlines()
                             if 'OpenGL renderer string' in line), 'NOT_AVAILABLE')
            print('S16_GLIBC_GLXINFO_EXIT=' + str(code), flush=True)
            print('S16_GLIBC_GL_RENDERER=' + renderer.strip(), flush=True)
            if code or 'Adreno (TM) 840' not in renderer:
                print('S16_GLIBC_GLX=FAIL_RENDERER_OR_CLIENT', flush=True)
                return 5
            code, gears = guest_command('/usr/bin/glxgears', ['-info'], client_env, 7)
            (LOG_DIR / 's16-glibc-glxgears-isolated.log').write_text(gears)
            fps = re.findall(r'([0-9]+(?:\.[0-9]+)?)\s*FPS', gears)
            print('S16_GLIBC_GLXGEARS_TIMED_EXIT=' + str(code), flush=True)
            if fps:
                print('S16_GLIBC_GLXGEARS_FPS_SAMPLE=' + fps[0], flush=True)
            if not fps or code not in (0, 124):
                print('S16_GLIBC_GLX=FAIL_NO_FRAMES', flush=True)
                return 6
            print('S16_GLIBC_GLX_GPU_EXECUTION=PASS', flush=True)
            print('S16_ANDROID_HOST_GPU_PRESENT=NOT_VERIFIED_OLD_INSTALLED_HOST', flush=True)
            print('S16_GLX_FPS_IS_OFFSCREEN_OR_UNVERIFIED_PRESENT_NOT_A_BENCHMARK', flush=True)
            return 0
    finally:
        finish(xserver)
        if listener:
            listener.close()
        if guest_link is not None and guest_link.is_symlink() and guest_link.readlink() == x_socket:
            guest_link.unlink()
        if x_socket is not None and x_socket.exists():
            x_socket.unlink()
        print('S16_PRIVATE_XWAYLAND_TEST_CLEANUP=PASS', flush=True)


if __name__ == '__main__':
    sys.exit(main())
