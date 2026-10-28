#!/data/data/com.termux/files/usr/bin/python3
"""Non-destructive P08 configuration regression with synthetic home dirs."""
import importlib.util
import tempfile
from pathlib import Path

SOURCE = Path("/data/data/com.termux/files/home/alroot_wayland/"
              "termux/configure-xfce-profile.py")
spec = importlib.util.spec_from_file_location("alroot_profile_under_test", SOURCE)
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)

with tempfile.TemporaryDirectory(
        prefix="p08-wallpaper-", dir="/data/data/com.termux/files/home/"
        "alroot_wayland/.source") as scratch:
    scratch = Path(scratch)
    original_home = scratch / "original"
    alroot_home = scratch / "project"
    source = original_home / ".config/xfce4"
    project = alroot_home / "xfce-config/xfce4"
    xml = "xfconf/xfce-perchannel-xml"
    (source / xml).mkdir(parents=True)
    (project / xml).mkdir(parents=True)

    native_wallpaper = "<xfce wallpaper='native-photo.jpg'/>"
    project_wallpaper = "<xfce wallpaper='alroot-custom-photo.png'/>"
    (source / xml / "xfce4-desktop.xml").write_text(native_wallpaper)
    (project / xml / "xfce4-desktop.xml").write_text(project_wallpaper)
    (source / xml / "xfce4-panel.xml").write_text("Termux-X11-panel")
    (project / xml / "xfce4-panel.xml").write_text("old-Alroot-panel")
    (source / "desktop").mkdir()
    (project / "desktop").mkdir()
    (source / "desktop/icons.screen0.yaml").write_text("native-icons")
    (project / "desktop/icons.screen0.yaml").write_text("Alroot-icon-layout")

    profile.HOME = original_home
    profile.STATE = alroot_home
    profile.CONFIG = alroot_home / "xfce-config"
    result = profile.mirror_xfce()
    assert "PERSISTED" in result
    assert (project / xml / "xfce4-desktop.xml").read_text() == project_wallpaper
    assert (project / "desktop/icons.screen0.yaml").read_text() == "Alroot-icon-layout"
    assert (project / xml / "xfce4-panel.xml").read_text() == "Termux-X11-panel"
    assert (source / xml / "xfce4-desktop.xml").read_text() == native_wallpaper

    # A fresh project installation still inherits the initial settings
    # exactly once, then starts preserving user-selected desktop values.
    (project / xml / "xfce4-desktop.xml").unlink()
    (project / "desktop/icons.screen0.yaml").unlink()
    (project / "desktop").rmdir()
    profile.mirror_xfce()
    assert (project / xml / "xfce4-desktop.xml").read_text() == native_wallpaper
    assert (project / "desktop/icons.screen0.yaml").read_text() == "native-icons"
    (project / xml / "xfce4-desktop.xml").write_text(project_wallpaper)
    profile.mirror_xfce()
    assert (project / xml / "xfce4-desktop.xml").read_text() == project_wallpaper

print("PASS: Alroot wallpaper and icon positions persist; original :0 config untouched")
