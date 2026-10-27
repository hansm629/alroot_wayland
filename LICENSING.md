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
