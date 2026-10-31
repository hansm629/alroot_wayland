#!/data/data/com.termux/files/usr/bin/python3
"""Read-only allowlist for Alroot's actually running private KGSL Xwayland.

S28 introduced a second private executable while S24/S26 and S23's live
tests still accepted only the S16 directory. Centralize the strict check:
PID/UID, exact /proc/PID/exe path, executable SHA256, argv[0] and DISPLAY.
Never accept arbitrary project binaries or the installed Termux-X11 server.
"""

import hashlib
import os
from pathlib import Path
import re

ROOT = Path('/data/data/com.termux/files/home/alroot_wayland')
AUTHORITY = ROOT.parent / '.alroot-wayland/Xauthority'
PRIVATE_XWAYLANDS = {
    ROOT / '.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland': (
        'S16_S27_KGSL_BASELINE',
        'f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701',
    ),
    ROOT / '.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland': (
        'S28_KGSL_RESIZE_PRESERVE',
        'cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7',
    ),
    ROOT / '.source/gpu-stage1/s29-native-resize/runtime/Xwayland': (
        'S29_KGSL_NATIVE_RESIZE',
        '3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c',
    ),
    ROOT / '.source/gpu-stage1/s30-min-extent/runtime/Xwayland': (
        'S30_KGSL_STABLE_FRAME_TINY_AHB_FD0',
        '4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31',
    ),
    ROOT / '.source/gpu-stage1/s31-resize-continuity/runtime/Xwayland': (
        'S31_KGSL_ADAPTIVE_RESIZE_PRESENT_EXTENT',
        '73acf8472faf1e5b91b5f4e998c32ed3322d30fc68e7b0683eef62b6a9c1fe90',
    ),
    ROOT / '.source/gpu-stage1/s32-no-blank-present/runtime/Xwayland': (
        'S32_KGSL_NONBLANK_REAL_PRESENT',
        'b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455',
    ),
    ROOT / '.source/gpu-stage1/s36-ahb-output/runtime/Xwayland': (
        'S36_KGSL_ROOTFUL_AHB_DIRECT_VKIMAGE',
        'df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5',
    ),
    ROOT / '.source/gpu-stage1/s37-thin-ahb/runtime/Xwayland': (
        'S37_KGSL_THIN_HEIGHT_DRI3_AHB_FD0',
        '193f43e03f0e754c39cfb73e04ac1e0032f8f632e6d06b5d1b675f67c834a594',
    ),
    ROOT / '.source/gpu-stage1/s38-frame-continuity/runtime/Xwayland': (
        'S38_KGSL_FRAME_LIFETIME_LATEST_PRESENT',
        '3f27886fae2bde560d34ca53dedacf4cec09fd9dbcddc591efafc4ec9bc994c2',
    ),
}


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def validate_private_kgsl_xwayland(pid: int, display: str) -> str:
    """Return the exact allowlisted build label or raise RuntimeError.

    The authenticated display comes from awstart status, not the caller's
    DISPLAY. We never discover, attach to, or modify other Xservers.
    """
    if not isinstance(pid, int) or pid <= 0 or not re.fullmatch(r':[0-9]{2,3}', display):
        raise RuntimeError('refusing invalid PID or private X11 display')
    if display == ':0':
        raise RuntimeError('refusing Termux-X11 display :0')

    proc = Path('/proc') / str(pid)
    try:
        if proc.stat().st_uid != os.getuid():
            raise RuntimeError('running Xwayland is not owned by native Termux UID')
        args = (proc / 'cmdline').read_bytes().split(b'\x00')
        argv0 = os.fsdecode(args[0])
        argv1 = os.fsdecode(args[1]) if len(args) > 1 else ''
        executable = (proc / 'exe').resolve(strict=True)
    except (OSError, IndexError, ValueError) as exc:
        raise RuntimeError('cannot inspect owned private Xwayland executable') from exc

    if argv1 != display:
        raise RuntimeError('private Xwayland PID / actual DISPLAY mismatch')

    # Match the authenticated, accelerated Alroot profile as well as the
    # binary. Do not accept a developer test server started with another
    # Xauthority or generic Xwayland that merely reuses the same executable.
    if b'-auth' not in args or args.index(b'-auth') + 1 >= len(args) or \
            os.fsdecode(args[args.index(b'-auth') + 1]) != str(AUTHORITY):
        raise RuntimeError('private Xwayland is not using Alroot Xauthority')
    if b'-glamor' not in args or args.index(b'-glamor') + 1 >= len(args) or \
            args[args.index(b'-glamor') + 1] != b'es':
        raise RuntimeError('private KGSL Xwayland GLAMOR mode is not es')

    for trusted, (label, sha256) in PRIVATE_XWAYLANDS.items():
        # Exact path and real executable check: a crafted argv merely
        # containing the old directory name must not pass.
        if argv0 != str(trusted) or executable != trusted.resolve(strict=True):
            continue
        if trusted.stat().st_uid != os.getuid() or _file_sha256(trusted) != sha256:
            raise RuntimeError('private Xwayland signature changed; refusing unknown binary')
        return label

    raise RuntimeError('unexpected Xwayland executable; refusing system Termux-X11')
