#!/data/data/com.termux/files/usr/bin/bash
# S42/S44 S26U development baselines. Sourcing this file has no side effects.
# The installed Bionic client drivers and Xwayland's renderer are scoped
# separately. Both native-shell and Activity starts resolve this same policy.

alroot_s42_select_server() {
    local project="$1" profile="$2"
    [[ "$profile" == xfce-shm-direct ]] || return 0
    # Explicit stage starters keep their selected server and existing guard.
    [[ -z "${ALROOT_XWAYLAND_BINARY:-}" ]] || return 0
    # The existing S25 Activity bridge validates and consumes this marker
    # inside start_session, then supplies its explicit server selection.
    local marker="$project/../.alroot-wayland/control/s25-kgsl-topapp-once"
    [[ ! -e "$marker" && ! -L "$marker" ]] || return 0
    unset LD_LIBRARY_PATH LD_PRELOAD LIBGL_ALWAYS_SOFTWARE LIBGL_DRIVERS_PATH
    unset VK_DRIVER_FILES VK_ICD_FILENAMES LIBGL_KOPPER_DRI2 ALROOT_CLIENT_GRAPHICS
    unset S24_WRAPPER_XCB_AHB_PAD_HEIGHT S26_WRAPPER_XCB_RETRY_TRANSIENT
    unset MESA_GL_VERSION_OVERRIDE MESA_GLES_VERSION_OVERRIDE MESA_NO_ERROR
    unset S44_WRAPPER_PROFILE S44_WSI_PROFILE S44_ZINK_SUBMIT_STATS
    unset S44_WRAPPER_SUBMIT_STACK S44_ZINK_COMPACT_SUBMITS
    unset S28_KGSL_RESIZE_PRESERVE S29_KGSL_NATIVE_RESIZE S30_KGSL_STABLE_FRAME
    unset S31_KGSL_RESIZE_CONTENT_POLICY S30_DIAG_TINY_AHB S37_DIAG_THIN_AHB
    unset S30_TRACE_STABLE S31_RESIZE_DIAG S32_KGSL_SKIP_REPLAY S39_RESIZE_WORK_TRACE
    export ALROOT_XWAYLAND_BINARY="$project/.source/gpu-stage1/s39-live-resize/runtime/Xwayland"
    export ALROOT_XWAYLAND_GLAMOR=es ALROOT_XWAYLAND_DUAL_ABI_FENCE=1
    export XWAYLAND_FORCE_KGSL_SURFACELESS=1 FD_KGSL_ENABLE_DMABUF=1
    export MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno
    export S30_KGSL_TINY_AHB_FD0=1 S32_KGSL_NO_BLANK_PRESENT=1
    export S36_KGSL_ROOT_AHB_OUTPUT=1 S37_KGSL_THIN_AHB_FD0=1
    export S38_KGSL_FRAME_LIFETIME=1 S39_KGSL_RESIZE_ORIGIN=1
    export S39_KGSL_KEEP_UNRELATED_ROOT_POOL=1
}

