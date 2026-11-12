"""Oelpipeline (Scav): ziehbar wie eine Mauer (Typ WALL: gerade, Kreuz, T, Ecke), dient als Verbindung zwischen Oeltuermen und Raffinerien.
Mesh nach den blauen Pipeline-Features (PipeFull / PipeEnd / PipeCollapsed: 128 lang, Achteck, Rohrmitte auf Hoehe 41, zwei Staender, Bodenplatte),
Textur der grauen Rost-Pipeline (page-31-features-urban, Streifen x 346..548, y 970..1022).
Zerstoerte Varianten (je Form) fuer spaeter: Explosion + schwarzer Rauch, wieder reparierbar (Engine-Teil kommt spaeter).
pipeline.py OUT"""
import sys, os, json, math
import numpy as np

OUT = sys.argv[1]
PAGE = "page-31-features-urban.png"
SIZE = 1024.0
LEN = 64.0
YC = 41.0          # Rohrmitte
R = 19.0           # Rohrradius (Achteck, Umkreis)

PIPE = (352, 972, 468, 1018)        # grauer Rost-Streifen, oben hell, unten dunkel
DARK = (352, 1004, 468, 1020)       # dunkler Streifen
RUSTEND = (535, 975, 546, 998)      # Rostkante am Rohrende
BLACK = (602, 978, 618, 994)        # schwarz (Innenraum, Oellache)


class Model:
    def __init__(self):
        self.pts = []; self.pol = []; self.conn = []
    def p(self, v):
        self.pts.append(tuple(float(c) for c in v)); return len(self.pts) - 1
    def tri(self, a, b, c, ua, ub, uc, centre):
        A, B, C = (np.array(self.pts[i]) for i in (a, b, c))
        n = np.cross(B - A, C - A)
        if n @ ((A + B + C) / 3.0 - centre) < 0: b, c, ub, uc = c, b, uc, ub
        self.pol.append((a, b, c, ua, ub, uc))
    def quad(self, v, uvs, centre):
        ids = [self.p(x) for x in v]
        uv = [(u / SIZE, w / SIZE) for (u, w) in uvs]
        self.tri(ids[0], ids[1], ids[2], uv[0], uv[1], uv[2], centre)
        self.tri(ids[0], ids[2], ids[3], uv[0], uv[2], uv[3], centre)
    def poly(self, v, rect, centre):
        """Flaeche mit einem kleinen Ausschnitt gefuellt (Fan aus Dreiecken, alle Ecken auf derselben Texelmitte)"""
        ids = [self.p(x) for x in v]
        u = ((rect[0] + rect[2]) / 2.0 / SIZE, (rect[1] + rect[3]) / 2.0 / SIZE)
        for i in range(1, len(ids) - 1): self.tri(ids[0], ids[i], ids[i + 1], u, u, u, centre)
    def text(self):
        out = ["PIE 3", "TYPE 200", "TEXTURE 0 %s 0 0" % PAGE, "LEVELS 1", "LEVEL 1", "POINTS %d" % len(self.pts)]
        out += ["\t%.3f %.3f %.3f" % p for p in self.pts]
        out.append("POLYGONS %d" % len(self.pol))
        for a, b, c, ua, ub, uc in self.pol:
            out.append("\t200 3 %d %d %d %.6f %.6f %.6f %.6f %.6f %.6f" % (a, b, c, ua[0], ua[1], ub[0], ub[1], uc[0], uc[1]))
        out += ["CONNECTORS %d" % max(1, len(self.conn))]
        out += ["\t%d %d %d" % c for c in (self.conn or [(0, 0, 60)])]
        out.append("")
        return "\n".join(out)


