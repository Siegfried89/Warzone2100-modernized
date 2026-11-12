"""Rueckenfehler bei Punk-3D-Modellen: Irokesen-Schwanz (rot) im Nacken entfernen, dunkle Schulterdreiecke hautfarben. backfix.py OUT"""
import os, sys
import numpy as np
from PIL import Image
OUT = sys.argv[1]; B = os.path.join(OUT, "components", "bodies")
tex = np.array(Image.open(os.path.join(OUT, "texpages", "page-32-punk.png")).convert("RGBA"))
for f in ("exbloke3d.pie", "runanim3d.pie", "fireknee3d.pie"):
    p = os.path.join(B, f); L = open(p, encoding="latin-1").read().split("\n")
    pi = next(i for i, l in enumerate(L) if l.startswith("POINTS")); n = int(L[pi].split()[1])
    gi = next(i for i, l in enumerate(L) if l.startswith("POLYGONS")); m = int(L[gi].split()[1])
    pts = [tuple(map(float, x.split())) for x in L[pi + 1:pi + 1 + n]]; keep = []; dele = skin = 0
    for l in L[gi + 1:gi + 1 + m]:
        t = l.split(); c = int(t[1]); ids = list(map(int, t[2:2 + c])); uv = np.array(list(map(float, t[2 + c:2 + 3 * c]))).reshape(c, 2)
        cu, cv = uv.mean(0); px = tex[min(255, int(cv * 255)), min(255, int(cu * 255))]
        ys = [pts[i][1] for i in ids]; zs = [pts[i][2] for i in ids]
        if 0.84 <= cu <= 0.90 and 0.29 <= cv <= 0.37 and px[0] > 200 and px[1] < 80 and max(ys) < 18.5 and min(ys) > 14.8 and max(zs) < -1.4:
            dele += 1; continue                                                      # roter Irokesen-Schwanz im Nacken
        if px[:3].mean() < 40 and 13.5 < np.mean(ys) < 18 and np.mean(zs) < 0 and px[3] > 200:
            t = t[:2 + c] + ["0.18", "0.8"] * c; l = "\t" + " ".join(t); skin += 1   # dunkle Schulterdreiecke -> Haut
        pv = [tex[min(255, int(v * 256)), min(255, int(u * 256))][:3].mean() for u, v in uv]
        if 13.5 < np.mean(ys) < 18 and min(pv) < 110 and max(pv) > 150 and px[3] > 200:    # einzelne dunkle Eckpunkte (Schulterblatt-Flecken) -> hellste Ecke
            b = int(np.argmax(pv)); t = t[:2 + c] + [("%g" % uv[b][0]), ("%g" % uv[b][1])] * c; l = "\t" + " ".join(t); skin += 1
        keep.append(l)
    L[gi + 1:gi + 1 + m] = keep; L[gi] = "POLYGONS %d" % len(keep)
    open(p, "w", encoding="latin-1", newline="").write("\n".join(L)); print(f, "entfernt", dele, "hautfarben", skin)
