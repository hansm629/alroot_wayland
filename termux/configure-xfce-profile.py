#!/data/data/com.termux/files/usr/bin/python3
"""Modify only the Alroot copy of Xfce: GenMon battery and Plank autostart."""
import os
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path

HOME = Path("/data/data/com.termux/files/home")
PROJECT = HOME / "alroot_wayland"
STATE = HOME / ".alroot-wayland"
CONFIG = STATE / "xfce-config"


def install_plank():
    src = HOME / ".config/autostart/Plank dock.desktop"
    dest = CONFIG / "autostart/Plank dock.desktop"
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        return "PRESENT"
    if src.is_file():
        shutil.copy2(src, dest)
    else:
        dest.write_text(
            "[Desktop Entry]\nType=Application\nName=Plank dock\n"
            "Exec=plank\nOnlyShowIn=XFCE;\n"
            "Terminal=false\nStartupNotify=false\n"
        )
    docksrc = HOME / ".config/plank"
    dockdest = CONFIG / "plank"
    if docksrc.is_dir() and not dockdest.exists():
        shutil.copytree(docksrc, dockdest, symlinks=False)
    return "ADDED"


def install_battery():
    xmlpath = CONFIG / "xfce4/xfconf/xfce-perchannel-xml/xfce4-panel.xml"
    if not xmlpath.is_file():
        return "NO_PANEL_CONFIG"
    widget = "/data/data/com.termux/files/home/alroot_wayland/termux/alroot-battery-genmon.sh"
    root = ET.parse(xmlpath).getroot()
    plugins = root.find("./property[@name='plugins']")
    if plugins is None:
        return "NO_PLUGINS"
    panels = root.find("./property[@name='panels']")
    if panels is None:
        return "NO_PANELS"
    existing = []
    for entry in plugins.findall("property"):
        name = entry.get("name", "")
        if name.startswith("plugin-") and name[7:].isdigit():
            existing.append(int(name[7:]))
            command = entry.find("./property[@name='command']")
            if entry.get("value") == "genmon" and command is not None \
                    and command.get("value") == widget:
                return "PRESENT"
    target = max([106, *existing]) + 1
    inserted = False
    for panel in panels.findall("property"):
        ids = panel.find("./property[@name='plugin-ids']")
        if ids is None:
            continue
        for node in list(ids):
            if node.get("value") == "7" and any(
                e.get("name") == "plugin-7" and e.get("value") == "battery"
                for e in plugins.findall("property")):
                ids.remove(node)
        if not inserted:
            index = next((i for i, v in enumerate(ids)
                          if v.get("value") == "22"), len(ids))
            ids.insert(index, ET.Element("value",
                       {"type": "int", "value": str(target)}))
            inserted = True
    if not inserted:
        return "NO_PANEL_IDS"
    genmon = ET.SubElement(plugins, "property",
                          {"name": "plugin-" + str(target),
                           "type": "string", "value": "genmon"})
    for name, kind, value in (
        ("command", "string", widget),
        ("update-period", "int", "60"),
        ("use-label", "bool", "false"),
        ("enable-single-row", "bool", "true"),
        ("font", "string", "Sans 9"),
        ("text", "string", "Battery"),
    ):
        ET.SubElement(genmon, "property",
                      {"name": name, "type": kind, "value": value})
    temp = xmlpath.with_name(xmlpath.name + ".alroot-tmp")
    ET.ElementTree(root).write(temp, encoding="UTF-8", xml_declaration=True)
    os.replace(temp, xmlpath)
    return "ADDED:" + str(target)


if __name__ == "__main__":
    assert CONFIG.is_dir(), "No isolated config tree"
    print("Alroot XFCE Plank: " + install_plank())
    print("Alroot XFCE battery GenMon: " + install_battery())
