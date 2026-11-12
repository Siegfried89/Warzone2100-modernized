"""Zusatz-Infanterie: rote Hosen (Sprite + 3D oberkoerperfrei), Spikes-Schulterplatte an der Weste. variants.py OUT"""
import sys as _s, os as _o; _s.path.insert(0, _o.path.dirname(_o.path.abspath(__file__)))
import json, os, sys, subprocess
from PIL import Image
OUT = sys.argv[1]; B = os.path.join(OUT, "components", "bodies"); TP = os.path.join(OUT, "texpages")
def rd(p): return open(p, encoding="latin-1").read()
def wr(p, t): open(p, "w", encoding="latin-1", newline="").write(t)
import colorsys
def red(im, rects, keybg=True):
    px = im.load()
    for (x0, y0, x1, y1) in rects:
        for y in range(y0, min(y1, im.size[1])):
            for x in range(x0, min(x1, im.size[0])):
                p = px[x, y]; r, g, b = p[:3]
                h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
                if 0.55 <= h <= 0.86 and s > 0.18 and v > 0.12 and not (keybg and b >= 212 and r <= 120):   # blau/violett (Hosen-Schattierungen), Cyan ausgenommen
                    r2, g2, b2 = colorsys.hsv_to_rgb(0.0, min(1.0, s * 1.35 + 0.15), min(1.0, v * 1.05)); px[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255)) + tuple(p[3:])
    return im
def silver(path):
    im = Image.open(path).convert("RGBA"); px = im.load()
    for y in range(236, 254):
        for x in range(232, 250):
            t = (x - 232) / 17; v = int(222 + 30 * (1 - abs(t - 0.5) * 2)); px[x, y] = (v, v, min(255, v + 6), 255)
    im.save(path)
silver(os.path.join(TP, "page-32-soldier.png"))
def heal(path, rect):
    im = Image.open(path).convert("RGBA"); px = im.load(); W, H = im.size; x0, y0, x1, y1 = rect; bad = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b, a = px[x, y]
            if max(r, g, b) > 25 and (r + g + b) / 3 < 125 and r >= g: bad.append((x, y))
    for (x, y) in bad:
        acc = []
        for dy in range(-7, 8):
            for dx in range(-7, 8):
                xx, yy = x + dx, y + dy
                if 0 <= xx < W and 0 <= yy < H:
                    q = px[xx, yy]; m = (q[0] + q[1] + q[2]) / 3
                    if m > 150 and q[0] > q[2] + 15: acc.append(q)
        if acc: px[x, y] = tuple(sum(c[i] for c in acc) // len(acc) for i in range(3)) + (255,)
    im.save(path); return len(bad)
print("Flecken repariert:", heal(os.path.join(TP, "page-32-punk.png"), (150, 90, 252, 225)))
# Texturen
s = Image.open(os.path.join(TP, "page-7-scavspr.png")).convert("RGBA")
p = Image.open(os.path.join(TP, "page-32-punk.png")).convert("RGBA")
red(p, [(60, 0, 215, 95)], keybg=False).save(os.path.join(TP, "page-32-punk-red.png"))
# Modelle: 3D rot
for a, b in (("exbloke3d.pie", "exbloke3dr.pie"), ("runanim3d.pie", "runanim3dr.pie"), ("fireknee3d.pie", "fireknee3dr.pie")):
    t = rd(os.path.join(B, a)).replace("runanim3d.pie", "runanim3dr.pie").replace("fireknee3d.pie", "fireknee3dr.pie").replace("page-32-punk.png", "page-32-punk-red.png")
    wr(os.path.join(B, b), t)
# Modell "Spikeguard": Weste + beidseitig schwarze Schulterplatten mit weissen Spikes (vor dem Marauder-Spiken kopiert)
import plates
_sd = Image.open(os.path.join(TP, "page-32-soldier.png")).convert("RGBA"); _q = _sd.load()      # dunkler Mohawk (schwarz mit Petrol-Spitzen) fuer den Spikeguard
for _y in range(_sd.size[1]):
    for _x in range(_sd.size[0]):
        _r, _g, _b, _a = _q[_x, _y]
        if _a and _r > 190 and _g < 90 and _b < 100:
            _t = max(0.0, min(1.0, (_r - 222) / 24.0)); _q[_x, _y] = (int(10 + 8 * _t), int(14 + 105 * _t), int(18 + 110 * _t), _a)
_sd.save(os.path.join(TP, "page-32-soldier-dark.png"))
for a, b in (("exbloke3dv.pie", "exbloke3ds.pie"), ("runanim3dv.pie", "runanim3ds.pie"), ("fireknee3dv.pie", "fireknee3ds.pie")):
    t = rd(os.path.join(B, a)).replace("runanim3dv.pie", "runanim3ds.pie").replace("fireknee3dv.pie", "fireknee3ds.pie").replace("page-32-soldier.png", "page-32-soldier-dark.png")
    wr(os.path.join(B, b), t); plates.apply(os.path.join(B, b), [(-1, (241 / 256, 246 / 256), (241 / 256, 246 / 256)), (1, (241 / 256, 246 / 256), (241 / 256, 246 / 256))])
for a, b in (("exbloke3dv.pie", "exbloke3dvn.pie"), ("runanim3dv.pie", "runanim3dvn.pie"), ("fireknee3dv.pie", "fireknee3dvn.pie")):   # saubere Weste (Zwischenkopie fuer more.py)
    wr(os.path.join(B, b), rd(os.path.join(B, a)).replace("runanim3dv.pie", "runanim3dvn.pie").replace("fireknee3dv.pie", "fireknee3dvn.pie"))
# Spikes an die Weste (nur das Haupt-Modell)
for _f in ("exbloke3dv.pie", "runanim3dv.pie", "fireknee3dv.pie"):
    subprocess.run([sys.executable, "-I", "vest_spikes.py", os.path.join(B, _f)], check=True)
# Reserve: Sprite blau (BaBaPeople) und rot (BaBaPeopleR), versteckt, alles klein geschrieben
sp = Image.open(os.path.join(TP, "page-7-scavspr.png")).convert("RGBA"); q = sp.load()
for y in range(sp.size[1]):
    for x in range(sp.size[0]):
        r, g, b, a = q[x, y]; h, s_, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        if 0.58 <= h <= 0.80 and 0.10 <= s_ <= 0.55 and v > 0.35:
            r2, g2, b2 = colorsys.hsv_to_rgb(0.0, min(1.0, s_ * 1.6 + 0.25), v); q[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255), a)
