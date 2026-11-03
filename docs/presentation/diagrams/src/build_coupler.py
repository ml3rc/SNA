"""Initial source for hw-10. Edit the generated draw.io file after creation."""
from dio import *


def transformer_principle():
    d = Diagram("hw-10-transformer-coupler", 1600, 900)
    d.title("Transformer directional coupler", "SEDC-10-63+ · simplified functional principle, not an internal netlist")
    d.text(60, 145, 100, 40, "IN", RF, 24, True)
    d.line(180, 165, 1420, 165, RF, width=4)
    d.text(1430, 145, 110, 40, "OUT", RF, 24, True)
    d.text(550, 195, 500, 35, "I positive from IN to OUT · Z₀ = 50 Ω", MUTED, 19)
    v = d.box(270, 295, 340, 110, "Voltage transformer", "Voltage sample V", CORE0, size=23, sub_size=20)
    i = d.box(990, 295, 340, 110, "Current transformer", "Scaled current sample Z₀ I", CORE1, size=23, sub_size=20)
    d.line(440, 165, 440, 295, CORE0, dashed=True)
    d.line(1160, 165, 1160, 295, CORE1, dashed=True)
    cpl = d.box(270, 505, 340, 100, "CPL → detector", "V + Z₀ I", MEAS, size=23, sub_size=23)
    rejected = d.box(990, 505, 340, 100, "Internal termination", "V − Z₀ I", MUTED, size=23, sub_size=23)
    d.edge(v, cpl, CORE0, dashed=True, exit=(0.2, 1), entry=(0.2, 0))
    d.edge(i, rejected, CORE1, dashed=True, exit=(0.8, 1), entry=(0.8, 0))
    d.edge(v, rejected, CORE0, dashed=True, exit=(0.8, 1), entry=(0.2, 0),
           pts=((542, 445), (1058, 445)))
    d.edge(i, cpl, CORE1, dashed=True, exit=(0.4, 1), entry=(0.6, 0),
           pts=((1126, 475), (474, 475)))
    d.line(1160, 605, 1160, 635, MUTED, end="none")
    d.rect(1115, 635, 90, 30, BG, MUTED)
    d.line(1160, 665, 1160, 690, MUTED, end="none")
    for y, w in ((690, 42), (699, 28), (708, 14)):
        d.line(1160-w/2, y, 1160+w/2, y, MUTED, end="none")
    d.text(1220, 630, 290, 60, "50 Ω", MUTED, 19, align="left")
    d.text(245, 630, 430, 80, "Forward: V = Z₀ I → CPL adds\nReverse: V = −Z₀ I → CPL cancels", MEAS, 21)
    d.text(100, 750, 1400, 65,
           "Transformer ratios set the coupling (about −10 dB). Finite directivity leaves reverse leakage.\n"
           "The termination absorbs only the rejected sample; the main reverse wave continues to IN.", TEXT, 20)
    d.text(100, 830, 1400, 30, "Dashed arrows: functional sampling / combining", MUTED, 18)
    d.save()


if __name__ == "__main__":
    transformer_principle()
