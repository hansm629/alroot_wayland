#!/data/data/com.termux/files/usr/bin/python3
"""Isolated Bionic Xwayland rootful smoke test against headless Labwc.

Only our own new X socket is used (DISPLAY 201..240). The real :0 and :90,
Termux-X11, XFCE, device ICDs and the installed Alroot APK are never touched.
Headless Labwc has no GPU DMA-BUF path: this test is for server boot and
GLX/Present availability only, NOT proof of GLAMOR/DRI3 GPU rendering.
"""
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import time


PROJECT = pathlib.Path(__file__).resolve().parents[2]
PREFIX = pathlib.Path(os.environ.get("PREFIX", "/data/data/com.termux/files/usr"))
BUILD = PROJECT / ".source/gpu-stage1/xwayland/build/hw/xwayland/Xwayland"
LIBS = PROJECT / "packages/prefix/lib"


def stop(proc):
    if proc is None or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)


def main():
    if not BUILD.is_file():
        raise RuntimeError("Build the Alroot-only Xwayland before running this test")
    runtime = pathlib.Path(tempfile.mkdtemp(prefix="awgpu-", dir=PREFIX / "tmp"))
    runtime.chmod(0o700)
    config = runtime / "cfg"
    config.mkdir(mode=0o700)
    log_dir = PROJECT / ".source/gpu-stage1/xwayland/private-runtime"
    log_dir.mkdir(parents=True, exist_ok=True)
    labwc = None
    xwayland = None
    listener = None
    owned_socket = None
    try:
        env = os.environ.copy()
        env.update(
            XDG_RUNTIME_DIR=str(runtime),
            XDG_CONFIG_HOME=str(config),
            WLR_BACKENDS="headless",
            WLR_RENDERER="pixman",
            WLR_HEADLESS_OUTPUTS="1",
            WLR_NO_HARDWARE_CURSORS="1",
            LD_LIBRARY_PATH=str(LIBS),
        )
        with (log_dir / "labwc.log").open("w") as log:
            labwc = subprocess.Popen(["labwc", "-C", str(config)],
                                     env=env, stdout=log, stderr=subprocess.STDOUT)
        start = time.monotonic()
        wayland_socket = None
        while time.monotonic() - start < 7:
            for path in runtime.glob("wayland-*"):
                if path.is_socket():
                    wayland_socket = path.name
                    break
            if wayland_socket or labwc.poll() is not None:
                break
            time.sleep(0.08)
        if not wayland_socket:
            raise RuntimeError("Isolated headless Labwc did not create Wayland socket")
        # A caller may have other private X servers. Never steal their ID.
        for number in range(201, 241):
            candidate = PREFIX / f"tmp/.X11-unix/X{number}"
            lock = PREFIX / f"tmp/.X{number}-lock"
            if not candidate.exists() and not lock.exists():
                owned_socket = candidate
                break
        if owned_socket is None:
            raise RuntimeError("No unused private X display IDs")
        owned_socket.parent.mkdir(parents=True, exist_ok=True)
        listener = socket.socket(socket.AF_UNIX)
        listener.bind(str(owned_socket))
        listener.listen(16)
        owned_socket.chmod(0o600)
        display = f":{number}"
        auth = runtime / "Xauthority"
        auth.touch(mode=0o600)
        subprocess.run(["xauth", "-f", str(auth), "add", display,
                        "MIT-MAGIC-COOKIE-1", os.urandom(16).hex()],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       check=True)
        child_env = os.environ.copy()
        child_env.update(
            XDG_RUNTIME_DIR=str(runtime),
            WAYLAND_DISPLAY=wayland_socket,
            XAUTHORITY=str(auth),
        )
        child_env.pop("DISPLAY", None)
        xclient_env = {**os.environ, "DISPLAY": display, "XAUTHORITY": str(auth)}
        for mode in ("off", "es"):
            with (log_dir / f"xwayland-{mode}.log").open("w") as log:
                xwayland = subprocess.Popen(
                    [str(BUILD), display, "-listenfd", str(listener.fileno()),
                     "-geometry", "640x360", "-dpi", "96", "-auth", str(auth),
                     "-nolisten", "tcp", "-glamor", mode, "-verbose", "3"],
                    env=child_env, pass_fds=(listener.fileno(),),
                    stdout=log, stderr=subprocess.STDOUT,
                )
            probe = None
            for _ in range(50):
                try:
                    probe = subprocess.run(["xdpyinfo", "-display", display,
                                            "-queryExtensions"], env=xclient_env,
                                           capture_output=True, text=True, timeout=1)
                except subprocess.TimeoutExpired:
                    probe = None
                if probe is not None and probe.returncode == 0:
                    break
                if xwayland.poll() is not None:
                    break
                time.sleep(0.08)
            if probe is None or probe.returncode != 0:
                raise RuntimeError(f"Private Xwayland {mode} did not accept X11 clients")
            extensions = probe.stdout
            for name in ("GLX", "Present"):
                if not any(line.lstrip().startswith(name + "  (")
                           for line in extensions.splitlines()):
                    raise RuntimeError(f"Private Xwayland {mode} missing {name}")
            has_dri3 = any(line.lstrip().startswith("DRI3  (")
                           for line in extensions.splitlines())
            print(f"PASS Xwayland -glamor {mode}: rootful start, GLX, Present; "
                  f"DRI3 advertised={int(has_dri3)}", flush=True)
            print("GPU_FRAME_PRESENT=NOT_TESTED (headless Labwc/pixman)", flush=True)
            stop(xwayland)
            xwayland = None
    finally:
        stop(xwayland)
        stop(labwc)
        if listener is not None:
            listener.close()
        if owned_socket is not None and owned_socket.exists():
            owned_socket.unlink()
        shutil.rmtree(runtime, ignore_errors=True)


if __name__ == "__main__":
    main()
