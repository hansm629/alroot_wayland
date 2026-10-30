# GPU-S27 — XFCE startup/stop safety and S25 helper fallback

**2026-10-30, Galaxy S26 Ultra.** This is a project-local Termux session
launcher fix. It does **not** modify Android host rendering, Vulkan Wrapper,
system Termux-X11, GPU drivers, cursor/IME implementation, or the default
`awstart` profile.

## User-reported symptom

After using the previously suggested two-command sequence:

```bash
awstart --stop
bash termux/start-s25-kgsl-topapp.sh
```

XFCE disappeared. `awstart --stop` is *intentionally destructive*: it stops
the whole project XFCE4 session, not only an old GPU demo. The separate S25
`top-app` launcher must successfully re-create XFCE afterward; its Android
foreground `REQUESTED` acknowledgement is **not** evidence that it did.

### Evidence from the actual S26U

At **23:16:36–23:16:38**, an authenticated private KGSL Xwayland `:96`
(PID 5281) and XFCE4 (session PID 5520 / `xfce4-session` PID 5573) started.
At **23:17:03**, XFCE, Plank and Fcitx received `SIGTERM (15)` and private
session D-Bus closed. The `xfce.pid`, `clipboard.pid` and `ime.pid` markers
were removed, but Xwayland PID 5281 and its marker remained. This state
is consistent with a partially interrupted `stop`/rollback. It is **not**
evidence of a Vulkan client SIGSEGV or an Xwayland crash. We cannot prove
which original caller sent the 23:17:03 TERM, because that attempt lacked
a persistent session-control action log.

Furthermore `control/start-ack` was timestamped **04:06**, not 23:16;
the original 23:16 start cannot be attributed confidently to the S25
foreground Activity request. The private Wayland Android host stayed READY.
Backups are under the ignored
`.source/gpu-stage1/s27-session-control-recovery/logs/20261030-2316-*.log`.

At **23:24**, after cleaning up only the validated orphan Xwayland,
the Android Activity logged `trusted Termux action start spawned` and
acknowledged `REQUESTED`, but neither the process output `spawn.log` nor
the new `action=START_BEGIN` session-control record appeared. The actual
Xwayland and XFCE4 processes remained DOWN. Running the equivalent
sanitized CLI environment directly from a native Termux shell worked.
This isolates a **helper-process child execution/retention problem**
independent of S26 Vulkan-WSI resizing. Its exact cause in the installed
Android Activity ProcessBuilder (early child failure vs OS child lifecycle)
remains unresolved; no unobserved crash reason is asserted.

## S27 changes

- `termux/alroot-wayland`: creates non-sensitive timestamps of START_BEGIN,
  START_READY, STOP_BEGIN, STOP_COMPLETE and START_ROLLBACK in
  `~/.alroot-wayland/logs/session-control.log`. No command arguments,
  credentials or user environment content are stored.
- Project start and stop now share the existing startup `flock` so an
  unrelated stop cannot destroy half-created Xwayland/IME/XFCE children.
- Explicit stop verifies each owned PID actually stopped, boundedly waits
  after TERM, and only if necessary SIGKILLs the **same verified** private
  PID/session group. It preserves the PID marker on a failed stop instead
  of silently losing track of a still-running Xwayland. It never uses a
  global `pkill` or touches Termux-X11 `:0`.
- Stops launched **inside their own XFCE terminal** with its private
  XDG_CONFIG_HOME and DISPLAY are rejected. The startup rollback attempts
  every owned role even when one role's cleanup fails.
- `termux/awstart`: distinguishes Android `REQUESTED` from a real Termux
  `START_BEGIN`; after a readiness timeout, reports a helper child spawn
  failure instead of wrongly attributing this to Xwayland. No change to
  the normal default renderer/profile.
