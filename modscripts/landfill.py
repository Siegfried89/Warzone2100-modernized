"""Landfill: grosses Deko-Feature aus den vorhandenen Autowracks und Schrotthaufen, ringsum von Scav-Mauern umschlossen.
Drei Versionen (4 = 2x2, 6 = 2x3, 9 = 3x3 Kacheln). Haufen, teils mehrlagig gestapelte Autos, Kreiswege.
landfill.py OUT   -> OUT/features/landfill{4,6,9}.pie, OUT/texpages/page-430-landfill.png, Eintraege in OUT/stats/features.json"""
import sys, os, re, math, json, random
import numpy as np
from PIL import Image

OUT = sys.argv[1]
SRC = ["/tmp/claude-0/cmp/base", "/tmp/claude-0/cmp/mp"]
FEAT = "/home/claude/w/feat/features"
TILE = 128.0
PAGES = ["page-6.png", "page-7-barbarians-arizona.png", "page-29-features-arizona.png", "page-31-features-urban.png", "page-16-droid-drives.png", "page-1-icevan.png", "page-2-crane.png"]
SLOTS = {p: i for i, p in enumerate(PAGES)}          # 4 x 2 Plaetze zu je 512 Pixeln -> Atlas 2048 x 1024


def parse(path):
    t = open(path, encoding="latin-1").read().replace("\r", "").split("\n")
    page, tw, th = None, 0, 0
    pts, pol = [], []
    for i, l in enumerate(t):
        w = l.split()
        if not w: continue
        if w[0] == "TEXTURE" and page is None:
            page = w[2]; tw, th = float(w[3]), float(w[4])
        elif w[0] == "POINTS":
            n = int(w[1]); pts = [tuple(float(v) for v in t[i + 1 + k].split()[:3]) for k in range(n)]
        elif w[0] == "POLYGONS":
            n = int(w[1])
            for k in range(n):
                v = t[i + 1 + k].split(); c = int(v[1])
                idx = [int(x) for x in v[2:2 + c]]
                uv = [float(x) for x in v[2 + c:2 + 3 * c]]
                if tw and th: uv = [x / (tw if j % 2 == 0 else th) for j, x in enumerate(uv)]
                pol.append((int(v[0]), idx, [(uv[2 * j], uv[2 * j + 1]) for j in range(c)]))
    return page, np.array(pts), pol


MODELS = {}
def model(name, path):
    if name not in MODELS: MODELS[name] = parse(path)
    return MODELS[name]


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


class Builder:
    def __init__(self):
        self.pts = []; self.pol = []
    def add(self, name, path, pos, yaw=0.0, scale=1.0, pitch=0.0, roll=0.0):
        page, pts, pol = model(name, path)
        slot = SLOTS[page]; cx, cy = slot % 4, slot // 4
        R = rot_y(yaw) @ rot_x(pitch) @ rot_z(roll)
        q = (R @ (pts * scale).T).T + np.array(pos)
        base = len(self.pts); self.pts += [tuple(p) for p in q]
        for fl, idx, uv in pol:
            u2 = [((cx + min(max(u, 0.0), 1.0)) / 4.0, (cy + min(max(v, 0.0), 1.0)) / 2.0) for u, v in uv]
            self.pol.append((fl, [base + i for i in idx], u2))
    def add_box(self, pos, yaw, L, Wd, H, side, top_px, end_px, scale=1.0, pitch=0.0, roll=0.0):
        """Quader (Laenge entlang z) mit Seitenansicht aus page-7 (side = (x0, y0, x1, y1) in 1024er Pixeln)"""
        cx, cy = 1, 0       # page-7 liegt im Atlas auf Platz 1
        def uvp(px, py): return ((cx + px / 1024.0) / 4.0, (cy + py / 1024.0) / 2.0)
        x0, y0, x1, y1 = side
        hw, hl = Wd / 2.0, L / 2.0
        P = np.array([(-hw, 0, -hl), (hw, 0, -hl), (hw, 0, hl), (-hw, 0, hl), (-hw, H, -hl), (hw, H, -hl), (hw, H, hl), (-hw, H, hl)])
        R = rot_y(yaw) @ rot_x(pitch) @ rot_z(roll)
        Q = (R @ (P * scale).T).T + np.array(pos)
        c = Q.mean(0)
        base = len(self.pts); self.pts += [tuple(p) for p in Q]
        faces = [((4, 5, 6, 7), top_px, None), ((1, 2, 6, 5), None, (x0, y0, x1, y1)), ((3, 0, 4, 7), None, (x1, y0, x0, y1)),
                 ((0, 1, 5, 4), end_px, None), ((2, 3, 7, 6), end_px, None)]
        for quad, flat, rect in faces:
            for tri in ((0, 1, 2), (0, 2, 3)):
                ids = [quad[t] for t in tri]
                a, b2, c2 = [Q[i] for i in ids]
                n = np.cross(b2 - a, c2 - a)
                if n @ ((a + b2 + c2) / 3 - c) < 0: ids = [ids[0], ids[2], ids[1]]
                if flat is not None:
                    uv = [uvp(*flat)] * 3
                else:
                    # Seitenflaeche: Ecken der Seitenansicht -> Quad-Ecken 0..3 (unten-links, unten-rechts, oben-rechts, oben-links)
                    corner = {quad[0]: (x0, y1), quad[1]: (x1, y1), quad[2]: (x1, y0), quad[3]: (x0, y0)} if rect[0] < rect[2] else {quad[0]: (x1, y1), quad[1]: (x0, y1), quad[2]: (x0, y0), quad[3]: (x1, y0)}
                    uv = [uvp(*corner[i]) for i in ids]
                self.pol.append((200, [base + i for i in ids], uv))

    def write(self, path, page_name):
        out = ["PIE 3", "TYPE 200", "TEXTURE 0 %s 0 0" % page_name, "LEVELS 1", "LEVEL 1", "POINTS %d" % len(self.pts)]
        out += ["\t%.2f %.2f %.2f" % p for p in self.pts]
        out.append("POLYGONS %d" % len(self.pol))
        for fl, idx, uv in self.pol:
            out.append("\t%d %d %s %s" % (fl, len(idx), " ".join(str(i) for i in idx), " ".join("%.5f %.5f" % u for u in uv)))
        out.append("")
        open(path, "w", encoding="latin-1", newline="").write("\n".join(out))


