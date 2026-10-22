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

# --- Rendering options (env) ----------------------------------------------------------------------------------------
# SNA_SPEED     playback speed factor for every animation and wait (default 1.3)
# SNA_CAPTIONS  1 = burn the step captions into the video; default 0: no captions on screen, the caption texts go to
#               <SNA_NOTES_DIR>/<Scene>.json per step (render.sh turns them into speaker notes)
# SNA_CHECK_BOUNDS=<file>  append every object that leaves the visible frame (checked on every rendered frame)
import json  # noqa: E402
import os  # noqa: E402

from manim.renderer.cairo_renderer import CairoRenderer  # noqa: E402

SPEED = float(os.environ.get("SNA_SPEED", "1.3"))
SHOW_CAPTIONS = os.environ.get("SNA_CAPTIONS", "0") == "1"
_CHECK = os.environ.get("SNA_CHECK_BOUNDS")
_current = {"scene": None}


def mark_caption(mob):
    """Caption text: hidden (and its animations skipped) unless SNA_CAPTIONS=1; recorded as a note for this step."""
    scene = _current["scene"]
    if scene is not None:
        scene._sna_notes.setdefault(scene._sna_step, []).append(mob._sna_text)
    if not SHOW_CAPTIONS:
        mob._sna_caption = True
        mob.set_opacity(0)
    return mob


def _is_caption_anim(a):
    return getattr(getattr(a, "mobject", None), "_sna_caption", False)


_orig_play = Scene.play


def _play(self, *args, **kwargs):
    args = [a for a in args if not _is_caption_anim(a)]
    if not args:
        return
    self._sna_plays = getattr(self, "_sna_plays", 0) + 1
    return _orig_play(self, *args, **kwargs)


_orig_compile = Scene.compile_animations


def _compile(self, *args, **kwargs):
    anims = _orig_compile(self, *args, **kwargs)
    for a in anims:
        a.run_time = a.run_time / SPEED
    return anims


_orig_next_section = Scene.next_section


def _next_section(self, *args, **kwargs):
    if getattr(self, "_sna_plays", 0):   # Manim drops empty sections, so only a non-empty one is a step
        # End the step on a freshly drawn frame: objects removed after the last animation (travelling dots, glows)
        # are otherwise still in the step's last frame, which PowerPoint shows until the next click -> visible jump.
        self.wait(0.05)
        self._sna_step += 1
        self._sna_plays = 0
    return _orig_next_section(self, *args, **kwargs)


_orig_render = Scene.render


def _render(self, *args, **kwargs):
    self._sna_step, self._sna_plays, self._sna_notes = 1, 0, {}
    _current["scene"] = self
    try:
        return _orig_render(self, *args, **kwargs)
    finally:
        _current["scene"] = None
        out = os.environ.get("SNA_NOTES_DIR")
        if out:
            os.makedirs(out, exist_ok=True)
            with open(os.path.join(out, f"{type(self).__name__}.json"), "w", encoding="utf-8") as f:
                json.dump({str(k): v for k, v in sorted(self._sna_notes.items())}, f, ensure_ascii=False, indent=1)


Scene.play, Scene.compile_animations, Scene.next_section, Scene.render = _play, _compile, _next_section, _render