def prism(m, A, B, r, tex=PIPE, cap_a=False, cap_b=True, shade=True):
    """Achteck-Rohr von A nach B (beliebig geneigt). Seiten mit Hell-Dunkel-Verlauf (oben hell, unten dunkel), Kappen als dunkle Flaechen."""
    A = np.array(A, float); B = np.array(B, float)
    d = B - A; L = np.linalg.norm(d); d = d / L
    up = np.array([0.0, 1.0, 0.0])
    right = np.cross(d, up)
    right = right / np.linalg.norm(right) if np.linalg.norm(right) > 1e-6 else np.array([1.0, 0.0, 0.0])
    up2 = np.cross(right, d)
    centre = (A + B) / 2
    ang = [math.radians(22.5 + 45 * k) for k in range(8)]
    ra = [A + r * (math.cos(a) * right + math.sin(a) * up2) for a in ang]
    rb = [B + r * (math.cos(a) * right + math.sin(a) * up2) for a in ang]
    u0, v0, u1, v1 = tex
    lw = u1 - u0
    nseg = max(1, int(round(L / 40.0)))
    for k in range(8):
        k2 = (k + 1) % 8
        # Winkel vom Scheitel (oben = 90 Grad): 0 = hell, 180 = dunkel
        def vv(a):
            t = (1 - math.sin(a)) / 2.0                         # 0 oben, 1 unten
            return v0 + t * (v1 - v0) if shade else (v0 + v1) / 2
        va, vb = vv(ang[k]), vv(ang[k2])
        for s in range(nseg):
            f0, f1 = s / nseg, (s + 1) / nseg
            pa0 = ra[k] + (rb[k] - ra[k]) * f0; pa1 = ra[k] + (rb[k] - ra[k]) * f1
            pb0 = ra[k2] + (rb[k2] - ra[k2]) * f0; pb1 = ra[k2] + (rb[k2] - ra[k2]) * f1
            ua0 = u0 + lw * (0.05 + 0.9 * f0 * 0.9); ua1 = u0 + lw * (0.05 + 0.9 * f1 * 0.9 + 0.0)
            m.quad([pa0, pa1, pb1, pb0], [(ua0, va), (ua1, va), (ua1, vb), (ua0, vb)], centre)
    if cap_a: m.poly(ra, BLACK, centre)
    if cap_b: m.poly(rb, BLACK, centre)


def flange(m, A, B):
    prism(m, A, B, R + 2.5, tex=DARK, cap_a=False, cap_b=False, shade=False)


def slab(m, x0, x1, z0, z1, y=2.5):
    c = np.array([(x0 + x1) / 2, y - 1.0, (z0 + z1) / 2])
    m.poly([(x0, y, z1), (x1, y, z1), (x1, y, z0), (x0, y, z0)], DARK, c)


def post(m, cx, cz, y1, lean=(0.0, 0.0)):
    """Staender: schmaler Quader von der Bodenplatte zum Rohr"""
    w = 3.2
    pts = [(cx - w, 2.5, cz - w), (cx + w, 2.5, cz - w), (cx + w, 2.5, cz + w), (cx - w, 2.5, cz + w)]
    top = [(x + lean[0], y1, z + lean[1]) for (x, _, z) in pts]
    c = np.array([cx, y1 / 2, cz])
    for i in range(4):
        j = (i + 1) % 4
        m.poly([pts[i], pts[j], top[j], top[i]], DARK, c)


def wheel(m, cx, cz, y):
    """Ventilrad auf der Verteilerbox: Stiel + flacher Ring"""
    m.poly([(cx - 1.6, y, cz - 1.6), (cx + 1.6, y, cz - 1.6), (cx + 1.6, y + 9, cz - 1.6), (cx - 1.6, y + 9, cz - 1.6)], DARK, np.array([cx, y + 4, cz]))
    prism(m, (cx, y + 9, cz), (cx, y + 11.5, cz), 9.5, tex=DARK, cap_a=False, cap_b=True, shade=False)


