from dio import *


def system_overview():
    d = Diagram("hw-01-system-overview", 1600, 900)
    d.title("SNA — system overview", "2-port scalar network analyzer · 50 MHz – 6 GHz · 50 Ω SMA / 75 Ω BNC")
    pc = d.box(60, 330, 220, 110, "PC / Android", "SNA Client\nKotlin + Compose", HOST)
    d.group(360, 130, 860, 600, "SNA board — 4-layer PCB in a milled aluminium case", LINE)
    pwr = d.box(400, 175, 780, 60, "Power", "USB 5 V → boost 6 V → LDOs for RF  ·  buck 3.3 V for digital", POWER)
    mcu = d.box(400, 300, 260, 200, "RP2350B", "2× Cortex-M33 · 150 MHz\nUSB · ADC · 48 GPIO", CORE0, size=20)
    ee = d.box(400, 550, 120, 64, "EEPROM", "24AA32A · 4 KB", CORE0, size=14, sub_size=11)
    fl = d.box(540, 550, 120, 64, "Flash", "W25Q128 · 16 MB", CORE0, size=14, sub_size=11)
    fp = d.box(400, 650, 260, 60, "LEDs + SDI button", "on the PCB · through holes in the case", CORE0, size=14,
               sub_size=11)
    src = d.box(760, 280, 420, 90, "RF source", "MAX2871 PLL → LPF bank → attenuator → amplifier", RF)
    rt = d.box(760, 430, 420, 110, "Port + coupler switching", "3× SPDT port select · 6× SP4T coupler bank\n50 / 75 Ω switch", RF)
    det = d.box(760, 610, 420, 70, "AD8318 log detector", "RF power → DC voltage", MEAS)
    ports = d.box(1300, 430, 220, 110, "Port 1 · Port 2", "SMA 50 Ω  /  BNC 75 Ω", HOST)
    dut = d.box(1300, 640, 220, 90, "Device under test", "SDI cable, filter, amp …", LINE, dashed=True)

    d.edge(pc, mcu, HOST, exit=(1, 0.5), entry=(0, 0.425), start="block", label="USB-C · JSON")
    d.edge(pwr, mcu, POWER, exit=(0.168, 1), entry=(0.5, 0), dashed=True, width=1.5)
    d.edge(pwr, src, POWER, exit=(0.731, 1), entry=(0.5, 0), dashed=True, width=1.5)
    d.edge(mcu, src, CORE1, exit=(1, 0.125), entry=(0, 0.5), label="SPI")
    d.edge(mcu, rt, CORE1, exit=(1, 0.8), entry=(0, 0.273), label="GPIO")
    d.edge(det, mcu, MEAS, exit=(0, 0.5), entry=(1, 0.95), pts=[(710, 645), (710, 490)], label="ADC", lpos=(-0.6, 0))
    d.edge(mcu, ee, CORE0, exit=(0.231, 1), entry=(0.5, 0), label="I2C", width=1.5)
    d.edge(mcu, fl, CORE0, exit=(0.769, 1), entry=(0.5, 0), label="QSPI", width=1.5)
    d.edge(mcu, fp, CORE0, exit=(0, 0.8), entry=(0, 0.5), pts=[(380, 460), (380, 680)], width=1.5)
    d.edge(src, rt, RF, exit=(0.5, 1), entry=(0.5, 0))
    d.edge(rt, det, MEAS, exit=(0.5, 1), entry=(0.5, 0), label="coupled wave")
    d.edge(rt, ports, RF, start="block")
    d.edge(ports, dut, RF, start="block", label="cable")
    d.legend(40, 500, [(RF, "RF signal", False), (MEAS, "measured / coupled wave", False), (CORE1, "RF control", False),
                       (CORE0, "digital bus", False), (POWER, "supply rails", True), (HOST, "USB / ports", False)],
             w=280)
    d.text(60, 780, 1480, 60,
           "Measures how much RF power a device reflects (S11, S22) and lets through (S21, S12) — magnitude only, "
           "no phase.", MUTED, 16)
    return d.save()


