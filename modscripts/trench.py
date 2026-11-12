"""Schuetzengraeben und Gruben (Scav): zwei parallele, duenne Sandsack-Waende mit dunklem Dreckboden dazwischen.
Graeben (Typ WALL, je 4 Modelle: gerade, Kreuz, T, L):
  A0BaBaTrench             offen, an der Oberflaeche (Waende 26 hoch, Boden auf Bodenhoehe)
  A0BaBaTrenchCover        mit Abdeckung: weissgraues Tuch an zwei sehr duennen Holzstaeben auf den Sandsackwaenden
  A0BaBaTrenchDeep         offen, eingegraben (Boden 19,5 unter dem Gelaende, Brustwehr 13 hoch)
  A0BaBaTrenchDeepCover    eingegraben mit Abdeckung
Gruben (Typ DEFENSE, ohne Waffe = LEER, spaeter von Infanterie besetzbar):
  A0BaBaPitSquare / A0BaBaPitHex (quadratisch wie die Moerstellung / sechseckig wie die Scav-Raketenstellung), je auch ...Deep.
Textur: page-431-trench.png (trenchtex.py): Sandsack der Moerserstellung, Matsch, Tuch, Holz.
trench.py OUT"""
import sys, os, json, math
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trenchtex as TX

OUT = sys.argv[1]
PAGE = "page-431-trench.png"
SIZE = float(TX.SZ)
LEN = 64.0                                         # halbe Kachellaenge (ein Feld = 128)
HI = 20.0                                          # halbe lichte Breite des Grabens (40 breit)
PX = 1.57                                          # Texel pro Einheit beim Sandsack (wie sandbag.py)
SBX, SBY = TX.SB_AT
U0, U1 = SBX + 0.5, SBX + 43.5                     # Sandsack-Muster im Atlas
V0, V1 = float(SBY), SBY + 112.0
FLAT = [(0.0, 6.5, 12.0), (6.5, 13.0, 11.0), (13.0, 19.5, 9.5), (19.5, 26.0, 8.0)]
DEEP = [(-19.5, -13.0, 12.0), (-13.0, -6.5, 12.0), (-6.5, 0.0, 12.0), (0.0, 6.5, 12.0), (6.5, 13.0, 11.0)]
PIT_FLAT = [(0.0, 7.0, 14.0), (7.0, 14.0, 12.5), (14.0, 21.0, 11.0), (21.0, 28.0, 9.5)]
PIT_DEEP = [(-21.0, -14.0, 14.0), (-14.0, -7.0, 14.0), (-7.0, 0.0, 14.0), (0.0, 7.0, 14.0), (7.0, 14.0, 12.5)]
PIT_HI = 38.0                                      # lichte halbe Breite der Gruben (76)
SAG = 3.0                                          # Durchhang des Tuchs


class Model:
    def __init__(self):
        self.pts = []; self.pol = []
    def p(self, x, y, z):
        self.pts.append((x, y, z)); return len(self.pts) - 1
    def tri(self, a, b, c, ua, ub, uc, centre):
        A, B, C = (np.array(self.pts[i]) for i in (a, b, c))
        n = np.cross(B - A, C - A)
        if n @ ((A + B + C) / 3.0 - centre) < 0: b, c, ub, uc = c, b, uc, ub
        self.pol.append((a, b, c, ua, ub, uc))
    def quad_px(self, bl, br, tr, tl, uvs, centre):
        ids = [self.p(*v) for v in (bl, br, tr, tl)]
        uv = [(u / SIZE, v / SIZE) for (u, v) in uvs]
        self.tri(ids[0], ids[1], ids[2], uv[0], uv[1], uv[2], centre)
        self.tri(ids[0], ids[2], ids[3], uv[0], uv[2], uv[3], centre)
    def text(self):
        out = ["PIE 3", "TYPE 200", "TEXTURE 0 %s 0 0" % PAGE, "LEVELS 1", "LEVEL 1", "POINTS %d" % len(self.pts)]
        out += ["\t%.3f %.3f %.3f" % p for p in self.pts]
        out.append("POLYGONS %d" % len(self.pol))
        for a, b, c, ua, ub, uc in self.pol:
            out.append("\t200 3 %d %d %d %.6f %.6f %.6f %.6f %.6f %.6f" % (a, b, c, ua[0], ua[1], ub[0], ub[1], uc[0], uc[1]))
        out += ["CONNECTORS 1", "\t0 0 26", ""]
        return "\n".join(out)


