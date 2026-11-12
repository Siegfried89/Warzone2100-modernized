"""Mehr Modelle: Bazooka-Varianten (Dreadhawk, Spikeguard) und Dreadhawk-Weste mit roter Hose. more.py OUT"""
import json, os, sys, math, colorsys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
OUT = sys.argv[1]; B = os.path.join(OUT, "components", "bodies"); TP = os.path.join(OUT, "texpages")
def rd(p): return open(p, encoding="latin-1").read()
def wr(p, t): open(p, "w", encoding="latin-1", newline="").write(t)
def cp(src, dst, ren):
    t = rd(os.path.join(B, src))
    for a, b in ren: t = t.replace(a, b)
    wr(os.path.join(B, dst), t)
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
# ---- 1) Bazooka-Varianten
import newguys_lib as ng
pd = Image.open(os.path.join(TP, "page-32-punk-dread.png")).convert("RGBA"); paste_bb(pd).save(os.path.join(TP, "page-32-punk-dread.png"))
sdk = Image.open(os.path.join(TP, "page-32-soldier-dark.png")).convert("RGBA"); sb = Image.new("RGBA", (256, 512), (0, 0, 0, 255)); sb.paste(sdk, (0, 0)); paste_bb(sb).save(os.path.join(TP, "page-32-soldier-dark-bb.png"))
for a, b in (("3dd", "3ddb"), ("3ds", "3dsb")):
    for base in ("exbloke", "runanim", "fireknee"):
        t = rd(os.path.join(B, base + a + ".pie")).replace("runanim" + a + ".pie", "runanim" + b + ".pie").replace("fireknee" + a + ".pie", "fireknee" + b + ".pie")
        if a == "3ds": t = ng.scale_v(t.replace("page-32-soldier-dark.png", "page-32-soldier-dark-bb.png"))
        f = os.path.join(B, base + b + ".pie"); wr(f, t); level10(f)
# ---- 2) Dreadhawk-Weste mit roter Hose (kein Schulterschmuck)
sd = Image.open(os.path.join(TP, "page-32-soldier.png")).convert("RGBA"); px = sd.load()
for y in range(0, 95):
    for x in range(60, 215):
        r, g, b, a = px[x, y]; h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        if 0.55 <= h <= 0.86 and s > 0.18 and v > 0.12:
            r2, g2, b2 = colorsys.hsv_to_rgb(0.0, min(1.0, s * 1.35 + 0.15), min(1.0, v * 1.05)); px[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255), a)
tx = Image.new("RGBA", (256, 512), (0, 0, 0, 255)); tx.paste(sd, (0, 0))
for i, c in enumerate(((0, 0, 0), (247, 247, 253), (72, 50, 30), (30, 22, 16))):
    for y in range(256, 264):
        for x in range(i * 8, i * 8 + 8): tx.putpixel((x, y), c + (255,))
tx.save(os.path.join(TP, "page-32-soldier-dreadred.png"))
for base in ("exbloke", "runanim", "fireknee"):
    t = rd(os.path.join(B, base + "3dvn.pie")).replace("runanim3dvn.pie", "runanim3ddv.pie").replace("fireknee3dvn.pie", "fireknee3ddv.pie").replace("page-32-soldier.png", "page-32-soldier-dreadred.png")
    f = os.path.join(B, base + "3ddv.pie"); wr(f, ng.scale_v(t)); ng.strip_mohawk(f); ng.apply(f)
    L = rd(f).split("\n"); pi = next(i for i, l in enumerate(L) if l.startswith("POINTS")); n = int(L[pi].split()[1]); gi = next(i for i, l in enumerate(L) if l.startswith("POLYGONS")); m = int(L[gi].split()[1])
    ys = [float(x.split()[1]) for x in L[pi + 1:pi + 1 + n]]; keep = []
    for l in L[gi + 1:gi + 1 + m]:
        t = l.split(); c = int(t[1]); ids = list(map(int, t[2:2 + c])); u, v = float(t[2 + c]), float(t[3 + c]); p = sd.getpixel((min(255, int(u * 256)), min(255, int(v * 512))))
        if p[0] > 190 and p[1] < 90 and p[2] < 100 and min(ys[i] for i in ids) > 13: continue      # uebrige rote Irokesen-Reste im Nacken
        keep.append(l)
    L[gi + 1:gi + 1 + m] = keep; L[gi] = "POLYGONS %d" % len(keep); wr(f, "\n".join(L))
    import plates; plates.apply(f, [(-1, (241 / 256, 246 / 512), (241 / 256, 246 / 512))])          # eine silberne Schulterplatte links
