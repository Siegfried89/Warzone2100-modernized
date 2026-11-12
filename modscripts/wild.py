"""4. Scav-Infanterie 'Wildmane': 70er-Wuschelfrisur (goldgelb), blau karierte Hose. wild.py OUT"""
import json, os, sys, math, random
from PIL import Image
OUT = sys.argv[1]; B = os.path.join(OUT, "components", "bodies"); TP = os.path.join(OUT, "texpages")
def rd(p): return open(p, encoding="latin-1").read()
def wr(p, t): open(p, "w", encoding="latin-1", newline="").write(t)
# ---- Textur: Karohose + Haarfeld (freier Block 0..16 / 0..16)
import colorsys
im = Image.open(os.path.join(TP, "page-32-punk.png")).convert("RGBA"); px = im.load()
for y in range(0, 95):
    for x in range(60, 215):
        r, g, b, a = px[x, y]; h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        if a > 0 and 0.55 <= h <= 0.86 and s > 0.18 and v > 0.12:
            sx, sy = (x // 5) % 3 == 0, (y // 5) % 3 == 0     # Karo-Streifen
            if sx and sy: c = (235, 240, 250)
            elif sx or sy: c = (110, 170, 245)
            else: c = (30, 70, 190)
            k = 0.65 + 0.35 * v; px[x, y] = (int(c[0] * k), int(c[1] * k), int(c[2] * k), a)
# Textur wird 256x512: oben Original (Punk + Karohose), unten die echte Haar-Strähnenkarte aus der RTW-Textur (mit Alpha-Zacken)
src = Image.open("/home/claude/w/woad_full.png").convert("RGBA").crop((200, 0, 256, 72))   # Strähnenblock der WOAD-Textur
big = src.resize((src.width * 2, src.height * 2), Image.BICUBIC)
_bp = big.load()                                   # Haar heller und goldener (im Spiel wirkte es dunkel/grau)
for _y in range(big.height):
    for _x in range(big.width):
        _r, _g, _b, _a = _bp[_x, _y]
        _l = (_r + _g + _b) / 765.0; _t = max(0.0, min(1.0, (_l - 0.34) / 0.50))      # kraeftiges Gold statt Goldgrau
        _r, _g, _b = 150 + 105 * _t, 126 + 118 * _t, 74 + 130 * _t
        _bp[_x, _y] = (int(min(255, _r)), int(min(255, _g)), int(min(255, _b)), _a)
bp_ = big.load()
for yy in range(big.height):
    for xx in range(big.width):
        r_, g_, b_, a_ = bp_[xx, yy]; bp_[xx, yy] = (r_, g_, b_, 255 if a_ >= 128 else 0)
tx = Image.new("RGBA", (256, 512), (0, 0, 0, 0)); tx.paste(im, (0, 0)); tx.paste(big, (0, 256), big)
SX, SY = 0, 256; SW, SH = big.width, big.height
tx.save(os.path.join(TP, "page-32-punk-blue.png"))
# ---- Modelle
random.seed(7)
import numpy as np
_z = np.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), "hairmesh_src.npz")); _P, _bone, _idx = _z["P"], _z["bone"], _z["idx"]
_cen = np.array([0, 0.11, 0.0]); SC = 12.5; CY = float(os.environ.get('CY',19.8)); CZ = float(os.environ.get('CZ',0.5))
_UV = np.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), "uv_aos.npy"))
_w = np.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), "hairmesh_woad.npz")); _WP, _WI, _WUV = _w["P"], _w["I"], _w["UV"]
HAIR = [tuple(int(i) for i in t) for t in _WI]                                         # echte Maehne aus woad_warrior.cas (124 Dreiecke)
def _cap(t):
    c = np.mean([_P[i] for i in t], 0)
    return c[1] > 0.15 and not (c[2] > 0.08 and c[1] < 0.19)
CAP = [tuple(int(i) for i in t) for k, t in enumerate(_idx) if k < 518 and all(_bone[i] == 7 for i in t) and _cap(t)]
UVMODE = os.environ.get("UVMODE", "B")
def hmap(u, v):
    a = max(0.0, min(1.0, u / 0.52)); b = max(0.0, min(1.0, (v - 0.774) / 0.222))
    if UVMODE == "B": a = 1 - a
    if UVMODE == "C": a, b = b, a
    if UVMODE == "D": a, b = 1 - a, 1 - b
    if UVMODE == "E": a, b = 1 - b, a
    return (SX + 1 + a * (SW - 2)) / 256, (SY + 1 + b * (SH - 2)) / 512
def wmap(u, v):
    uu, vv = 1 - u, 1 - v
    xt = 214 + (min(1.0, max(0.78, uu)) - 0.78) / 0.22 * 32
    return (max(0.5, (xt - 200) * 2) + SX) / 256, (min(SH - 1, max(0.5, vv * 256 * 2)) + SY) / 512
