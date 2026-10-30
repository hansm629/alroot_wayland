#!/data/data/com.termux/files/usr/bin/python3
"""Read only the XFCE cursor theme and size for private Xwayland startup.

No changes to the user's source XFCE configuration or global Termux-X11.
The regular XFCE startup mirrors the source xsettings.xml into its profile.
"""

import re
import xml.etree.ElementTree as ET
from pathlib import Path

HOME = Path("/data/data/com.termux/files/home")
REL = Path("xfce4/xfconf/xfce-perchannel-xml/xsettings.xml")


def cursor_values(config: Path) -> tuple[str, int] | None:
    try:
        root = ET.parse(config).getroot()
    except (OSError, ET.ParseError):
        return None
    values = {p.attrib.get("name"): p.attrib.get("value")
              for p in root.iter("property")}
    theme = values.get("CursorThemeName")
    if not theme or not re.fullmatch(r"[A-Za-z0-9_+.-]{1,128}", theme) or theme in (".", ".."):
        return None
    try:
        size = int(values.get("CursorThemeSize") or 24)
    except ValueError:
        size = 24
    if not 8 <= size <= 256:
        size = 24
    return theme, size


if __name__ == "__main__":
    # The original is re-mirrored into Alroot's private XFCE profile on every
    # session launch, so it also determines the correct early Xwayland theme.
    for config in (HOME / ".config" / REL,
                   HOME / ".alroot-wayland/xfce-config" / REL):
        result = cursor_values(config)
        if result:
            print(f"{result[0]}\t{result[1]}")
            break
