"""Sandsack-Mauer (Scav): etwas dicker und niedriger als die Scav-Mauer (unten 24, oben 15 breit, 26 hoch), aus vier zurueckgesetzten Lagen waagerecht liegender Saecke.
Struktur "A0BaBaSandbagWall" (Typ WALL, Modelle: gerade, Kreuz, T, L wie A0BaBaHorizontalWall).
Textur: Sandsack-Muster der Moerserstellung (page-7-barbarians-arizona, Bereich x 874..918, y 273..386).
sandbag.py OUT"""
import sys, os, json, math
import numpy as np

OUT = sys.argv[1]
PAGE = "page-7-barbarians-arizona.png"
SIZE = 1024.0
LEN = 64.0                                            # halbe Laenge (ein Feld = 128)
COURSES = [(0.0, 6.5, 12.0), (6.5, 13.0, 10.5), (13.0, 19.5, 9.0), (19.5, 26.0, 7.5)]   # (y0, y1, halbe Dicke): unten 24, oben 15 breit, 26 hoch
PX = 1.57                                              # Texel pro Einheit (ein Sack ca. 11 x 30 Texel = 7 x 19 Einheiten)
U0, U1 = 874.5, 917.5
V0, V1 = 274.0, 385.0


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
    def quad(self, bl, br, tr, tl, uvr, centre):
        ids = [self.p(*v) for v in (bl, br, tr, tl)]
        (u0, v0, u1, v1) = uvr
        uv = [((u0 + 0.0) / SIZE, (v1) / SIZE), ((u1) / SIZE, (v1) / SIZE), ((u1) / SIZE, (v0) / SIZE), ((u0) / SIZE, (v0) / SIZE)]
        self.tri(ids[0], ids[1], ids[2], uv[0], uv[1], uv[2], centre)
        self.tri(ids[0], ids[2], ids[3], uv[0], uv[2], uv[3], centre)
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


rng_state = [12345]
def rnd(n):
    rng_state[0] = (rng_state[0] * 1103515245 + 12345) & 0x7fffffff
    return rng_state[0] % n

def rnd_uv(wpx, lpx):
    ua = U0 + rnd(int(max(1, (U1 - U0) - wpx)))
    va = V0 + rnd(int(max(1, (V1 - V0) - lpx)))
    return ua, va

def side(m, axis, fixed, a0, a1, y0, y1, centre):
    """Seitenflaeche: Saecke liegen waagerecht, die lange Achse des Sackmusters (Textur v) laeuft entlang der Mauer"""
    n = max(1, int(math.ceil((a1 - a0) / 30.0)))
    hpx = (y1 - y0) * PX
    for i in range(n):
        s0 = a0 + (a1 - a0) * i / n; s1 = a0 + (a1 - a0) * (i + 1) / n
        lpx = (s1 - s0) * PX
        ua, va = rnd_uv(hpx, lpx)
        uvs = [(ua, va), (ua, va + lpx), (ua + hpx, va + lpx), (ua + hpx, va)]       # bl, br, tr, tl
        if axis == "x":
            m.quad_px((s0, y0, fixed), (s1, y0, fixed), (s1, y1, fixed), (s0, y1, fixed), uvs, centre)
        else:
            m.quad_px((fixed, y0, s0), (fixed, y0, s1), (fixed, y1, s1), (fixed, y1, s0), uvs, centre)

