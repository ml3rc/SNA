"""RF basics for the SNA presentation: what a transmission line is, how reflections work, what S-parameters mean.

Made for a class that knows impedance over frequency (R, L, C) but no RF: no Smith chart, no complex math beyond
jωL and 1/(jωC). Render (from this folder):
    .venv/Scripts/python.exe -m manim -qh --media_dir media basics_scenes.py Reflections
Scenes: TransmissionLine, Reflections, SParameters
"""
from fw_common import *
from hw_common import capacitor, chip, inductor, travel_anim
from i18n import tr, tr_exact

# Trace colours of the client (ui/ChartColors.kt), so the slides match the app
S11_C = "#F2E450"     # yellow
S21_C = "#4FD6EA"     # cyan
S12_C = "#E466D8"     # magenta
S22_C = "#5ED86A"     # green
CABLE = "#3A414B"


def at(x, y):
    return np.array([x, y, 0])


def text(s, size=24, color=TEXT, weight=NORMAL):
    """Label whose text is already translated (live readouts: tr() the words, numbers appended)."""
    return Text(s, font=FONT, font_size=size, color=color, weight=weight)


def formula(s, size=26, color=TEXT, weight=NORMAL):
    """Formula label: translated only on an exact table hit (V → U in German), since it has no words to look up."""
    return Text(tr_exact(s), font=FONT, font_size=size, color=color, weight=weight)


def num(s):
    """Number string in the slide language (3.3 -> 3,3 in German)."""
    return tr(s)


def coax(x0, x1, y=0.0, name=None):
    """Cable seen from the side: shield as a dark tube, inner conductor in the middle."""
    tube = RoundedRectangle(corner_radius=0.12, width=x1 - x0, height=0.34, stroke_color=LINE, stroke_width=2,
                            fill_color=CABLE, fill_opacity=1).move_to(at((x0 + x1) / 2, y))
    core = Line(at(x0, y), at(x1, y), color=MUTED, stroke_width=2)
    g = VGroup(tube, core)
    if name:
        g.add(label(name, 20, MUTED).next_to(tube, DOWN, buff=0.15))
    return g


def bump(center, amp, color, x0, x1, y=0.0, w=0.38, h=1.0):
    """A voltage pulse (Gaussian) drawn on the line, clipped to [x0, x1]."""
    lo, hi = max(x0, center - 3 * w), min(x1, center + 3 * w)
    if hi - lo < 0.03 or abs(amp) < 1e-3:
        return VGroup()
    xs = np.linspace(lo, hi, 80)
    pts = [at(x, y + h * amp * np.exp(-((x - center) / w) ** 2)) for x in xs]
    fill = Polygon(*pts, at(hi, y), at(lo, y), stroke_width=0, fill_color=color, fill_opacity=0.28)
    curve = VMobject(stroke_color=color, stroke_width=5).set_points_smoothly(pts)
    return VGroup(fill, curve).set_z_index(5)


def gamma_of(zl, z0=50.0):
    if zl == float("inf"):
        return 1.0
    return (zl - z0) / (zl + z0)


def db(x):
    return 20 * np.log10(max(abs(x), 1e-6))


