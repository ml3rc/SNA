"""Shared look for all SNA presentation animations (matches the draw.io diagrams).

Dark theme, no LaTeX (Text only). Import with `from sna_style import *`. Labels are English; SNA_LANG=de renders the
German version (translations in ../i18n_de.py, applied in label(); code()/JSON text stays as is).
"""
import sys
from pathlib import Path

from manim import *

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))  # docs/presentation: shared i18n
from i18n import LANG, tr  # noqa: E402

# Palette (same hex values as the draw.io diagrams in ../diagrams)
BG = "#121417"        # slide background
PANEL = "#1C2128"     # block fill
PANEL_2 = "#262C36"   # nested / secondary fill
LINE = "#8B949E"      # neutral strokes, arrows
TEXT = "#E6EDF3"      # main text
MUTED = "#8B949E"     # secondary text

RF = "#F78166"        # RF signal / source chain
CORE0 = "#58A6FF"     # core 0, USB, communication
CORE1 = "#A371F7"     # core 1, RF control firmware
MEAS = "#3FB950"      # detector, measurement, results
POWER = "#F2CC60"     # power, warnings
HOST = "#39C5CF"      # PC / client
BAD = "#FF7B72"       # errors, "not allowed"

FONT = "Segoe UI"
MONO = "Consolas"

config.background_color = BG


def label(text, size=28, color=TEXT, weight=NORMAL, font=FONT):
    return Text(tr(text), font=font, font_size=size, color=color, weight=weight)


def block(title, sub=None, color=LINE, w=2.6, h=1.1, fill=PANEL, title_size=26, sub_size=18):
    """Rounded block with a title and an optional muted subtitle, like the draw.io blocks."""
    box = RoundedRectangle(corner_radius=0.12, width=w, height=h, stroke_color=color,
                           stroke_width=3, fill_color=fill, fill_opacity=1)
    t = label(title, title_size, TEXT, weight=BOLD)
    if sub:
        s = label(sub, sub_size, MUTED)
        grp = VGroup(t, s).arrange(DOWN, buff=0.08)
    else:
        grp = VGroup(t)
    grp.move_to(box.get_center())
    if grp.width > w - 0.2:
        grp.scale_to_fit_width(w - 0.2)
    return VGroup(box, grp)


def arrow(a, b, color=LINE, buff=0.08, stroke=3):
    return Arrow(a, b, buff=buff, color=color, stroke_width=stroke,
                 max_tip_length_to_length_ratio=0.15, tip_length=0.18)


def title_card(text, sub=None):
    t = label(text, 44, TEXT, weight=BOLD)
    g = VGroup(t)
    if sub:
        g.add(label(sub, 26, MUTED))
    g.arrange(DOWN, buff=0.2).to_edge(UP, buff=0.4)
    return g


def pulse_dot(color=RF, r=0.09):
    return Dot(radius=r, color=color).set_z_index(10)
