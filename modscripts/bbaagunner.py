"""Scav-MG-Turm: Schuetze (3D-Infanterist in Originalgroesse x0,95) steht hinter dem MG auf einer kleinen Metallplattform mit Minitreppe,
Arme waagerecht nach vorn zum MG. Er haengt an der Halterung (bbaaturret.pie, dreht nur seitlich), NICHT am Waffenmodell, damit er
beim Heben/Senken des MG nicht mitkippt. Der winzige Original-Schuetze aus bbaamuzzle.pie wird entfernt. bbaagunner.py OUT"""
import sys, os, math
import numpy as np
from PIL import Image
OUT = sys.argv[1]
MP = "/tmp/claude-0/cmp/mp/components/weapons"
W = os.path.join(OUT, "components", "weapons", "bbaamuzzle.pie"); T = os.path.join(OUT, "components", "weapons", "bbaaturret.pie")
B = os.path.join(OUT, "components", "bodies")


def parse_levels(t):
    L = t.replace("\r", "").split("\n"); lv = []; i = 0
    while i < len(L):
        if L[i].startswith("LEVEL "):
            pts = []; pol = []
            j = i + 1
            while j < len(L) and not L[j].startswith("LEVEL "):
                if L[j].startswith("POINTS"):
                    n = int(L[j].split()[1]); pts = [tuple(map(float, x.split()[:3])) for x in L[j + 1:j + 1 + n]]
                if L[j].startswith("POLYGONS"):
                    n = int(L[j].split()[1]); pol = [x.split() for x in L[j + 1:j + 1 + n]]
                j += 1
            lv.append((pts, pol)); i = j
        else: i += 1
    return lv


def halve_u(pol):
    """Waffen-Atlas liegt in der linken Haelfte der 2048 breiten Seite -> u halbieren"""
    res = []
    for p in pol:
        c = int(p[1]); uv = list(p[2 + c:2 + 3 * c])
        for k in range(0, 2 * c, 2): uv[k] = "%.6f" % (float(uv[k]) / 2)
        res.append(p[:2 + c] + uv)
    return res


def components(pts, pol):
    par = list(range(len(pts)))
    def f(x):
        while par[x] != x: par[x] = par[par[x]]; x = par[x]
        return x
    for p in pol:
        idx = [int(v) for v in p[2:2 + int(p[1])]]
        for a in idx[1:]: par[f(a)] = f(idx[0])
    return f


# ---- Waffe (MG): Original ohne den kleinen Schuetzen und ohne die gelbe Sitzplatte
mz = parse_levels(open(os.path.join(MP, "bbaamuzzle.pie"), encoding="latin-1").read())[0]
mpts, mpol = mz
find = components(mpts, mpol)
cen = {}
for p in mpol:
    idx = [int(v) for v in p[2:2 + int(p[1])]]
    cen.setdefault(find(idx[0]), set()).update(idx)
drop = {r for r, vs in cen.items() if np.mean([mpts[v][2] for v in vs]) > 0.9}      # Schuetze (Torso, Kopf, Arme, Beine) und Sitzplatte liegen hinten
keep_pol = [p for p in mpol if find(int(p[2])) not in drop]
used = sorted({int(v) for p in keep_pol for v in p[2:2 + int(p[1])]}); remap = {o: n for n, o in enumerate(used)}
m_pts = [mpts[o] for o in used]
m_pol = []
for p in keep_pol:
    c = int(p[1]); m_pol.append(p[:2] + [str(remap[int(v)]) for v in p[2:2 + c]] + p[2 + c:])
m_pol = halve_u(m_pol)
out = ["PIE 3", "TYPE 200", "TEXTURE 0 page-420-bbaagun.png 0 0", "LEVELS 1", "LEVEL 1", "POINTS %d" % len(m_pts)]
out += ["\t%.4f %.4f %.4f" % p for p in m_pts] + ["POLYGONS %d" % len(m_pol)] + ["\t" + " ".join(p) for p in m_pol] + ["CONNECTORS 1", "\t0 -11 7", ""]
open(W, "w", encoding="latin-1", newline="").write("\n".join(out))

