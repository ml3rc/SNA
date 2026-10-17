"""Hardware animations for the SNA class presentation.

Render (from this folder):
    .venv/Scripts/python.exe -m manim -qh --media_dir media hardware_scenes.py PllSynthesis
Scenes: PllSynthesis, DirectionalCoupler, LogDetector, ImpedancePads, PowerTree
"""
from hw_common import *


def at(x, y):
    return np.array([x, y, 0])


# ---------------------------------------------------------------------------
class PllSynthesis(Scene):
    """MAX2871: 100 MHz reference -> PLL -> VCO 3-6 GHz -> output divider."""

    def construct(self):
        title = title_card("MAX2871: PLL frequency synthesis")
        self.play(FadeIn(title, shift=DOWN * 0.2))

        W, H = 1.85, 1.1
        top, bot = 1.6, -0.5
        ref = block("Reference", "100 MHz", HOST, W, H, sub_size=20).move_to(at(-5.75, top))
        rdiv = block("÷R", "R = 1", LINE, W, H, sub_size=20).move_to(at(-3.55, top))
        pfd = block("PFD", "phase detector", LINE, W, H, sub_size=20).move_to(at(-1.35, top))
        cp = block("Charge pump", "", LINE, W, H, sub_size=20).move_to(at(0.85, top))
        lf = block("Loop filter", "450 kHz BW", LINE, W, H, sub_size=20).move_to(at(3.05, top))
        vco = block("VCO", "3 – 6 GHz", RF, W, H, sub_size=20).move_to(at(3.05, bot))
        odiv = block("÷DIV", "÷1 … ÷128", RF, 1.7, H, sub_size=20).move_to(at(5.45, bot))
        ndiv = block("÷N", "N + F/M", LINE, W, H, sub_size=20).move_to(at(-1.35, bot))

        w_ref = wire([(-4.825, top), (-4.475, top)], HOST)
        w_r = wire([(-2.625, top), (-2.275, top)], HOST)
        w_pfd = wire([(-0.425, top), (-0.075, top)])
        w_cp = wire([(1.775, top), (2.125, top)])
        w_lf = wire([(3.05, top - H / 2), (3.05, bot + H / 2)])
        w_vco = wire([(3.975, bot), (4.6, bot)], RF)
        w_fb = wire([(2.125, bot), (-0.425, bot)], RF)
        w_n = wire([(-1.35, bot + H / 2), (-1.35, top - H / 2)])
        w_out = wire([(5.45, bot - H / 2), (5.45, -1.75)], RF)
        out_lbl = label("RF out", 26, RF, weight=BOLD).move_to(at(5.45, -2.05))

        cap = caption("A 100 MHz crystal oscillator is the reference")
        self.play(FadeIn(ref), FadeIn(cap))
        self.play(Create(w_ref), FadeIn(rdiv), Create(w_r), FadeIn(pfd))
        self.next_section()
        self.play(Transform(cap, caption("The phase detector compares reference and feedback")))
        self.play(Create(w_pfd), FadeIn(cp), Create(w_cp), FadeIn(lf))
        self.next_section()
        self.play(Transform(cap, caption("Charge pump + loop filter turn the phase error into a tuning voltage")))
        self.play(Create(w_lf), FadeIn(vco))
        self.next_section()
        self.play(Transform(cap, caption("The VCO frequency is divided by N and fed back")))
        self.play(Create(w_fb), FadeIn(ndiv), Create(w_n))
        self.wait(0.5)

        # Pulses around the loop
        loop_pts = [(-1.35, top), (0.85, top), (3.05, top), (3.05, bot), (-1.35, bot), (-1.35, top)]
        for _ in range(2):
            travel(self, loop_pts, CORE1, run_time=1.6)

        # Locking: VCO frequency converges to N * fPFD
        t = ValueTracker(0)
        f_target = 4.0

        def f_of(tt):
            return f_target - 0.45 * np.exp(-6 * tt) * np.cos(12 * tt)

        readout = always_redraw(lambda: VGroup(
            label("f_VCO =", 28, MUTED),
            label(f"{f_of(t.get_value()):.3f} GHz", 28, RF, weight=BOLD, font=MONO),
        ).arrange(RIGHT, buff=0.2).move_to(at(-4.2, -2.35)))
        target = label("target: N × f_PFD = 40 × 100 MHz = 4.000 GHz", 24, MUTED).move_to(at(-2.9, -2.95))
        target.align_to(readout, LEFT)

        err_lbl = label("phase error", 22, MUTED).move_to(at(0.85, 0.75))
        err_bar = always_redraw(lambda: Rectangle(
            width=max(0.02, 2.2 * abs(f_of(t.get_value()) - f_target) / 0.45), height=0.16,
            stroke_width=0, fill_color=POWER, fill_opacity=1).move_to(at(0.85, 0.35)))

        ld_dot = Dot(radius=0.14, color=BAD).move_to(at(0.0, -2.35))
        ld_txt = label("LD: low", 24, BAD).next_to(ld_dot, RIGHT, buff=0.2)

        self.next_section()
        self.play(Transform(cap, caption("Closing the loop: the VCO is pulled onto N × f_PFD")))
        self.play(FadeIn(readout), FadeIn(target), FadeIn(err_lbl), FadeIn(err_bar), FadeIn(ld_dot), FadeIn(ld_txt))
        self.play(t.animate.set_value(1.0), run_time=4.5, rate_func=linear)
        ld_txt2 = label("LD: high → locked", 24, MEAS).next_to(ld_dot, RIGHT, buff=0.2)
        self.play(ld_dot.animate.set_color(MEAS), Transform(ld_txt, ld_txt2), FadeOut(err_bar), FadeOut(err_lbl))
        self.wait(0.8)

        # Output divider
        self.next_section()
        self.play(Transform(cap, caption("The output divider reaches the low frequencies")))
        self.play(Create(w_vco), FadeIn(odiv), Create(w_out), FadeIn(out_lbl))
        travel(self, [(3.05, bot), (5.45, bot), (5.45, -1.8)], RF, run_time=1.2)

        self.next_section()
        eq1 = VGroup(label("f_out = f_VCO ÷ DIV = 4 GHz ÷ 4 =", 26, TEXT),
                     label("1 GHz", 26, RF, weight=BOLD)).arrange(RIGHT, buff=0.15)
        eq1.move_to(at(-2.0, -2.35)).align_to(readout, LEFT)
        self.play(FadeOut(readout), FadeOut(target), FadeOut(ld_dot), FadeOut(ld_txt), FadeIn(eq1))
        eq2 = label("range: 3 GHz ÷ 128 = 23.4 MHz   …   6 GHz ÷ 1 = 6 GHz", 24, MUTED)
        eq2.next_to(eq1, DOWN, buff=0.25).align_to(eq1, LEFT)
        self.play(FadeIn(eq2))
        self.play(Transform(cap, caption("Fractional N (F/M) gives fine frequency steps; the SNA sweeps 50 MHz – 6 GHz")))
        self.wait(3)