alroot_s42_preflight() {
    local project="$1" prefix="$2" profile="$3"
    [[ "$profile" == xfce-shm-direct ]] || return 0
    "$prefix/bin/python3" - "$project" "$prefix" <<'PY'
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

project, prefix = map(Path, sys.argv[1:])

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(data)
    return result.hexdigest()

def installed(package):
    result = subprocess.run([str(prefix / 'bin/dpkg-query'), '-W',
        '-f=${Status}\t${Version}\t${Architecture}', package],
        capture_output=True, text=True, timeout=10)
    fields = result.stdout.split('\t')
    require(result.returncode == 0 and len(fields) == 3 and
            fields[0] == 'install ok installed' and fields[2] == 'aarch64',
            'install a validated native ' + package + ' package before starting this profile')
    return fields[1]

def prefix_path(relative):
    path = Path(relative)
    require(not path.is_absolute() and '..' not in path.parts,
            'invalid installed Mesa manifest path')
    full = prefix / path
    require(full.resolve(strict=True).is_relative_to(prefix.resolve()),
            'installed Mesa runtime points outside the Termux prefix: ' + relative)
    return full

try:
    wrapper_version = installed('vulkan-wrapper-android')
    mesa_version = installed('mesa')
    accepted_pairs = {
        ('26.3.0+alroot.s42.1', '27.0.0~devel+alroot.s42.1'),
        ('26.3.0+alroot.s44.1', '27.0.0~devel+alroot.s44.1'),
    }
    require((wrapper_version, mesa_version) in accepted_pairs,
            'install a coherent validated S42 or S44 Wrapper/Mesa package pair before starting this profile')
    wrapper = json.loads((prefix / 'share/doc/vulkan-wrapper-android/alroot-build.json').read_text())
    require(wrapper.get('package_version') == wrapper_version, 'installed wrapper provenance version mismatch')
    library = prefix / 'lib/libvulkan_wrapper.so'
    icd = prefix / 'share/vulkan/icd.d/wrapper_icd.aarch64.json'
    require(digest(library) == wrapper.get('library_sha256'), 'installed wrapper hash mismatch')
    require(digest(icd) == wrapper.get('icd_sha256'), 'installed ICD hash mismatch')
    require(json.loads(icd.read_text()).get('ICD', {}).get('library_path') == str(library),
            'installed ICD does not select the canonical Termux wrapper')
    mesa = json.loads((prefix / 'share/doc/mesa/alroot-s42-build.json').read_text())
    require(mesa.get('package_version') == mesa_version and mesa.get('mesa_version') == '27.0.0-devel'
            and mesa.get('source_commit') == '6f90e92dfdacc4759d8e9ccb328805d0f0b4439b',
            'installed Mesa 27 provenance mismatch')
    if wrapper_version == '26.3.0+alroot.s44.1':
        defaults = {'S44_WRAPPER_SUBMIT_STACK': '1', 'S44_ZINK_COMPACT_SUBMITS': '1'}
        profiles = {'S44_WRAPPER_PROFILE', 'S44_WSI_PROFILE', 'S44_ZINK_SUBMIT_STATS'}
        for owner, document, flag in (
                ('Wrapper', wrapper, 'S44_WRAPPER_SUBMIT_STACK'),
                ('Mesa', mesa, 'S44_ZINK_COMPACT_SUBMITS')):
            require(document.get('runtime_defaults') == {flag: '1'},
                    'installed S44 ' + owner + ' runtime defaults mismatch')
            details = document.get('s44')
            policy = details.get('default_policy') if isinstance(details, dict) else None
            profiling = policy.get('profiling') if isinstance(policy, dict) else None
            require(isinstance(policy, dict) and policy.get('activation') == 'compiled'
                    and policy.get('runtime_defaults') == defaults
                    and isinstance(profiling, dict) and set(profiling) == profiles
                    and all(value is False for value in profiling.values()),
                    'installed S44 ' + owner + ' compiled default policy mismatch')
    files = mesa.get('runtime_files')
    links = mesa.get('runtime_symlinks')
    require(isinstance(files, dict) and files and isinstance(links, dict),
            'installed Mesa runtime manifest is incomplete')
    for relative, expected in files.items():
        path = prefix_path(relative)
        require(not path.is_symlink() and path.is_file() and digest(path) == expected,
                'installed Mesa runtime hash mismatch: ' + relative)
    for relative, target in links.items():
        path = prefix_path(relative)
        require(path.is_symlink() and os.readlink(path) == target,
                'installed Mesa runtime symlink mismatch: ' + relative)
    for relative in ('lib/libGLX_mesa.so', 'lib/libEGL_mesa.so', 'lib/libgbm.so',
                     'lib/libgallium-27.0.0-devel.so', 'lib/dri/zink_dri.so', 'lib/dri/kgsl_dri.so'):
        path = prefix_path(relative)
        require(path.is_file() and path.stat().st_size > 0,
                'required installed Mesa runtime is missing: ' + relative)
    server = Path(os.environ.get('ALROOT_XWAYLAND_BINARY', str(project / '.source/gpu-stage1/s39-live-resize/runtime/Xwayland')))
    canonical = project / '.source/gpu-stage1/s39-live-resize/runtime/Xwayland'
    if server.resolve() == canonical.resolve():
        model = subprocess.check_output(['/system/bin/getprop', 'ro.product.model'], text=True, timeout=4).strip()
        require(model.startswith('SM-S948'), 'S42 default Xwayland profile is validated only on S26 Ultra')
        require(server.is_file() and not server.is_symlink() and os.access(server, os.X_OK)
                and digest(server) == 'c5f6c9a512f84e31f60ec52a9f10e689e0746f831049e57a4223ade2750f3899',
                'validated S39 default Xwayland is missing or changed')
    print('[Alroot graphics] Installed Vulkan Wrapper + Mesa 27 Zink preflight passed')
except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
    print('[Alroot graphics] ' + str(error), file=sys.stderr)
    raise SystemExit(1)
PY
}

