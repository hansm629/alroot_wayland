# S26U run, diagnostics and safe recovery

Install the locally signed helper via Android system installer without deleting
Termux or Termux-X11, then open Alroot Wayland. Verify UID/signature MATCH and
the actual host socket status.

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

Logs: ~/.alroot-wayland/logs, ~/.alroot-wayland/spawn.log;
Android: logcat -s AlrootWayland DroidDeckWayland

Stop only this project's session:

    bash ~/alroot_wayland/termux/alroot-wayland stop

Then use the app menu to stop its server. Existing tx11start/Termux-X11 is
untouched. Text composition and nested clipboard require real IDE testing.