- `termux/start-s25-kgsl-topapp.sh`: accepts `--restart` for one coherent
  stop/start request and `--shell` for an explicit, **known-good Termux
  shell KGSL GPU** path. Without `--shell`, it first tries the original
  Android Activity top-app path. If Activity accepts but the desktop never
  starts, it reports the error, verifies/cleans only project-owned leftover
  processes, and uses the native shell path as a **clearly labeled** fallback.
  It refuses to tear down any active XFCE unless `--restart` is supplied.
  After launch, it verifies the private Xwayland binary and monitors
  Xwayland+XFCE+WM+panel liveness for 36 seconds (longer than the earlier
  observed 26-second failure).

## Live S26U acceptance

1. The original orphan `:96` PID 5281 was retired using the owned-PID stop
   path with a completed STOP_BEGIN/STOP_COMPLETE control record.
2. A native shell GPU session started with the existing validated KGSL
   GLAMOR and dual-ABI xshmfence bridge: `Xwayland :96`, WM/panel/XFCE
   READY. Running processes reported CPU `0–5` in `/foreground`.
3. The new single-command `--restart` was executed against that live
   session: it stopped only owned IME/clipboard/XFCE/Xwayland, then reproduced
   the Android helper's `REQUESTED`-but-not-executed behavior.
4. The script **detected it**, reported
   `S25_TOPAPP=HELPER_ACKNOWLEDGED_BUT_DESKTOP_NOT_READY`, used the scoped
   shell fallback, and started new Xwayland PID **22033**, XFCE session PID
   **22164**, IME PID 22086 and clipboard PID 22167.
5. `S25_ACTUAL_LAUNCH_ROUTE=SHELL_FALLBACK`,
   `S25_TOPAPP_PRIVATE_KGSL_XWAYLAND=PASS_EXPECTED_BINARY` and
   `S25_DESKTOP_STABILITY_OBSERVATION=PASS_36_SECONDS_ROUTE_SHELL_FALLBACK`
   were verified. Subsequent separate status checks showed the same PIDs
   and `x11_desktop: READY`.
6. Live source/safety test `bash graphics/tools/test-s27-session-restart-safety.sh`
   PASS. Simulated invocation from within XFCE was blocked without changing
   any running PID, as was `--shell` without the required explicit restart.
   Existing S25 launcher source and S22 cursor/input tests PASS. Protected
   Termux GPU libraries and installed Xwayland hashes PASS.

This **fixes session availability**, not the original Android top-app child
execution problem. Shell fallback remains limited to CPU `0–5` under the
observed One UI `/foreground` cpuset; do not claim top-app performance or
Termux-X11-equivalent benchmark numbers.

## Safe commands

When desktop already READY, **do not stop it**: simply use the private S26
visual test from an independent native Termux terminal:

```bash
cd ~/alroot_wayland
awstart --status
bash graphics/tools/test-s26-wrapper-xcb-resize.sh --visual zink
```

To **intentionally** replace the running XFCE with the same GPU-accelerated
private profile in one coherent command, from the **native Termux shell**,
not inside the XFCE terminal:

```bash
cd ~/alroot_wayland
bash termux/start-s25-kgsl-topapp.sh --restart
```

This attempts Activity top-app first and reports a visibly labeled
`SHELL_FALLBACK` if it does not really start. To skip the known-failing
Activity path and use the confirmed native shell KGSL backend immediately:

```bash
bash termux/start-s25-kgsl-topapp.sh --restart --shell
```

No separate `awstart --stop` call is necessary before either command.
**Do not** use `--restart` if you wish to preserve your currently running
XFCE session and its applications. To diagnose future exits inspect
`~/.alroot-wayland/logs/session-control.log`, `xfce.log` and
`awstart --status` before starting another session.

## Remaining work

The S25 foreground Activity's Java ProcessBuilder child lifecycle requires
separate instrumentation and validation, potentially an approved helper APK
update. Do not quietly repurpose fallback as top-app or make it the
default `awstart` path. GPU-S26 human-controlled resizing and FPS/Present
optimization remain separate workstreams.
