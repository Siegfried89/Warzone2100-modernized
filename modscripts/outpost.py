"""Scav-Aussenposten: der Scav-Bunker (Wellblechdach) in doppelter Groesse mit Dachaufbau und drehender Sensor-Antenne.
Struktur: structs/scavoutpost.pie (Anschluss 0 = Spitze des Sensorturms links oben), Sensor-Modell: normale Sensorschuessel gnlsnsr1 auf trlsnsr1 (dreht sich selbst,
weil die Engine Sensor-Gebaeude ohne Waffe alle 3 Sekunden einmal herumdreht).
Eintraege: stats/structure.json (A0BaBaOutpost), stats/sensor.json (ScavOutpostSensor), Forschung R-Sys-Spade1Mk1.
outpost.py OUT"""
import sys, os, re, math, json
import numpy as np

OUT = sys.argv[1]
BASE = "/tmp/claude-0/cmp/base"
PAGE = "page-7-barbarians-arizona.png"
S = 2.0                                           # doppelte Groesse
SIZE = 1024.0

# Muster-Ausschnitte der Scav-Textur (Pixel der 1024er Seite)
TRUSS = (718, 777, 759, 803)       # X-Fachwerk
CORR = (880, 900, 988, 958)        # graues Wellblech
STEEL = (693, 830, 711, 900)       # hellgraues Blech
DARKP = (885, 965, 965, 1008)      # dunkle Platte mit Markierung
ORANGE = (892, 984, 893, 985)      # Warnlampe
YELLOW = (395, 640, 420, 760)      # gelbes Blech (Technikhaus, alte Variante)
BRICK = (633, 12, 647, 70)         # Scav-Mauer (roter Backstein, wie A0BaBaHorizontalWall)
FAC_SIDE = (335, 24, 380, 124)     # Scav-Fabrik: graue Betonwand
FAC_FRONT = (394, 56, 440, 156)    # Scav-Fabrik: Fensterwand
HOUSE = os.environ.get("HOUSE", "brick")


def uvpt(px, py): return ((px + 0.5) / SIZE, (py + 0.5) / SIZE)
def flat(rect): return uvpt((rect[0] + rect[2]) / 2.0, (rect[1] + rect[3]) / 2.0)


class Model:
    def __init__(self):
        self.pts = []; self.pol = []
    def p(self, x, y, z):
        self.pts.append((x, y, z)); return len(self.pts) - 1
    def tri(self, a, b, c, uva, uvb, uvc, centre):
        A, B, C = (np.array(self.pts[i]) for i in (a, b, c))
        n = np.cross(B - A, C - A)
        if n @ ((A + B + C) / 3.0 - centre) < 0: b, c, uvb, uvc = c, b, uvc, uvb       # Kreuzprodukt zeigt nach aussen
        self.pol.append((a, b, c, uva, uvb, uvc))
    def quad(self, bl, br, tr, tl, rect, centre, full=False):
        """Viereck (von aussen: links unten, rechts unten, rechts oben, links oben); full = ganze Textur, sonst ein Texel"""
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
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), fr, c, full_front)
        self.quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), bk, c, full_front)
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
    def text(self, connector):
        out = ["PIE 3", "TYPE 200", "TEXTURE 0 %s 0 0" % PAGE, "LEVELS 1", "LEVEL 1", "POINTS %d" % len(self.pts)]
        out += ["\t%.3f %.3f %.3f" % p for p in self.pts]
        out.append("POLYGONS %d" % len(self.pol))
        for a, b, c, ua, ub, uc in self.pol:
            out.append("\t200 3 %d %d %d %.6f %.6f %.6f %.6f %.6f %.6f" % (a, b, c, ua[0], ua[1], ub[0], ub[1], uc[0], uc[1]))
        out += ["CONNECTORS 1", "\t%s" % connector, ""]
        return "\n".join(out)


def parse(path):
    t = open(path, encoding="latin-1").read().replace("\r", "").split("\n")
    pts, pol = [], []
    for i, l in enumerate(t):
        w = l.split()
        if not w: continue
        if w[0] == "POINTS":
            n = int(w[1]); pts = [tuple(float(v) for v in t[i + 1 + k].split()[:3]) for k in range(n)]
        elif w[0] == "POLYGONS":
            n = int(w[1])
            for k in range(n):
                v = t[i + 1 + k].split(); c = int(v[1])
                idx = [int(x) for x in v[2:2 + c]]; uv = [float(x) for x in v[2 + c:2 + 3 * c]]
                pol.append((idx, [(uv[2 * j], uv[2 * j + 1]) for j in range(c)]))
    return pts, pol


# ------------------------------------------------------------------ Bunker x2 + Dachaufbau

