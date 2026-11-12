"""Scav-Lkw: Ladeflaeche aus dem Systemturm (trlcon.pie) auf Scav-Rumpf-Teilen, drei Vorschlaege + ein grosser Raupen-Radlader.
  A  Scav-Pickup     kurzer Rahmen (Big-Rig-Fuehrerhaus, 4 Raeder) + Ladeflaeche
  B  Scav-Pritschen-Lkw   Big Rig ohne Kisten, lange Ladeflaeche
  C  Scav-Traktor    Traktor mit kleiner Hinterachse und Ladeflaeche hinten
  D  Scav-Radlader   grosses Raupenfahrwerk (Bagger-Fahrwerk x1.5), Fahrerhaus, Ladeschaufel an zwei Armen
Neue Dateien: components/bodies/scav{pick,lkw,trak,lader}.pie, components/weapons/scav{bed1,bed2,bed3,bucket}.pie
Stats: body.json (Body15..18TRK-Scav), construction.json (ScavBed1..3, ScavBucket), templates.json (ScavPickup, ScavLkw, ScavTraktor, ScavRadlader),
Freischaltung ueber research R-Sys-Spade1Mk1 (Start).
scavtruck.py OUT [PREVIEW.png]"""
import sys, os, json, math, copy
import numpy as np

OUT = sys.argv[1]
PREV = sys.argv[2] if len(sys.argv) > 2 else None
W_ = os.path.dirname(os.path.abspath(sys.argv[0]))
SRC_BODIES = os.path.join(W_, "out_u", "components", "bodies") if os.path.isdir(os.path.join(W_, "out_u")) else os.path.join(OUT, "components", "bodies")
P2, P411 = "page-2-crane.png", "page-411-scavcrane.png"
SIZE = 256


def uvpt(px, py): return ((px + 0.5) / SIZE, (py + 0.5) / SIZE)
def flat(rect): return uvpt((rect[0] + rect[2]) / 2.0, (rect[1] + rect[3]) / 2.0)