# ---------------------------------------------------------------------------
class DirectionalCoupler(Scene):
    """One directional coupler + switch bank measures all four S-parameters."""

    def construct(self):
        title = title_card("Directional coupler: seeing one direction")
        self.play(FadeIn(title, shift=DOWN * 0.2))

        # ---- Part A: principle -------------------------------------------------
        y_main, y_cpl = 0.85, 0.45
        box = RoundedRectangle(corner_radius=0.12, width=3.6, height=2.3, stroke_color=MEAS,
                               stroke_width=3, fill_color=PANEL, fill_opacity=1).move_to(at(0, 0.45))
        name = label("SEDC-10-63+", 20, MUTED).move_to(at(0, 1.35))
        main = Line(at(-1.8, y_main), at(1.8, y_main), color=RF, stroke_width=4)
        cline = Line(at(-1.0, y_cpl), at(1.0, y_cpl), color=MEAS, stroke_width=3)
        cpl_out = Line(at(-1.0, y_cpl), at(-1.0, -0.7), color=MEAS, stroke_width=3)
        term = resistor(at(1.0, y_cpl), at(1.0, -0.45), MUTED)
        gnd = ground(at(1.0, -0.45), MUTED)
        term_lbl = label("50 Ω", 20, MUTED).move_to(at(1.48, 0.0))
        lbl_in = label("IN", 20, MUTED).move_to(at(-1.55, 1.1))
        lbl_out = label("OUT", 20, MUTED).move_to(at(1.45, 1.1))
        lbl_cpl = label("CPL", 20, MEAS).move_to(at(-0.65, -0.3))
        coupler = VGroup(box, name, main, cline, cpl_out, term, gnd, term_lbl, lbl_in, lbl_out, lbl_cpl)

        src = block("Source", None, RF, 2.0, 0.9, sub_size=20).move_to(at(-5.2, y_main))
        dut = block("DUT", None, LINE, 2.0, 0.9, sub_size=20).move_to(at(5.2, y_main))
        w1 = Line(at(-4.2, y_main), at(-1.8, y_main), color=RF, stroke_width=4)
        w2 = Line(at(1.8, y_main), at(4.2, y_main), color=RF, stroke_width=4)
        det = block("Detector", "AD8318", MEAS, 2.2, 0.85, sub_size=20).move_to(at(-1.0, -1.65))
        w_det = Line(at(-1.0, -0.7), at(-1.0, -1.225), color=MEAS, stroke_width=3)

        self.play(FadeIn(src), FadeIn(dut), Create(w1), Create(w2), FadeIn(coupler))
        self.play(FadeIn(det), Create(w_det))

        cap = caption("Forward wave (source → DUT): a −10 dB copy reaches CPL")
        self.play(FadeIn(cap))
        travel(self, [(-4.2, y_main), (-1.0, y_main)], RF, run_time=1.0)
        g1, a1 = travel_anim([(-1.0, y_main), (4.2, y_main)], RF, run_time=1.3)
        g2, a2 = travel_anim([(-1.0, y_cpl), (-1.0, -1.25)], MEAS, run_time=0.9, r=0.07)
        self.add(g1, g2)
        self.play(a1, a2)
        self.remove(g1, g2)
        hit = chip("measured", MEAS).next_to(det, RIGHT, buff=0.3)
        self.play(flash_fill(det, MEAS), FadeIn(hit))
        self.wait(0.5)

        self.next_section()
        self.play(Transform(cap, caption("Reflected wave (DUT → source): coupled into the internal 50 Ω, CPL stays dark")),
                  FadeOut(hit))
        travel(self, [(4.2, y_main), (1.0, y_main)], POWER, run_time=1.0)
        g1, a1 = travel_anim([(1.0, y_main), (-4.2, y_main)], POWER, run_time=1.3)
        g2, a2 = travel_anim([(1.0, y_cpl), (1.0, -0.4)], POWER, run_time=0.7, r=0.07)
        self.add(g1, g2)
        self.play(a1, a2)
        self.play(FadeOut(g2), run_time=0.3)
        self.remove(g1)
        no = chip("nothing", BAD).next_to(det, RIGHT, buff=0.3)
        self.play(FadeIn(no))
        self.wait(1.0)

        self.next_section()
        # ---- Part B: one coupler, six switches ---------------------------------
        part_a = VGroup(coupler, src, dut, w1, w2, det, w_det, no)
        title2 = title_card("One coupler, six SP4T switches")
        self.play(FadeOut(part_a), FadeOut(cap), Transform(title, title2))

        y1, y2, xe = 1.3, -1.3, 0.4
        source = block("Source", None, RF, 1.6, 0.9, sub_size=20).move_to(at(-5.7, 0))
        psw = block("Port", "switch", LINE, 1.5, 0.9, sub_size=20).move_to(at(-3.6, 0))
        w_s = wire([(-4.9, 0), (-4.35, 0)], RF)
        lane1 = VGroup(Line(at(-3.6, 0.45), at(-3.6, y1), color=LINE, stroke_width=3),
                       Line(at(-3.6, y1), at(-0.6, y1), color=LINE, stroke_width=3),
                       Line(at(1.4, y1), at(4.6, y1), color=LINE, stroke_width=3))
        lane2 = VGroup(Line(at(-3.6, -0.45), at(-3.6, y2), color=LINE, stroke_width=3),
                       Line(at(-3.6, y2), at(-0.6, y2), color=LINE, stroke_width=3),
                       Line(at(1.4, y2), at(4.6, y2), color=LINE, stroke_width=3))
        p1 = VGroup(Dot(at(3.6, y1), radius=0.1, color=TEXT), label("Port 1", 22, TEXT).move_to(at(3.6, y1 + 0.38)))
        p2 = VGroup(Dot(at(3.6, y2), radius=0.1, color=TEXT), label("Port 2", 22, TEXT).move_to(at(3.6, y2 - 0.38)))
        dut2 = block("DUT", "e.g. cable", LINE, 1.5, 3.2, sub_size=20).move_to(at(5.4, 0))
        detector = block("Detector", None, MEAS, 2.0, 0.8, sub_size=20).move_to(at(xe, 0))

        def make_coupler(direction):
            bx = RoundedRectangle(corner_radius=0.1, width=2.0, height=0.7, stroke_color=MEAS,
                                  stroke_width=3, fill_color=PANEL, fill_opacity=1)
            ar = Arrow(LEFT * 0.32, RIGHT * 0.32, buff=0, color=MEAS, stroke_width=5, tip_length=0.18,
                       max_tip_length_to_length_ratio=0.5)
            if direction < 0:
                ar.rotate(PI)
            txt = label("coupler", 20, TEXT)
            g = VGroup(bx, VGroup(txt, ar).arrange(RIGHT, buff=0.15).move_to(bx))
            return g

        def make_pad():
            bx = RoundedRectangle(corner_radius=0.1, width=2.0, height=0.7, stroke_color=LINE,
                                  stroke_width=3, fill_color=PANEL_2, fill_opacity=1)
            return VGroup(bx, label("5.6 dB pad", 20, MUTED).move_to(bx))

        cpl = make_coupler(+1).move_to(at(xe, y1))
        pad = make_pad().move_to(at(xe, y2))
        tap1 = Line(at(xe, y1 - 0.35), at(xe, 0.4), color=MEAS, stroke_width=3)
        tap2 = Line(at(xe, y2 + 0.35), at(xe, -0.4), color=MEAS, stroke_width=3)

        self.play(FadeIn(source), Create(w_s), FadeIn(psw), Create(lane1), Create(lane2),
                  FadeIn(p1), FadeIn(p2), FadeIn(dut2))
        bank = DashedVMobject(RoundedRectangle(corner_radius=0.15, width=2.6, height=3.75, stroke_color=MUTED,
                                               stroke_width=2), num_dashes=60).move_to(at(xe, 0))
        bank_lbl = label("6 × SP4T", 22, MUTED).move_to(at(-1.75, 1.62))
        self.play(FadeIn(detector), FadeIn(cpl), Create(tap1), FadeIn(pad), Create(bank), FadeIn(bank_lbl))
        cap = caption("The other port gets a pad with the same loss as switches + coupler")
        self.play(FadeIn(cap))
        self.wait(1.2)

        reads = VGroup()

        def show_path(name):
            c = chip(name, MEAS, 24).move_to(at(xe, 2.55))
            return c

        self.next_section()
        # P1_FWD
        tag = show_path("P1_FWD  ·  source on port 1")
        self.play(FadeIn(tag), Transform(cap, caption("Reference: the wave going out of port 1")))
        travel(self, [(-4.9, 0), (-3.6, 0), (-3.6, y1), (xe, y1)], RF, run_time=1.2)
        g1, a1 = travel_anim([(xe, y1), (4.6, y1)], RF, run_time=1.0)
        g2, a2 = travel_anim([(xe, y1 - 0.35), (xe, 0.42)], MEAS, run_time=0.7, r=0.07)
        self.add(g1, g2)
        self.play(a1, a2)
        self.remove(g1, g2)
        r1 = chip("P1_FWD = reference", MEAS, 22)
        reads.add(r1)
        r1.move_to(at(-4.3, -2.55))
        self.play(flash_fill(detector, MEAS), FadeIn(r1))

        self.next_section()
        # P1_BWD
        cpl_b = make_coupler(-1).move_to(at(xe, y1))
        tag2 = show_path("P1_BWD  ·  source on port 1")
        self.play(Transform(cpl, cpl_b), Transform(tag, tag2),
                  Transform(cap, caption("Switches flip the coupler: now it sees what comes back from port 1")))
        travel(self, [(-4.9, 0), (-3.6, 0), (-3.6, y1), (4.6, y1)], RF, run_time=1.4)
        travel(self, [(4.6, y1), (xe, y1)], POWER, run_time=0.9)
        g1, a1 = travel_anim([(xe, y1), (-0.6, y1)], POWER, run_time=0.4)
        g2, a2 = travel_anim([(xe, y1 - 0.35), (xe, 0.42)], MEAS, run_time=0.7, r=0.07)
        self.add(g1, g2)
        self.play(a1, a2)
        self.remove(g1, g2)
        r2 = chip("P1_BWD = reflected", POWER, 22).move_to(at(0.0, -2.55))
        reads.add(r2)
        self.play(flash_fill(detector, MEAS), FadeIn(r2))

        self.next_section()
        # P2_BWD: coupler moves to port 2's line, pad to port 1's line
        tag3 = show_path("P2_BWD  ·  source on port 1")
        cpl_new = make_coupler(-1).move_to(at(xe, y2))
        pad_new = make_pad().move_to(at(xe, y1))
        self.play(FadeOut(cpl, shift=RIGHT * 0.4), FadeOut(pad, shift=RIGHT * 0.4),
                  FadeIn(cpl_new, shift=RIGHT * 0.4), FadeIn(pad_new, shift=RIGHT * 0.4),
                  FadeOut(tap1), Create(tap2), Transform(tag, tag3),
                  Transform(cap, caption("Coupler moves to port 2: it catches what went through the DUT")),
                  run_time=1.5)
        travel(self, [(-4.9, 0), (-3.6, 0), (-3.6, y1), (5.4, y1), (5.4, y2), (xe, y2)], RF, run_time=2.4)
        g1, a1 = travel_anim([(xe, y2), (-0.6, y2)], RF, run_time=0.4)
        g2, a2 = travel_anim([(xe, y2 + 0.35), (xe, -0.42)], MEAS, run_time=0.7, r=0.07)
        self.add(g1, g2)
        self.play(a1, a2)
        self.remove(g1, g2)
        r3 = chip("P2_BWD = transmitted", RF, 22).move_to(at(4.3, -2.55))
        reads.add(r3)
        self.play(flash_fill(detector, MEAS), FadeIn(r3))
        self.wait(0.8)

        self.next_section()
        # Formulas
        f1 = VGroup(label("S11 = P1_BWD − P1_FWD", 26, TEXT, weight=BOLD),
                    label("S21 = P2_BWD − P1_FWD", 26, TEXT, weight=BOLD)).arrange(RIGHT, buff=1.0)
        f2 = label("source on port 2 (mirrored):  S22 = P2_BWD − P2_FWD    S12 = P1_BWD − P2_FWD", 22, MUTED)
        fs = VGroup(f1, f2).arrange(DOWN, buff=0.22).move_to(at(0, -2.75))
        self.play(FadeOut(cap), FadeOut(reads), FadeIn(f1))
        self.play(FadeIn(f2))
        self.wait(1.0)
        note = label("all values in dB", 20, MUTED).move_to(at(0, -3.6))
        self.play(FadeIn(note))
        self.wait(3)


