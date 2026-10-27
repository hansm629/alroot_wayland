#!/data/data/com.termux/files/usr/bin/bash
# Runs only within Alroot's dbus-run-session. Never touches the user's :0.
set -uo pipefail
PREFIX=/data/data/com.termux/files/usr
HOME=/data/data/com.termux/files/home
STATE="$HOME/.alroot-wayland"
LOGS="$STATE/logs"
if [[ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]]; then
    echo 'Alroot XFCE has no private D-Bus session' >&2
    exit 2
fi
# Unrooted Android has no system_bus_socket. Redirect only the Alroot
# child session's UPower queries to its own project-private bus.
export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
"$PREFIX/bin/python3" "$HOME/alroot_wayland/termux/alroot-upower.py" \
    > "$LOGS/upower.log" 2>&1 &
provider=$!
cleanup() {
    kill -TERM "$provider" 2>/dev/null || true
    wait "$provider" 2>/dev/null || true
}
trap cleanup EXIT
for i in $(seq 1 20); do
    if grep -q '^ALROOT_UPOWER_READY' "$LOGS/upower.log" 2>/dev/null; then
        echo 'Alroot UPower cache registered on private D-Bus'
        break
    fi
    if ! kill -0 "$provider" 2>/dev/null; then
        echo 'WARNING: Alroot UPower provider stopped unexpectedly' >&2
        break
    fi
    sleep .1
done
"$PREFIX/bin/xfce4-session"
