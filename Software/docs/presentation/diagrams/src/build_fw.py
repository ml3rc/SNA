from dio import *


def dual_core():
    d = Diagram("fw-01-dual-core", 1600, 900)
    d.title("Firmware — two cores, one job each", "RP2350B · bare metal C11 · Pico SDK + TinyUSB · no RTOS")
    pc = d.box(40, 155, 220, 90, "PC client", "USB CDC · JSON lines", HOST)
    d.group(320, 120, 440, 640, "Core 0 — communication", CORE0, size=17)
    c0 = []
    for i, (t, s) in enumerate([("TinyUSB", "CDC line reader / writer"), ("protocol.c", "parse · validate · reply"),
                                ("measurement.c", "ADC counts → dB − calibration"), ("eeprom.c", "I2C · cal curves in RAM"),
                                ("main.c · sdi_test.c", "LEDs · SDI button · watchdog")]):
        c0.append(d.box(360, 180 + 115 * i, 360, 80, t, s, CORE0))
    usb, proto, meas, ee, main = c0
    d.group(1040, 120, 330, 640, "Core 1 — RF hardware", CORE1, size=17)
    loop = d.box(1080, 180, 250, 280, "rf_task loop", "one request\nor one sweep point\nper step", CORE1)
    lay = [d.box(1080, 490 + 70 * i, 250, 56, t, None, CORE1, size=15, fill=PANEL_2)
           for i, t in enumerate(["application  rf_app_*", "state  rf_state_*", "blocks  rf_ctrl_*", "HAL  hal_*"])]
    rf = d.box(1420, 330, 150, 300, "RF\nhardware", "MAX2871\nHMC1119\nTRF37A73\nswitches\nAD8318", RF)
    req = d.box(820, 190, 160, 60, "request queue", "8 entries", LINE, size=14, sub_size=11)
    can = d.box(820, 330, 160, 50, "cancel counter", "atomic", BAD, size=14, sub_size=11)
    rep = d.box(820, 420, 160, 60, "reply queue", "8 entries", LINE, size=14, sub_size=11)
    d.text(800, 130, 200, 40, "rf_task.c\nonly link between cores", MUTED, 13)

    d.edge(pc, usb, HOST, exit=(1, 0.5), entry=(0, 0.25), start="block")
    d.edge(usb, proto, CORE0)
    d.edge(proto, req, CORE0, exit=(1, 0.25), entry=(0, 0.5), pts=[(790, 315), (790, 220)])
    d.edge(req, loop, CORE0, exit=(1, 0.5), entry=(0, 40 / 280))
    d.edge(proto, can, BAD, exit=(1, 0.75), entry=(0, 0.5), dashed=True, width=1.5)
    d.edge(can, loop, BAD, exit=(1, 0.5), entry=(0, 175 / 280), dashed=True, width=1.5)
    d.edge(loop, rep, CORE1, exit=(0, 270 / 280), entry=(1, 0.5), pts=[(1010, 450), (1010, 450)])
    d.edge(rep, meas, CORE1, exit=(0, 0.5), entry=(1, 0.5))
    d.edge(meas, usb, CORE0, exit=(0, 0.5), entry=(0, 0.75), pts=[(340, 450), (340, 240)])
    d.edge(meas, ee, CORE0, width=1.5)
    d.edge(loop, lay[0], CORE1)
    for a, b in zip(lay, lay[1:]):
        d.edge(a, b, CORE1, width=1.5)
    d.edge(lay[3], rf, RF, exit=(1, 0.5), entry=(0, 0.87))
    d.text(40, 790, 1520, 70, "Core 1 owns every RF chip in every mode → no shared state, no locks.   "
           "Core 0 never waits for RF → USB answers status / abort even mid-sweep.", MUTED, 16)
    return d.save()


def layers():
    d = Diagram("fw-02-layers", 1600, 900)
    d.title("Core 1 — four layers", "Each layer only calls the one directly below it")
    rows = [("Application", "rf_app_*", "modes, measurements, boot self-test", "rf_app_sweep_point(f)"),
            ("State", "rf_state_*", "current configuration · safe order of every change", "rf_state_set_freq(f)"),
            ("Blocks", "rf_ctrl_*", "one RF block each: source · coupler · detector · ports", "rf_ctrl_source_set_freq(f)"),
            ("HAL", "hal_*", "one driver per chip: registers and pins", "hal_max2871_set_freq(f)")]
    ids = []
    ex = []
    d.text(1000, 112, 560, 30, "Example: retune to f", MUTED, 15, True, "left")
    for i, (t, p, s, call) in enumerate(rows):
        y = 150 + 140 * i
        ids.append(d.box(60, y, 860, 100, f"{t}   ", None, CORE1, size=22, align="left"))
        d.text(330, y + 14, 560, 36, p, CORE1, 20, True, "left", MONO)
        d.text(330, y + 50, 580, 36, s, MUTED, 15, False, "left")
        ex.append(d.box(1000, y + 20, 560, 60, call, None, LINE, size=16, bold=False, font=MONO, fill=PANEL_2))
    for a, b in zip(ids, ids[1:]):
        d.edge(a, b, CORE1, exit=(0.1, 1), entry=(0.1, 0))
    for a, b in zip(ex, ex[1:]):
        d.edge(a, b, LINE, width=1.5)
    for a, b in zip(ids, ex):
        d.edge(a, b, LINE, end="none", dashed=True, width=1)
    hw = d.box(60, 720, 1500, 70, "Hardware", "MAX2871 · HMC1119 · TRF37A73 · RFSW6042 / QPC6324 switches · AD8318 + ADC",
               RF)
    d.edge(ids[3], hw, RF, exit=(0.1, 1), entry=(0.0573, 0))
    d.edge(ex[3], hw, RF, exit=(0.5, 1), entry=(0.8133, 0))
    d.text(60, 815, 1500, 50, "Safety rules (port switch order, double register write after power-up) live in "
           "one place: the state layer.", MUTED, 16)
    return d.save()


