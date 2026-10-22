"""Tiny helper to write hand-laid-out draw.io files (explicit positions and edge waypoints, no auto layout).

The .drawio files in ../ are the deliverable and can be edited in draw.io directly; this module and the
build_*.py scripts are only how the first version was produced. If you edit a .drawio by hand, don't re-run
its build script (it would overwrite your edit).
"""
from html import escape
from pathlib import Path

BG = "#121417"
PANEL = "#1C2128"
PANEL_2 = "#262C36"
LINE = "#8B949E"
TEXT = "#E6EDF3"
MUTED = "#8B949E"
RF = "#F78166"
CORE0 = "#58A6FF"
CORE1 = "#A371F7"
MEAS = "#3FB950"
POWER = "#F2CC60"
HOST = "#39C5CF"
BAD = "#FF7B72"
FONT = "Segoe UI"
MONO = "Consolas"

OUT = Path(__file__).resolve().parent.parent


def _fmt(title, sub=None, title_size=None, sub_size=12, sub_color=MUTED):
    t = escape(title).replace("\n", "<br>")
    if title_size:
        t = f'<span style="font-size:{title_size}px">{t}</span>'
    v = f"<b>{t}</b>"
    if sub:
        s = escape(sub).replace("\n", "<br>")
        v += f'<br><span style="font-size:{sub_size}px;color:{sub_color}">{s}</span>'
    return v


def _label_attr(label):
    return escape(escape(label or "").replace("\n", "<br>"), quote=True)


