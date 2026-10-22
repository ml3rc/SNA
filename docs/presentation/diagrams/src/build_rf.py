from dio import *


def rf_chain():
    d = Diagram("hw-04-rf-chain", 1600, 900)
    d.title("RF chain", "One source routed to port 1 or 2 · one directional coupler measures every direction")

    # ---- row 1: signal generation (left → right)
    d.group(40, 110, 1520, 270, "Signal generation", RF)
    ref = d.box(70, 205, 150, 80, "LMK6C", "100 MHz reference", RF)
    pll = d.box(270, 195, 190, 100, "MAX2871", "PLL + VCO\n23.5 MHz – 6 GHz", RF)
    d.group(500, 128, 520, 236, "LPF bank — removes harmonics", LINE, size=13)
    sin = d.box(525, 205, 80, 80, "SP4T", "RFSW6042", LINE, size=14, sub_size=10)
    fl = []
    for i, (t, c) in enumerate([("Bypass", "> 3.5 GHz"), ("LPF 3.5 GHz", ""), ("LPF 1.75 GHz", ""),
                                ("LPF 0.95 GHz", "")]):
        y = 162 + i * 44
        fl.append(d.box(670, y, 200, 34, t + ("   " + c if c else ""), None, LINE, size=13, bold=False,
                        fill=PANEL_2))
    sout = d.box(915, 205, 80, 80, "SP4T", "RFSW6042", LINE, size=14, sub_size=10)
    att = d.box(1060, 200, 180, 90, "HMC1119", "attenuator\n0 … 31.75 dB", RF)
    amp = d.tri(1285, 195, 120, 100, RF, "AMP")
    d.text(1270, 300, 150, 40, "TRF37A73\n+12 dB", MUTED, 12)

    d.edge(ref, pll, RF)
    d.edge(pll, sin, RF)
    for i, f in enumerate(fl):
        y = 162 + i * 44 + 17
        d.edge(sin, f, LINE, exit=(1, 0.5), entry=(0, 0.5), pts=[(637, 245), (637, y)], width=1.5)
        d.edge(f, sout, LINE, exit=(1, 0.5), entry=(0, 0.5), pts=[(893, y), (893, 245)], width=1.5)
    d.edge(sout, att, RF)
    d.edge(att, amp, RF)

    # ---- row 2: routing and measurement (right → left)
    d.group(1240, 410, 320, 350, "Port select", RF)
    psw = d.box(1290, 480, 200, 240, "3× QPC6324", "SPDT, absorptive\n\nsource → port 1 or 2\n\nother port → 50 Ω", RF)
    d.edge(amp, psw, RF, exit=(1, 0.5), entry=(0.5, 0), pts=[(1450, 245), (1450, 440), (1390, 440)])

    d.group(560, 410, 640, 350, "Coupler bank — 6× RFSW6042 SP4T", MEAS)
    rcol = d.box(1060, 480, 110, 240, "3× SP4T", None, LINE, size=14)
    lcol = d.box(590, 480, 110, 240, "3× SP4T", None, LINE, size=14)
    pad1 = d.box(760, 500, 120, 40, "−5.6 dB pad", None, LINE, size=12, bold=False, fill=PANEL_2)
    cpl = d.box(790, 560, 180, 80, "SEDC-10-63+", "directional coupler", MEAS)
    pad2 = d.box(760, 660, 120, 40, "−5.6 dB pad", None, LINE, size=12, bold=False, fill=PANEL_2)
    d.text(985, 488, 70, 20, "port 1", MUTED, 11)
    d.text(985, 648, 70, 20, "port 2", MUTED, 11)

    d.edge(psw, rcol, RF, exit=(0, 1 / 6), entry=(1, 1 / 6))
    d.edge(psw, rcol, RF, exit=(0, 5 / 6), entry=(1, 5 / 6))
    for tgt in (pad1, pad2):
        d.edge(rcol, tgt, LINE, exit=(0, 1 / 6 if tgt == pad1 else 5 / 6), entry=(1, 0.5), width=1.5)
        d.edge(tgt, lcol, LINE, exit=(0, 0.5), entry=(1, 1 / 6 if tgt == pad1 else 5 / 6), width=1.5)
    d.edge(rcol, cpl, MEAS, exit=(0, 0.5), entry=(1, 0.5), width=1.5)
    d.edge(cpl, lcol, MEAS, exit=(0, 0.5), entry=(1, 0.5), width=1.5)
    d.text(712, 706, 228, 50, "Switches put the coupler into the measured port (either direction); "
           "the other port takes the matching pad.", MUTED, 12, align="left")

    d.group(40, 410, 490, 350, "Ports — 50 Ω SMA / 75 Ω BNC", HOST)
    for lane, yc, name in ((1, 520, "Port 1"), (2, 680, "Port 2")):
        z = d.box(330, yc - 40, 110, 80, "QPC6324", "50 / 75 Ω", HOST, size=14, sub_size=11)
        sma = d.box(70, yc - 74, 130, 44, f"SMA 50 Ω", name, HOST, size=13, sub_size=11)
        pad = d.box(215, yc + 1, 95, 46, "50→75 Ω", "pad −5.7 dB", LINE, size=12, sub_size=10, fill=PANEL_2)
        bnc = d.box(70, yc + 1, 130, 46, "BNC 75 Ω", name, HOST, size=13, sub_size=11)
        d.edge(lcol, z, RF, exit=(0, 1 / 6 if lane == 1 else 5 / 6), entry=(1, 0.5))
        d.edge(z, sma, HOST, exit=(0, 0.2), entry=(1, 0.5), pts=[(262, yc - 24), (262, yc - 52)], width=1.5)
        d.edge(z, pad, HOST, exit=(0, 0.8), entry=(1, 0.5), width=1.5)
        d.edge(pad, bnc, HOST, width=1.5)

    # ---- detector
    det = d.box(857, 790, 190, 70, "AD8318", "log detector · −55 … 0 dBm", MEAS)
    adc = d.box(1105, 790, 190, 70, "RP2350B ADC", "12 bit · median of 16", CORE1)
    fw = d.box(1350, 790, 190, 70, "Core 1 firmware", "dB = ΔADC / slope", CORE1)
    d.edge(cpl, det, MEAS, exit=(0.9, 1), entry=(0.5, 0))
    d.edge(det, adc, MEAS)
    d.edge(adc, fw, CORE1)
    return d.save()


if __name__ == "__main__":
    print(rf_chain())
