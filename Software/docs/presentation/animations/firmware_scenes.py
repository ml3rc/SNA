"""Firmware animations for the SNA presentation.

Render (from this folder):
  .venv/Scripts/python.exe -m manim -qh firmware_scenes.py MeasurementFlow
Scenes: MeasurementFlow, SweepPipeline, Calibration, SourceSafety, ModesAndCommands
"""
import math

import numpy as np

from fw_common import *


# ---------------------------------------------------------------------------
# 1. One S11 point, end to end
# ---------------------------------------------------------------------------
class MeasurementFlow(Scene):
    def construct(self):
        cap = Captions(self)
        title = title_card("One measurement point", "S11 at 1 GHz, from click to chart")
        self.play(FadeIn(title))
        self.wait(0.5)
        self.play(title.animate.scale(0.7).to_edge(UP, buff=0.2))

        # --- digital row ---------------------------------------------------
        y_top = 1.9
        pc = block("PC client", "Kotlin app", HOST, w=2.4, h=1.1).move_to([-5.3, y_top, 0])
        c0 = block("Core 0", "USB · JSON · dB", CORE0, w=2.4, h=1.1).move_to([-1.7, y_top, 0])
        c1 = block("Core 1", "RF hardware", CORE1, w=2.4, h=1.1).move_to([3.25, y_top, 0])
        req_q = queue_slots(6, 0.26, CORE1).move_to([0.8, y_top + 0.28, 0])
        rep_q = queue_slots(6, 0.26, CORE0).move_to([0.8, y_top - 0.28, 0])
        req_l = label("requests →", 20, MUTED).next_to(req_q, UP, buff=0.08)
        rep_l = label("← replies", 20, MUTED).next_to(rep_q, DOWN, buff=0.08)
        usb = label("USB", 20, MUTED).move_to([-3.5, y_top + 0.35, 0])
        usb_line = Line(pc.get_right(), c0.get_left(), color=LINE, stroke_width=3)

        # --- RF row --------------------------------------------------------
        y_rf = -1.55
        xs = [-5.35, -3.2, -1.05, 1.1, 3.25, 5.4]
        names = [("MAX2871", "synthesizer"), ("LPF bank", "harmonics"), ("Atten + Amp", "level"),
                 ("Port switch", "port 1 / 2"), ("Coupler", "directional"), ("DUT", "cable")]
        cols = [RF, RF, RF, RF, MEAS, LINE]
        rf = [block(n, s, c, w=1.8, h=1.0, title_size=22, sub_size=17).move_to([x, y_rf, 0])
              for (n, s), c, x in zip(names, cols, xs)]
        rf_lines = VGroup(*[Line(rf[i].get_right(), rf[i + 1].get_left(), color=RF, stroke_width=3)
                            for i in range(5)])
        det = block("AD8318", "log detector", MEAS, w=1.9, h=0.95, title_size=22, sub_size=17).move_to([3.25, 0.15, 0])
        det_line = Line(rf[4].get_top(), det.get_bottom(), color=MEAS, stroke_width=3)
        adc_line = Line(det.get_top(), c1.get_bottom(), color=MEAS, stroke_width=3)
        adc_l = label("ADC", 20, MEAS).next_to(adc_line, RIGHT, buff=0.1)

        self.play(LaggedStart(FadeIn(pc), FadeIn(c0), FadeIn(c1), Create(usb_line), FadeIn(usb),
                              FadeIn(req_q), FadeIn(rep_q), FadeIn(req_l), FadeIn(rep_l), lag_ratio=0.1))
        self.play(LaggedStart(*[FadeIn(b) for b in rf], lag_ratio=0.1), Create(rf_lines))
        self.play(FadeIn(det), Create(det_line), Create(adc_line), FadeIn(adc_l))

        # --- 1. request -------------------------------------------------------
        cap.show("1  The client sends one JSON line over USB")
        msg = token('{"cmd":"sweep","params":["s11"],…}', HOST, 20).next_to(pc, DOWN, buff=0.25).align_to(pc, LEFT)
        self.play(FadeIn(msg, shift=UP * 0.2))
        self.play(msg.animate.scale(0.55).move_to(c0.get_center()), run_time=1.2)
        self.play(FadeOut(msg), flash_box(c0, CORE0))

        cap.show("2  Core 0 checks it and queues a request for core 1")
        req = token("SWEEP", CORE1, 18, mono=False).move_to(c0.get_right() + RIGHT * 0.3)
        self.play(FadeIn(req))
        self.play(req.animate.move_to(req_q[0]), run_time=0.6)
        self.play(req.animate.move_to(req_q[-1]), run_time=0.6)
        self.play(req.animate.move_to(c1.get_center()), run_time=0.5)
        self.play(FadeOut(req), flash_box(c1, CORE1))

        # --- 3. configure RF ---------------------------------------------------
        cap.show("3  Core 1 sets up the RF path for 1 GHz")
        notes = [("tune 1 GHz", 0), ("947 MHz LPF", 1), ("port 1", 3)]
        shown = []
        for text, i in notes:
            n = label(text, 20, CORE1).next_to(rf[i], DOWN, buff=0.15)
            self.play(flash_box(rf[i], CORE1), FadeIn(n), run_time=0.6)
            shown.append(n)
        lock = label("wait for PLL lock …", 22, POWER).next_to(rf[0], UP, buff=0.25)
        self.play(FadeIn(lock))
        self.wait(0.4)
        locked = label("locked ✓", 22, MEAS).move_to(lock)
        self.play(Transform(lock, locked))
        self.wait(0.3)

        # --- 4. RF wave --------------------------------------------------------
        cap.show("4  The RF wave travels to the cable, part of it is reflected")
        fwd_path = Line(rf[0].get_center(), rf[5].get_left(), stroke_width=0)
        wave = pulse_dot(RF, 0.13)
        self.play(MoveAlongPath(wave, fwd_path), run_time=2.0, rate_func=linear)
        refl = pulse_dot(RF, 0.08).set_opacity(0.8).move_to(rf[5].get_left())
        self.play(FadeOut(wave), FadeIn(refl))
        self.play(refl.animate.move_to(rf[4].get_center()), run_time=0.8, rate_func=linear)

        cap.show("5  The coupler samples the forward wave, then the reflected wave")
        fw_tag = token("P1_FWD", MEAS, 18).move_to(rf[4].get_center())
        self.play(FadeIn(fw_tag))
        self.play(fw_tag.animate.move_to(det.get_center()), run_time=0.7)
        fw_code = label("ref  = 1500", 22, MEAS).next_to(det, LEFT, buff=0.35)
        self.play(FadeOut(fw_tag), FadeIn(fw_code))
        bw_tag = token("P1_BWD", MEAS, 18).move_to(rf[4].get_center())
        self.play(FadeOut(refl), FadeIn(bw_tag))
        self.play(bw_tag.animate.move_to(det.get_center()), run_time=0.7)
        bw_code = label("refl = 1871", 22, MEAS).next_to(fw_code, DOWN, buff=0.12, aligned_edge=LEFT)
        self.play(FadeOut(bw_tag), FadeIn(bw_code))
        med = label("median of 16 ADC samples each", 20, MUTED).next_to(bw_code, DOWN, buff=0.12).align_to(bw_code, RIGHT)
        self.play(FadeIn(med))
        self.wait(0.6)

        # --- 6. reply ------------------------------------------------------------
        cap.show("6  Raw codes go back to core 0 through the reply queue")
        codes = VGroup(fw_code, bw_code)
        raw = token("1500 · 1871", MEAS, 18).move_to(det.get_center())
        self.play(FadeOut(med), ReplacementTransform(codes, raw))
        self.play(raw.animate.move_to(c1.get_center()), run_time=0.6)
        self.play(raw.animate.move_to(rep_q[-1]), run_time=0.5)
        self.play(raw.animate.move_to(rep_q[0]), run_time=0.6)
        self.play(raw.animate.move_to(c0.get_center()), run_time=0.5)
        self.play(FadeOut(raw), flash_box(c0, CORE0))

        # --- 7. maths --------------------------------------------------------------
        cap.show("7  Core 0 turns the two codes into dB and applies the calibration")
        self.play(*[FadeOut(m) for m in shown], FadeOut(lock))
        panel = RoundedRectangle(corner_radius=0.12, width=6.2, height=1.75, stroke_color=CORE0,
                                 stroke_width=2, fill_color=PANEL, fill_opacity=1).move_to([-3.2, 0.0, 0])
        l1 = code("(1871 − 1500) / −30.4  = −12.2 dB", 22, TEXT)
        l2 = code("−12.2 dB − cal(1 GHz) 0.1 = −12.3 dB", 22, TEXT)
        lines = VGroup(l1, l2).arrange(DOWN, buff=0.22, aligned_edge=LEFT).move_to(panel)
        if lines.width > panel.width - 0.3:
            lines.scale_to_fit_width(panel.width - 0.3)
        self.play(FadeIn(panel), FadeIn(l1))
        self.wait(0.6)
        self.play(FadeIn(l2))
        self.wait(1.0)

        # --- 8. JSON point to chart ------------------------------------------------
        cap.show("8  One JSON line per point goes back to the client")
        self.play(FadeOut(VGroup(panel, lines)))
        out = token('{"pt":0,"f":1000000000,"s11_db":-12.3}', CORE0, 20).move_to([-2.2, 0.35, 0])
        self.play(FadeIn(out, shift=DOWN * 0.2))
        self.play(out.animate.scale(0.5).move_to(pc.get_center()), run_time=1.0)
        self.play(FadeOut(out), flash_box(pc, HOST))

        ax = Axes(x_range=[0, 6, 1], y_range=[-30, 0, 10], x_length=4.2, y_length=1.2,
                  axis_config={"color": LINE, "stroke_width": 2, "include_ticks": False},
                  tips=False).move_to([-4.4, 0.2, 0])
        ax_bg = RoundedRectangle(corner_radius=0.1, width=4.9, height=1.7, stroke_color=HOST,
                                 stroke_width=2, fill_color=PANEL, fill_opacity=1).move_to(ax)
        ylab = label("S11 (dB) over frequency", 20, MUTED).next_to(ax_bg, RIGHT, buff=0.3).align_to(ax_bg, UP)
        self.play(FadeIn(ax_bg), Create(ax), FadeIn(ylab))
        pt = Dot(ax.c2p(1, -12.3), radius=0.08, color=HOST)
        self.play(GrowFromCenter(pt))
        val = label("−12.3 dB @ 1 GHz", 22, HOST).next_to(ax_bg, RIGHT, buff=0.3).shift(DOWN * 0.3)
        val_arrow = Arrow(val.get_left(), pt.get_center(), buff=0.12, color=HOST, stroke_width=2,
                          max_tip_length_to_length_ratio=0.08)
        self.play(FadeIn(val), GrowArrow(val_arrow))
        self.wait(0.8)
        self.next_section()
        self.play(FadeOut(val), FadeOut(val_arrow))
        cap.show("…repeated for every frequency of the sweep: ≈ 2 ms per point", step=False)
        f = lambda x: -8.16 + 5 * math.cos(2.2 * x) - 1.2 * x
        rest = VGroup(*[Dot(ax.c2p(x, f(x)), radius=0.05, color=HOST)
                        for x in np.linspace(0.1, 5.9, 45) if abs(x - 1) > 0.08])
        self.play(LaggedStart(*[GrowFromCenter(d) for d in rest], lag_ratio=0.08), run_time=2.5)
        self.wait(1.5)


