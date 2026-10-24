"""Export light-mode variants of every (or the named) diagram to export-light/<name>.svg and .png (2x).

The .drawio sources stay dark; this remaps the dio.py palette to a light one in a temp copy and exports that.
Shapes filled with an accent colour get white text (the dark theme puts dark text on bright accents).
Usage: python export_light.py [name ...]   (needs draw.io desktop, same as export.sh)
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
DRAWIO = os.environ.get("DRAWIO", str(Path.home() / "scoop/apps/draw.io/current/draw.io.exe"))

# dark -> light, keyed by dio.py names
SURFACES = {
    "#121417": "#FFFFFF",  # BG
    "#1C2128": "#F6F8FA",  # PANEL
    "#262C36": "#EAEEF2",  # PANEL_2
    "#3A3F2A": "#EFE9CF",  # FR-4 layers in the stackup
}
INK = {
    "#E6EDF3": "#1F2328",  # TEXT
    "#8B949E": "#59636E",  # LINE / MUTED
    "#F78166": "#BC4C00",  # RF
    "#58A6FF": "#0969DA",  # CORE0
    "#A371F7": "#8250DF",  # CORE1
    "#3FB950": "#1A7F37",  # MEAS
    "#F2CC60": "#9A6700",  # POWER
    "#39C5CF": "#1B7C83",  # HOST
    "#FF7B72": "#CF222E",  # BAD
}
ACCENT_FILLS = set(INK) - {"#E6EDF3"}
HEX = re.compile(r"#[0-9A-Fa-f]{6}")


def recolor_style(m):
    style = m.group(0)
    fill = re.search(r"fillColor=(#[0-9A-Fa-f]{6})", style)
    if fill and fill.group(1).upper() in ACCENT_FILLS:
        style = re.sub(r"fontColor=#[0-9A-Fa-f]{6}", "fontColor=#FFFFFF", style)
    return style


def recolor(xml):
    xml = re.sub(r'style="[^"]*"', recolor_style, xml)
    table = {**INK, **SURFACES, "#FFFFFF": "#FFFFFF"}
    return HEX.sub(lambda h: table.get(h.group(0).upper(), h.group(0)), xml)


def main(names):
    out = HERE / "export-light"
    out.mkdir(exist_ok=True)
    names = names or sorted(p.stem for p in HERE.glob("*.drawio"))
    with tempfile.TemporaryDirectory() as tmp:
        for n in names:
            src = Path(tmp) / f"{n}.drawio"
            src.write_text(recolor((HERE / f"{n}.drawio").read_text(encoding="utf-8")), encoding="utf-8")
            for fmt, extra in (("svg", []), ("png", ["-s", "2"])):
                subprocess.run([DRAWIO, "-x", "-f", fmt, *extra, "-o", str(out / f"{n}.{fmt}"), str(src)],
                               stdin=subprocess.DEVNULL, capture_output=True, check=True)
            print(f"exported export-light/{n}")


if __name__ == "__main__":
    main(sys.argv[1:])