def top(m, x0, x1, z0, z1, y1, centre, along):
    """Oberseite in quadratischen Feldern: Sackoberseiten liegen mit der langen Achse entlang der Mauer"""
    L = (x1 - x0) if along == "x" else (z1 - z0)
    Wd = (z1 - z0) if along == "x" else (x1 - x0)
    n = max(1, int(round(L / max(Wd, 1.0))))
    wpx = min(U1 - U0 - 1, Wd * PX)
    for i in range(n):
        if along == "x":
            s0 = x0 + L * i / n; s1 = x0 + L * (i + 1) / n; lpx = (s1 - s0) * PX
            ua, va = rnd_uv(wpx, lpx)
            m.quad_px((s0, y1, z1), (s1, y1, z1), (s1, y1, z0), (s0, y1, z0), [(ua, va), (ua, va + lpx), (ua + wpx, va + lpx), (ua + wpx, va)], centre)
        else:
            s0 = z0 + L * i / n; s1 = z0 + L * (i + 1) / n; lpx = (s1 - s0) * PX
            ua, va = rnd_uv(wpx, lpx)
            # bl=(x0,z1) -> tl=(x0,z0): entlang -z wachsendes v
            m.quad_px((x0, y1, s1), (x1, y1, s1), (x1, y1, s0), (x0, y1, s0), [(ua, va), (ua + wpx, va), (ua + wpx, va + lpx), (ua, va + lpx)], centre)

def rect(m, x0, x1, z0, z1, y0, y1, free, along="x"):
    """Quader ohne Boden; free = Seiten, die gezeichnet werden ('+x','-x','+z','-z')"""
    c = np.array([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])
    if "+x" in free: side(m, "z", x1, z0, z1, y0, y1, c)
    if "-x" in free: side(m, "z", x0, z0, z1, y0, y1, c)
    if "+z" in free: side(m, "x", z1, x0, x1, y0, y1, c)
    if "-z" in free: side(m, "x", z0, x0, x1, y0, y1, c)
    top(m, x0, x1, z0, z1, y1, c, along)

def build(arms):
    m = Model()
    for (y0, y1, t) in COURSES:
        free_c = {"+x", "-x", "+z", "-z"} - set(arms)
        rect(m, -t, t, -t, t, y0, y1, free_c)
        if "-x" in arms: rect(m, -LEN, -t, -t, t, y0, y1, {"+z", "-z", "-x"}, "x")
        if "+x" in arms: rect(m, t, LEN, -t, t, y0, y1, {"+z", "-z", "+x"}, "x")
        if "+z" in arms: rect(m, -t, t, t, LEN, y0, y1, {"+x", "-x", "+z"}, "z")
        if "-z" in arms: rect(m, -t, t, -LEN, -t, y0, y1, {"+x", "-x", "-z"}, "z")
    return m

MODELS = {"sbwlh.pie": ("-x", "+x"), "sbwlx.pie": ("-x", "+x", "+z", "-z"), "sbwlt.pie": ("-x", "+x", "+z"), "sbwll.pie": ("-x", "+z")}
os.makedirs(os.path.join(OUT, "structs"), exist_ok=True)
for name, arms in MODELS.items():
    m = build(arms)
    open(os.path.join(OUT, "structs", name), "w", encoding="latin-1", newline="").write(m.text())

# ------------------------------------------------------------ Stats
sp = os.path.join(OUT, "stats", "structure.json"); st = json.load(open(sp, encoding="utf-8"))
base = st["A0BaBaHorizontalWall"]
e = dict(base)
e.update({"id": "A0BaBaSandbagWall", "name": "Sandbag Wall", "structureModel": ["sbwlh.pie", "sbwlx.pie", "sbwlt.pie", "sbwll.pie"],
          "hitpoints": 200, "armour": 9, "thermal": 8, "buildPower": 14, "buildPoints": 14, "strength": "MEDIUM", "height": 2})
st["A0BaBaSandbagWall"] = e
json.dump(st, open(sp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
rp = os.path.join(OUT, "stats", "research.json"); rs = json.load(open(rp, encoding="utf-8"))
res = rs["R-Sys-Spade1Mk1"]["resultStructures"]
if "A0BaBaSandbagWall" not in res: res.append("A0BaBaSandbagWall")
json.dump(rs, open(rp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
print("Sandsack-Mauer: 4 Modelle, Struktur A0BaBaSandbagWall")
