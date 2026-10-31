#!/data/data/com.termux/files/usr/bin/python3
"""User-visible S24 Vulkan Wrapper smoke, with no automatic demo timeout.

This does NOT show a graphical window in Termux's default DISPLAY=:0.
The temporary client belongs to the already-running Alroot Wayland helper
XFCE display (e.g. :94); the user must bring THAT Android Activity forward.
--coalesced-xcb explicitly selects a pinned private vkcube event-loop build;
it does not change the default installed vkcube or the S26 wrapper ICD.
"""
import argparse
import hashlib
import os
import pathlib
import re
import signal
import subprocess
import sys
import time
from private_kgsl_xwayland_session import validate_private_kgsl_xwayland

HOME = pathlib.Path('/data/data/com.termux/files/home')
ROOT = HOME / 'alroot_wayland'
PREFIX = pathlib.Path('/data/data/com.termux/files/usr')
STATE = HOME / '.alroot-wayland'
ICD = ROOT / '.source/gpu-stage1/s24-wrapper-xcb-resize/runtime/wrapper_icd.private.json'
S26_ICD = ROOT / '.source/gpu-stage1/s26-wrapper-resize-retry/runtime/wrapper_icd.private.json'
GLX27 = ROOT / '.source/gpu-stage27-pr25/mesa-private/glx-runtime/lib'
S39_COALESCED_VKCUBE = ROOT / '.source/gpu-stage1/s39-live-resize/runtime/vkcube.coalesced'
S39_COALESCED_SHA256 = '0050e7126c8d096cd14a7e0b1026b7063f898e33eb7be5b80116991cee57c208'


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def x11_windows(env):
    result = subprocess.run(['xprop', '-root', '_NET_CLIENT_LIST'], env=env,
                            capture_output=True, text=True, timeout=4)
    if result.returncode:
        raise RuntimeError('private authenticated X11 window list unavailable')
    return set(re.findall(r'0x[0-9a-fA-F]+', result.stdout))


def require_session():
    result = subprocess.run(['awstart', '--status'], capture_output=True,
                            text=True, timeout=8)
    status = result.stdout
    m = re.search(r'^xwayland: RUNNING ([0-9]+)$', status, re.M)
    d = re.search(r'^X DISPLAY: (:[0-9]{2,3})$', status, re.M)
    if not m or not d or 'x11_desktop: READY' not in status:
        raise RuntimeError('Alroot helper private XFCE Xwayland not ready')
    display, pid = d.group(1), int(m.group(1))
    if display == ':0' or not (STATE / 'Xauthority').is_file():
        raise RuntimeError('refusing to target :0 or an unauthenticated X server')
    validate_private_kgsl_xwayland(pid, display)
    return pid, display


