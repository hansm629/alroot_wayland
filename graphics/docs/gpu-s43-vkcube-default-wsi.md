# GPU-S43 — vkcube opens in the X11 desktop by default

## Installed result

On **2026-11-01 (Asia/Seoul)**, the S26U Bionic owner package `vulkan-tools`
was upgraded from `1.4.365+alroot.s42.1` to **`1.4.365+alroot.s42.2`**.
Its state is **ACTIVE_VERIFIED**. Plain `vkcube`, with no WSI argument, now
creates an XFWM-managed XCB window in the running `awstart` XFCE session.
The installed executable passed a window-manager close request and exited
normally with code 0. The desktop was not restarted.

This is a follow-up to [S42](gpu-s42-installed-defaults.md), whose accepted
Wrapper, Mesa 27 Zink, glxgears and launcher defaults remain the baseline.

## Reproduced cause

S42 enabled all four upstream vkcube backends: XCB, Xlib, Wayland and display.
In Vulkan-Tools 1.4.365, `demo_check_and_set_wsi_platform()` tries Wayland
before XCB when the selection is AUTO. Its `wl_display_connect(NULL)` can
reach the host's default socket through `XDG_RUNTIME_DIR`, even when
`WAYLAND_DISPLAY` is absent.

The original installed binary was reproduced in the actual desktop:

| Observation | Before the correction |
| --- | --- |
| `XDG_SESSION_TYPE` | `x11` |
| `DISPLAY` | `:98` |
| `WAYLAND_DISPLAY` | Absent |
| `XDG_RUNTIME_DIR` | `/data/data/com.termux/files/home/.alroot-wayland/runtime` |
| AUTO diagnostic | `Selected WSI platform: wayland` |
| Protocol evidence | Native `xdg_surface` and `alroot_gpu_buffer_v1` |
| XFWM managed-window list | No new window |

The cube therefore appeared as a direct host Wayland surface, outside XFWM's
title bar and close-button handling. The prior S42 X11 probes explicitly
passed `--wsi xcb`; they did not test the unqualified command and missed this
automatic-selection behavior.

## Correction and package scope

[Patch 0005](../patches/vulkan-tools/1.4.365/0005-vkcube-prefer-x11-session.patch)
changes only the Wayland AUTO condition. When `XDG_SESSION_TYPE` is exactly
`x11`, `DISPLAY` is nonempty and XCB or Xlib support is compiled, AUTO skips
the Wayland probe and proceeds to the existing X11 connection checks.
Selection is still committed only after the existing backend initialization
succeeds. The patch does not force an uninitialized XCB connection.

Explicit `--wsi wayland`, `--wsi xcb`, `--wsi xlib` and `--wsi display`
retain their existing selection paths. A Wayland session retains upstream
Wayland-first AUTO even if `DISPLAY` is also available. The accepted S39
ConfigureNotify coalescing and all rendering/resize code are unchanged.

The [S43 builder](../tools/build-s43-vkcube-deb.py) copies the pinned S42
source into an independent tree, applies the one patch, and rebuilds the
`vkcube` target with the same four WSI backends. The exact S42 `.deb` supplies
the staging payload and original control members. Only these two package
payload paths change, relative to `/data/data/com.termux/files/usr`:

- `bin/vkcube`
- `share/doc/vulkan-tools/alroot-build.json`

Other binaries, copyright link, directory modes and payload remain those of
the archived owner package. The package control version/size and checksums
are regenerated. The builder creates the package without installing it and
requires the accepted S42 package as its installed input baseline.

| Identity | SHA-256 |
| --- | --- |
| S42 rollback `.deb` | `c6926d52fea7b199d4c2263a34e0abe17972bcf76949c5bc3ace3554f4610dcc` |
| S43 installed `.deb` | `3da604ae04f32fae58d836decd885aeb48a278eb6ec61d4cec273064d93a6eed` |
| S43 installed `vkcube` | `f848eae71659918458a2ea6c17a01f9e0551939a4bf4d324c774831c09bac64c` |
| Patch 0005 | `a3c2db3eebf9e0c9be86dfbe9a7f75cbc2771445669fd3013fbfbf06ebaf75bc` |
| Patched `cube.c` | `bf7991b13c280820cd48566150382ca9ddf049ab219d1d4bd0c28fb32087bf47` |
| Builder used for this package | `4052c2bfc7054b83d5166b5ff3657184c05c562e8727e84baa1999d2e0d4d3d5` |

