#!/data/data/com.termux/files/usr/bin/python3
"""Bounded S26 fault-injection regression on ONLY our own XCB demo XID.

With --expect-failure this intentionally injects one BadAlloc into the
project-private Vulkan Wrapper's DRI3 import without its S26 retry enabled.
The exact GLXBadCurrentWindow from the user report must occur, proving
this recovery path is exercised rather than merely avoiding the problem.
Never used with the installed system driver or on Termux-X11 :0.
"""
import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from private_kgsl_xwayland_session import validate_private_kgsl_xwayland

HOME = Path('/data/data/com.termux/files/home')
PREFIX = Path('/data/data/com.termux/files/usr')
ROOT = HOME / 'alroot_wayland'
STATE = HOME / '.alroot-wayland'
PRIVATE = ROOT / '.source/gpu-stage1/s26-wrapper-resize-retry'
GLX = ROOT / '.source/gpu-stage27-pr25/mesa-private/glx-runtime/lib'
OUTPUT = PRIVATE / 'logs'
RESIZE = PRIVATE / 's26-xfwm-resize'
SIZES = ((300, 300), (900, 651), (901, 649), (1200, 701),
         (500, 503), (777, 673), (900, 650), (500, 500))


def run_quick(command, env, timeout=5):
    return subprocess.run(command, env=env, capture_output=True,
                          text=True, timeout=timeout)


def window_ids(env):
    p = run_quick(['xprop', '-root', '_NET_CLIENT_LIST'], env)
    if p.returncode:
        raise RuntimeError('authenticated private X11 WM not responsive')
    return set(re.findall(r'0x[0-9a-fA-F]+', p.stdout))