def modes():
    d = Diagram("fw-03-modes", 1600, 900)
    d.title("Modes — the host borrows the RF hardware", "mode_enter only from IDLE · mode_exit (or USB disconnect) "
            "cancels what runs and parks the output")
    idle = d.box(80, 160, 260, 640, "IDLE", "output parked\nsource running", LINE, size=30, sub_size=15)
    ms = [("SWEEP", "sweep — S-parameters or raw paths", "any number of sweeps · status / set / abort allowed mid-sweep", MEAS),
          ("GENERATOR", "gen_start · gen_stop · probe_read", "CW signal on port 1 or 2 · level changes live", RF),
          ("POWER METER", "power_read · power_start / stop", "external power at a port · streamed events", POWER),
          ("MANUAL", "manual lpf / cplr / port / src_freq / src_en", "single blocks for bench diagnostics", CORE1)]
    for i, (t, c, s, col) in enumerate(ms):
        y = 160 + 170 * i
        b = d.box(640, y, 520, 130, t, f"{c}\n{s}", col, size=22, sub_size=14)
        d.edge(idle, b, col, exit=(1, (y + 65 - 160) / 640), entry=(0, 0.5), start="block", width=2.5,
               label="enter ⇄ exit", lsize=13)
    d.group(1220, 160, 340, 470, "Any time, any mode", HOST)
    d.html(1240, 200, 310, 590,
           f'<span style="font-family:{MONO};color:{TEXT};font-size:16px">ping<br>status<br>set  z · atten<br>abort<br>'
           f'<br>cal_set · cal_get<br>cal_clear<br>sdi_test_set<br>sdi_test_get<br><br>src_temp_read<br>bootsel</span>'
           f'<br><br><span style="color:{MUTED};font-size:14px">Only requests that would endanger the hardware are '
           f'refused — everything else is the host\'s call.</span>')
    return d.save()


def sweep_sequence():
    d = Diagram("fw-04-sweep-sequence", 1600, 900)
    d.title("A sweep, end to end", "Points stream while they are measured · core 1 measures point i+1 while core 0 sends point i")
    lanes = [("Host", HOST, 170), ("Core 0", CORE0, 570), ("Core 1", CORE1, 970), ("RF hardware", RF, 1370)]
    for t, c, x in lanes:
        d.box(x - 110, 115, 220, 50, t, None, c, size=18)
        d.line(x, 165, x, 870, c, 1.5, end="none", dashed=True)
    X = {t: x for t, _, x in lanes}

    def msg(a, b, y, text, color, dashed=False, mono=False):
        d.line(X[a] + (6 if X[b] > X[a] else -6), y, X[b] + (-6 if X[b] > X[a] else 6), y, color, 2.5, dashed=dashed)
        x0 = min(X[a], X[b])
        d.text(x0 + 10, y - 30, abs(X[b] - X[a]) - 20, 26, text, color, 14, True, font=MONO if mono else FONT)

    def note(lane, y, text, color):
        d.rect(X[lane] - 150, y - 16, 300, 32, PANEL_2, color, text, TEXT, 13, rounded=True)

    msg("Host", "Core 0", 215, '{"cmd":"sweep", f0, f1, points, …}', HOST, mono=True)
    note("Core 0", 250, "validate · load cal curves into RAM", CORE0)
    msg("Core 0", "Core 1", 305, "SWEEP request  (request queue)", CORE0)
    msg("Core 1", "RF hardware", 355, "park · set z + level · tune f0 · wait lock", CORE1)
    d.group(90, 390, 1420, 300, "", LINE, size=14)
    d.pill(100, 398, 180, 30, "loop · every point", LINE, 13)
    msg("Core 1", "RF hardware", 450, "tune f · route source · read coupler paths", CORE1)
    msg("RF hardware", "Core 1", 500, "ADC medians", MEAS, dashed=True)
    msg("Core 1", "Core 0", 550, "point reply: raw codes  (reply queue)", CORE1)
    note("Core 0", 590, "dB − interpolated calibration", CORE0)
    msg("Core 0", "Host", 650, '{"pt":i, "f":…, "s11_db":-12.3}', CORE0, mono=True)
    msg("Core 1", "RF hardware", 740, "park output", CORE1)
    msg("Core 1", "Core 0", 790, "final reply + status", CORE1)
    msg("Core 0", "Host", 845, '{"ok":true, "done":true}', CORE0, mono=True)
    return d.save()


