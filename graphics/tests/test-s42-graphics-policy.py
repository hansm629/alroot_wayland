#!/usr/bin/env python3
"""Exercise S42 renderer scoping and fail-before-stop with disposable fixtures.

No installed packages, Android app, X server or user desktop is touched.
The package preflight fixture uses an explicit older server selection, whose
existing KGSL guard is independently covered by test-s40-kgsl-profiles.py.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
POLICY = ROOT / 'termux/graphics-defaults.sh'
BASH = shutil.which('bash')
assert BASH and Path(BASH).is_absolute(), 'an absolute bash executable is required'
SHELL_SHEBANG = '#!' + BASH + '\n'


def fixture_shell(text, project, prefix):
    paths = {'/data/data/com.termux/files/home': str(project.parent),
             '/data/data/com.termux/files/usr': str(prefix)}
    # Native Termux fixtures live under the original prefix's tmp directory;
    # replace source paths once so inserted fixture paths cannot be rewritten.
    text = re.sub('|'.join(map(re.escape, paths)), lambda match: paths[match.group()], text)
    return SHELL_SHEBANG + text.partition('\n')[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def shell(code, extra=None, policy=POLICY):
    env = {'PATH': os.environ['PATH'], **(extra or {})}
    return subprocess.run([BASH, '-eu', '-c', 'source "$1"; ' + code, 'fixture', str(policy)],
                          env=env, text=True, capture_output=True, timeout=10)


def exported(code, extra=None):
    result = shell(code + '; env', extra)
    assert result.returncode == 0, result.stderr
    return dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)


def require_failure(result, message):
    assert result.returncode != 0 and message in result.stderr, (result.returncode, result.stderr)


def main():
    checks = []
    server = exported('alroot_s42_select_server /nonexistent/alroot-s42 xfce-shm-direct')
    assert server['GALLIUM_DRIVER'] == 'freedreno' and server['MESA_LOADER_DRIVER_OVERRIDE'] == 'kgsl'
    assert server['ALROOT_XWAYLAND_BINARY'].endswith('/s39-live-resize/runtime/Xwayland')
    assert server['S39_KGSL_RESIZE_ORIGIN'] == server['S38_KGSL_FRAME_LIFETIME'] == '1'
    checks.append('sanitized_activity_gets_default_s39_server')
    client = exported('alroot_s42_select_server /nonexistent/alroot-s42 xfce-shm-direct; '
                      'alroot_s42_apply_client /canonical/prefix',
                      {'LD_LIBRARY_PATH': '/old/private', 'LIBGL_ALWAYS_SOFTWARE': '1',
                       'TU_DEBUG': 'noconform', 'VK_DRIVER_FILES': '/old/icd',
                       'S44_WRAPPER_PROFILE': '1', 'S44_WSI_PROFILE': '1', 'S44_ZINK_SUBMIT_STATS': '1',
                       'S44_WRAPPER_SUBMIT_STACK': '1', 'S44_ZINK_COMPACT_SUBMITS': '1'})
    assert client['GALLIUM_DRIVER'] == client['MESA_LOADER_DRIVER_OVERRIDE'] == 'zink'
    assert client['VK_DRIVER_FILES'] == client['VK_ICD_FILENAMES'] == '/canonical/prefix/share/vulkan/icd.d/wrapper_icd.aarch64.json'
    assert client['S24_WRAPPER_XCB_AHB_PAD_HEIGHT'] == client['S26_WRAPPER_XCB_RETRY_TRANSIENT'] == client['LIBGL_KOPPER_DRI2'] == '1'
    for name in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'LIBGL_ALWAYS_SOFTWARE', 'TU_DEBUG',
                 'ALROOT_XWAYLAND_BINARY', 'S39_KGSL_RESIZE_ORIGIN', 'XWAYLAND_FORCE_KGSL_SURFACELESS',
                 'S44_WRAPPER_PROFILE', 'S44_WSI_PROFILE', 'S44_ZINK_SUBMIT_STATS',
                 'S44_WRAPPER_SUBMIT_STACK', 'S44_ZINK_COMPACT_SUBMITS'):
        assert name not in client, name
    checks.append('desktop_uses_installed_wrapper_zink_without_server_or_private_ld_env')
    parent = exported('alroot_s42_select_server /nonexistent/alroot-s42 xfce-shm-direct; '
                      '(alroot_s42_apply_client /canonical/prefix)')
    assert parent['GALLIUM_DRIVER'] == 'freedreno' and 'VK_DRIVER_FILES' not in parent
    checks.append('client_subshell_does_not_change_server')
    explicit = exported('alroot_s42_select_server /nonexistent/alroot-s42 xfce-shm-direct',
                        {'ALROOT_XWAYLAND_BINARY': '/explicit/s38/Xwayland', 'S38_KGSL_FRAME_LIFETIME': '1'})
    assert explicit['ALROOT_XWAYLAND_BINARY'] == '/explicit/s38/Xwayland' and 'S39_KGSL_RESIZE_ORIGIN' not in explicit
    checks.append('explicit_older_server_is_preserved')
    nested = exported('alroot_s42_select_server /nonexistent/alroot-s42 xfce-labwc')
    assert 'ALROOT_XWAYLAND_BINARY' not in nested
    assert shell('alroot_s42_preflight /missing /missing xfce-labwc').returncode == 0
    checks.append('explicit_nested_profile_is_preserved')

    with tempfile.TemporaryDirectory(prefix='s42-graphics-policy-') as directory:
        fixture = Path(directory)
        prefix = fixture / 'prefix'
        project = fixture / 'native-home/alroot_wayland'
        for path in (prefix / 'bin', prefix / 'tmp', prefix / 'lib/dri', project / 'termux'):
            path.mkdir(parents=True)
        (prefix / 'bin/python3').symlink_to(sys.executable)
        query = prefix / 'bin/dpkg-query'
        query.write_text(SHELL_SHEBANG + 'case "$3" in\n'
                         'vulkan-wrapper-android) printf "install ok installed\\t26.3.0+alroot.s42.1\\taarch64";;\n'
                         'mesa) printf "install ok installed\\t27.0.0~devel+alroot.s42.1\\taarch64";;\n'
                         '*) exit 1;; esac\n')
        query.chmod(0o755)
        library = prefix / 'lib/libvulkan_wrapper.so'
        library.write_bytes(b'isolated wrapper fixture')
        icd = prefix / 'share/vulkan/icd.d/wrapper_icd.aarch64.json'
        icd.parent.mkdir(parents=True)
        icd.write_text(json.dumps({'ICD': {'library_path': str(library)}}))
        wrapper_meta = prefix / 'share/doc/vulkan-wrapper-android/alroot-build.json'
        wrapper_meta.parent.mkdir(parents=True)
        wrapper = {'package_version': '26.3.0+alroot.s42.1', 'library_sha256': digest(library), 'icd_sha256': digest(icd)}
        wrapper_meta.write_text(json.dumps(wrapper))
        files = {}
        for relative in ('lib/libGLX_mesa.so', 'lib/libEGL_mesa.so', 'lib/libgbm.so',
                         'lib/libgallium-27.0.0-devel.so', 'lib/dri/zink_dri.so', 'lib/dri/kgsl_dri.so'):
            path = prefix / relative
            path.write_bytes(('isolated fixture ' + relative).encode())
            files[relative] = digest(path)
        alias = prefix / 'lib/libGLX_mesa.so.0'
        alias.symlink_to('libGLX_mesa.so')
        mesa_meta = prefix / 'share/doc/mesa/alroot-s42-build.json'
        mesa_meta.parent.mkdir(parents=True)
        mesa = {'package_version': '27.0.0~devel+alroot.s42.1', 'mesa_version': '27.0.0-devel',
                'source_commit': '6f90e92dfdacc4759d8e9ccb328805d0f0b4439b',
                'runtime_files': files, 'runtime_symlinks': {'lib/libGLX_mesa.so.0': 'libGLX_mesa.so'}}
        mesa_meta.write_text(json.dumps(mesa))
        code = 'alroot_s42_preflight "$S42_PROJECT" "$S42_PREFIX" xfce-shm-direct'
        env = {'S42_PROJECT': str(project), 'S42_PREFIX': str(prefix),
               'ALROOT_XWAYLAND_BINARY': '/explicit/s38/Xwayland'}
        result = shell(code, env)
        assert result.returncode == 0, result.stderr
        checks.append('matching_installed_package_manifest_accepts')
        query_s42 = query.read_text()
        query.write_text(query_s42.replace('+alroot.s42.1', '+alroot.s44.1'))
        s44_policy = {'activation': 'compiled',
                      'runtime_defaults': {'S44_WRAPPER_SUBMIT_STACK': '1', 'S44_ZINK_COMPACT_SUBMITS': '1'},
                      'profiling': {'S44_WRAPPER_PROFILE': False, 'S44_WSI_PROFILE': False,
                                    'S44_ZINK_SUBMIT_STATS': False}}
        for document, flag in ((wrapper, 'S44_WRAPPER_SUBMIT_STACK'), (mesa, 'S44_ZINK_COMPACT_SUBMITS')):
            document['runtime_defaults'] = {flag: '1'}
            document['s44'] = {'default_policy': json.loads(json.dumps(s44_policy))}
        wrapper['package_version'] = '26.3.0+alroot.s44.1'; wrapper_meta.write_text(json.dumps(wrapper))
        mesa['package_version'] = '27.0.0~devel+alroot.s44.1'; mesa_meta.write_text(json.dumps(mesa))
        result = shell(code, env)
        assert result.returncode == 0, result.stderr
        checks.append('coherent_s44_pair_with_matching_provenance_accepts')
        s44_env = {**env, 'S44_WRAPPER_SUBMIT_STACK': '0', 'S44_ZINK_COMPACT_SUBMITS': '0',
                   'S44_WRAPPER_PROFILE': '1', 'S44_WSI_PROFILE': '1', 'S44_ZINK_SUBMIT_STATS': '1'}
        result = shell(code + '; alroot_s42_apply_client "$S42_PREFIX"; env', s44_env)
        assert result.returncode == 0, result.stderr
        received = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
        assert received['VK_DRIVER_FILES'] == str(icd) and received['GALLIUM_DRIVER'] == 'zink'
        assert not any(name in received for name in s44_env if name.startswith('S44_'))
        checks.append('compiled_s44_defaults_need_no_enable_env_and_inherited_profiles_are_cleared')
        result = shell('alroot_s42_apply_client "$S42_PREFIX"; '
                       'S44_WRAPPER_SUBMIT_STACK=0 S44_ZINK_COMPACT_SUBMITS=0 env', env)
        assert result.returncode == 0, result.stderr
        received = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
        assert received['S44_WRAPPER_SUBMIT_STACK'] == received['S44_ZINK_COMPACT_SUBMITS'] == '0'
        checks.append('later_per_command_s44_kill_switches_remain_available')
        for document, path, flag in ((wrapper, wrapper_meta, 'S44_WRAPPER_SUBMIT_STACK'),
                                    (mesa, mesa_meta, 'S44_ZINK_COMPACT_SUBMITS')):
            document['runtime_defaults'][flag] = '0'; path.write_text(json.dumps(document))
            require_failure(shell(code, env), 'runtime defaults mismatch')
            document['runtime_defaults'][flag] = '1'
            document['s44']['default_policy']['activation'] = 'scoped-environment'
            path.write_text(json.dumps(document))
            require_failure(shell(code, env), 'compiled default policy mismatch')
            document['s44']['default_policy']['activation'] = 'compiled'
            document['s44']['default_policy']['profiling']['S44_WSI_PROFILE'] = True
            path.write_text(json.dumps(document))
            require_failure(shell(code, env), 'compiled default policy mismatch')
            document['s44']['default_policy']['profiling']['S44_WSI_PROFILE'] = False
            path.write_text(json.dumps(document))
        checks.append('both_owner_default_activation_and_profile_tampering_reject')
        for wrapper_version, mesa_version in (('s44', 's42'), ('s42', 's44')):
            mixed = query_s42.replace('26.3.0+alroot.s42.1', '26.3.0+alroot.' + wrapper_version + '.1')
            mixed = mixed.replace('27.0.0~devel+alroot.s42.1', '27.0.0~devel+alroot.' + mesa_version + '.1')
            query.write_text(mixed)
            require_failure(shell(code, env), 'coherent validated S42 or S44')
        checks.append('both_mixed_s42_s44_pairs_reject')
        query.write_text(query_s42)
        for document in (wrapper, mesa):
            document.pop('runtime_defaults'); document.pop('s44')
        wrapper['package_version'] = '26.3.0+alroot.s42.1'; wrapper_meta.write_text(json.dumps(wrapper))
        mesa['package_version'] = '27.0.0~devel+alroot.s42.1'; mesa_meta.write_text(json.dumps(mesa))
        result = shell(code + '; alroot_s42_apply_client "$S42_PREFIX"; env', s44_env)
        assert result.returncode == 0, result.stderr
        received = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
        assert not any(name in received for name in s44_env if name.startswith('S44_'))
        checks.append('coherent_s42_rollback_preserves_clean_environment')
        library.write_bytes(b'wrong wrapper')
        require_failure(shell(code, env), 'wrapper hash mismatch')
        library.write_bytes(b'isolated wrapper fixture')
        checks.append('wrong_wrapper_rejected')
        old_icd = icd.read_bytes()
        icd.write_text(json.dumps({'ICD': {'library_path': '/private/unknown/libvulkan_wrapper.so'}}))
        wrapper['icd_sha256'] = digest(icd); wrapper_meta.write_text(json.dumps(wrapper))
        require_failure(shell(code, env), 'canonical Termux wrapper')
        icd.write_bytes(old_icd); wrapper['icd_sha256'] = digest(icd); wrapper_meta.write_text(json.dumps(wrapper))
        checks.append('noncanonical_icd_rejected_even_with_matching_hash')
        alias.unlink(); alias.symlink_to('libEGL_mesa.so')
        require_failure(shell(code, env), 'symlink mismatch')
        alias.unlink(); alias.symlink_to('libGLX_mesa.so')
        checks.append('changed_mesa_link_rejected')
        runtime = prefix / 'lib/libgallium-27.0.0-devel.so'
        original = runtime.read_bytes(); runtime.write_bytes(b'wrong Gallium')
        require_failure(shell(code, env), 'Mesa runtime hash mismatch')
        runtime.write_bytes(original)
        checks.append('wrong_mesa_runtime_rejected')
        mesa['source_commit'] = 'wrong-source'; mesa_meta.write_text(json.dumps(mesa))
        require_failure(shell(code, env), 'Mesa 27 provenance mismatch')
        checks.append('wrong_mesa_source_rejected')

        # Exercise the actual awstart control flow with an invalid package.
        # If --restart reaches its stop branch, this fixture records it.
        shutil.copyfile(POLICY, project / 'termux/graphics-defaults.sh')
        stop_record = fixture / 'cli-actions'
        cli = project / 'termux/alroot-wayland'
        cli.write_text(SHELL_SHEBANG + 'printf "%s\\n" "$1" >> "$S42_STOP_RECORD"\n')
        cli.chmod(0o755)
        awstart = project / 'termux/awstart'
        awstart.write_text(fixture_shell((ROOT / 'termux/awstart').read_text(), project, prefix))
        query.write_text(SHELL_SHEBANG + 'printf "install ok installed\\t26.3.0\\taarch64"\n')
        result = subprocess.run([BASH, str(awstart), '--restart', '--no-topapp'],
            env={'PATH': os.environ['PATH'], 'S42_STOP_RECORD': str(stop_record)},
            capture_output=True, text=True, timeout=10)
        require_failure(result, 'existing desktop was not stopped')
        assert not stop_record.exists(), 'invalid package preflight reached session CLI'
        checks.append('invalid_package_restart_rejected_before_stop')
        query.write_text(query_s42.replace('26.3.0+alroot.s42.1', '26.3.0+alroot.s44.1'))
        result = subprocess.run([BASH, str(awstart), '--restart', '--no-topapp'],
            env={'PATH': os.environ['PATH'], 'S42_STOP_RECORD': str(stop_record)},
            capture_output=True, text=True, timeout=10)
        require_failure(result, 'coherent validated S42 or S44')
        assert not stop_record.exists(), 'mixed S42/S44 preflight reached session CLI'
        checks.append('mixed_pair_restart_rejected_before_stop')

        state = project.parent / '.alroot-wayland'
        state.mkdir()
        for name, content in (('display', ':98\n'), ('ime-active', ':98\n'), ('Xauthority', 'fixture')):
            (state / name).write_text(content)
        guest_record = fixture / 'guest-env.json'
        guest = prefix / 'bin/pdrun'
        guest.write_text('#!' + sys.executable + '\nimport os,json\nfrom pathlib import Path\n'
                         'Path(os.environ["S42_GUEST_RECORD"]).write_text(json.dumps(dict(os.environ)))\n')
        guest.chmod(0o755)
        bridge = project / 'termux/aw-proot'
        bridge.write_text(fixture_shell((ROOT / 'termux/aw-proot').read_text(), project, prefix))
        guest_env = {'PATH': os.environ['PATH'], 'S42_GUEST_RECORD': str(guest_record),
                     'ALROOT_CLIENT_GRAPHICS': 'wrapper-zink', 'VK_DRIVER_FILES': str(icd),
                     'VK_ICD_FILENAMES': str(icd), 'MESA_LOADER_DRIVER_OVERRIDE': 'zink',
                     'GALLIUM_DRIVER': 'zink',
                     'LIBGL_KOPPER_DRI2': '1', 'S24_WRAPPER_XCB_AHB_PAD_HEIGHT': '1',
                     'S26_WRAPPER_XCB_RETRY_TRANSIENT': '1'}
        result = subprocess.run([BASH, str(bridge), 'pdrun', '/bin/true'], env=guest_env,
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        received = json.loads(guest_record.read_text())
        for name in guest_env.keys() - {'PATH', 'S42_GUEST_RECORD'}:
            assert name not in received, name
        assert received['DISPLAY'] == ':98'
        checks.append('bionic_driver_selection_does_not_leak_to_glibc_launcher')
        guest_env['VK_ICD_FILENAMES'] = '/guest/share/vulkan/icd.d/test.json'
        guest_env['LIBGL_ALWAYS_SOFTWARE'] = '1'
        result = subprocess.run([BASH, str(bridge), 'pdrun', '/bin/true'], env=guest_env,
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        received = json.loads(guest_record.read_text())
        assert 'VK_DRIVER_FILES' not in received and received['VK_ICD_FILENAMES'] == '/guest/share/vulkan/icd.d/test.json'
        assert received['LIBGL_ALWAYS_SOFTWARE'] == '1'
        checks.append('explicit_guest_icd_override_is_preserved')
        guest_env.pop('ALROOT_CLIENT_GRAPHICS')
        result = subprocess.run([BASH, str(bridge), 'pdrun', '/bin/true'], env=guest_env,
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        received = json.loads(guest_record.read_text())
        assert received['VK_DRIVER_FILES'] == str(icd)
        checks.append('unrelated_explicit_guest_environment_is_preserved')
    print(json.dumps({'status': 'PASS', 'count': len(checks), 'checks': checks}, indent=2))


if __name__ == '__main__':
    main()