# ---------------------------------------------------------------------------
# 1. A cable is not just a wire
# ---------------------------------------------------------------------------
class TransmissionLine(Scene):
    def construct(self):
        cap = Captions(self)
        title = title_card("At high frequency a cable is more than a wire")
        self.play(FadeIn(title, shift=DOWN * 0.2))
        self.play(title.animate.scale(0.75).to_edge(UP, buff=0.25))

        x0, x1, y = -5.0, 5.0, 0.9
        src = block("Source", None, RF, 1.5, 0.9).move_to(at(x0 - 1.0, y))
        load = block("Load", None, LINE, 1.5, 0.9).move_to(at(x1 + 1.0, y))
        cable = coax(x0, x1, y)
        ry = y - 1.05
        ruler = VGroup(Line(at(x0, ry), at(x1, ry), color=MUTED, stroke_width=2),
                       Line(at(x0, ry + 0.1), at(x0, ry - 0.1), color=MUTED, stroke_width=2),
                       Line(at(x1, ry + 0.1), at(x1, ry - 0.1), color=MUTED, stroke_width=2),
                       label("1 m", 20, MUTED).move_to(at(0, ry - 0.25)))
        self.play(FadeIn(src), FadeIn(load), FadeIn(cable), FadeIn(ruler))

        # voltage along the cable: v(x, t) = A sin(2π x / λ − φ); λ in scene units (10 units = 1 m)
        loglam = ValueTracker(8.0)
        phase = ValueTracker(0.0)
        phase.add_updater(lambda m, dt: m.increment_value(dt * 2 * PI * 0.6))
        self.add(phase)

        def wave():
            lam = 10 ** loglam.get_value()
            xs = np.linspace(x0, x1, 400)
            pts = [at(x, y + 0.7 * np.sin(2 * PI * (x - x0) / lam - phase.get_value())) for x in xs]
            return VMobject(stroke_color=RF, stroke_width=4).set_points_smoothly(pts).set_z_index(5)

        v = always_redraw(wave)
        freq = VGroup(label("50 Hz", 30, RF, weight=BOLD), label("wavelength in the cable: 4000 km", 22, MUTED)
                      ).arrange(DOWN, buff=0.1).move_to(at(0, 2.35))
        self.play(FadeIn(v), FadeIn(freq))
        cap.show("50 Hz: the whole cable has the same voltage at any moment, it is just a wire")
        self.wait(3)

        freq2 = VGroup(label("100 MHz", 30, RF, weight=BOLD), label("wavelength in the cable: 2 m", 22, MUTED)
                       ).arrange(DOWN, buff=0.1).move_to(freq)
        cap.show("100 MHz: the voltage already differs along the cable")
        self.play(loglam.animate.set_value(np.log10(20)), FadeOut(freq), FadeIn(freq2), run_time=2.5)
        freq = freq2
        self.wait(2.5)

        freq3 = VGroup(label("1 GHz", 30, RF, weight=BOLD), label("wavelength in the cable: 20 cm", 22, MUTED)
                       ).arrange(DOWN, buff=0.1).move_to(freq)
        cap.show("1 GHz: five waves fit on 1 m, the signal travels along the cable as a wave")
        self.play(loglam.animate.set_value(np.log10(2)), FadeOut(freq), FadeIn(freq3), run_time=2.5)
        freq = freq3
        speed = chip("≈ 200 000 km/s  ·  1 m in 5 ns", RF, 22).move_to(at(0, -0.9))
        self.play(FadeIn(speed))
        self.wait(2.5)

        cap.show("Rule of thumb: longer than 1/10 of the wavelength → treat it as a transmission line")
        rule = chip("in the cable: 1 GHz → λ/10 = 2 cm  ·  6 GHz → λ/10 ≈ 3 mm", POWER, 22).move_to(at(0, -1.6))
        self.play(FadeIn(rule))
        self.wait(3)

        # --- what the wave sees: a ladder of L and C ----------------------------------------
        cap.show("What does the wave see? Every centimetre of cable is a small L and a small C")
        self.play(FadeOut(speed), FadeOut(rule), FadeOut(ruler))
        seg = Rectangle(width=1.0, height=0.5, stroke_color=POWER, stroke_width=3).move_to(at(1.5, y))
        self.play(Create(seg))

        yt, yb, lx0, n, dx = -0.45, -2.0, -5.2, 5, 2.0
        top = VGroup()
        caps_ = VGroup()
        for i in range(n):
            xa = lx0 + i * dx
            top.add(Line(at(xa, yt), at(xa + 0.3, yt), color=TEXT, stroke_width=3))
            top.add(inductor(at(xa + 0.3, yt), at(xa + 1.3, yt)))
            top.add(Line(at(xa + 1.3, yt), at(xa + dx, yt), color=TEXT, stroke_width=3))
            caps_.add(capacitor(at(xa + 1.65, yt), at(xa + 1.65, yb), MEAS))
            caps_.add(Dot(at(xa + 1.65, yt), radius=0.05, color=TEXT), Dot(at(xa + 1.65, yb), radius=0.05, color=TEXT))
        bottom = Line(at(lx0, yb), at(lx0 + n * dx, yb), color=MUTED, stroke_width=3)
        dots_l = label("…", 30, MUTED).move_to(at(lx0 - 0.35, (yt + yb) / 2))
        dots_r = label("…", 30, MUTED).move_to(at(lx0 + n * dx + 0.35, (yt + yb) / 2))
        lab_l = label("L′  inner conductor", 20, TEXT).move_to(at(lx0 + 0.8, yt + 0.42))
        lab_c = label("C′  inner conductor ↔ shield", 20, MEAS).move_to(at(lx0 + 3.6, yb - 0.38))
        zoom = VGroup(Line(seg.get_corner(DL), at(lx0, yt + 0.25), color=POWER, stroke_width=1.5),
                      Line(seg.get_corner(DR), at(lx0 + n * dx, yt + 0.25), color=POWER, stroke_width=1.5))
        self.play(FadeOut(v), FadeOut(freq), Create(zoom))
        self.play(Create(top), Create(bottom), FadeIn(dots_l), FadeIn(dots_r), run_time=1.5)
        self.play(Create(caps_), FadeIn(lab_l), FadeIn(lab_c), run_time=1.5)
        self.wait(2)

        ladder = VGroup(top, bottom, caps_, dots_l, dots_r, lab_l, lab_c)
        cap.show("You know: L and C change their impedance with frequency")
        zl = label("Z_L = jωL′  (rises with f)", 24, TEXT).move_to(at(-3.2, 2.35))
        zc = label("Z_C = 1 / (jωC′)  (falls with f)", 24, MEAS).move_to(at(3.0, 2.35))
        self.play(FadeOut(zoom), FadeOut(seg), FadeIn(zl), FadeIn(zc))
        self.wait(2.5)

        cap.show("Along the ladder only their ratio counts: ω cancels out")
        f1 = label("Z₀ = √( jωL′ · 1/(jωC′) ) = √( L′ / C′ )", 34, TEXT, weight=BOLD).move_to(at(0, 1.55))
        self.play(FadeOut(cable), FadeOut(src), FadeOut(load), FadeIn(f1))
        self.wait(2.5)

        cap.show("Typical coax: L′ ≈ 250 nH/m, C′ ≈ 100 pF/m → Z₀ = √2500 = 50 Ω")
        f2 = label("√( 250 nH / 100 pF ) = 50 Ω", 30, RF, weight=BOLD).next_to(f1, DOWN, buff=0.3)
        self.play(FadeIn(f2))
        self.wait(3)

        cap.show("Z₀ is the characteristic impedance: no loss, the same for any length and frequency")
        self.play(FadeOut(ladder), FadeOut(zl), FadeOut(zc), VGroup(f1, f2).animate.move_to(at(0, 1.9)))
        ex = VGroup(chip("SMA coax  ·  50 Ω", RF, 24), chip("SDI coax, BNC  ·  75 Ω", HOST, 24),
                    chip("PCB trace (coplanar waveguide)  ·  50 Ω", MEAS, 24)).arrange(DOWN, buff=0.3).move_to(at(0, -0.6))
        self.play(LaggedStart(*[FadeIn(e, shift=UP * 0.15) for e in ex], lag_ratio=0.3))
        self.wait(3)

        cap.show("At the end of the line the wave meets the load. If the load is not Z₀, part of it comes back.",
                 POWER)
        self.wait(4)


