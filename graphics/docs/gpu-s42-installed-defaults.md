# GPU-S42 — installed Wrapper / Mesa 27 defaults for S26U

**S43 follow-up:** the `vulkan-tools` package below was subsequently updated to
`1.4.365+alroot.s42.2` to correct plain `vkcube` selecting the host Wayland socket
from an X11 desktop. See [GPU-S43](gpu-s43-vkcube-default-wsi.md) for the installed
fix and plain-launch/WM-close evidence. This page retains the original S42
package identities and receipts; its X11 demo probes explicitly selected XCB.

## Scope and deployment state

S42 promotes the accepted Vulkan Wrapper and Mesa 27 Zink client path to the
default **S26 Ultra, Termux Bionic, rootful Xwayland / XFCE4** session started
by `awstart`. The drivers and the accepted resize-aware demos are installed
through their existing Debian package owners under the normal Termux prefix.
This is also the baseline from which subsequent Turnip and direct Freedreno
KGSL client experiments can be run one process at a time.

**All five S42 packages are installed and verified by dpkg.** The staged
S39/Wrapper/Zink, native Wayland and padding gates passed at their recorded
scope; a plain `awstart --restart` also passed with the new native default.
Installed-mode isolated client tests and a real default XFCE terminal after
the scoped startup correction also passed. The automated gates for this
installed native Bionic baseline are complete; their receipts are recorded
below. Explicit Activity spawning remains unconfirmed, and the physical
display visual review was not rerun after packaging. Earlier S39–S41
acceptance is kept separate from these new S42 results.

The promotion does not change the Android helper APK or install Bionic
libraries into a glibc rootfs. The S39 Xwayland renderer remains a distinct
server choice. Historical restrictions against changing global packages in
the S14/S23 experiments describe those earlier experiments; S42 is the
explicitly requested Bionic package/default promotion.

## Package identity and source pins

All installed paths below use
`PREFIX=/data/data/com.termux/files/usr`.

| Package | S42 version | Architecture | Scope |
| --- | --- | --- | --- |
| `vulkan-wrapper-android` | `26.3.0+alroot.s42.1` | `aarch64` | Patched Android Vulkan Wrapper, X11 and Wayland WSI |
| `mesa` | `27.0.0~devel+alroot.s42.1` | `aarch64` | Coherent EGL, GLX, GBM, Gallium and DRI runtime |
| `mesa-dev` | `27.0.0~devel+alroot.s42.1` | `all` | Matching Mesa development headers |
| `vulkan-tools` | `1.4.365+alroot.s42.1` | `aarch64` | Accepted S39 coalesced `vkcube`; other installed package payload preserved |
| `mesa-demos` | `9.0.0-7+alroot.s42.1` | `aarch64` | Accepted S40 coalesced `glxgears`; other installed package payload preserved |

The Mesa source version reported by the library is `27.0.0-devel`; its Debian
package version uses `~devel` as shown above.

| Input | Reviewed identity |
| --- | --- |
| Wrapper reference commit | `b3b32abb82af4d374fd35c7192a7f524a5fc8bd3` |
| S41 Wrapper edge-padding patch SHA-256 | `a077c1aae0df79daef60ab782a64490dc62cd8d5d54af38419b60281e4525a6e` |
| Resulting Wrapper `wsi_common.c` SHA-256 | `dcffb2d95a8b493580856ae1ca0726d8a15996ad7f53530cfe7e85fa4af9af26` |
| Wrapper `wsi_common_x11.c` SHA-256 | `49a887ba8fbd34fa40ad518086e82f89aef26b1c996863ba3d97823737ad39db` |
| Mesa source commit | `6f90e92dfdacc4759d8e9ccb328805d0f0b4439b` |
| PR25 recipe reference | `d03437f224ce3a85cc7f719bd79bb9e5cc2e6237` |
| Accepted `vkcube` `cube.c` SHA-256 | `b396d52524270fce6791bbc32648eb7ac05104986f0dcce3e264399b18503059` |
| Accepted coalesced `glxgears` source SHA-256 | `d4ca3fe86499fc4618dcba03332351d99e35bcee281c6dac60a0904f0f6ac3a3` |
| Accepted `glxgears` ELF SHA-256 | `4f5444726d455d522ebaf37b17760febc028db4f6653dcd237517913c836e086` |
| Pre-S42 project reference | `9640c231978b2045b97c5412a7a7c247dc04cc19` |

The [Wrapper builder](../tools/build-s42-wrapper-deb.py) exports a fresh tree
from the reference commit, copies the accepted private source overrides,
and applies the pinned S41 padding-edge fix. It records the complete overlay
hashes. It does not build directly in, or modify, the original lab checkout.
The package ELF is rebuilt and staged without private Android-stub search
paths; its installed hash comes from the new package manifest, not from the
old private S41 ELF.