def power_tree():
    d = Diagram("hw-02-power-tree", 1600, 900)
    d.title("Power tree", "Everything from USB (≤ 500 mA) · boost + LDOs keep the RF rails quiet")
    usb = d.box(40, 470, 170, 90, "USB-C", "5 V · ~500 mA", POWER)
    boost = d.box(280, 355, 190, 90, "Boost 6 V", "MIC2253", POWER)
    lc = d.box(520, 370, 100, 60, "LC filter", None, POWER, size=13, fill=PANEL_2)
    buck = d.box(280, 720, 190, 90, "Buck 3.3 V", "TPS62802", CORE0)
    l5 = [d.box(730, 160, 200, 80, "LDO 5 V", "LDL212 · PLL", POWER),
          d.box(730, 390, 200, 80, "LDO 5 V", "LDL212 · RF ctrl", POWER),
          d.box(730, 560, 200, 80, "LDO 5 V", "LDL212 · detector", POWER)]
    l3 = [d.box(1020, 120, 220, 70, "LDO 3.3 V", "AP2114 · PLL RF", POWER, size=15),
          d.box(1020, 220, 220, 70, "LDO 3.3 V", "NCP702 · PLL charge pump", POWER, size=15),
          d.box(1020, 360, 220, 70, "LDO 3.3 V", "NCP702 · attenuator + amp", POWER, size=15),
          d.box(1020, 460, 220, 70, "LDO 3.3 V", "NCP702 · RF switches", POWER, size=15)]
    loads = [("MAX2871 VCO / RF", RF, 155), ("MAX2871 charge pump", RF, 255), ("HMC1119 · TRF37A73", RF, 395),
             ("RF switches", RF, 495), ("AD8318 log detector", MEAS, 600), ("RP2350B · flash · EEPROM · LEDs", CORE0, 765)]
    lb = [d.box(1300, y - 26, 260, 52, t, None, c, size=14, fill=PANEL_2) for t, c, y in loads]

    d.edge(usb, boost, POWER, exit=(1, 0.3), entry=(0, 0.5), pts=[(245, 497), (245, 400)], label="~450 mA", lpos=(0.3, 0))
    d.edge(usb, buck, CORE0, exit=(1, 0.7), entry=(0, 0.5), pts=[(245, 533), (245, 765)], label="~50 mA", lpos=(0.3, 0))
    d.edge(boost, lc, POWER)
    d.edge(lc, l5[0], POWER, exit=(1, 0.5), entry=(0, 0.5), pts=[(660, 400), (660, 200)])
    d.edge(lc, l5[1], POWER, exit=(1, 0.5), entry=(0, 0.5), pts=[(660, 400), (660, 430)])
    d.edge(lc, l5[2], POWER, exit=(1, 0.5), entry=(0, 0.5), pts=[(660, 400), (660, 600)])
    for y, t in ((200, "200 mA"), (430, "60 mA"), (600, "70 mA")):
        d.text(664, y - 24, 64, 20, t, POWER, 12, True)
    for src, dst, y0, y1, t in ((0, 0, 200, 155, "135 mA"), (0, 1, 200, 255, "55 mA"), (1, 2, 430, 395, "55 mA"),
                                (1, 3, 430, 495, "1 mA")):
        d.edge(l5[src], l3[dst], POWER, exit=(1, 0.5), entry=(0, 0.5), pts=[(960, y0), (960, y1)])
        d.text(962, y1 - 24, 58, 20, t, POWER, 12, True)
    for i in range(4):
        d.edge(l3[i], lb[i], RF, width=1.5)
    d.edge(l5[2], lb[4], MEAS, width=1.5)
    d.edge(buck, lb[5], CORE0, width=1.5)
    d.group(40, 125, 560, 170, "Why this structure", MUTED)
    d.html(55, 158, 540, 170,
           f'<span style="color:{TEXT}">• Boost to 6 V gives every LDO headroom<br>• LDOs sit next to their circuit<br>'
           f'• Ferrite bead after every LDO<br>• 0 Ω link per rail for bring-up<br>• Digital switcher kept away '
           f'from the RF rails</span>', size=15)
    return d.save()