def build(brown, fname):
    global TX, TZ, TOWER_TOP, DECK
    bp, bpol = parse(os.path.join(BASE, "structs", "blbrbbnk.pie"))
    m = Model()
    for x, y, z in bp: m.pts.append((x * S, y * S, z * S))
    ROOF_DECK = (206.0, 6.0, 298.0, 98.0)             # Scav-Fabrik: braune Wellbleche (parallele Bahnen)
    def roof_uv(x, z):
        u0, v0, u1, v1 = ROOF_DECK
        return ((u0 + (x + 72.0) / 144.0 * (u1 - u0)) / SIZE, (v1 - (z + 72.0) / 144.0 * (v1 - v0)) / SIZE)
    for k, (idx, uv) in enumerate(bpol):
        if brown and k in (16, 17):                              # Dachflaeche (Oberseite auf Hoehe 50)
            uv = [roof_uv(bp[i][0] * S, bp[i][2] * S) for i in idx[:3]]
        m.pol.append((idx[0], idx[1], idx[2], uv[0], uv[1], uv[2]))

    DECK = 50.0                                      # Oberseite des Bunkerdachs (25 * 2)

    # Sensorturm (der normale Sensorturm mit Streben, BLGUARDM) links oben (Standardkamera: Norden = +z) auf dem Dach, normale Groesse
    TS = 1.0
    TX, TZ = -40.0, 40.0
    tp, tpol = parse(os.path.join(BASE, "structs", "blguardm.pie"))
    tb = len(m.pts)
    for x, y, z in tp: m.pts.append((x * TS + TX, y * TS + DECK, z * TS + TZ))
    for idx, uv in tpol:
        for i in range(1, len(idx) - 1):
            m.pol.append((tb + idx[0], tb + idx[i], tb + idx[i + 1], uv[0], uv[i], uv[i + 1]))
    TOWER_TOP = DECK + 115.0 * TS

    # Technikhaus (gelb) rechts unten (Suedseite, zur Standardkamera) auf dem Dach, mit Tuer
    PH0, PH1 = DECK, DECK + 22.0
    ROOF_TEX = (222.0, 92.0, 258.0, 142.0)            # braunes Wellblech (senkrechte Rillen)
    if HOUSE == "factory":
        m.box(14, 58, PH0, PH1, -50, -8, FAC_SIDE, FAC_SIDE, front=FAC_SIDE, back=FAC_FRONT, full_front=True, full_side=True)
    else:
        m.box(14, 58, PH0, PH1, -50, -8, BRICK, BRICK, front=BRICK, back=BRICK, full_front=True, full_side=True)
    # Satteldach (First laengs x), 3 Einheiten Ueberstand
    xL, xR, zF, zB, zr = 11.0, 61.0, -53.0, -5.0, -29.0
    yE, yR = PH1 - 1.0, PH1 + 11.0
    cr = np.array([36.0, PH1 + 2.0, -29.0])
    m.quad((xL, yE, zF), (xR, yE, zF), (xR, yR, zr), (xL, yR, zr), ROOF_TEX, cr, full=True)
    m.quad((xR, yE, zB), (xL, yE, zB), (xL, yR, zr), (xR, yR, zr), ROOF_TEX, cr, full=True)
    for gx in (14.0, 58.0):                            # Giebeldreiecke in Backstein
        a = m.p(gx, PH1, -50.0); b = m.p(gx, PH1, -8.0); c = m.p(gx, yR - 1.0, zr)
        bu0, bv0, bu1, bv1 = BRICK
        m.tri(a, b, c, uvpt(bu0, bv1), uvpt(bu1, bv1), uvpt((bu0 + bu1) / 2, bv0), np.array([36.0, PH1 + 4, zr]))
    m.box(26, 44, PH0, PH0 + 15, -50.8, -50, DARKP, DARKP, front=DARKP, full_front=True)         # Tuer
    m.box(16, 24, PH0 + 6, PH0 + 15, -50.7, -50, ORANGE, ORANGE)                                   # Warnlicht
    # Tuer im Bunker (Suedseite -z) als dunkle Platte vor der schraegen Wand
    m.box(-16, 16, 0.0, 34.0, -61.0, -60.0, DARKP, DARKP, front=DARKP, full_front=True)
    # Stuetzbalken unter dem Dachrand (wirkt wie gewachsene Scav-Bauweise)
    for sx in (-1, 1):
        m.box(sx * 64 - 2.5, sx * 64 + 2.5, 0.0, 40.0, 60.0, 65.0, STEEL, STEEL)
        m.box(sx * 64 - 2.5, sx * 64 + 2.5, 0.0, 40.0, -65.0, -60.0, STEEL, STEEL)
    os.makedirs(os.path.join(OUT, "structs"), exist_ok=True)
    open(os.path.join(OUT, "structs", fname), "w", encoding="latin-1", newline="").write(m.text("%d %d %d" % (TX, TZ, int(round(TOWER_TOP)))))
    return m, TOWER_TOP