def sweep_point():
    d = Diagram("fw-05-sweep-point", 1600, 900)
    d.title("One sweep point on core 1", "≈ 2 ms with all four S-parameters")
    row1 = [("Select LPF", "for frequency f"), ("Retune", "MAX2871 registers"), ("Route source", "switch port, then\nrelease attenuator"),
            ("Select path", "first coupler path")]
    b1 = [d.box(60 + 265 * i, 180, 215, 90, t, s, CORE1) for i, (t, s) in enumerate(row1)]
    for a, b in zip(b1, b1[1:]):
        d.edge(a, b, CORE1)
    lock = d.shape(1130, 160, 200, 130, f"rhombus;whiteSpace=wrap;html=1;fillColor={PANEL};strokeColor={POWER};"
                   f"strokeWidth=2;fontColor={TEXT};fontFamily={FONT};fontSize=15;fontStyle=1;",
                   "PLL locked<br>within 20 ms?")
    d.edge(b1[3], lock, CORE1)
    flag = d.box(1390, 185, 170, 80, "Mark point", "locked: false", BAD, size=15)
    d.edge(lock, flag, BAD, label="no")
    settle = d.box(1127, 400, 205, 90, "Detector settle", "200 µs", MEAS)
    d.edge(lock, settle, MEAS, exit=(0.5, 1), entry=(0.5, 0), label="yes")
    d.edge(flag, settle, BAD, exit=(0.5, 1), entry=(1, 0.5), pts=[(1475, 445)], dashed=True, width=1.5)
    ref = d.box(860, 400, 220, 90, "Read reference", "P1_FWD", MEAS)
    meas = d.box(590, 400, 220, 90, "Read DUT side", "P1_BWD → S11\nP2_BWD → S21", MEAS)
    d.edge(settle, ref, MEAS)
    d.edge(ref, meas, MEAS)
    q = d.shape(320, 380, 230, 130, f"rhombus;whiteSpace=wrap;html=1;fillColor={PANEL};strokeColor={LINE};"
                f"strokeWidth=2;fontColor={TEXT};fontFamily={FONT};fontSize=15;fontStyle=1;", "S12 / S22<br>requested?")
    d.edge(meas, q, MEAS)
    p2 = d.box(320, 600, 230, 100, "Source → port 2", "read P2_FWD, P2_BWD,\nP1_BWD", MEAS)
    d.edge(q, p2, LINE, exit=(0.5, 1), entry=(0.5, 0), label="yes")
    done = d.box(40, 600, 230, 100, "Raw ADC medians", "→ reply queue → core 0", CORE0)
    d.edge(q, done, LINE, exit=(0, 0.5), entry=(0.5, 0), pts=[(155, 445)], label="no", lpos=(-0.4, 0))
    d.edge(p2, done, MEAS)
    d.group(775, 590, 785, 150, "Every reading", LINE, size=14)
    steps = ["select coupler path", "settle 100 µs", "16 ADC samples", "median"]
    sb = [d.box(800 + 190 * i, 640, 165, 60, s, None, MEAS, size=14, bold=False, fill=PANEL_2) for i, s in enumerate(steps)]
    for a, b in zip(sb, sb[1:]):
        d.edge(a, b, MEAS, width=1.5)
    d.text(60, 790, 1500, 50, "The output stays routed between points; it is parked only when the sweep ends.", MUTED, 16)
    return d.save()


def timing():
    d = Diagram("fw-06-point-timing", 1600, 900)
    d.title("Where the time goes", "Estimated per point, all four S-parameters (from profiling of the earlier firmware)")
    x0, s = 230, 600  # px per ms
    d.text(40, 160, 180, 40, "Core 1", CORE1, 18, True, "right")
    d.text(40, 240, 180, 40, "Core 0", CORE0, 18, True, "right")
    segs = [(0, 0.43, "Retune\n0.43 ms", RF), (0.43, 1.13, "PLL lock + stable LD\n0.7 ms", POWER),
            (1.13, 1.33, "settle\n0.2", MEAS), (1.33, 2.03, "6 path reads\n0.7 ms", MEAS)]
    for a, b, t, c in segs:
        d.rect(x0 + a * s, 150, (b - a) * s - 4, 60, c, value=t, fcolor=BG, size=13, bold=True, rounded=True)
    d.rect(x0, 230, 0.7 * s, 60, CORE0, value="previous point: dB · cal · JSON · USB", fcolor=BG, size=13, bold=True,
           rounded=True)
    for i in range(5):
        x = x0 + i * 0.5 * s
        d.line(x, 305, x, 315, LINE, 1.5, end="none")
        d.text(x - 40, 318, 80, 22, f"{i * 0.5:.1f} ms", MUTED, 12)
    d.line(x0, 305, x0 + 2.1 * s, 305, LINE, 1.5, end="none")

    d.text(40, 390, 1500, 30, "Pipelined over several points", TEXT, 20, True, "left")
    x0, s = 230, 190
    d.text(40, 450, 180, 40, "Core 1", CORE1, 18, True, "right")
    d.text(40, 520, 180, 40, "Core 0", CORE0, 18, True, "right")
    for i in range(3):
        d.rect(x0 + i * 2.03 * s, 440, 2.03 * s - 4, 56, CORE1, value=f"measure point {i}", fcolor=BG, size=14,
               bold=True, rounded=True)
        d.rect(x0 + (i + 1) * 2.03 * s, 512, 0.7 * s, 56, CORE0, value=f"send {i}", fcolor=BG, size=14, bold=True,
               rounded=True)
    d.rect(x0 + 3 * 2.03 * s, 440, 0.9 * s, 56, CORE1, value="…", fcolor=BG, size=14, bold=True, rounded=True,
           opacity=50)
    for i in range(8):
        x = x0 + i * s
        d.line(x, 585, x, 595, LINE, 1.5, end="none")
        d.text(x - 30, 598, 60, 22, f"{i} ms", MUTED, 12)
    d.line(x0, 585, x0 + 7 * s, 585, LINE, 1.5, end="none")
    d.group(60, 680, 1500, 110, "Speed levers (next steps)", LINE)
    d.html(80, 715, 1460, 130,
           f'<span style="color:{TEXT};font-size:16px">1. Shorter LD stability wait (biggest fixed cost)&emsp;&emsp;'
           f'2. Characterize settle times on the bench<br>3. Write only changed MAX2871 registers / faster SPI&emsp;&emsp;'
           f'4. Less JSON per point</span>')
    return d.save()


