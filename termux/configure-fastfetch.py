#!/data/data/com.termux/files/usr/bin/python3
"""Build an Alroot-only fastfetch view, preserving the user's Debian modules.

The kernel's XRandR output name is XWAYLAND0; renaming it would be a fake
hardware identifier. Hide that implementation detail in human-readable
fastfetch output ONLY in a project-scoped config, preserving real refresh.
"""
import json
import os
import subprocess
from pathlib import Path

HOME = Path("/data/data/com.termux/files/home")
STATE = HOME / ".alroot-wayland"
PREFIX = Path("/data/data/com.termux/files/usr")
target = STATE / "fastfetch/config.jsonc"

original = None
command = [str(PREFIX / "bin/aw-pdrun"), "/usr/bin/cat",
           "/home/hsm/.config/fastfetch/config.jsonc"]
try:
    result = subprocess.run(command, capture_output=True, text=True,
                            timeout=12, check=True)
    original = json.loads(result.stdout)
except (OSError, subprocess.SubprocessError, json.JSONDecodeError):
    source = HOME / ".config/fastfetch/config.jsonc"
    if source.is_file():
        try:
            original = json.loads(source.read_text(encoding="utf-8"))
        except ValueError:
            pass
if not isinstance(original, dict):
    original = {"modules": ["title", "separator", "os", "host", "kernel",
                            "packages", "shell", "display", "de", "wm",
                            "terminal", "cpu", "gpu", "memory", "disk"]}
modules = original.get("modules")
if not isinstance(modules, list):
    modules = ["title", "separator", "os", "display", "cpu", "gpu", "memory"]
new_modules = []
for entry in modules:
    if isinstance(entry, str) and entry.casefold() == "display":
        new_modules.append({"type": "display", "key": "Display",
                            "format": "{width}x{height} @ {refresh-rate} Hz"})
    elif isinstance(entry, dict) and entry.get("type", "").casefold() == "display":
        new_entry = entry.copy()
        new_entry.update({"key": "Display",
                          "format": "{width}x{height} @ {refresh-rate} Hz"})
        new_modules.append(new_entry)
    else:
        new_modules.append(entry)
original["modules"] = new_modules
target.parent.mkdir(parents=True, exist_ok=True)
target.parent.chmod(0o700)
tmp = target.with_name(".config-alroot-tmp")
tmp.write_text(json.dumps(original, ensure_ascii=False, indent=2) + "\n",
               encoding="utf-8")
tmp.chmod(0o600)
os.replace(tmp, target)
print("Alroot-only fastfetch: generic Display label, actual XRandR refresh")
