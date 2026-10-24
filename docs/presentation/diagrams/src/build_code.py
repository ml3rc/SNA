"""One diagram per firmware source file: code-01..06 HAL, code-07..09 rf_control, code-10..16 the rest of src/.

Visual aids only: the explaining text goes in the document, so the diagrams carry names, flows and tables.
Same rule as the other build_*.py: this only made the first version; re-running it overwrites hand edits.
"""
from html import escape as esc

from dio import *


# --- helpers -------------------------------------------------------------------------------------

def panel(d, x, y, w, h, color=LINE, fill=PANEL, dashed=False, width=1.5):
    style = (f"rounded=1;arcSize=10;absoluteArcSize=1;whiteSpace=wrap;html=1;fillColor={fill};strokeColor={color};"
             f"strokeWidth={width};{'dashed=1;dashPattern=6 4;' if dashed else ''}")
    return d.shape(x, y, w, h, style)


def api(d, x, y, w, names, color, title="API", row_h=40, size=15):
    """Function names in a panel. Returns the bottom y."""
    h = 52 + row_h * len(names)
    panel(d, x, y, w, h, color)
    d.text(x + 18, y + 10, w - 36, 26, title, color, 15, True, "left")
    for i, n in enumerate(names):
        d.text(x + 18, y + 46 + i * row_h, w - 36, row_h, n, TEXT, size, False, "left", font=MONO)
    return y + h


def table(d, x, y, widths, rows, color=LINE, row_h=34, size=14, mono=(), align=None, head=True, hl=None):
    """Grid of cells. mono: column indices in Consolas. hl: {(row, col): color} for coloured cell text."""
    hl = hl or {}
    yy = y
    for r, row in enumerate(rows):
        xx = x
        is_head = head and r == 0
        for c, cell in enumerate(row):
            d.rect(xx, yy, widths[c], row_h, PANEL_2 if is_head else PANEL, BG, esc(str(cell)),
                   color if is_head else hl.get((r, c), TEXT), size, is_head, align[c] if align else "left",
                   font=MONO if (c in mono and not is_head) else FONT)
            xx += widths[c]
        yy += row_h
    return yy


def mono_box(d, x, y, w, h, title, sub=None, color=LINE, size=15, sub_size=12):
    """Box whose title is code."""
    v = f'<font face="{MONO}"><b>{esc(title)}</b></font>'
    if sub:
        v += f'<br><font color="{MUTED}" style="font-size:{sub_size}px">{esc(sub)}</font>'
    style = (f"rounded=1;arcSize=10;whiteSpace=wrap;html=1;fillColor={PANEL};strokeColor={color};strokeWidth=2;"
             f"fontColor={TEXT};fontFamily={FONT};fontSize={size};align=center;verticalAlign=middle;spacing=6;")
    return d.shape(x, y, w, h, style, v)


def diamond(d, x, y, w, h, text, color, size=15):
    style = (f"rhombus;whiteSpace=wrap;html=1;fillColor={PANEL};strokeColor={color};strokeWidth=2;fontColor={TEXT};"
             f"fontFamily={FONT};fontSize={size};fontStyle=1;")
    return d.shape(x, y, w, h, style, esc(text).replace("\n", "<br>"))


def wave(d, x, y, h, segs, unit, color):
    """Digital waveform: segs = [(level, units), ...], high = y, low = y + h."""
    pts = []
    cx = x
    for lvl, n in segs:
        yv = y if lvl else y + h
        pts.append((cx, yv))
        cx += n * unit
        pts.append((cx, yv))
    d.line(pts[0][0], pts[0][1], pts[-1][0], pts[-1][1], color, 2, end="none", pts=pts[1:-1])


def bus(d, x, y, h, cells, unit, color):
    """Bus lane: cells = [(label, units), ...]."""
    cx = x
    for label, n in cells:
        d.rect(cx, y, n * unit, h, PANEL, color, esc(label), TEXT, 13, False, "center", font=MONO)
        cx += n * unit


def frame(d, x, y, cells, color, h=44):
    """I2C frame: cells = [(label, width, fill_color or None)]."""
    cx = x
    for label, w, c in cells:
        d.rect(cx, y, w, h, c or PANEL, color, esc(label), BG if c else TEXT, 14, bool(c), "center")
        cx += w
    return cx


def chain(d, x, y, w, h, gap, items, color, size=16, sub_size=12, mono=False, vertical=False):
    """Boxes joined by arrows. items: (title, sub or None[, color]). Returns the box ids."""
    ids = []
    for i, it in enumerate(items):
        t, s = it[0], it[1]
        c = it[2] if len(it) > 2 else color
        bx, by = (x, y + i * (h + gap)) if vertical else (x + i * (w + gap), y)
        b = (mono_box(d, bx, by, w, h, t, s, c, size, sub_size) if mono
             else d.box(bx, by, w, h, t, s, c, size=size, sub_size=sub_size))
        if ids:
            d.edge(ids[-1], b, c, width=1.5 if vertical else 2)
        ids.append(b)
    return ids


def label(d, x, y, w, s, color=TEXT, size=15):
    return d.text(x, y, w, 26, s, color, size, True, "left")


# --- HAL -----------------------------------------------------------------------------------------