# ---------------------------------------------------------------------------
class LogDetector(Scene):
    """AD8318: power in dBm -> voltage; median filter; ratios become subtractions."""

    SLOPE = -0.0245     # V/dB
    INTERCEPT = 20.0    # dBm (900 MHz typical)

    def v_of(self, p):
        return self.SLOPE * (p - self.INTERCEPT)

    def construct(self):
        title = title_card("Log detector: power → voltage")
        self.play(FadeIn(title, shift=DOWN * 0.2))

        ax = Axes(x_range=[0, 65, 10], y_range=[0, 2.5, 0.5], x_length=7.6, y_length=4.4,
                  axis_config={"color": LINE, "stroke_width": 2, "include_tip": False},
                  ).move_to(at(-1.5, -0.35))
        c2p = lambda pw, v: ax.c2p(pw + 60, v)
        ticks = VGroup(*[label(str(v), 20, MUTED).next_to(c2p(v, 0), DOWN, buff=0.15)
                         for v in (-60, -50, -40, -30, -20, -10, 0)],
                       *[label(f"{v:.1f}", 20, MUTED).next_to(c2p(-60, v), LEFT, buff=0.15)
                         for v in (0.5, 1.0, 1.5, 2.0, 2.5)])
        ax.add(ticks)
        xl = label("input power (dBm)", 22, MUTED).next_to(ax.get_x_axis(), DOWN, buff=0.45)
        yl = label("output (V)", 22, MUTED).rotate(PI / 2).next_to(ax.get_y_axis(), LEFT, buff=0.55)
        curve = ax.plot(lambda x: self.v_of(x - 60), x_range=[5, 60], color=MEAS, stroke_width=5)
        tail_l = DashedLine(c2p(-60, self.v_of(-55) + 0.02), c2p(-55, self.v_of(-55)), color=MUTED)
        tail_r = DashedLine(c2p(0, self.v_of(0)), c2p(5, self.v_of(0) - 0.03), color=MUTED)

        self.play(Create(ax), FadeIn(xl), FadeIn(yl))
        self.play(Create(curve), FadeIn(tail_l), FadeIn(tail_r), run_time=1.5)
        rng = label("usable: −55 … 0 dBm", 22, MEAS).move_to(c2p(-27, 2.3))
        self.play(FadeIn(rng))

        self.next_section()
        p = ValueTracker(-50)
        dot = always_redraw(lambda: Dot(c2p(p.get_value(), self.v_of(p.get_value())), radius=0.11, color=RF))
        vguide = always_redraw(lambda: DashedLine(c2p(p.get_value(), 0),
                                                  c2p(p.get_value(), self.v_of(p.get_value())),
                                                  color=RF, stroke_width=2))
        hguide = always_redraw(lambda: DashedLine(c2p(-60, self.v_of(p.get_value())),
                                                  c2p(p.get_value(), self.v_of(p.get_value())),
                                                  color=MEAS, stroke_width=2))

        def panel():
            pv = p.get_value()
            v = self.v_of(pv)
            rows = VGroup(
                VGroup(label("P_in", 24, MUTED), label(f"{pv:6.1f} dBm", 26, RF, font=MONO)).arrange(RIGHT, buff=0.3),
                VGroup(label("V_out", 24, MUTED), label(f"{v:5.2f} V", 26, MEAS, font=MONO)).arrange(RIGHT, buff=0.3),
                VGroup(label("ADC", 24, MUTED), label(f"{round(v * 4096 / 3.3):5d}", 26, CORE1, font=MONO)).arrange(RIGHT, buff=0.3),
            ).arrange(DOWN, aligned_edge=LEFT, buff=0.3)
            return rows.move_to(at(4.6, 0.9))

        info = always_redraw(panel)
        self.play(FadeIn(dot), FadeIn(vguide), FadeIn(hguide), FadeIn(info))
        self.play(p.animate.set_value(-5), run_time=3.5, rate_func=smooth)
        self.play(p.animate.set_value(-30), run_time=2.0, rate_func=smooth)

        self.next_section()
        facts = VGroup(
            label("more power → lower voltage", 24, TEXT),
            label("slope −24.5 mV/dB", 24, MEAS),
            label("12-bit ADC: −30.4 counts/dB", 24, CORE1),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.22).move_to(at(4.6, -1.4))
        for f in facts:
            self.play(FadeIn(f, shift=RIGHT * 0.2), run_time=0.7)
        self.wait(2)

        self.next_section()
        # ---- Median of 16 ------------------------------------------------------
        self.play(*[FadeOut(m) for m in [ax, xl, yl, curve, tail_l, tail_r, rng, dot, vguide, hguide, info, facts]])
        self.play(Transform(title, title_card("16 samples → median")))

        samples = [1502, 1498, 1505, 1499, 1586, 1501, 1497, 1503, 1500, 1496, 1504, 1431, 1500, 1502, 1499, 1501]
        base, scale = 1380, 0.02
        n = len(samples)
        xs = [-4.875 + i * 0.65 for i in range(n)]
        y0 = -2.3

        def bar(v, x, color=CORE1):
            h = (v - base) * scale
            return Rectangle(width=0.5, height=h, stroke_width=0, fill_color=color, fill_opacity=1) \
                .move_to(at(x, y0 + h / 2))

        bars = [bar(v, x) for v, x in zip(samples, xs)]
        for b, v in zip(bars, samples):
            if v in (1586, 1431):
                b.set_fill(BAD)
        baseline = Line(at(-5.3, y0), at(5.3, y0), color=LINE, stroke_width=2)
        self.play(Create(baseline))
        self.play(LaggedStart(*[GrowFromEdge(b, DOWN) for b in bars], lag_ratio=0.08), run_time=2.0)
        cap = caption("Noise and single spikes (red) disturb individual ADC readings")
        self.play(FadeIn(cap))
        self.wait(1.2)

        self.next_section()
        order = sorted(range(n), key=lambda i: samples[i])
        self.play(Transform(cap, caption("Sort the 16 values …")))
        self.play(*[bars[i].animate.move_to(at(xs[k], bars[i].get_center()[1])) for k, i in enumerate(order)],
                  run_time=2.0)
        mid = [bars[order[7]], bars[order[8]]]
        bracket = Brace(VGroup(*mid), UP, color=MEAS, buff=0.15)
        med = sorted(samples)
        med_val = (med[7] + med[8]) // 2
        med_lbl = label(f"median = {med_val}", 26, MEAS, weight=BOLD).next_to(bracket, UP, buff=0.12)
        self.play(Transform(cap, caption("… and take the middle: the spikes have no effect")),
                  *[m.animate.set_fill(MEAS) for m in mid], GrowFromCenter(bracket), FadeIn(med_lbl))
        mean = sum(samples) / n
        mean_lbl = label(f"(the mean would be {mean:.1f}, pulled by the spikes)", 22, MUTED).move_to(at(0, -2.75))
        self.play(FadeIn(mean_lbl))
        self.wait(2)

        self.next_section()
        # ---- dB math is a subtraction ----------------------------------------
        self.play(*[FadeOut(m) for m in bars + [baseline, bracket, med_lbl, mean_lbl, cap]])
        self.play(Transform(title, title_card("Ratios become subtractions")))

        a = VGroup(label("reference  (P1_FWD)", 26, MUTED), label("1216 counts", 28, CORE1, font=MONO)) \
            .arrange(RIGHT, buff=0.5)
        b = VGroup(label("reflected  (P1_BWD)", 26, MUTED), label("1520 counts", 28, CORE1, font=MONO)) \
            .arrange(RIGHT, buff=0.5)
        rows = VGroup(a, b).arrange(DOWN, aligned_edge=LEFT, buff=0.35).move_to(at(0, 1.3))
        self.play(FadeIn(a))
        self.play(FadeIn(b))
        self.next_section()
        eq = VGroup(label("S11 = (1520 − 1216) ÷ (−30.4 counts/dB) =", 30, TEXT),
                    label("−10.0 dB", 30, MEAS, weight=BOLD)).arrange(RIGHT, buff=0.2).move_to(at(0, -0.4))
        self.play(Write(eq[0]), run_time=1.5)
        self.play(FadeIn(eq[1], scale=1.3))
        note = label("a power ratio is just the difference of two readings — no logarithm needed", 22, MUTED)
        note.move_to(at(0, -1.6))
        self.play(FadeIn(note))
        self.wait(3)