if _CHECK:
    _orig_update_frame = CairoRenderer.update_frame
    _seen = set()

    def _visible_points(m):
        pts = []
        for sm in m.get_family():
            if isinstance(sm, VMobject) and sm.has_points() and (
                    sm.get_fill_opacity() > 0.02 or sm.get_stroke_opacity() > 0.02 and sm.get_stroke_width() > 0):
                pts.append(sm.points)
        return np.concatenate(pts) if pts else None

    def _update_frame(self, scene, *args, **kwargs):
        hw, hh = config.frame_width / 2 - 0.04, config.frame_height / 2 - 0.04
        for m in scene.mobjects:
            if getattr(m, "_sna_caption", False):
                continue
            p = _visible_points(m)
            if p is None:
                continue
            lo, hi = p.min(axis=0), p.max(axis=0)
            if lo[0] < -hw or hi[0] > hw or lo[1] < -hh or hi[1] > hh:
                texts = [t.text for t in m.get_family() if isinstance(t, Text)][:2]
                key = (type(scene).__name__, id(m))
                if key not in _seen:
                    _seen.add(key)
                    with open(_CHECK, "a", encoding="utf-8") as f:
                        f.write(f"{type(scene).__name__}\tt={scene.renderer.time:.1f}\t{type(m).__name__}\t"
                                f"x {lo[0]:.2f}..{hi[0]:.2f} y {lo[1]:.2f}..{hi[1]:.2f}\t{texts}\n")
        return _orig_update_frame(self, scene, *args, **kwargs)

    CairoRenderer.update_frame = _update_frame


MAX_TEXT_W = 13.4   # frame is 14.2 wide: longer lines (often the German ones) shrink instead of being cut off


def label(text, size=28, color=TEXT, weight=NORMAL, font=FONT):
    t = Text(tr(text), font=font, font_size=size, color=color, weight=weight)
    if t.width > MAX_TEXT_W:
        t.scale_to_fit_width(MAX_TEXT_W)
    return t


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


def highlight(mob, color=POWER, **kwargs):
    """Colour flash in place. No scaling: a zoom (Indicate, Wiggle, FadeIn(scale>1)) can push text off the frame."""
    kwargs["scale_factor"] = 1.0
    return Indicate(mob, color=color, **kwargs)


def pulse_dot(color=RF, r=0.09):
    return Dot(radius=r, color=color).set_z_index(10)


# IEC symbols, same size everywhere (and in the draw.io diagrams: 90 x 30 px resistor, 40 px ground)
R_LEN, R_WID = 0.8, 0.27
GND_W = 0.36


def resistor(start, end, color=TEXT, stroke=3, wire=None):
    """IEC resistor (rectangle) centred between two points, any orientation; the leads (colour `wire`, default
    `color`) take up the rest."""
    s, e = np.array(start, dtype=float), np.array(end, dtype=float)
    if len(s) == 2:
        s = np.append(s, 0)
        e = np.append(e, 0)
    d = e - s
    L = np.linalg.norm(d)
    u = d / L
    mid = (s + e) / 2
    body = Rectangle(width=R_LEN, height=R_WID, stroke_color=color, stroke_width=stroke,
                     fill_color=BG, fill_opacity=1).rotate(np.arctan2(u[1], u[0])).move_to(mid)
    lead_a = Line(s, mid - u * R_LEN / 2, color=wire or color, stroke_width=stroke)
    lead_b = Line(mid + u * R_LEN / 2, e, color=wire or color, stroke_width=stroke)
    return VGroup(lead_a, lead_b, body)


def ground(at, color=TEXT):
    """Ground symbol hanging below the given point."""
    p = np.array([at[0], at[1], 0])
    return VGroup(*[Line(p + LEFT * GND_W * k / 2 + DOWN * 0.08 * i, p + RIGHT * GND_W * k / 2 + DOWN * 0.08 * i,
                         color=color, stroke_width=3)
                    for i, k in enumerate((1, 2 / 3, 1 / 3))])


def amplifier(text, color=RF, w=1.3, h=1.0, size=20):
    """Amplifier triangle pointing right, like the draw.io RF chain."""
    tri = Polygon(LEFT * w / 2 + UP * h / 2, LEFT * w / 2 + DOWN * h / 2, RIGHT * w / 2, stroke_color=color,
                  stroke_width=3, fill_color=PANEL, fill_opacity=1)
    t = label(text, size, TEXT, weight=BOLD).move_to(tri.get_center() + LEFT * w * 0.12)
    return VGroup(tri, t)