sp.save(os.path.join(TP, "page-7-scavspr-red.png"))
for a, b in (("exbloke.pie", "exblokered.pie"), ("runanim.pie", "runanimred.pie"), ("fireknee.pie", "fireknee-red.pie")):
    wr(os.path.join(B, b), rd(os.path.join(B, a)).replace("runanim.pie", "runanimred.pie").replace("fireknee.pie", "fireknee-red.pie").replace("page-7-scavspr.png", "page-7-scavspr-red.png"))
# Stats
bp = os.path.join(OUT, "stats", "body.json"); bs = json.load(open(bp)); tp_ = os.path.join(OUT, "stats", "templates.json"); tp = json.load(open(tp_))
for bid, src, mdl, tid, ttl in (("B1BaBaPunkR01", "B1BaBaPunk01", "exbloke3dr.pie", "BaBaPunkR", "BaBaPunk"),):
    v = json.loads(json.dumps(bs[src])); v.update({"id": bid, "model": mdl}); bs[bid] = v
    t = json.loads(json.dumps(tp[ttl])); t.update({"id": tid, "name": tid, "body": bid, "available": True}); tp[tid] = t
v = json.loads(json.dumps(bs["B1BaBaPerson01"])); v.update({"id": "B1BaBaPersonR01", "model": "exblokered.pie"}); bs["B1BaBaPersonR01"] = v
t = json.loads(json.dumps(tp["BaBaPeople"])); t.update({"id": "BaBaPeopleR", "body": "B1BaBaPersonR01", "available": False}); tp["BaBaPeopleR"] = t
json.dump(bs, open(bp, "w"), indent="\t", sort_keys=True); json.dump(tp, open(tp_, "w"), indent="\t", sort_keys=True)
print("Varianten: rote Hose (Sprite, 3D), Weste mit Spikes")
wp_ = os.path.join(OUT, "stats", "weapons.json"); ww = json.load(open(wp_))
for k in ("BabaPitRocket", "BabaPitRocketAT", "Mortar1Mk1", "BabaFlame", "BabaTowerMG", "MG3-Tower", "bbaawep", "BabaBuggyMG"):
    if k in ww: ww[k]["rotate"] = 360
json.dump(ww, open(wp_, "w"), indent="\t", sort_keys=True); print("Drehwinkel 360 gesetzt")