def source_states():
    d = Diagram("fw-07-source-states", 1600, 900)
    d.title("RF source — OFF · PARKED · ON", "Every change goes through rf_state_set_source() so the order is always safe")
    off = d.box(80, 260, 280, 150, "OFF", "synth + amp powered down", LINE, size=28, sub_size=14)
    park = d.box(660, 260, 280, 150, "PARKED", "running · output terminated", POWER, size=28, sub_size=14)
    on = d.box(1240, 260, 280, 150, "ON", "routed to port 1 or 2", RF, size=28, sub_size=14)
    d.edge(park, on, RF, exit=(1, 0.3), entry=(0, 0.3), width=2.5)
    d.edge(on, park, POWER, exit=(0, 0.7), entry=(1, 0.7), width=2.5)
    d.html(950, 160, 290, 90, f'<span style="color:{RF}"><b>1.</b> port switch → port<br><b>2.</b> release attenuator</span>',
           size=15, valign="bottom")
    d.html(950, 420, 290, 90, f'<span style="color:{POWER}"><b>1.</b> attenuator → 127 (max)<br><b>2.</b> switch → 50 Ω</span>',
           size=15)
    d.edge(park, off, LINE, exit=(0, 0.3), entry=(1, 0.3), width=2.5)
    d.edge(off, park, LINE, exit=(1, 0.7), entry=(0, 0.7), width=2.5)
    d.html(370, 170, 290, 80, f'<span style="color:{MUTED}">manual src_en: false</span>', size=15, valign="bottom")
    d.html(370, 420, 290, 90, f'<span style="color:{TEXT}">power up → registers written<br><b>twice, ≥ 20 ms apart</b></span>',
           size=15)
    d.edge(on, off, LINE, exit=(0.5, 0), entry=(0.5, 0), pts=[(1380, 140), (220, 140)], dashed=True, width=1.5,
           label="park first, then power down", lpos=(0, 0))
    d.rect(80, 580, 1440, 130, PANEL, BAD, rounded=True,
           value=f'<span style="font-size:20px;color:{BAD}"><b>Why the order matters</b></span><br>'
                 f'<span style="font-size:16px">The amplifier delivers about 12–15 dBm at 0 dB attenuation. Switching to the '
                 f'termination first would dump that into the switch\'s 50 Ω resistor.<br>So: attenuate fully before '
                 f'terminating, and route before releasing the attenuator.</span>')
    d.text(80, 750, 1440, 60, "Between sweeps the source stays PARKED and running: stable temperature, "
           "fast retune, no repeated power-up.", MUTED, 16)
    return d.save()


def calibration():
    d = Diagram("fw-08-calibration", 1600, 900)
    d.title("Calibration", "The client does the math · the device stores, interpolates and subtracts")
    d.group(40, 130, 700, 560, "Client — calibration wizard (per impedance)", HOST)
    steps = [("Open", "port 1"), ("Short", "port 1"), ("Open", "port 2"), ("Short", "port 2"), ("Thru", "port 1 ↔ 2")]
    sb = [d.box(70, 185 + 95 * i, 170, 70, t, s, HOST, size=16) for i, (t, s) in enumerate(steps)]
    curves = [d.box(330, 215, 370, 80, "S11 curve", "average of Open + Short (port 1)", HOST),
              d.box(330, 405, 370, 80, "S22 curve", "average of Open + Short (port 2)", HOST),
              d.box(330, 565, 370, 80, "S21 + S12 curves", "Thru reading directly", HOST)]
    d.edge(sb[0], curves[0], LINE, exit=(1, 0.5), entry=(0, 0.3), width=1.5)
    d.edge(sb[1], curves[0], LINE, exit=(1, 0.5), entry=(0, 0.7), pts=[(285, 315), (285, 271)], width=1.5)
    d.edge(sb[2], curves[1], LINE, exit=(1, 0.5), entry=(0, 0.3), pts=[(285, 410), (285, 429)], width=1.5)
    d.edge(sb[3], curves[1], LINE, exit=(1, 0.5), entry=(0, 0.7), pts=[(285, 505), (285, 461)], width=1.5)
    d.edge(sb[4], curves[2], LINE, exit=(1, 0.5), entry=(0, 0.5), width=1.5)
    d.group(860, 130, 700, 340, "Device EEPROM — 8 curves", POWER)
    for r, z in enumerate(("50 Ω", "75 Ω")):
        d.text(880, 205 + 120 * r, 70, 40, z, POWER, 16, True)
        for c, sp in enumerate(("S11", "S21", "S12", "S22")):
            d.box(960 + 145 * c, 190 + 120 * r, 130, 70, f"{sp.lower()}_{z[:2]}", "240 points", POWER, size=15,
                  sub_size=11, fill=PANEL_2)
    d.text(880, 430, 660, 30, "one shared frequency grid · 0.01 dB per step (int16)", MUTED, 13)
    up = d.box(860, 520, 700, 70, "cal_set over USB", "upload the computed curve, not the raw sweeps", HOST)
    for c, cy in zip(curves, (255, 445, 605)):
        d.edge(c, up, HOST, exit=(1, 0.5), entry=(0, 0.5), pts=[(800, cy), (800, 555)], width=1.5)
    d.line(1210, 520, 1210, 472, POWER, 2.5)
    d.rect(40, 730, 1520, 120, PANEL, MEAS, rounded=True,
           value=f'<span style="font-size:22px"><b>every point:&emsp;S_cal(f) = S_raw(f) − curve(f)</b></span><br>'
                 f'<span style="color:{MUTED};font-size:15px">curve(f) linearly interpolated between the two nearest of the '
                 f'240 stored points · outside the stored range the point is sent with "cal": false</span>')
    return d.save()