def digital():
    d = Diagram("hw-03-mcu", 1600, 900)
    d.title("MCU — RP2350B", "All 48 GPIOs used · every GPIO has a series resistor to soften edges")
    mcu = d.box(600, 230, 400, 420, "RP2350B", "QFN-80 · 2× Cortex-M33 · 520 KB RAM\n\ncore 0: USB, JSON, calibration\n"
                "core 1: all RF hardware", CORE0, size=26, sub_size=14)
    left = [("USB-C", "ESD diodes · reverse-polarity FET", HOST, 250),
            ("SWD header", "debug / flashing (J202)", LINE, 380),
            ("12 MHz crystal", "Y201", LINE, 500),
            ("LEDs + SDI button", "on the PCB · 7 LEDs · 1 button", CORE0, 610)]
    for t, s, c, y in left:
        b = d.box(130, y - 40, 300, 80, t, s, c)
        d.edge(mcu, b, c, exit=(0, (y - 230) / 420), entry=(1, 0.5), start="block" if t == "USB-C" else "none",
               end="block" if t != "12 MHz crystal" else "none")
    right = [("MAX2871 synthesizer", "SPI + CE, LD, MUX, RF_EN · 7 pins", RF, 255),
             ("HMC1119 attenuator", "7 bit parallel + LE · 8 pins", RF, 345),
             ("TRF37A73 amplifier", "power-down · 1 pin", RF, 425),
             ("RF switches", "LPF 4 · coupler 12 · port 2 · Z 2 pins", RF, 505),
             ("AD8318 detector", "ADC input + enable · 2 pins", MEAS, 600)]
    for t, s, c, y in right:
        b = d.box(1170, y - 34, 330, 68, t, s, c, size=15)
        if t.startswith("AD8318"):
            d.edge(b, mcu, c, exit=(0, 0.5), entry=(1, (y - 230) / 420), label="ADC5", lpos=(0, 0))
        else:
            d.edge(mcu, b, c, exit=(1, (y - 230) / 420), entry=(0, 0.5))
    fl = d.box(560, 730, 220, 80, "W25Q128 flash", "QSPI · firmware (boot)", CORE0, size=15)
    ee = d.box(820, 730, 220, 80, "24AA32A EEPROM", "I2C · calibration, SDI test", CORE0, size=15)
    d.edge(mcu, fl, CORE0, exit=(0.175, 1), entry=(0.5, 0), start="block", label="QSPI")
    d.edge(mcu, ee, CORE0, exit=(0.825, 1), entry=(0.5, 0), start="block", label="I2C")
    p3 = d.box(700, 120, 200, 56, "3.3 V digital", "+ internal 1.1 V core buck", POWER, size=14, sub_size=11)
    d.edge(p3, mcu, POWER, dashed=True, width=1.5)
    return d.save()