# ---------------------------------------------------------------------------
# 2. Sweep: two cores in a pipeline
# ---------------------------------------------------------------------------
def lowpass_s21(f_ghz):
    """S21 in dB of a ~1.8 GHz low-pass filter, the demo DUT."""
    x = f_ghz / 1.8
    db = -10 * math.log10(1 + x ** 14) - 0.4 * f_ghz / 6
    return max(db, -52 + 2.0 * math.sin(5 * f_ghz))


class SweepPipeline(Scene):
    def construct(self):
        cap = Captions(self)
        title = title_card("A sweep, point by point", "two CPU cores work in a pipeline")
        self.play(FadeIn(title))
        self.wait(0.5)
        self.play(title.animate.scale(0.7).to_edge(UP, buff=0.2))

        # --- A: one point, exploded ---------------------------------------------
        cap.show("Core 1 measures one point (all four S-parameters)")
        u = 5.5  # units per ms
        segs = [("retune", 0.43, 0.30), ("PLL lock", 0.70, 0.50), ("settle", 0.20, 0.18), ("6 path reads", 0.70, 0.38)]
        bar = VGroup()
        times = VGroup()
        x = -5.6
        for name, ms, op in segs:
            r = Rectangle(width=ms * u, height=0.8, stroke_color=CORE1, stroke_width=2,
                          fill_color=CORE1, fill_opacity=op).move_to([x + ms * u / 2, 1.0, 0])
            t = label(name, 22, TEXT).move_to(r)
            if t.width > r.width - 0.1:
                t.scale_to_fit_width(r.width - 0.1)
            tm = label(f"{ms:.2f} ms", 20, MUTED).next_to(r, DOWN, buff=0.15)
            bar.add(VGroup(r, t))
            times.add(tm)
            x += ms * u
        c1_tag = label("Core 1", 24, CORE1, weight=BOLD).next_to(bar, UP, buff=0.25).align_to(bar, LEFT)
        self.play(FadeIn(c1_tag))
        for b, tm in zip(bar, times):
            self.play(GrowFromEdge(b, LEFT), FadeIn(tm), run_time=0.7)
        brace = Brace(bar, DOWN, buff=0.7, color=LINE)
        total = label("≈ 2 ms per point", 26, TEXT, weight=BOLD).next_to(brace, DOWN, buff=0.15)
        self.play(GrowFromCenter(brace), FadeIn(total))
        self.wait(1.5)

        # --- B: pipeline -----------------------------------------------------------
        cap.show("While core 1 measures point N+1, core 0 sends point N")
        self.play(FadeOut(VGroup(bar, times, brace, total, c1_tag)))
        u = 1.75
        t0 = -5.2
        y1, y0 = 1.3, 0.2
        lane1 = label("Core 1", 24, CORE1, weight=BOLD).move_to([-6.3, y1, 0])
        lane0 = label("Core 0", 24, CORE0, weight=BOLD).move_to([-6.3, y0, 0])
        axis = Line([t0, -0.55, 0], [6.7, -0.55, 0], color=LINE, stroke_width=2)
        ticks = VGroup()
        for ms in range(0, 7):
            xx = t0 + ms * u
            ticks.add(Line([xx, -0.62, 0], [xx, -0.48, 0], color=LINE, stroke_width=2))
            ticks.add(label(f"{ms}", 20, MUTED).move_to([xx, -0.85, 0]))
        ms_l = label("ms", 20, MUTED).move_to([6.75, -0.85, 0])
        self.play(FadeIn(lane1), FadeIn(lane0), Create(axis), FadeIn(ticks), FadeIn(ms_l))
        p1 = VGroup()
        p0 = VGroup()
        links = VGroup()
        for i in range(3):
            a = t0 + i * 2.03 * u
            r1 = Rectangle(width=2.03 * u, height=0.65, stroke_color=CORE1, stroke_width=2,
                           fill_color=CORE1, fill_opacity=0.45 if i % 2 == 0 else 0.25).move_to([a + 1.015 * u, y1, 0])
            l1 = label(f"measure point {i}", 22).move_to(r1)
            p1.add(VGroup(r1, l1))
            b = a + 2.03 * u
            r0 = Rectangle(width=0.7 * u, height=0.65, stroke_color=CORE0, stroke_width=2,
                           fill_color=CORE0, fill_opacity=0.45 if i % 2 == 0 else 0.25).move_to([b + 0.35 * u, y0, 0])
            l0 = label(f"send {i}", 20).move_to(r0)
            p0.add(VGroup(r0, l0))
            links.add(DashedLine([b, y1 - 0.33, 0], [b, y0 + 0.33, 0], color=LINE, stroke_width=2))
        for i in range(3):
            self.play(GrowFromEdge(p1[i], LEFT), run_time=0.9)
            self.play(Create(links[i]), GrowFromEdge(p0[i], LEFT), run_time=0.6)
        hl = SurroundingRectangle(VGroup(p1[1], p0[0]), color=POWER, buff=0.12, corner_radius=0.08)
        hl_l = label("at the same time", 22, POWER).next_to(hl, UP, buff=0.1)
        self.play(Create(hl), FadeIn(hl_l))
        note = label("core 0: dB + calibration + JSON + USB ≈ 0.5–0.8 ms", 22, CORE0).move_to([0, -1.6, 0])
        self.play(FadeIn(note))
        self.wait(2)

        # --- C: queue + chart ---------------------------------------------------------
        self.next_section()
        self.play(FadeOut(VGroup(lane1, lane0, axis, ticks, ms_l, p1, p0, links, hl, hl_l, note)))
        cap.show("Every point travels through a reply queue with 8 places", step=False)
        yb = 2.2
        c1 = block("Core 1", "measures", CORE1, w=2.0, h=0.95).move_to([-5.6, yb, 0])
        c0 = block("Core 0", "sends", CORE0, w=2.0, h=0.95).move_to([3.0, yb, 0])
        slots = queue_slots(8, 0.5, CORE0, 0.08).move_to([-1.3, yb, 0])
        q_l = label("reply queue (8)", 20, MUTED).next_to(slots, UP, buff=0.12)
        pc = block("PC", "chart", HOST, w=1.5, h=0.95).move_to([5.9, yb, 0])
        l_a = Line(c1.get_right(), slots.get_left(), color=LINE, stroke_width=2)
        l_b = Line(slots.get_right(), c0.get_left(), color=LINE, stroke_width=2)
        l_c = Line(c0.get_right(), pc.get_left(), color=LINE, stroke_width=2)
        ax = Axes(x_range=[0, 6, 1], y_range=[-60, 5, 20], x_length=11.0, y_length=3.0,
                  axis_config={"color": LINE, "stroke_width": 2, "include_ticks": False}, tips=False).move_to([0.3, -1.1, 0])
        yl = label("S21 (dB)", 20, MUTED).next_to(ax, LEFT, buff=0.1).align_to(ax, UP)
        zl = label("0 dB", 20, MUTED).next_to(ax.c2p(6, 0), RIGHT, buff=0.1)
        xl = label("frequency 0 – 6 GHz", 20, MUTED).next_to(ax, DOWN, buff=0.08).align_to(ax, RIGHT)
        self.play(FadeIn(c1), FadeIn(c0), FadeIn(pc), FadeIn(slots), FadeIn(q_l), Create(l_a), Create(l_b), Create(l_c))
        self.play(Create(ax), FadeIn(yl), FadeIn(xl), FadeIn(zl))

        n_pts = 60
        freqs = np.linspace(0.05, 6.0, n_pts)

        def chart_dot(i):
            return Dot(ax.c2p(freqs[i], lowpass_s21(freqs[i])), radius=0.05, color=HOST)

        def point_trip(i, rt=1.0):
            tok = Square(0.36, stroke_width=0, fill_color=CORE1, fill_opacity=1).move_to(c1.get_right() + RIGHT * 0.25).set_z_index(20)
            d = chart_dot(i)
            return d, Succession(
                FadeIn(tok, run_time=0.05 * rt),
                tok.animate(run_time=0.35 * rt, rate_func=linear).move_to(slots[-1]),
                tok.animate(run_time=0.2 * rt, rate_func=linear).move_to(c0.get_left() + LEFT * 0.2).set_fill(CORE0),
                FadeOut(tok, run_time=0.05 * rt),
                GrowFromCenter(d, run_time=0.25 * rt),
            )

        first = [point_trip(i, 1.4)[1] for i in range(6)]
        self.play(LaggedStart(*first, lag_ratio=0.35))
        fast = [point_trip(i, 0.6)[1] for i in range(6, 34)]
        self.play(LaggedStart(*fast, lag_ratio=0.25))

        # --- backpressure ---------------------------------------------------------
        cap.show("If USB stalls, the queue fills up and core 1 waits: no point is lost", POWER)
        stall = label("USB busy", 22, POWER).next_to(c0, DOWN, buff=0.15)
        self.play(FadeIn(stall), c0[0].animate.set_stroke(POWER))
        toks = []
        for k in range(8):
            tok = Square(0.36, stroke_width=0, fill_color=CORE1, fill_opacity=1).move_to(c1.get_right() + RIGHT * 0.25).set_z_index(20)
            self.add(tok)
            self.play(tok.animate.move_to(slots[7 - k]), run_time=0.22, rate_func=linear)
            toks.append(tok)
        wait_l = label("queue full → wait", 22, POWER).next_to(c1, DOWN, buff=0.15)
        self.play(FadeIn(wait_l), c1[0].animate.set_stroke(POWER))
        self.wait(1.2)
        self.play(FadeOut(stall), c0[0].animate.set_stroke(CORE0))
        step = slots[1].get_x() - slots[0].get_x()
        idx = 34
        for k in range(8):
            head = toks.pop(0)
            d = chart_dot(idx)
            idx += 1
            self.play(head.animate.move_to(c0.get_left() + LEFT * 0.2).set_fill(CORE0),
                      *[t.animate.shift(RIGHT * step) for t in toks], run_time=0.22, rate_func=linear)
            self.play(FadeOut(head), GrowFromCenter(d), run_time=0.15)
            if k == 0:
                self.play(FadeOut(wait_l), c1[0].animate.set_stroke(CORE1), run_time=0.2)
        cap.show("The sweep finishes, the client draws the whole curve")
        rest = [point_trip(i, 0.5)[1] for i in range(idx, n_pts)]
        self.play(LaggedStart(*rest, lag_ratio=0.25))
        curve = ax.plot(lowpass_s21, x_range=[0.05, 6.0, 0.02], color=HOST, stroke_width=3)
        self.play(Create(curve), run_time=1.2)
        tag = label("1.8 GHz low-pass filter", 22, HOST).move_to(ax.c2p(4.3, -20))
        self.play(FadeIn(tag))
        self.wait(2)


