"""Helpers shared by the firmware scenes (on top of sna_style)."""
from sna_style import *
from i18n import tr_exact


class Captions:
    """One caption line at the bottom of the frame, replaced step by step."""

    def __init__(self, scene, y=-3.45, size=26):
        self.scene = scene
        self.y = y
        self.size = size
        self.cur = None

    def show(self, text, color=TEXT, run_time=0.5):
        new = label(text, self.size, color).move_to([0, self.y, 0])
        if new.width > 13.4:
            new.scale_to_fit_width(13.4)
        if self.cur is None:
            self.scene.play(FadeIn(new, shift=UP * 0.15), run_time=run_time)
        else:
            self.scene.play(FadeOut(self.cur, shift=UP * 0.15), FadeIn(new, shift=UP * 0.15), run_time=run_time)
        self.cur = new

    def clear(self, run_time=0.4):
        if self.cur is not None:
            self.scene.play(FadeOut(self.cur), run_time=run_time)
            self.cur = None


def code(text, size=22, color=TEXT):
    return Text(tr_exact(text), font=MONO, font_size=size, color=color)


def token(text, color=CORE0, size=20, mono=True):
    """A small rounded 'message' chip that can travel between blocks."""
    t = code(text, size, TEXT) if mono else label(text, size, TEXT)
    box = RoundedRectangle(corner_radius=0.08, width=t.width + 0.3, height=t.height + 0.22,
                           stroke_color=color, stroke_width=2.5, fill_color=PANEL_2, fill_opacity=1)
    t.move_to(box)
    return VGroup(box, t).set_z_index(20)


def flash_box(blk, color, run_time=0.6):
    """Briefly highlight a block's outline with an overlay (keeps the block's text on top)."""
    ring = blk[0].copy().set_fill(opacity=0).set_stroke(color, width=7).set_z_index(15)
    return Succession(FadeIn(ring, run_time=run_time / 2), FadeOut(ring, run_time=run_time / 2))


def queue_slots(n=8, size=0.32, color=LINE, buff=0.05):
    slots = VGroup(*[Square(size, stroke_color=color, stroke_width=2, fill_color=PANEL, fill_opacity=1)
                     for _ in range(n)]).arrange(RIGHT, buff=buff)
    return slots