# ---------------------------------------------------------------------------
# 2. Reflections
# ---------------------------------------------------------------------------
class Reflections(Scene):
    X0, X1, Y = -4.6, 3.6, 0.6

    def load_symbol(self, kind, text_):
        """Load hanging from the line end down to ground: 'r' resistor, 'open', 'short'."""
        x, y = self.X1, self.Y
        top, bot = at(x + 0.6, y), at(x + 0.6, y - 1.9)
        feed = Line(at(x, y), top, color=TEXT, stroke_width=3)
        if kind == "r":
            el = resistor(top, bot, TEXT)
        elif kind == "short":
            el = Line(top, bot, color=TEXT, stroke_width=3)
        else:
            el = VGroup(Line(top, top + DOWN * 0.55, color=TEXT, stroke_width=3),
                        Line(bot + UP * 0.55, bot, color=TEXT, stroke_width=3))
        g = VGroup(feed, el, ground(bot))
        name = label(text_, 26, TEXT, weight=BOLD).move_to(at(x + 1.75, y - 0.95))
        return VGroup(g, name)

    def send(self, gamma, load, t_line=2.2, absorb=False, show_sum=False):
        """Pulse runs to the load; a pulse Γ times as high runs back. show_sum: also draw their sum (the voltage
        actually on the cable), which doubles at an open end and stays 0 at a short."""
        x0, x1, y = self.X0, self.X1, self.Y
        w, h = 0.38, 0.75
        s = ValueTracker(x0 - 3 * w)
        inc = always_redraw(lambda: bump(s.get_value(), 1.0, RF, x0, x1, y, h=h))
        ref = always_redraw(lambda: bump(2 * x1 - s.get_value(), gamma, POWER, x0, x1, y, h=h)
                            if s.get_value() > x1 - 3 * w else VGroup())
        self.add(inc, ref)
        if show_sum:
            def total():
                c = s.get_value()
                xs = np.linspace(max(x0, x1 - 3.2), x1, 120)
                v = np.exp(-((xs - c) / w) ** 2) * (xs <= x1) + gamma * np.exp(-((xs - (2 * x1 - c)) / w) ** 2)
                keep = np.abs(v) > 0.01   # only the pulse itself, not a white line along the whole cable
                if keep.sum() < 3:
                    return VGroup()
                return VMobject(stroke_color=TEXT, stroke_width=4).set_points_smoothly(
                    [at(x, y + h * vv) for x, vv in zip(xs[keep], v[keep])]).set_z_index(7)
            tot = always_redraw(total)
            self.add(tot)
        L = x1 - x0
        self.play(s.animate.set_value(x1), run_time=t_line * (L + 3 * w) / L, rate_func=linear)
        anims = [s.animate(run_time=t_line * (L + 6 * w) / L, rate_func=linear).set_value(2 * x1 - x0 + 3 * w)]
        if absorb:
            glow = load[0][1].copy().set_stroke(RF, width=8).set_z_index(15)
            anims.append(Succession(FadeIn(glow, run_time=0.3), FadeOut(glow, run_time=0.8)))
        self.play(*anims)
        self.remove(inc, ref)
        if show_sum:
            self.remove(tot)

    def construct(self):
        cap = Captions(self)
        title = title_card("Reflections")
        self.play(FadeIn(title, shift=DOWN * 0.2))
        self.play(title.animate.scale(0.75).to_edge(UP, buff=0.25))

        x0, x1, y = self.X0, self.X1, self.Y
        src = block("Source", "Z₀ = 50 Ω", RF, 1.7, 1.0).move_to(at(x0 - 1.15, y))
        cable = coax(x0, x1, y, "cable  Z₀ = 50 Ω")
        legend = VGroup(VGroup(Line(LEFT * 0.3, RIGHT * 0.3, color=RF, stroke_width=5), label("outgoing wave", 20, MUTED)
                               ).arrange(RIGHT, buff=0.15),
                        VGroup(Line(LEFT * 0.3, RIGHT * 0.3, color=POWER, stroke_width=5), label("reflected wave", 20, MUTED)
                               ).arrange(RIGHT, buff=0.15),
                        VGroup(Line(LEFT * 0.3, RIGHT * 0.3, color=TEXT, stroke_width=3), label("sum = voltage on the cable", 20, MUTED)
                               ).arrange(RIGHT, buff=0.15)).arrange(RIGHT, buff=0.6).move_to(at(-0.5, 2.55))
        load = self.load_symbol("r", "50 Ω")
        self.play(FadeIn(src), FadeIn(cable), FadeIn(load), FadeIn(legend))

        # --- matched -------------------------------------------------------------------------
        cap.show("Load = 50 Ω = Z₀: the load takes the whole wave, nothing comes back")
        self.send(0.0, load, absorb=True)
        res = chip("Γ = 0  ·  no reflection", MEAS, 24).move_to(at(-0.5, -2.2))
        self.play(FadeIn(res))
        self.wait(1.5)

        # --- open ----------------------------------------------------------------------------
        cap.show("Open end (∞ Ω): no current can flow, the whole wave turns around")
        new = self.load_symbol("open", "open")
        self.play(FadeOut(load), FadeIn(new), FadeOut(res))
        load = new
        self.send(1.0, load, t_line=2.8, show_sum=True)
        why = VGroup(label("Open end: the current must be 0", 26, TEXT, weight=BOLD),
                     formula("I = (V→ − V←) / Z₀ = 0   →   V← = +V→", 26, POWER),
                     label("The returning wave carries its current the other way: the currents cancel, the voltages add (2 · V→)",
                           20, MUTED)).arrange(DOWN, buff=0.18).move_to(at(0, -2.1))
        self.play(FadeIn(why[0]))
        self.play(FadeIn(why[1]), FadeIn(why[2]))
        res = chip("Γ = +1  ·  everything comes back", POWER, 24).move_to(at(-0.5, -3.25))
        self.play(FadeIn(res))
        self.wait(1.5)

        # --- short ---------------------------------------------------------------------------
        cap.show("Short (0 Ω): the voltage must stay 0, the wave comes back upside down")
        new = self.load_symbol("short", "short")
        self.play(FadeOut(load), FadeIn(new), FadeOut(res), FadeOut(why))
        load = new
        self.send(-1.0, load, t_line=2.8, show_sum=True)
        why = VGroup(label("Short: the voltage must be 0", 26, TEXT, weight=BOLD),
                     formula("V = V→ + V← = 0   →   V← = −V→", 26, POWER),
                     label("The reflection comes back inverted: the voltages cancel, the currents add (2 · I→)",
                           20, MUTED)).arrange(DOWN, buff=0.18).move_to(at(0, -2.1))
        self.play(FadeIn(why[0]))
        self.play(FadeIn(why[1]), FadeIn(why[2]))
        res = chip("Γ = −1  ·  everything comes back, inverted", POWER, 24).move_to(at(-0.5, -3.25))
        self.play(FadeIn(res))
        self.wait(1.5)

        # --- any load: Γ from the same two rules ------------------------------------------------
        cap.show("Any load: at the end, voltage / current must equal Z_L. That fixes size and sign of the reflection.")
        self.play(FadeOut(why), FadeOut(res))
        gen = VGroup(formula("V / I = (V→ + V←) / ((V→ − V←) / Z₀) = Z_L", 26, TEXT),
                     formula("Γ = V← / V→ = (Z_L − Z₀) / (Z_L + Z₀)", 32, POWER, weight=BOLD),
                     label("Z_L > Z₀  →  Γ > 0, same sign      Z_L < Z₀  →  Γ < 0, inverted", 22, MUTED)
                     ).arrange(DOWN, buff=0.28).move_to(at(-0.5, -2.3))
        self.play(FadeIn(gen[0]))
        self.play(FadeIn(gen[1]))
        self.play(FadeIn(gen[2]))
        self.wait(2.5)

        # --- 100 ohm ---------------------------------------------------------------------------
        cap.show("100 Ω: in between, part of the wave is absorbed, part comes back")
        new = self.load_symbol("r", "100 Ω")
        self.play(FadeOut(load), FadeIn(new), FadeOut(gen))
        load = new
        self.send(1 / 3, load)
        f = VGroup(label("Γ = (Z_L − Z₀) / (Z_L + Z₀)", 30, TEXT, weight=BOLD),
                   label("= (100 − 50) / (100 + 50) = 1/3", 26, POWER)).arrange(RIGHT, buff=0.35).move_to(at(-0.5, -2.2))
        self.play(FadeIn(f[0]))
        self.play(FadeIn(f[1]))
        self.wait(2.5)

        # --- Γ scale ---------------------------------------------------------------------------
        cap.show("Γ (reflection coefficient) only depends on how far Z_L is from Z₀")
        self.play(FadeOut(f), FadeOut(legend))
        nl_y = -1.75
        nl = NumberLine(x_range=[-1, 1, 0.5], length=8.0, color=LINE, stroke_width=3, include_numbers=False
                        ).move_to(at(-1.0, nl_y))
        nl_lbl = VGroup(*[label(t, 20, MUTED).next_to(nl.n2p(v), DOWN, buff=0.18)
                          for v, t in ((-1, "−1"), (-0.5, "−0.5"), (0, "0"), (0.5, "+0.5"), (1, "+1"))])
        marks = VGroup()
        for v, t, c in ((-1, "short", POWER), (0, "50 Ω", MEAS), (0.2, "75 Ω", TEXT), (1 / 3, "100 Ω", TEXT),
                        (1, "open", POWER)):
            tk = Line(nl.n2p(v) + UP * 0.12, nl.n2p(v) + UP * 0.32, color=c, stroke_width=3)
            tl = label(t, 18, c).next_to(tk, UP, buff=0.06)
            marks.add(VGroup(tk, tl))
        marks[2][1].shift(LEFT * 0.18)
        marks[3][1].shift(RIGHT * 0.22)
        self.play(Create(nl), FadeIn(nl_lbl))
        self.play(LaggedStart(*[FadeIn(m) for m in marks], lag_ratio=0.2))

        zt = ValueTracker(50.0)

        def zl():
            return zt.get_value()

        ptr = always_redraw(lambda: Triangle(color=RF, fill_color=RF, fill_opacity=1).scale(0.13).rotate(PI)
                            .move_to(nl.n2p(gamma_of(zl())) + UP * 0.62))
        read = always_redraw(lambda: text(f"Z_L = {num(f'{zl():.0f}')} Ω   →   Γ = {num(f'{gamma_of(zl()):+.2f}')}",
                                          24, RF, BOLD).move_to(at(-0.5, -0.65)))
        self.play(FadeIn(ptr), FadeIn(read))
        self.play(zt.animate.set_value(5), run_time=2.5)
        self.play(zt.animate.set_value(500), run_time=3.5, rate_func=smooth)
        self.play(zt.animate.set_value(100), run_time=1.5)
        self.wait(1.5)

        # --- power and dB ------------------------------------------------------------------------
        cap.show("Power goes with the square: with 100 Ω, (1/3)² = 11 % of the power comes back")
        self.play(FadeOut(ptr), FadeOut(read), FadeOut(nl), FadeOut(nl_lbl), FadeOut(marks), FadeOut(cable),
                  FadeOut(src), FadeOut(load))
        rows = [("Load", "Γ", "power back", "|S11|"),
                ("50 Ω", "0", "0 %", "−∞ dB"),
                ("75 Ω", "0.2", "4 %", "−14 dB"),
                ("100 Ω", "0.33", "11 %", "−9.5 dB"),
                ("open / short", "±1", "100 %", "0 dB")]
        tbl = VGroup()
        for i, r in enumerate(rows):
            row = VGroup(*[label(c, 24, MUTED if i == 0 else (S11_C if j == 3 else TEXT),
                                 weight=BOLD if i == 0 or j == 3 else NORMAL) for j, c in enumerate(r)])
            for j, cell in enumerate(row):
                cell.move_to(at(-4.1 + j * 2.75, 1.6 - i * 0.62))
            tbl.add(row)
        rule = Line(at(-5.5, 1.29), at(5.4, 1.29), color=LINE, stroke_width=2)
        self.play(FadeIn(tbl[0]), Create(rule), FadeIn(tbl[1:]))
        self.wait(2)
        cap.show("In dB: |S11| = 20 · log₁₀|Γ|. The more negative, the better the match (Return Loss).")
        self.play(highlight(VGroup(*[r[3] for r in tbl[1:]]), color=S11_C))
        self.wait(2.5)
        cap.show("That is why the SNA has a 50 → 75 Ω pad: an SDI cable directly on 50 Ω would reflect at −14 dB",
                 POWER)
        self.play(highlight(tbl[2], color=POWER))
        self.wait(3)

        # --- frequency ------------------------------------------------------------------------------
        cap.show("Real loads change with frequency, e.g. 50 Ω with 2 pF of connector capacitance in parallel")
        self.play(FadeOut(tbl), FadeOut(rule), FadeOut(title))

        C, R = 2e-12, 50.0

        def zload(f_ghz):
            w_ = 2 * PI * f_ghz * 1e9
            return R / (1 + 1j * w_ * R * C)

        def s11db(f_ghz):
            zz = zload(f_ghz)
            return max(db((zz - 50) / (zz + 50)), -40)

        # small circuit on the left
        cx, cy = -5.0, 0.4
        circ = VGroup(Line(at(cx - 0.9, cy + 1.2), at(cx + 0.9, cy + 1.2), color=TEXT, stroke_width=3),
                      resistor(at(cx - 0.5, cy + 1.2), at(cx - 0.5, cy - 0.6), TEXT),
                      capacitor(at(cx + 0.5, cy + 1.2), at(cx + 0.5, cy - 0.6), MEAS),
                      Line(at(cx - 0.5, cy - 0.6), at(cx + 0.5, cy - 0.6), color=TEXT, stroke_width=3),
                      ground(at(cx, cy - 0.6)),
                      label("50 Ω", 20, TEXT).move_to(at(cx - 1.15, cy + 0.3)),
                      label("2 pF", 20, MEAS).move_to(at(cx + 1.05, cy + 0.3)),
                      label("from the cable", 18, MUTED).move_to(at(cx - 0.1, cy + 1.55)))
        ax = Axes(x_range=[0, 6, 1], y_range=[-40, 0, 10], x_length=8.0, y_length=4.2,
                  axis_config={"color": LINE, "stroke_width": 2, "include_ticks": False}, tips=False
                  ).move_to(at(1.9, 0.35))
        ticks = VGroup(*[label(f"{v}", 18, MUTED).next_to(ax.c2p(v, -40), DOWN, buff=0.12) for v in range(0, 7)],
                       *[label(f"{v}", 18, MUTED).next_to(ax.c2p(0, v), LEFT, buff=0.12) for v in (0, -10, -20, -30, -40)])
        xl = label("frequency (GHz)", 20, MUTED).next_to(ax, DOWN, buff=0.5)
        yl = label("|S11| (dB)", 20, S11_C).next_to(ax, UP, buff=0.12).align_to(ax, LEFT)
        self.play(FadeIn(circ), Create(ax), FadeIn(ticks), FadeIn(xl), FadeIn(yl))

        ft = ValueTracker(0.05)
        trace = always_redraw(lambda: ax.plot(s11db, x_range=[0.05, max(ft.get_value(), 0.06), 0.02], color=S11_C,
                                              stroke_width=4))
        dot = always_redraw(lambda: Dot(ax.c2p(ft.get_value(), s11db(ft.get_value())), radius=0.08, color=S11_C))

        def readout():
            f_ = ft.get_value()
            xc = 1 / (2 * PI * f_ * 1e9 * C)
            lines = VGroup(text(f"f = {num(f'{f_:.2f}')} GHz", 22, TEXT, BOLD),
                           text(f"Z_C = {num(f'{xc:.0f}')} Ω", 22, MEAS),
                           text(f"|Z_L| = {num(f'{abs(zload(f_)):.0f}')} Ω", 22, TEXT),
                           text(f"|S11| = {num(f'{s11db(f_):.1f}')} dB", 22, S11_C, BOLD)).arrange(DOWN, buff=0.12,
                                                                                                aligned_edge=LEFT)
            return lines.move_to(at(-5.0, -2.0))

        ro = always_redraw(readout)
        self.add(trace, dot)
        self.play(FadeIn(ro))
        cap.show("Low frequency: the capacitor is almost open, the load is a good 50 Ω")
        self.play(ft.animate.set_value(0.3), run_time=1.5, rate_func=linear)
        self.wait(1)
        cap.show("Higher frequency: Z_C falls, the load drifts away from 50 Ω, more is reflected")
        self.play(ft.animate.set_value(6.0), run_time=6, rate_func=linear)
        self.wait(1)
        cap.show("Impedance over frequency = reflection over frequency. That is why the SNA sweeps.", S11_C)
        self.wait(1.5)
        note = label("The SNA measures only the size |Γ|, not the phase (scalar).", 22, MUTED).move_to(at(1.9, -2.85))
        self.play(FadeIn(note))
        self.wait(4)