def pll():
    d = Diagram("hw-05-pll", 1600, 900)
    d.title("MAX2871 — fractional-N PLL", "Turns a fixed 100 MHz reference into any frequency from 23.5 MHz to 6 GHz")
    d.group(300, 140, 1000, 470, "MAX2871", RF)
    ref = d.box(60, 290, 180, 90, "Reference", "LMK6C · 100 MHz", RF)
    r = d.box(340, 300, 110, 70, "÷ R", "R = 1", LINE, size=18)
    pfd = d.box(500, 290, 150, 90, "Phase / freq.\ndetector", None, RF, size=15)
    cp = d.box(700, 300, 140, 70, "Charge\npump", None, RF, size=15)
    lf = d.box(700, 470, 140, 90, "Loop filter", "external RC\n450 kHz BW", POWER, size=15)
    vco = d.box(900, 290, 160, 90, "VCO", "3 – 6 GHz", RF, size=20)
    div = d.box(1110, 290, 150, 90, "÷ DIV", "1, 2, 4 … 128", LINE, size=18)
    n = d.box(900, 170, 160, 70, "÷ (N + F/M)", "frac-N, M = 4095", LINE, size=15)
    out = d.box(1360, 290, 180, 90, "RF out", "23.5 MHz – 6 GHz", RF)
    ld = d.box(500, 470, 150, 70, "Lock detect", "LD pin → MCU", MEAS, size=15)
    d.edge(ref, r, RF)
    d.edge(r, pfd, RF)
    d.text(446, 306, 58, 22, "f_PFD", RF, 12, True)
    d.edge(pfd, cp, RF)
    d.edge(cp, lf, RF, exit=(0.5, 1), entry=(0.5, 0))
    d.edge(lf, vco, POWER, exit=(1, 0.5), entry=(0.5, 1), pts=[(980, 515)], label="tune voltage", lpos=(-0.3, 0))
    d.edge(vco, div, RF)
    d.text(1056, 306, 58, 22, "f_VCO", RF, 12, True)
    d.edge(div, out, RF)
    d.edge(vco, n, LINE, exit=(0.5, 0), entry=(0.5, 1), dashed=False)
    d.edge(n, pfd, LINE, exit=(0, 0.5), entry=(0.5, 0), pts=[(575, 205)], label="feedback")
    d.edge(pfd, ld, MEAS, exit=(0.5, 1), entry=(0.5, 0))
    d.rect(300, 650, 1000, 110, PANEL, LINE, rounded=True,
           value=f'<span style="font-size:22px"><b>f_out = f_PFD · (N + F/M) / DIV</b></span><br>'
                 f'<span style="color:{MUTED};font-size:15px">Example 1 GHz:  VCO = 100 MHz · 40 = 4 GHz,  DIV = 4</span>')
    d.text(60, 790, 1480, 50, "Loop locks when the divided VCO matches the reference in phase. Firmware waits for LD "
           "(stable 500 µs) before measuring.", MUTED, 15)
    return d.save()


