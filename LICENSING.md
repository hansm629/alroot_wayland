# Licenses and provenance

This APK includes portions of DroidDeck from commit
0f839bef5d5cf109af614d0ff15844c95a8e16d0, licensed under GPLv3.
See LICENSES/DroidDeck-GPL-3.0.txt. Source code is in native/waylandcomp,
app/src/main/java/com/alroot/wayland/WaylandCompositor.java, and native/shaders.
Build-time standalone integration, locally renamed JNI/runtime identifiers,
and Android front-end are Alroot changes. Original DroidDeck copyright and
GPLv3 provenance are preserved in LICENSES/ and this document.

native/adrenotools is copied from the same pinned upstream source snapshot;
its separate upstream license is preserved under LICENSES/adrenotools-LICENSE.
Wayland generated protocol interfaces retain their upstream copyright notices.
The native prebuilt Wayland server and FFI libraries are copied as-is from
DroidDeck's pinned repository; their SHA256 checksums are in upstream.lock.

No GPLv3 code is embedded into external Labwc or wlroots GPLv2-only modules.
The full source for shipped APK-specific modifications and reproducible build
scripts is this repository. Third-party binary source correspondence remains
to be audited before public release or redistribution beyond development CI.

Project-private wlroots is compiled as a separate bionic library from upstream
0.18.2; its permissive license is retained in LICENSES/wlroots-MIT.txt.

The private Labwc output-resize utility uses the wlr-output-management-v1
protocol XML in termux/protocols/, taken from wlroots 0.18.2 (copyright
Purism SPC, permission/notice text included verbatim in that XML). The
client implementation is Alroot project source, generated protocol stubs
are compiled locally with wayland-scanner, and no Termux global library or
original X11 output configuration is modified.

P14 includes an Alroot-only patch to GNOME VTE 0.84.1 (pinned source
commit f2223e45d7f04c5383c59e76772501f0ed4215bc). VTE retains its
LGPL-3.0-or-later, GPL-3.0-or-later and MIT licenses and upstream
copyright notices. The corresponding patch is retained in
termux/patches/vte-0.84.1-caret.patch. The build script fetches and
retains the matching upstream source in .source/p14-vte-src. Only the
Alroot XFCE terminal loads the project-private modified VTE library;
the native Termux libvte, Termux-X11 :0 and Android APK are unchanged.
