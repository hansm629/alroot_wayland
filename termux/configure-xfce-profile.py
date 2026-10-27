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

def mirror_xfce():
    # The first snapshot taken on P01 was never refreshed, so later XFCE
    # panel/theme/desktop updates in the user's existing Termux-X11 were
    # missing in Alroot. Always mirror those original files into the private
    # project tree BEFORE starting this new XFCE session; originals are read
    # only and the Alroot battery widget is re-added afterward.
    xmlroot = Path("xfce4/xfconf/xfce-perchannel-xml")
    originals = (
        "xfce4-panel.xml",
        "xfce4-desktop.xml",
        "xfwm4.xml",
        "xsettings.xml",
        "xfce4-session.xml",
        "xfce4-keyboard-shortcuts.xml",
    )
    for filename in originals:
        rel = xmlroot / filename
        src = HOME / ".config" / rel
        dst = CONFIG / rel
        if src.is_file():
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
    for rel in (Path("xfce4/panel"), Path("xfce4/desktop")):
        src = HOME / ".config" / rel
        dst = CONFIG / rel
        if src.is_dir():
            shutil.copytree(src, dst, dirs_exist_ok=True, symlinks=False)
    return "MIRRORED original Xfce panel/theme/desktop (isolated copy)"


def install_plank():
    src = HOME / ".config/autostart/Plank dock.desktop"
    dest = CONFIG / "autostart/Plank dock.desktop"
    dest.parent.mkdir(parents=True, exist_ok=True)
    if src.is_file():
        shutil.copy2(src, dest)
    elif not dest.exists():
        dest.write_text(
            "[Desktop Entry]\nType=Application\nName=Plank dock\n"
            "Exec=plank\nOnlyShowIn=XFCE;\n"
            "Terminal=false\nStartupNotify=false\n"
        )
    # Plank keeps pinned icons, order, zoom, size and theme in DConf, NOT
    # only in the .dockitem files. The old implementation copied launchers
    # but left the isolated DConf with independently generated defaults.
    docksrc = HOME / ".config/plank"
    dockdest = CONFIG / "plank"
    if docksrc.is_dir():
        shutil.copytree(docksrc, dockdest, symlinks=False, dirs_exist_ok=True)
    user_db = HOME / ".config/dconf/user"
    project_db = CONFIG / "dconf/user"
    if user_db.is_file():
        project_db.parent.mkdir(parents=True, exist_ok=True)
        target_tmp = project_db.with_name(".alroot-dconf.tmp")
        shutil.copy2(user_db, target_tmp)
        os.chmod(target_tmp, 0o600)
        os.replace(target_tmp, project_db)
    return "SYNCED (autostart, launchers, DConf ordering/theme/zoom)"

def install_notes():
    # The original native Termux-X11 Notes process is bound to DISPLAY :0.
    # Alroot must start its own XEmbed tray instance, on DISPLAY :90, using
    # a private copy of the user's sticky notes (never write originals).
    source_data = HOME / ".local/share/notes"
    project_data = STATE / "notes-data/notes"
    if source_data.is_dir() and not project_data.exists():
        project_data.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source_data, project_data, symlinks=False)
    source_cfg = HOME / ".config/xfce4/notes"
    project_cfg = CONFIG / "xfce4/notes"
    if source_cfg.is_dir() and not project_cfg.exists():
        project_cfg.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source_cfg, project_cfg, symlinks=False)
    dest = CONFIG / "autostart/xfce4-notes-autostart.desktop"
    dest.parent.mkdir(parents=True, exist_ok=True)
    original = HOME / ".config/autostart/xfce4-notes-autostart.desktop"
    if original.is_file():
        text = original.read_text(encoding="utf-8")
        text = text.replace("Exec=xfce4-notes",
                            "Exec=" + str(PROJECT / "termux/alroot-notes.sh"))
    else:
        text = ("[Desktop Entry]\nType=Application\nName=Notes\n"
                "Exec=" + str(PROJECT / "termux/alroot-notes.sh")
                + "\nOnlyShowIn=XFCE;\nTerminal=false\n")
    dest.write_text(text, encoding="utf-8")
    return "ENABLED (separate X11 tray and private data snapshot)"


def install_battery_tray():
    dest = CONFIG / "autostart/alroot-battery-tray.desktop"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(
        "[Desktop Entry]\nType=Application\nName=Alroot Battery\n"
        "Exec=" + str(PROJECT / "termux/alroot-battery-tray.sh") + "\n"
        "OnlyShowIn=XFCE;\nTerminal=false\nStartupNotify=false\n",
        encoding="utf-8")
    return "ENABLED (GTK XEmbed status icon + Android percentage)"


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
                # Keep a visible label even if a transient Android battery
                # update fails. Otherwise the plugin can collapse to zero
                # visible width although its XEmbed worker is running.
                desired = {
                    "update-period": ("int", "15"),
                    "use-label": ("bool", "true"),
                    "text": ("string", "BAT"),
                    "font": ("string", "Sans 10"),
                }
                for setting, (kind, value) in desired.items():
                    prop = entry.find("./property[@name='" + setting + "']")
                    if prop is None:
                        prop = ET.SubElement(entry, "property",
                                             {"name": setting, "type": kind})
                    prop.set("value", value)
                temp = xmlpath.with_name(xmlpath.name + ".alroot-tmp")
                ET.ElementTree(root).write(temp, encoding="UTF-8", xml_declaration=True)
                os.replace(temp, xmlpath)
                return "UPDATED (visible BAT label + 15s battery refresh)"
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
        ("update-period", "int", "15"),
        ("use-label", "bool", "true"),
        ("enable-single-row", "bool", "true"),
        ("font", "string", "Sans 10"),
        ("text", "string", "BAT"),
    ):
        ET.SubElement(genmon, "property",
                      {"name": name, "type": kind, "value": value})
    temp = xmlpath.with_name(xmlpath.name + ".alroot-tmp")
    ET.ElementTree(root).write(temp, encoding="UTF-8", xml_declaration=True)
    os.replace(temp, xmlpath)
    return "ADDED:" + str(target)


if __name__ == "__main__":
    assert CONFIG.is_dir(), "No isolated config tree"
    print("Alroot XFCE: " + mirror_xfce())
    print("Alroot XFCE Plank: " + install_plank())
    print("Alroot XFCE battery GenMon: " + install_battery())
    print("Alroot XFCE battery tray: " + install_battery_tray())
    print("Alroot XFCE sticky notes: " + install_notes())