The [Mesa builder](../tools/build-s42-mesa-debs.py) creates a detached source
worktree at the pinned commit. It applies the 14 PR25 patches and the Alroot
`0001-zink-kopper-ahb-wayland-no-rendernode.patch` and
`0003-zink-optional-driconf-kopper.patch` patches. The PR25 Wayland device
setup conflict is merged explicitly: Android retains the PR25 software
device setup while the non-Android path retains the DRM-less Kopper fallback.
The builder compares physical tracked overrides against the accepted private
Mesa 27 source so a later accepted source fix cannot silently be dropped.
See [the PR25 source contract](mesa27-pr25-revalidation.md) for that history.

The [demo/rollback builder](../tools/build-s42-validation-debs.py) keeps
ownership of `bin/vkcube` in `vulkan-tools` and `bin/glxgears` in `mesa-demos`.
It retains every other file from the verified pre-S42 package payload.
The new `vkcube` is built with **XCB, Xlib, Wayland and display WSI** enabled,
and its help output is checked for all four. Its ELF can therefore differ
from the narrower private S39 build even though the accepted event-loop
source is pinned. `glxgears` is rebuilt and checked against the accepted S40
ELF hash.

## Installed layout and runtime coherence

| Installed path relative to `PREFIX` | Owner / purpose |
| --- | --- |
| `lib/libvulkan_wrapper.so` | `vulkan-wrapper-android`, production Wrapper library |
| `share/vulkan/icd.d/wrapper_icd.aarch64.json` | Wrapper ICD; `library_path` is the absolute canonical library path above |
| `lib/libGLX_mesa.so`, `lib/libEGL_mesa.so` | Mesa GLVND vendor libraries, with matching SONAME aliases |
| `lib/libgallium-27.0.0-devel.so` | Shared Mesa 27 Gallium runtime |
| `lib/libgbm.so*`, `lib/gbm/dri_gbm.so` | Matching Mesa GBM runtime |
| `lib/dri/` | Matching Zink, KGSL, software and DRI-loader files/aliases |
| `share/glvnd/egl_vendor.d/50_mesa.json` | Canonical installed Mesa EGL vendor selection |
| `include/` | Matching headers, split between the existing `mesa` and `mesa-dev` ownership conventions |
| `bin/vkcube`, `bin/glxgears` | Accepted coalesced event-loop demo builds |
| `share/doc/vulkan-wrapper-android/alroot-build.json` | Wrapper version, source/overlay and installed library/ICD provenance |
| `share/doc/mesa/alroot-s42-build.json` | Mesa source, options, runtime files, symlinks and development-file hashes |
| `share/doc/vulkan-tools/alroot-build.json`, `share/doc/mesa-demos/alroot-build.json` | Demo source and installed binary hashes |

Mesa EGL, GLX, Gallium, GBM and DRI components come from **one build**, with
`zink,freedreno,softpipe,llvmpipe`, `freedreno-kmds=msm,kgsl`, X11 and Wayland,
GLVND, and shared LLVM enabled. The build requires the installed LLVM 21
toolchain and records its version. The package declares `libllvm (<< 22)`;
the install gate must also confirm that each ELF dependency resolves against
the installed runtime. LLVM itself is not replaced by this recipe. Mixing a
private no-LLVM Gallium library with an independently built GLX/EGL frontend
is not the installed configuration.

Both `mesa` and `mesa-dev` are installed at the S42 version. The development
package is `Architecture: all`; the launcher runtime check intentionally
checks the architecture of `mesa` and the Wrapper, which are `aarch64`.
GLVND's public dispatch libraries remain owned by the existing `libglvnd`
packages. The build uses Android API 35 native ELF TLS and does not apply the
old Mesa 26.3 emulated-TLS export patch.

The Mesa package build has `vulkan-drivers=`. Existing
`mesa-vulkan-icd-freedreno` and software Vulkan ICD packages are separate
owners and are not replaced with Mesa 27 ICDs by installing this `mesa`.
In particular, a test of the existing installed Freedreno ICD must not be
reported as a test of Mesa 27 Turnip without independently verifying its
version and provenance.

The builders create artifacts below
`.source/gpu-stage1/s42-default-graphics/` using `DESTDIR` and owned staging
directories. Those are build/receipt locations. Default clients use the
installed prefix paths, with no private Mesa `LD_LIBRARY_PATH` or
`LIBGL_DRIVERS_PATH`. A blanket `LD_LIBRARY_PATH=$PREFIX/lib` is also avoided:
the referenced `tx11start` explicitly removes it for Android vendor Vulkan
loading. No Android stub libraries are installed.