def src(rel):
    for d in SRC:
        p = os.path.join(d, rel)
        if os.path.exists(p): return p
    raise FileNotFoundError(rel)


PILES = [("miwreck.pie", 69.0), ("miwrek1.pie", 60.0), ("miwrek2.pie", 29.5), ("miwrek3.pie", 61.0), ("miwrek4.pie", 19.5), ("miwrek5.pie", 44.0)]
# (Name, Pfad, Hoehe, Gewicht, halbe Laenge, Massstab); Name "bus"/"fire" = Quader mit Seitenansicht aus der Scav-Textur
CARS = [
    ("mijeep.pie", FEAT + "/mijeep.pie", 20.0, 3, 28, 1.0),
    ("micamper.pie", FEAT + "/micamper.pie", 24.0, 3, 31, 1.0),
    ("miblucar.pie", FEAT + "/miblucar.pie", 18.0, 3, 29, 1.0),
    ("mipickup.pie", FEAT + "/mipickup.pie", 20.0, 3, 28, 1.0),
    ("michevy.pie", FEAT + "/michevy.pie", 23.0, 3, 28, 1.0),
    ("miarthov.pie", FEAT + "/miarthov.pie", 30.0, 1, 44, 1.0),
    ("icevan.pie", src("components/bodies/icevan.pie"), 33.0, 1, 42, 1.0),
    ("mitanker.pie", FEAT + "/mitanker.pie", 27.0, 1, 50, 0.5),
    ("scavtruck.pie", src("components/bodies/scavtruck.pie"), 25.0, 1, 52, 0.55),
    ("bus", None, 28.0, 1, 40, 1.0),
    ("fire", None, 26.0, 1, 38, 1.0),
]
BUS_SIDE, BUS_TOP, BUS_END = (4, 834, 276, 914), (150, 868), (230, 880)
FIRE_SIDE, FIRE_TOP, FIRE_END = (2, 740, 288, 822), (60, 800), (100, 790)


def place_car(b, car, pos, yaw, sc, pitch=0.0, roll=0.0):
    if car[0] == "bus": b.add_box(pos, yaw, 80.0, 28.0, 27.0, BUS_SIDE, BUS_TOP, BUS_END, sc, pitch, roll)
    elif car[0] == "fire": b.add_box(pos, yaw, 72.0, 25.0, 24.0, FIRE_SIDE, FIRE_TOP, FIRE_END, sc, pitch, roll)
    else: b.add(car[0], car[1], pos, yaw, sc * car[5], pitch=pitch, roll=roll)


WALL = src("structs/blbrbwlh.pie")