def eeprom():
    d = Diagram("fw-09-eeprom-map", 1600, 900)
    d.title("EEPROM map — 24AA32A, 4096 bytes", "Fits because the 8 curves share one frequency grid and store only int16 dB values")
    x0, w = 80, 1440
    sc = w / 4096
    parts = [(0, 32, "header", LINE), (32, 24, "grid", LINE), (56, 188, "SDI", HOST)]
    cal = ["s11_50", "s21_50", "s12_50", "s22_50", "s11_75", "s21_75", "s12_75", "s22_75"]
    d.text(x0, 140, 600, 30, "Whole device, to scale", TEXT, 18, True, "left")
    for a, n, t, c in parts:
        d.rect(x0 + a * sc, 180, n * sc, 80, c, BG)
    for i, t in enumerate(cal):
        a = 244 + 480 * i
        d.rect(x0 + a * sc, 180, 480 * sc, 80, POWER, BG, t, BG, 14, True)
    d.rect(x0 + 4084 * sc, 180, 12 * sc, 80, PANEL_2, BG)
    for addr in (0, 0x0F4, 0x400, 0x800, 0xC00, 0xFFF):
        x = x0 + addr * sc
        d.line(x, 265, x, 275, LINE, 1.5, end="none")
        d.text(x - 40, 278, 80, 22, f"0x{addr:03X}", MUTED, 12, font=MONO)
    d.text(x0, 330, 800, 30, "Zoom: first 244 bytes (0x000 – 0x0F3)", TEXT, 18, True, "left")
    zs = w / 244
    info = [("Header", "32 B", "magic · format · hw rev · serial"), ("Cal grid", "24 B", "f0 · f1 · 240 points · valid mask"),
            ("SDI test", "188 B", "up to 15 points: f · threshold · S-param · &lt; or &gt;")]
    for (a, n, _, c), (t, sz, s) in zip(parts, info):
        d.rect(x0 + a * zs, 370, n * zs - 3, 110, PANEL, c, rounded=True,
               value=f'<b>{t}</b> <span style="color:{MUTED}">{sz}</span>' + (f'<br><span style="font-size:12px;color:{MUTED}">{s}</span>' if n > 30 else ""),
               size=15)
    d.group(80, 560, 1440, 190, "Why it fits", POWER)
    d.html(100, 595, 1400, 150,
           f'<span style="font-size:18px;color:{TEXT}">8 curves × 240 points × 2 bytes = <b>3840 B</b><br>'
           f'Frequencies are not stored per point: point k sits at f0 + (f1 − f0)·k / 239<br>'
           f'Values are int16 in 0.01 dB → ±327 dB range, 0.01 dB resolution<br>'
           f'Total used: <b style="color:{POWER}">4084 of 4096 bytes</b></span>')
    return d.save()


def boot():
    d = Diagram("fw-10-boot", 1600, 900)
    d.title("Boot and LEDs", "Self-test at power-up · result on the LEDs and in status.init_errors · LEDs and button "
            "sit on the PCB, seen through holes in the case")
    d.text(40, 165, 150, 40, "Core 0", CORE0, 18, True, "right")
    d.text(40, 345, 150, 40, "Core 1", CORE1, 18, True, "right")
    c0 = [("GPIO init", "BOOTED fast blink"), ("Launch core 1", ""), ("EEPROM init", "USB start"),
          ("Wait for ready", "+ error bits"), ("Heartbeat", "or red error code")]
    xs = [220, 470, 760, 1050, 1320]
    b0 = [d.box(x, 150, 220 if i != 4 else 240, 70, t, s or None, CORE0, size=15) for i, (x, (t, s)) in enumerate(zip(xs, c0))]
    for a, b in zip(b0, b0[1:]):
        d.edge(a, b, CORE0)
    c1 = d.box(560, 320, 640, 90, "RF init", "source off 1 s → power up → tune 1 GHz (double write) → lock wait ≤ 2 s "
               "→ parked detector check", CORE1, size=16, sub_size=13)
    d.edge(b0[1], c1, CORE1, exit=(0.5, 1), entry=(0.03125, 0), label="launch")
    d.edge(c1, b0[3], CORE1, exit=(0.9375, 0), entry=(0.5, 1), label="ready + error bits")
    d.group(40, 470, 620, 380, "Blink codes (red LED)", BAD)
    for i, (n, t) in enumerate([(1, "EEPROM not readable / writable"), (2, "synthesizer did not lock"),
                                (3, "detector reading implausible"), (4, "USB")]):
        y = 520 + 75 * i
        for k in range(n):
            d.circle(70 + 30 * k, y + 12, 18, BAD, fill=BAD)
        d.text(220, y, 420, 40, t, TEXT, 16, align="left")
    d.group(700, 470, 860, 380, "LEDs while running", LINE)
    rows = [("BOOTED", "1 short pulse per second = firmware alive", MEAS),
            ("READY / BUSY", "brightness = load of both cores (PWM)", CORE0),
            ("PORT 1 OUT · 50 Ω", "set by core 1 together with the switches", RF),
            ("SDI PASS / FAIL", "one green / red blink per test point", POWER)]
    for i, (t, s, c) in enumerate(rows):
        y = 520 + 75 * i
        d.pill(730, y + 5, 210, 34, t, c, 13)
        d.text(960, y, 580, 44, s, TEXT, 16, align="left")
    return d.save()


def api_messages():
    d = Diagram("api-01-messages", 1600, 900)
    d.title("API — JSON lines over USB", "USB CDC (virtual COM port) · one JSON object per line · no driver needed")
    pc = d.box(40, 140, 220, 690, "PC", "SNA Client\nor any serial terminal", HOST, size=26)
    dev = d.box(1340, 140, 220, 690, "SNA", "firmware 0.4\nprotocol 3", CORE0, size=26)
    kinds = [("Request", '{"cmd":"sweep","id":4,"f0":50000000,"f1":6000000000,"points":201, …}', HOST, 1),
             ("Reply", '{"id":4,"ok":true, …}   or   {"id":4,"ok":false,"error":"busy"}', CORE0, -1),
             ("Stream", '{"id":4,"pt":0,"f":50000000,"s11_db":-12.3,"locked":true}  …  {"id":4,"ok":true,"done":true}', MEAS, -1),
             ("Event", '{"event":"power","port":2,"dbm":-9.6}     {"event":"sdi_test_result", …}', POWER, -1)]
    for i, (t, code, c, dirn) in enumerate(kinds):
        y = 180 + 170 * i
        d.text(300, y - 10, 300, 32, t, c, 20, True, "left")
        d.rect(300, y + 26, 1000, 52, PANEL, c, code, TEXT, 14, rounded=True, font=MONO, align="left")
        if dirn > 0:
            d.line(300, y + 100, 1300, y + 100, c, 3)
        else:
            d.line(1300, y + 100, 300, y + 100, c, 3)
    d.text(40, 845, 1520, 40, "Every request gets exactly one final reply with the same id · frequencies are integer Hz "
           "· errors carry a code (and the bad field)", MUTED, 15)
    return d.save()