def coupler_paths():
    d = Diagram("hw-06-coupler-paths", 1600, 900)
    d.title("How one coupler measures S11 and S21", "Source on port 1 · three readings per frequency · all values in dB")
    rows = [("P1_FWD", "wave going out of port 1", "reference", 1, "fwd"),
            ("P1_BWD", "wave reflected by the DUT", "S11", 1, "bwd"),
            ("P2_BWD", "wave transmitted through the DUT", "S21", 2, "bwd")]
    for i, (name, what, role, side, dirn) in enumerate(rows):
        y = 125 + i * 205
        d.group(40, y, 1520, 185, f"{name} — {what}", MEAS if role != "reference" else LINE, size=15)
        cy = y + 100
        src = d.box(70, cy - 30, 130, 60, "Source", "port switch", RF, size=14, sub_size=11)
        e1 = (d.box(260, cy - 35, 170, 70, "Coupler", "→ forward" if dirn == "fwd" else "← backward", MEAS, size=15)
              if side == 1 else d.box(275, cy - 22, 140, 44, "−5.6 dB pad", None, LINE, size=13, bold=False, fill=PANEL_2))
        p1 = d.box(480, cy - 25, 100, 50, "Port 1", None, HOST, size=14)
        dut = d.box(640, cy - 35, 220, 70, "DUT", "cable · filter", LINE, size=16, dashed=True)
        p2 = d.box(920, cy - 25, 100, 50, "Port 2", None, HOST, size=14)
        e2 = (d.box(1070, cy - 35, 170, 70, "Coupler", "← backward", MEAS, size=15)
              if side == 2 else d.box(1085, cy - 22, 140, 44, "−5.6 dB pad", None, LINE, size=13, bold=False, fill=PANEL_2))
        term = d.box(1290, cy - 30, 120, 60, "50 Ω", "termination", LINE, size=14, sub_size=11)
        for a, b in ((src, e1), (e1, p1), (p1, dut), (dut, p2), (p2, e2), (e2, term)):
            d.edge(a, b, LINE, end="none", width=2)
        # waves
        wy = cy - 55
        d.line(140, wy, 750, wy, RF, 3)
        if i == 1:
            d.line(750, cy + 52, 345, cy + 52, RF, 3, dashed=True)
        if i == 2:
            d.line(750, wy, 1150, wy, RF, 3)
        cpl = e1 if side == 1 else e2
        rd = d.pill(1430, cy - 18, 110, 36, role, MEAS if role != "reference" else LINE, size=14)
        d.edge(cpl, rd, MEAS, exit=(0.5, 1), entry=(0.5, 1),
               pts=[(345 if side == 1 else 1155, y + 174), (1485, y + 174)], width=1.5, dashed=True)
    d.legend(690, 92, [(RF, "incident / transmitted wave", False), (RF, "reflected wave", True),
                       (MEAS, "coupled sample → reading", True, 1.5)], title="Legend", horizontal=True, col_w=270)
    d.rect(40, 745, 1520, 110, PANEL, MEAS, rounded=True,
           value=f'<span style="font-size:22px"><b>S11 = P1_BWD − P1_FWD&emsp;&emsp;&emsp;S21 = P2_BWD − P1_FWD</b></span><br>'
                 f'<span style="color:{MUTED};font-size:15px">Source on port 2 is the mirror image: '
                 f'S22 = P2_BWD − P2_FWD,  S12 = P1_BWD − P2_FWD.  The pad gives the unused side the same loss as '
                 f'switches + coupler.</span>')
    return d.save()


def resistor(d, x, y, w, h, val, color=HOST):
    return d.box(x, y, w, h, val, None, color, size=13, bold=True, fill=PANEL_2, rounded=False)


def gnd(d, x, y):
    for i, ww in enumerate((40, 26, 12)):
        d.line(x - ww / 2, y + i * 7, x + ww / 2, y + i * 7, LINE, 2, end="none")


