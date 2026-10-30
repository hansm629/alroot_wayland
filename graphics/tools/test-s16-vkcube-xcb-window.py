#!/data/data/com.termux/files/usr/bin/python3
"""S16 real on-device Turnip Vulkan XCB swapchain X11 drawable proof.

Creates only its own vkcube child, detects the added WM-managed X11 window
and samples five pixels from THAT window via XGetImage. This demonstrates
client-side X11 pixels, not the Android onscreen swapchain's image quality.
Never touches the installed Alroot APK or desktop session and never reads
personal windows.
"""
import os
import pathlib
import re
import subprocess
import sys
import time

PREFIX = pathlib.Path('/data/data/com.termux/files/usr')
HOME = pathlib.Path.home()
ROOT = pathlib.Path(__file__).resolve().parents[2]
STATE = HOME / '.alroot-wayland'
OUT = ROOT / '.source/gpu-stage27-pr25/s16-xwayland-gpu'


def clients(env):
    result = subprocess.run(['xprop', '-root', '_NET_CLIENT_LIST'],
                            env=env, text=True, capture_output=True, timeout=3)
    if result.returncode:
        raise RuntimeError('X11 root window list unavailable: ' + result.stderr[:150])
    return set(re.findall(r'0x[0-9a-f]+', result.stdout))


def main():
    if not (STATE / 'display').is_file() or not (STATE / 'Xauthority').is_file():
        print('S16_VKCUBE_X11=NO_ALROOT_DESKTOP')
        return 2
    display = (STATE / 'display').read_text().strip()
    if not re.fullmatch(r':[1-9][0-9]{1,2}', display):
        print('S16_VKCUBE_X11=UNSAFE_DISPLAY')
        return 2
    icd = PREFIX / 'share/vulkan/icd.d/freedreno_icd.aarch64.json'
    if not icd.is_file():
        print('S16_VKCUBE_X11=NO_TURNIP_ICD')
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    binary = OUT / 's16-x11-pixmap-sample'
    subprocess.run(['clang', '--target=aarch64-linux-android35', '-O2',
                    '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    str(ROOT / 'graphics/tests/s16-x11-pixmap-sample.c'),
                    '-lX11', '-o', str(binary)], check=True, timeout=15)
    env = os.environ.copy()
    env.update(DISPLAY=display, XAUTHORITY=str(STATE / 'Xauthority'),
               VK_ICD_FILENAMES=str(icd))
    env.pop('WAYLAND_DISPLAY', None)
    before = clients(env)
    proc = None
    try:
        with (OUT / 's16-vkcube-xcb-window.log').open('w') as log:
            proc = subprocess.Popen([str(PREFIX / 'bin/vkcube'), '--wsi', 'xcb'],
                                    env=env, stdout=log, stderr=subprocess.STDOUT)
            new = set()
            for _ in range(28):
                if proc.poll() is not None:
                    break
                new = clients(env) - before
                if new:
                    break
                time.sleep(.12)
            print('S16_VKCUBE_X11_NEW_MANAGED_WINDOWS=' + str(len(new)), flush=True)
            if len(new) != 1:
                print('S16_VKCUBE_X11=FAIL_NO_ONE_NEW_WINDOW', flush=True)
                return 3
            xid = next(iter(new))
            # Query exactly the X window our child added. Do not probe any
            # other desktop window or log any screen contents as an image.
            xprop = subprocess.run(['xprop', '-id', xid, '_NET_WM_NAME', 'WM_STATE'],
                                   env=env, capture_output=True, text=True, timeout=3)
            mapped = 'window state: Normal' in xprop.stdout
            print('S16_VKCUBE_X11_WM_MAPPED=' + ('PASS' if mapped else 'FAIL'), flush=True)
            print('S16_VKCUBE_X11_TITLE=' +
                  ('Vkcube X11' if 'Vkcube X11' in xprop.stdout else 'UNKNOWN'),
                  flush=True)
            if not mapped:
                return 4
            time.sleep(.6)
            sampled = subprocess.run([str(binary), xid], env=env,
                                     text=True, capture_output=True, timeout=9)
            (OUT / 's16-vkcube-x11-pixels.log').write_text(sampled.stdout + sampled.stderr)
            print('S16_VKCUBE_X11_XGETIMAGE_EXIT=' + str(sampled.returncode), flush=True)
            for row in sampled.stdout.splitlines():
                if 'S16_X11_GETIMAGE_READBACK' in row:
                    print(row, flush=True)
            if sampled.returncode != 0:
                return 5
            print('S16_VKCUBE_X11_PIXELS=PASS', flush=True)
            print('S16_VKCUBE_ANDROID_VISIBLE_PIXELS=NOT_VERIFIED', flush=True)
            return 0
    finally:
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)
        print('S16_VKCUBE_TEST_PROCESS_CLEANUP=PASS', flush=True)


if __name__ == '__main__':
    sys.exit(main())