def hal_max2871():
    d = Diagram("code-01-hal-max2871", 1600, 900)
    d.title("hal/max2871.c — PLL / VCO", "MAX2871 (U301) · 23.5 MHz … 6 GHz")
    api(d, 40, 130, 420, ["hal_max2871_init()", "hal_max2871_enable(en)", "hal_max2871_locked()",
                                "hal_max2871_set_freq(f)", "hal_max2871_read_die_id()",
                                "hal_max2871_read_temp_c(&t)"], RF)

    label(d, 540, 130, 1000, "set_freq(f)")
    chain(d, 540, 175, 200, 90, 40, [("f", None, LINE), ("DIVA", "÷1 … ÷128"), ("N + F / M", "fVCO / 100 MHz"),
                                     ("R5 → … → R0", "6 writes")], RF, size=18)

    label(d, 540, 345, 1000, "write_reg(r)")
    u = 30
    for lab, yy in (("CLK", 400), ("DATA", 440), ("LE", 490)):
        d.text(540, yy - 2, 70, 26, lab, MUTED, 13, True, "left", font=MONO)
    wave(d, 620, 398, 22, [(0, 1), (1, 1), (0, 1), (1, 1), (0, 1), (1, 1), (0, 3), (1, 1), (0, 1), (1, 1), (0, 5)],
         u, RF)
    bus(d, 620, 436, 28, [("b31", 2), ("b30", 2), ("b29", 2), ("…", 2), ("b1", 2), ("b0", 2)], u, LINE)
    wave(d, 620, 488, 22, [(0, 13), (1, 2), (0, 2)], u, RF)

    d.group(540, 580, 1020, 270, "Readback over MUX (register 6)", RF, size=16)
    chain(d, 575, 660, 215, 110, 37, [("overlay", "MUX = 1100"), ("shift in", "29 bits"),
                                      ("restore", "R2 · R3 · R5"), ("result", "die id · temp", LINE)], RF, size=17,
          sub_size=14)
    d.save()


def hal_hmc1119():
    d = Diagram("code-02-hal-hmc1119", 1600, 900)
    d.title("hal/hmc1119.c — step attenuator", "HMC1119 (U401) · 7-bit word + latch · 0.25 dB per step")
    api(d, 40, 130, 420, ["hal_hmc1119_init()", "hal_hmc1119_set_atten(code)"], RF)
    d.pill(40, 270, 200, 34, "starts at 127", POWER, 14)

    d.box(560, 140, 200, 440, "RP2350B", None, CORE1, size=18)
    d.box(1080, 140, 220, 440, "HMC1119", None, RF, size=18)
    pins = [("D0", 23), ("D1", 22), ("D2", 21), ("D3", 20), ("D4", 19), ("D5", 18), ("D6", 17), ("LE", 24)]
    for i, (n, g) in enumerate(pins):
        yy = 185 + i * 50
        d.line(760, yy, 1080, yy, RF if n != "LE" else POWER, 2, label=n, lsize=14)
    d.line(1520, 250, 1300, 250, RF, 2.5, label="RF in", lsize=14)
    d.line(1300, 470, 1520, 470, RF, 2.5, label="RF out", lsize=14)

    u = 40
    d.text(560, 640, 90, 26, "D0–D6", MUTED, 13, True, "left", font=MONO)
    d.text(560, 690, 90, 26, "LE", MUTED, 13, True, "left", font=MONO)
    bus(d, 660, 638, 30, [("old code", 5), ("new code", 15)], u, RF)
    wave(d, 660, 688, 22, [(0, 9), (1, 2), (0, 9)], u, POWER)

    d.rect(640, 790, 840, 26, PANEL_2, RF)
    for code in (0, 32, 64, 96, 127):
        xx = 640 + code / 127 * 840
        d.line(xx, 784, xx, 822, RF, 2, end="none")
        d.text(xx - 60, 756, 120, 24, str(code), TEXT, 13, True, "center", font=MONO)
        d.text(xx - 70, 826, 140, 24, f"{code * 0.25:g} dB", MUTED, 13, False, "center")
    d.save()


def hal_trf37a73():
    d = Diagram("code-03-hal-trf37a73", 1600, 900)
    d.title("hal/trf37a73.c — RF amplifier", "TRF37A73 (U402) · +12 dB · PWDN active high")
    api(d, 40, 130, 420, ["hal_trf37a73_init()", "hal_trf37a73_enable(en)"], RF)
    table(d, 40, 300, [210, 210], [
        ["PWDN", "Amp"], ["high", "off"], ["low", "on"], ["floating", "on"]], RF, row_h=40,
        size=15, hl={(1, 1): BAD, (2, 1): MEAS, (3, 1): POWER})

    att = d.box(580, 190, 200, 80, "HMC1119", None, RF, size=17)
    amp = d.tri(890, 170, 180, 120, RF, "+12 dB")
    d.text(890, 136, 180, 24, "TRF37A73", RF, 15, True, "center")
    sw = d.box(1180, 190, 220, 80, "Port select", None, RF, size=17)
    d.edge(att, amp, RF)
    d.edge(amp, sw, RF)
    mcu = d.box(850, 400, 260, 70, "RP2350B", None, CORE1, size=17)
    d.edge(mcu, amp, POWER, exit=(0.5, 0), entry=(0.5, 0.75), label="PWDN")

    d.group(560, 560, 1000, 290, "rf_ctrl_source_power(on)", RF, size=16)
    d.text(590, 625, 80, 30, "on", MEAS, 17, True, "left")
    chain(d, 680, 610, 250, 60, 50, [("amp on", None), ("MAX2871 on", None), ("100 ms", None)], MEAS, size=16)
    d.text(590, 745, 80, 30, "off", BAD, 17, True, "left")
    chain(d, 680, 730, 250, 60, 50, [("MAX2871 off", None), ("amp off", None)], BAD, size=16)
    d.save()