alroot_s42_apply_client() {
    local prefix="$1"
    # Installed libraries use their normal Termux lookup paths. A blanket
    # LD_LIBRARY_PATH=$PREFIX/lib can break Android vendor Vulkan loading.
    unset LD_LIBRARY_PATH LD_PRELOAD LIBGL_ALWAYS_SOFTWARE LIBGL_DRIVERS_PATH
    unset MESA_GL_VERSION_OVERRIDE MESA_GLES_VERSION_OVERRIDE MESA_NO_ERROR
    unset S44_WRAPPER_PROFILE S44_WSI_PROFILE S44_ZINK_SUBMIT_STATS
    unset S44_WRAPPER_SUBMIT_STACK S44_ZINK_COMPACT_SUBMITS
    unset TU_DEBUG MESA_VK_WSI_DEBUG MESA_VK_WSI_PRESENT_MODE WRAPPER_BLIT WRAPPER_MAX_IMAGE_COUNT
    unset ALROOT_XWAYLAND_BINARY ALROOT_XWAYLAND_GLAMOR ALROOT_XWAYLAND_DUAL_ABI_FENCE
    unset XWAYLAND_FORCE_KGSL_SURFACELESS FD_KGSL_ENABLE_DMABUF
    unset S28_KGSL_RESIZE_PRESERVE S29_KGSL_NATIVE_RESIZE S30_KGSL_STABLE_FRAME
    unset S30_KGSL_TINY_AHB_FD0 S31_KGSL_RESIZE_CONTENT_POLICY S32_KGSL_NO_BLANK_PRESENT
    unset S36_KGSL_ROOT_AHB_OUTPUT S37_KGSL_THIN_AHB_FD0 S38_KGSL_FRAME_LIFETIME
    unset S39_KGSL_RESIZE_ORIGIN S39_KGSL_KEEP_UNRELATED_ROOT_POOL
    unset S26_WRAPPER_TEST_FAIL_PIXMAP_ONCE S39_WRAPPER_XCB_CHECK_BEFORE_ACK
    unset S39_WSI_TRACE S39_VULKAN_TRACE S39_VULKAN_TRACE_FILE S40_VKCUBE_TRACE S40_GLXGEARS_TRACE
    # S44 optimizations are compiled defaults. No enabling environment is
    # needed, including for a new app in an already-running XFCE session.
    # A later per-command S44_* override remains available for comparisons.
    export VK_DRIVER_FILES="$prefix/share/vulkan/icd.d/wrapper_icd.aarch64.json"
    export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
    export MESA_LOADER_DRIVER_OVERRIDE=zink GALLIUM_DRIVER=zink LIBGL_KOPPER_DRI2=1
    export S24_WRAPPER_XCB_AHB_PAD_HEIGHT=1 S26_WRAPPER_XCB_RETRY_TRANSIENT=1
    export ALROOT_CLIENT_GRAPHICS=wrapper-zink
}

alroot_s42_client_status() {
    local pid="$1"
    [[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/environ" ]] || return 0
    local entry renderer=unknown icd=unknown
    { while IFS= read -r -d '' entry; do
        case "$entry" in
            ALROOT_CLIENT_GRAPHICS=*) renderer="${entry#*=}" ;;
            VK_DRIVER_FILES=*) icd="${entry#*=}" ;;
        esac
    done < "/proc/$pid/environ"; } 2>/dev/null || return 0
    printf 'client_graphics: %s\n' "$renderer"
    [[ "$renderer" != wrapper-zink ]] || printf 'client_vulkan_icd: %s\n' "$icd"
}