rng_state = [4711]
def rnd(n):
    rng_state[0] = (rng_state[0] * 1103515245 + 12345) & 0x7fffffff
    return rng_state[0] % n

def rnd_uv(wpx, lpx):
    ua = U0 + rnd(int(max(1, (U1 - U0) - wpx)))
    va = V0 + rnd(int(max(1, (V1 - V0) - lpx)))
    return ua, va


# ---------------------------------------------------------------- Sandsack-Flaechen
def side_seg(m, p0, p1, y0, y1, centre):
    """senkrechte Seitenflaeche entlang der Strecke p0-p1 (x,z); Saecke liegen waagerecht: lange Achse des Musters (v) entlang der Wand"""
    L = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    n = max(1, int(math.ceil(L / 30.0)))
    hpx = (y1 - y0) * PX
    for i in range(n):
        a = (p0[0] + (p1[0] - p0[0]) * i / n, p0[1] + (p1[1] - p0[1]) * i / n)
        b = (p0[0] + (p1[0] - p0[0]) * (i + 1) / n, p0[1] + (p1[1] - p0[1]) * (i + 1) / n)
        lpx = L / n * PX
        ua, va = rnd_uv(hpx, lpx)
        uvs = [(ua, va), (ua, va + lpx), (ua + hpx, va + lpx), (ua + hpx, va)]       # bl, br, tr, tl
        m.quad_px((a[0], y0, a[1]), (b[0], y0, b[1]), (b[0], y1, b[1]), (a[0], y1, a[1]), uvs, centre)


def top_quad(m, bl, br, tr, tl, y1, centre, along_len, across_w):
    """Oberseite (Viereck, bl->br entlang der Wand): Sackoberseiten liegen mit der langen Achse entlang der Wand"""
    wpx = min(U1 - U0 - 1, across_w * PX)
    lpx = min(V1 - V0 - 1, along_len * PX)
    ua, va = rnd_uv(wpx, lpx)
    # bl=(across 0, along 0); br=(across 0, along 1)?  -> bl->br laeuft entlang der Wand = v, bl->tl quer = u
    m.quad_px((bl[0], y1, bl[1]), (br[0], y1, br[1]), (tr[0], y1, tr[1]), (tl[0], y1, tl[1]),
              [(ua, va), (ua, va + lpx), (ua + wpx, va + lpx), (ua + wpx, va)], centre)


# ---------------------------------------------------------------- Zellen-Raster
def classify(arms):
    """5 x 5 Raster, t[ix][iz]: 0 = -x/-z aussen, 1 = Wandband, 2 = Mitte, 3 = Wandband, 4 = +x/+z aussen. Typen: W Wand, F Boden, E leer"""
    t = [["E"] * 5 for _ in range(5)]
    for ix in (1, 2, 3):
        for iz in (1, 2, 3): t[ix][iz] = "W"
    t[2][2] = "F"
    if "+x" in arms: t[3][2] = "F"; t[4][2] = "F"; t[4][1] = "W"; t[4][3] = "W"
    if "-x" in arms: t[1][2] = "F"; t[0][2] = "F"; t[0][1] = "W"; t[0][3] = "W"
    if "+z" in arms: t[2][3] = "F"; t[2][4] = "F"; t[1][4] = "W"; t[3][4] = "W"
    if "-z" in arms: t[2][1] = "F"; t[2][0] = "F"; t[1][0] = "W"; t[3][0] = "W"
    return t


def bands(hi, w, edge):
    return [-edge, -(hi + w), -hi, hi, hi + w, edge]


def wall_courses(m, arms, hi, courses, edge=LEN):
    t = classify(arms)
    for (y0, y1, w) in courses:
        xb = bands(hi, w, edge); zb = xb
        for ix in range(5):
            for iz in range(5):
                if t[ix][iz] != "W": continue
                x0, x1, z0, z1 = xb[ix], xb[ix + 1], zb[iz], zb[iz + 1]
                c = np.array([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])
                if ix > 0 and t[ix - 1][iz] != "W": side_seg(m, (x0, z1), (x0, z0), y0, y1, c)
                if ix < 4 and t[ix + 1][iz] != "W": side_seg(m, (x1, z0), (x1, z1), y0, y1, c)
                if iz > 0 and t[ix][iz - 1] != "W": side_seg(m, (x0, z0), (x1, z0), y0, y1, c)
                if iz < 4 and t[ix][iz + 1] != "W": side_seg(m, (x1, z1), (x0, z1), y0, y1, c)
                # Oberseite: lange Achse entlang der laengeren Kantenlaenge
                if (x1 - x0) >= (z1 - z0):
                    n = max(1, int(round((x1 - x0) / max(z1 - z0, 1.0))))
                    for i in range(n):
                        a = x0 + (x1 - x0) * i / n; b = x0 + (x1 - x0) * (i + 1) / n
                        top_quad(m, (a, z1), (b, z1), (b, z0), (a, z0), y1, c, b - a, z1 - z0)
                else:
                    n = max(1, int(round((z1 - z0) / max(x1 - x0, 1.0))))
                    for i in range(n):
                        a = z0 + (z1 - z0) * i / n; b = z0 + (z1 - z0) * (i + 1) / n
                        top_quad(m, (x0, b), (x0, a), (x1, a), (x1, b), y1, c, b - a, x1 - x0)
    return t