def scale_v(txt):
    out = []
    for l in txt.split("\n"):
        t = l.split()
        if len(t) >= 5 and t[0].isdigit() and t[1].isdigit() and int(t[1]) in (3, 4) and len(t) == 2 + 3 * int(t[1]):
            c = int(t[1]); u = t[2 + c:]
            for k in range(c): u[2 * k + 1] = "%g" % (float(u[2 * k + 1]) * 0.5)
            l = "\t" + " ".join(t[:2 + c] + u)
        out.append(l)
    return "\n".join(out)
def hair(f):
    L = rd(f).split("\n")
    pi = next(i for i, l in enumerate(L) if l.startswith("POINTS")); n = int(L[pi].split()[1])
    gi = next(i for i, l in enumerate(L) if l.startswith("POLYGONS")); m = int(L[gi].split()[1])
    # alte Irokesen-Spikes entfernen (Kopfbereich, Textur-u > 0.7)
    pts = [tuple(map(float, x.split())) for x in L[pi + 1:pi + 1 + n]]; keep = []; rm = 0
    for l in L[gi + 1:gi + 1 + m]:
        t = l.split(); c = int(t[1]); ids = list(map(int, t[2:2 + c])); uu = sum(float(x) for x in t[2 + c::2][:c]) / c
        if uu > 0.7 and ((min(pts[i][1] for i in ids) > 17.0) or (max(pts[i][1] for i in ids) > 17.0 and min(pts[i][1] for i in ids) > 15.0 and min(pts[i][2] for i in ids) < -1.2)): rm += 1; continue
        keep.append(l)
    Q = []; P = []; SXs, SYs, SZs = float(os.environ.get('SXS',14.5)), float(os.environ.get('SYS',15)), float(os.environ.get('SZS',15))
    def tp(v): return ((float(_WP[v][0]) * SXs), CY + (float(_WP[v][1]) - 0.115) * SYs, CZ + float(_WP[v][2]) * SZs)
    def tpc(v): return ((float(_P[v][0]) * SXs), CY + (float(_P[v][1]) - 0.115) * SYs, CZ + float(_P[v][2]) * SZs)
    def addtri(t, uvf, tp=tp):
        b0 = n + len(P); P.extend([tp(i) for i in t]); uv = [uvf(i) for i in t]
        Q.append("\t200 3 %d %d %d %s %s %s %s %s %s" % (b0, b0 + 1, b0 + 2, uv[0][0], uv[0][1], uv[1][0], uv[1][1], uv[2][0], uv[2][1]))
        Q.append("\t200 3 %d %d %d %s %s %s %s %s %s" % (b0, b0 + 2, b0 + 1, uv[0][0], uv[0][1], uv[2][0], uv[2][1], uv[1][0], uv[1][1]))
    for t in HAIR:
        addtri(t, lambda i: wmap(float(_WUV[i][0]), float(_WUV[i][1])))
    capuv = ((SX + 60) / 256, (SY + 40) / 512)
    for t in CAP: addtri(t, lambda i: capuv, tpc)
    end = gi + 1 + m
    L[gi + 1:end] = keep + Q; L[gi] = "POLYGONS %d" % (len(keep) + len(Q))
    L[pi + 1 + n:pi + 1 + n] = ["\t%.4f %.4f %.4f" % p for p in P]; L[pi] = "POINTS %d" % (n + len(P))
    wr(f, "\n".join(L)); return len(P), len(Q), rm
for a, b in (("exbloke3d.pie", "exbloke3dw.pie"), ("runanim3d.pie", "runanim3dw.pie"), ("fireknee3d.pie", "fireknee3dw.pie")):
    t = rd(os.path.join(B, a)).replace("runanim3d.pie", "runanim3dw.pie").replace("fireknee3d.pie", "fireknee3dw.pie").replace("page-32-punk.png", "page-32-punk-blue.png")
    wr(os.path.join(B, b), scale_v(t)); print(b, hair(os.path.join(B, b)))
# ---- Stats
bp = os.path.join(OUT, "stats", "body.json"); bs = json.load(open(bp)); tp_ = os.path.join(OUT, "stats", "templates.json"); tp = json.load(open(tp_))
v = json.loads(json.dumps(bs["B1BaBaPunk01"])); v.update({"id": "B1BaBaPunkW01", "model": "exbloke3dw.pie", "name": "Wildmane"}); bs["B1BaBaPunkW01"] = v
t = json.loads(json.dumps(tp["BaBaPunk"])); t.update({"id": "BaBaPunkW", "name": "Wildmane", "body": "B1BaBaPunkW01", "available": True}); tp["BaBaPunkW"] = t
json.dump(bs, open(bp, "w"), indent="\t", sort_keys=True); json.dump(tp, open(tp_, "w"), indent="\t", sort_keys=True)