def pads():
    d = Diagram("hw-07-pads", 1600, 900)
    d.title("Resistive pads", "Matching 50 Ω to 75 Ω and balancing path losses — chosen values, checked by calculation")
    # min-loss pad
    d.group(40, 120, 740, 480, "50 Ω → 75 Ω minimum-loss pad (each port)", HOST)
    d.box(70, 250, 120, 60, "50 Ω side", "switch", HOST, size=14, sub_size=11)
    d.box(630, 250, 120, 60, "75 Ω side", "BNC", HOST, size=14, sub_size=11)
    d.line(190, 280, 450, 280, LINE, 2, end="none")
    resistor(d, 450, 262, 110, 36, "43.2 Ω")
    d.line(560, 280, 630, 280, LINE, 2, end="none")
    d.line(320, 280, 320, 340, LINE, 2, end="none")
    resistor(d, 284, 340, 72, 90, "86.6 Ω")
    d.line(320, 430, 320, 465, LINE, 2, end="none")
    gnd(d, 320, 465)
    d.html(70, 520, 700, 120,
           f'<span style="color:{TEXT}">Looking in from 50 Ω side: 86.6 ∥ (43.2 + 75) = <b>50.0 Ω</b><br>'
           f'Looking in from 75 Ω side: 43.2 + (86.6 ∥ 50) = <b>74.9 Ω</b><br>'
           f'Insertion loss: <b style="color:{POWER}">5.7 dB</b> (the price of a resistive match)</span>', size=16)
    # T pad
    d.group(820, 120, 740, 480, "5.6 dB T-pad (coupler bank, unused port)", MEAS)
    d.box(850, 250, 110, 60, "50 Ω", None, MEAS, size=14)
    d.box(1420, 250, 110, 60, "50 Ω", None, MEAS, size=14)
    d.line(960, 280, 1010, 280, LINE, 2, end="none")
    resistor(d, 1010, 262, 100, 36, "15.4 Ω", MEAS)
    d.line(1110, 280, 1270, 280, LINE, 2, end="none")
    resistor(d, 1270, 262, 100, 36, "15.4 Ω", MEAS)
    d.line(1370, 280, 1420, 280, LINE, 2, end="none")
    d.line(1190, 280, 1190, 340, LINE, 2, end="none")
    resistor(d, 1154, 340, 72, 90, "71.5 Ω", MEAS)
    d.line(1190, 430, 1190, 465, LINE, 2, end="none")
    gnd(d, 1190, 465)
    d.html(850, 520, 700, 120,
           f'<span style="color:{TEXT}">Input impedance: 15.4 + 71.5 ∥ 65.4 = <b>49.6 Ω</b><br>'
           f'Attenuation: <b style="color:{POWER}">5.6 dB</b> = loss of 2 switches + coupler<br>'
           f'→ both ports see the same path loss, readings stay comparable</span>', size=16)
    d.text(40, 630, 1520, 60, "0.1 % thin-film resistors (0402) keep the match accurate up to 6 GHz.", MUTED, 16)
    return d.save()


def stackup():
    d = Diagram("hw-08-pcb-stackup", 1600, 900)
    d.title("PCB — stackup and RF layout rules", "JLCPCB JLC04161H-7628 · 4 layers · 1.6 mm · ENIG")
    layers = [("L1", "Signal + GND", "RF lines, components", RF, 170, 26),
              ("", "Prepreg 7628 · 0.21 mm", "", None, 196, 40),
              ("L2", "GND", "unbroken reference plane", LINE, 236, 22),
              ("", "Core · 1.07 mm", "", None, 258, 150),
              ("L3", "Power + signal", "rails, slow digital", POWER, 408, 22),
              ("", "Prepreg 7628 · 0.21 mm", "", None, 430, 40),
              ("L4", "Signal + GND", "digital, LEDs", CORE0, 470, 26)]
    for name, t, s, c, y, h in layers:
        if c:
            d.rect(160, y, 520, h, c, value="", opacity=85)
            d.text(60, y, 90, h, name, c, 18, True, "right")
            d.text(700, y - 4, 400, h + 8, f"{t}  ·  {s}", TEXT, 15, False, "left")
        else:
            d.rect(160, y, 520, h, "#3A3F2A", value=t, fcolor=MUTED, size=13)
    # coplanar cross-section
    d.group(1000, 560, 560, 290, "Grounded coplanar waveguide (cross-section)", RF)
    d.rect(1040, 640, 160, 18, LINE)
    d.rect(1240, 640, 80, 18, RF, value="")
    d.rect(1360, 640, 160, 18, LINE)
    d.rect(1040, 658, 480, 50, "#3A3F2A")
    d.rect(1040, 708, 480, 14, LINE)
    d.text(1040, 612, 160, 24, "GND", MUTED, 13)
    d.text(1240, 612, 80, 24, "signal", RF, 13, True)
    d.text(1360, 612, 160, 24, "GND", MUTED, 13)
    d.text(1040, 726, 480, 24, "L2 GND plane", MUTED, 13)
    d.rect(1180, 658, 12, 50, LINE)
    d.rect(1368, 658, 12, 50, LINE)
    d.text(1030, 760, 520, 80, "50 Ω SE (SMA side) · 75 Ω SE (BNC side) · 90 Ω differential (USB)\n"
           "widths from the JLCPCB impedance calculator, vias from Saturn PCB Toolkit", MUTED, 13)
    d.group(40, 560, 920, 290, "Layout rules", LINE)
    d.html(60, 600, 880, 240,
           f'<span style="color:{TEXT}">• Never route RF or USB over a gap in the L2 ground plane<br>'
           f'• GND via next to every signal via → short return path (also for the SPI bus)<br>'
           f'• Via fence along RF lines<br>'
           f'• ESD diodes right at the connectors with a short ground return<br>'
           f'• RF section shielded by the milled aluminium case (pockets per section)</span>', size=17)
    return d.save()