def floor_cells(m, arms, hi, courses, fy, edge=LEN):
    """Dreckboden: komplett bedeckt, planar abgebildet (1 Texel pro Einheit, Muster ist kachelbar)"""
    t = classify(arms)
    xb = bands(hi, courses[-1][2], edge); zb = xb
    cen = np.array([0.0, fy - 100.0, 0.0])
    for ix in range(5):
        for iz in range(5):
            if t[ix][iz] != "F": continue
            x0, x1, z0, z1 = xb[ix], xb[ix + 1], zb[iz], zb[iz + 1]
            def uv(x, z): return (TX.MUD_AT[0] + (x + 64.0), TX.MUD_AT[1] + (z + 64.0))
            m.quad_px((x0, fy, z0), (x1, fy, z0), (x1, fy, z1), (x0, fy, z1), [uv(x0, z0), uv(x1, z0), uv(x1, z1), uv(x0, z1)], cen)


# ---------------------------------------------------------------- Abdeckung (Tuch + Staebe)
def box_wood(m, x0, x1, y0, y1, z0, z1, along):
    """duenner Holzstab; along = 'x' oder 'z'"""
    c = np.array([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])
    L = (x1 - x0) if along == "x" else (z1 - z0)
    ux = TX.WOOD_AT[0] + 8.0; uy = TX.WOOD_AT[1] + 4.0
    lp = min(L, 120.0)
    # Stab-Flaechen: v entlang des Stabs
    def q(a, b, cc, d):
        m.quad_px(a, b, cc, d, [(ux, uy), (ux, uy + lp), (ux + 2.0, uy + lp), (ux + 2.0, uy)] if along == "x" else
                  [(ux, uy), (ux, uy + lp), (ux + 2.0, uy + lp), (ux + 2.0, uy)], c)
    if along == "x":
        q((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)); q((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0))
        q((x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)); q((x0, y0, z1), (x1, y0, z1), (x1, y0, z0), (x0, y0, z0))
    else:
        q((x1, y0, z0), (x1, y0, z1), (x1, y1, z1), (x1, y1, z0)); q((x0, y0, z1), (x0, y0, z0), (x0, y1, z0), (x0, y1, z1))
        q((x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)); q((x0, y0, z1), (x1, y0, z1), (x1, y0, z0), (x0, y0, z0))
    # Stirnflaechen
    cap = [(ux, uy), (ux, uy + 2.0), (ux + 2.0, uy + 2.0), (ux + 2.0, uy)]
    if along == "x":
        m.quad_px((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), cap, c); m.quad_px((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), cap, c)
    else:
        m.quad_px((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), cap, c); m.quad_px((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), cap, c)


