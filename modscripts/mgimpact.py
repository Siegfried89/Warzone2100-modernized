"""MG-Einschlag: Projektil unsichtbar (kein flightGfx), Einschlag = kleiner Staubschwall (hellbraune Pixel) + ein paar gelbe Funken.
Gleicher Effekt fuer Treffer und Fehlschuss, fuer jedes Ziel. Gilt fuer alle echten Maschinengewehre (auch Turm, VTOL, Zyborg, Rotary).
Textur page-440-mgimpact.png (256 x 256, Bilder je 40 x 40 nebeneinander: Reihe 1 Staub, Reihe 2 Funken, je 6 Bilder),
Effekt effects/fxmgimp.pie (PIE 2, Alpha-Mischung, zur Kamera gedreht).
mgimpact.py OUT"""
import sys, os, json, math, random
import numpy as np
from PIL import Image

OUT = sys.argv[1]
N, FW, RATE = 6, 40, 60
PAGE = "page-440-mgimpact.png"


def paint():
    rnd = random.Random(2024)
    img = np.zeros((256, 256, 4), np.uint8)

    def px(fx, fy, x, y, col, a, size=2):
        for dx in range(size):
            for dy in range(size):
                xx, yy = int(round(x)) + dx, int(round(y)) + dy
                if 2 <= xx < FW - 2 and 2 <= yy < FW - 2:
                    X, Y = fx * FW + xx, fy * FW + yy
                    old = img[Y, X]
                    if a >= old[3]: img[Y, X] = (col[0], col[1], col[2], a)

    # Staub: Pixelwolke, waechst, steigt auf, wird durchsichtig
    DUST = [(214, 184, 136), (196, 160, 110), (176, 140, 94), (226, 200, 156)]
    seeds = [(rnd.uniform(0, 2 * math.pi), rnd.uniform(0.2, 1.0), rnd.choice(DUST), rnd.random()) for _ in range(46)]
    for f in range(N):
        t = f / (N - 1)
        R = 5 + 13 * (1 - (1 - t) ** 2)
        alpha = int(235 * (1 - t ** 1.6))
        if alpha < 8: continue
        for (ang, rr, col, jit) in seeds:
            # etwas Haufen am Boden, hoeher mit der Zeit
            x = 20 + math.cos(ang) * rr * R * 1.15
            y = 31 - abs(math.sin(ang)) * rr * R * 0.95 - t * 5.0 - jit * 2.0
            px(f, 0, x, y, col, alpha, 2 if (jit > 0.35 or f < 2) else 1)
    # Funken: wenige gelbe Punkte mit kurzem orangem Schweif, fliegen nach oben/aussen und fallen
    sp = []
    for k in range(6):
        ang = math.radians(rnd.uniform(25, 155)) * (1 if rnd.random() < 0.5 else 1)
        sp.append((ang, rnd.uniform(2.0, 3.8), rnd.uniform(0.0, 0.5)))
    for f in range(N - 1):
        t = f / (N - 2)
        alpha = int(255 * (1 - max(0.0, t - 0.55) / 0.45))
        for (ang, v, g) in sp:
            r = v * (f + 0.6)
            x = 20 + math.cos(ang) * r * 1.3
            y = 30 - math.sin(ang) * r + g * f * f * 0.9
            col = (255, int(236 - 70 * t), int(96 - 50 * t))
            px(f, 1, x, y, col, alpha, 2 if f < 3 else 1)
            # Schweif (ein Pixel zurueck in Flugrichtung, orange)
            xt = 20 + math.cos(ang) * (r - v * 0.8) * 1.3
            yt = 30 - math.sin(ang) * (r - v * 0.8) + g * (f - 0.5) ** 2 * 0.9
            if f > 0: px(f, 1, xt, yt, (255, 150, 40), int(alpha * 0.8), 1)
    return Image.fromarray(img, "RGBA")


def tex_frames(img):
    """je Reihe 6 Bilder: x = 0..239, Reihe 0 y = 0..39 (Staub), Reihe 1 y = 40..79 (Funken)"""
    return img


def build_pie():
    def quad(hs, yo, z, row):
        x0, y0, x1, y1 = 0, row * FW, FW, row * FW + FW
        pts = [(hs, hs + yo, z), (-hs, hs + yo, z), (-hs, -hs + yo, z), (hs, -hs + yo, z)]
        uv = [(x1, y0), (x0, y0), (x0, y1), (x1, y1)]
        return pts, uv
    d_pts, d_uv = quad(12.0, 12.0, 0.0, 0)
    s_pts, s_uv = quad(15.0, 13.0, 1.0, 1)
    pts = d_pts + s_pts
    polys = []
    for base, uv in ((0, d_uv), (4, s_uv)):
        for (a, b, c, ia, ib, ic) in ((0, 1, 2, 0, 1, 2), (0, 2, 3, 0, 2, 3), (3, 2, 1, 3, 2, 1), (3, 1, 0, 3, 1, 0)):
            u = [uv[ia], uv[ib], uv[ic]]
            polys.append("\t4200 3 %d %d %d %d %d %d %d %s" % (a + base, b + base, c + base, N, RATE, FW, FW, " ".join("%d %d" % p for p in u)))
    t = ["PIE 2", "TYPE 221", "TEXTURE 0 %s 256 256" % PAGE, "LEVELS 1", "LEVEL 1", "POINTS %d" % len(pts)]
    t += ["\t%g %g %g" % p for p in pts]
    t += ["POLYGONS %d" % len(polys)] + polys + ["CONNECTORS 1", "\t0 0 0", ""]
    return "\n".join(t)


def main():
    os.makedirs(os.path.join(OUT, "effects"), exist_ok=True); os.makedirs(os.path.join(OUT, "texpages"), exist_ok=True)
    paint().save(os.path.join(OUT, "texpages", PAGE))
    open(os.path.join(OUT, "effects", "fxmgimp.pie"), "w", encoding="latin-1", newline="").write(build_pie())
    wp = os.path.join(OUT, "stats", "weapons.json"); W = json.load(open(wp, encoding="utf-8"))
    skip = {"ZNULLWEAPON", "bbaawep", "Superweapon"}
    n = 0
    for k, v in W.items():
        if not isinstance(v, dict) or v.get("weaponSubClass") != "MACHINE GUN" or k in skip: continue
        if k.startswith("MFA_") and not k.startswith("MFA_GattlingGun"): continue
        v.pop("flightGfx", None)
        v["hitGfx"] = "fxmgimp.pie"; v["missGfx"] = "fxmgimp.pie"; v["facePlayer"] = 1
        n += 1
    json.dump(W, open(wp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("MG-Einschlag: %d Waffen ohne Flug-Grafik, Effekt fxmgimp.pie" % n)


main()