def cube(m, cx, cz, h=20.0):
    """Verteilerbox in Rohrhoehe (Junction), Seiten mit dem Rost-Streifen (oben hell, unten dunkel)"""
    x0, x1, z0, z1, y0, y1 = cx - h, cx + h, cz - h, cz + h, YC - 20, YC + 20
    c = np.array([cx, YC, cz])
    u0, v0, u1, v1 = PIPE
    sides = ([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)],
             [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)])
    for i, v in enumerate(sides):
        ua = u0 + 8 + i * 24
        m.quad(v, [(ua, v1), (ua + 22, v1), (ua + 22, v0), (ua, v0)], c)
    m.quad([(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], [(u0 + 4, v0), (u0 + 48, v0), (u0 + 48, v0 + 8), (u0 + 4, v0 + 8)], c)
    wheel(m, cx, cz, YC + 20)


DIRS = {"-x": (-1, 0), "+x": (1, 0), "-z": (0, -1), "+z": (0, 1)}


def pt(d, s, y):
    dx, dz = DIRS[d]; return (dx * s, y, dz * s)


def arm_intact(m, d, J):
    dx, dz = DIRS[d]
    prism(m, pt(d, J, YC), pt(d, LEN, YC), R, cap_b=True)
    flange(m, pt(d, LEN - 4.0, YC), pt(d, LEN, YC))
    flange(m, pt(d, J, YC), pt(d, J + 4.0, YC)) if J else None
    # Bodenplatte + Staender
    if dx: slab(m, min(J, LEN - 2) * dx if dx > 0 else -(LEN - 2), (LEN - 2) * dx if dx > 0 else -J, -11, 11)
    else: slab(m, -11, 11, min(J, LEN - 2) * dz if dz > 0 else -(LEN - 2), (LEN - 2) * dz if dz > 0 else -J)
    ps = 44.0 if J else 28.0
    post(m, dx * ps, dz * ps, YC - R + 1.0)


BURNT = (352, 992, 468, 1018)       # dunkle, verbrannte Haelfte des Rost-Streifens


def arm_broken(m, d, J):
    """Rohr ist in der Mitte abgerissen und eingeknickt: Innenstueck sackt ab, Aussenstueck liegt mit dem Ende am Boden. Dazwischen eine Oellache."""
    dx, dz = DIRS[d]
    s_gap0 = (J + 14.0) if J else 0.0
    if J:
        prism(m, pt(d, J, YC), pt(d, s_gap0, YC - 7.0), R, tex=BURNT, cap_b=True)
        prism(m, pt(d, s_gap0 - 1.0, YC - 7.0), pt(d, s_gap0 + 1.0, YC - 7.5), R + 1.4, tex=RUSTEND, cap_a=False, cap_b=False, shade=False)   # aufgerissene Kante
        s_in = J + 20.0
    else:
        s_in = 4.0
    prism(m, pt(d, s_in, YC - 16.0), pt(d, LEN, YC), R, tex=BURNT, cap_a=True, cap_b=True)
    prism(m, pt(d, s_in, YC - 16.0), pt(d, s_in + 1.5, YC - 15.5), R + 1.4, tex=RUSTEND, cap_a=False, cap_b=False, shade=False)
    flange(m, pt(d, LEN - 4.0, YC), pt(d, LEN, YC))
    px, pz = dx * (s_in - 3), dz * (s_in - 3)
    ring = [(px + 17 * math.cos(math.radians(a)), 2.7, pz + 17 * math.sin(math.radians(a))) for a in range(0, 360, 45)]
    m.poly(ring, BLACK, np.array([px, 0.0, pz]))
    post(m, dx * 50, dz * 50, YC - R - 3, lean=(3.5 * dx + 4 * (dz != 0), 3.5 * dz + 4 * (dx != 0)))


def build(arms, broken):
    m = Model()
    J = 0.0 if len(arms) == 2 and (("-x" in arms and "+x" in arms) or ("-z" in arms and "+z" in arms)) else 20.0
    if J: cube(m, 0.0, 0.0)
    for a in arms: (arm_broken if broken else arm_intact)(m, a, J)
    # Effektpunkt (Rauch/Feuer) fuer die spaetere Engine: Mitte, Rohrhoehe
    m.conn = [(0, 0, int(YC) + 20)]
    return m


MODELS = {"h": ("-x", "+x"), "x": ("-x", "+x", "+z", "-z"), "t": ("-x", "+x", "+z"), "l": ("-x", "+z")}
os.makedirs(os.path.join(OUT, "structs"), exist_ok=True)
for key, arms in MODELS.items():
    for broken in (False, True):
        m = build(arms, broken)
        name = "pipe_%s%s.pie" % (key, "_b" if broken else "")
        open(os.path.join(OUT, "structs", name), "w", encoding="latin-1", newline="").write(m.text())

sp = os.path.join(OUT, "stats", "structure.json"); st = json.load(open(sp, encoding="utf-8"))
base = st["A0BaBaHorizontalWall"]
e = dict(base)
e.update({"id": "A0BaBaPipeline", "name": "Oil Pipeline", "structureModel": ["pipe_h.pie", "pipe_x.pie", "pipe_t.pie", "pipe_l.pie"],
          "hitpoints": 260, "armour": 6, "thermal": 3, "buildPower": 16, "buildPoints": 14, "strength": "MEDIUM", "height": 2})
st["A0BaBaPipeline"] = e
b = dict(e)
b.update({"id": "A0BaBaPipelineBroken", "name": "Broken Oil Pipeline", "structureModel": ["pipe_h_b.pie", "pipe_x_b.pie", "pipe_t_b.pie", "pipe_l_b.pie"], "hitpoints": 120})
st["A0BaBaPipelineBroken"] = b
json.dump(st, open(sp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
rp = os.path.join(OUT, "stats", "research.json"); rs = json.load(open(rp, encoding="utf-8"))
res = rs["R-Sys-Spade1Mk1"]["resultStructures"]
if "A0BaBaPipeline" not in res: res.append("A0BaBaPipeline")
json.dump(rs, open(rp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
print("Pipeline: 8 Modelle (4 Formen x intakt/zerstoert), Struktur A0BaBaPipeline + A0BaBaPipelineBroken (nicht baubar)")