def cover(m, arms, ytop, wt):
    c = HI + wt / 2.0                         # Mittellinie der Wandoberseite
    yst = ytop + 2.0                          # Oberkante der Staebe = Tuchkante
    h = 1.0                                   # halbe Stabdicke
    S = []                                    # Staebe: (achse, fest, a0, a1)
    for d in ("+x", "-x", "+z", "-z"):
        if d in arms:
            sg = 1.0 if d[0] == "+" else -1.0
            if d[1] == "x":
                S += [("x", c, sg * c, sg * LEN), ("x", -c, sg * c, sg * LEN)]
            else:
                S += [("z", c, sg * c, sg * LEN), ("z", -c, sg * c, sg * LEN)]
        else:
            sg = 1.0 if d[0] == "+" else -1.0
            S.append(("z", sg * c, -c, c) if d[1] == "x" else ("x", sg * c, -c, c))
    for (ax, fixed, a0, a1) in S:
        lo, hi_ = min(a0, a1) - h, max(a0, a1) + h
        if ax == "x": box_wood(m, lo, hi_, ytop, yst, fixed - h, fixed + h, "x")
        else: box_wood(m, fixed - h, fixed + h, ytop, yst, lo, hi_, "z")

    def g(x, z):
        ax = (1.0 - (z / c) ** 2) if ("+x" in arms or "-x" in arms) else 0.0
        az = (1.0 - (x / c) ** 2) if ("+z" in arms or "-z" in arms) else 0.0
        v = max(ax, az, 0.0)
        for d in ("+x", "-x", "+z", "-z"):
            if d in arms: continue
            opp = ("-" if d[0] == "+" else "+") + d[1]
            if opp not in arms: continue                       # nur geschlossene Enden einer vorhandenen Achse
            coord = x if d[1] == "x" else z
            out = max(0.0, coord if d[0] == "+" else -coord)
            v *= max(0.0, 1.0 - (out / c) ** 2)
        return v

    rects = [(-c, c, -c, c)]
    if "+x" in arms: rects.append((c, LEN, -c, c))
    if "-x" in arms: rects.append((-LEN, -c, -c, c))
    if "+z" in arms: rects.append((-c, c, c, LEN))
    if "-z" in arms: rects.append((-c, c, -LEN, -c))
    cen = np.array([0.0, yst - 100.0, 0.0])
    for (x0, x1, z0, z1) in rects:
        nx = max(1, int(round((x1 - x0) / 6.0))); nz = max(1, int(round((z1 - z0) / 6.0)))
        for i in range(nx):
            for j in range(nz):
                xa = x0 + (x1 - x0) * i / nx; xb_ = x0 + (x1 - x0) * (i + 1) / nx
                za = z0 + (z1 - z0) * j / nz; zb_ = z0 + (z1 - z0) * (j + 1) / nz
                def P(x, z): return (x, yst - SAG * g(x, z), z)
                def uv(x, z): return (TX.CLOTH_AT[0] + (x + 64.0), TX.CLOTH_AT[1] + (z + 64.0))
                m.quad_px(P(xa, za), P(xb_, za), P(xb_, zb_), P(xa, zb_), [uv(xa, za), uv(xb_, za), uv(xb_, zb_), uv(xa, zb_)], cen)


# ---------------------------------------------------------------- Graeben
def trench(arms, deep, covered):
    rng_state[0] = 4711 + len(arms) * 17 + (7 if deep else 0)
    m = Model()
    courses = DEEP if deep else FLAT
    wall_courses(m, arms, HI, courses)
    fy = (courses[0][0] + 0.3) if deep else 0.6
    floor_cells(m, arms, HI, courses, fy)
    if covered: cover(m, arms, courses[-1][1], courses[-1][2])
    return m


# ---------------------------------------------------------------- Gruben
def pit_square(deep):
    rng_state[0] = 9001 + (3 if deep else 0)
    m = Model()
    courses = PIT_DEEP if deep else PIT_FLAT
    wall_courses(m, (), PIT_HI, courses)
    floor_cells(m, (), PIT_HI, courses, (courses[0][0] + 0.3) if deep else 0.6)
    return m


def pit_hex(deep):
    """regelmaessiges Sechseck, flache Seiten nach +z/-z, Ecken nach +-x"""
    rng_state[0] = 9101 + (3 if deep else 0)
    m = Model()
    courses = PIT_DEEP if deep else PIT_FLAT
    ai = PIT_HI - 2.0                                    # lichter Radius bis zur Seite (Apothem)
    def poly(a):                                         # Sechseck mit Apothem a; Ecken bei 0,60,...Grad
        r = a / math.cos(math.radians(30))
        return [(r * math.cos(math.radians(60 * k)), r * math.sin(math.radians(60 * k))) for k in range(6)]
    P_in = poly(ai)
    for (y0, y1, w) in courses:
        P_out = poly(ai + w)
        for k in range(6):
            k2 = (k + 1) % 6
            a_i, b_i, a_o, b_o = P_in[k], P_in[k2], P_out[k], P_out[k2]
            c = np.array([(a_i[0] + b_i[0] + a_o[0] + b_o[0]) / 4, (y0 + y1) / 2, (a_i[1] + b_i[1] + a_o[1] + b_o[1]) / 4])
            side_seg(m, (b_i[0], b_i[1]), (a_i[0], a_i[1]), y0, y1, c)            # Innenflaeche (zur Grube)
            side_seg(m, (a_o[0], a_o[1]), (b_o[0], b_o[1]), y0, y1, c)            # Aussenflaeche
            L = math.hypot(b_i[0] - a_i[0], b_i[1] - a_i[1])
            top_quad(m, a_i, b_i, b_o, a_o, y1, c, L, w)
    fy = (courses[0][0] + 0.3) if deep else 0.6
    cen = np.array([0.0, fy - 100.0, 0.0])
    def uv(x, z): return (TX.MUD_AT[0] + (x + 64.0), TX.MUD_AT[1] + (z + 64.0))
    for k in range(6):
        a, b = P_in[k], P_in[(k + 1) % 6]
        ids = [m.p(0.0, fy, 0.0), m.p(a[0], fy, a[1]), m.p(b[0], fy, b[1])]
        m.tri(ids[0], ids[1], ids[2], tuple(v / SIZE for v in uv(0, 0)), tuple(v / SIZE for v in uv(*a)), tuple(v / SIZE for v in uv(*b)), cen)
    return m