def hal_rf_switch():
    d = Diagram("code-04-hal-rf-switch", 1600, 900)
    d.title("hal/rf_switch.c — RF switches", "QPC6324 SPDT · RFSW6042 SP4T · two control lines each")
    api(d, 40, 130, 460, ["hal_rf_switch_t { c1, c2 }", "hal_rf_switch_init(&sw)", "hal_rf_switch_set(&sw, code)"],
        RF)
    label(d, 40, 330, 460, "RFSW6042")
    table(d, 40, 365, [80, 80, 90, 210], [
        ["C1", "C2", "code", "selected"], ["0", "0", "0", "RF4"], ["1", "0", "1", "RF1"], ["0", "1", "2", "RF2"],
        ["1", "1", "3", "RF3"]], RF, mono=(0, 1, 2), row_h=36, size=15)
    label(d, 40, 580, 460, "QPC6324")
    table(d, 40, 615, [80, 80, 90, 210], [
        ["C1", "C2", "code", "selected"], ["0", "0", "0", "all terminated"], ["1", "0", "1", "RF1"],
        ["0", "1", "2", "RF2"], ["1", "1", "3", "—"]], RF, mono=(0, 1, 2), row_h=36, size=15)

    table(d, 580, 130, [200, 200, 200], [
        ["Switch", "Part", "Block"],
        ["LPF1", "RFSW6042", "source.c"], ["LPF2", "RFSW6042", "source.c"],
        ["CPLR1 … 6", "RFSW6042", "coupler.c"], ["PORT", "QPC6324", "ports.c"],
        ["Z", "QPC6324", "ports.c"]], RF, row_h=36, size=15)

    label(d, 580, 380, 980, "Port 2 → port 1  (C1 C2)")
    for i, (t, s, c) in enumerate((("one write", "10 → 01", MEAS), ("C1 first", "10 → 00 → 01", BAD),
                                   ("C2 first", "10 → 11 → 01", BAD))):
        d.box(580 + i * 330, 420, 300, 110, t, s, c, size=18, sub_size=17)

    b1 = d.rect(640, 690, 120, 50, PANEL_2, RF, "bit 1", TEXT, 15, True, "center", font=MONO)
    b0 = d.rect(760, 690, 120, 50, PANEL_2, RF, "bit 0", TEXT, 15, True, "center", font=MONO)
    p2 = d.box(1080, 630, 240, 56, "C2", None, RF, size=16)
    p1 = d.box(1080, 744, 240, 56, "C1", None, RF, size=16)
    d.edge(b1, p2, RF, exit=(0.5, 0), entry=(0, 0.5), pts=[(700, 658)])
    d.edge(b0, p1, RF, exit=(0.5, 1), entry=(0, 0.5), pts=[(820, 772)])
    d.save()


def hal_ad8318():
    d = Diagram("code-05-hal-ad8318", 1600, 900)
    d.title("hal/ad8318.c — log detector", "AD8318 (U507) · RF power → DC voltage linear in dB")
    api(d, 40, 130, 420, ["hal_ad8318_init()", "hal_ad8318_enable(en)", "hal_ad8318_read_raw()"], MEAS)

    chain(d, 540, 140, 200, 80, 70, [("Coupler bank", None), ("AD8318", None), ("RP2350 ADC", None, CORE1),
                                     ("0 … 4095", None, LINE)], MEAS, size=17)

    x0, x1, y0, y1 = 620, 1180, 330, 790

    def px(dbm):
        return x0 + (dbm + 65) / 70 * (x1 - x0)

    def py(v):
        return y1 - v / 2.4 * (y1 - y0)

    d.rect(px(-55), y0, px(0) - px(-55), y1 - y0, MEAS, "none", opacity=12)
    d.line(x0, y1, x1 + 10, y1, LINE, 1.5)
    d.line(x0, y1, x0, y0 - 10, LINE, 1.5)
    for dbm in (-60, -40, -20, 0):
        d.line(px(dbm), y1, px(dbm), y1 + 6, LINE, 1.5, end="none")
        d.text(px(dbm) - 30, y1 + 8, 60, 20, str(dbm), MUTED, 13, False, "center")
    for v in (0.5, 1.0, 1.5, 2.0):
        d.line(x0 - 6, py(v), x0, py(v), LINE, 1.5, end="none")
        d.text(x0 - 60, py(v) - 10, 50, 20, f"{v:g} V", MUTED, 13, False, "right")
    d.text(x1 - 100, y1 + 30, 140, 20, "dBm", MUTED, 13, True, "right")
    d.line(px(-60), py(-0.0245 * -80), px(0), py(-0.0245 * -20), MEAS, 3, end="none")
    d.text(px(-55) + 8, y0 + 8, 200, 22, "−55 … 0 dBm", MEAS, 13, True, "left")
    d.line(x0, py(2.11), x1, py(2.11), POWER, 1.5, end="none", dashed=True)
    d.text(px(-20), py(2.11) - 26, 150, 22, "parked", POWER, 13, True, "left")
    d.text(px(-60), py(1.1), 200, 24, "−24.5 mV/dB", MEAS, 14, True, "left")

    panel(d, 1260, 330, 300, 230, CORE1)
    d.html(1285, 350, 260, 200, f'<font face="{MONO}">V = raw · 3.3 / 4095<br><br>dBm ≈ 20 + V / −0.0245<br><br>'
           f'ΔdB = Δraw / −30.4</font>', TEXT, 15)
    d.save()