# ---------------------------------------------------------------------------
class ImpedancePads(Scene):
    """50 Ω SMA or 75 Ω BNC per port, via a minimum-loss pad."""

    def construct(self):
        title = title_card("50 Ω and 75 Ω on every port")
        self.play(FadeIn(title, shift=DOWN * 0.2))

        # ---- Part A: switch -------------------------------------------------
        yc = -0.2
        inp = label("from coupler bank", 22, MUTED).move_to(at(-5.4, yc + 0.4))
        w_in = wire([(-6.6, yc), (-3.6, yc)], RF)
        sw = block("Z switch", "SPDT", LINE, 1.9, 1.0, sub_size=20).move_to(at(-2.65, yc))
        up = wire([(-1.7, yc + 0.25), (-1.0, yc + 0.25), (-1.0, 1.1), (3.3, 1.1)], LINE)
        dn = wire([(-1.7, yc - 0.25), (-1.0, yc - 0.25), (-1.0, -1.5), (0.15, -1.5)], LINE)
        pad = block("50 → 75 Ω pad", "−5.7 dB", POWER, 2.3, 0.9, sub_size=20).move_to(at(1.3, -1.5))
        dn2 = wire([(2.45, -1.5), (3.3, -1.5)], LINE)
        sma = block("SMA", "50 Ω", RF, 1.9, 0.9, sub_size=20).move_to(at(4.3, 1.1))
        bnc = block("BNC", "75 Ω (SDI)", HOST, 1.9, 0.9, sub_size=20).move_to(at(4.3, -1.5))

        self.play(FadeIn(inp), Create(w_in), FadeIn(sw))
        self.play(Create(up), FadeIn(sma), Create(dn), FadeIn(pad), Create(dn2), FadeIn(bnc))
        cap = caption("50 Ω mode: straight to the SMA jack")
        self.play(FadeIn(cap))
        travel(self, [(-6.6, yc), (-1.7, yc + 0.25), (-1.0, yc + 0.25), (-1.0, 1.1), (3.35, 1.1)], RF, run_time=1.8)
        self.next_section()
        self.play(Transform(cap, caption("75 Ω mode: through the pad to the BNC jack")))
        travel(self, [(-6.6, yc), (-1.7, yc - 0.25), (-1.0, yc - 0.25), (-1.0, -1.5), (3.35, -1.5)], RF, run_time=1.8)
        self.wait(0.8)

        self.next_section()
        # ---- Part B: the pad --------------------------------------------------
        part_a = VGroup(inp, w_in, sw, up, dn, pad, dn2, sma, bnc)
        self.play(FadeOut(part_a), FadeOut(cap), Transform(title, title_card("Minimum-loss pad: 50 Ω ↔ 75 Ω")))

        y = 0.9
        A, B = at(-1.6, y), at(2.2, y)
        left_t = Dot(at(-4.6, y), radius=0.09, color=TEXT)
        right_t = Dot(at(4.8, y), radius=0.09, color=TEXT)
        l50 = label("50 Ω side", 24, RF).next_to(left_t, UP, buff=0.25)
        l75 = label("75 Ω side", 24, HOST).next_to(right_t, UP, buff=0.25)
        w_l = Line(left_t.get_center(), A, color=TEXT, stroke_width=3)
        r_ser = resistor(A, B, POWER, wire=TEXT)
        w_r = Line(B, right_t.get_center(), color=TEXT, stroke_width=3)
        r_sh = resistor(A, at(-1.6, -0.8), POWER, wire=TEXT)
        g = ground(at(-1.6, -0.8), TEXT)
        node = Dot(A, radius=0.08, color=TEXT)
        ls = label("43.2 Ω", 24, POWER).move_to(at(0.3, y + 0.45))
        lp = label("86.6 Ω", 24, POWER).move_to(at(-0.85, 0.05))
        pad_g = VGroup(left_t, right_t, l50, l75, w_l, r_ser, w_r, r_sh, g, node, ls, lp)
        self.play(Create(VGroup(w_l, r_ser, w_r, r_sh, g)), FadeIn(VGroup(left_t, right_t, node)))
        self.play(FadeIn(l50), FadeIn(l75), FadeIn(ls), FadeIn(lp))
        self.wait(0.8)

        self.next_section()
        # Direction 1: from the 50 Ω side, with 75 Ω load
        load = VGroup(Line(at(4.8, y), at(5.6, y), color=TEXT, stroke_width=3),
                      resistor(at(5.6, y), at(5.6, -0.8), HOST, wire=TEXT), ground(at(5.6, -0.8), TEXT),
                      label("75 Ω", 22, HOST).move_to(at(6.25, 0.05)))
        eye1 = Arrow(at(-6.2, y), at(-4.75, y), buff=0, color=RF, stroke_width=4, tip_length=0.2)
        self.play(FadeIn(load), GrowArrow(eye1))
        eq1 = VGroup(label("seen from the 50 Ω side:", 24, MUTED),
                     label("86.6 ∥ (43.2 + 75) =", 26, TEXT),
                     label("50.0 Ω", 26, RF, weight=BOLD)).arrange(RIGHT, buff=0.2).move_to(at(0, -2.0))
        self.play(FadeIn(eq1[0]), Write(eq1[1]))
        self.play(FadeIn(eq1[2], scale=1.3))
        self.wait(1.5)

        self.next_section()
        # Direction 2: from the 75 Ω side, with 50 Ω source
        src = VGroup(Line(at(-4.6, y), at(-5.4, y), color=TEXT, stroke_width=3),
                     resistor(at(-5.4, y), at(-5.4, -0.8), RF, wire=TEXT), ground(at(-5.4, -0.8), TEXT),
                     label("50 Ω", 22, RF).move_to(at(-6.05, 0.05)))
        eye2 = Arrow(at(6.6, y), at(4.95, y), buff=0, color=HOST, stroke_width=4, tip_length=0.2)
        self.play(FadeOut(load), FadeOut(eye1), FadeIn(src), GrowArrow(eye2))
        eq2 = VGroup(label("seen from the 75 Ω side:", 24, MUTED),
                     label("43.2 + (86.6 ∥ 50) =", 26, TEXT),
                     label("74.9 Ω", 26, HOST, weight=BOLD)).arrange(RIGHT, buff=0.2).move_to(at(0, -2.7))
        self.play(FadeIn(eq2[0]), Write(eq2[1]))
        self.play(FadeIn(eq2[2], scale=1.3))
        self.wait(1.0)
        self.next_section()
        loss = label("matched both ways — price: 5.7 dB insertion loss (removed by calibration)", 22, POWER)
        loss.move_to(at(0, -3.45))
        self.play(FadeIn(loss))
        self.wait(3)