SHAPES = {"h": ("-x", "+x"), "x": ("-x", "+x", "+z", "-z"), "t": ("-x", "+x", "+z"), "l": ("-x", "+z")}
VARIANTS = {   # Kuerzel: (tief, Abdeckung)
    "trnf": (False, False), "trnc": (False, True), "trdf": (True, False), "trdc": (True, True),
}

if __name__ == "__main__":
    os.makedirs(os.path.join(OUT, "structs"), exist_ok=True); os.makedirs(os.path.join(OUT, "texpages"), exist_ok=True)
    TX.build().save(os.path.join(OUT, "texpages", PAGE))
    names = {}
    for key, (deep, cov) in VARIANTS.items():
        for s, arms in SHAPES.items():
            m = trench(arms, deep, cov)
            fn = "%s_%s.pie" % (key, s)
            open(os.path.join(OUT, "structs", fn), "w", encoding="latin-1", newline="").write(m.text())
        names[key] = ["%s_h.pie" % key, "%s_x.pie" % key, "%s_t.pie" % key, "%s_l.pie" % key]
    pits = {"pitsq": pit_square(False), "pitsq_d": pit_square(True), "pithx": pit_hex(False), "pithx_d": pit_hex(True)}
    for k, m in pits.items():
        open(os.path.join(OUT, "structs", k + ".pie"), "w", encoding="latin-1", newline="").write(m.text())

    # ---- Stats
    sp = os.path.join(OUT, "stats", "structure.json"); st = json.load(open(sp, encoding="utf-8"))
    wall = st["A0BaBaSandbagWall"]
    new = [("A0BaBaTrench", "Trench", "trnf", 160, 12, 12), ("A0BaBaTrenchCover", "Covered Trench", "trnc", 140, 16, 14),
           ("A0BaBaTrenchDeep", "Deep Trench", "trdf", 200, 20, 20), ("A0BaBaTrenchDeepCover", "Deep Covered Trench", "trdc", 180, 24, 22)]
    ids = []
    for (sid, nm, key, hp, bp, bpts) in new:
        e = dict(wall)
        e.update({"id": sid, "name": nm, "structureModel": names[key], "hitpoints": hp, "armour": 8, "thermal": 6, "buildPower": bp, "buildPoints": bpts, "strength": "MEDIUM", "height": 1})
        st[sid] = e; ids.append(sid)
    pit_base = dict(st["A0BaBaMortarPit"])
    for k in ("weapons", "sensorID"): pit_base.pop(k, None)
    for (sid, nm, mdl, hp, bp) in (("A0BaBaPitSquare", "Sandbag Pit", "pitsq.pie", 220, 14), ("A0BaBaPitHex", "Hex Pit", "pithx.pie", 220, 14),
                                   ("A0BaBaPitSquareDeep", "Deep Sandbag Pit", "pitsq_d.pie", 260, 22), ("A0BaBaPitHexDeep", "Deep Hex Pit", "pithx_d.pie", 260, 22)):
        e = dict(pit_base)
        e.update({"id": sid, "name": nm, "structureModel": [mdl], "hitpoints": hp, "armour": 10, "thermal": 8, "buildPower": bp, "buildPoints": bp, "strength": "MEDIUM", "type": "DEFENSE", "width": 1, "breadth": 1, "height": 1})
        st[sid] = e; ids.append(sid)
    json.dump(st, open(sp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    rp = os.path.join(OUT, "stats", "research.json"); rs = json.load(open(rp, encoding="utf-8"))
    res = rs["R-Sys-Spade1Mk1"]["resultStructures"]
    for i in ids:
        if i not in res: res.append(i)
    json.dump(rs, open(rp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Graeben: 16 Modelle, Gruben: 4 Modelle, Strukturen:", ", ".join(ids))