def hal_eeprom():
    d = Diagram("code-06-hal-eeprom-24aa32a", 1600, 900)
    d.title("hal/eeprom_24aa32a.c — I2C EEPROM", "24AA32A (U203) · 4096 bytes · 32-byte pages")
    api(d, 40, 130, 420, ["hal_eeprom_24aa32a_init()", "hal_eeprom_24aa32a_read()", "hal_eeprom_24aa32a_write()"],
        CORE0)
    table(d, 40, 330, [160, 260], [
        ["Speed", "400 kHz"], ["Address", "0x50"]], CORE0, row_h=36, size=15,
        head=False)

    label(d, 540, 130, 600, "write")
    end = frame(d, 540, 162, [("S", 50, None), ("0x50 W", 110, CORE0), ("addr hi", 100, None),
                              ("addr lo", 100, None), ("data ≤ 32", 200, None), ("P", 50, None)], CORE0)
    d.box(end + 40, 159, 150, 50, "5 ms", None, POWER, size=15)
    label(d, 540, 235, 600, "read")
    frame(d, 540, 267, [("S", 50, None), ("0x50 W", 110, CORE0), ("addr hi", 100, None), ("addr lo", 100, None),
                        ("Sr", 50, None), ("0x50 R", 110, CORE0), ("data", 200, None), ("P", 50, None)], CORE0)

    d.group(540, 360, 1020, 200, "Page split: write(0x0F0, 40 bytes)", CORE0, size=16)
    bx, scale = 590, 14.5
    d.rect(bx, 430, 64 * scale, 50, PANEL_2, LINE)
    d.rect(bx + 16 * scale, 430, 16 * scale, 50, CORE0, "none", "16 B", BG, 14, True)
    d.rect(bx + 32 * scale, 430, 24 * scale, 50, MEAS, "none", "24 B", BG, 14, True)
    for a in (0x0E0, 0x100, 0x120):
        xx = bx + (a - 0x0E0) * scale
        d.line(xx, 420, xx, 492, POWER if a == 0x100 else LINE, 2 if a == 0x100 else 1, end="none")
        d.text(xx - 40, 498, 80, 20, f"0x{a:03X}", MUTED, 13, False, "center", font=MONO)

    d.group(540, 600, 1020, 250, "Bus recovery", CORE0, size=16)
    r0 = d.box(575, 690, 200, 70, "release", None, CORE0, size=16)
    q = diamond(d, 820, 670, 190, 110, "SDA low?", CORE0)
    pulse = d.box(1070, 690, 200, 70, "1 SCL clock", None, CORE0, size=16)
    init = d.box(1320, 690, 210, 70, "i2c_init()", None, MEAS, size=16)
    d.edge(r0, q, CORE0)
    d.edge(q, pulse, CORE0, label="yes")
    d.edge(pulse, q, CORE0, exit=(0.5, 1), entry=(0.5, 1), pts=[(1170, 815), (915, 815)], label="≤ 9")
    d.edge(q, init, MEAS, exit=(0.5, 0), entry=(0.5, 0), pts=[(915, 650), (1425, 650)], label="no")
    d.save()


# --- rf_control ----------------------------------------------------------------------------------

def rf_ctrl():
    d = Diagram("code-07-rf-ctrl-blocks", 1600, 900)
    d.title("rf_control blocks — rf_ctrl_*", "One RF block per file · the only HAL callers")
    top = d.box(40, 120, 1520, 46, "rf_state_*", None, CORE1, fill=PANEL_2, size=16, font=MONO)
    cols = [40, 430, 820, 1210]
    w = 350
    groups = []
    for x, (name, color) in zip(cols, (("source.c", RF), ("coupler.c", MEAS), ("detector.c", MEAS),
                                       ("ports.c", HOST))):
        groups.append(d.group(x, 200, w, 570, name, color, size=16))
        d.edge(top, groups[-1], CORE1, exit=((x + w / 2 - 40) / 1520, 1), entry=(0.5, 0), width=1.5)

    def fns(x, names):
        for i, n in enumerate(names):
            d.text(x + 18, 240 + i * 32, w - 36, 30, n, TEXT, 13, False, "left", font=MONO)
        return 240 + len(names) * 32

    x = cols[0]
    y = fns(x, ["rf_ctrl_source_power()", "rf_ctrl_atten_write()", "rf_ctrl_source_set_freq()",
                "rf_ctrl_source_locked()", "rf_ctrl_lpf_select()"])
    table(d, x + 15, y + 30, [130, 95, 95], [
        ["LPF", "LPF1", "LPF2"], ["none", "3", "1"], ["3500", "4", "2"], ["1747", "2", "4"], ["947", "1", "3"]],
        RF, row_h=34, align=["left", "center", "center"])

    x = cols[1]
    y = fns(x, ["rf_ctrl_coupler_select()"])
    table(d, x + 10, y + 30, [84] + [41] * 6, [
        ["path", "502", "501", "503", "504", "506", "505"],
        ["P1 fwd", "2", "4", "3", "2", "4", "1"], ["P1 bwd", "4", "4", "4", "1", "2", "1"],
        ["P2 fwd", "1", "2", "1", "3", "3", "2"], ["P2 bwd", "1", "1", "2", "4", "3", "4"]], MEAS, row_h=36,
        align=["left"] + ["center"] * 6, mono=(1, 2, 3, 4, 5, 6))

    x = cols[2]
    y = fns(x, ["rf_ctrl_detector_init()", "rf_ctrl_detector_read_raw()", "rf_ctrl_detector_sample()"])
    import random
    random.seed(4)
    vals = sorted(random.randint(10, 60) for _ in range(16))
    bx, by = x + 30, y + 200
    for i, v in enumerate(vals):
        d.rect(bx + i * 18, by - v * 2, 13, v * 2, MEAS if i in (7, 8) else PANEL_2, "none")
    d.line(bx - 6, by, bx + 16 * 18, by, LINE, 1, end="none")
    d.text(bx, by + 8, 290, 22, "16 samples → median", MUTED, 13, False, "center")

    x = cols[3]
    y = fns(x, ["rf_ctrl_port_switch_set()", "rf_ctrl_z_switch_set()"])
    table(d, x + 15, y + 30, [60, 80, 85, 95], [
        ["port", "C1", "C2", "LED lit"], ["0", "0", "0", "PORT2"], ["1", "0", "1", "PORT1"],
        ["2", "1", "0", "PORT2"]], HOST, row_h=34, mono=(1, 2))
    table(d, x + 15, y + 200, [60, 80, 85, 95], [
        ["z", "C1", "C2", "LED lit"], ["50 Ω", "1", "0", "Z50"], ["75 Ω", "0", "1", "Z75"]], HOST,
        row_h=34, mono=(1, 2))

    hals = ["hal_max2871 · hal_hmc1119\nhal_trf37a73 · hal_rf_switch", "hal_rf_switch × 6", "hal_ad8318",
            "hal_rf_switch × 2"]
    for x, g, t, c in zip(cols, groups, hals, (RF, MEAS, MEAS, HOST)):
        h = d.box(x + 25, 800, w - 50, 60, t, None, LINE, fill=PANEL_2, size=13, font=MONO)
        d.edge(g, h, c, width=1.5)
    d.save()


