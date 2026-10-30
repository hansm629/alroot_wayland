# GPU-S20 — XFCE pointer-hit test drift and atomic Android → Xwayland click

**S26 Ultra, 2026-10-30**. Isolated Alroot Android helper, private KGSL Xwayland and native XFCE4 session. **Not a global Termux-X11/Xorg patch.**

## User evidence / diagnosis

The supplied 7.23 s video (`Screen_Recording_20261030_140311_Alroot Wayland.mp4`, recorded at an Android screen-capture scaled 2340×1080) shows a terminal right-click menu with a visible pointer/touch indication separated from the menu item being acted upon. The pointer offset becomes noticeable with continued use; a one-time fixed display-scale correction is insufficient.

The S26U's saved Android preferences use `input_mode=0` (**persistent touchpad cursor**), `width=1920`, `height=1080`, `fullscreen=true`, `pointer_speed=123`, `pointer_accel=98`. Its on-device GPU display has FIT letterboxing, so cursor→guest mapping must use the *visible picture rectangle*, not screen dimensions or an asynchronously cached Vulkan map.

Crucial version check: the actual installed APK SHA-256 was `d955436dc1c9...`, while the pre-existing installer artifact (`artifacts/alroot-wayland-p01-signed.apk`, S16 `d7f5833`) was `b5178a65d1bb...`. **The prior Java single guest-scene coordinate-path and captured-touchpad corrections were not installed on the user's phone** at the time of the complaint; do not label them experimentally failed without deploying the APK first.

A stronger native-only check using the live S26U Android Wayland host's isolated `test-input` FIFO sent five absolute points across `Xwayland :92` and checked `xdotool getmouselocation`. **All five returned EXACTLY the requested positions**, including near both edges of the 1920×1080 scene. An X11 internal warp between host moves still resulted in exact absolute positions. This supports treating the **Android touchpad/hardware cursor → compositor input** path as the main remaining gap, rather than reworking the already-correct X11 scene transform.

## S20 implementation

1. `app/.../PointerSceneCoordinatePolicy.java`: one pure-Java `view (x,y) → guest scene (x,y)` inverse picture mapping with finite-value validation and 0..extent-1 clamping. Both Android cursor motion and clicks now reuse it. This avoids a second screen→scene transform and works for letterboxed FIT, stretched, cropped, PiP and DeX geometry.
2. `InputSurfaceView.sendButton`: uncaptured touchpad/virtual touch and physical mouse click **send `nativeSendSceneButton(sceneX,sceneY,evdev,pressed)`** instead of separate `absolute(lastX,lastY)` and button messages. `onGenericMotionEvent` already updates lastX for physical mouse buttons; persistent touchpad taps use the *visible* lastX/lastY rather than the finger contact point. Both right/left buttons receive the same fix, with duplicate physical button suppression unchanged. Captured games retain their established relative-input button path.
3. Native JNI and compositor: new input message **type 13** carries coordinate and button **in the same pipe packet**. The Wayland input thread clamps to its current scene and sends motion before the button in one Wayland pointer frame, preventing a delayed/dropped motion message from being overtaken by the button. The priority click packet gets a bounded 4 ms retry if the existing nonblocking input pipe is full, logging failures (not inventing successful clicks).
4. For the **rootful desktop only**, native ordinary absolute motion no longer synthesizes an additional zwp_relative_pointer_v1 event, which can drive Xwayland's pointer-warp emulator and accumulate a second movement during grabs or popup menus. `wl_pointer.motion` remains the sole position authority. True relative movement (pointer capture/games) and non-desktop Wayland apps retain their relative pointer protocol.
5. Native relative pointer updates now reanchor `g_raw_x/y` to the actual native pointer after movement; a later absolute touch after captured/game motion must not derive a large phantom delta from old coordinates. The existing pointer constraint unlock still invalidates the baseline.
6. Captured pointer deltas do not advance the Java overlay lastX/lastY while the native Wayland pointer is frozen by capture. On release, the persistent cursor therefore does not reappear displaced by accumulated raw game movement.

No changes to Fcitx5, Hangul IME, VTE caret, clipboard, GPU-copy, GLAMOR `highp`, Vulkan ICDs, Termux-X11, current app settings or desktop theme. No automatic update or restart of the user's running Android helper was performed by the development work.

## Tests and acceptance limits

`bash graphics/tools/test-s20-pointer-atomic.sh` compiles and runs a standalone Java geometry class on the **actual S26U Termux JDK**, with 50,000 deterministic simulated movements each for 3120×1440 FIT, 2340×1080 video-like FIT, 1920×1080, 752×423 PiP, stretch and crop. **Each mapping had maximum half-pixel quantization error**. The existing S16 mouse-policy test remains PASS with updated atomic button semantics; Android JNI native compositor and Vulkan present source syntax tests PASS.

**Required physical test after installing the signed APK**: move cursor across terminal right-click menu and popup items for 30–60 s; use both single and two-finger clicks, open/close repeatedly; scroll; type Korean/English; rotate and return; perform pointer capture/release and confirm cursor/target still agree. Compare first/last click coordinates in `logcat -d -s AlrootWayland | grep 'P20 atomic'`. `xdotool getmouselocation` may be used separately to view the actual X11 target. Do not claim real pointer UX PASS until that visual test is completed.

The Android installed APK still requires an **explicit user-approved** update. Replacing only a Termux private Xwayland binary cannot update the Java cursor/atomic button sender or native Android Wayland compositor in the APK.
