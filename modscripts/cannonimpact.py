"""Kanonen-Einschlag: Pixel-Partikel mit Feuer, Trümmern, Funken und aufsteigendem Rauch in drei Groessen (leicht / mittel / schwer).
Textur page-441-cannonimpact.png (1024 x 1024, 8 Bilder je Reihe; je Klasse eine Feuer- und eine Rauch-Reihe),
Effekte effects/fxcanl.pie, fxcanm.pie, fxcanh.pie (PIE 2, Alpha-Mischung, zur Kamera gedreht).
Zuordnung nach Waffe: leicht = kleine/schnelle Kanonen (smlcan, rapdcan, hivelcan, Scav-Kanonen), mittel = medcan + MF-Plasma/Teppich,
schwer = lrgcan (375 mm), Super-Kanone, MF-Haubitze.  Nicht angefasst: Flak (AAGun2), Plasmakanone (Laser4), Superweapon2, Railguns.
cannonimpact.py OUT [PREVIEW.png]"""
import sys, os, json, math, random
import numpy as np
from PIL import Image

OUT = sys.argv[1]
PREV = sys.argv[2] if len(sys.argv) > 2 else None
N = 8
PAGE = "page-441-cannonimpact.png"
PS = 1024
UPP = 0.8          # Welteinheiten je Textur-Pixel (gleich fuer alle Klassen -> gleiche Pixelgroesse)
# Klasse: Bildgroesse, Rate (ms je Bild), Feuerradius, Feuerteilchen, Truemmer, Funken, Rauchteilchen, Rauchhoehe, Pixelgroesse Rauch
CLS = {
    "l": dict(S=64, rate=90, fr=15, nf=70, nd=9, ns=7, nsm=60, rise=14, sp=2, rows=(0, 64)),
    "m": dict(S=96, rate=110, fr=24, nf=170, nd=16, ns=12, nsm=120, rise=24, sp=2, rows=(128, 224)),
    "h": dict(S=128, rate=130, fr=36, nf=330, nd=26, ns=20, nsm=220, rise=36, sp=3, rows=(320, 448)),
}
FIRE = [(0.0, (255, 250, 210)), (0.18, (255, 226, 96)), (0.42, (255, 162, 34)), (0.68, (226, 84, 22)), (0.9, (120, 42, 26)), (1.0, (70, 36, 28))]
DUST = [(200, 170, 124), (180, 146, 100), (214, 188, 144)]


def fire_col(a):
    a = min(max(a, 0.0), 1.0)
    for i in range(len(FIRE) - 1):
        t0, c0 = FIRE[i]; t1, c1 = FIRE[i + 1]
        if a <= t1:
            u = (a - t0) / (t1 - t0)
            return tuple(int(c0[k] + (c1[k] - c0[k]) * u) for k in range(3))
    return FIRE[-1][1]


