"""Bunker-Buster-Rohr (aus more.py herausgeloest): paste_bb(Bild) und level10(Datei) (OUT muss vorher gesetzt werden)."""
import os
from PIL import Image
OUT = None
def rd(p): return open(p, encoding="latin-1").read()
def wr(p, t): open(p, "w", encoding="latin-1", newline="").write(t)
BBSRC = Image.open("/tmp/claude-0/cmp/base/texpages/page-17-droid-weapons.png").convert("RGBA")
def paste_bb(img):
    k = BBSRC.size[0] // 256
    img.paste(BBSRC.crop((155 * k, 27 * k, 167 * k, 39 * k)).resize((12, 12), Image.LANCZOS), (72, 264)); img.paste(BBSRC.crop((217 * k, 101 * k, 233 * k, 150 * k)).resize((16, 49), Image.LANCZOS), (90, 264)); return img
def level10(f, *_):
    """Gewehr (LEVEL 10) durch das echte Bunker-Buster-Rohr (gnmrktbb.pie, weiss) ersetzen, halbe Groesse"""
    W = rd("/tmp/claude-0/cmp/base/components/weapons/gnmrktbb.pie").split("\n") if os.path.exists("/tmp/claude-0/cmp/base/components/weapons/gnmrktbb.pie") else rd(os.path.join(OUT, "components", "weapons", "gnmrktbb.pie")).split("\n")
    wi = next(i for i, l in enumerate(W) if l.startswith("POINTS")); wn = int(W[wi].split()[1]); wg = next(i for i, l in enumerate(W) if l.startswith("POLYGONS")); wm = int(W[wg].split()[1])
    sc = 0.25; pts = []
    for l in W[wi + 1:wi + 1 + wn]:
        x, y, z = map(float, l.split()); pts.append((-0.9 + (x - 7.5) * sc, 4.0 + (z + 2.5) * sc, 0.75 + (y - 11.5) * sc))
    quads = []
    for l in W[wg + 1:wg + 1 + wm]:
        t = l.split(); ids = list(map(int, t[2:5])); uv = list(map(float, t[5:11])); out = []
        for k in range(3):
            px, py = uv[2 * k], uv[2 * k + 1]
            if px < 200: out += [(px - 155 + 72) / 256, (py - 27 + 264) / 512]
            else: out += [(px - 217 + 90) / 256, (py - 101 + 264) / 512]
        quads.append("\t200 3 %d %d %d %s" % (ids[0], ids[1], ids[2], " ".join("%.5f" % v for v in out)))
        quads.append("\t200 3 %d %d %d %s" % (ids[0], ids[2], ids[1], " ".join("%.5f" % v for v in (out[0], out[1], out[4], out[5], out[2], out[3]))))
    L = rd(f).split("\n"); i = L.index("LEVEL 10"); pi = i + 1; n = int(L[pi].split()[1]); gi = pi + 1 + n; m = int(L[gi].split()[1])
    new = ["POINTS %d" % len(pts)] + ["\t%.4f %.4f %.4f" % p for p in pts] + ["POLYGONS %d" % len(quads)] + quads
    L[pi:gi + 1 + m] = new; wr(f, "\n".join(L)); return len(pts)
