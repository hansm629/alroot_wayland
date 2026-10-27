# S26U run, diagnostics and safe recovery

Install the locally signed helper via Android system installer without deleting
Termux or Termux-X11, then open Alroot Wayland. Verify UID/signature MATCH and
the actual host socket status.

If default Termux Labwc aborts with libtermuxgui protobuf missing symbols, build
the project-private wlroots library using
`bash ~/alroot_wayland/packages/build-wlroots-termux.sh`.
This does not overwrite system wlroots or protobuf; the Labwc profile
sets an isolated LD_LIBRARY_PATH for its own process.

From the Termux shell (NOT inside PRoot):

    bash ~/alroot_wayland/termux/alroot-wayland doctor
    bash ~/alroot_wayland/termux/alroot-wayland status
    bash ~/alroot_wayland/termux/alroot-wayland start --profile xfce-labwc
    bash ~/alroot_wayland/termux/alroot-wayland logs
    bash ~/alroot_wayland/termux/alroot-wayland open

Diagnostic only: start --profile xfce-shm-direct (if Labwc fails).
The Android menu provides controls for user capture, keyboard, output size,
fit/stretch/fill, rotation, fullscreen, screen-on, and explicit stopping.
Ctrl+Alt+Esc releases mouse capture and all injected key/button states.

Install a new, non-overwriting Termux command symlink and run diagnostics:

    bash ~/alroot_wayland/tools/install-cli.sh
    alroot-wayland doctor
    bash ~/alroot_wayland/tests/p01-smoke.sh

X11 clipboard and Android committed IME text use a separate project-owned
bridge under ~/.alroot-wayland/ipc. It uses only the isolated rootful X DISPLAY
and pinned Termux xclip/xdotool packages. Clipboard is UTF-8 text only
(maximum 1 MiB); X11 IME sends completed text via XTEST (up to 16 KiB
per commit). Preedit remains Android-side until commit. Korean and multiline
typing remain TEST-PENDING until live IDE verification.

Logs: ~/.alroot-wayland/logs, ~/.alroot-wayland/spawn.log;
Android: logcat -s AlrootWayland DroidDeckWayland

Stop only this project's session:

    bash ~/alroot_wayland/termux/alroot-wayland stop

Then use the app menu to stop its server. Existing tx11start/Termux-X11 is
untouched. Text composition and nested clipboard require real IDE testing.
