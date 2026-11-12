"""Scav-Raffinerie (3x3 Felder): Bodenplatte, zwei liegende Tankzylinder (Rohoel), Destillationskolonne, Schornstein mit Fackel, Pumpenhaus
mit braunem Wellblechdach, Rohrleitungen und Gitterwerk-Rohrbruecke. Alles aus Scav-Texturstellen (page-7-barbarians-arizona).
Struktur A0BaBaRefinery (Typ GENERIC, 3x3), freigeschaltet mit R-Sys-Spade1Mk1.
refinery.py OUT [PREVIEW.png]"""
import sys, os, json, math, copy
import numpy as np

OUT = sys.argv[1]
PREV = sys.argv[2] if len(sys.argv) > 2 else None
PAGE = "page-7-barbarians-arizona.png"
SIZE = 1024.0
TRUSS = (718, 777, 759, 803)
CORR = (880, 900, 988, 958)
STEEL = (693, 830, 711, 900)
DARKP = (885, 965, 965, 1008)
ORANGE = (892, 984, 893, 985)
BRICK = (633, 12, 647, 70)
ROOF = (206.0, 6.0, 298.0, 98.0)


def uvpt(px, py): return ((px + 0.5) / SIZE, (py + 0.5) / SIZE)
def flat(rect): return uvpt((rect[0] + rect[2]) / 2.0, (rect[1] + rect[3]) / 2.0)


class Model:
    def __init__(self):
        self.pts = []; self.pol = []
    def p(self, x, y, z):
        self.pts.append((float(x), float(y), float(z))); return len(self.pts) - 1
    def tri(self, a, b, c, uva, uvb, uvc, centre):
        A, B, C = (np.array(self.pts[i]) for i in (a, b, c))
        n = np.cross(B - A, C - A)
        if n @ ((A + B + C) / 3.0 - centre) < 0: b, c, uvb, uvc = c, b, uvc, uvb
        self.pol.append((a, b, c, uva, uvb, uvc))
    def quad(self, bl, br, tr, tl, rect, centre, full=False):
        ids = [self.p(*v) for v in (bl, br, tr, tl)]
        if full:
            x0, y0, x1, y1 = rect
            uv = [uvpt(x0 + 0.5, y1 - 0.5), uvpt(x1 - 0.5, y1 - 0.5), uvpt(x1 - 0.5, y0 + 0.5), uvpt(x0 + 0.5, y0 + 0.5)]
        else:
            uv = [flat(rect)] * 4
        self.tri(ids[0], ids[1], ids[2], uv[0], uv[1], uv[2], centre)
        self.tri(ids[0], ids[2], ids[3], uv[0], uv[2], uv[3], centre)
    def box(self, x0, x1, y0, y1, z0, z1, side, top, front=None, back=None, full_front=False, full_side=False):
        c = np.array([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])
        fr = front or side; bk = back or side
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), bk, c, full_front)
        self.quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), fr, c, full_front)
        self.quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), side, c, full_side)
        self.quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), side, c, full_side)
        self.quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), top, c)
    def prism(self, cx, cz, r, y0, y1, n, side, top):
        c = np.array([cx, (y0 + y1) / 2, cz])
        ang = [2 * math.pi * (i + 0.5) / n for i in range(n)]
        for i in range(n):
            a0, a1 = ang[i], ang[(i + 1) % n]
            p0 = (cx + r * math.cos(a0), cz + r * math.sin(a0)); p1 = (cx + r * math.cos(a1), cz + r * math.sin(a1))
            self.quad((p0[0], y0, p0[1]), (p1[0], y0, p1[1]), (p1[0], y1, p1[1]), (p0[0], y1, p0[1]), side, c)
            ids = [self.p(cx, y1, cz), self.p(p0[0], y1, p0[1]), self.p(p1[0], y1, p1[1])]
            self.tri(ids[0], ids[1], ids[2], flat(top), flat(top), flat(top), c)
    def cyl_x(self, cy, cz, r, x0, x1, n, side, cap):
        """liegender Zylinder entlang x"""
        c = np.array([(x0 + x1) / 2.0, cy, cz])
        ang = [2 * math.pi * (i + 0.5) / n for i in range(n)]
        for i in range(n):
            a0, a1 = ang[i], ang[(i + 1) % n]
            p0 = (cy + r * math.cos(a0), cz + r * math.sin(a0)); p1 = (cy + r * math.cos(a1), cz + r * math.sin(a1))
            self.quad((x0, p0[0], p0[1]), (x1, p0[0], p0[1]), (x1, p1[0], p1[1]), (x0, p1[0], p1[1]), side, c)
            for xx in (x0, x1):
                ids = [self.p(xx, cy, cz), self.p(xx, p0[0], p0[1]), self.p(xx, p1[0], p1[1])]
                self.tri(ids[0], ids[1], ids[2], flat(cap), flat(cap), flat(cap), c)
    def text(self, connector):
        out = ["PIE 3", "TYPE 200", "TEXTURE 0 %s 0 0" % PAGE, "LEVELS 1", "LEVEL 1", "POINTS %d" % len(self.pts)]
        out += ["\t%.3f %.3f %.3f" % p for p in self.pts]
        out.append("POLYGONS %d" % len(self.pol))
        for a, b, c, ua, ub, uc in self.pol:
            out.append("\t200 3 %d %d %d %.6f %.6f %.6f %.6f %.6f %.6f" % (a, b, c, ua[0], ua[1], ub[0], ub[1], uc[0], uc[1]))
        out += ["CONNECTORS 1", "\t%s" % connector, ""]
        return "\n".join(out)


