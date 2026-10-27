#!/data/data/com.termux/files/usr/bin/python3
"""
Apply an XRandR mode matching the actual parent Android wl_output refresh.
The Android host is paced by real Choreographer VSync; this corrects only
the rootful Xwayland CVT modeline's otherwise hardcoded ~60Hz capability.
Never touches DISPLAY :0 or edits an installed Xwayland binary.
"""
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

HOME = Path("/data/data/com.termux/files/home")
PROJECT = HOME / "alroot_wayland"
STATE = HOME / ".alroot-wayland"
PREFIX = Path("/data/data/com.termux/files/usr")
PROBE = PROJECT / "packages/bin/wayland-refresh-probe"


def run(args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, text=True,
                          timeout=6, **kwargs).stdout


def main():
    try:
        display = (STATE / "display").read_text().strip()
        if not re.fullmatch(r":[0-9]{1,3}", display) or display == ":0":
            raise RuntimeError("refusing original/invalid X DISPLAY")
        auth = STATE / "Xauthority"
        if not auth.is_file():
            raise RuntimeError("isolated Xauthority missing")

        if not PROBE.is_file() or PROBE.stat().st_mtime < (
                PROJECT / "termux/wayland-refresh-probe.c").stat().st_mtime:
            PROBE.parent.mkdir(parents=True, exist_ok=True)
            run([str(PREFIX / "bin/clang"), "-O2",
                 str(PROJECT / "termux/wayland-refresh-probe.c"),
                 "-o", str(PROBE), "-lwayland-client"])
            PROBE.chmod(0o700)

        parent_env = dict(os.environ,
                          XDG_RUNTIME_DIR=str(STATE / "runtime"),
                          WAYLAND_DISPLAY="wayland-0")
        mhz = int(run([str(PROBE)], env=parent_env).strip())
        if not 30000 <= mhz <= 240000:
            raise RuntimeError("untrusted output refresh outside 30..240Hz")
        hz = mhz / 1000.0

        d, w, h = display, 1920, 1080
        geometry = (STATE / "x11-geometry").read_text().split()
        if len(geometry) != 2:
            raise RuntimeError("missing configured rootful Xwayland dimensions")
        w, h = int(geometry[0]), int(geometry[1])
        if not (320 <= w <= 3840 and 240 <= h <= 2160):
            raise RuntimeError("untrusted X11 output dimensions")

        env = dict(os.environ, DISPLAY=d, XAUTHORITY=str(auth))
        before = run([str(PREFIX / "bin/xrandr"), "--query"], env=env)
        output_match = re.search(r"^(\S+)\s+connected\s+", before, re.M)
        if not output_match:
            raise RuntimeError("no isolated rootful Xwayland output")
        output = output_match.group(1)
        # Use CVT's stock, server-accepted pixel clock. The earlier
        # "exact MHz" adjustment was rejected by rootful Xwayland's
        # RRAddOutputMode (BadMatch), leaving the mode stuck at 59.96Hz.
        # At 120Hz CVT-RB is 119.88Hz; the actual frame pacing is still
        # provided by the Android host's physical VSync, not the modeline.
        name = "Alroot-%dx%d-%dHz" % (w, h, round(hz))
        if re.search(r"^\s*" + re.escape(name) + r"\s+[\d.]+\*", before, re.M):
            print("Alroot XRandR already synchronized: %.3fHz" % hz)
            return 0

        # CVT reduced blanking requires >=60Hz; 30Hz PiP uses classic CVT.
        command = [str(PREFIX / "bin/cvt")]
        if hz >= 59:
            command.append("-r")
        command += [str(w), str(h), str(round(hz))]
        cvt = run(command)
        modeline = next((line.split("Modeline", 1)[1].strip() for line in
                         cvt.splitlines() if "Modeline" in line), None)
        if not modeline:
            raise RuntimeError("could not obtain CVT modeline")
        tokens = shlex.split(modeline)
        if len(tokens) < 10:
            raise RuntimeError("incomplete CVT modeline")
        model = [name, tokens[1], *tokens[2:]]

        if name not in before:
            run([str(PREFIX / "bin/xrandr"), "--newmode", *model], env=env)
        # Adding an already-attached mode is harmless but emits BadMatch;
        # only add if the listed output does not already include it.
        if not re.search(r"^\s*" + re.escape(name) + r"\s", before, re.M):
            run([str(PREFIX / "bin/xrandr"), "--addmode", output, name], env=env)
        run([str(PREFIX / "bin/xrandr"), "--output", output,
             "--mode", name], env=env)
        after = run([str(PREFIX / "bin/xrandr"), "--query"], env=env)
        selected = re.search(r"^\s*" + re.escape(name) +
                             r"\s+([\d.]+)\*", after, re.M)
        if not selected or abs(float(selected.group(1)) - hz) >= 0.5:
            raise RuntimeError("XRandR selected mode did not match parent actual refresh")
        print("PASS: rootful Xwayland %dx%d at %.3fHz" % (w, h, hz))
        print("Source: actual Android parent wl_output CURRENT mode=%d mHz" % mhz)
        return 0
    except (ValueError, subprocess.CalledProcessError, subprocess.TimeoutExpired,
            OSError, RuntimeError, StopIteration) as exc:
        if isinstance(exc, subprocess.CalledProcessError) and exc.stderr:
            print("Alroot XRandR stderr: %s" % exc.stderr[:600],
                  file=sys.stderr)
        print("P05 XRandR refresh sync blocked: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
