#!/usr/bin/env python3
"""Exercise S42's real Bash startup boundary with disposable private files.

No X server, terminal process, installed package or user file is changed.
Xfce 1.2.0's explicit-command/new-tab precedence is checked from its upstream
source separately; these fixtures test the configured default shell itself.
"""
import importlib.util
import json
import os
from pathlib import Path
import pty
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
BASH = shutil.which('bash')
assert BASH and Path(BASH).is_absolute()


def relocate(text, paths):
    return re.sub('|'.join(map(re.escape, paths)), lambda match: paths[match.group()], text)


def main():
    checks = []
    spec = importlib.util.spec_from_file_location('s42_profile', ROOT / 'termux/configure-xfce-profile.py')
    profile = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(profile)
    with tempfile.TemporaryDirectory(prefix='s42-terminal-policy-') as temporary:
        fixture = Path(temporary)
        native_home, prefix = fixture / 'native-home', fixture / 'prefix'
        project, state = native_home / 'alroot_wayland', native_home / '.alroot-wayland'
        config = state / 'xfce-config'
        for directory in (prefix / 'bin', project / 'termux', config):
            directory.mkdir(parents=True)
        (prefix / 'bin/bash').symlink_to(BASH)
        paths = {'/data/data/com.termux/files/home': str(native_home),
                 '/data/data/com.termux/files/usr': str(prefix)}
        for name in ('alroot-terminal-shell.sh', 'alroot-terminal.bashrc', 'graphics-defaults.sh'):
            target = project / 'termux' / name
            target.write_text(relocate((ROOT / 'termux' / name).read_text(), paths))
            target.chmod(0o755 if name.endswith('.sh') else 0o644)
        (state / 'display').write_text(':98\n')
        (state / 'Xauthority').write_bytes(b'fixture authority, never a real cookie')
        (native_home / '.shell_rc_content').write_text(
            'export DISPLAY=:0 XAUTHORITY=/wrong/user/auth\n'
            'export MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno\n'
            'export VK_DRIVER_FILES=/wrong/user/icd VK_ICD_FILENAMES=/wrong/user/icd\n'
            'export LD_LIBRARY_PATH=/old/private/mesa\n')
        (native_home / '.bashrc').write_text(
            'source "$HOME/.shell_rc_content"\n'
            'declare -x S42_USER_DECLARE=kept\n'
            'export S42_RC_COUNT=$(( ${S42_RC_COUNT:-0} + 1 ))\n'
            'alias s42_user_alias="printf alias-kept"\nPS1=\n')
        originals = {p: p.read_bytes() for p in (native_home / '.bashrc', native_home / '.shell_rc_content')}

        relative = Path('xfce4/xfconf/xfce-perchannel-xml/xfce4-terminal.xml')
        original_xml = ('<?xml version="1.0"?>\n<channel name="xfce4-terminal" version="1.0">'
                        '<property name="font-name" type="string" value="Droid Sans Mono 11"/>'
                        '<property name="background-darkness" type="double" value="0.83"/>'
                        '<property name="misc-default-geometry" type="string" value="88x24"/>'
                        '</channel>\n')
        global_xml = native_home / '.config' / relative
        global_xml.parent.mkdir(parents=True)
        global_xml.write_text(original_xml)
        profile.HOME, profile.PROJECT, profile.STATE, profile.CONFIG = native_home, project, state, config
        profile.install_s42_terminal()
        private_xml = config / relative
        root = ET.parse(private_xml).getroot()
        properties = {p.attrib['name']: dict(p.attrib) for p in root.findall('property')}
        expected = {p.attrib['name']: dict(p.attrib) for p in ET.fromstring(original_xml).findall('property')}
        assert {k: v for k, v in properties.items() if k not in ('run-custom-command', 'custom-command')} == expected
        assert properties['run-custom-command']['value'] == 'true'
        wrapper = project / 'termux/alroot-terminal-shell.sh'
        assert properties['custom-command']['value'] == str(wrapper)
        first = private_xml.read_bytes()
        profile.install_s42_terminal()
        assert private_xml.read_bytes() == first and global_xml.read_text() == original_xml
        checks.append('private_xfconf_changes_only_two_properties_and_is_idempotent')

        environment = {'PATH': os.environ['PATH'], 'HOME': str(native_home), 'PREFIX': str(prefix),
                       'SHELL': str(prefix / 'bin/bash'), 'XDG_CONFIG_HOME': str(config),
                       'DISPLAY': ':98.0', 'XAUTHORITY': str(state / 'Xauthority'),
                       'ALROOT_CLIENT_GRAPHICS': 'wrapper-zink', 'S42_FIXTURE_PYTHON': sys.executable,
                       'TERM': 'xterm', 'LD_LIBRARY_PATH': str(state / 'vte-private')}
        probe = '"$S42_FIXTURE_PYTHON" -c \'import os,json; print(json.dumps(dict(os.environ)))\'\n'

        def run(extra=None, args=(), command=probe):
            env = dict(environment)
            for key, value in (extra or {}).items():
                if value is None:
                    env.pop(key, None)
                else:
                    env[key] = value
            # A default shell detects interactivity from the terminal. Use
            # actual TTY input/stderr even when its JSON stdout is captured.
            master, slave = pty.openpty()
            try:
                os.write(master, (command + '\nexit\n').encode())
                result = subprocess.run([str(wrapper), *args], env=env, stdin=slave, stderr=slave,
                                        stdout=subprocess.PIPE, text=True, timeout=10)
            finally:
                os.close(master)
                os.close(slave)
            assert result.returncode == 0, (result.returncode, result.stdout)
            return [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]

        received = run()[0]
        assert received['DISPLAY'] == ':98.0' and received['XAUTHORITY'] == str(state / 'Xauthority')
        assert received['VK_DRIVER_FILES'] == received['VK_ICD_FILENAMES'] == str(prefix / 'share/vulkan/icd.d/wrapper_icd.aarch64.json')
        assert received['MESA_LOADER_DRIVER_OVERRIDE'] == received['GALLIUM_DRIVER'] == 'zink'
        assert received['LIBGL_KOPPER_DRI2'] == '1' and 'LD_LIBRARY_PATH' not in received
        assert received['S42_USER_DECLARE'] == 'kept' and received['S42_RC_COUNT'] == '1'
        assert received['SHELL'] == environment['SHELL']
        checks.append('real_bash_sources_user_includes_then_restores_authenticated_graphics_once')
        assert run()[0]['S42_RC_COUNT'] == '1'
        checks.append('each_fresh_default_shell_gets_the_same_scoped_startup')

        result = run(command=probe + 'VK_DRIVER_FILES=/test/turnip VK_ICD_FILENAMES=/test/turnip ' + probe + probe)
        assert result[1]['VK_DRIVER_FILES'] == result[1]['VK_ICD_FILENAMES'] == '/test/turnip'
        assert result[0]['VK_DRIVER_FILES'] == result[2]['VK_DRIVER_FILES']
        checks.append('later_per_command_driver_overrides_remain_effective')
        for changes in ({'ALROOT_CLIENT_GRAPHICS': None}, {'XDG_CONFIG_HOME': str(native_home / '.config')},
                        {'DISPLAY': ':0'}, {'XAUTHORITY': '/other/session/auth'}):
            received = run(changes)[0]
            assert received['DISPLAY'] == ':0' and received['GALLIUM_DRIVER'] == 'freedreno'
        checks.append('untagged_external_and_unauthenticated_sessions_are_not_repaired')

        alternate = prefix / 'bin/alternate-shell'
        alternate.write_text('#!' + sys.executable + '\nimport os,json\nprint(json.dumps(dict(os.environ)))\n')
        alternate.chmod(0o755)
        received = run({'SHELL': str(alternate)})[0]
        assert received['SHELL'] == str(alternate) and 'S42_RC_COUNT' not in received
        checks.append('non_bash_shell_is_executed_unchanged')
        received = run(args=('-c', 'export S42_EXPLICIT_COMMAND=honored; ' + probe))[0]
        assert received['S42_EXPLICIT_COMMAND'] == 'honored'
        checks.append('explicit_shell_arguments_do_not_force_interactive_hook')
        assert all(path.read_bytes() == expected for path, expected in originals.items())
        assert global_xml.read_text() == original_xml
        checks.append('global_shell_and_terminal_files_remain_unchanged')
    print(json.dumps({'status': 'PASS', 'count': len(checks), 'checks': checks}, indent=2))


if __name__ == '__main__':
    main()