# ---------------------------------------------------------------------------
class PowerTree(Scene):
    """USB 5 V -> boost 6 V -> LDOs for clean RF rails; buck for digital."""

    def construct(self):
        title = label("Power: clean rails for the RF part", 40, TEXT, weight=BOLD).to_edge(UP, buff=0.3)
        self.play(FadeIn(title, shift=DOWN * 0.2))

        H = 0.9
        usb = block("USB-C 5 V", "≤ 500 mA", POWER, 2.2, H, sub_size=20).move_to(at(-5.65, 0))
        boost = block("Boost 6 V", "MIC2253 + LC", POWER, 2.4, H, sub_size=20).move_to(at(-2.55, 0))
        y5 = (1.825, -0.325, -1.9)
        ldo5 = [block("LDO 5 V", sub, POWER, 2.4, H, sub_size=20).move_to(at(0.65, yy))
                for sub, yy in zip(("LDL212 · 200 mA", "LDL212 · 60 mA", "LDL212 · 70 mA"), y5)]
        y3 = (2.35, 1.3, 0.2, -0.85, -1.9)
        names = [("PLL RF 3.3 V", "AP2114 · 135 mA", RF), ("PLL CP 3.3 V", "NCP702 · 55 mA", RF),
                 ("Atten/Amp 3.3 V", "NCP702 · 55 mA", RF), ("RF switch 3.3 V", "NCP702 · 1 mA", RF),
                 ("Detector 5 V", "AD8318", MEAS)]
        ldo3 = [block(t, sub, c, 2.9, H, sub_size=20).move_to(at(4.35, yy)) for (t, sub, c), yy in zip(names, y3)]
        buck = block("Buck 3.3 V", "TPS62802 · 50 mA", CORE0, 2.4, H, sub_size=20).move_to(at(-2.55, -3.05))
        dig = block("Digital 3.3 V", "RP2350B · flash · EEPROM", CORE0, 3.4, H, sub_size=20).move_to(at(1.05, -3.05))

        w_ub = wire([(-4.55, 0), (-3.75, 0)], POWER)
        bus5 = [wire([(-1.35, 0), (-0.9, 0), (-0.9, yy), (-0.55, yy)], POWER) for yy in y5]
        bus3 = [
            wire([(1.85, y5[0]), (2.35, y5[0]), (2.35, y3[0]), (2.9, y3[0])], RF),
            wire([(2.35, y5[0]), (2.35, y3[1]), (2.9, y3[1])], RF),
            wire([(1.85, y5[1]), (2.35, y5[1]), (2.35, y3[2]), (2.9, y3[2])], RF),
            wire([(2.35, y5[1]), (2.35, y3[3]), (2.9, y3[3])], RF),
            wire([(1.85, y5[2]), (2.9, y5[2])], MEAS),
        ]
        w_buck = wire([(-5.65, -H / 2), (-5.65, -3.05), (-3.75, -3.05)], CORE0)
        w_dig = wire([(-1.35, -3.05), (-0.65, -3.05)], CORE0)

        def step(text):
            return label(text, 26, MUTED).move_to(at(-4.1, 1.9))

        def flash(*wires):
            return [ShowPassingFlash(w.copy().set_color(WHITE).set_stroke(width=8), time_width=0.6) for w in wires]

        self.play(FadeIn(usb))
        cap = step("1. boost 5 V → 6 V")
        self.play(FadeIn(cap), Create(w_ub), FadeIn(boost))
        self.play(*flash(w_ub))
        self.wait(0.5)
        self.next_section()
        self.play(Transform(cap, step("2. 5 V LDOs per area")))
        self.play(*[Create(b) for b in bus5], *[FadeIn(l) for l in ldo5])
        self.play(*flash(*bus5))
        self.wait(0.5)
        self.next_section()
        self.play(Transform(cap, step("3. one LDO per RF circuit")))
        self.play(*[Create(b) for b in bus3], *[FadeIn(l) for l in ldo3], run_time=1.5)
        self.play(*flash(*bus3))
        self.wait(0.8)
        self.next_section()
        self.play(Transform(cap, step("4. digital: its own buck")))
        self.play(Create(w_buck), FadeIn(buck), Create(w_dig), FadeIn(dig))
        self.play(*flash(w_buck, w_dig))
        self.wait(0.5)

        self.next_section()
        msg = VGroup(label("boost + LDOs", 26, RF, weight=BOLD), label("→ quiet RF rails", 26, RF),
                     label("buck", 26, CORE0, weight=BOLD), label("→ efficient, digital only", 26, CORE0))             .arrange(DOWN, aligned_edge=LEFT, buff=0.12).move_to(at(-4.4, 2.0))
        self.play(FadeOut(cap), FadeIn(msg))
        total = label("≈ 500 mA total from USB", 24, POWER).next_to(msg, DOWN, buff=0.3).align_to(msg, LEFT)
        self.play(FadeIn(total), flash_fill(usb, POWER))
        self.wait(3)