def build(W, B, seed):
    rnd = random.Random(seed)
    b = Builder()
    HX, HZ = W * TILE / 2.0, B * TILE / 2.0
    # ---- Scav-Mauerring (eine Kachel = ein Mauerstueck, Dicke 14)
    for i in range(W):
        x = -HX + TILE / 2 + i * TILE
        b.add("wall", WALL, (x, 0, HZ - 7.5), 0.0)
        b.add("wall", WALL, (x, 0, -HZ + 7.5), math.pi)
    for j in range(B):
        z = -HZ + TILE / 2 + j * TILE
        b.add("wall", WALL, (HX - 7.5, 0, z), math.pi / 2)
        b.add("wall", WALL, (-HX + 7.5, 0, z), -math.pi / 2)
    # ---- Kreiswege (Ellipsen); breiter Weg zwischen Aussen- und Innenhaufen
    inner = (HX - 18.0, HZ - 18.0)
    rings = [0.5] if W * B < 9 else [0.6]
    road_half = 22.0
    def road_dist(x, z):
        best = 1e9
        for k in rings:
            a, c = inner[0] * k, inner[1] * k
            rho = math.hypot(x / a, z / c)
            best = min(best, abs(rho - 1.0) * min(a, c))
        return best
    def in_wall_zone(x, z, m):
        return abs(x) > HX - 16 - m or abs(z) > HZ - 16 - m
    # ---- Haufen: grosser Zentrumshaufen + viele kleinere
    mounds = []   # (x, z, R, h)
    def pile(x, z, s, kind=None, yaw=None):
        nm, h = PILES[kind if kind is not None else rnd.randrange(len(PILES))]
        b.add(nm, FEAT + "/" + nm, (x, 0, z), rnd.uniform(0, 2 * math.pi) if yaw is None else yaw, s)
        mounds.append((x, z, 60.0 * s, h * s))
    kc = rings[-1] * 0.55
    pile(0.0, 0.0, 0.8 if W * B >= 9 else 0.5, kind=0)
    pass
    tries = 0
    target = int(W * B * 4)
    while len([m for m in mounds]) < target + 2 and tries < 4000:
        tries += 1
        s = rnd.uniform(0.45, 0.85)
        R = 60.0 * s
        x = rnd.uniform(-HX, HX); z = rnd.uniform(-HZ, HZ)
        if in_wall_zone(x, z, R * 0.7): continue
        if road_dist(x, z) < road_half + R * 0.5: continue
        if any(math.hypot(x - m[0], z - m[1]) < (R + m[2]) * 0.5 for m in mounds): continue
        pile(x, z, s)
    def ground(x, z):
        h = 0.0
        for mx, mz, R, mh in mounds:
            d = math.hypot(x - mx, z - mz)
            if d < R: h = max(h, mh * (1 - (d / R) ** 2) * 0.85)
        return h
    # ---- Autos: Reihen am Weg und an den Mauern, auf Haufen, teils zwei- bis dreilagig
    wsum = sum(c[3] for c in CARS)
    def pick():
        r = rnd.uniform(0, wsum)
        for c in CARS:
            r -= c[3]
            if r <= 0: return c
        return CARS[0]
    SMALL = [c for c in CARS if c[2] <= 24.0 and c[0] not in ('bus', 'fire')]
    def pick_small():
        return SMALL[rnd.randrange(len(SMALL))]
    n_cars = int(W * B * 13)
    placed = 0; tries = 0; spots = []
    while placed < n_cars and tries < 6000:
        tries += 1
        x = rnd.uniform(-HX + 20, HX - 20); z = rnd.uniform(-HZ + 20, HZ - 20)
        car = pick()
        if in_wall_zone(x, z, car[4] * 1.2): continue
        if road_dist(x, z) < road_half + 22: continue
        if any(math.hypot(x - sx, z - sz) < 25 for sx, sz in spots): continue
        spots.append((x, z)); placed += 1
        g = ground(x, z)
        sc = rnd.uniform(0.95, 1.12)
        # Autos in Reihen parallel zu den Mauern, leicht verdreht; auf Haufen gekippt
        yaw = rnd.choice([0.0, math.pi / 2, math.pi, -math.pi / 2]) + rnd.uniform(-0.35, 0.35)
        tilt = 0.0 if g < 2 else rnd.uniform(-0.28, 0.28)
        place_car(b, car, (x, max(0.0, g - 3.0), z), yaw, sc, pitch=tilt, roll=tilt * rnd.uniform(-1, 1))
        layers = rnd.choice([1, 1, 2, 2, 3]) if g < 2 else rnd.choice([1, 2])
        hh = car[2] * sc * 0.86
        for k in range(1, layers):
            c2 = pick_small()
            if max(0.0, g - 3.0) + hh * k > 48: break
            place_car(b, c2, (x + rnd.uniform(-7, 7), max(0.0, g - 3.0) + hh * k, z + rnd.uniform(-7, 7)),
                      yaw + rnd.uniform(-0.6, 0.6), rnd.uniform(0.92, 1.08), pitch=rnd.uniform(-0.16, 0.16), roll=rnd.uniform(-0.16, 0.16))
    # ---- Autos auf den Haufen
    for mx, mz, R, mh in list(mounds):
        if R < 22: continue
        for _ in range(rnd.choice([0, 1, 1, 2])):
            car = pick(); ang = rnd.uniform(0, 2 * math.pi); d = rnd.uniform(0, 0.45) * R
            x, z = mx + d * math.cos(ang), mz + d * math.sin(ang)
            if in_wall_zone(x, z, car[4] * 1.2): continue
            t = rnd.uniform(-0.3, 0.3)
            place_car(b, car, (x, max(0.0, ground(x, z) - 3.0), z), rnd.uniform(0, 2 * math.pi), rnd.uniform(0.95, 1.1), pitch=t, roll=rnd.uniform(-0.3, 0.3))
    # ---- Gestapelte Autoreihen an den Mauern (zwei bis drei Lagen)
    def wall_row(sx, sz, dx, dz, n, yaw):
        for i in range(n):
            if rnd.random() < 0.18: continue
            x, z = sx + dx * i * 52.0, sz + dz * i * 52.0
            for k in range(rnd.choice([1, 2, 2, 3])):
                car = pick() if k == 0 else pick_small()
                if k > 0 and k * car[2] * 0.88 > 44: break
                place_car(b, car, (x + rnd.uniform(-3, 3), k * car[2] * 0.88, z + rnd.uniform(-3, 3)),
                          yaw + rnd.choice([0, math.pi]) + rnd.uniform(-0.12, 0.12), rnd.uniform(0.95, 1.05), pitch=rnd.uniform(-0.05, 0.05), roll=rnd.uniform(-0.05, 0.05))
    nx = max(1, int((2 * HX - 90) // 52)); nz = max(1, int((2 * HZ - 180) // 52)) if B > 2 else 0
    for sgn in (1, -1):
        wall_row(-(nx - 1) * 26.0, sgn * (HZ - 38.0), 1, 0, nx, math.pi / 2)
        if nz: wall_row(sgn * (HX - 38.0), -(nz - 1) * 26.0, 0, 1, nz, 0.0)
    return b


def atlas():
    im = Image.new("RGBA", (2048, 1024), (0, 0, 0, 0))
    for p, i in SLOTS.items():
        f = None
        for d in [os.path.join(OUT, "texpages")] + [os.path.join(s, "texpages") for s in SRC]:
            c = os.path.join(d, p)
            if os.path.exists(c): f = c; break
        assert f, p
        t = Image.open(f).convert("RGBA").resize((512, 512), Image.LANCZOS)
        im.paste(t, ((i % 4) * 512, (i // 4) * 512))
    return im


def main():
    os.makedirs(os.path.join(OUT, "features"), exist_ok=True)
    page = "page-430-landfill.png"
    atlas().save(os.path.join(OUT, "texpages", page))
    fp = os.path.join(OUT, "stats", "features.json")
    feats = json.load(open(fp, encoding="utf-8"))
    tmpl = dict(feats["Wreck0"])
    for n, (W, B, seed) in {4: (2, 2, 14), 6: (2, 3, 17), 9: (3, 3, 6)}.items():
        b = build(W, B, seed)
        b.write(os.path.join(OUT, "features", "landfill%d.pie" % n), page)
        d = dict(tmpl); d.update({"id": "Landfill%d" % n, "model": "landfill%d.pie" % n, "name": "*Landfill %d*" % n, "width": W, "breadth": B, "hitpoints": 2000, "armour": 30, "type": "BUILDING"})
        feats["Landfill%d" % n] = d
        print("Landfill %d (%dx%d): %d Punkte, %d Flaechen" % (n, W, B, len(b.pts), len(b.pol)))
    json.dump(feats, open(fp, "w", encoding="utf-8"), indent="\t", sort_keys=True)


main()