m, TOWER_TOP = build(True, "scavoutpost.pie")
m2, _ = build(False, "scavoutpost_g.pie")
OUTPOST_TOP = TOWER_TOP

# ------------------------------------------------------------------ dritte Variante: graues Wellblechdach + rote Backstein-Untermauer
BRICKPAGE = "page-7-barbarians-arizona-brick.png"
def brick_page():
    """Seite page-7 kopieren; die Untermauer-Flaeche (874..918 x 398..522: x = Hoehe, y = Laenge) bekommt rote Backsteine, schwarze Stellen bleiben schwarz"""
    from PIL import Image
    src = Image.open(os.path.join(BASE, "texpages", PAGE)).convert("RGBA"); px = src.load()
    X0, X1, Y0, Y1 = 874, 918, 398, 522
    dark = {(x, y) for x in range(X0 - 2, X1 + 3) for y in range(Y0 - 2, Y1 + 3) if max(px[x, y][:3]) < 22}
    for y in range(Y0, Y1 + 1):
        for x in range(X0, X1 + 1):
            if (x, y) in dark: px[x, y] = (0, 0, 0, 255); continue
            cx, cy = x - X0, y - Y0
            course = cx // 6; off = 10 if course % 2 else 0
            if cx % 6 == 5 or (cy + off) % 20 == 19: col = (122, 112, 100)                                # Moertelfuge
            else:
                h = (course * 7919 + ((cy + off) // 20) * 104729) % 1000 / 1000.0                          # jeder Stein etwas anders
                col = (int(150 + 45 * h), int(48 + 22 * h), int(36 + 14 * h))
            near = any((x + dx, y + dy) in dark for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2))
            k = 0.55 if near else (0.88 + 0.12 * ((cx * 13 + cy * 7) % 5) / 4.0)                            # dunkler am Lochrand, leichtes Rauschen
            px[x, y] = (int(col[0] * k), int(col[1] * k), int(col[2] * k), 255)
    os.makedirs(os.path.join(OUT, "texpages"), exist_ok=True)
    src.save(os.path.join(OUT, "texpages", BRICKPAGE), optimize=True)

_PAGE_STD = PAGE
brick_page(); PAGE = BRICKPAGE
m3, _ = build(False, "scavoutpost_b.pie")
PAGE = _PAGE_STD

# ------------------------------------------------------------------ Stats
sp = os.path.join(OUT, "stats", "sensor.json"); sens = json.load(open(sp, encoding="utf-8"))
sens["ScavOutpostSensor"] = {"buildPoints": 25, "buildPower": 5, "hitpoints": 300, "id": "ScavOutpostSensor", "location": "TURRET",
                             "name": "Scavenger Outpost Sensor", "range": 1152, "sensorModel": "gnlsnsr1.pie", "mountModel": "trlsnsr1.pie", "type": "STANDARD", "weight": 100}
json.dump(sens, open(sp, "w", encoding="utf-8"), indent="\t", sort_keys=True)

tp = os.path.join(OUT, "stats", "structure.json"); st = json.load(open(tp, encoding="utf-8"))
for sid, nm, mdl in (("A0BaBaOutpost", "Scavenger Outpost (brown roof)", "scavoutpost.pie"), ("A0BaBaOutpostG", "Scavenger Outpost (grey roof)", "scavoutpost_g.pie"), ("A0BaBaOutpostB", "Scavenger Outpost (brick walls)", "scavoutpost_b.pie")):
    st[sid] = {"armour": 15, "breadth": 2, "buildPoints": 60, "buildPower": 220, "ecmID": "ZNULLECM", "height": 3, "hitpoints": 700,
               "id": sid, "name": nm, "resistance": 150, "sensorID": "ScavOutpostSensor", "strength": "HARD",
               "structureModel": [mdl], "thermal": 15, "type": "DEFENSE", "width": 2}
json.dump(st, open(tp, "w", encoding="utf-8"), indent="\t", sort_keys=True)

rp = os.path.join(OUT, "stats", "research.json"); rs = json.load(open(rp, encoding="utf-8"))
res = rs["R-Sys-Spade1Mk1"]["resultStructures"]
for sid in ("A0BaBaOutpost", "A0BaBaOutpostG", "A0BaBaOutpostB"):
    if sid not in res: res.append(sid)
json.dump(rs, open(rp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
print("Aussenposten: %d Punkte %d Flaechen; Anschluss (%d, %d, %d)" % (len(m.pts), len(m.pol), TX, TZ, OUTPOST_TOP))