def rf_state():
    d = Diagram("code-08-rf-state", 1600, 900)
    d.title("rf_control/state.c — rf_state_*", "Current RF configuration · safe order of every change")
    table(d, 40, 130, [200, 220], [
        ["state", "after init"], ["source", "PARKED"], ["port", "1"], ["atten", "127"], ["z", "50"],
        ["freq", "1 GHz"], ["lpf", "1747 MHz"], ["coupler path", "P1 fwd"], ["source_cold", "false"]], CORE1,
        row_h=36, mono=(0,))

    label(d, 540, 120, 600, "rf_state_set_source()")
    off = d.box(560, 220, 230, 100, "OFF", None, BAD, size=22)
    park = d.box(940, 220, 230, 100, "PARKED", None, POWER, size=22)
    on = d.box(1320, 220, 230, 100, "ON", None, MEAS, size=22)
    d.edge(off, park, POWER, exit=(0.75, 0), entry=(0.25, 0), pts=[(732, 190), (997, 190)])
    d.edge(park, off, BAD, exit=(0.25, 1), entry=(0.75, 1), pts=[(997, 350), (732, 350)])
    d.edge(park, on, MEAS, exit=(0.75, 0), entry=(0.25, 0), pts=[(1112, 190), (1377, 190)])
    d.edge(on, park, POWER, exit=(0.25, 1), entry=(0.75, 1), pts=[(1377, 350), (1112, 350)])
    d.edge(on, off, BAD, exit=(0.5, 1), entry=(0.5, 1), pts=[(1435, 400), (675, 400)], dashed=True, width=1.5)
    d.text(700, 160, 330, 24, "atten 127 → terminate → power up", POWER, 13, True, "center")
    d.text(1080, 160, 330, 24, "route → atten", MEAS, 13, True, "center")
    d.text(700, 354, 330, 24, "atten 127 → terminate → power down", BAD, 13, True, "center")
    d.text(1080, 354, 330, 24, "atten 127 → terminate", POWER, 13, True, "center")

    d.group(540, 470, 500, 200, "rf_state_set_freq()", RF, size=16)
    s1 = d.box(570, 540, 130, 60, "tune", None, RF, size=16)
    q = diamond(d, 740, 520, 120, 100, "cold?", RF)
    s2 = d.box(900, 540, 120, 60, "20 ms\ntune", None, POWER, size=15)
    d.edge(s1, q, RF)
    d.edge(q, s2, POWER, label="yes")

    d.group(1080, 470, 480, 200, "rf_state_set_lpf_for_freq()", RF, size=16)
    table(d, 1110, 515, [210, 210], [
        ["≤ 947 MHz", "947"], ["≤ 1747 MHz", "1747"], ["≤ 3500 MHz", "3500"], ["above", "none"]], RF, row_h=34,
        head=False)

    label(d, 40, 710, 600, "rf_state_init()")
    chain(d, 40, 760, 222, 70, 37, [("init blocks", None), ("OFF", None), ("50 Ω · LPF · P1", None),
                                    ("wait 1 s", None), ("PARKED", None), ("1 GHz ×2", None)], CORE1, size=16)
    d.save()


def rf_app():
    d = Diagram("code-09-rf-app", 1600, 900)
    d.title("rf_control/application.c — rf_app_*", "The RF API for the rest of the firmware · one mode at a time")
    d.group(40, 120, 480, 420, "Modes", CORE1, size=16)
    idle = d.box(190, 290, 180, 80, "IDLE", None, CORE1, size=20)
    ms = [d.box(60, 170, 200, 60, "SWEEP", None, CORE1, size=16),
          d.box(300, 170, 200, 60, "GENERATOR", None, CORE1, size=16),
          d.box(60, 430, 200, 60, "PWR_DETECTOR", None, CORE1, size=16),
          d.box(300, 430, 200, 60, "MANUAL", None, CORE1, size=16)]
    for m, ex, en in zip(ms, ((0.5, 1), (0.5, 1), (0.5, 0), (0.5, 0)), ((0.2, 0), (0.8, 0), (0.2, 1), (0.8, 1))):
        d.edge(idle, m, CORE1, exit=en, entry=ex, start="block", width=1.5)

    d.group(40, 580, 480, 280, "wait_for_lock()", RF, size=16)
    chain(d, 75, 640, 410, 50, 24, [("LD high 500 µs", None), ("≤ 2 s first · ≤ 20 ms retune", None),
                                    ("cancelled() every 1 ms", None)], RF, size=15, vertical=True)

    d.group(560, 120, 520, 740, "One sweep point", MEAS, size=16)
    chain(d, 600, 170, 440, 50, 22, [("set_lpf_for_freq(f)", None), ("set_freq(f)", None),
                                     ("set_port() → ON", None), ("coupler path · 100 µs", None),
                                     ("wait_for_lock()", None), ("200 µs", None)], MEAS, size=15, mono=True,
          vertical=True)
    table(d, 600, 600, [140, 150, 150], [
        ["source", "path", ""], ["port 1", "P1 fwd", "ref 1"], ["", "P1 bwd", "S11"], ["", "P2 bwd", "S21"],
        ["port 2", "P2 fwd", "ref 2"], ["", "P2 bwd", "S22"], ["", "P1 bwd", "S12"]], MEAS, row_h=34)

    flows = [("Session", CORE1, ["PARKED", "tune f0", "lock ≤ 2 s"]),
             ("GENERATOR", RF, ["PARKED", "tune", "lock", "ON"]),
             ("PWR_DETECTOR", MEAS, ["PARKED", "bwd path", "read"]),
             ("MANUAL", POWER, ["lpf", "cplr", "freq", "port", "src"])]
    for i, (t, c, steps) in enumerate(flows):
        y = 120 + i * 190
        panel(d, 1120, y, 440, 170, c)
        d.text(1140, y + 12, 400, 26, t, c, 16, True, "left")
        sw = (400 - 24 * (len(steps) - 1)) / len(steps)
        prev = None
        for j, s in enumerate(steps):
            b = d.box(1140 + j * (sw + 24), y + 75, sw, 56, s, None, c, size=14)
            if prev and t != "MANUAL":
                d.edge(prev, b, c, width=1.5)
            prev = b
    d.save()