def api_commands():
    d = Diagram("api-02-command-map", 1600, 900)
    d.title("API — commands by mode", "Enter a mode, use its commands, leave it again")
    d.group(40, 130, 1520, 170, "Any mode", HOST)
    anyc = ["ping", "status", "set", "abort", "mode_enter", "mode_exit", "cal_set", "cal_get", "cal_clear",
            "sdi_test_set", "sdi_test_get", "src_temp_read", "bootsel"]
    for i, c in enumerate(anyc):
        r, k = divmod(i, 7)
        d.pill(70 + 210 * k, 175 + 60 * r, 190, 40, c, HOST, 15)
    groups = [("sweep", MEAS, [("sweep", "S11 · S21 · S12 · S22"), ("sweep (raw)", "p1_fwd … p2_bwd")]),
              ("generator", RF, [("gen_start", "port · z · atten · f"), ("gen_stop", ""), ("probe_read", "detector on paths")]),
              ("power_meter", POWER, [("power_read", "one reading"), ("power_start", "→ power events"), ("power_stop", "")]),
              ("manual", CORE1, [("manual", "lpf · cplr · port\nsrc_freq · src_en"), ("probe_read", "detector on paths")])]
    for g, (name, c, cmds) in enumerate(groups):
        x = 40 + 385 * g
        d.group(x, 340, 365, 500, name, c, size=18)
        for i, (cmd, s) in enumerate(cmds):
            d.box(x + 25, 395 + 125 * i, 315, 100, cmd, s or None, c, size=18, font=MONO, sub_size=13)
    return d.save()


def client():
    d = Diagram("sw-01-client-architecture", 1600, 900)
    d.title("Client — Kotlin Multiplatform", "One code base for Windows/Linux desktop (JVM) and Android · Compose UI")
    rows = [("ui/", "one screen per tab", ["Sweep", "Calibration", "SDI test", "Generator", "Power meter", "Raw sweep",
                                           "Diagnostics", "Console"], HOST),
            ("session/", "state per connection\nsurvives tab changes",["AppState", "Session", "DeviceMonitor",
                                                                         "ToolActivity", "SweepState", "CalibrationState", "…"], CORE0),
            ("client/", "SnaClient · coroutines", ["id matching", "mode ownership", "backpressure", "TrafficLog"], CORE1),
            ("transport", "SerialTransport", ["desktop: jSerialComm", "Android: usb-serial-for-android"], LINE)]
    prev = None
    for i, (t, s, items, c) in enumerate(rows):
        y = 140 + 160 * i
        g = d.box(40, y, 300, 120, t, s, c, size=22, font=MONO)
        w = (1180 - 20 * (len(items) - 1)) / len(items)
        for k, it in enumerate(items):
            d.box(380 + k * (w + 20), y + 25, w, 70, it, None, c, size=14, fill=PANEL_2)
        if prev:
            d.edge(prev, g, c)
        prev = g
    dev = d.box(40, 790, 1520, 60, "USB CDC → SNA device", None, RF, size=18)
    d.edge(prev, dev, RF, exit=(0.5, 1), entry=(0.0987, 0))
    return d.save()


def diamond(d, x, y, w, h, text, color, size=15):
    return d.shape(x, y, w, h, f"rhombus;whiteSpace=wrap;html=1;fillColor={PANEL};strokeColor={color};strokeWidth=2;"
                               f"fontColor={TEXT};fontFamily={FONT};fontSize={size};fontStyle=1;",
                   text.replace("\n", "<br>"))


def request_path():
    d = Diagram("fw-11-request-path", 1600, 900)
    d.title("A request through the firmware", "Core 0 answers what it can at once · RF work goes to core 1 and comes "
            "back as exactly one reply")
    line = d.box(40, 200, 180, 80, "JSON line", "from USB", HOST)
    parse = d.box(270, 200, 230, 80, "parse + validate", "core 0 · protocol.c", CORE0)
    now = d.box(580, 120, 300, 80, "reply at once", "EEPROM commands · ping · errors", CORE0)
    pend = d.box(580, 270, 220, 80, "pending table", "8 slots · id → request", CORE0)
    q = d.box(860, 280, 190, 60, "request queue", None, LINE, size=15)
    c1 = d.box(1110, 255, 260, 110, "core 1", "mode check → rf_app_*\nruns the hardware", CORE1)
    rq = d.box(1110, 430, 260, 60, "reply queue", "sweep points stream first", LINE, size=15, sub_size=11)
    conv = d.box(760, 425, 290, 70, "convert", "raw codes → dB − calibration", CORE0)
    js = d.box(430, 425, 270, 70, "JSON reply", "same id · to USB", HOST)
    d.edge(line, parse, HOST)
    d.edge(parse, now, CORE0, exit=(1, 0.25), entry=(0, 0.5), pts=[(540, 220), (540, 160)])
    d.edge(parse, pend, CORE0, exit=(1, 0.75), entry=(0, 0.5), pts=[(540, 260), (540, 310)])
    d.text(890, 140, 300, 40, "no hardware involved", MUTED, 14, align="left")
    d.text(580, 354, 220, 26, "RF commands", CORE0, 14, True, "left")
    d.edge(pend, q, CORE0)
    d.edge(q, c1, CORE0)
    d.edge(c1, rq, CORE1)
    d.edge(rq, conv, CORE1)
    d.edge(conv, js, CORE0)

    d.group(40, 560, 1520, 250, "While a sweep runs", MEAS, size=16)
    r = d.box(80, 625, 170, 70, "request", None, LINE, size=16)
    dec = diamond(d, 310, 600, 260, 120, "allowed\nmid-sweep?", MEAS)
    ok = d.box(660, 615, 330, 60, "handled between two points", None, MEAS, size=15)
    busy = d.box(660, 720, 330, 60, "error: busy", None, BAD, size=15)
    d.edge(r, dec, LINE)
    d.edge(dec, ok, MEAS, exit=(1, 0.5), entry=(0, 0.5), pts=[(615, 660), (615, 645)])
    d.edge(dec, busy, BAD, exit=(0.5, 1), entry=(0, 0.5), pts=[(440, 750)])
    d.legend(1040, 615, [(MEAS, "yes", False), (BAD, "no", False)], title=None, w=120, row=28)
    d.html(1190, 605, 360, 190,
           f'<span style="color:{MUTED};font-size:14px">Allowed mid-sweep:</span><br>'
           f'<span style="font-family:{MONO};color:{TEXT};font-size:16px">status · ping · set<br>abort · mode_exit<br>'
           f'src_temp_read</span>')
    d.text(40, 830, 1520, 40, "abort, mode_exit, a USB disconnect or a 2 s host stall cancel everything queued before "
           "them (cancel counter).", MUTED, 15)
    return d.save()