# ---------------------------------------------------------------------------
# 3. Calibration
# ---------------------------------------------------------------------------
def cal_s11(f):   # systematic error of the S11 path (switches, coupler, cables)
    return -0.9 - 0.35 * f + 0.25 * math.sin(1.3 * f)


def ripple(f):    # mismatch ripple: opposite sign for open and short
    return 1.1 * math.sin(3.2 * f + 0.4)


def cal_s21(f):   # thru loss of the S21 path
    return -1.2 - 0.55 * f + 0.2 * math.sin(2.0 * f)


def cable(f):     # the DUT: a 75-ohm cable's true loss
    return -0.25 * f - 0.3 * math.sqrt(f)


class Calibration(Scene):
    def construct(self):
        cap = Captions(self)
        title = title_card("Calibration", "removing the analyzer's own losses")
        self.play(FadeIn(title))
        self.wait(0.5)
        self.play(title.animate.scale(0.7).to_edge(UP, buff=0.2))

        # --- the 5 capture steps -------------------------------------------------
        cap.show("Client wizard: 5 raw sweeps with known standards (per impedance)")
        steps = [("Open", "port 1"), ("Short", "port 1"), ("Open", "port 2"), ("Short", "port 2"), ("Thru", "port 1 ↔ 2")]
        chips = VGroup(*[block(n, s, MEAS if n != "Thru" else HOST, w=2.2, h=1.0, title_size=24, sub_size=18)
                         for n, s in steps]).arrange(RIGHT, buff=0.3).move_to([0, 1.6, 0])
        self.play(LaggedStart(*[FadeIn(c, shift=UP * 0.2) for c in chips], lag_ratio=0.15))
        ideal = VGroup(
            label("Open / Short: all power is reflected → ideal S11 = 0 dB", 24, MEAS),
            label("Thru: all power arrives at the other port → ideal S21 = 0 dB", 24, HOST),
        ).arrange(DOWN, buff=0.25, aligned_edge=LEFT).move_to([0, 0.0, 0])
        self.play(FadeIn(ideal[0]))
        self.play(FadeIn(ideal[1]))
        self.wait(1.5)
        self.play(FadeOut(ideal), chips.animate.scale(0.7).move_to([0, 2.42, 0]))

        # --- S11 correction ----------------------------------------------------------
        ax = Axes(x_range=[0, 6, 1], y_range=[-5, 2, 1], x_length=9.5, y_length=4.0,
                  axis_config={"color": LINE, "stroke_width": 2, "include_ticks": False}, tips=False).move_to([-1.0, -0.55, 0])
        yl = label("dB", 20, MUTED).next_to(ax, LEFT, buff=0.1).align_to(ax, UP)
        xl = label("frequency 0 – 6 GHz", 20, MUTED).next_to(ax, DOWN, buff=0.08).align_to(ax, RIGHT)
        zl = label("0 dB = ideal", 20, MUTED).next_to(ax.c2p(6, 0), RIGHT, buff=0.1)
        self.play(Create(ax), FadeIn(yl), FadeIn(xl), FadeIn(zl))
        cap.show("Raw S11 with Open and with Short: real paths have loss and ripple")
        o = ax.plot(lambda f: cal_s11(f) + ripple(f), x_range=[0.05, 6, 0.02], color=MEAS, stroke_width=3)
        s = ax.plot(lambda f: cal_s11(f) - ripple(f), x_range=[0.05, 6, 0.02], color=POWER, stroke_width=3)
        ol = label("Open", 22, MEAS).next_to(ax.c2p(6, cal_s11(6) + ripple(6)), RIGHT, buff=0.1)
        sl = label("Short", 22, POWER).next_to(ax.c2p(6, cal_s11(6) - ripple(6)), RIGHT, buff=0.1)
        self.play(FadeOut(zl), chips[0][0].animate.set_stroke(MEAS, 6))
        self.play(Create(o), FadeIn(ol), run_time=1.5)
        self.play(chips[0][0].animate.set_stroke(MEAS, 3), chips[1][0].animate.set_stroke(POWER, 6))
        self.play(Create(s), FadeIn(sl), run_time=1.5)
        self.play(chips[1][0].animate.set_stroke(MEAS, 3))
        cap.show("S11 correction = average of Open and Short: the ripple cancels")
        avg = ax.plot(cal_s11, x_range=[0.05, 6, 0.02], color=TEXT, stroke_width=5)
        al = label("S11 correction", 22, TEXT, weight=BOLD).next_to(ax.c2p(6, cal_s11(6)), RIGHT, buff=0.1)
        self.play(ReplacementTransform(VGroup(o.copy(), s.copy()), avg), FadeIn(al), run_time=1.5)
        self.play(o.animate.set_stroke(opacity=0.25), s.animate.set_stroke(opacity=0.25), ol.animate.set_opacity(0.3),
                  sl.animate.set_opacity(0.3))
        same = label("port 2: Open + Short → S22 correction (the same way)", 22, MUTED).move_to([-1.0, 1.6, 0])
        self.play(FadeIn(same))
        self.wait(1.5)
        self.play(FadeOut(VGroup(o, s, ol, sl, same)))

        cap.show("S21 / S12 correction = the Thru reading itself")
        self.play(chips[4][0].animate.set_stroke(HOST, 6))
        thru = ax.plot(cal_s21, x_range=[0.05, 6, 0.02], color=HOST, stroke_width=5)
        tl = label("S21 correction", 22, HOST, weight=BOLD).next_to(ax.c2p(6, cal_s21(6)), RIGHT, buff=0.1)
        self.play(Create(thru), FadeIn(tl), run_time=1.5)
        self.play(chips[4][0].animate.set_stroke(HOST, 3))
        self.wait(1)

        # --- upload into the EEPROM ---------------------------------------------------
        cap.show("cal_set uploads each curve as 240 points (0.01 dB steps) into the EEPROM")
        grid_dots = VGroup(*[Dot(ax.c2p(f, cal_s11(f)), radius=0.03, color=TEXT)
                             for f in np.linspace(0.05, 6, 240)])
        self.play(FadeIn(grid_dots), avg.animate.set_stroke(opacity=0.3), run_time=1)
        self.wait(0.6)
        self.play(FadeOut(VGroup(ax, yl, xl, thru, tl, avg, al, chips)), grid_dots.animate.scale(0.3).move_to([-4.9, -1.5, 0]))
        names = [["s11_50", "s21_50", "s12_50", "s22_50"], ["s11_75", "s21_75", "s12_75", "s22_75"]]
        cells = VGroup()
        for r, row in enumerate(names):
            for c, n in enumerate(row):
                cell = VGroup(Rectangle(width=1.55, height=0.75, stroke_color=LINE, stroke_width=2,
                                        fill_color=PANEL, fill_opacity=1), code(n, 20, MUTED))
                cell[1].move_to(cell[0])
                cell.move_to([-2.4 + c * 1.6, 1.0 - r * 0.8, 0])
                cells.add(cell)
        ee_box = SurroundingRectangle(cells, color=POWER, buff=0.25, corner_radius=0.1)
        ee_l = label("EEPROM 24AA32A · 4 KB · 8 curves × 240 points", 22, POWER).next_to(ee_box, UP, buff=0.12)
        rows_l = VGroup(label("50 Ω", 20, MUTED).next_to(cells[0], LEFT, buff=0.35),
                        label("75 Ω", 20, MUTED).next_to(cells[4], LEFT, buff=0.35))
        self.play(Create(ee_box), FadeIn(ee_l), FadeIn(cells), FadeIn(rows_l))
        pkt = token('{"cmd":"cal_set","set":"s11_50","points":[…240…]}', HOST, 20).move_to([0, -1.2, 0])
        self.play(FadeIn(pkt, shift=UP * 0.2), FadeOut(grid_dots))
        self.play(pkt.animate.scale(0.25).move_to(cells[0]), run_time=1)
        self.play(FadeOut(pkt), cells[0][0].animate.set_fill(MEAS, 0.35).set_stroke(MEAS),
                  cells[0][1].animate.set_color(TEXT))
        others = [1, 2, 3]
        self.play(*[cells[i][0].animate.set_fill(MEAS, 0.35).set_stroke(MEAS) for i in others],
                  *[cells[i][1].animate.set_color(TEXT) for i in others])
        self.wait(1)
        self.play(FadeOut(VGroup(ee_box, ee_l, cells, rows_l)))

        # --- using it in a sweep ------------------------------------------------------------
        cap.show("Measuring a cable: the raw S21 contains the cable AND the analyzer's loss")
        ax2 = Axes(x_range=[0, 6, 1], y_range=[-9, 1, 1], x_length=9.5, y_length=4.6,
                   axis_config={"color": LINE, "stroke_width": 2, "include_ticks": False}, tips=False).move_to([-1.0, -0.3, 0])
        yl2 = label("S21 (dB)", 20, MUTED).next_to(ax2, LEFT, buff=0.1).align_to(ax2, UP)
        xl2 = label("frequency 0 – 6 GHz", 20, MUTED).next_to(ax2, DOWN, buff=0.08).align_to(ax2, RIGHT)
        self.play(Create(ax2), FadeIn(yl2), FadeIn(xl2))
        raw = ax2.plot(lambda f: cal_s21(f) + cable(f), x_range=[0.05, 6, 0.02], color=RF, stroke_width=4)
        rl = label("raw", 22, RF).next_to(ax2.c2p(6, cal_s21(6) + cable(6)), RIGHT, buff=0.1)
        corr = ax2.plot(cal_s21, x_range=[0.05, 6, 0.02], color=HOST, stroke_width=3)
        cl = label("correction", 22, HOST).next_to(ax2.c2p(6, cal_s21(6)), RIGHT, buff=0.1)
        self.play(Create(raw), FadeIn(rl), run_time=1.5)
        self.play(Create(corr), FadeIn(cl), run_time=1.2)

        cap.show("The device interpolates the stored curve at every sweep frequency")
        gfs = np.linspace(0.05, 6, 13)
        gd = VGroup(*[Dot(ax2.c2p(f, cal_s21(f)), radius=0.06, color=HOST) for f in gfs])
        self.play(FadeIn(gd))
        fa, fb = gfs[6], gfs[7]
        fx = fa + 0.38 * (fb - fa)
        seg = Line(ax2.c2p(fa, cal_s21(fa)), ax2.c2p(fb, cal_s21(fb)), color=POWER, stroke_width=4)
        vline = DashedLine(ax2.c2p(fx, 0.8), ax2.c2p(fx, -8.8), color=POWER, stroke_width=2)
        ip = Dot(ax2.c2p(fx, cal_s21(fa) + 0.38 * (cal_s21(fb) - cal_s21(fa))), radius=0.09, color=POWER)
        ipl = label("sweep point between two stored points", 20, POWER).next_to(ax2.c2p(fx, 0.8), RIGHT, buff=0.15)
        self.play(Create(vline), FadeIn(ipl))
        self.play(Create(seg), GrowFromCenter(ip))
        self.wait(1.2)
        self.play(FadeOut(VGroup(gd, seg, vline, ip, ipl)))

        cap.show("corrected = raw − correction  →  only the cable is left")
        fixed = ax2.plot(cable, x_range=[0.05, 6, 0.02], color=MEAS, stroke_width=5)
        fl = label("corrected = cable", 22, MEAS, weight=BOLD).next_to(ax2.c2p(6, cable(6)), RIGHT, buff=0.1)
        self.play(ReplacementTransform(raw.copy(), fixed), FadeOut(rl), run_time=2)
        self.play(FadeIn(fl), raw.animate.set_stroke(opacity=0.25), corr.animate.set_stroke(opacity=0.4))
        self.wait(2.5)