def paint_class(img, key, seed):
    P = CLS[key]; S = P["S"]; rnd = random.Random(seed)
    y_fire, y_smoke = P["rows"]

    def put(row_y, f, x, y, col, a, size):
        for dx in range(size):
            for dy in range(size):
                xx, yy = int(round(x)) + dx, int(round(y)) + dy
                if 1 <= xx < S - 1 and 1 <= yy < S - 1:
                    X, Y = f * S + xx, row_y + yy
                    if a >= img[Y, X, 3]: img[Y, X] = (col[0], col[1], col[2], a)

    cx, gy = S / 2, S * 0.80           # Mittelpunkt und Bodenlinie im Bild
    fy = S * 0.62
    # Feuerball
    fp = []
    for _ in range(P["nf"]):
        ang = rnd.uniform(0, 2 * math.pi); r = math.sqrt(rnd.random())
        fp.append((ang, r, rnd.uniform(0.35, 1.0), rnd.random(), 2 if rnd.random() < 0.8 else 3))
    deb = [(math.radians(rnd.uniform(25, 155)), rnd.uniform(1.4, 3.2) * (S / 64) ** 0.7, rnd.choice([(58, 42, 30), (92, 68, 42), (124, 96, 62), (40, 34, 30)]), rnd.choice([3, 3, 4] if key != "l" else [2, 3])) for _ in range(P["nd"])]
    sparks = [(math.radians(rnd.uniform(15, 165)), rnd.uniform(2.2, 4.6) * (S / 64) ** 0.7) for _ in range(P["ns"])]
    for f in range(N):
        t = f / (N - 1)
        tt = min(1.0, (t + 0.14) / 0.5)
        R = P["fr"] * (1 - (1 - tt) ** 2)
        for (ang, r, life, jit, size) in fp:
            if t > life: continue
            age = t / life + r * 0.25
            a = 255 if t < life * 0.75 else int(255 * (life - t) / (life * 0.25))
            if a < 12: continue
            x = cx + math.cos(ang) * r * R * 1.1
            y = min(gy - 1, fy - math.sin(ang) * r * R * 0.9 - t * P["rise"] * 0.7 * (1 - r * 0.5))
            put(y_fire, f, x, y, fire_col(age), a, size)
        # Truemmer
        for (ang, v, col, size) in deb:
            tf = f + 0.4
            x = cx + math.cos(ang) * v * tf * 2.2
            y = gy - math.sin(ang) * v * tf * 2.6 + 0.42 * tf * tf * (S / 64) ** 0.7 * 2.2
            if y > gy + 2 or t > 0.92: continue
            put(y_fire, f, x, y, col, 255, size)
        # Funken mit Schweif
        for (ang, v) in sparks:
            if f > 5: continue
            tf = f + 0.5
            x = cx + math.cos(ang) * v * tf * 2.4
            y = gy - math.sin(ang) * v * tf * 2.8 + 0.5 * tf * tf * 1.6
            a = 255 if f < 3 else int(255 * (6 - f) / 3)
            put(y_fire, f, x, y, (255, int(236 - 60 * t), int(110 - 60 * t)), a, 2)
            if f > 0:
                xt = x - math.cos(ang) * v * 1.9; yt = y + math.sin(ang) * v * 2.2
                put(y_fire, f, xt, yt, (255, 160, 50), int(a * 0.8), 1)
    # Rauch + Staub
    sm = []
    for _ in range(P["nsm"]):
        sm.append((rnd.uniform(-1, 1), rnd.uniform(0.0, 1.0), rnd.uniform(0.6, 1.4), rnd.random(), rnd.choice([0, 0, 1, 2])))
    dust = [(rnd.uniform(0, 2 * math.pi), rnd.uniform(0.3, 1.0), rnd.choice(DUST)) for _ in range(P["nsm"] // 2)]
    for f in range(N):
        t = f / (N - 1)
        # Staubring am Boden: frueh, breitet sich aus
        if f <= 5:
            da = int(210 * (1 - (f / 5.0) ** 1.4))
            for (ang, rr, col) in dust:
                Rr = P["fr"] * (0.55 + 0.85 * min(1, f / 3.0)) * rr
                put(y_smoke, f, cx + math.cos(ang) * Rr * 1.5, gy - abs(math.sin(ang)) * Rr * 0.5 - f * 0.6, col, da, 2)
        # Rauchsaeule: steigt auf, dehnt sich, wird heller und durchsichtig
        if f == 0: continue
        for (dx, h, sz, jit, shade) in sm:
            lt = (f - 0.5) / (N - 0.5)
            ri = lt * P["rise"] * (0.5 + h) * 1.5
            wid = (0.35 + lt * 1.1) * P["fr"] * 0.9
            x = cx + dx * wid * (0.6 + 0.4 * (1 - h)) + math.sin(lt * 3 + jit * 6) * 1.2
            y = fy + 2 - ri - h * P["fr"] * 0.5 * lt
            a = int(235 * min(1.0, lt * 5) * (1 - lt ** 2.2))
            if a < 10: continue
            g = int(30 + 62 * lt + shade * 12)
            put(y_smoke, f, x, y, (g, g - 2, g - 5), a, P["sp"] if jit > 0.3 else max(1, P["sp"] - 1))


def build_pie(key):
    P = CLS[key]; S = P["S"]
    hs = S * UPP / 2

    def quad(hh, yo, z, row_y):
        x0, y0, x1, y1 = 0, row_y, S, row_y + S
        pts = [(hh, hh + yo, z), (-hh, hh + yo, z), (-hh, -hh + yo, z), (hh, -hh + yo, z)]
        uv = [(x1, y0), (x0, y0), (x0, y1), (x1, y1)]
        return pts, uv
    s_pts, s_uv = quad(hs, hs * 0.95, 0.0, P["rows"][1])     # Rauch hinten
    f_pts, f_uv = quad(hs, hs * 0.80, 1.0, P["rows"][0])     # Feuer davor
    pts = s_pts + f_pts
    polys = []
    for base, uv in ((0, s_uv), (4, f_uv)):
        for (a, b, c, ia, ib, ic) in ((0, 1, 2, 0, 1, 2), (0, 2, 3, 0, 2, 3), (3, 2, 1, 3, 2, 1), (3, 1, 0, 3, 1, 0)):
            u = [uv[ia], uv[ib], uv[ic]]
            polys.append("\t4200 3 %d %d %d %d %d %d %d %s" % (a + base, b + base, c + base, N, P["rate"], S, S, " ".join("%d %d" % p for p in u)))
    t = ["PIE 2", "TYPE 221", "TEXTURE 0 %s %d %d" % (PAGE, PS, PS), "LEVELS 1", "LEVEL 1", "POINTS %d" % len(pts)]
    t += ["\t%g %g %g" % p for p in pts]
    t += ["POLYGONS %d" % len(polys)] + polys + ["CONNECTORS 1", "\t0 0 0", ""]
    return "\n".join(t)


def classify(k, v):
    """leicht / mittel / schwer oder None"""
    if v.get("weaponSubClass") != "CANNON" or v.get("movement") not in ("DIRECT", "HOMING-DIRECT"): return None
    if k in ("AAGun2Mk1Quad", "Laser4-PlasmaCannon", "Superweapon2", "ZNULLWEAPON", "bbaawep", "Superweapon"): return None
    if k.startswith("MFA_Howitzer"): return "h"
    if k.startswith("MFA_"): return "m"
    wav = str(v.get("weaponWav", "")).lower()
    if k == "CannonSuper" or "lrgcan" in wav: return "h"
    if "medcan" in wav: return "m"
    return "l"


def main():
    os.makedirs(os.path.join(OUT, "effects"), exist_ok=True); os.makedirs(os.path.join(OUT, "texpages"), exist_ok=True)
    img = np.zeros((PS, PS, 4), np.uint8)
    for i, key in enumerate(("l", "m", "h")):
        paint_class(img, key, 777 + i)
        open(os.path.join(OUT, "effects", "fxcan%s.pie" % key), "w", encoding="latin-1", newline="").write(build_pie(key))
    Image.fromarray(img, "RGBA").save(os.path.join(OUT, "texpages", PAGE))
    wp = os.path.join(OUT, "stats", "weapons.json"); W = json.load(open(wp, encoding="utf-8"))
    cnt = {"l": 0, "m": 0, "h": 0}
    for k, v in W.items():
        if not isinstance(v, dict): continue
        c = classify(k, v)
        if c is None: continue
        v["hitGfx"] = "fxcan%s.pie" % c; v["missGfx"] = "fxcan%s.pie" % c
        v["facePlayer"] = 1; v["effectSize"] = 100; v["numExplosions"] = 1
        cnt[c] += 1
    json.dump(W, open(wp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Kanonen-Einschlag: leicht %d, mittel %d, schwer %d" % (cnt["l"], cnt["m"], cnt["h"]))
    if PREV: preview(img, PREV)


def preview(img, path):
    """je Klasse zwei Zeilen (Feuer + Rauch zusammengesetzt) mit allen 8 Bildern, 3x vergroessert, auf Erdboden"""
    bg = np.array([112, 92, 64, 255], np.uint8)
    rows = []
    for key in ("l", "m", "h"):
        P = CLS[key]; S = P["S"]
        strip = np.zeros((S, S * N, 4), np.uint8); strip[:] = bg
        for f in range(N):
            for ry in (P["rows"][1], P["rows"][0]):   # Rauch zuerst, Feuer darueber
                fr = img[ry:ry + S, f * S:(f + 1) * S].astype(np.float32)
                a = fr[..., 3:4] / 255.0
                reg = strip[:, f * S:(f + 1) * S].astype(np.float32)
                reg[..., :3] = reg[..., :3] * (1 - a) + fr[..., :3] * a
                strip[:, f * S:(f + 1) * S] = reg.astype(np.uint8)
        rows.append(Image.fromarray(strip, "RGBA"))
    W_ = max(r.width for r in rows); H_ = sum(r.height for r in rows) + 8 * (len(rows) - 1)
    canvas = Image.new("RGBA", (W_, H_), (30, 28, 26, 255)); y = 0
    for r in rows:
        canvas.paste(r, (0, y)); y += r.height + 8
    canvas = canvas.resize((canvas.width * 2, canvas.height * 2), Image.NEAREST)
    canvas.convert("RGB").save(path)


main()
