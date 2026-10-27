# P01 status (2026-10-27; update against actual evidence)

| Scope | Result | Evidence / limitation |
|---|---|---|
| Public repo | PASS | hansm629/alroot_wayland created |
| Native source import | PASS | DroidDeck pinned 0f839bef; JNI/Vulkan native component built by CI |
| Termux compatibility | PASS | UID 10400 and installed APK/candidate signing cert match |
| Existing X11 preserved | PASS | no reset or package upgrades |
| Labwc | PARTIAL / TEST-PENDING | private upstream wlroots 0.18.2 built and Labwc help works; nested session not yet verified |
| Xwayland | TEST-PENDING | installed; rootful options verified |
| CI APK | PASS (unsigned) | Actions run 40510004620 built app-release-unsigned.apk, native JNI included |
| local APK signing | PASS | locally signed from CI artifact, v2/v3 verified, installed Termux cert matched |
| S26U helper display | TEST-PENDING | session and actual surface not yet proven |
| Cursor/capture | TEST-PENDING | guest cursor/hotspot and host escape path wired; screen test pending |
| Clipboard | TEST-PENDING | Android/X11 bounded private IPC and native Wayland path wired; live copy pending |
| Keyboard/IME | TEST-PENDING | physical scan codes, X11 XTEST commit and bounded deletion wired; Hangul pending |
| Service persistence | TEST-PENDING | recreate/DeX/offscreen untested |
| CPU top-app lineage | TEST-PENDING | shared UID not a CPU scheduling proof |
| GPU-copy/zero-copy | NOT STARTED | P03-P08, not P01 |

Do not promote code existence or build success into screen/input PASS.

## S26U awstart actual shell integration
- awstart installed as a new non-overwriting Termux CLI shortcut; help, doctor, dry run and smoke tests succeeded.
- S26U nested Labwc: host socket READY, Labwc and project-owned rootful Xwayland :90, XFCE, clipboard processes RUNNING.
- Private wlroots Android patch now also fixes Termux X11 socket paths (system lib untouched).
- Rootful Xwayland now stays alive after readiness probe: removed -terminate.
- Guest P01 initial rootful output size 1280x720; APK size synchronization is P02.
- Android host logs have compositor/client frames but GUI/capture/IME/clipboard user confirmation is still TEST-PENDING.
- New foreground Activity handoff requires app version 2 update; top-app cgroup/cpuset worker policy remains TEST-PENDING.
- User's original Termux-X11 :0 and XFCE process survived session tests.

## Installer status / failed-start follow-up
- S26U PackageManager now returns com.alroot.wayland on shared UID 10400. Installed base.apk and the local signed P01 APK match byte-for-byte, including the intended Termux signing certificate.
- The user's earlier awstart command saw NOT INSTALLED; installation was subsequently confirmed. The reason the earlier lookup was empty was not independently determined.
- Actual default awstart --debug returned 0: foreground Activity acknowledged launch; host READY and Labwc/Xwayland/XFCE/clipboard RUNNING at isolated X DISPLAY :90.
- The installer command now distinguishes opening the Android installer from confirmed installation and avoids reopening it when local and installed APKs match.
- On-screen correctness, capture, Korean IME and clipboard remain TEST-PENDING.

## Black-screen investigation: 2026-10-27

- **Correction**: process and host frame counters were wrongly treated as
  evidence of successful XFCE display. The user saw an actual black screen.
- Isolated Xwayland :90 had no X11 window-manager registration. Its xfdesktop
  and xfce4-panel processes selected native GTK Wayland and logged missing
  layer-shell errors; the only xfwm4 was running for the unrelated original
  Termux-X11 session on :0.
- The project-only XFCE environment now forces GDK_BACKEND=x11,
  QT_QPA_PLATFORM=xcb, SDL_VIDEODRIVER=x11, DESKTOP_SESSION=xfce and
  XDG_CURRENT_DESKTOP=XFCE. No global environment or Mesa is changed.
- After a project-only restart, xfwm4, xfdesktop and xfce4-panel were
  registered on isolated :90 at 1280x720 with their actual X11 WM_CLASS;
  prior GTK Wayland/layer-shell warnings disappeared.
- An Xlib XGetImage probe read 576/576 nonblack sample pixels from each of
  isolated X11 root, desktop and panel. This proves nonblack source X11
  pixels, **not yet their presentation on the Android SurfaceView**.
- x11_desktop: READY/INCOMPLETE now verifies the project's real X11 WM,
  desktop and panel registration before awstart reports session readiness.
  Mocked missing WM correctly reported INCOMPLETE.
- Transient empty pm path queries can misreport an installed APK as missing.
  awstart now falls back to package-list evidence, and the explicit --open
  action relies on Android ActivityManager's actual result.
- APK v0.1.2 adds lightweight PixelCopy sampling of the helper's own
  SurfaceView, logging nonblack pixel counts and brightness range. A real
  PixelCopy run and final display correctness remain TEST-PENDING until the
  Android-approved APK update.

## 2026-10-28 P02 revision 2 (S26U)

- User screenshot: distorted fullscreen 16:9, 96-DPI expectation not met, Android soft input missing, Plank layout different, battery widget BAT --%, stray cursor cross. These were treated as reproduction evidence, not resolved by old process counters.
- Rootful Xwayland was hardcoded to 1280x720 despite Android guest setting 1920x1080. Separate server now starts with configured guest geometry, explicitly -dpi 96; isolated xdpyinfo verified 1920x1080 and 96x96 DPI. Fullscreen compositor changed from STRETCH to aspect-preserving FIT (landscape height fills, side margins may remain). Android visible result pending updated APK installation.
- Termux-X11 Plank icon order/theme/zoom came from ~/.config/dconf/user, not merely .dockitem launchers; original and isolated DConf SHA256 matched in live scoped test. Refresh private Xfce panel/theme/desktop snapshot each start. Original user settings were not modified.
- Battery GenMon's direct Termux API calls were unreliable in the panel; Android helper now publishes a bounded BatteryManager JSON snapshot periodically and GenMon reads it with a three-minute freshness check (Termux API fallback). Java cache production in newly installed APK remains unverified; battery fixture passed.
- A disposable X11 test revealed `xdotool type` Korean produced keysym=0 despite successful XTEST send. New Unicode commit via X11 clipboard paste was verified in a GTK entry and XFCE terminal on isolated :90 (2 Korean chars / 6 UTF8 bytes); the previous CLIPBOARD hash matched after restoration. Android keyboard source path is not yet verified.
- Removed temporary CursorOverlay white cross (the genuine Wayland cursor remains).
- CI and Android visual validation statuses should be recorded only after each actual run.