# --- the rest of src/ ----------------------------------------------------------------------------

def main_c():
    d = Diagram("code-10-main", 1600, 900)
    d.title("main.c — boot, main loop, LEDs, watchdog", "Core 0")
    d.group(40, 120, 460, 600, "Boot", CORE0, size=16)
    chain(d, 70, 165, 400, 44, 16, [("board_gpio_init()", None), ("boot blink", None), ("wait 200 ms", None),
                                    ("rf_task_start()", None), ("eeprom_init()", None), ("tusb_init()", None),
                                    ("USB re-attach", None), ("rf_task_wait_ready()", None),
                                    ("watchdog 8 s", None)], CORE0, size=15, mono=True, vertical=True)

    d.group(540, 120, 480, 600, "Main loop", CORE0, size=16)
    ids = chain(d, 570, 170, 400, 60, 30, [("tud_task()", None), ("poll_usb_commands()", None),
                                           ("protocol_poll()", None), ("button → sdi_test_start()", None),
                                           ("sdi_test_update_leds()", None), ("update_status_leds()", None)],
                CORE0, size=15, mono=True, vertical=True)
    d.edge(ids[-1], ids[0], CORE0, exit=(1, 0.5), entry=(1, 0.5), pts=[(1000, 650), (1000, 200)], width=1.5)

    table(d, 1060, 120, [300], [
        ["LEDs"], ["BOOTED"], ["READY"], ["BUSY"], ["SDI pass"], ["SDI fail"], ["PORT1 / PORT2"], ["Z50 / Z75"]],
        CORE0, row_h=34)
    table(d, 1060, 430, [100, 300], [
        ["blinks", "failed"], ["1", "EEPROM"], ["2", "source lock"], ["3", "detector"], ["4", "USB"]], BAD, row_h=34)

    d.group(40, 750, 1520, 110, "Watchdog", BAD, size=16)
    chain(d, 80, 790, 330, 50, 70, [("update_heartbeat()", None, CORE0), ("core 1 progressing?", None, CORE1),
                                    ("watchdog_update()", None, MEAS)], CORE0, size=15)
    d.save()


def rf_task():
    d = Diagram("code-11-rf-task", 1600, 900)
    d.title("rf_task.c — link between the cores", "Requests to core 1 · one final reply each")
    api(d, 40, 120, 420, ["rf_task_start()", "rf_task_wait_ready()", "rf_task_send()", "rf_task_poll()",
                          "rf_task_cancel()", "rf_task_busy_us()", "rf_task_core1_loops()"], CORE0, title="Core 0")

    req = d.box(520, 150, 220, 70, "request queue", "8", CORE0, size=16)
    can = d.box(520, 290, 220, 70, "cancel_seq", None, BAD, size=16)
    rep = d.box(520, 430, 220, 70, "reply queue", "8", CORE1, size=16)

    d.group(800, 120, 760, 460, "Core 1 · rf_task_step()", CORE1, size=16)
    q1 = diamond(d, 840, 155, 190, 100, "request?", CORE1)
    h = mono_box(d, 1110, 175, 400, 60, "handle(req)", None, CORE1)
    q2 = diamond(d, 840, 305, 190, 100, "sweep?", CORE1)
    s = mono_box(d, 1110, 325, 400, 60, "sweep_step()", None, CORE1)
    idle = d.box(865, 470, 140, 50, "idle", None, LINE, size=15)
    d.edge(q1, h, CORE1, label="yes")
    d.edge(q1, q2, CORE1, label="no")
    d.edge(q2, s, CORE1, label="yes")
    d.edge(q2, idle, CORE1, label="no")
    d.edge(req, q1, CORE0, exit=(1, 0.5), entry=(0, 0.5))
    d.edge(can, q2, BAD, exit=(1, 0.5), entry=(0, 0.5), dashed=True, width=1.5)
    d.edge(s, rep, CORE1, exit=(20 / 400, 1), entry=(1, 0.5), pts=[(1130, 545), (770, 545), (770, 465)])
    table(d, 1160, 430, [170, 200], [
        ["set, status", "run"], ["mode_exit", "end sweep"], ["other", "BUSY"]], CORE1, row_h=34, mono=(0,),
        head=False)

    d.group(40, 620, 1520, 240, "Sweep", MEAS, size=16)
    ids = chain(d, 75, 680, 250, 70, 50, [("start_sweep()", None), ("point_frequency(i)", None),
                                          ("measure", None), ("reply", None), ("finish_sweep()", None)], MEAS,
                size=15, mono=True)
    d.edge(ids[3], ids[1], MEAS, exit=(0.5, 1), entry=(0.5, 1), pts=[(1075, 785), (500, 785)], width=1.5)
    for i, (t, c) in enumerate((("DONE", MEAS), ("ABORTED", BAD), ("STARTUP_FAILED", BAD), ("POINT_FAILED", BAD))):
        d.pill(780 + i * 190, 810, 175, 32, t, c, 13)
    d.save()


