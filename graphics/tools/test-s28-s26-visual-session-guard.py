#!/data/data/com.termux/files/usr/bin/python3
"""Read-only S28/S26 visual-test launcher guard regression.

Do not create/kill any window or restart XFCE. Verify that all three live
test entrypoints recognize the same, exact allowlisted S28 private Xwayland
and reject wrong processes, displays and a mismatching executable hash.
"""

import hashlib
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import private_kgsl_xwayland_session as guard


def load_script(stem: str):
    spec = importlib.util.spec_from_file_location(stem, TOOLS / (stem + '.py'))
    script = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(script)
    return script


def rejected(pid: int, display: str):
    try:
        guard.validate_private_kgsl_xwayland(pid, display)
    except RuntimeError:
        return
    raise AssertionError(f'untrusted PID/DISPLAY accepted: {pid} {display}')


def main():
    status = subprocess.check_output(['awstart', '--status'], text=True, timeout=8)
    x = re.search(r'^xwayland: RUNNING (\d+)$', status, re.M)
    d = re.search(r'^X DISPLAY: (:[0-9]{2,3})$', status, re.M)
    assert x and d and 'x11_desktop: READY' in status, 'active private XFCE needed'
    pid = int(x.group(1))
    display = d.group(1)
    initial_status = status

    for path, (name, sha256) in guard.PRIVATE_XWAYLANDS.items():
        assert path.is_file(), f'missing allowlisted {name} binary'
        assert hashlib.sha256(path.read_bytes()).hexdigest() == sha256, (
            f'allowlisted {name} binary changed')
    print('S28_ALLOWLISTED_S16_AND_S28_BINARY_SHA256=PASS', flush=True)
    assert len(guard.PRIVATE_XWAYLANDS) == 10, 'unexpected number of private KGSL server profiles'
    print('S39_ALL_TEN_PINNED_PRIVATE_XWAYLAND_BINARY_HASHES=PASS', flush=True)

    label = guard.validate_private_kgsl_xwayland(pid, display)
    assert label in ('S16_S27_KGSL_BASELINE', 'S28_KGSL_RESIZE_PRESERVE',
                     'S29_KGSL_NATIVE_RESIZE',
                     'S30_KGSL_STABLE_FRAME_TINY_AHB_FD0',
                     'S31_KGSL_ADAPTIVE_RESIZE_PRESENT_EXTENT',
                     'S32_KGSL_NONBLANK_REAL_PRESENT',
                     'S36_KGSL_ROOTFUL_AHB_DIRECT_VKIMAGE',
                     'S37_KGSL_THIN_HEIGHT_DRI3_AHB_FD0',
                     'S38_KGSL_FRAME_LIFETIME_LATEST_PRESENT',
                     'S39_KGSL_RESIZE_ORIGIN_ROOT_POOL')
    print(f'S28_LIVE_PRIVATE_XWAYLAND={label} PID={pid} DISPLAY={display}', flush=True)

    scripts = {
        'S24_S26_VISUAL': ('run-s24-wrapper-visual', 'require_session'),
        'S23_WINDOWED_MATRIX': ('test-s23-live-xfce-windowed', 'require_private_session'),
        'S26_FAULT_RECOVERY': ('test-s26-xcb-fault-recovery', 'private_session'),
    }
    for name, (module, function) in scripts.items():
        script = load_script(module)
        assert getattr(script, function)() == (pid, display)
        print(f'S28_{name}_SESSION_VALIDATION=PASS', flush=True)

    rejected(pid, ':0')
    rejected(pid, ':99' if display != ':99' else ':98')
    rejected(0, display)
    rejected(os.getpid(), display)
    rejected(2_147_483_647, display)
    print('S28_WRONG_DISPLAY_AND_UNRELATED_PROCESS_REJECTION=PASS', flush=True)

    # Do not alter any Xwayland binary, even for a negative test.
    with patch.object(guard, '_file_sha256', return_value='f' * 64):
        rejected(pid, display)
    print('S28_ALTERED_BINARY_SIGNATURE_REJECTION=PASS', flush=True)

    after = subprocess.check_output(['awstart', '--status'], text=True, timeout=8)
    assert initial_status == after, 'running XFCE state unexpectedly changed'
    print('S28_USER_XFCE_AND_XWAYLAND_UNCHANGED=PASS', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('S28_VISUAL_GUARD_REGRESSION=FAIL ' + str(exc)[:250], flush=True)
        sys.exit(2)