def run():
    parser = argparse.ArgumentParser(description='Open a persistent S24 demo on Alroot Helper XFCE, not Termux-X11.')
    parser.add_argument('demo', nargs='?', default='vulkan', choices=('vulkan', 'zink'))
    parser.add_argument('--s26', action='store_true',
                        help='Opt-in S26 private XCB transient-import retry (default retains S24 binary)')
    parser.add_argument('--coalesced-xcb', action='store_true',
                        help='Explicit private vkcube XCB Configure batching; requires vulkan and --s26')
    parser.add_argument('--auto-resize', action='store_true',
                        help='move and resize only this demo, then leave it open for manual testing')
    args = parser.parse_args()
    if args.coalesced_xcb and (args.demo != 'vulkan' or not args.s26):
        parser.error('--coalesced-xcb requires demo vulkan and --s26')
    icd = S26_ICD if args.s26 else ICD
    if not icd.is_file() or not (icd.parent / 'libvulkan_wrapper.so').is_file():
        raise RuntimeError('missing private wrapper ICD; build the requested route first')
    pid, display = require_session()
    private_xwayland = validate_private_kgsl_xwayland(pid, display)
    env = os.environ.copy()
    original_display = env.get('DISPLAY', 'unset')
    for key in ('WAYLAND_DISPLAY', 'LD_PRELOAD', 'LIBGL_ALWAYS_SOFTWARE',
                'MESA_LOADER_DRIVER_OVERRIDE', 'GALLIUM_DRIVER', 'LIBGL_DRIVERS_PATH',
                'S26_WRAPPER_TEST_FAIL_PIXMAP_ONCE', 'S26_WRAPPER_XCB_RETRY_TRANSIENT',
                'S39_WRAPPER_XCB_CHECK_BEFORE_ACK', 'S39_WSI_TRACE',
                'S39_VULKAN_TRACE', 'S39_VULKAN_TRACE_FILE'):
        env.pop(key, None)
    env.update(DISPLAY=display, XAUTHORITY=str(STATE / 'Xauthority'),
               VK_DRIVER_FILES=str(icd), VK_ICD_FILENAMES=str(icd),
               S24_WRAPPER_XCB_AHB_PAD_HEIGHT='1')
    if args.s26:
        env['S26_WRAPPER_XCB_RETRY_TRANSIENT'] = '1'
    if args.demo == 'vulkan':
        client = S39_COALESCED_VKCUBE if args.coalesced_xcb else PREFIX / 'bin/vkcube'
        client_label = ('PRIVATE_VKCUBE_XCB_CONFIGURE_COALESCED' if args.coalesced_xcb
                        else 'INSTALLED_VKCUBE_XCB')
        if not client.is_file() or not os.access(client, os.X_OK):
            raise RuntimeError('selected vkcube executable is missing or not executable')
        if args.coalesced_xcb and file_sha256(client) != S39_COALESCED_SHA256:
            raise RuntimeError('private coalesced vkcube hash mismatch; refusing unknown client')
        command = [str(client), '--wsi', 'xcb']
    else:
        if not (GLX27 / 'libGLX_mesa.so').is_file():
            raise RuntimeError('missing private Mesa27 GLX runtime')
        env.update(LD_LIBRARY_PATH=str(GLX27) + (':' + os.environ['LD_LIBRARY_PATH']
                                                if os.environ.get('LD_LIBRARY_PATH') else ''),
                   LIBGL_DRIVERS_PATH=str(GLX27 / 'dri'),
                   MESA_LOADER_DRIVER_OVERRIDE='zink', GALLIUM_DRIVER='zink',
                   LIBGL_KOPPER_DRI2='1')
        client = PREFIX / 'bin/glxgears'
        client_label = 'INSTALLED_GLXGEARS_ZINK'
        command = [str(client)]

    client_sha256 = file_sha256(client)
    baseline = x11_windows(env)
    print('S24_VISUAL_PRIVATE_WRAPPER=' + ('S26_TRANSIENT_RETRY' if args.s26 else 'S24_BASELINE'), flush=True)
    print('S24_VISUAL_PRIVATE_ICD=' + str(icd), flush=True)
    print('S24_VISUAL_WRAPPER_SHA256=' + file_sha256(icd.parent / 'libvulkan_wrapper.so'), flush=True)
    print('S24_VISUAL_CLIENT=' + client_label, flush=True)
    print('S24_VISUAL_CLIENT_EXECUTABLE=' + str(client), flush=True)
    print('S24_VISUAL_CLIENT_SHA256=' + client_sha256, flush=True)
    print('S24_VISUAL_CLIENT_PATCH=' +
          ('VKCUBE_XCB_CONFIGURE_BATCH_ONLY' if args.coalesced_xcb else 'NONE_SELECTED'), flush=True)
    print('S24_VISUAL_PRIVATE_XWAYLAND=' + private_xwayland, flush=True)
    print('S24_VISUAL_TARGET=ALROOT_WAYLAND_HELPER_XFCE_' + display, flush=True)
    print('S24_VISUAL_CALLER_DISPLAY=' + original_display, flush=True)
    print('S24_VISUAL_NOT_ON_TERMUX_X11_DISPLAY_0=TRUE', flush=True)
    print('S24_VISUAL_ACTION=OPEN_ALROOT_WAYLAND_ANDROID_HELPER_APP_TO_SEE_WINDOW', flush=True)
    print('S24_VISUAL_ANDROID_FOREGROUND=NOT_VERIFIABLE_FROM_TERMUX', flush=True)
    print('S24_VISUAL_LIFETIME=PERSISTENT_UNTIL_WINDOW_CLOSED_OR_CTRL_C', flush=True)
    print('S24_VISUAL_EXPECTED=DRAG_AND_RESIZE_TEST_WINDOW_MANUALLY', flush=True)

    start_time = time.monotonic()
    proc = subprocess.Popen(command, env=env, start_new_session=True)
    window_id = None
    stopping_signal = None

    def on_signal(signum, frame):
        nonlocal stopping_signal
        stopping_signal = signum
        raise KeyboardInterrupt

    old_term = signal.signal(signal.SIGTERM, on_signal)
    try:
        until = time.monotonic() + 12
        while time.monotonic() < until and proc.poll() is None:
            new = x11_windows(env) - baseline
            for wid in sorted(new):
                detail = subprocess.run(['xprop', '-id', wid, 'WM_STATE', '_NET_WM_PID'],
                                        env=env, capture_output=True, text=True, timeout=4)
                if 'window state: Normal' not in detail.stdout:
                    continue
                pidmatch = re.search(r'_NET_WM_PID\(CARDINAL\) = ([0-9]+)', detail.stdout)
                if pidmatch and int(pidmatch.group(1)) != proc.pid:
                    continue
                if pidmatch or len(new) == 1:
                    window_id = wid
                    break
            if window_id:
                break
            time.sleep(.18)
        if not window_id:
            print('S24_VISUAL_MAPPED_WINDOW=NOT_CONFIRMED', flush=True)
            if proc.poll() is not None:
                return 3
            print('S24_VISUAL_CLIENT_STILL_RUNNING=TRUE_CHECK_ALROOT_HELPER', flush=True)
        else:
            print('S24_VISUAL_MAPPED_WINDOW=' + window_id, flush=True)
            raised = subprocess.run(['xdotool', 'windowraise', window_id], env=env,
                                    capture_output=True, timeout=4)
            activated = subprocess.run(['xdotool', 'windowactivate', window_id], env=env,
                                       capture_output=True, timeout=4)
            print('S24_VISUAL_OWN_WINDOW_RAISED=' +
                  ('PASS' if raised.returncode == 0 and activated.returncode == 0
                   else 'NOT_CONFIRMED'), flush=True)
            if args.auto_resize:
                for index, (width, height) in enumerate(((900, 650), (1200, 701), (500, 500)), 1):
                    if proc.poll() is not None:
                        break
                    moved = subprocess.run(['xdotool', 'windowmove', window_id,
                                            str(125 + 25 * index), str(100 + 20 * index)],
                                           env=env, capture_output=True, timeout=4)
                    resized = subprocess.run(['xdotool', 'windowsize', window_id,
                                              str(width), str(height)],
                                             env=env, capture_output=True, timeout=4)
                    if moved.returncode or resized.returncode:
                        print('S24_VISUAL_RESIZE_COMMAND_FAILED=' + str(index), flush=True)
                        break
                    print(f'S24_VISUAL_RESIZE_{index}={width}x{height}', flush=True)
                    time.sleep(2.5)
        print('S24_VISUAL_WINDOW_STAYS_OPEN_FOR_USER_CHECK=TRUE', flush=True)
        ret = proc.wait()
        lifetime = time.monotonic() - start_time
        print('S24_VISUAL_CLIENT_LIFETIME_SECONDS=' + str(round(lifetime, 1)), flush=True)
        print('S24_VISUAL_CLIENT_EXIT_CODE=' + str(ret), flush=True)
        if lifetime < 4:
            print('S24_VISUAL_EARLY_EXIT=RECHECK_WHETHER_USER_CLOSED_WINDOW', flush=True)
        if ret:
            print('S24_VISUAL_CLIENT_CRASH_OR_NONZERO_EXIT=FAIL', flush=True)
        else:
            print('S24_VISUAL_CLIENT_CLOSED_NORMALLY=TRUE', flush=True)
        return 0 if ret == 0 else 5
    except KeyboardInterrupt:
        print('S24_VISUAL_STOPPED=' +
              ('EXTERNAL_SIGTERM' if stopping_signal == signal.SIGTERM
               else 'USER_KEYBOARD_INTERRUPT'), flush=True)
        return 0
    finally:
        signal.signal(signal.SIGTERM, old_term)
        if proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
                proc.wait(timeout=3)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                if proc.poll() is None:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait(timeout=3)
        try:
            current = require_session()
        except Exception:
            current = None
        print('S24_VISUAL_EXISTING_XFCE_SERVER=' +
              ('UNCHANGED' if current == (pid, display) else 'NEEDS_RECHECK'), flush=True)
        print('S24_VISUAL_ANDROID_VISIBLE_PIXELS=USER_OBSERVATION_REQUIRED', flush=True)


if __name__ == '__main__':
    try:
        sys.exit(run())
    except Exception as error:
        print('S24_VISUAL_TEST_ERROR=' + str(error)[:220], flush=True)
        sys.exit(2)