def protocol():
    d = Diagram("code-12-protocol", 1600, 900)
    d.title("protocol.c — JSON commands", "Core 0 · validate → forward → reply")
    ids = chain(d, 40, 130, 230, 60, 40, [("line", None), ("cJSON_Parse", None), ("command table", None),
                                          ("cmd_*()", None)], CORE0, size=15, mono=True)

    loc = d.group(40, 250, 480, 210, "core 0", CORE0, size=16)
    for i, t in enumerate(["ping · abort", "cal_set · cal_get · cal_clear", "sdi_test_set · sdi_test_get"]):
        d.text(70, 295 + i * 44, 430, 34, t, TEXT, 14, False, "left", font=MONO)
    fwd = d.group(560, 250, 480, 210, "core 1", CORE1, size=16)
    chain(d, 590, 320, 190, 60, 40, [("pending slot", "8"), ("rf_task_send()", None)], CORE1, size=15)
    d.edge(ids[3], loc, CORE0, exit=(0.3, 1), entry=(0.5, 0), pts=[(919, 220), (280, 220)])
    d.edge(ids[3], fwd, CORE1, exit=(0.7, 1), entry=((1011 - 560) / 480, 0))

    rows = [["command", "core", "in sweep"]]
    for n, w, s in [("ping", "0", "✓"), ("status", "1", "✓"), ("set", "1", "✓"), ("abort", "0", "✓"),
                    ("mode_exit", "1", "✓"), ("src_temp_read", "1", "✓"), ("mode_enter", "1", "busy"),
                    ("sweep", "1", "busy"), ("gen_start / _stop", "1", "busy"), ("power_*", "1", "busy"),
                    ("probe_read", "1", "busy"), ("manual", "1", "busy"), ("cal_*", "0", "busy"),
                    ("sdi_test_*", "0", "busy"), ("bootsel", "1", "busy")]:
        rows.append([n, w, s])
    hl = {}
    for r in range(1, len(rows)):
        hl[(r, 1)] = CORE0 if rows[r][1] == "0" else CORE1
        hl[(r, 2)] = MEAS if rows[r][2] == "✓" else MUTED
    table(d, 1100, 120, [230, 90, 140], rows, CORE0, row_h=31, size=14, mono=(0,), hl=hl,
          align=["left", "center", "center"])

    d.group(40, 500, 1000, 360, "protocol_poll()", CORE1, size=16)
    rp = d.box(70, 620, 200, 70, "rf_task_poll()", None, CORE1, size=15)
    sdi = d.box(330, 555, 220, 56, "SDI test", None, HOST, size=15)
    sw = d.box(330, 627, 220, 56, "USB sweep", None, MEAS, size=15)
    pd = d.box(330, 699, 220, 56, "pending slot", None, CORE1, size=15)
    for b, ey in ((sdi, 0.2), (sw, 0.5), (pd, 0.8)):
        d.edge(rp, b, CORE1, exit=(1, ey), entry=(0, 0.5))
    table(d, 620, 655, [200, 190], [
        ["REPLY", "answer"], ["BOOTSEL", "reboot"], ["POWER_EVENT", "event"], ["SILENT", "—"]], CORE1, row_h=36,
        mono=(0,), head=False)
    d.line(550, 727, 618, 727, CORE1, 1.5, dashed=True)
    for i, t in enumerate(["service_power_stream()", "service_host_gone()"]):
        d.text(70 + i * 330, 815, 320, 30, t, MUTED, 14, False, "left", font=MONO)

    d.group(1100, 640, 460, 220, "Output", HOST, size=16)
    chain(d, 1125, 710, 190, 60, 30, [("send_json()", None), ("write_cdc()", None)], HOST, size=15, mono=True)
    d.text(1125, 790, 410, 26, "pumps tud_task() · 2 s stall", MUTED, 13, False, "left")
    d.save()


def measurement():
    d = Diagram("code-13-measurement", 1600, 900)
    d.title("measurement.c — raw counts → dB", "Core 0 · one call per sweep point")
    api(d, 40, 130, 460, ["measurement_process()"], CORE0)
    panel(d, 40, 270, 460, 150, CORE1)
    d.text(60, 280, 420, 26, "rf_app_raw_measurement_t", CORE1, 15, True, "left", font=MONO)
    d.html(60, 320, 420, 130, f'<font face="{MONO}">source1_ref · reflection · transmission<br><br>'
           f'source2_ref · reflection · transmission</font>', TEXT, 13)

    table(d, 560, 130, [100, 300, 300], [
        ["S", "numerator", "reference"], ["S11", "P1 bwd", "P1 fwd"], ["S21", "P2 bwd", "P1 fwd"],
        ["S12", "P1 bwd", "P2 fwd"], ["S22", "P2 bwd", "P2 fwd"]], CORE0, row_h=40, size=15, mono=(0,))

    chain(d, 40, 560, 440, 110, 100, [("(num − ref) / −30.4", None, CORE0), ("− c(f)", "calibration curve", POWER),
                                      ("dB", None, MEAS)], CORE0, size=20, sub_size=14)
    d.text(560, 690, 440, 30, "eeprom_interpolate_cached()", MUTED, 14, False, "center", font=MONO)

    for i, (t, c) in enumerate((("locked", MEAS), ("calibrated", MEAS))):
        d.pill(40 + i * 220, 790, 200, 40, t, c, 15)
    d.save()