class Diagram:
    def __init__(self, name, w, h):
        self.name, self.w, self.h = name, w, h
        self.cells = []
        self.n = 0

    def _id(self, prefix="c"):
        self.n += 1
        return f"{prefix}{self.n}"

    def _cell(self, cid, value, style, x, y, w, h, vertex=True, parent="1"):
        self.cells.append(
            f'<mxCell id="{cid}" value="{escape(value, quote=True)}" style="{style}" vertex="1" parent="{parent}">'
            f'<mxGeometry x="{x}" y="{y}" width="{w}" height="{h}" as="geometry"/></mxCell>')
        return cid

    # --- shapes -------------------------------------------------------------------------------
    def box(self, x, y, w, h, title, sub=None, color=LINE, fill=PANEL, size=16, sub_size=12, bold=True,
            rounded=True, dashed=False, align="center", font=FONT, stroke=2, sub_color=MUTED, fcolor=TEXT):
        value = _fmt(title, sub, None, sub_size, sub_color) if bold else escape(title).replace("\n", "<br>")
        style = (f"rounded={1 if rounded else 0};arcSize=10;whiteSpace=wrap;html=1;fillColor={fill};"
                 f"strokeColor={color};strokeWidth={stroke};fontColor={fcolor};fontFamily={font};fontSize={size};"
                 f"align={align};verticalAlign=middle;spacing=6;{'dashed=1;dashPattern=6 4;' if dashed else ''}")
        return self._cell(self._id("b"), value, style, x, y, w, h)

    def group(self, x, y, w, h, title, color=LINE, size=15, fill="none", dashed=True, align="left"):
        style = (f"rounded=1;arcSize=3;absoluteArcSize=1;whiteSpace=wrap;html=1;fillColor={fill};strokeColor={color};"
                 f"strokeWidth=1.5;{'dashed=1;dashPattern=8 5;' if dashed else ''}fontColor={color};fontFamily={FONT};"
                 f"fontSize={size};fontStyle=1;verticalAlign=top;align={align};spacingLeft=12;spacingTop=4;")
        cid = self._cell(self._id("g"), escape(title), style, x, y, w, h)
        # keep groups behind everything else
        self.cells.insert(0, self.cells.pop())
        return cid

    def text(self, x, y, w, h, s, color=TEXT, size=14, bold=False, align="center", font=FONT, valign="middle",
             italic=False):
        fs = (1 if bold else 0) + (2 if italic else 0)
        style = (f"text;html=1;whiteSpace=wrap;strokeColor=none;fillColor=none;fontColor={color};fontFamily={font};"
                 f"fontSize={size};fontStyle={fs};align={align};verticalAlign={valign};")
        return self._cell(self._id("t"), escape(s).replace("\n", "<br>"), style, x, y, w, h)

    def html(self, x, y, w, h, value, color=TEXT, size=14, align="left", valign="top", font=FONT):
        style = (f"text;html=1;whiteSpace=wrap;strokeColor=none;fillColor=none;fontColor={color};fontFamily={font};"
                 f"fontSize={size};align={align};verticalAlign={valign};")
        return self._cell(self._id("t"), value, style, x, y, w, h)

    def title(self, s, sub=None):
        self.text(40, 24, self.w - 80, 40, s, TEXT, 28, True, "left")
        if sub:
            self.text(40, 62, self.w - 80, 26, sub, MUTED, 16, False, "left")

    def pill(self, x, y, w, h, s, color, size=12, fill=None):
        style = (f"rounded=1;arcSize=50;whiteSpace=wrap;html=1;fillColor={fill or color};strokeColor=none;"
                 f"fontColor={BG};fontFamily={FONT};fontSize={size};fontStyle=1;")
        return self._cell(self._id("p"), escape(s), style, x, y, w, h)

    def shape(self, x, y, w, h, style, value=""):
        return self._cell(self._id("s"), value, style, x, y, w, h)

    def tri(self, x, y, w, h, color, title="", direction="east"):
        style = (f"triangle;whiteSpace=wrap;html=1;direction={direction};fillColor={PANEL};strokeColor={color};"
                 f"strokeWidth=2;fontColor={TEXT};fontFamily={FONT};fontSize=13;fontStyle=1;spacingRight=14;")
        return self._cell(self._id("s"), escape(title), style, x, y, w, h)

    def circle(self, x, y, d, color, value="", fill=PANEL, size=13):
        style = (f"ellipse;whiteSpace=wrap;html=1;fillColor={fill};strokeColor={color};strokeWidth=2;"
                 f"fontColor={TEXT};fontFamily={FONT};fontSize={size};fontStyle=1;")
        return self._cell(self._id("s"), value, style, x, y, d, d)

    def rect(self, x, y, w, h, fill, stroke="none", value="", fcolor=TEXT, size=13, bold=False, align="center",
             rounded=False, opacity=100, font=FONT, valign="middle"):
        style = (f"rounded={1 if rounded else 0};arcSize=12;whiteSpace=wrap;html=1;fillColor={fill};strokeColor={stroke};"
                 f"fontColor={fcolor};fontFamily={font};fontSize={size};fontStyle={1 if bold else 0};align={align};"
                 f"verticalAlign={valign};opacity={opacity};spacingLeft=6;spacingRight=6;")
        return self._cell(self._id("r"), value, style, x, y, w, h)

    # --- connectors ---------------------------------------------------------------------------
    def edge(self, src, dst, color=LINE, pts=(), label=None, exit=None, entry=None, width=2, dashed=False,
             end="block", start="none", lsize=12, lcolor=None, curved=False, lpos=None, lbg=BG, straight=False):
        st = (f"edgeStyle={'none' if straight else 'orthogonalEdgeStyle'};rounded=1;arcSize=16;html=1;"
              f"strokeColor={color};strokeWidth={width};endArrow={end};endFill=1;startArrow={start};startFill=1;"
              f"endSize=7;startSize=7;fontColor={lcolor or color};fontFamily={FONT};fontSize={lsize};fontStyle=1;"
              f"labelBackgroundColor={lbg};jumpStyle=arc;jumpSize=10;")
        if curved:
            st += "curved=1;"
        if dashed:
            st += "dashed=1;dashPattern=6 4;"
        if exit:
            st += f"exitX={exit[0]};exitY={exit[1]};exitDx=0;exitDy=0;"
        if entry:
            st += f"entryX={entry[0]};entryY={entry[1]};entryDx=0;entryDy=0;"
        cid = self._id("e")
        pts_xml = "".join(f'<mxPoint x="{px}" y="{py}"/>' for px, py in pts)
        arr = f'<Array as="points">{pts_xml}</Array>' if pts else ""
        val = _label_attr(label)
        geo_extra = ""
        if lpos is not None:
            geo_extra = f' x="{lpos[0]}" y="{lpos[1]}" relative="1"'
        self.cells.append(
            f'<mxCell id="{cid}" value="{val}" style="{st}" edge="1" parent="1" source="{src}" target="{dst}">'
            f'<mxGeometry{geo_extra or " relative=\"1\""} as="geometry">{arr}</mxGeometry></mxCell>')
        return cid

    def line(self, x1, y1, x2, y2, color=LINE, width=2, end="block", start="none", dashed=False, pts=(),
             label=None, lsize=12, lcolor=None):
        st = (f"edgeStyle=none;rounded=1;html=1;strokeColor={color};strokeWidth={width};endArrow={end};endFill=1;"
              f"startArrow={start};startFill=1;endSize=7;startSize=7;fontColor={lcolor or color};fontFamily={FONT};"
              f"fontSize={lsize};fontStyle=1;labelBackgroundColor={BG};")
        if dashed:
            st += "dashed=1;dashPattern=6 4;"
        cid = self._id("l")
        pts_xml = "".join(f'<mxPoint x="{px}" y="{py}"/>' for px, py in pts)
        arr = f'<Array as="points">{pts_xml}</Array>' if pts else ""
        val = _label_attr(label)
        self.cells.append(
            f'<mxCell id="{cid}" value="{val}" style="{st}" edge="1" parent="1">'
            f'<mxGeometry relative="1" as="geometry"><mxPoint x="{x1}" y="{y1}" as="sourcePoint"/>'
            f'<mxPoint x="{x2}" y="{y2}" as="targetPoint"/>{arr}</mxGeometry></mxCell>')
        return cid

    def legend(self, x, y, items, title="Legend", w=None, row=26, horizontal=False, col_w=None):
        """Key for line styles. items: (color, text, dashed) or (color, text, dashed, width).

        Use it instead of repeating the same label on several arrows."""
        sample = 46
        if horizontal:
            cw = col_w or 230
            if title:
                self.text(x, y, 120, row, title, MUTED, 13, True, "left")
                x += 90
            for i, it in enumerate(items):
                color, label, dashed = it[:3]
                lx = x + i * cw
                self.line(lx, y + row / 2, lx + sample, y + row / 2, color, it[3] if len(it) > 3 else 2.5,
                          dashed=dashed)
                self.text(lx + sample + 10, y, cw - sample - 14, row, label, TEXT, 13, False, "left")
            return
        w = w or 300
        h = (28 if title else 8) + row * len(items) + 8
        self.rect(x, y, w, h, PANEL, LINE, rounded=True)
        yy = y + 6
        if title:
            self.text(x + 12, yy, w - 24, 22, title, MUTED, 13, True, "left")
            yy += 24
        for it in items:
            color, label, dashed = it[:3]
            self.line(x + 14, yy + row / 2, x + 14 + sample, yy + row / 2, color, it[3] if len(it) > 3 else 2.5,
                      dashed=dashed)
            self.text(x + 24 + sample, yy, w - sample - 34, row, label, TEXT, 13, False, "left")
            yy += row

    # --- output -------------------------------------------------------------------------------
    def save(self):
        body = "".join(self.cells)
        xml = (f'<mxfile host="drawio" type="device"><diagram name="{escape(self.name)}" id="{self.name}">'
               f'<mxGraphModel dx="{self.w}" dy="{self.h}" grid="1" gridSize="10" guides="1" tooltips="1" connect="1" '
               f'arrows="1" fold="1" page="1" pageScale="1" pageWidth="{self.w}" pageHeight="{self.h}" '
               f'background="{BG}" math="0" shadow="0"><root><mxCell id="0"/><mxCell id="1" parent="0"/>'
               f'{body}</root></mxGraphModel></diagram></mxfile>')
        # page-sized invisible frame so every export has the same 16:9 canvas
        OUT.mkdir(parents=True, exist_ok=True)
        path = OUT / f"{self.name}.drawio"
        path.write_text(xml.replace('<mxCell id="1" parent="0"/>',
                                    '<mxCell id="1" parent="0"/>'
                                    f'<mxCell id="frame" value="" style="rounded=0;fillColor={BG};strokeColor=none;'
                                    f'movable=0;resizable=0;editable=0;deletable=0;locked=1;" vertex="1" parent="1">'
                                    f'<mxGeometry x="0" y="0" width="{self.w}" height="{self.h}" as="geometry"/>'
                                    f'</mxCell>', 1), encoding="utf-8")
        return path
