"""Helpers for hardware_scenes.py (wires, travelling pulses). Resistor/ground/amplifier symbols live in sna_style."""
from sna_style import *


def wire(points, color=LINE, stroke=3, tip=True):
    """Orthogonal wire through the given points, arrow tip on the last segment."""
    pts = [np.array([p[0], p[1], 0]) if len(p) == 2 else np.array(p) for p in points]
    parts = VGroup()
    for a, b in zip(pts[:-2], pts[1:-1]):
        parts.add(Line(a, b, color=color, stroke_width=stroke))
    if tip:
        parts.add(Arrow(pts[-2], pts[-1], buff=0, color=color, stroke_width=stroke,
                        tip_length=0.18, max_tip_length_to_length_ratio=0.5))
    else:
        parts.add(Line(pts[-2], pts[-1], color=color, stroke_width=stroke))
    return parts


def path_of(points):
    pts = [np.array([p[0], p[1], 0]) if len(p) == 2 else np.array(p) for p in points]
    return VMobject().set_points_as_corners(pts)


def travel(scene, points, color=RF, run_time=1.5, r=0.1, keep=False):
    """Move a glowing dot along a polyline."""
    d = Dot(radius=r, color=color).set_z_index(20)
    glow = Dot(radius=r * 2.2, color=color, fill_opacity=0.25).set_z_index(19)
    g = VGroup(glow, d)
    p = path_of(points)
    g.move_to(p.get_start())
    scene.add(g)
    scene.play(MoveAlongPath(g, p), run_time=run_time, rate_func=linear)
    if not keep:
        scene.remove(g)
    return g


def travel_anim(points, color=RF, run_time=1.5, r=0.1):
    """Same as travel(), but returns (mobject, animation) to combine with other animations."""
    d = Dot(radius=r, color=color).set_z_index(20)
    glow = Dot(radius=r * 2.2, color=color, fill_opacity=0.25).set_z_index(19)
    g = VGroup(glow, d)
    p = path_of(points)
    g.move_to(p.get_start())
    return g, MoveAlongPath(g, p, run_time=run_time, rate_func=linear)


def chip(text, color, size=22):
    """Small rounded tag."""
    t = label(text, size, TEXT)
    box = RoundedRectangle(corner_radius=0.1, width=t.width + 0.35, height=t.height + 0.25,
                           stroke_color=color, stroke_width=2.5, fill_color=PANEL, fill_opacity=1)
    t.move_to(box)
    return VGroup(box, t)


def caption(text, size=26, color=MUTED):
    """Step caption at the bottom; hidden unless SNA_CAPTIONS=1, always recorded as a speaker note (sna_style)."""
    m = label(text, size, color).to_edge(DOWN, buff=0.35)
    m._sna_text = tr(text)
    return mark_caption(m)


def flash_fill(mob, color, opacity=0.45):
    """Briefly tint a block's background (keeps its text readable, unlike Indicate)."""
    return mob[0].animate(rate_func=there_and_back, run_time=0.8).set_fill(color, opacity=opacity)


def inductor(start, end, color=TEXT, n=3, stroke=3):
    """Inductor as a row of half circles between two horizontal points."""
    s, e = np.array(start, dtype=float), np.array(end, dtype=float)
    r = (e[0] - s[0]) / (2 * n)
    arcs = VGroup(*[Arc(radius=r, start_angle=PI, angle=-PI, color=color, stroke_width=stroke)
                    .move_arc_center_to(s + RIGHT * r * (2 * i + 1)) for i in range(n)])
    return arcs


def capacitor(top, bottom, color=TEXT, stroke=3, gap=0.09, plate=0.34):
    """Capacitor between two vertically aligned points (plates horizontal)."""
    t, b = np.array(top, dtype=float), np.array(bottom, dtype=float)
    mid = (t + b) / 2
    return VGroup(Line(t, mid + UP * gap, color=color, stroke_width=stroke),
                  Line(mid + UP * gap + LEFT * plate / 2, mid + UP * gap + RIGHT * plate / 2, color=color,
                       stroke_width=stroke + 1),
                  Line(mid + DOWN * gap + LEFT * plate / 2, mid + DOWN * gap + RIGHT * plate / 2, color=color,
                       stroke_width=stroke + 1),
                  Line(mid + DOWN * gap, b, color=color, stroke_width=stroke))