## Launcher policy and optional Android Activity boundary

[graphics-defaults.sh](../../termux/graphics-defaults.sh) supplies pure shell
functions shared by [awstart](../../termux/awstart) and
[alroot-wayland](../../termux/alroot-wayland). Sourcing the file does not
change the environment. The policy is applied at the following boundaries:

1. `awstart` defaults to `xfce-shm-direct` and starts the session from the
   native Termux shell, the same mode used by the accepted S39 stage starter.
   It still opens the Android helper Activity. Before a start or restart it
   selects the default server and validates the installed package versions,
   manifests, runtime hashes and canonical ICD path. A mismatch prevents
   `--restart` from stopping the existing desktop.
2. `--topapp` remains an explicit Activity-spawn option. That launch boundary
   intentionally discards caller environment variables.
   `alroot-wayland start_session` resolves the same server policy and repeats
   the package check, so it works with both the native environment and a
   sanitized Activity environment. Exporting variables only in the original
   Termux shell is insufficient for the explicit Activity route.
3. Inside the XFCE child, just before its private `dbus-run-session`, the
   policy installs the client environment. XFCE applications and session
   D-Bus activation therefore inherit the same defaults, while the server
   retains its own renderer environment.

During S42 deployment, ActivityManager reported delivery of the Activity-open
intent, but the session launch inbox produced no acknowledgement. The inbox
requires both Activity window focus and a running helper service;
ActivityManager reporting an already topmost
Activity is not proof that those conditions were met or the request was
consumed. Ordinary `awstart` therefore uses the established native launch
path. This does not weaken the Activity inbox checks, modify the APK, or
silently retry an accepted request through a second path.

Native launch does not establish top-app CPU scheduling: child processes
inherit the native Termux launch policy, which can have different CPU
affinity. The accepted S39 starter already uses `--no-topapp`. The shared
policy's sanitized-environment behavior is covered by fixtures; an actual
Activity-launched S42 desktop is **not confirmed** by that fixture result.

| Process scope | Graphics selection |
| --- | --- |
| Android helper | Existing Android Vulkan presentation path |
| Xwayland server | Validated S39 KGSL surfaceless renderer and server-only dual ABI fence preload |
| Termux Bionic XFCE applications | Installed Vulkan Wrapper + Mesa 27 Zink / Kopper |
| Explicit `xfce-labwc` profile | Existing nested legacy route; no S42 direct-profile preflight or client policy |
| Explicit private Xwayland selection | Existing stage-specific server validator; direct XFCE still gets the installed client baseline |

The default S39 server is
`$HOME/alroot_wayland/.source/gpu-stage1/s39-live-resize/runtime/Xwayland`,
SHA-256
`c5f6c9a512f84e31f60ec52a9f10e689e0746f831049e57a4223ade2750f3899`.
The existing server guard and required S30/S32/S36/S37/S38/S39 flags remain
in force. S42 packages the requested drivers and demo owners; it does not
repackage this validated Xwayland binary. Because the server loads Mesa EGL
and Gallium too, the staged Mesa 27 runtime must pass an isolated S39
compatibility test before replacing the installed Mesa 26.3 runtime.

The direct XFCE environment exports:

```sh
VK_DRIVER_FILES="$PREFIX/share/vulkan/icd.d/wrapper_icd.aarch64.json"
VK_ICD_FILENAMES="$VK_DRIVER_FILES"
MESA_LOADER_DRIVER_OVERRIDE=zink
GALLIUM_DRIVER=zink
LIBGL_KOPPER_DRI2=1
S24_WRAPPER_XCB_AHB_PAD_HEIGHT=1
S26_WRAPPER_XCB_RETRY_TRANSIENT=1
ALROOT_CLIENT_GRAPHICS=wrapper-zink
```

The client boundary clears private loader paths, inherited software/GL
version overrides, server renderer variables, server preloads and old
test-injection flags. Real terminal validation then exposed a separate
startup override: the user's Bash startup includes reset `DISPLAY` to `:0`.
The scoped default-terminal hook below restores the authenticated Alroot
endpoint after those settings have loaded. Global shell/configuration files
and the user's `SHELL` selection are preserved.

`awstart --status` reports the active XFCE leader's graphics tag and ICD.
The tag describes the desktop baseline, not proof of the renderer used by
every child process. An already running direct session is reopened without
being restarted; if it predates the new graphics tag, `awstart` reports that
`awstart --restart` applies the installed defaults. `--open` and `--stop`
remain available independently of the installed-version gate.

### Default terminal startup after user Bash settings

