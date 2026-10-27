# Licenses and provenance

This APK includes portions of DroidDeck from commit
0f839bef5d5cf109af614d0ff15844c95a8e16d0, licensed under GPLv3.
See LICENSES/DroidDeck-GPL-3.0.txt. Source code is in native/waylandcomp,
app/src/main/java/com/droiddeck/launcher/wayland, and native/shaders.
Build-time standalone integration and Android front-end are Alroot changes.

native/adrenotools is copied from the same pinned upstream source snapshot;
its separate upstream license is preserved under LICENSES/adrenotools-LICENSE.
Wayland generated protocol interfaces retain their upstream copyright notices.
The native prebuilt Wayland server and FFI libraries are copied as-is from
DroidDeck's pinned repository; their SHA256 checksums are in upstream.lock.

No GPLv3 code is embedded into external Labwc or wlroots GPLv2-only modules.
The full source for shipped APK-specific modifications and reproducible build
scripts is this repository. Third-party binary source correspondence remains
to be audited before public release or redistribution beyond development CI.