for base in ("exbloke", "runanim", "fireknee"): os.remove(os.path.join(B, base + "3dvn.pie"))
# weisses Geschoss (Sprite aus page-23-fx: heller weisser Kern) statt blauer Flamme
os.makedirs(os.path.join(OUT, "effects"), exist_ok=True)
wr(os.path.join(OUT, "effects", "fxbbwhite.pie"), "PIE 2\nTYPE 212\nTEXTURE 0 page-23-fx.png 256 256\nLEVELS 1\nLEVEL 1\nPOINTS 4\n\t-3 0 12\n\t-3 0 -12\n\t3 0 -12\n\t3 0 12\nPOLYGONS 2\n\t200 3 3 2 1 117 181 111 181 111 175\n\t200 3 3 1 0 117 181 111 175 117 175\nCONNECTORS 1\n\t0 0 0\n")
# weisser Muendungsrauch: weiche weisse Wolke (eigene Textur, 64x64) in der Kreuzform des Standard-Effekts
import numpy as _np
_yy, _xx = _np.mgrid[0:64, 0:64]; _d = _np.sqrt(((_xx - 20) / 30.0) ** 2 + ((_yy - 32) / 29.0) ** 2); _n = _np.random.RandomState(5).rand(64, 64)
_a = _np.clip(1.0 - _d, 0, 1) ** 1.4 * (0.75 + 0.5 * _n); _a = _np.clip(_a, 0, 1)
_img = _np.zeros((64, 64, 4), _np.uint8); _img[..., :3] = (240 + 15 * _n[..., None]).astype(_np.uint8); _img[..., 3] = (_a * 235).astype(_np.uint8)
Image.fromarray(_img).save(os.path.join(TP, "page-498-whitesmoke.png"))
wr(os.path.join(OUT, "effects", "fxbbmuzzle.pie"), "PIE 2\nTYPE 202\nTEXTURE 0 page-498-whitesmoke.png 64 64\nLEVELS 1\nLEVEL 1\nPOINTS 8\n\t0 -12 0\n\t0 12 0\n\t0 12 -40\n\t0 -12 -40\n\t12 0 0\n\t-12 0 0\n\t-12 0 -40\n\t12 0 -40\nPOLYGONS 8\n" + "".join("\t200 3 %d %d %d %s\n" % (a, b, c, uv) for (a, b, c, uv) in ((0, 1, 2, "63 63 0 63 0 0"), (0, 2, 3, "63 63 0 0 63 0"), (3, 2, 1, "63 0 0 0 0 63"), (3, 1, 0, "63 0 0 63 63 63"), (4, 5, 6, "63 63 0 63 0 0"), (4, 6, 7, "63 63 0 0 63 0"), (7, 6, 5, "63 0 0 0 0 63"), (7, 5, 4, "63 0 0 63 63 63"))) + "CONNECTORS 1\n\t0 0 0\n")
# ---- Stats
wp = os.path.join(OUT, "stats", "weapons.json"); ws = json.load(open(wp))
bb = json.loads(json.dumps(ws["Rocket-BB"])); [bb.pop(k, None) for k in ("model", "mountModel", "designable")]
bb.update({"id": "BabaBB", "name": "Bazooka", "buildPoints": 1, "buildPower": 20, "weight": 1, "hitpoints": 1, "flightGfx": "fxbbwhite.pie", "muzzleGfx": "fxbbmuzzle.pie", "flightSpeed": 1000}); ws["BabaBB"] = bb; json.dump(ws, open(wp, "w"), indent="\t", sort_keys=True)
bp = os.path.join(OUT, "stats", "body.json"); bs = json.load(open(bp)); tp_ = os.path.join(OUT, "stats", "templates.json"); tp = json.load(open(tp_))
def add(bid, tid, src_b, src_t, model, name, weap=None):
    v = json.loads(json.dumps(bs[src_b])); v.update({"id": bid, "model": model, "name": name}); bs[bid] = v
    t = json.loads(json.dumps(tp[src_t])); t.update({"id": tid, "name": name, "body": bid, "available": True}); 
    if weap: t["weapons"] = [weap]
    tp[tid] = t
add("B1BaBaDreadB01", "BaBaDreadB", "B1BaBaDread01", "BaBaDread", "exbloke3ddb.pie", "Dreadhawk RPG", "BabaBB")
add("B1BaBaVestSB01", "BaBaVestSB", "B1BaBaVestS01", "BaBaVestS", "exbloke3dsb.pie", "Spikeguard RPG", "BabaBB")
add("B1BaBaDreadV01", "BaBaDreadV", "B1BaBaVest01", "BaBaVest", "exbloke3ddv.pie", "Dread Raider")
json.dump(bs, open(bp, "w"), indent="\t", sort_keys=True); json.dump(tp, open(tp_, "w"), indent="\t", sort_keys=True)
print("more.py fertig")