def build():
    m = Model()
    m.box(-188, 188, 0, 4, -188, 188, DARKP, DARKP)                                   # Bodenplatte
    # zwei liegende Rohoel-Tanks links (x -170..-60), Sattel-Ruecken aus Stahl
    for zc in (-95.0, 75.0):
        m.cyl_x(40.0, zc, 38.0, -165.0, -55.0, 12, CORR, STEEL)
        for xx in (-150.0, -70.0):
            m.box(xx - 5, xx + 5, 4, 30, zc - 30, zc + 30, STEEL, STEEL)                # Saettel
        for xx in (-140.0, -110.0, -80.0):
            m.cyl_x(40.0, zc, 39.5, xx - 3, xx + 3, 12, STEEL, STEEL)                  # Spannringe
        m.box(-115, -105, 76, 84, zc - 6, zc + 6, DARKP, DARKP)                         # Einstiegsluke
    # Destillationskolonne (Mitte) mit Ringen und Kopf
    m.prism(10.0, 20.0, 28.0, 4, 215, 10, STEEL, STEEL)
    for yy in (55.0, 110.0, 165.0):
        m.prism(10.0, 20.0, 31.0, yy, yy + 8, 10, DARKP, DARKP)
    m.prism(10.0, 20.0, 18.0, 215, 232, 8, CORR, CORR)
    # Schornstein mit Fackelkopf (rechts hinten)
    m.prism(120.0, 120.0, 10.0, 4, 235, 8, CORR, CORR)
    m.prism(120.0, 120.0, 14.0, 90, 98, 8, DARKP, DARKP)
    m.box(112, 128, 235, 247, 112, 128, ORANGE, ORANGE)                                  # Fackelflamme (Orange)
    # Pumpenhaus vorn rechts mit Satteldach in braunem Wellblech
    m.box(45, 170, 4, 62, -175, -95, BRICK, BRICK, front=BRICK, back=BRICK, full_front=True, full_side=True)
    xL, xR, zF, zB, zr = 41.0, 174.0, -180.0, -90.0, -135.0
    yE, yR = 61.0, 88.0
    cr = np.array([107.0, 66.0, -135.0])
    m.quad((xL, yE, zF), (xR, yE, zF), (xR, yR, zr), (xL, yR, zr), ROOF, cr, full=True)
    m.quad((xR, yE, zB), (xL, yE, zB), (xL, yR, zr), (xR, yR, zr), ROOF, cr, full=True)
    for gx in (45.0, 170.0):
        a = m.p(gx, 62.0, -175.0); b = m.p(gx, 62.0, -95.0); c = m.p(gx, yR - 1.0, zr)
        m.tri(a, b, c, uvpt(640, 20), uvpt(640, 60), uvpt(645, 40), np.array([gx, 70.0, zr]))
    # Rohrleitungen: Tanks -> Kolonne -> Pumpenhaus, Kolonne -> Schornstein
    m.box(-55, -18, 52, 58, -100, -94, STEEL, STEEL)
    m.box(-55, -18, 52, 58, 70, 76, STEEL, STEEL)
    m.box(-24, -18, 52, 58, -100, 76, STEEL, STEEL)
    m.box(-18, 8, 52, 58, 17, 23, STEEL, STEEL)
    m.box(38, 120, 120, 126, 17, 23, STEEL, STEEL)
    m.box(114, 126, 120, 126, 23, 117, STEEL, STEEL)
    m.box(100, 106, 62, 120, -135, -129, STEEL, STEEL)
    m.box(100, 108, 62, 70, -135, 20, STEEL, STEEL)
    # Gitterwerk-Rohrbruecke an der Rueckseite
    m.box(-170, 170, 0, 34, 168, 174, TRUSS, TRUSS, front=TRUSS, back=TRUSS, full_front=True)
    for xx in (-170, -85, 0, 85, 170):
        m.box(xx - 3, xx + 3, 0, 44, 166, 176, STEEL, STEEL)
    return m


def main():
    os.makedirs(os.path.join(OUT, "structs"), exist_ok=True)
    m = build()
    open(os.path.join(OUT, "structs", "scavrefinery.pie"), "w", encoding="latin-1", newline="").write(m.text("120 120 247"))
    sp = os.path.join(OUT, "stats", "structure.json"); S = json.load(open(sp, encoding="utf-8"))
    e = copy.deepcopy(S["A0PowerPole"])
    e.update({"id": "A0BaBaRefinery", "name": "Scavenger Refinery", "type": "GENERIC", "structureModel": ["scavrefinery.pie"], "width": 3, "breadth": 3,
              "hitpoints": 1100, "buildPoints": 70, "buildPower": 300, "armour": 12, "thermal": 8, "height": 4})
    e.pop("sensorID", None)
    S["A0BaBaRefinery"] = e
    json.dump(S, open(sp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    rp = os.path.join(OUT, "stats", "research.json"); R = json.load(open(rp, encoding="utf-8"))
    rs = R["R-Sys-Spade1Mk1"].setdefault("resultStructures", [])
    if "A0BaBaRefinery" not in rs: rs.append("A0BaBaRefinery")
    json.dump(R, open(rp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Raffinerie: A0BaBaRefinery angelegt (%d Punkte, %d Flaechen)" % (len(m.pts), len(m.pol)))


main()