## Validation

[The default-launch probe](../tools/test-s43-vkcube-default.py) reads the
running XFCE session's environment, verifies the selected executable hash,
and starts a child with **no arguments**. The native
[close helper](../tests/s43-close-owned-window.c) uses XRes local-client PID
to prove that the candidate XID belongs to that child. It checks managed
window membership, frame extents, `WM_DELETE_WINDOW` and the allowed close
action. It then sends `_NET_CLOSE_WINDOW` through the window manager;
the caller waits for normal process exit and disappearance of the XID.
It does not send mouse/keyboard input or directly destroy a window.

| Gate | Result |
| --- | --- |
| Candidate, plain launch in the real X11 desktop | XCB; owned managed window; frame extents left/right/top/bottom = 2/2/32/2 |
| Candidate, WM close | Close allowed, `WM_DELETE_WINDOW` present, exit 0, window removed |
| Explicit `--wsi wayland --c 120` from that X11 environment | Native Wayland/AHB protocol observed; exit 0 |
| AUTO with `XDG_SESSION_TYPE=wayland`, while retaining `DISPLAY` | Native Wayland/AHB protocol observed; exit 0 after 120 frames |
| Existing isolated resize probe with the candidate executable | PASS; 527 Configure events, 502 Present completions, maximum completion receive gap 146 ms, no empty core COPY bins |
| One/two-pixel height and normal-size recovery | PASS; fixed-size animation advancement PASS |
| Installed `/data/data/com.termux/files/usr/bin/vkcube`, plain launch and WM close | PASS; XCB, frame extents 2/2/32/2, exit 0, window removed |
| `dpkg --verify vulkan-tools` and `dpkg --audit` | Clean |
| Protected shared-driver baseline | 8/8 PASS |
| Other owner payload and selected graphics hashes | Unchanged, including `vkcubepp`, `vulkaninfo`, glxgears, Wrapper, Mesa GLX/Gallium and existing Freedreno/swrast ICDs |
| Live desktop identity | Same XFCE and Xwayland PID/start-time pairs before and after |

The resize counts measure X Present progress. They are not physical Android
frame-rate measurements. The close probe exercises the WM close protocol;
it does not claim a physical pointer click or a new complete visual review.

## On-device artifacts and immediate rollback

The project is `/data/data/com.termux/files/home/alroot_wayland`. S43 evidence
and packages are under `.source/gpu-stage1/s43-vkcube-default-wsi/`:

- `before-default.json`, `before-default.log`: original plain-command reproduction.
- `build-manifest.json`, `source-inputs.json`, `build.log`: independent build and payload comparison.
- `packages/vulkan-tools_1.4.365+alroot.s42.2_aarch64.deb`: installed correction.
- `rollback/vulkan-tools_1.4.365+alroot.s42.1_aarch64.deb`: exact immediate predecessor.
- `probes/candidate-20261101-013031-17661/summary.json`: plain X11, close, explicit Wayland and Wayland AUTO results.
- `probes/installed-20261101-013232-21106/summary.json`: installed plain-command and WM-close result.
- `pre-install.json`, `install.log`, `install-receipt.json`: package transaction and unchanged-state evidence.

The reused resize receipt is
`.source/gpu-stage1/s42-default-graphics/session-tests/logs/installed-vkcube-20261101-012925-15255/summary.json`.
Its selected runtime is the installed S42 drivers and its client hash is
the S43 candidate listed above.

If an immediate owner-package rollback is needed, install the retained S42
`vulkan-tools` archive with `dpkg -i`. That restores the former Wayland-first
AUTO behavior as well as its binary. There is no reason to roll back the
Wrapper, Mesa, glxgears, desktop configuration or launcher for this correction.
