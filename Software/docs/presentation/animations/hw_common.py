"""Helpers for hardware_scenes.py (wires, resistors, travelling pulses)."""
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


def resistor(start, end, color=TEXT, zigs=6, amp=0.13, lead=0.25, stroke=3):
    """Zig-zag resistor between two points (any orientation)."""
    s, e = np.array(start, dtype=float), np.array(end, dtype=float)
    if len(s) == 2:
        s = np.append(s, 0)
        e = np.append(e, 0)
    d = e - s
    L = np.linalg.norm(d)
    u = d / L
    n = np.array([-u[1], u[0], 0])
    pts = [s, s + u * lead]
    body = L - 2 * lead
    for i in range(zigs):
        t = lead + body * (i + 0.5) / zigs
        pts.append(s + u * t + n * amp * (1 if i % 2 == 0 else -1))
    pts += [s + u * (L - lead), e]
    return VMobject(color=color, stroke_width=stroke).set_points_as_corners(pts)


def ground(at, color=TEXT, w=0.36):
    p = np.array([at[0], at[1], 0])
    return VGroup(
        Line(p + LEFT * w / 2, p + RIGHT * w / 2, color=color, stroke_width=3),
        Line(p + LEFT * w / 3 + DOWN * 0.08, p + RIGHT * w / 3 + DOWN * 0.08, color=color, stroke_width=3),
        Line(p + LEFT * w / 6 + DOWN * 0.16, p + RIGHT * w / 6 + DOWN * 0.16, color=color, stroke_width=3),
    )


def chip(text, color, size=22):
    """Small rounded tag."""
    t = label(text, size, TEXT)
    box = RoundedRectangle(corner_radius=0.1, width=t.width + 0.35, height=t.height + 0.25,
                           stroke_color=color, stroke_width=2.5, fill_color=PANEL, fill_opacity=1)
    t.move_to(box)
    return VGroup(box, t)


def caption(text, size=26, color=MUTED):
    return label(text, size, color).to_edge(DOWN, buff=0.35)


def flash_fill(mob, color, opacity=0.45):
    """Briefly tint a block's background (keeps its text readable, unlike Indicate)."""
    return mob[0].animate(rate_func=there_and_back, run_time=0.8).set_fill(color, opacity=opacity)