# ---------------------------------------------------------------------------
# 4. Source safety: the order of switching matters
# ---------------------------------------------------------------------------
class SourceSafety(Scene):
    def construct(self):
        cap = Captions(self)
        title = title_card("Switching the RF source safely", "the order of two steps matters")
        self.play(FadeIn(title))
        self.wait(0.5)
        self.play(title.animate.scale(0.7).to_edge(UP, buff=0.2))

        # --- state machine ------------------------------------------------------------
        cap.show("Three source states")
        info = {
            "OFF": "synth off · terminated · atten 127",
            "PARKED": "synth running · terminated · atten 127",
            "ON": "synth running · routed to a port · atten = level",
        }
        st = {}
        for i, n in enumerate(["OFF", "PARKED", "ON"]):
            st[n] = block(n, None, [LINE, POWER, MEAS][i], w=2.2, h=0.8, title_size=26).move_to([-4.5 + 4.5 * i, 1.2, 0])
        subs = VGroup(*[label(info[n], 20, MUTED).next_to(st[n], DOWN, buff=0.2) for n in st])
        for sub in subs:
            if sub.width > 4.2:
                sub.scale_to_fit_width(4.2)
        a1 = DoubleArrow(st["OFF"].get_right(), st["PARKED"].get_left(), color=LINE, buff=0.1, stroke_width=3,
                         tip_length=0.18)
        a2 = DoubleArrow(st["PARKED"].get_right(), st["ON"].get_left(), color=LINE, buff=0.1, stroke_width=3,
                         tip_length=0.18)
        self.play(LaggedStart(*[FadeIn(st[n]) for n in st], lag_ratio=0.2), Create(a1), Create(a2))
        self.play(LaggedStart(*[FadeIn(sb) for sb in subs], lag_ratio=0.3))
        idle = label("between sweeps the source stays PARKED and running (stable temperature, fast retune)", 22,
                     TEXT).move_to([0, -1.0, 0])
        self.play(FadeIn(idle))
        self.wait(2.5)
        sm = VGroup(*st.values(), a1, a2)
        self.play(FadeOut(subs), FadeOut(idle), sm.animate.scale(0.6).move_to([0, 2.55, 0]))

        # --- the chain --------------------------------------------------------------------
        y = 0.3
        synth = block("MAX2871", "synth", RF, w=1.8, h=0.9, title_size=22).move_to([-6.0, y, 0])
        att = block("Attenuator", "atten 0", RF, w=2.0, h=0.9, title_size=22).move_to([-3.6, y, 0])
        amp = amplifier("+12 dB", RF).move_to([-1.2, y, 0])
        piv = np.array([1.4, y, 0])
        piv_d = Dot(piv, radius=0.08, color=TEXT)
        c_p1 = np.array([3.0, y + 1.0, 0])
        c_p2 = np.array([3.0, y, 0])
        c_t = np.array([3.0, y - 1.0, 0])
        cdots = VGroup(*[Dot(p, radius=0.07, color=TEXT) for p in (c_p1, c_p2, c_t)])
        port1 = block("Port 1", None, LINE, w=1.4, h=0.6, title_size=22).move_to([4.6, y + 1.0, 0])
        port2 = block("Port 2", None, LINE, w=1.4, h=0.6, title_size=22).move_to([4.6, y, 0])
        w1 = Line(c_p1, port1.get_left(), color=LINE, stroke_width=3)
        w2 = Line(c_p2, port2.get_left(), color=LINE, stroke_width=3)
        # termination resistor to ground
        res = resistor(c_t, c_t + RIGHT * 1.6, LINE)
        gnd_x = c_t + RIGHT * 1.6
        gnd = VGroup(Line(gnd_x, gnd_x + DOWN * 0.35, color=LINE, stroke_width=3), ground(gnd_x + DOWN * 0.35, LINE))
        res_l = label("50 Ω termination", 20, MUTED).next_to(res, DOWN, buff=0.45)
        sw_l = label("port switch", 20, MUTED).move_to(piv + DOWN * 1.6 + RIGHT * 0.2)
        lever = Line(piv, c_p1, color=TEXT, stroke_width=5)
        sig1 = Line(synth.get_right(), att.get_left(), color=RF, stroke_width=4)
        sig2 = Line(att.get_right(), amp.get_left(), color=RF, stroke_width=6)
        sig3 = Line(amp.get_right(), piv, color=RF, stroke_width=10)
        lvl = label("+13 dBm", 24, RF, weight=BOLD).next_to(sig3, UP, buff=0.15)
        p1_lvl = label("+13 dBm out", 20, RF).next_to(port1, UP, buff=0.12)
        self.play(LaggedStart(FadeIn(synth), FadeIn(att), FadeIn(amp), Create(sig1), Create(sig2), Create(sig3),
                              FadeIn(piv_d), FadeIn(cdots), FadeIn(port1), FadeIn(port2), Create(w1), Create(w2),
                              Create(res), Create(gnd), FadeIn(res_l), FadeIn(sw_l), Create(lever), lag_ratio=0.05))
        self.play(FadeIn(lvl), FadeIn(p1_lvl), st["ON"][0].animate.set_fill(MEAS, 0.35))
        cap.show("ON: full power goes out of port 1")
        self.wait(1.5)

        def set_att(v, text):
            new_sub = label(text, 17, MUTED).move_to(att[1][1])
            return Transform(att[1][1], new_sub)

        # --- wrong order -------------------------------------------------------------------
        cap.show("Parking the WRONG way: switch to the termination first …", BAD)
        self.play(Rotate(lever, angle=-(math.atan2(1.0, 1.6) * 2), about_point=piv), FadeOut(p1_lvl), run_time=0.8)
        hot = label("+13 dBm into the 50 Ω resistor!", 24, BAD, weight=BOLD).next_to(res_l, DOWN, buff=0.2)
        glow = VGroup(*[Circle(radius=0.3 + 0.18 * k, color=BAD, stroke_width=3).move_to(res.get_center())
                        .set_stroke(opacity=0.8 - 0.25 * k) for k in range(3)])
        self.play(res.animate.set_stroke(BAD, width=6), FadeIn(hot), FadeIn(glow))
        self.play(Wiggle(res), glow.animate.scale(1.25).set_stroke(opacity=0.2), run_time=1.2)
        cross = label("✗", 64, BAD, weight=BOLD).move_to([-4.0, -1.6, 0])
        self.play(FadeIn(cross, scale=1.5))
        self.wait(1)
        self.next_section()
        # back to ON
        self.play(FadeOut(VGroup(hot, glow, cross)), res.animate.set_stroke(LINE, width=3),
                  Rotate(lever, angle=(math.atan2(1.0, 1.6) * 2), about_point=piv), FadeIn(p1_lvl))

        # --- correct parking --------------------------------------------------------------
        cap.show("Parking the right way: 1. attenuator to 127 (−31.75 dB) …", MEAS, step=False)
        self.play(set_att(127, "atten 127"), sig2.animate.set_stroke(width=2), sig3.animate.set_stroke(width=2),
                  Transform(lvl, label("−19 dBm", 24, RF, weight=BOLD).move_to(lvl)),
                  Transform(p1_lvl, label("−19 dBm out", 20, RF).move_to(p1_lvl)), run_time=1.2)
        cap.show("2. … then switch to the termination: it only gets a weak signal", MEAS)
        self.play(Rotate(lever, angle=-(math.atan2(1.0, 1.6) * 2), about_point=piv), FadeOut(p1_lvl), run_time=0.8)
        ok = label("✓", 64, MEAS, weight=BOLD).move_to([-4.0, -1.6, 0])
        self.play(FadeIn(ok, scale=1.5), st["ON"][0].animate.set_fill(PANEL, 1),
                  st["PARKED"][0].animate.set_fill(POWER, 0.35))
        self.wait(1)
        self.play(FadeOut(ok))

        # --- routing ------------------------------------------------------------------------
        cap.show("Routing is the reverse: 1. switch to the port while still attenuated …", MEAS)
        p1_lvl2 = label("−19 dBm out", 20, RF).next_to(port1, UP, buff=0.12)
        self.play(Rotate(lever, angle=(math.atan2(1.0, 1.6) * 2), about_point=piv), FadeIn(p1_lvl2), run_time=0.8)
        cap.show("2. … then release the attenuator to the requested level", MEAS)
        self.play(set_att(40, "atten 40"), sig2.animate.set_stroke(width=5), sig3.animate.set_stroke(width=6),
                  Transform(lvl, label("+3 dBm", 24, RF, weight=BOLD).move_to(lvl)),
                  Transform(p1_lvl2, label("+3 dBm out", 20, RF).move_to(p1_lvl2)),
                  st["PARKED"][0].animate.set_fill(PANEL, 1), st["ON"][0].animate.set_fill(MEAS, 0.35), run_time=1.2)
        code_l = code("rf_state_set_source(OFF | PARKED | ON)  ← the only way to change it", 20, CORE1).move_to([0, -2.55, 0])
        self.play(FadeIn(code_l))
        self.wait(2)

        self.next_section()
        # --- power-up double write -----------------------------------------------------------
        self.play(FadeOut(VGroup(synth, att, amp, piv_d, cdots, port1, port2, w1, w2, res, gnd, res_l, sw_l, lever,
                                 sig1, sig2, sig3, lvl, p1_lvl2, code_l, sm)))
        cap.show("After a power-up (OFF → PARKED) the MAX2871 needs its registers written twice", step=False)
        yy = 0.6
        ax_line = Arrow([-6.4, yy - 0.9, 0], [6.6, yy - 0.9, 0], color=LINE, stroke_width=2, buff=0,
                        max_tip_length_to_length_ratio=0.02)
        t_l = label("time", 20, MUTED).next_to(ax_line, DOWN, buff=0.1).align_to(ax_line, RIGHT)
        ce = block("CE on", "power up", POWER, w=1.7, h=0.95, title_size=22).move_to([-5.4, yy, 0])
        w_a = block("write R5 … R0", "SPI", CORE1, w=2.4, h=0.95, title_size=22).move_to([-2.9, yy, 0])
        gap_a = np.array([-1.6, yy, 0])
        gap_b = np.array([2.0, yy, 0])
        gap = DashedLine(gap_a, gap_b, color=POWER, stroke_width=3)
        gap_t = label("wait ≥ 20 ms", 24, POWER, weight=BOLD).next_to(gap, UP, buff=0.15)
        w_b = block("write R5 … R0", "again", CORE1, w=2.4, h=0.95, title_size=22).move_to([3.3, yy, 0])
        lk = block("lock ✓", None, MEAS, w=1.3, h=0.95, title_size=22).move_to([5.6, yy, 0])
        self.play(Create(ax_line), FadeIn(t_l))
        self.play(FadeIn(ce))
        self.play(FadeIn(w_a))
        self.play(Create(gap), FadeIn(gap_t), run_time=1.2)
        self.play(FadeIn(w_b))
        self.play(FadeIn(lk))
        why = label("datasheet requirement: the VCO needs time to settle after power-up", 22, MUTED).move_to([0, -1.4, 0])
        once = label("done once per power-up; normal retunes write the registers once", 22, MUTED).move_to([0, -2.0, 0])
        self.play(FadeIn(why))
        self.play(FadeIn(once))
        self.wait(2.5)