def sheets():
    d = Diagram("hw-09-schematic-sheets", 1600, 900)
    d.title("Schematic — 6 sheets + overview", "Altium project SNA.PrjPcb · sheets connected with harnesses")
    w, h = 300, 190
    s3 = d.box(60, 170, w, h, "3  RF Source", "LMK6C 100 MHz reference\nMAX2871 PLL + loop filter\nLPF bank: 2× RFSW6042,\n"
               "3 LPFs (0.95/1.75/3.5 GHz)", RF, size=18, sub_size=13)
    s4 = d.box(440, 170, w, h, "4  RF Attenuation / Amp", "HMC1119 attenuator\nTRF37A73 amplifier\n3× QPC6324 port select",
               RF, size=18, sub_size=13)
    s5 = d.box(820, 170, w, h, "5  RF Directional Coupler", "SEDC-10-63+ coupler\n6× RFSW6042 SP4T\n5.6 dB pads\n"
               "AD8318 detector", MEAS, size=18, sub_size=13)
    s6 = d.box(1200, 170, w + 40, h, "6  RF Ports", "2× QPC6324 50/75 Ω\n50→75 Ω pads\nSMA + BNC per port\nRF ESD diodes",
               HOST, size=18, sub_size=13)
    s2 = d.box(440, 470, 680, 150, "2  MCU", "RP2350B · W25Q128 flash · 24AA32A EEPROM · 12 MHz crystal\nUSB-C + ESD + "
               "reverse polarity · SWD · LEDs + buttons", CORE0, size=20, sub_size=14)
    s1 = d.box(440, 700, 680, 130, "1  Power", "MIC2253 boost 6 V · 3× LDL212 5 V · AP2114 + 3× NCP702 3.3 V\n"
               "TPS62802 buck 3.3 V (digital) · ferrite beads · 0 Ω bring-up links", POWER, size=20, sub_size=14)
    d.edge(s3, s4, RF)
    d.edge(s4, s5, RF)
    d.edge(s5, s6, RF, start="block")
    for i, s in enumerate((s3, s4, s5, s6)):
        x = (210, 590, 970, 1370)[i]
        d.edge(s2, s, CORE1, exit=((x - 440) / 680 if 440 <= x <= 1120 else (0 if x < 440 else 1), 0) if 440 <= x <= 1120
               else ((0, 0.3) if x < 440 else (1, 0.3)), entry=(0.5, 1),
               pts=[] if 440 <= x <= 1120 else [(x, 515)], dashed=True, width=1.5)
    d.edge(s5, s2, MEAS, exit=(0.3, 1), entry=(0.76, 0), width=1.5)
    d.edge(s1, s2, POWER, dashed=True, width=1.5)
    d.legend(1180, 560, [(RF, "RF signal", False), (CORE1, "control (SPI / GPIO)", True, 1.5),
                         (MEAS, "detector voltage → ADC", False, 1.5), (POWER, "supply rails", True, 1.5)], w=360)
    d.text(1160, 720, 400, 100, "Every rail and every GPIO is RC / ferrite filtered at the sheet boundary.", MUTED, 15,
           align="left")
    return d.save()


if __name__ == "__main__":
    for f in (system_overview, power_tree, digital, pll, coupler_paths, pads, stackup, sheets):
        print(f())