# ---- Halterung (dreht nur seitlich): Pfosten + Schuetze + Plattform + Treppe; Ursprung = Drehpunkt, Waffe sitzt 16 darueber
tp, tpol = parse_levels(open(os.path.join(MP, "bbaaturret.pie"), encoding="latin-1").read())[0]
pts = list(tp); pol = halve_u(tpol)
inf = parse_levels(open(os.path.join(B, "exbloke3d.pie"), encoding="latin-1").read())
S = 0.95
DECK = 19.0              # Turmdach (Oberkante der Turm-Box y=69) liegt 19 ueber dem Nullpunkt der Halterung (Turm-Anschluss y=50)
HAND_Z = 0.6             # Haende greifen an der Rueckseite des MG-Gehaeuses
FEET_WISH = DECK + 4.8   # Standflaeche des Schuetzen: 4,8 ueber dem Dach (kleine Plattform)
PLV_MIN_Y = (min(y for l in range(9) for _, y, _ in inf[l][0]) - 10.5) * S
# MG-Drehpunkt (Anschluss der Waffe an die Halterung) so waehlen, dass die Griffhoehe (Waffen-Nullpunkt + 4,5) = Schulterhoehe des stehenden Schuetzen ist
CONN_Y = int(round(FEET_WISH + ((15.0 - 10.5) * S - PLV_MIN_Y) - 4.5))
HAND_Y = CONN_Y + 4.5
YOFF = HAND_Y - (15.0 - 10.5) * S    # Schulterhoehe = Griffhoehe -> Arme waagerecht


def arm_tf(x, y, z, left):
    sx = -1.8 if left else 1.8
    th = math.radians(90 if left else -90); ph = math.radians(90)           # 90 Grad: Arm zeigt exakt waagerecht nach vorn
    rx, ry, rz = x - sx, y - 15.0, z
    x1 = rx * math.cos(th) - ry * math.sin(th); y1 = rx * math.sin(th) + ry * math.cos(th)
    y2 = y1 * math.cos(ph) + rz * math.sin(ph); z2 = -y1 * math.sin(ph) + rz * math.cos(ph)
    return x1 + sx, y2 + 15.0, z2


def place(zoff):
    res = []
    for li in range(9):
        ipts, _ = inf[li]
        for x, y, z in ipts:
            if li in (1, 2, 5, 6): x, y, z = arm_tf(x, y, z, li in (1, 2))
            res.append((-x * S, (y - 10.5) * S + YOFF, -z * S + zoff))
    return res


PLV = [l for l in range(9) for _ in inf[l][0]]          # Teil-Nummer je Punkt
probe = place(0.0)
arm_zmin = min(q[2] for l, q in zip(PLV, probe) if l in (1, 2, 5, 6))
ZOFF = HAND_Z - arm_zmin
g_pts = place(ZOFF)
feet_y = min(q[1] for q in g_pts)
foot_z0 = min(q[2] for l, q in zip(PLV, g_pts) if l in (3, 4, 7, 8))
foot_z1 = max(q[2] for l, q in zip(PLV, g_pts) if l in (3, 4, 7, 8))
base = len(pts); pts += g_pts
off = base
for li in range(9):
    ipts, ipol = inf[li]
    for p in ipol:
        c = int(p[1]); idx = [str(int(v) + off) for v in p[2:2 + c]][::-1]            # Spiegelung -> Windungsrichtung umdrehen
        uv = p[2 + c:2 + 3 * c]; pairs = [(float(uv[k]), float(uv[k + 1])) for k in range(0, 2 * c, 2)][::-1]
        pol.append([p[0], p[1]] + idx + sum([["%.6f" % ((1024 + u * 256) / 2048), "%.6f" % (v * 256 / 1024)] for u, v in pairs], []))
    off += len(ipts)


