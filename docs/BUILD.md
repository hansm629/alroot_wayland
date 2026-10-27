# Build and local signing

Workflow: .github/workflows/helper-apk.yml. Versions and hashes: upstream.lock.
GitHub Actions builds an arm64 release; without optional signing secrets, its
app-release-unsigned.apk is **not installable**.

To download the actual CI run and sign on S26U with the existing compatible
Termux-X11 keystore (no private-key upload):

    gh run list -R hansm629/alroot_wayland --workflow helper-apk.yml
    mkdir -p ~/alroot_wayland/artifacts
    gh run download RUN_ID -R hansm629/alroot_wayland -D ~/alroot_wayland/artifacts
    bash ~/alroot_wayland/tools/sign-local.sh /path/to/app-release-unsigned.apk

The local signer refuses to sign unless the candidate cert matches the
installed Termux APK. Android installation still requires user approval.

Optional protected CI secrets: ALROOT_SIGNING_KEYSTORE_B64,
ALROOT_SIGNING_STORE_PASSWORD, ALROOT_SIGNING_KEY_ALIAS,
ALROOT_SIGNING_KEY_PASSWORD. They are never used for pull requests.

TargetSdk 28 is deliberate for private Termux executable entrypoints on
Android. Android compatibility and minimum-target restrictions require
revalidation before shipping.