def eeprom_c():
    d = Diagram("code-14-eeprom", 1600, 900)
    d.title("eeprom.c — calibration and SDI storage", "Layout on top of hal_eeprom_24aa32a_*")
    table(d, 40, 120, [100, 120, 260], [
        ["offset", "size", ""], ["0x000", "32 B", "header"], ["0x020", "24 B", "cal grid"],
        ["0x038", "188 B", "SDI config"], ["0x0F4", "8 × 480 B", "cal tables"]], CORE0, row_h=36, mono=(0, 1))

    d.group(40, 330, 480, 530, "eeprom_init()", CORE0, size=16)
    a = d.box(80, 380, 400, 56, "read header", None, CORE0, size=16)
    q = diamond(d, 170, 470, 220, 110, "valid?", CORE0)
    ok = d.box(70, 640, 170, 60, "keep", None, MEAS, size=16)
    fm = d.box(300, 640, 190, 170, "format", "grid\nSDI\nheader last", POWER, size=16, sub_size=14)
    d.edge(a, q, CORE0)
    d.edge(q, ok, MEAS, exit=(0, 0.5), entry=(0.5, 0), pts=[(155, 525)], label="yes")
    d.edge(q, fm, POWER, exit=(1, 0.5), entry=(0.5, 0), pts=[(395, 525)], label="no")

    d.group(560, 120, 500, 740, "eeprom_cal_set()", POWER, size=16)
    chain(d, 600, 170, 420, 70, 46, [("validate", None), ("read grid", None), ("resample → 240", None),
                                     ("clear valid bit", None, BAD), ("write table", None),
                                     ("set valid bit", None, MEAS)], POWER, size=17, vertical=True)

    d.group(1100, 120, 460, 360, "Sweep read path", MEAS, size=16)
    chain(d, 1130, 180, 400, 70, 60, [("eeprom_cal_prefetch()", "→ RAM"), ("eeprom_interpolate_cached()", None)],
          MEAS, size=14, mono=True, vertical=True)

    api(d, 1100, 520, 460, ["eeprom_cal_get()", "eeprom_cal_clear()", "eeprom_cal_clear_all()",
                            "eeprom_sdi_test_set()", "eeprom_sdi_test_get()"], CORE0, title="Other")
    d.save()


def sdi_test():
    d = Diagram("code-15-sdi-test", 1600, 900)
    d.title("sdi_test.c — on-board SDI test", "Core 0 · non-blocking state machine")
    idle = d.box(60, 330, 180, 80, "IDLE", None, LINE, size=19)
    ent = d.box(360, 180, 240, 80, "ENTERING", None, HOST, size=18)
    swp = d.box(720, 180, 240, 80, "SWEEPING", None, HOST, size=18)
    ext = d.box(1080, 180, 240, 80, "EXITING", None, HOST, size=18)
    pulses = d.group(330, 480, 1020, 170, "LED pulses", LINE, size=15)
    ref = d.box(360, 540, 240, 80, "REFUSED", None, BAD, size=18)
    noc = d.box(720, 540, 240, 80, "NO_CONFIG", None, POWER, size=18)
    shw = d.box(1080, 540, 240, 80, "SHOWING", None, MEAS, size=18)

    d.edge(idle, ent, HOST, exit=(0.6, 0), entry=(0, 0.5), pts=[(168, 220)], label="press")
    d.edge(ent, swp, HOST, label="ok")
    d.edge(swp, ext, HOST, label="done")
    d.edge(ext, shw, MEAS, label="ok")
    d.edge(pulses, idle, LINE, exit=(0.85, 1), entry=(0, 0.5), pts=[(1197, 790), (36, 790), (36, 370)])
    d.edge(idle, ref, BAD, exit=(0.75, 1), entry=(0, 0.5), pts=[(195, 580)])
    d.edge(ent, ref, BAD, exit=(0.5, 1), entry=(0.5, 0))
    d.edge(idle, noc, POWER, exit=(0.25, 1), entry=(0.3, 1), pts=[(105, 710), (792, 710)])
    d.edge(ext, idle, LINE, exit=(0.25, 0), entry=(0.25, 0), pts=[(1140, 140), (105, 140)], dashed=True, width=1.5)
    d.text(560, 116, 400, 22, "aborted", MUTED, 13, True, "center")

    table(d, 1380, 180, [100, 80], [
        ["z", "75"], ["atten", "0"], ["points", "≤ 15"]], HOST, row_h=36, head=False, mono=(1,))
    for i, (c, t) in enumerate(((MEAS, "pass"), (BAD, "fail"))):
        d.circle(1390 + i * 100, 470, 40, c, fill=c)
        d.text(1370 + i * 100, 516, 80, 22, t, c, 14, True, "center")
    d.save()


def usb_desc():
    d = Diagram("code-16-usb-descriptors", 1600, 900)
    d.title("usb_descriptors.c — what the PC sees", "One CDC-ACM interface · virtual COM port")
    dev = d.box(40, 150, 400, 160, "Device", "0xCAFE : 0x4001", HOST, size=20, sub_size=15)
    cfg = d.box(560, 150, 400, 160, "Configuration 1", "100 mA", HOST, size=20, sub_size=15)
    i0 = d.box(1080, 130, 480, 90, "Interface 0 · CDC control", "EP 0x81", HOST, size=17, sub_size=14)
    i1 = d.box(1080, 240, 480, 90, "Interface 1 · CDC data", "EP 0x02 · 0x82", HOST, size=17, sub_size=14)
    d.edge(dev, cfg, HOST)
    d.edge(cfg, i0, HOST, exit=(1, 0.3), entry=(0, 0.5))
    d.edge(cfg, i1, HOST, exit=(1, 0.7), entry=(0, 0.5))

    label(d, 40, 420, 700, "Strings")
    table(d, 40, 460, [80, 200, 420], [
        ["0", "language", "0x0409"], ["1", "manufacturer", "ml3rc"], ["2", "product", "SNA Scalar Network Analyzer"],
        ["3", "serial", "chip unique ID"], ["4", "interface", "SNA Control"]], HOST, row_h=40, size=15, mono=(0,),
        head=False)

    api(d, 860, 420, 700, ["tud_descriptor_device_cb()", "tud_descriptor_configuration_cb()",
                           "tud_descriptor_string_cb()"], HOST, title="Callbacks")
    d.save()


if __name__ == "__main__":
    for f in (hal_max2871, hal_hmc1119, hal_trf37a73, hal_rf_switch, hal_ad8318, hal_eeprom, rf_ctrl, rf_state,
              rf_app, main_c, rf_task, protocol, measurement, eeprom_c, sdi_test, usb_desc):
        f()