# ---- Plattform mit Minitreppe (Metall aus dem Waffen-Atlas: Grau fuer Waende, Dunkelgrau oben, Gelb fuer die Trittflaechen)
def texel(px, py): return ((px + 0.5) / 1024 / 2, (py + 0.5) / 1024)
UV_SIDE, UV_TOP, UV_TREAD = texel(210, 295), texel(213, 295), texel(69, 277)


def box(x0, x1, y0, y1, z0, z1, uv_top, uv_side):
    global pts
    b = len(pts)
    pts += [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), (x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)]
    c = np.array([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])
    faces = [((4, 5, 6, 7), uv_top), ((0, 1, 5, 4), uv_side), ((1, 2, 6, 5), uv_side), ((2, 3, 7, 6), uv_side), ((3, 0, 4, 7), uv_side)]
    for quad, uv in faces:
        P = [np.array(pts[b + i]) for i in quad]
        for tri in ((0, 1, 2), (0, 2, 3)):
            a, bb, cc = quad[tri[0]], quad[tri[1]], quad[tri[2]]
            n = np.cross(np.array(pts[b + bb]) - np.array(pts[b + a]), np.array(pts[b + cc]) - np.array(pts[b + a]))
            cen = (np.array(pts[b + a]) + np.array(pts[b + bb]) + np.array(pts[b + cc])) / 3 - c
            if n @ cen < 0: bb, cc = cc, bb                              # Aussenseite wie bei den Modellflaechen (Kreuzprodukt zeigt nach aussen)
            pol.append(["200", "3", str(b + a), str(b + bb), str(b + cc)] + ["%.6f" % uv[0], "%.6f" % uv[1]] * 3)


# Pfosten verlaengern: Deckel der Halterung (y=16) auf Drehpunkt-Hoehe ziehen -> Pfosten steht sichtbar ueber dem Dach
for i in range(4): pts[i] = (pts[i][0], float(CONN_Y), pts[i][2])
top = feet_y                          # Standflaeche = Fusssohle
bot = DECK - 0.25                     # ein Stueck ins Deck, keine Luecke
H = top - DECK
pz0, pz1 = foot_z0 - 2.2, foot_z1 + 2.2
box(-4.6, 4.6, bot, top, pz0, pz1, UV_TOP, UV_SIDE)
d = 2.3
box(-3.2, 3.2, bot, DECK + H * 2 / 3, pz1, pz1 + d, UV_TREAD, UV_SIDE)
box(-3.2, 3.2, bot, DECK + H * 1 / 3, pz1 + d, pz1 + 2 * d, UV_TREAD, UV_SIDE)

out = ["PIE 3", "TYPE 200", "TEXTURE 0 page-420-bbaagun.png 0 0", "LEVELS 1", "LEVEL 1", "POINTS %d" % len(pts)]
out += ["\t%.4f %.4f %.4f" % tuple(p) for p in pts] + ["POLYGONS %d" % len(pol)] + ["\t" + " ".join(p) for p in pol] + ["CONNECTORS 1", "\t0 0 %d" % CONN_Y, ""]
open(T, "w", encoding="latin-1", newline="").write("\n".join(out))

# Atlas: links Waffen-Atlas (1024x1024), rechts Infanterie (256x256, gelbe Hose)
a = Image.open("/tmp/claude-0/cmp/base/texpages/page-17-droid-weapons.png").convert("RGBA")
atl = Image.new("RGBA", (2048, 1024), (0, 0, 0, 0)); atl.paste(a, (0, 0)); sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import yelcol
atl.paste(yelcol.to_yellow(Image.open(os.path.join(OUT, "texpages", "page-32-punk.png")), yelcol.BUGGY_YELLOW, white_hair=False), (1024, 0))
atl.save(os.path.join(OUT, "texpages", "page-420-bbaagun.png"))
print("Schuetze am MG-Turm: MG %d Punkte, Halterung %d Punkte; Fuesse y=%.2f, Plattformhoehe %.2f, Treppe, Drehpunkt-Hoehe %d, ZOFF %.2f" % (len(m_pts), len(pts), feet_y, H, CONN_Y, ZOFF))
