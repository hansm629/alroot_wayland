# S39: resize coordinates, root output reuse, and vkcube event handling

## Scope and result

S39 separates three changes that must not be described as one driver fix:

1. A private Xwayland patch makes resize snapshot replay use the same top-left
   origin as the real Present copy. It removes a verified shift/crop mismatch.
2. The same patch keeps the unchanged root output buffer pool when only an
   independent child-window backing pixmap changes. It removes repeated full-HD
   AHB output allocations during a drag.
3. A separate, explicitly selected private **vkcube 1.4.365** build batches XCB
   Configure handling before rebuilding its swapchain. This is the change that
   restored continuous Present submissions during the fast resize reproduction.

The installed vkcube is not replaced. It can still stop submitting frames during
a fast drag, including on the S39 server. The S26 wrapper ICD is unchanged in the
final route. A tested wrapper ACK/check reordering experiment did not resolve the
stall and is not part of the release.

Tests use an authenticated disposable Xwayland display, its own XFWM/DBus session,
and only test-owned windows/processes. The user's existing S38 XFCE session is
preserved. These results do not certify physical Android display smoothness.

## Why the gears could shift or appear clipped

The stable snapshot had native 1:1 pixels, but its resize replay centered or cropped
the saved image. A subsequent genuine Present copy started at `(0, 0)`. Therefore
the same landmark could move between resize preservation and new presentation.

The diagnostic uses three exact colored landmarks, enlargement, shrinkage, and
a new Present whose size differs from the current window. The S38 negative
control retained 0/3 original positions and 3/3 centered positions. The origin
candidate retained 3/3 original positions through all four phases.

`S39_KGSL_RESIZE_ORIGIN=1` changes only the replay source/destination origins in
the S32 opt-in preservation path. The copied extent remains the minimum of source
and destination. It does not scale the image or change the real Present extent.
This corrects the coordinate defect; the reported intermittent blue-gear clipping
still needs a visual check on the Android screen with the final server selected.

## Why independent child resizes caused full-screen allocation churn

`SetWindowPixmap` could find the ancestor root's `xwl_window` and dispose its
output pool when a child pixmap changed size. The next root output swap then had
no available buffer and allocated another 1920x1080 output pixmap/AHB, although
the root's geometry had not changed. Decorated XFWM windows amplified the number
of disposal calls.

During the measured 8 ms interactive drag:

| Server policy | Full-HD allocations per approximately 500 ms | Output reuse per interval |
|---|---:|---:|
| S38 policy, diagnostic build | 36–40 | 0 |
| S39 pool guard, diagnostic build | 0 | 35–41 |

The baseline also logged 713–851 independent-child disposal calls in full drag
intervals. With the guard, these disposal calls were zero. This is an allocation
and reuse result, **not** evidence that the vkcube stall was solved by the guard.
The vkcube stall remained in that isolated comparison.

The guard requires all of the following: explicit opt-in, rootful KGSL GLAMOR,
the real screen root as output surface, a non-root affected window, and both
old/new child pixmaps distinct from the current root pixmap. Root changes,
shared-root backing changes, other surfaces and other renderers retain the
original disposal path. Damage, complete GPU preservation on reuse, compositor
references and release callbacks are unchanged.

## The vkcube stall is event-loop starvation