class Model:
    def __init__(self, page):
        self.pts = []; self.pol = []; self.page = page; self.conn = []
    def p(self, x, y, z):
        self.pts.append((float(x), float(y), float(z))); return len(self.pts) - 1
    def tri(self, a, b, c, uva, uvb, uvc, centre=None):
        if centre is not None:
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
    def box(self, x0, x1, y0, y1, z0, z1, side, top, front=None, back=None, full_front=False, full_side=False, full_all=False):
        if full_all: full_front = full_side = True
        c = np.array([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])
        fr = front or side; bk = back or side
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), bk, c, full_all)       # +z (hinten)
        self.quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), fr, c, full_front)  # -z (vorn)
        self.quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), side, c, full_side)
        self.quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), side, c, full_side)
        self.quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), top, c, full_all)
    def hexa(self, v, rect):
        """beliebiger Quader aus 8 Ecken: v[0..3] = vorderer Ring (z klein), v[4..7] = hinterer Ring, je Ring im Kreis herum"""
        c = np.mean(np.array(v, float), axis=0)
        for (a, b, d, e) in ((0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
            self.quad(v[a], v[b], v[d], v[e], rect, c, True)
    def add(self, pts, pols, dx=0, dy=0, dz=0, sx=1, sy=1, sz=1):
        """fertige Dreiecke (Index-Tripel + 3 UV) uebernehmen"""
        base = len(self.pts)
        for q in pts: self.p(q[0] * sx + dx, q[1] * sy + dy, q[2] * sz + dz)
        for (a, b, c, ua, ub, uc) in pols:
            if sx * sy * sz < 0: b, c, ub, uc = c, b, uc, ub
            self.pol.append((base + a, base + b, base + c, ua, ub, uc))
    def text(self, conn):
        out = ["PIE 3", "TYPE 200", "TEXTURE 0 %s 0 0" % self.page, "LEVELS 1", "LEVEL 1", "POINTS %d" % len(self.pts)]
        out += ["\t%.3f %.3f %.3f" % p for p in self.pts]
        out.append("POLYGONS %d" % len(self.pol))
        for a, b, c, ua, ub, uc in self.pol:
            out.append("\t200 3 %d %d %d %.6f %.6f %.6f %.6f %.6f %.6f" % (a, b, c, ua[0], ua[1], ub[0], ub[1], uc[0], uc[1]))
        out += ["CONNECTORS 1", "\t%g %g %g" % conn, ""]
        return "\n".join(out)


def load(path):
    t = open(path, encoding="latin-1").read().replace("\r", "").split("\n")
    pts, pol, dims = [], [], (1.0, 1.0); i = 0
    for l in t:
        w = l.split()
        if w and w[0] == "TEXTURE" and len(w) >= 5 and float(w[3]) > 0: dims = (float(w[3]), float(w[4]))
    while i < len(t):
        w = t[i].split()
        if w and w[0] == "POINTS":
            n = int(w[1]); pts = [[float(v) for v in t[i + 1 + k].split()[:3]] for k in range(n)]; i += n
        elif w and w[0] == "POLYGONS":
            n = int(w[1])
            for k in range(n):
                q = t[i + 1 + k].split(); c = int(q[1]); assert c == 3
                idx = [int(v) for v in q[2:5]]; uv = [float(v) for v in q[5:11]]
                uvs = [(uv[0] / dims[0], uv[1] / dims[1]), (uv[2] / dims[0], uv[3] / dims[1]), (uv[4] / dims[0], uv[5] / dims[1])]
                pol.append((idx[0], idx[1], idx[2], uvs[0], uvs[1], uvs[2]))
            i += n
        i += 1
    return np.array(pts), pol


def components(pts, pol):
    par = list(range(len(pts)))
    def f(x):
        while par[x] != x: par[x] = par[par[x]]; x = par[x]
        return x
    for (a, b, c, *_r) in pol: par[f(b)] = f(a); par[f(c)] = f(a)
    g = {}
    for k, (a, *_r) in enumerate(pol): g.setdefault(f(a), []).append(k)
    return sorted(g.values(), key=lambda lst: (min(pts[pol[k][0]][2] for k in lst), min(pts[pol[k][0]][0] for k in lst)))


def subset(pts, pol, keep):
    """Dreiecke mit Index in keep behalten, Punkte neu nummerieren"""
    used = sorted({v for k in keep for v in pol[k][:3]}); mp = {o: n for n, o in enumerate(used)}
    return [pts[o] for o in used], [(mp[pol[k][0]], mp[pol[k][1]], mp[pol[k][2]], pol[k][3], pol[k][4], pol[k][5]) for k in keep]


def comp_info(pts, pol, comps):
    for n, g in enumerate(comps):
        idx = sorted({v for k in g for v in pol[k][:3]}); p = pts[idx]
        print("   comp", n, len(g), "polys", p.min(0).round(0).tolist(), p.max(0).round(0).tolist())


def bed_from_trlcon(m, rect, sx=1.0, sy=1.0, sz=1.0, dx=0.0, dy=0.0, dz=0.0, yaw180=False):
    """Ladeflaeche = Systemturm-Modell trlcon.pie, mit Scav-Rostblech-Textur (Flaeche rect) neu belegt"""
    pts, pol = load(os.path.join(W_, "out_u", "components", "weapons", "trlcon.pie"))
    P = pts.copy() * np.array([sx, sy, sz])
    if yaw180: P[:, 0] *= -1; P[:, 2] *= -1
    # Projektionen: oben/unten (x,z), Seiten (z,y), vorn/hinten (x,y); je Projektion ueber alle Flaechen in die Textur einpassen
    proj = []
    for (a, b, c, *_r) in pol:
        n = np.cross(P[b] - P[a], P[c] - P[a]); ax = int(np.argmax(np.abs(n))); proj.append(ax)
    ranges = {}
    for k, (a, b, c, *_r) in enumerate(pol):
        ax = proj[k]; co = {0: (2, 1), 1: (0, 2), 2: (0, 1)}[ax]
        for v in (a, b, c):
            for j in (0, 1):
                lo, hi = ranges.get((ax, j), (1e9, -1e9)); val = P[v][co[j]]; ranges[(ax, j)] = (min(lo, val), max(hi, val))
    x0, y0, x1, y1 = rect
    npol = []
    for k, (a, b, c, *_r) in enumerate(pol):
        ax = proj[k]; co = {0: (2, 1), 1: (0, 2), 2: (0, 1)}[ax]; uv = []
        for v in (a, b, c):
            u_ = []
            for j in (0, 1):
                lo, hi = ranges[(ax, j)]; t = (P[v][co[j]] - lo) / max(hi - lo, 1e-6)
                u_.append(t)
            uv.append(uvpt(x0 + 1 + u_[0] * (x1 - x0 - 2), y0 + 1 + (1 - u_[1]) * (y1 - y0 - 2)))
        npol.append((a, b, c, uv[0], uv[1], uv[2]))
    m.add(P, npol, dx, dy, dz)


def body_from_truck(page=P2):
    pts, pol = load(os.path.join(SRC_BODIES, "scavtruck.pie"))
    return pts, pol, components(pts, pol)


# ---------------- A  Pickup / B  Lkw ----------------
PLATE2 = (36, 86, 72, 126)    # rostiges Blech auf page-2-crane (Pixel)
def build_truck(kind):
    pts, pol, comps = body_from_truck()
    # Komponentenreihenfolge nach (kleinstes z, kleinstes x); ueber bbox die gesuchten finden
    def find(lo, hi, cond):
        res = []
        for n, g in enumerate(comps):
            idx = sorted({v for k in g for v in pol[k][:3]}); p = pts[idx]; mn, mx = p.min(0), p.max(0)
            if cond(mn, mx): res.append(n)
        return res
    cab = find(0, 0, lambda a, b: a[2] < -70 and b[1] > 30)
    hood = find(0, 0, lambda a, b: a[2] < -90 and b[1] < 25 and b[0] - a[0] > 15)
    fw = find(0, 0, lambda a, b: a[2] < -90 and b[0] - a[0] < 8)
    frame = find(0, 0, lambda a, b: a[1] > 5 and b[1] < 12 and a[2] < -40 and b[2] > -15 and b[0] - a[0] > 20)
    deck = find(0, 0, lambda a, b: b[2] > 70 and b[0] - a[0] > 30 and b[1] < 20)
    crates = find(0, 0, lambda a, b: a[1] > 18 and b[1] > 30 and b[0] - a[0] > 20)
    wheels = find(0, 0, lambda a, b: b[0] - a[0] < 8 and b[1] - a[1] > 12)
    mid = [n for n in wheels if all(pts[v][2] < 0 for k in comps[n] for v in pol[k][:3]) and n not in fw]
    rear = [n for n in wheels if all(pts[v][2] > 30 for k in comps[n] for v in pol[k][:3])]
    keep = set(cab + hood + fw + frame)
    m = Model(P2); pc = None
    if kind == "pick":
        # nur hintere der beiden Mittelachsen-Raeder (z -23..-8) behalten
        mid_r = [n for n in mid if min(pts[v][2] for k in comps[n] for v in pol[k][:3]) > -30]
        keep |= set(mid_r) | set(deck)
        k_idx = [k for n in sorted(keep) for k in comps[n]]
        sp, sq = subset(pts, pol, k_idx)
        sp = [list(q) for q in sp]
        # Deck kuerzen: alle Punkte mit z > 20 auf z = -3 (nur Deck-Quader hat so weit hinten Punkte)
        sp = [[q[0], q[1], (-3.0 if q[2] > 20 else q[2])] for q in sp]
        m.add(sp, sq)
        conn = (0, -22, 18); bed = dict(sx=1.12, sy=1.0, sz=1.0)
    else:
        keep |= set(mid) | set(rear) | set(deck)
        k_idx = [k for n in sorted(keep) for k in comps[n]]
        sp, sq = subset(pts, pol, k_idx)
        m.add(sp, sq)
        conn = (0, 17, 18); bed = dict(sx=1.15, sy=1.0, sz=2.4)
    return m, conn, bed


def beds_turret(bed, conn):
    t = Model(P2)
    bed_from_trlcon(t, PLATE2, **bed)
    return t


# ---------------- C  Traktor ----------------
def build_tractor():
    pts, pol = load(os.path.join(SRC_BODIES, "tractor.pie")); comps = components(pts, pol)
    m = Model(P2); m.add(pts, pol)
    # Hinterachse: zwei kleine Vorderraeder (z -41..-29) nach hinten kopieren
    small = []
    for n, g in enumerate(comps):
        idx = sorted({v for k in g for v in pol[k][:3]}); p = pts[idx]
        if p.max(0)[2] < -28 and p.min(0)[2] > -45 and p.max(0)[1] < 14: small.append(n)
    for n in small:
        sp, sq = subset(pts, pol, comps[n]); m.add(sp, sq, dz=88)
    # Rahmen unter der Ladeflaeche
    c = np.array([0, 10, 38.0])
    m.box(-7, 7, 8, 12, 14, 62, (6, 176, 90, 230), (6, 176, 90, 230))
    return m, (0, 38, 13), dict(sx=1.0, sy=1.0, sz=1.15)


# ---------------- D  Radlader ----------------
RUST411 = (4, 175, 96, 231)
WIN411 = (3, 70, 35, 111)
def build_loader():
    pts, pol = load(os.path.join(SRC_BODIES, "cranebody2.pie"))
    m = Model(P411); m.add(pts, pol, sx=1.5, sy=1.5, sz=1.5)
    # Fahrerhaus hinten, Motorhaube davor, Gegengewicht, Auspuff
    m.box(-13, 13, 24, 52, 12, 38, RUST411, RUST411, front=WIN411, full_front=True, full_side=True)
    m.box(-18, 18, 24, 36, -18, 12, RUST411, RUST411, full_all=True)
    m.box(-28, 28, 24, 42, 38, 58, RUST411, RUST411, full_all=True)
    m.box(9, 13, 52, 66, 30, 34, (2, 2, 30, 30), (2, 2, 30, 30))
    conn = (0, 25, 52)
    # Schaufel-Turm: im Koerper-Koordinatensystem aufbauen, dann um den Anschluss verschieben (siehe unten)
    t = Model(P411)
    ox, oy, oz = 0.0, 52.0, 25.0     # Anschluss (x, y, z) im Koerper
    def S(x, y, z): return (x - ox, y - oy, z - oz)
    for sgn in (-1, 1):
        xa, xb = (30, 36) if sgn > 0 else (-36, -30)
        v = [S(xa, 26, 6), S(xb, 26, 6), S(xb, 36, 6), S(xa, 36, 6),         # vorderer Ring (z klein) -> hier der hintere Drehpunkt, siehe unten
             S(xa, 10, -62), S(xb, 10, -62), S(xb, 20, -62), S(xa, 20, -62)]
        # Ringe: vorn = z klein = Bucket-Ende; wir bauen Strebe von z=-62 (vorn) nach z=6 (hinten)
        v = [S(xa, 10, -62), S(xb, 10, -62), S(xb, 20, -62), S(xa, 20, -62),
             S(xa, 26, 6), S(xb, 26, 6), S(xb, 38, 6), S(xa, 38, 6)]
        t.hexa(v, RUST411)
    # Querstrebe hinten + Zylinder-Stangen
    t.box(*(-36 - ox + 0, 36 - ox, 30 - oy, 40 - oy, 2 - oz, 8 - oz), side=RUST411, top=RUST411)
    bed_from_trlcon(t, RUST411, sx=2.0, sy=1.7, sz=1.4, dx=S(0, 0, -62)[0], dy=S(0, 4, 0)[1], dz=S(0, 0, -62)[2])
    return m, conn, t


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w", encoding="latin-1", newline="").write(text)


# ---------------- E  Tank-Lkw (Oeltransporter) ----------------
TANK_SKIN = (86, 40, 154, 116)       # grosse rostige Platte
TANK_BAND = (125, 153, 140, 175)     # gelbe Farbe
TANK_CAP = (205, 200, 240, 235)      # rostige Nabe mit Schrauben
def cyl_z(m, cx, cy, r, z0, z1, n, rect, cap=None):
    c = np.array([cx, cy, (z0 + z1) / 2.0])
    ang = [2 * math.pi * (i + 0.5) / n for i in range(n)]
    for i in range(n):
        a0, a1 = ang[i], ang[(i + 1) % n]
        p0 = (cx + r * math.cos(a0), cy + r * math.sin(a0)); p1 = (cx + r * math.cos(a1), cy + r * math.sin(a1))
        m.quad((p0[0], p0[1], z1), (p1[0], p1[1], z1), (p1[0], p1[1], z0), (p0[0], p0[1], z0), rect, c)
    if cap is not None:
        for zz in (z0, z1):
            for i in range(n):
                a0, a1 = ang[i], ang[(i + 1) % n]
                ids = [m.p(cx, cy, zz), m.p(cx + r * math.cos(a0), cy + r * math.sin(a0), zz), m.p(cx + r * math.cos(a1), cy + r * math.sin(a1), zz)]
                m.tri(ids[0], ids[1], ids[2], flat(cap), flat(cap), flat(cap), c)

def build_tank_bed():
    t = Model(P2)
    bed_from_trlcon(t, PLATE2, sx=1.15, sy=1.0, sz=2.4)          # gleiche Pritsche wie der Pritschen-Lkw
    cy = 10.0 + 15.0                                              # Tankachse ueber der Pritsche
    cyl_z(t, 0.0, cy, 15.0, -42.0, 40.0, 10, TANK_SKIN, TANK_CAP)
    for zz in (-30.0, 0.0, 30.0):                                  # gelbe Spannbaender
        cyl_z(t, 0.0, cy, 16.2, zz - 2.5, zz + 2.5, 10, TANK_BAND)
    t.box(-4, 4, cy + 14.0, cy + 19.0, -14, -6, PLATE2, TANK_CAP)   # Einstiegsluke oben
    t.box(-2.0, 2.0, cy + 19.0, cy + 21.0, -12, -8, TANK_BAND, TANK_BAND)
    t.box(-14, -12, 10, 36, 42, 44, PLATE2, PLATE2)                # Leiter hinten (zwei Holme)
    t.box(12, 14, 10, 36, 42, 44, PLATE2, PLATE2)
    for yy in (16.0, 22.0, 28.0, 34.0):
        t.box(-12, 12, yy, yy + 1.5, 42, 44, PLATE2, PLATE2)
    t.box(15.5, 19.0, 12, 16, -30, -12, TANK_CAP, TANK_CAP)         # Schlauchtrommel seitlich
    return t


def main():
    comp_b = os.path.join(OUT, "components", "bodies"); comp_w = os.path.join(OUT, "components", "weapons")
    parts = {}
    m, conn, bed = build_truck("pick"); write(os.path.join(comp_b, "scavpick.pie"), m.text(conn)); write(os.path.join(comp_w, "scavbed1.pie"), beds_turret(bed, conn).text((0, 0, 0))); parts["A"] = (m, conn, beds_turret(bed, conn))
    m, conn, bed = build_truck("lkw"); write(os.path.join(comp_b, "scavlkw.pie"), m.text(conn)); write(os.path.join(comp_w, "scavbed2.pie"), beds_turret(bed, conn).text((0, 0, 0))); parts["B"] = (m, conn, beds_turret(bed, conn))
    m, conn, bed = build_tractor(); write(os.path.join(comp_b, "scavtrak.pie"), m.text(conn)); write(os.path.join(comp_w, "scavbed3.pie"), beds_turret(bed, conn).text((0, 0, 0))); parts["C"] = (m, conn, beds_turret(bed, conn))
    m, conn, t = build_loader(); write(os.path.join(comp_b, "scavlader.pie"), m.text(conn)); write(os.path.join(comp_w, "scavbucket.pie"), t.text((0, 0, 0))); parts["D"] = (m, conn, t)
    m, conn, bed = build_truck("lkw"); tk = build_tank_bed(); write(os.path.join(comp_w, "scavtank.pie"), tk.text((0, 0, 0))); parts["E"] = (m, conn, tk)
    # ---- Stats ----
    sp = os.path.join(OUT, "stats")
    B = json.load(open(os.path.join(sp, "body.json"), encoding="utf-8"))
    base = B["Body14SUP-Scav"]
    def body(bid, name, model, size, weight, hp, bp, power, ak, ah):
        v = copy.deepcopy(base); v.update({"id": bid, "name": name, "model": model, "size": size, "weight": weight, "hitpoints": hp, "buildPoints": bp,
                                           "buildPower": power, "armourKinetic": ak, "armourHeat": ah, "weaponSlots": 1, "powerOutput": 9000 if size != "HEAVY" else 22000, "designable": 1})
        v.pop("propulsionExtraModels", None); B[bid] = v
    body("Body15TRK-Scav", "Pickup", "scavpick.pie", "MEDIUM", 1400, 120, 90, 30, 14, 10)
    body("Body16TRK-Scav", "Pritschen-Lkw", "scavlkw.pie", "HEAVY", 3200, 240, 160, 50, 24, 18)
    body("Body17TRK-Scav", "Traktor", "scavtrak.pie", "MEDIUM", 2200, 180, 120, 40, 18, 14)
    body("Body18TRK-Scav", "Radlader", "scavlader.pie", "HEAVY", 5200, 420, 260, 80, 30, 22)
    json.dump(B, open(os.path.join(sp, "body.json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
    C = json.load(open(os.path.join(sp, "construction.json"), encoding="utf-8"))
    def cons(cid, name, model, cp, hp, wt):
        v = copy.deepcopy(C["scavCrane2"]); v.update({"id": cid, "name": name, "sensorModel": model, "constructPoints": cp, "hitpoints": hp, "weight": wt, "designable": 1}); C[cid] = v
    cons("ScavBed1", "Ladeflaeche klein", "scavbed1.pie", 10, 40, 500)
    cons("ScavBed2", "Ladeflaeche lang", "scavbed2.pie", 12, 55, 900)
    cons("ScavBed3", "Ladeflaeche Traktor", "scavbed3.pie", 11, 45, 700)
    cons("ScavBucket", "Ladeschaufel", "scavbucket.pie", 16, 80, 1400)
    cons("ScavTank", "Tankaufbau", "scavtank.pie", 12, 60, 1100)
    json.dump(C, open(os.path.join(sp, "construction.json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
    T = json.load(open(os.path.join(sp, "templates.json"), encoding="utf-8"))
    def tpl(tid, name, body_, cons_, prop):
        T[tid] = {"id": tid, "name": name, "available": True, "body": body_, "construct": cons_, "propulsion": prop, "type": "CONSTRUCT"}
    tpl("ScavPickup", "Scav-Pickup", "Body15TRK-Scav", "ScavBed1", "wheeled01-Scav")
    tpl("ScavLkw", "Scav-Pritschen-Lkw", "Body16TRK-Scav", "ScavBed2", "wheeled01-Scav")
    tpl("ScavTraktor", "Scav-Traktor", "Body17TRK-Scav", "ScavBed3", "wheeled01-Scav")
    tpl("ScavRadlader", "Scav-Radlader", "Body18TRK-Scav", "ScavBucket", "tracked01-Scav")
    tpl("ScavTanker", "Scav-Oeltransporter", "Body16TRK-Scav", "ScavTank", "wheeled01-Scav")
    json.dump(T, open(os.path.join(sp, "templates.json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
    R = json.load(open(os.path.join(sp, "research.json"), encoding="utf-8"))
    rc = R["R-Sys-Spade1Mk1"].setdefault("resultComponents", [])
    for k in ("Body15TRK-Scav", "Body16TRK-Scav", "Body17TRK-Scav", "Body18TRK-Scav", "ScavBed1", "ScavBed2", "ScavBed3", "ScavBucket", "ScavTank"):
        if k not in rc: rc.append(k)
    json.dump(R, open(os.path.join(sp, "research.json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Scav-Lkw: 4 Fahrzeuge (Pickup, Pritschen-Lkw, Traktor, Radlader) gebaut")
    if PREV: preview(parts, PREV)


def preview(parts, path):
    sys.path.insert(0, W_)
    import pieview
    from PIL import Image, ImageDraw
    texs = {}
    for pg in (P2, P411):
        f = "/tmp/pv/all/texpages/" + pg
        texs[pg] = np.array(Image.open(f).convert("RGBA"))
    tiles = []
    for key in "ABCDE":
        body, conn, turret = parts[key]
        # Anschluss (x z y) -> Verschiebung
        ox, oz, oy = conn
        pts = [list(q) for q in body.pts]; pol = [(a, b, c, ua, ub, uc) for (a, b, c, ua, ub, uc) in body.pol]
        n0 = len(pts)
        for q in turret.pts: pts.append([q[0] + ox, q[1] + oy, q[2] + oz])
        polys = [([a, b, c], [ua, ub, uc]) for (a, b, c, ua, ub, uc) in pol] + [([a + n0, b + n0, c + n0], [ua, ub, uc]) for (a, b, c, ua, ub, uc) in turret.pol]
        tex = texs[body.page]
        row = []
        for yaw, pitch in ((35, -28), (215, -28), (90, -12)):
            row.append(pieview.render(np.array(pts), polys, tex, yaw=yaw, pitch=pitch, size=380))
        tiles.append(row)
    W = Image.new("RGB", (380 * 3, 380 * 5), (40, 44, 70)); d = ImageDraw.Draw(W)
    lab = {"A": "A  Scav-Pickup", "B": "B  Scav-Pritschen-Lkw", "C": "C  Scav-Traktor", "D": "D  Scav-Radlader (gross)", "E": "E  Scav-Oeltransporter (Tank-Lkw)"}
    for r, key in enumerate("ABCDE"):
        for c in range(3): W.paste(tiles[r][c], (c * 380, r * 380))
        d.text((8, r * 380 + 6), lab[key], fill=(255, 255, 255))
    W.save(path)


main()