def raw_to_db():
    d = Diagram("fw-12-raw-to-db", 1600, 900)
    d.title("Raw codes → calibrated dB", "Done on core 0 for every point, while core 1 already measures the next one")
    raw = d.box(40, 220, 230, 100, "ADC medians", "reference path\nmeasured path", CORE1)
    db = d.box(330, 210, 300, 120, "ratio in dB", "(measured − reference) / slope\nslope ≈ −30.4 counts / dB", MEAS)
    dec = diamond(d, 700, 195, 260, 150, "cal curve\ncovers f?", POWER)
    sub = d.box(1040, 150, 260, 90, "− interpolated curve", "between the 2 nearest\nof 240 stored points", POWER)
    nc = d.box(1040, 300, 260, 90, "uncorrected", "sent with \"cal\": false", BAD)
    js = d.box(1360, 220, 200, 100, "JSON point", "{pt, f, s11_db, …}", HOST)
    d.edge(raw, db, CORE1)
    d.edge(db, dec, MEAS)
    d.edge(dec, sub, MEAS, exit=(1, 0.5), entry=(0, 0.5), pts=[(1000, 270), (1000, 195)])
    d.edge(dec, nc, BAD, exit=(1, 0.5), entry=(0, 0.5), pts=[(1000, 270), (1000, 345)])
    d.edge(sub, js, HOST, exit=(1, 0.5), entry=(0, 0.3), pts=[(1330, 195), (1330, 250)])
    d.edge(nc, js, HOST, exit=(1, 0.5), entry=(0, 0.7), pts=[(1330, 345), (1330, 290)])
    d.legend(700, 380, [(MEAS, "yes", False), (BAD, "no", False)], title=None, w=120, row=28)
    d.group(40, 500, 1520, 300, "Frequency grid — computed, never sent", LINE, size=16)
    d.rect(80, 560, 700, 110, PANEL, LINE, rounded=True,
           value=f'<span style="color:{MUTED};font-size:14px">linear</span><br>'
                 f'<span style="font-family:{MONO};font-size:18px">f(i) = f0 + (f1 − f0) · i / (n − 1)</span><br>'
                 f'<span style="color:{MUTED};font-size:13px">exact integer Hz</span>')
    d.rect(820, 560, 700, 110, PANEL, LINE, rounded=True,
           value=f'<span style="color:{MUTED};font-size:14px">logarithmic</span><br>'
                 f'<span style="font-family:{MONO};font-size:18px">f(i) = exp(ln f0 + (ln f1 − ln f0) · i / (n − 1))</span>'
                 f'<br><span style="color:{MUTED};font-size:13px">rounded to the Hz</span>')
    d.text(80, 700, 1440, 60, "Firmware and client compute the same grid, so the client can draw placeholders before "
           "the data arrives; missing points stay gaps.", TEXT, 16)
    return d.save()


def sdi_watchdog():
    d = Diagram("fw-13-sdi-button-watchdog", 1600, 900)
    d.title("SDI button and watchdog", "The button test works without a PC · the watchdog resets a stuck device")
    d.group(40, 130, 960, 640, "SDI test button (on the PCB, pressed through the case)", MEAS, size=16)
    idle = d.box(70, 400, 160, 90, "IDLE", None, LINE, size=22)
    org = d.box(370, 180, 300, 70, "short orange blink", "no points stored · nothing measured", POWER, size=15)
    red = d.box(370, 290, 300, 70, "short red blink", "not idle (a host mode is active)", BAD, size=15)
    swp = d.box(370, 410, 300, 80, "SWEEPING", "sweep mode · atten 0 · current z", MEAS)
    show = d.box(730, 410, 240, 80, "SHOWING", "green / red blink\nper test point", MEAS)
    d.edge(idle, org, POWER, exit=(0.5, 0), entry=(0, 0.5), pts=[(150, 215)])
    d.edge(idle, red, BAD, exit=(0.75, 0), entry=(0, 0.5), pts=[(190, 325)])
    d.edge(idle, swp, MEAS, exit=(1, 0.5), entry=(0, 0.5))
    d.edge(swp, show, MEAS)
    d.edge(show, idle, LINE, exit=(0.5, 1), entry=(0.5, 1), pts=[(850, 610), (150, 610)], dashed=True)
    d.edge(swp, idle, BAD, exit=(0.5, 1), entry=(0.75, 1), pts=[(520, 560), (190, 560)], dashed=True, width=1.5)
    d.text(250, 418, 110, 24, "press", MEAS, 13, True)
    d.text(672, 418, 56, 24, "done", MEAS, 13, True)
    d.text(560, 580, 320, 26, "result shown → back to idle", MUTED, 13, align="left")
    d.text(210, 530, 320, 26, "abort / mode_exit from the host", BAD, 13, align="left")
    d.text(70, 650, 900, 100, "Test points (f, S-parameter, threshold, < or >) come from sdi_test_set and live in EEPROM.\n"
           "With a host connected the result is also sent as an sdi_test_result event.", TEXT, 15, align="left")

    d.group(1040, 130, 520, 640, "Watchdog", POWER, size=16)
    c0 = d.box(1070, 200, 220, 80, "core 0 main loop", "update_heartbeat()", CORE0, size=15)
    c1 = d.box(1310, 200, 220, 80, "core 1 step", "must finish < 6 s", CORE1, size=15)
    wd = d.box(1180, 370, 240, 90, "watchdog", "RP2350 hardware timer", POWER)
    rst = d.box(1180, 560, 240, 90, "reset", "device re-enumerates on USB", BAD)
    d.edge(c0, wd, CORE0, exit=(0.5, 1), entry=(0.25, 0))
    d.edge(c1, wd, CORE1, exit=(0.5, 1), entry=(0.75, 0))
    d.edge(wd, rst, BAD, label="missed")
    d.text(1070, 670, 470, 80, "Every blocking wait is bounded (lock waits 2 s), so a healthy device never "
           "trips it.", MUTED, 14, align="left")
    return d.save()


