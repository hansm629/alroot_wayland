#!/data/data/com.termux/files/usr/bin/bash
# Read-only Xwayland DRI3/GLX/Present check against the existing host session.
# GLX extensions, or a glxinfo renderer string, are NEVER proof of GLAMOR.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
RUNTIME="$ROOT/.source/gpu-stage27-pr25/mesa-private/private-runtime"
OUT="$ROOT/.source/gpu-stage27-pr25/results"
mkdir -p "$OUT"
: "${ALROOT_S14_XWAYLAND_DISPLAY:=:90}"
case "$ALROOT_S14_XWAYLAND_DISPLAY" in :90|:0) ;; *) echo 'S14_XWAYLAND_DISPLAY=UNSUPPORTED' >&2;exit 2;; esac
log="$OUT/xwayland-${ALROOT_S14_XWAYLAND_DISPLAY#:}.log"
if [[ "$ALROOT_S14_XWAYLAND_DISPLAY" == :90 ]]; then
 export XAUTHORITY="$HOME/.alroot-wayland/Xauthority"
fi
if ! DISPLAY="$ALROOT_S14_XWAYLAND_DISPLAY" timeout 5s xdpyinfo -queryExtensions > "$log" 2>&1; then
 echo "S14_XWAYLAND_DISPLAY=$ALROOT_S14_XWAYLAND_DISPLAY"
 echo 'S14_XWAYLAND_SERVER=UNAVAILABLE'
 exit 2
fi
echo "S14_XWAYLAND_DISPLAY=$ALROOT_S14_XWAYLAND_DISPLAY"
for ext in DRI3 GLX Present; do
 if grep -qE "^[[:space:]]+${ext}[[:space:]]" "$log"; then
  echo "S14_XWAYLAND_${ext}=1"
 else
  echo "S14_XWAYLAND_${ext}=0"
 fi
done
if grep -qE '^[[:space:]]+DRI3[[:space:]]' "$log"; then
 echo 'S14_XWAYLAND_DRI3_TRANSPORT=ADVERTISEMENT_ONLY'
else
 echo 'S14_XWAYLAND_DRI3_TRANSPORT=BLOCKED_NO_DRI3'
fi
if [[ -s "$RUNTIME/lib/libgallium-27.0.0-devel.so" &&
     -s "$RUNTIME/lib/libEGL_mesa.so" ]]; then
  egl_log="$OUT/glxinfo-${ALROOT_S14_XWAYLAND_DISPLAY#:}.log"
  DISPLAY="$ALROOT_S14_XWAYLAND_DISPLAY" timeout 10s env \
    -u LIBGL_ALWAYS_SOFTWARE \
    MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno \
    LIBGL_DRIVERS_PATH="$RUNTIME/lib/dri" \
    LD_LIBRARY_PATH="$RUNTIME/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    glxinfo -B > "$egl_log" 2>&1 && rc=0 || rc=$?
  echo "S14_XWAYLAND_SCOPED_GLXINFO_EXIT=$rc"
  grep -E 'direct rendering|OpenGL vendor string|OpenGL renderer string|OpenGL version string|Error|BadValue' "$egl_log" | tail -10 || :
  if grep -q 'Mesa 27\.0\.0-devel' "$egl_log"; then
     echo 'S14_XWAYLAND_PRIVATE_MESA27_GLX=RENDERER_IDENTIFIED_ONLY'
  else
     echo 'S14_XWAYLAND_PRIVATE_MESA27_GLX=NOT_CONFIRMED'
  fi
else
  echo 'S14_XWAYLAND_PRIVATE_MESA27_GLX=NOT_BUILT'
fi
echo 'S14_XWAYLAND_GLAMOR_GPU_PRESENT=NOT_VERIFIED'
echo "S14_XWAYLAND_LOG=$log"
