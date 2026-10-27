#!/data/data/com.termux/files/usr/bin/python3
"""
Project-only UPower-compatible battery provider on the Alroot private D-Bus.

The installed xfce4-battery-plugin expects org.freedesktop.UPower, whereas
Android unrooted Termux has no system_bus_socket. Rather than replace the
native XFCE plugin or edit its size/position, supply the small read-only
UPower Device interface from the Android helper's cached BatteryManager.

Scope: the dbus-run-session for Alroot XFCE ONLY. This is not a system
daemon, does not claim device-global D-Bus, and never touches :0.
"""
import json
import os
import signal
import time
from pathlib import Path
import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

STATE = Path("/data/data/com.termux/files/home/.alroot-wayland")
CACHE = STATE / "android-battery.json"
ROOT = "/org/freedesktop/UPower"
BATTERY = ROOT + "/devices/battery_BAT0"
DISPLAY = ROOT + "/devices/DisplayDevice"
NAME = "org.freedesktop.UPower"

ROOT_INTERFACE = """
<node><interface name="org.freedesktop.UPower">
 <method name="EnumerateDevices">
  <arg name="devices" type="ao" direction="out"/>
 </method>
 <method name="GetDisplayDevice">
  <arg name="device" type="o" direction="out"/>
 </method>
 <property name="DaemonVersion" type="s" access="read"/>
 <property name="OnBattery" type="b" access="read"/>
 <property name="LidIsPresent" type="b" access="read"/>
 <property name="LidIsClosed" type="b" access="read"/>
 <signal name="DeviceAdded"><arg type="o"/></signal>
 <signal name="DeviceRemoved"><arg type="o"/></signal>
 <signal name="DeviceChanged"><arg type="o"/></signal>
</interface></node>
"""

DEVICE_INTERFACE = """
<node><interface name="org.freedesktop.UPower.Device">
 <method name="Refresh"/>
 <property name="NativePath" type="s" access="read"/>
 <property name="Vendor" type="s" access="read"/>
 <property name="Model" type="s" access="read"/>
 <property name="Serial" type="s" access="read"/>
 <property name="Type" type="u" access="read"/>
 <property name="State" type="u" access="read"/>
 <property name="IsPresent" type="b" access="read"/>
 <property name="PowerSupply" type="b" access="read"/>
 <property name="Online" type="b" access="read"/>
 <property name="Percentage" type="d" access="read"/>
 <property name="Energy" type="d" access="read"/>
 <property name="EnergyEmpty" type="d" access="read"/>
 <property name="EnergyFull" type="d" access="read"/>
 <property name="EnergyFullDesign" type="d" access="read"/>
 <property name="EnergyRate" type="d" access="read"/>
 <property name="Capacity" type="d" access="read"/>
 <property name="TimeToEmpty" type="x" access="read"/>
 <property name="TimeToFull" type="x" access="read"/>
 <property name="UpdateTime" type="t" access="read"/>
 <property name="WarningLevel" type="u" access="read"/>
 <property name="Technology" type="u" access="read"/>
 <property name="IconName" type="s" access="read"/>
 <signal name="Changed"/>
</interface></node>
"""

def snapshot():
    try:
        if time.time() - CACHE.stat().st_mtime > 180:
            raise ValueError("battery cache is stale")
        content = json.loads(CACHE.read_text(encoding="utf-8"))
        percent = float(content["percentage"])
        if not 0 <= percent <= 100:
            raise ValueError("battery out of range")
        status = content.get("status", "UNKNOWN")
        state = (1 if status == "CHARGING" else
                 4 if status == "FULL" else
                 2 if status == "DISCHARGING" else 0)
        return percent, state
    except (OSError, ValueError, KeyError, TypeError):
        return -1.0, 0