# ---------------------------------------------------------------------------
# 5. Modes and the JSON protocol
# ---------------------------------------------------------------------------
class ModesAndCommands(Scene):
    def construct(self):
        cap = Captions(self)
        title = title_card("Modes and commands", "the host enters a mode, uses it, leaves it")
        self.play(FadeIn(title))
        self.wait(0.5)
        self.play(title.animate.scale(0.7).to_edge(UP, buff=0.2))

        # --- state machine (left) -------------------------------------------------------
        idle = Circle(radius=0.75, color=LINE, stroke_width=3, fill_color=PANEL, fill_opacity=1).move_to([-5.6, 0.2, 0])
        idle_t = label("IDLE", 26, TEXT, weight=BOLD).move_to(idle)
        modes = {}
        names = [("SWEEP", MEAS), ("GENERATOR", RF), ("POWER_METER", POWER), ("MANUAL", CORE1)]
        for i, (n, c) in enumerate(names):
            modes[n] = block(n, None, c, w=2.5, h=0.62, title_size=21).move_to([-2.7, 1.95 - i * 1.15, 0])
        arrows = VGroup()
        for n in modes:
            tgt = modes[n].get_left()
            d = tgt - idle.get_center()
            start = idle.get_center() + 0.78 * d / np.linalg.norm(d)
            arrows.add(Arrow(start, tgt, buff=0.05, color=LINE, stroke_width=2.5, tip_length=0.15,
                             max_tip_length_to_length_ratio=0.2))
        enter_l = label("mode_enter", 20, MUTED).move_to([-4.65, 2.3, 0])
        exit_l = label("mode_exit → back to IDLE", 20, MUTED).move_to([-4.3, -2.6, 0])
        cap.show("One mode owns the RF hardware at a time")
        self.play(FadeIn(idle), FadeIn(idle_t))
        self.play(LaggedStart(*[GrowArrow(a) for a in arrows], lag_ratio=0.15),
                  LaggedStart(*[FadeIn(modes[n]) for n in modes], lag_ratio=0.15), FadeIn(enter_l))
        self.play(FadeIn(exit_l))
        self.wait(1)

        # --- chat log (right) -------------------------------------------------------------
        panel = RoundedRectangle(corner_radius=0.12, width=8.2, height=5.5, stroke_color=LINE, stroke_width=2,
                                 fill_color=PANEL, fill_opacity=1).move_to([2.85, -0.3, 0])
        hdr = VGroup(label("PC → device", 20, HOST), label("device → PC", 20, CORE0)).arrange(RIGHT, buff=0.6)
        hdr.next_to(panel, UP, buff=0.08)
        self.play(FadeIn(panel), FadeIn(hdr))
        log = VGroup()
        top_y = panel.get_top()[1] - 0.35
        left_x = panel.get_left()[0] + 0.25

        def say(text, color, mark):
            line = VGroup(code(mark, 20, color), code(text, 20, color)).arrange(RIGHT, buff=0.15)
            if line.width > panel.width - 0.4:
                line.scale_to_fit_width(panel.width - 0.4)
            line.move_to([0, 0, 0])
            line.align_to([left_x, 0, 0], LEFT)
            line.set_y(top_y - len(log) * 0.48)
            log.add(line)
            self.play(FadeIn(line, shift=RIGHT * 0.2 if mark == "→" else LEFT * 0.2), run_time=0.45)
            return line

        def light(n, on=True):
            m = modes[n][0] if n != "IDLE" else idle
            col = dict(names).get(n, LINE)
            return m.animate.set_fill(col if on else PANEL, 0.35 if on else 1)

        cap.show("Enter the sweep mode")
        say('{"cmd":"mode_enter","id":1,"mode":"sweep"}', HOST, "→")
        self.play(Indicate(arrows[0], color=MEAS), light("SWEEP"))
        say('{"id":1,"ok":true,"mode":"sweep"}', CORE0, "←")
        cap.show("Start a sweep: the points stream back as they are measured")
        say('{"cmd":"sweep","id":2,"points":201,…}', HOST, "→")
        say('{"id":2,"pt":0,"s11_db":-12.3,…}', CORE0, "←")
        say('{"id":2,"pt":1,"s11_db":-11.9,…}', CORE0, "←")
        cap.show("A command of another mode during the sweep is refused", POWER)
        say('{"cmd":"gen_start","id":3,…}', HOST, "→")
        busy = say('{"id":3,"ok":false,"error":"busy"}', BAD, "←")
        self.play(Indicate(busy, color=BAD, scale_factor=1.05))
        say('{"id":2,"pt":2,"s11_db":-11.4,…}', CORE0, "←")
        cap.show("mode_exit cancels the running sweep and returns to IDLE")
        say('{"cmd":"mode_exit","id":4}', HOST, "→")
        say('{"id":2,"ok":false,"done":true,"error":"aborted"}', POWER, "←")
        say('{"id":4,"ok":true,"mode":"idle"}', CORE0, "←")
        self.play(light("SWEEP", False), Indicate(idle, color=TEXT))
        self.wait(1.5)

        cap.show("Same pattern for the other modes, e.g. the signal generator")
        self.play(FadeOut(log), run_time=0.4)
        log.remove(*log.submobjects)
        say('{"cmd":"mode_enter","id":5,"mode":"generator"}', HOST, "→")
        self.play(Indicate(arrows[1], color=RF), light("GENERATOR"))
        say('{"id":5,"ok":true,"mode":"generator"}', CORE0, "←")
        say('{"cmd":"gen_start","id":6,"port":1,"f":1000000000,…}', HOST, "→")
        say('{"id":6,"ok":true}', CORE0, "←")
        cap.show("Settings like the output level can be changed in every mode, even live")
        say('{"cmd":"set","id":7,"atten":40}', HOST, "→")
        say('{"id":7,"ok":true,"z":50,"atten":40}', CORE0, "←")
        cap.show("mode_exit stops the output and frees the hardware again")
        say('{"cmd":"mode_exit","id":8}', HOST, "→")
        say('{"id":8,"ok":true,"mode":"idle"}', CORE0, "←")
        self.play(light("GENERATOR", False), Indicate(idle, color=TEXT))
        self.wait(2.5)