# ---------------------------------------------------------------------------
# 3. S-parameters
# ---------------------------------------------------------------------------
class SParameters(Scene):
    def construct(self):
        cap = Captions(self)
        title = title_card("S-parameters")
        self.play(FadeIn(title, shift=DOWN * 0.2))
        self.play(title.animate.scale(0.75).to_edge(UP, buff=0.25))

        y = 1.0
        dut = block("DUT", "two-port", LINE, 2.6, 2.0, title_size=30, sub_size=22).move_to(at(0, y))
        l1 = Line(at(-5.6, y), at(-1.3, y), color=CABLE, stroke_width=10)
        l2 = Line(at(1.3, y), at(5.6, y), color=CABLE, stroke_width=10)
        p1 = VGroup(Dot(at(-1.3, y), radius=0.1, color=TEXT), label("Port 1", 22, TEXT).move_to(at(-2.1, y + 1.25)))
        p2 = VGroup(Dot(at(1.3, y), radius=0.1, color=TEXT), label("Port 2", 22, TEXT).move_to(at(2.1, y + 1.25)))
        ex = label("cable · filter · attenuator · amplifier …", 22, MUTED).move_to(at(0, y - 1.45))
        self.play(FadeIn(dut), Create(l1), Create(l2), FadeIn(p1), FadeIn(p2))
        cap.show("A two-port: anything with an input and an output")
        self.play(FadeIn(ex))
        self.wait(2)

        def wave_arrow(xa, xb, yy, color, name):
            a = Arrow(at(xa, yy), at(xb, yy), buff=0, color=color, stroke_width=6, tip_length=0.25,
                      max_tip_length_to_length_ratio=0.3)
            t = label(name, 26, color, weight=BOLD).next_to(a, UP if yy > y else DOWN, buff=0.1)
            return VGroup(a, t)

        # --- drive port 1 -------------------------------------------------------------------------
        cap.show("Send a wave a1 into port 1")
        self.play(FadeOut(ex))
        src1 = block("Source", None, RF, 1.4, 0.8).move_to(at(-6.2, y))
        term2 = VGroup(resistor(at(6.0, y), at(6.0, y - 1.3), MUTED), ground(at(6.0, y - 1.3), MUTED),
                       label("50 Ω", 20, MUTED).move_to(at(6.55, y - 0.65)),
                       Line(at(5.6, y), at(6.0, y), color=MUTED, stroke_width=3))
        a1 = wave_arrow(-4.8, -2.0, y + 0.45, RF, "a1")
        self.play(FadeIn(src1), FadeIn(term2))
        g, an = travel_anim([(-5.5, y), (-1.3, y)], RF, run_time=1.3)
        self.add(g)
        self.play(an, GrowArrow(a1[0]), FadeIn(a1[1]))
        self.remove(g)

        cap.show("Part of it comes back out of port 1 (b1), part comes out of port 2 (b2)")
        b1 = wave_arrow(-2.0, -4.8, y - 0.45, S11_C, "b1")
        b2 = wave_arrow(2.0, 4.8, y + 0.45, S21_C, "b2")
        g1, an1 = travel_anim([(-1.3, y), (-5.5, y)], S11_C, run_time=1.3)
        g2, an2 = travel_anim([(1.3, y), (5.6, y)], S21_C, run_time=1.3)
        self.add(g1, g2)
        self.play(an1, an2, GrowArrow(b1[0]), FadeIn(b1[1]), GrowArrow(b2[0]), FadeIn(b2[1]))
        self.remove(g1, g2)
        self.wait(1)

        eq_y = -1.15
        s11 = VGroup(label("S11 = b1 / a1", 32, S11_C, weight=BOLD), label("reflection at port 1", 22, MUTED)
                     ).arrange(DOWN, buff=0.12).move_to(at(-3.2, eq_y))
        s21 = VGroup(label("S21 = b2 / a1", 32, S21_C, weight=BOLD), label("transmission 1 → 2", 22, MUTED)
                     ).arrange(DOWN, buff=0.12).move_to(at(3.2, eq_y))
        cap.show("S-parameters are ratios: what comes out divided by what went in")
        self.play(FadeIn(s11, shift=UP * 0.15))
        self.play(FadeIn(s21, shift=UP * 0.15))
        self.wait(2)

        cap.show("The name says where: S out, in.  S21 = out at port 2, in at port 1")
        big = VGroup(label("S", 64, S21_C, weight=BOLD), label("2", 64, POWER, weight=BOLD),
                     label("1", 64, RF, weight=BOLD)).arrange(RIGHT, buff=0.05).move_to(at(0, eq_y - 0.15))
        o_l = label("out", 22, POWER).next_to(big[1], DOWN, buff=0.1)
        i_l = label("in", 22, RF).next_to(big[2], DOWN, buff=0.1).shift(RIGHT * 0.1)
        self.play(FadeIn(big), FadeIn(o_l), FadeIn(i_l))
        self.wait(3)

        # --- drive port 2 -------------------------------------------------------------------------
        cap.show("Now the other way: source on port 2, port 1 terminated with 50 Ω")
        src2 = block("Source", None, RF, 1.4, 0.8).move_to(at(6.2, y))
        term1 = VGroup(resistor(at(-6.0, y), at(-6.0, y - 1.3), MUTED), ground(at(-6.0, y - 1.3), MUTED),
                       label("50 Ω", 20, MUTED).move_to(at(-6.55, y - 0.65)),
                       Line(at(-5.6, y), at(-6.0, y), color=MUTED, stroke_width=3))
        self.play(FadeOut(VGroup(a1, b1, b2, big, o_l, i_l)), FadeOut(src1), FadeOut(term2), FadeIn(src2), FadeIn(term1),
                  s11.animate.set_opacity(0.35), s21.animate.set_opacity(0.35))
        a2 = wave_arrow(4.8, 2.0, y + 0.45, RF, "a2")
        g, an = travel_anim([(5.5, y), (1.3, y)], RF, run_time=1.3)
        self.add(g)
        self.play(an, GrowArrow(a2[0]), FadeIn(a2[1]))
        self.remove(g)
        b2r = wave_arrow(2.0, 4.8, y - 0.45, S22_C, "b2")
        b1t = wave_arrow(-2.0, -4.8, y + 0.45, S12_C, "b1")
        g1, an1 = travel_anim([(1.3, y), (5.5, y)], S22_C, run_time=1.3)
        g2, an2 = travel_anim([(-1.3, y), (-5.6, y)], S12_C, run_time=1.3)
        self.add(g1, g2)
        self.play(an1, an2, GrowArrow(b2r[0]), FadeIn(b2r[1]), GrowArrow(b1t[0]), FadeIn(b1t[1]))
        self.remove(g1, g2)
        s22 = VGroup(label("S22 = b2 / a2", 32, S22_C, weight=BOLD), label("reflection at port 2", 22, MUTED)
                     ).arrange(DOWN, buff=0.12).move_to(at(3.2, eq_y - 1.25))
        s12 = VGroup(label("S12 = b1 / a2", 32, S12_C, weight=BOLD), label("transmission 2 → 1", 22, MUTED)
                     ).arrange(DOWN, buff=0.12).move_to(at(-3.2, eq_y - 1.25))
        self.play(FadeIn(s22, shift=UP * 0.15), FadeIn(s12, shift=UP * 0.15))
        self.wait(2.5)

        # --- the matrix ---------------------------------------------------------------------------
        cap.show("Four numbers describe the two-port: reflections on the diagonal, transmissions across")
        self.play(FadeOut(VGroup(a2, b2r, b1t, src2, term1, dut, l1, l2, p1, p2, s11, s21, s22, s12)))

        def cell(name, color, sub):
            box = RoundedRectangle(corner_radius=0.12, width=4.0, height=1.45, stroke_color=color, stroke_width=3,
                                   fill_color=PANEL, fill_opacity=1)
            t = VGroup(label(name, 36, color, weight=BOLD), label(sub, 20, MUTED)).arrange(DOWN, buff=0.1).move_to(box)
            return VGroup(box, t)

        m = VGroup(cell("S11", S11_C, "Return Loss port 1"), cell("S12", S12_C, "Insertion Loss 2 → 1"),
                   cell("S21", S21_C, "Insertion Loss / Gain 1 → 2"), cell("S22", S22_C, "Return Loss port 2")
                   ).arrange_in_grid(rows=2, cols=2, buff=0.3).move_to(at(0, 0.45))
        hdr_c = VGroup(label("in at port 1", 20, RF).next_to(m[0], UP, buff=0.15),
                       label("in at port 2", 20, RF).next_to(m[1], UP, buff=0.15))
        hdr_r = VGroup(label("out at port 1", 20, POWER).next_to(m[0], LEFT, buff=0.2),
                       label("out at port 2", 20, POWER).next_to(m[2], LEFT, buff=0.2))
        self.play(LaggedStart(*[FadeIn(c, scale=0.9) for c in m], lag_ratio=0.2), FadeIn(hdr_c), FadeIn(hdr_r))
        self.wait(3)

        # --- dB -------------------------------------------------------------------------------------
        cap.show("Ratios are given in dB: 10 · log₁₀ of the power ratio")
        self.play(FadeOut(m), FadeOut(hdr_c), FadeOut(hdr_r))
        scale = [("+10 dB", "10 ×", 1.0, POWER), ("0 dB", "100 %", 0.1, TEXT), ("−3 dB", "50 %", 0.05, TEXT),
                 ("−10 dB", "10 %", 0.01, TEXT), ("−20 dB", "1 %", 0.001, TEXT), ("−30 dB", "0.1 %", 0.0001, TEXT)]
        bars = VGroup()
        for i, (d, p, frac, c) in enumerate(scale):
            yy = 2.0 - i * 0.62
            bar = Rectangle(width=max(8.0 * frac, 0.02), height=0.36, stroke_width=0, fill_color=c,
                            fill_opacity=0.85).move_to(at(-2.2, yy), aligned_edge=LEFT)
            bars.add(VGroup(label(d, 26, c, weight=BOLD).move_to(at(-4.6, yy)),
                            label(p, 24, MUTED).move_to(at(-3.15, yy)), bar))
        bars.shift(DOWN * 0.15)
        self.play(LaggedStart(*[FadeIn(b, shift=RIGHT * 0.2) for b in bars], lag_ratio=0.2))
        self.wait(1)
        cap.show("dB makes tiny and huge ratios readable on one axis, and ratios become subtractions")
        self.wait(3)

        # --- examples -------------------------------------------------------------------------------
        cap.show("Typical values")
        self.play(FadeOut(bars))
        hdr = ["", "S21", "S12", "S11 / S22"]
        data = [("good cable", "−1 dB", "−1 dB", "< −20 dB"),
                ("10 dB attenuator", "−10 dB", "−10 dB", "< −20 dB"),
                ("amplifier", "+15 dB", "−30 dB", "−10 dB")]
        cols = [S21_C, S12_C, S11_C]
        tbl = VGroup()
        hrow = VGroup(*[label(h, 26, cols[j - 1] if j else MUTED, weight=BOLD) for j, h in enumerate(hdr)])
        tbl.add(hrow)
        for r in data:
            tbl.add(VGroup(*[label(c, 26, TEXT, weight=BOLD if j == 0 else NORMAL) for j, c in enumerate(r)]))
        xs = [-3.9, -0.6, 1.9, 4.5]
        for i, row in enumerate(tbl):
            for j, c in enumerate(row):
                c.move_to(at(xs[j], 1.6 - i * 0.75))
        self.play(FadeIn(tbl[0]), LaggedStart(*[FadeIn(r) for r in tbl[1:]], lag_ratio=0.3))
        self.wait(1.5)
        cap.show("Passive parts work both ways (S21 = S12), an amplifier only one way")
        self.play(highlight(VGroup(tbl[1][1], tbl[1][2], tbl[2][1], tbl[2][2]), color=S21_C))
        self.play(highlight(VGroup(tbl[3][1], tbl[3][2]), color=POWER))
        self.wait(2.5)

        # --- low-pass over frequency ------------------------------------------------------------------
        cap.show("Over frequency, e.g. a 1 GHz low-pass filter")
        self.play(FadeOut(tbl))

        def s21p(f):  # 5th order Butterworth, lossless
            return 1 / (1 + (f / 1.0) ** 10)

        def s21db(f):
            return max(10 * np.log10(s21p(f)), -50)

        def s11db(f):
            return max(10 * np.log10(max(1 - s21p(f), 1e-9)), -50)

        ax = Axes(x_range=[0, 3, 0.5], y_range=[-50, 5, 10], x_length=9.0, y_length=4.4,
                  axis_config={"color": LINE, "stroke_width": 2, "include_ticks": False}, tips=False
                  ).move_to(at(0.3, 0.2))
        ticks = VGroup(*[label(t, 18, MUTED).next_to(ax.c2p(v, -50), DOWN, buff=0.12)
                         for v, t in ((0, "0"), (1, "1"), (2, "2"), (3, "3 GHz"))],
                       *[label(f"{v}", 18, MUTED).next_to(ax.c2p(0, v), LEFT, buff=0.12) for v in (0, -10, -20, -30, -40, -50)])
        yl = label("dB", 20, MUTED).next_to(ax, UP, buff=0.1).align_to(ax, LEFT)
        zero = DashedLine(ax.c2p(0, 0), ax.c2p(3, 0), color=LINE, stroke_width=1.5)
        self.play(Create(ax), FadeIn(ticks), FadeIn(yl), Create(zero))
        c21 = ax.plot(s21db, x_range=[0.01, 3, 0.005], color=S21_C, stroke_width=4)
        c11 = ax.plot(s11db, x_range=[0.32, 3, 0.005], color=S11_C, stroke_width=4)
        l21 = label("S21", 24, S21_C, weight=BOLD).next_to(ax.c2p(0.55, s21db(0.55)), UP, buff=0.12)
        l11 = label("S11", 24, S11_C, weight=BOLD).next_to(ax.c2p(2.4, s11db(2.4)), DOWN, buff=0.15)
        self.play(Create(c21), FadeIn(l21), run_time=2)
        cap.show("Below 1 GHz the signal passes (S21 ≈ 0 dB), above it is blocked")
        self.wait(2)
        self.play(Create(c11), FadeIn(l11), run_time=2)
        cap.show("What does not pass comes back: S11 rises where S21 falls")
        self.wait(3)

        # --- how the SNA measures it --------------------------------------------------------------------
        cap.show("The SNA measures powers in dBm, so S11 in dB is just a subtraction")
        self.play(FadeOut(VGroup(ax, ticks, yl, zero, c21, c11, l21, l11)))
        meas = VGroup(label("S11 (dB) = P reflected (dBm) − P outgoing (dBm)", 30, S11_C, weight=BOLD),
                      label("S21 (dB) = P at port 2 (dBm) − P outgoing (dBm)", 30, S21_C, weight=BOLD)
                      ).arrange(DOWN, buff=0.35).move_to(at(0, 1.1))
        exm = label("e.g. 100 Ω load:  −9.5 dBm − 0 dBm = −9.5 dB", 26, TEXT).next_to(meas, DOWN, buff=0.6)
        self.play(FadeIn(meas[0]))
        self.play(FadeIn(meas[1]))
        self.play(FadeIn(exm))
        self.wait(2)
        cap.show("Directional coupler and log detector measure these powers, calibration removes the SNA's own losses",
                 MEAS)
        self.wait(4)