def private_session():
    p = subprocess.run(['awstart', '--status'], capture_output=True,
                       text=True, timeout=8)
    x = re.search(r'^xwayland: RUNNING (\d+)$', p.stdout, re.M)
    d = re.search(r'^X DISPLAY: (:\d{2,3})$', p.stdout, re.M)
    if not x or not d or d.group(1) == ':0' or 'x11_desktop: READY' not in p.stdout:
        raise RuntimeError('private GPU XFCE not running')
    pid = int(x.group(1))
    validate_private_kgsl_xwayland(pid, d.group(1))
    return pid, d.group(1)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--demo', choices=('zink', 'vulkan'), default='zink')
    p.add_argument('--fault-once', type=int, default=0,
                   help='AHB pixmap creation index to fail once (0 disables injection)')
    p.add_argument('--no-retry', action='store_true',
                   help='Opt-out of S26 recovery for the negative control')
    p.add_argument('--expect-failure', action='store_true',
                   help='Require precisely the injected GLXBadCurrentWindow error')
    p.add_argument('--seconds', type=int, default=8)
    p.add_argument('--pause-ms', type=int, default=275)
    opts = p.parse_args()
    if not 0 <= opts.fault_once <= 10000 or not 2 <= opts.seconds <= 60 or not 35 <= opts.pause_ms <= 3000:
        p.error('fault index / duration / interval out of safe bounds')
    if opts.expect_failure and (opts.demo != 'zink' or not opts.fault_once or not opts.no_retry):
        p.error('negative control requires --demo zink --no-retry --fault-once N')
    if not (PRIVATE / 'runtime/wrapper_icd.private.json').is_file():
        raise RuntimeError('S26 private ICD not built')

    pid, display = private_session()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    if not RESIZE.is_file():
        subprocess.run(['clang', '--target=aarch64-linux-android35', '-std=c11', '-O2',
                        '-Wall', '-Wextra', '-Werror',
                        'graphics/tests/s26-xfwm-resize.c', '-lX11', '-o', str(RESIZE)],
                       cwd=ROOT, check=True, timeout=35)
    env = os.environ.copy()
    for key in ('WAYLAND_DISPLAY', 'LD_PRELOAD', 'VK_ICD_FILENAMES',
                'VK_DRIVER_FILES', 'MESA_LOADER_DRIVER_OVERRIDE',
                'GALLIUM_DRIVER', 'LIBGL_DRIVERS_PATH', 'LIBGL_ALWAYS_SOFTWARE',
                'S26_WRAPPER_XCB_RETRY_TRANSIENT', 'S26_WRAPPER_TEST_FAIL_PIXMAP_ONCE'):
        env.pop(key, None)
    icd = PRIVATE / 'runtime/wrapper_icd.private.json'
    env.update(DISPLAY=display, XAUTHORITY=str(STATE / 'Xauthority'),
               VK_DRIVER_FILES=str(icd), VK_ICD_FILENAMES=str(icd),
               LIBGL_KOPPER_DRI2='1', S24_WRAPPER_XCB_AHB_PAD_HEIGHT='1',
               MESA_LOG_LEVEL='debug')
    if not opts.no_retry:
        env['S26_WRAPPER_XCB_RETRY_TRANSIENT'] = '1'
    if opts.fault_once:
        env['S26_WRAPPER_TEST_FAIL_PIXMAP_ONCE'] = str(opts.fault_once)
        env['S16_WRAPPER_X11_WSI_TRACE'] = '1'
    if opts.demo == 'zink':
        env['LD_LIBRARY_PATH'] = str(GLX)
        env['LIBGL_DRIVERS_PATH'] = str(GLX / 'dri')
        env['GALLIUM_DRIVER'] = 'zink'
        env['MESA_LOADER_DRIVER_OVERRIDE'] = 'zink'
        command = [str(PREFIX / 'bin/glxgears')]
    else:
        command = [str(PREFIX / 'bin/vkcube'), '--wsi', 'xcb']

    logfile = OUTPUT / ('s26-fault-' + opts.demo + '-at' + str(opts.fault_once) +
                        ('-no-retry' if opts.no_retry else '-retry') + '.log')
    before = window_ids(env)
    new_window = None
    proc = None
    stopped_intentionally = False
    steps = 0
    try:
        with logfile.open('w') as f:
            proc = subprocess.Popen(command, env=env, stdout=f,
                                    stderr=subprocess.STDOUT, start_new_session=True)
        start = time.monotonic()
        while time.monotonic() - start < 8 and proc.poll() is None:
            new = window_ids(env) - before
            if len(new) == 1:
                candidate = next(iter(new))
                props = run_quick(['xprop', '-id', candidate, 'WM_STATE', '_NET_WM_PID'], env)
                if 'window state: Normal' in props.stdout:
                    owner = re.search(r'_NET_WM_PID\(CARDINAL\) = (\d+)', props.stdout)
                    if owner is None or int(owner.group(1)) == proc.pid:
                        new_window = candidate
                        break
            time.sleep(.14)
        print(f'S26_TEST_DISPLAY={display} XWAYLAND_PID={pid} OWN_WINDOW={new_window}', flush=True)
        if not new_window:
            raise RuntimeError('test client never mapped its own window')
        run_start = time.monotonic()
        while time.monotonic() - run_start < opts.seconds and proc.poll() is None:
            width, height = SIZES[steps % len(SIZES)]
            change = run_quick([str(RESIZE), new_window, '110', '120',
                                str(width), str(height)], env)
            if change.returncode:
                raise RuntimeError(f'own window resize rejected: {change.stderr[-180:]}')
            steps += 1
            time.sleep(opts.pause_ms / 1000.0)
        was_alive = proc.poll() is None
        print(f'S26_RESIZE_STEPS={steps} CLIENT_ALIVE={was_alive}', flush=True)
    finally:
        if proc is not None and proc.poll() is None:
            stopped_intentionally = True
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait(timeout=4)
    text = logfile.read_text(errors='replace')
    injected = 'S26_XCB_DRI3_INJECTED_TRANSIENT_BADALLOC_ONCE=' in text
    retry = 'S26_XCB_DRI3_AHB_RETRY attempt=1' in text
    bad = 'GLXBadCurrentWindow' in text
    same_server = private_session() == (pid, display)
    if opts.expect_failure:
        ok = injected and bad and proc.returncode == 1 and same_server
    else:
        ok = was_alive and not bad and same_server and (
            not opts.fault_once or (injected and retry)) and stopped_intentionally
    print(f'S26_CLIENT_RC={proc.returncode} INJECTED={injected} '
          f'RETRIED={retry} GLX_ERROR={bad} EXISTING_XFCE={same_server}', flush=True)
    print('S26_XCB_TRANSIENT_IMPORT_RECOVERY=' +
          ('PASS_EXPECTED_NEGATIVE' if opts.expect_failure and ok
           else 'PASS' if ok else 'FAIL'), flush=True)
    print(f'S26_LOG={logfile}', flush=True)
    if not ok:
        for line in text.splitlines()[-18:]:
            print('S26_FAIL_LOG=' + line[:220], flush=True)
    return 0 if ok else 5


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as exc:
        print('S26_TEST_ERROR=' + str(exc)[:260], flush=True)
        sys.exit(2)