def api_sequences():
    d = Diagram("api-03-sequences", 1600, 900)
    d.title("API — two typical sessions", "The client borrows a mode, uses it, gives it back")

    def lanes(xc, xd, y0, y1):
        for t, c, x in (("Client", HOST, xc), ("Device", CORE0, xd)):
            d.box(x - 100, y0, 200, 46, t, None, c, size=17)
            d.line(x, y0 + 46, x, y1, c, 1.5, end="none", dashed=True)

    def msg(xa, xb, y, text, color, dashed=False):
        d.line(xa + (6 if xb > xa else -6), y, xb + (-6 if xb > xa else 6), y, color, 2.5, dashed=dashed)
        d.text(min(xa, xb) + 10, y - 28, abs(xb - xa) - 20, 24, text, color, 13, True, font=MONO)

    def note(x0, x1, y, text):
        d.rect(x0, y - 15, x1 - x0, 30, PANEL_2, LINE, text, MUTED, 13, rounded=True)

    # left: sweep session
    a, b = 170, 650
    d.text(40, 110, 740, 30, "Sweep", MEAS, 20, True, "left")
    lanes(a, b, 150, 840)
    msg(a, b, 245, "mode_enter sweep", HOST)
    msg(b, a, 295, "ok · mode sweep", CORE0, True)
    msg(a, b, 355, "sweep f0 f1 points params", HOST)
    d.group(70, 380, 680, 110, "", LINE)
    d.pill(80, 388, 130, 26, "every point", LINE, 12)
    msg(b, a, 450, "{pt, f, s11_db, s21_db, locked}", MEAS, True)
    msg(b, a, 530, "{ok, done}", CORE0, True)
    note(90, 730, 620, "more sweeps … status / set / abort allowed meanwhile")
    msg(a, b, 700, "mode_exit", HOST)
    msg(b, a, 750, "ok · mode idle", CORE0, True)

    # right: generator, then power meter
    a, b = 950, 1430
    d.text(820, 110, 740, 30, "Generator, then power meter", RF, 20, True, "left")
    lanes(a, b, 150, 840)
    msg(a, b, 235, "mode_enter generator", HOST)
    msg(a, b, 285, "gen_start port 1, f, atten", HOST)
    note(1180, 1550, 320, "park → tune → wait lock → route")
    msg(b, a, 370, "ok", CORE0, True)
    msg(a, b, 420, "set atten 40", HOST)
    msg(b, a, 470, "ok (level changes live)", CORE0, True)
    msg(a, b, 520, "mode_exit", HOST)
    msg(a, b, 580, "mode_enter power_meter", HOST)
    msg(a, b, 630, "power_start port 2", HOST)
    d.group(850, 655, 680, 100, "", LINE)
    d.pill(860, 663, 150, 26, "every interval", LINE, 12)
    msg(b, a, 735, "{event: power, dbm}", POWER, True)
    msg(a, b, 810, "mode_exit", HOST)
    d.legend(40, 870, [(HOST, "request", False), (CORE0, "reply", True), (MEAS, "streamed point", True),
                       (POWER, "event", True)], title=None, horizontal=True, col_w=220)
    return d.save()


def api_errors():
    d = Diagram("api-04-errors", 1600, 900)
    d.title("API — how a request can fail", "Checked in this order · the first failing check is the error code")
    req = d.box(40, 250, 150, 80, "request", None, HOST, size=17)
    checks = ["valid JSON +\nknown cmd?", "arguments\nok?", "right mode,\nnot busy?", "hardware\nok?"]
    errs = [("bad_request", "unknown_cmd"), ("bad_args", "+ field"), ("wrong_mode · mode_active", "busy · output_active"),
            ("source_not_locked", "tune_failed · storage_error · aborted")]
    prev = req
    for i, (c, (e1, e2)) in enumerate(zip(checks, errs)):
        x = 230 + 280 * i
        dm = diamond(d, x, 220, 230, 140, c, LINE, 14)
        d.edge(prev, dm, MEAS if i else HOST)
        eb = d.box(x - 15, 470, 260, 90, e1, e2, BAD, size=15, sub_size=12, font=MONO)
        d.edge(dm, eb, BAD, exit=(0.5, 1), entry=(0.5, 0))
        prev = dm
    ok = d.box(1370, 250, 190, 80, "ok: true", "+ result fields", MEAS, size=17, font=MONO)
    d.edge(prev, ok, MEAS)
    d.legend(40, 420, [(MEAS, "yes", False), (BAD, "no", False)], title=None, w=140, row=28)
    d.rect(40, 640, 1520, 130, PANEL, BAD, rounded=True,
           value=f'<span style="color:{MUTED};font-size:15px">Error reply — same id, a code, and the offending field '
                 f'for bad_args</span><br><br><span style="font-family:{MONO};font-size:20px">'
                 f'{{"id":4,"ok":false,"error":"bad_args","field":"points"}}</span>')
    d.text(40, 800, 1520, 40, "Every code with its meaning: PROTOCOL.md, section Errors.", MUTED, 15)
    return d.save()


if __name__ == "__main__":
    for f in (dual_core, layers, modes, sweep_sequence, sweep_point, timing, source_states, calibration, eeprom, boot,
              api_messages, api_commands, client, request_path, raw_to_db, sdi_watchdog, api_sequences, api_errors):
        print(f())