The initial terminal probe failed because the user Bash startup chain
sources `.shell_rc_content`, whose line 9 exports `DISPLAY=:0`.
The private VTE terminal already inherited the correct Wrapper/Zink
selectors; the failed `glxinfo` connection was an endpoint override, not a
graphics-driver selection failure. The final `live-terminal/summary.json`
passes after the project-scoped correction described here.

The installed terminal is **xfce4-terminal 1.2.0**. Its exact upstream
[source archive](https://archive.xfce.org/src/apps/xfce4-terminal/1.2/xfce4-terminal-1.2.0.tar.xz)
has SHA-256
`6874c7b975cc3dc3bd636d57ffec723de7458202defe65377593d3a7e0734b94`.
The source establishes the relevant behavior:

- `terminal-preferences.c`, `terminal_preferences_init` and
  `terminal_preferences_get_property`: preferences use the `xfce4-terminal`
  Xfconf channel and slash-prefixed property names. Legacy `terminalrc` is
  imported only if that channel is absent.
- `terminal-screen.c`, `terminal_screen_get_child_command`: an explicit
  per-tab command takes precedence over the configured default command.
  `terminal-options.c` places both `--command` and `--execute` in that
  per-tab command field.
- `terminal-window.c`, `terminal_window_action_new_tab`: a new tab creates
  a fresh screen and runs the same child-command selection, so the default
  preference applies to new tabs as well as new windows.

`configure-xfce-profile.py --s42-terminal` updates only
`/run-custom-command=true` and `/custom-command` in the **private** channel.
The command is the absolute path to
[alroot-terminal-shell.sh](../../termux/alroot-terminal-shell.sh). Other
terminal properties, including font, colors, transparency and geometry,
are preserved. `alroot-wayland` requests this setting for the direct profile
before starting its private session bus. For an already running session the
same two properties can be set through that session's private D-Bus without
restarting XFCE; an offline XML edit alone must not be assumed to update a
running Xfconf daemon.

The default-command wrapper preserves `SHELL`. For native Bash in the tagged
S42 private session it executes Bash with
[alroot-terminal.bashrc](../../termux/alroot-terminal.bashrc) as its rc file.
That file captures the matching private display/authentication paths, sources
the user's `.bashrc` at top level (including its own included files), then
restores the captured endpoint and applies the installed Wrapper/Zink client
policy once. It requires the S42 tag, private config directory, native Bash,
a readable matching authority file and a matching non-`:0` session display.
Non-Bash shells are executed directly. Explicit terminal commands retain
upstream precedence and do not enter this default-command wrapper.

The hook does not install `PROMPT_COMMAND`, a debug trap or a recurring
driver reset. Per-command Turnip/KGSL overrides after startup still work.
The terminal parent retains its already mapped private VTE library; the
interactive Bash child clears the inherited VTE-only `LD_LIBRARY_PATH` when
it applies the normal client policy. The existing VTE terminal wrapper is
unchanged. Global Bash files, aliases and terminal configuration are not
rewritten.

## Per-command client tests

Run these from a terminal in the authenticated Bionic XFCE session so that
`DISPLAY` and `XAUTHORITY` already refer to that desktop. They do not restart
Xwayland or change the shell's later commands. Check the actual renderer,
ICD/library mappings and presentation results; the commands below are test
entry points, not acceptance claims.

### Installed Wrapper / Zink baseline

With the inherited default environment:

```sh
glxinfo -B
glxgears
vkcube --wsi xcb
```

To select the installed baseline explicitly for one GLX command:

```sh
env -u LD_LIBRARY_PATH -u LD_PRELOAD -u LIBGL_DRIVERS_PATH \
    -u LIBGL_ALWAYS_SOFTWARE \
    VK_DRIVER_FILES="$PREFIX/share/vulkan/icd.d/wrapper_icd.aarch64.json" \
    VK_ICD_FILENAMES="$PREFIX/share/vulkan/icd.d/wrapper_icd.aarch64.json" \
    MESA_LOADER_DRIVER_OVERRIDE=zink GALLIUM_DRIVER=zink \
    LIBGL_KOPPER_DRI2=1 S24_WRAPPER_XCB_AHB_PAD_HEIGHT=1 \
    S26_WRAPPER_XCB_RETRY_TRANSIENT=1 glxinfo -B
```

Use the installed `glxgears` and `vkcube` for continuous-resize acceptance:
the coalesced event handling is part of the accepted behavior. The rebuilt
`vkcube` also retains `--wsi wayland`, but a Wayland test needs its own valid
Wayland endpoint/environment and separate presentation evidence.

### Turnip Vulkan, or Mesa 27 Zink on Turnip

Set **both** Vulkan loader selectors. Leaving inherited `VK_DRIVER_FILES`
pointing at Wrapper while changing only `VK_ICD_FILENAMES` does not select
the intended alternative.

The following selects the existing installed Freedreno ICD. Its pre-S42
package version is `26.3.0`; verify the actual installed version before
labelling a result. A separately built Bionic Turnip ICD can be substituted
by using its absolute JSON path in both assignments without replacing the
installed Wrapper/Mesa baseline.

```sh
env -u LD_LIBRARY_PATH -u LD_PRELOAD \
    -u S24_WRAPPER_XCB_AHB_PAD_HEIGHT -u S26_WRAPPER_XCB_RETRY_TRANSIENT \
    VK_DRIVER_FILES="$PREFIX/share/vulkan/icd.d/freedreno_icd.aarch64.json" \
    VK_ICD_FILENAMES="$PREFIX/share/vulkan/icd.d/freedreno_icd.aarch64.json" \
    vkcube --wsi xcb

env -u LD_LIBRARY_PATH -u LD_PRELOAD -u LIBGL_DRIVERS_PATH \
    -u LIBGL_ALWAYS_SOFTWARE \
    -u S24_WRAPPER_XCB_AHB_PAD_HEIGHT -u S26_WRAPPER_XCB_RETRY_TRANSIENT \
    VK_DRIVER_FILES="$PREFIX/share/vulkan/icd.d/freedreno_icd.aarch64.json" \
    VK_ICD_FILENAMES="$PREFIX/share/vulkan/icd.d/freedreno_icd.aarch64.json" \
    MESA_LOADER_DRIVER_OVERRIDE=zink GALLIUM_DRIVER=zink \
    LIBGL_KOPPER_DRI2=1 glxinfo -B
```

After identification, use the same environment with `glxgears` for the Zink
presentation/resize test. A separate Turnip build must provide a coherent
Bionic ICD and dependencies; a glibc ICD cannot be loaded into these native
Termux processes.

### Direct Freedreno KGSL OpenGL

Clear inherited Vulkan/Kopper selection and select the installed KGSL DRI
driver for just the test process:

```sh
env -u LD_LIBRARY_PATH -u LD_PRELOAD -u LIBGL_DRIVERS_PATH \
    -u LIBGL_ALWAYS_SOFTWARE -u VK_DRIVER_FILES -u VK_ICD_FILENAMES \
    -u LIBGL_KOPPER_DRI2 \
    -u S24_WRAPPER_XCB_AHB_PAD_HEIGHT -u S26_WRAPPER_XCB_RETRY_TRANSIENT \
    MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno \
    glxinfo -B
```

Use the same environment with `glxgears` after renderer identification.
This is a direct-client compatibility experiment. The server's success
with its custom KGSL/AHB output does not establish support for an ordinary
KGSL GLX or Wayland client's buffer export path. Any additional export/shim
experiment must record its scoped flags and actual import/presentation
evidence separately.

The exact-version gate runs at desktop start/restart and doctor, **not**
when starting these applications. It does not reset per-command overrides.
There is therefore no need to replace packages or weaken the gate for the
tests above. Replacing `mesa` or `vulkan-wrapper-android` with a different
version intentionally prevents a subsequent default direct start until that
new baseline is reviewed and the policy is updated. `mesa-dev`, demo and
LLVM coherence are installation/validation checks; the runtime launcher
does not pin unrelated driver ICD package versions.

## Bionic / glibc boundary

The two library ABIs remain separate. The installed Wrapper/Mesa files in
the Termux prefix are for Bionic. Debian/PRoot and native glibc launchers
retain their own driver libraries, ICD JSON paths, loader environment and
authentication requirements; this promotion does not expand the historical
S23 native-glibc acceptance scope.

[aw-proot](../../termux/aw-proot) recognizes the new desktop baseline tag.
Before handing control to the guest launcher, it removes inherited Vulkan
ICD paths under the Bionic prefix and the added Zink/Kopper/Wrapper
selectors. This also handles a caller that supplies only a guest
`VK_ICD_FILENAMES`: inherited Bionic `VK_DRIVER_FILES` is removed so it cannot
take precedence. An explicit guest ICD outside the prefix and unrelated
explicit settings, such as a guest software-rendering override, survive.
Callers without the new tag keep the previous behavior.

The dual ABI fence interposer remains scoped to the Xwayland server. It is
not added globally to the Bionic desktop or guest clients. Launching a guest
outside the project boundary still requires that launcher's own environment
handling; no global user profile is changed to mask a missing boundary.

## Build, install gate and rollback

The scripts below build/stage packages and record receipts; they do not
install packages or stop the desktop. Run the rollback/demo builder while
the verified pre-S42 packages are still installed. It refuses to overwrite
its baseline if the installed package set no longer matches.

```sh
cd "$HOME/alroot_wayland"
python graphics/tools/build-s42-validation-debs.py --jobs 2
python graphics/tools/build-s42-wrapper-deb.py --jobs 2
python graphics/tools/build-s42-mesa-debs.py --jobs 2
```

Before installation, retain verified copies and hashes of the exact
pre-S42 launcher files and installed command/link arrangement, alongside
the package rollback receipt. The reviewed launcher reference is project
commit `9640c231978b2045b97c5412a7a7c247dc04cc19`; an uncommitted local launcher
change must be captured as bytes rather than assumed to equal that commit.

The promotion gate requires package payload/ownership/dependency checks,
isolated S39 compatibility using the staged coherent Mesa 27 runtime, and
Wrapper/Zink client presentation checks. Only then install all five S42
packages through `dpkg`, deploy the shared policy and launchers together,
and deliberately restart the project session from a separate native
Termux shell. Record the actual commands, artifact hashes and results in
the validation receipt. Do not infer a running process's library version
from the on-disk replacement alone: verify its mappings after restart.

The rollback builder saves the exact verified pre-S42 owned payloads,
metadata and symlink targets in `.source/gpu-stage1/s42-default-graphics/rollback/`.
It checks `dpkg --audit` and `dpkg --verify` first. The rollback package set is:

| Package | Rollback version | Rollback architecture |
| --- | --- | --- |
| `mesa` | `26.3.0` | `aarch64` |
| `mesa-dev` | `26.3.0` | `all` |
| `vulkan-wrapper-android` | `26.3.0` | `aarch64` |
| `vulkan-tools` | `1.4.365` | `aarch64` |
| `mesa-demos` | `9.0.0-7` | `aarch64` |

Rollback order, from a separate native Termux shell:

1. Stop only the project-owned session with `awstart --stop` and verify its
   processes have exited. This action does not require the new package
   versions to pass preflight.
2. Check the saved rollback artifact hashes against `rollback/packages.json`,
   then restore **all five exact saved packages** in one `dpkg -i` invocation:

   ```sh
   s42_rollback="$HOME/alroot_wayland/.source/gpu-stage1/s42-default-graphics/rollback/packages"
   dpkg -i \
       "$s42_rollback/mesa_26.3.0_aarch64.deb" \
       "$s42_rollback/mesa-dev_26.3.0_all.deb" \
       "$s42_rollback/vulkan-wrapper-android_26.3.0_aarch64.deb" \
       "$s42_rollback/vulkan-tools_1.4.365_aarch64.deb" \
       "$s42_rollback/mesa-demos_9.0.0-7_aarch64.deb"
   ```

3. Restore the captured pre-S42 bytes and executable modes for `termux/awstart`,
   `termux/alroot-wayland`, `termux/aw-proot` and
   `termux/configure-xfce-profile.py`, their recorded installed command/link
   arrangement, and the saved private terminal channel. Remove new
   `graphics-defaults.sh`, `alroot-terminal-shell.sh` and
   `alroot-terminal.bashrc` only where the receipts confirm they did not exist
   before S42. Do not start a desktop
   between package rollback and launcher restoration: the S42 launcher
   correctly rejects the restored old package versions.
4. Verify the restored package and launcher hashes and run `dpkg --audit`.
   The old `awstart` default is the earlier nested profile. Re-entering the
   accepted S39 server route uses the existing explicit
   `graphics/tools/start-s39-live-resize.sh --restart` procedure; its old
   private client/demo test choices remain separate from that server start.

Rollback uses saved package artifacts and launcher copies; it does not
depend on repository package availability, overwrite the entire dpkg
database, or reset unrelated repository changes.

## Validation record — S42 receipts

Receipt paths in this section are relative to
`$HOME/alroot_wayland/.source/gpu-stage1/s42-default-graphics/`.
The timestamped isolated test receipts below were recorded on 2026-11-01.

### Packages and rollback

All five packages built successfully at the versions in the package table.
`promotion-preflight.json` records **419 regular-file/symlink checks**,
resolved ELF `NEEDED` dependencies, and an apt transaction containing only
the five intended upgrades with no removals. The unrelated Freedreno and
software Vulkan ICD hashes remained unchanged. `install-receipt.json`
records the installed versions and successful `dpkg --audit` and
`dpkg --verify` checks. Its initial `INSTALLED_AND_DPKG_VERIFIED` state was
advanced to **ACTIVE_VERIFIED** after the final live-session checks.

The complete build/artifact inventories are in
`wrapper/build-manifest.json`, `mesa/package-manifest.json` and
`validation-debs.json`. Selected exact output identities are:

| Output | SHA-256 |
| --- | --- |
| `mesa/packages/mesa_27.0.0~devel+alroot.s42.1_aarch64.deb` | `87b07712d61616ae978bbaaf95f1d690820a3882f9c55b0ff8cfb6a351a345cf` |
| `mesa/packages/mesa-dev_27.0.0~devel+alroot.s42.1_all.deb` | `6df20b4ab28b44e73a596e482e7dcdc66d756af47f07b35a5384810c1b7db4f7` |
| `demos/packages/vulkan-tools_1.4.365+alroot.s42.1_aarch64.deb` | `c6926d52fea7b199d4c2263a34e0abe17972bcf76949c5bc3ace3554f4610dcc` |
| Installed `$PREFIX/lib/libvulkan_wrapper.so` | `e78fff330f54d4666937d1800c9c2c192afab34be5aba153fab36046937dbe5a` |
| Installed `$PREFIX/lib/libGLX_mesa.so` | `4d42c98f17a9f76ee2a585d7edf295792bd6d6e22254e17b4905cd36af5ceb53` |
| Installed `$PREFIX/lib/libgallium-27.0.0-devel.so` | `f1cdff782c991e3df4096be7183353339859952d0d06547697627e83e1aab0e0` |
| Installed `$PREFIX/bin/vkcube` ELF, all four WSI backends | `ae4f3ee457da41c25793459cb567a1cc941dc8e25260947d6421cb802f01fe6c` |
| Installed `$PREFIX/bin/glxgears` ELF | `4f5444726d455d522ebaf37b17760febc028db4f6653dcd237517913c836e086` |

Exact rollback package artifacts are recorded in `rollback/packages.json`.
The pre-install launchers and their arrangement are captured under
`rollback/launchers/`, with manifest
`rollback/launchers/manifest.json` SHA-256
`c8c5e4d206b16143130015fee17d8918fec041ec5addb93a147c071eb924099a`.
The former protected-file snapshot is retained as
`rollback/termux-x11-protected.pre-s42.sha256`; intentional S42 package
changes are validated against the new manifests rather than treated as
unexplained differences from that archived snapshot.
The terminal correction also records the original private channel and
global Bash/include/alias/profile/terminal XML hashes in
`rollback/terminal-profile/baseline.json`.
The pre-edit `configure-xfce-profile.py` and its source inventory are saved
in `rollback/terminal-profile/` and `rollback/terminal-profile/source-files.json`;
the original launcher manifest with SHA-256 `c8c5e4d2...` remains unchanged.
After the old protected-file guard was archived, only its five authorized
graphics-path entries were updated. The current guard's **8/8 checks pass**.

### Staged S39 and X11 client gates

The isolated tests mapped the selected package libraries and checked their
exact hashes. They preserved the then-running user session during the test.
The Present metrics below measure received completion timing during the
owned-window resize workload, not FPS or physical scanout timing.

| Gate | Result | Receipt below `session-tests/logs/` |
| --- | --- | --- |
| S39 server with coherent staged Mesa 27 EGL/Gallium/GBM/DRI | **PASS** | `staged-vkcube-20261101-002832-4024/summary.json` |
| Staged Wrapper `vkcube` | **PASS** — 527 Configure events; maximum Present completion receive gap 105 ms; no empty core COPY bins; fixed-size animation check passed; client exit 0 | `staged-vkcube-20261101-002926-7175/summary.json` |
| Staged Mesa 27 Zink `glxgears` | **PASS** — 521 Configure events; maximum Present completion receive gap 98 ms; no empty core COPY bins; fixed-size animation check passed; client exit 0 | `staged-zink-20261101-003003-9531/summary.json` |

The same owned-window cases subsequently passed with the **installed
canonical runtime**, independently of the staged-library tests:

| Installed client | Result | Receipt below `session-tests/logs/` |
| --- | --- | --- |
| Wrapper `vkcube` | **PASS** — 523 Configure events; maximum Present completion receive gap 140 ms; no empty core COPY bins; fixed-size animation passed; exit 0; user session unchanged | `installed-vkcube-20261101-004607-32653/summary.json` |
| Mesa 27 Zink `glxgears` | **PASS** — 525 Configure events; maximum Present completion receive gap 98 ms; no empty core COPY bins; fixed-size animation passed; exit 0; user session unchanged | `installed-zink-20261101-004624-2177/summary.json` |

The rebuilt Wrapper padding proof is
`ahb-padding/logs/gpu-proof-20261101-003324-14223/summary.json`:
**OBSERVATION_COMPLETE**, exit 0, with **5/5 source-padding and window-row
match cases** and unchanged shared inputs. Its scope is the native
source/window row values after the observed copy/idle events. The physical
display was **NOT_MEASURED**; this is not a new human visual acceptance or a
claim about unobserved memory initialization.

### Native Wayland gates

`wayland-smoke/vkcube-summary.json` records **120 frames PASS** for the
rebuilt Wrapper/demo path. `wayland-smoke/zink-summary.json` records
**60 swaps PASS**, Mesa 27 / Zink / Adreno 840 identification, mapping of the
staged EGL runtime, four AHB buffers and zero SHM buffers. Both runs have
host log evidence for **AHB → VkImage import and presentation**. These are
actual Wayland runtime checks in addition to the demo build's preservation
of all four WSI backends; neither result is a human physical-display review.

### Installed launcher and active desktop

The [launcher fixture suite](../tests/test-s42-graphics-policy.py) passed
**15/15 locally and 15/15 on native Termux** after the fixture shell paths
and atomic path relocation were made portable. It covers sanitized Activity
environment policy, server/client separation, rejection before a failed
restart can stop the desktop, explicit old profiles and guest ICD overrides.
Shell syntax checks also passed for the changed launchers and terminal hook.
These fixtures do not
establish that Android accepted an Activity inbox request.
The focused [terminal policy fixtures](../tests/test-s42-terminal-policy.py)
also passed **8/8 locally and 8/8 on native Termux**, using actual PTY-backed
Bash startup. They cover
the included-file display override, top-level user declarations, one-time
repair, later per-command ICD selection, non-Bash shells, private Xfconf
preference preservation and unchanged global files.

| Installed-session gate | Result / receipt |
| --- | --- |
| Explicit native `--no-topapp` start | **PASS** — `native-default-start.log`; server maps canonical Mesa 27 EGL/Gallium; XFCE has `wrapper-zink` and both canonical Wrapper ICD variables |
| Plain `awstart --restart` with native mode as the default | **PASS** — `default-native-restart.log`; Xwayland PID 30419 and XFCE leader PID 30509 on `:98`; Wrapper/Zink tag and canonical ICD; Fcitx IME and private VTE ready |
| Native launcher fixtures | **15/15 PASS** — native test run 156 |
| Updated P01 launcher smoke | **PASS** — `p01-smoke.log`, including native/direct default and explicit legacy/Activity dry-runs; signing preserved |
| Explicit Activity-spawn route | **NOT CONFIRMED** — initial S42 request received no inbox ACK; sanitized policy is covered by fixtures only |
| Real newly opened default XFCE terminal after the scoped hook | **PASS** — `live-terminal/summary.json`; ordinary default terminal command with no `--command`/`--execute`, correct authenticated `:98` endpoint and installed Wrapper/Zink selection |
| Isolated resize/render cases using installed canonical client libraries | **PASS** — both installed-mode summaries listed above |
| Human physical-display review after S42 promotion | **NOT RERUN** — prior S41 visual acceptance remains historical evidence |

The final live-terminal probe used an inherited, test-only `PROMPT_COMMAND`
to collect results after normal default-shell startup. It did not supply
graphics overrides or replace the terminal's default command, and no
production `PROMPT_COMMAND` hook was installed. The shell had `DISPLAY=:98`,
the correct project `XAUTHORITY`, the Wrapper/Zink tag, Kopper and the S24/S26
flags, and both canonical Vulkan selectors. `LD_LIBRARY_PATH`, `LD_PRELOAD`
and GL version overrides were absent in the shell. The parent terminal
continued to map the private VTE library.

Inside that real terminal, `glxinfo` identified direct Mesa 27 / Zink /
Adreno 840 rendering. Installed `vkcube` completed **180 frames and exited
0**, with the canonical Wrapper ELF mapped at the recorded hash. Installed
`glxgears` mapped the canonical Wrapper, GLX and Gallium files at the hashes
above, then was stopped by the probe's own bounded SIGTERM. Its normal
Escape/exit-0 path had already passed in the installed isolated case. The
terminal itself exited 0, and the current desktop remained unchanged during
this probe.

`final-validation.json` records **ACTIVE_VERIFIED**. The final readback
confirmed Xwayland PID 30419 using the canonical Mesa 27 EGL/Gallium hashes,
XFCE leader PID 30509 and the ready desktop on `:98`. Global `.bashrc`,
`.shell_rc_content`, `.aliases`, `.profile` and terminal XML hashes match
their saved values. Comparing the parsed private terminal XML to its backup
shows exactly the two intended custom-command property changes. The updated
`install-receipt.json` includes the terminal backup references and the final
validation file's SHA-256.

Turnip and direct KGSL experiments are subsequent work. S42 baseline
acceptance alone does not mark either route as passing.
