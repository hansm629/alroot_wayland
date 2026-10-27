#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
[[ "$#" -eq 1 && -f "$1" ]] || { echo 'usage: sign-local.sh unsigned.apk' >&2; exit 2; }
input="$(realpath "$1")"
home=/data/data/com.termux/files/home
repo="$home/alroot_wayland"
existing="$home/termux-x11/app"
keystore="$existing/testkey_untrusted.jks"
gradle="$existing/build.gradle"
[[ -r "$keystore" && -r "$gradle" ]] || { echo 'BLOCKED: no old keystore' >&2; exit 1; }
command -v apksigner >/dev/null || { echo 'Install apksigner first' >&2; exit 1; }
export ALROOT_STORE_PASS ALROOT_KEY_PASS
ALROOT_STORE_PASS="$(sed -nE "s/^[[:space:]]*storePassword[[:space:]]+['\\\"]([^'\\\"]+)['\\\"].*/\\1/p" "$gradle" | head -1)"
ALROOT_KEY_PASS="$(sed -nE "s/^[[:space:]]*keyPassword[[:space:]]+['\\\"]([^'\\\"]+)['\\\"].*/\\1/p" "$gradle" | head -1)"
alias_name="$(sed -nE "s/^[[:space:]]*keyAlias[[:space:]]+['\\\"]([^'\\\"]+)['\\\"].*/\\1/p" "$gradle" | head -1)"
[[ -n "$ALROOT_STORE_PASS" && -n "$ALROOT_KEY_PASS" && -n "$alias_name" ]] || exit 1
apk="$(pm path com.termux | sed -n '1s/^package://p')"
[[ -r "$apk" ]] || { echo 'BLOCKED: missing installed APK' >&2; exit 1; }
installed="$(apksigner verify --print-certs "$apk" | sed -nE 's/^V[0-9] Signer: certificate SHA-256 digest: //p' | head -1)"
candidate="$(keytool -list -v -keystore "$keystore" -storepass "$ALROOT_STORE_PASS" |
    sed -nE 's/.*SHA256: ([A-F0-9:]+).*/\1/p' | head -1 | tr -d ':' | tr 'A-F' 'a-f')"
[[ -n "$installed" && "$installed" == "$candidate" ]] || {
    echo 'BLOCKED: signing cert does not match installed Termux' >&2; exit 1;
}
mkdir -p "$repo/artifacts"
chmod 700 "$repo/artifacts"
output="$repo/artifacts/alroot-wayland-p01-signed.apk"
apksigner sign --ks "$keystore" --ks-key-alias "$alias_name" \
    --ks-pass env:ALROOT_STORE_PASS --key-pass env:ALROOT_KEY_PASS \
    --out "$output" "$input"
unset ALROOT_STORE_PASS ALROOT_KEY_PASS
apksigner verify --print-certs "$output" | grep 'certificate SHA-256 digest' | head -1
sha256sum "$output"
echo "Signed APK: $output"
echo 'Android installer approval still required.'