def device_props():
    level, state = snapshot()
    known = level >= 0
    level = max(level, 0.0)
    return {
        "NativePath": GLib.Variant("s", "android-battery"),
        "Vendor": GLib.Variant("s", "Android"),
        "Model": GLib.Variant("s", "Battery"),
        "Serial": GLib.Variant("s", ""),
        "Type": GLib.Variant("u", 2),  # UPowerDeviceKind.BATTERY
        "State": GLib.Variant("u", state),
        "IsPresent": GLib.Variant("b", known),
        "PowerSupply": GLib.Variant("b", True),
        "Online": GLib.Variant("b", state in (1, 4)),
        "Percentage": GLib.Variant("d", level),
        "Energy": GLib.Variant("d", level / 100),
        "EnergyEmpty": GLib.Variant("d", 0.0),
        "EnergyFull": GLib.Variant("d", 1.0),
        "EnergyFullDesign": GLib.Variant("d", 1.0),
        "EnergyRate": GLib.Variant("d", 0.0),
        "Capacity": GLib.Variant("d", 100.0),
        "TimeToEmpty": GLib.Variant("x", 0),
        "TimeToFull": GLib.Variant("x", 0),
        "UpdateTime": GLib.Variant("t", int(time.time())),
        "WarningLevel": GLib.Variant("u", 0),
        "Technology": GLib.Variant("u", 0),
        "IconName": GLib.Variant("s", "battery-good-symbolic"),
    }


def run():
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    root_iface = Gio.DBusNodeInfo.new_for_xml(ROOT_INTERFACE).interfaces[0]
    device_iface = Gio.DBusNodeInfo.new_for_xml(DEVICE_INTERFACE).interfaces[0]

    def methods(connection, sender, obj, iface, method, args, call):
        if obj == ROOT and method == "EnumerateDevices":
            call.return_value(GLib.Variant("(ao)", ([BATTERY],)))
        elif obj == ROOT and method == "GetDisplayDevice":
            call.return_value(GLib.Variant("(o)", (DISPLAY,)))
        elif iface == NAME + ".Device" and method == "Refresh":
            call.return_value(GLib.Variant("()", ()))
        else:
            call.return_dbus_error(NAME + ".Unsupported",
                                   "Unsupported battery method")

    def get_property(connection, sender, obj, iface, prop):
        if obj == ROOT:
            level, state = snapshot()
            root_props = {
                "DaemonVersion": GLib.Variant("s", "Alroot Android BatteryManager"),
                "OnBattery": GLib.Variant("b", state != 1 and state != 4),
                "LidIsPresent": GLib.Variant("b", False),
                "LidIsClosed": GLib.Variant("b", False),
            }
            return root_props.get(prop)
        return device_props().get(prop)

    objects = [
        bus.register_object(ROOT, root_iface, methods, get_property, None),
        bus.register_object(BATTERY, device_iface, methods, get_property, None),
        bus.register_object(DISPLAY, device_iface, methods, get_property, None),
    ]

    loop = GLib.MainLoop()
    def acquired(conn, name):
        print("ALROOT_UPOWER_READY private_session_bus battery=%s" %
              snapshot()[0], flush=True)
    def lost(conn, name):
        print("ALROOT_UPOWER_NAME_LOST", flush=True)
        loop.quit()
    owner = Gio.bus_own_name_on_connection(
        bus, NAME, Gio.BusNameOwnerFlags.NONE, acquired, lost)

    previous = [None]
    def refresh():
        now = snapshot()
        if now != previous[0]:
            previous[0] = now
            changed = device_props()
            bus.emit_signal(None, BATTERY, "org.freedesktop.DBus.Properties",
                            "PropertiesChanged",
                            GLib.Variant("(sa{sv}as)",
                                         (NAME + ".Device", changed, [])))
            bus.emit_signal(None, DISPLAY, "org.freedesktop.DBus.Properties",
                            "PropertiesChanged",
                            GLib.Variant("(sa{sv}as)",
                                         (NAME + ".Device", changed, [])))
            bus.emit_signal(None, BATTERY, NAME + ".Device", "Changed", None)
            bus.emit_signal(None, ROOT, NAME, "DeviceChanged",
                            GLib.Variant("(o)", (BATTERY,)))
            print("ALROOT_UPOWER_UPDATE percentage=%s state=%s" % now,
                  flush=True)
        return True
    GLib.timeout_add_seconds(5, refresh)
    GLib.idle_add(refresh)
    signal.signal(signal.SIGTERM, lambda sig, frame: loop.quit())
    signal.signal(signal.SIGINT, lambda sig, frame: loop.quit())
    try:
        loop.run()
    finally:
        Gio.bus_unown_name(owner)
        for registration in objects:
            bus.unregister_object(registration)

if __name__ == "__main__":
    run()