The exact installed package is Termux vulkan-tools 1.4.365. In the matching
[upstream cube.c](https://github.com/KhronosGroup/Vulkan-Tools/blob/v1.4.365/cube/cube.c),
the XCB event handler calls `demo_resize()` for each differently sized Configure
event. The main loop drains all queued events before drawing. Swapchain setup
queries the latest surface extent, so stale queued Configure sizes can trigger
additional rebuilds even after a newer extent has already been selected.

At 16 ms resize steps the initial baseline continued presenting. At 8 ms, both
direct XResizeWindow and a real XFWM `_NET_WM_MOVERESIZE`/XTest drag reproduced a
multi-second interval with zero new X11 COPY completions. Wrapper instrumentation
also showed zero Present requests during those intervals: the application was
recreating swapchains rather than drawing. Initial measurements found roughly
13–17 ms creation time during the drag versus about 1.2 ms after it stopped.
Later runs varied with device load. Allocation time alone was not the dominant
cost; serial server waits contributed substantially.

Patch `0004-xcb-configure-coalesce.patch` records the last received dimensions
while dispatching every pending event, then performs at most one event-triggered
resize before the next draw. A close event exits before another resize/draw.
Keyboard handling, surface capability queries, acquire/present error recovery,
the Vulkan ICD and the rendering code remain upstream behavior. No resize rate
limit is introduced. The model rotation advances per draw and is not reset by
resize in this upstream version.

The private baseline and candidate use the same source version, Termux platform
patches, compiler options and WSI configuration. Only the XCB event-loop patch
differs. The source archive, patches and resulting source files are hash-pinned;
see [build input provenance](../patches/vulkan-tools/1.4.365/README.md).

## Validation

### Continuous resize, measured during the drag

The observer counts real X11 Present Complete events, distinguishes COPY/FLIP/
SKIP, and records Configure events and receive gaps. It does not use changing
window geometry or whole-image hashes as evidence of animation. A 500 ms interval
is included only when it lies wholly within the middle of the resize operation.

| Final test, same S39 server and ordinary S26 ICD | COPY completions in the 3-second core interval | Longest receive gap |
|---|---:|---:|
| Same-build private vkcube baseline | 0; all six bins zero | 4583 ms |
| Private coalesced vkcube | 72 (24/s); all six bins nonzero | 103 ms |
| Installed glxgears / private Zink | 119 (39.7/s); all six bins nonzero | 70 ms |

The coalesced client's six 500 ms COPY counts were `12, 11, 12, 11, 9, 17`.
The Zink counts were `15, 21, 16, 20, 20, 27`. All clients and test servers remained
alive through the resize interval. The ordinary vkcube baseline still presented
before and after the drag, establishing a resize-specific negative control.
These are X11 completion rates, not Android display FPS or a visual rotation
measurement.

Earlier diagnostic runs also showed continuous submissions with the coalesced
client: 12–30 COPY completions per 500 ms with WSI counters (92 ms maximum gap),
and 7–13 per 500 ms while a CPU pixel-readback observer ran (120 ms maximum gap).
The readback helper rejected all middle-drag pairs whose geometry changed during
the two reads, so those images are **not** used as visual proof of rotation during
the drag. These diagnostic runs are not used as the final performance comparison.

Final formal logs, relative to `.source/gpu-stage1/s39-live-resize/`:

- Baseline: `logs/formal-vkcube-baseline-interactive-8ms-20261031-180814-3390`
- Candidate, thin sizes and close: `logs/formal-vkcube-coalesced-interactive-8ms-20261031-180826-6278`
- Origin: `logs/formal-vkcube-coalesced-interactive-8ms-20261031-180841-7648`
- Zink: `logs/formal-zink-installed-interactive-8ms-20261031-180844-3388`

Relevant original logs on the development device:

- S38/fast-drag reproduction: `logs/vkcube-interactive-8ms-20261031-173036-7180`
- Pool baseline: `logs/vkcube-interactive-8ms-20261031-174906-21510`
- Pool guard: `logs/vkcube-interactive-8ms-20261031-175048-25821`
- Same-build app baseline: `logs/vkcube-interactive-8ms-20261031-175656-8478`
- Coalesced client with WSI counters: `logs/vkcube-interactive-8ms-20261031-175546-4836`
- Coalesced client with diagnostic readback: `logs/vkcube-interactive-8ms-20261031-175743-11357`

These paths are relative to `.source/gpu-stage1/s39-live-resize/`.

### S38 regression checks retained

Three 1.5-second resize bursts at 10 ms, with 2-second settling periods, passed:

| Client | Settled Xwayland FD counts | Content after stress |
|---|---|---|
| Installed vkcube / S26 ICD | 73 → 73 → 73 | 22 fixed-size samples, 21 changes, no geometry changes/errors |
| Installed glxgears / private Zink | 56 → 56 → 56 | 48/48 samples with red, green and blue; 47 changes; zero blanks/errors |

The exact latest-Present check also passed: initial `2040e0`, newer `20e040`, and
`20e040` after resize. The S38 pre-idle snapshot ordering, S38 release callback
fix, and S37 thin-height FD selection remain linked unchanged.

Regression logs are under `.source/gpu-stage1/s38-frame-continuity/logs/`:

- `lifetime-candidate-vkcube-20261031-175857-13837`
- `lifetime-candidate-zink-20261031-175954-18115`

The final canonical S39 binary passed the origin landmark probe with all 3/3
landmarks in their original positions in all four checked resize phases. The
coalesced client also passed actual `200x1`, `200x2`, `400x1`, and `500x500` sizes,
continued animating at the restored fixed size, and exited normally with code 0
when Escape was sent to its own window.

Both formal builders reproduced their reference hashes. The existing read-only
session guard regression passed with ten pinned server profiles, including wrong
PID, wrong display and mismatching executable rejection. Launcher and S38 rollback
dry runs passed. The eight protected system graphics files and installed vkcube
hash remained unchanged. All S39 runtime test processes were gone after cleanup;
the existing S38 Xwayland/XFCE PIDs remained 25179/25289 on `:97`.

The S36 direct root output helper still uses `glFinish()`. S39 does not remove
that synchronization or claim a CPU-wait-free pipeline. Android helper/glibc
interoperability is not newly certified by these native Termux client tests.

## Reproduce and select

Both formal builders reproduced the tested binaries exactly. Neither installs a
system package, changes the Android APK, or restarts the user's desktop.

```sh
cd ~/alroot_wayland
python graphics/tools/relink-s39-live-resize.py
python graphics/tools/build-s39-vkcube.py --jobs 2
```

Artifacts:

| Component | SHA256 |
|---|---|
| S39 Xwayland | `c5f6c9a512f84e31f60ec52a9f10e689e0746f831049e57a4223ade2750f3899` |
| Private vkcube baseline | `2a4c55dc54b14ede27acdb9e88eace5d00c64c20580187ea8e02ceaf2c5cd56b` |
| Private coalesced vkcube | `0050e7126c8d096cd14a7e0b1026b7063f898e33eb7be5b80116991cee57c208` |

To select the server, save desktop work and run from a separate native Termux
shell, as with the preceding S38 launcher:

```sh
bash graphics/tools/start-s39-live-resize.sh --restart
```

Run the demos individually; each stays open until its window is closed or the
runner is interrupted:

```sh
python graphics/tools/run-s24-wrapper-visual.py zink --s26
python graphics/tools/run-s24-wrapper-visual.py vulkan --s26 --coalesced-xcb
```

The second command explicitly selects the private patched application. Omitting
`--coalesced-xcb` continues to select the installed vkcube and is a useful original
behavior comparison. The runner prints the selected client, its hash and the
unchanged S26 wrapper identity.

Rollback:

```sh
bash graphics/tools/start-s39-live-resize.sh --restore-s38
```

The installed application requires no rollback because it was not replaced.

The new continuous-resize harness can run the negative control, candidate,
origin and thin-window checks on its own disposable server:

```sh
python graphics/tools/test-s39-live-resize.py --client baseline --expect-starvation
python graphics/tools/test-s39-live-resize.py --client coalesced --thin --close-client
python graphics/tools/test-s39-live-resize.py --origin
```

Do not interpret the older post-resize-only animation probe as proof of animation
while resizing. The middle-of-drag measurements above were added specifically
because the previous tests did not establish that property.
