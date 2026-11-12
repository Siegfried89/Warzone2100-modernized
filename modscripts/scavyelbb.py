"""Bunkerbrecher-Varianten (Bazooka) fuer beide Gelb-Toene des Sunpunk: knalliges Buggy-Gelb (y) und weiches New-Paradigm-Gelb (n).
Gleiches Verfahren wie more.py (Dreadhawk RPG): Rohr aus gnmrktbb.pie in der Hand, Texturseite auf 256x512 erweitert. scavyelbb.py OUT"""
import sys as _s, os as _o; _s.path.insert(0, _o.path.dirname(_o.path.abspath(__file__)))
import json, os, sys
from PIL import Image
import newguys_lib as ng, bbtube
OUT = sys.argv[1]; bbtube.OUT = OUT
B = os.path.join(OUT, "components", "bodies"); TP = os.path.join(OUT, "texpages")
rd, wr = bbtube.rd, bbtube.wr
for sfx, page, newpage in (("y", "page-32-punk-sun.png", "page-32-punk-sun-bb.png"), ("n", "page-32-punk-npy.png", "page-32-punk-npy-bb.png")):
    src = Image.open(os.path.join(TP, page)).convert("RGBA")
    big = Image.new("RGBA", (256, 512), (0, 0, 0, 255)); big.paste(src, (0, 0)); bbtube.paste_bb(big).save(os.path.join(TP, newpage))
    for base in ("exbloke", "runanim", "fireknee"):
        a, b = "3d" + sfx, "3d" + sfx + "b"
        t = rd(os.path.join(B, base + a + ".pie")).replace("runanim" + a + ".pie", "runanim" + b + ".pie").replace("fireknee" + a + ".pie", "fireknee" + b + ".pie")
        t = ng.scale_v(t.replace(page, newpage))
        f = os.path.join(B, base + b + ".pie"); wr(f, t); bbtube.level10(f)
bp = os.path.join(OUT, "stats", "body.json"); bs = json.load(open(bp)); tp_ = os.path.join(OUT, "stats", "templates.json"); tp = json.load(open(tp_))
v = json.loads(json.dumps(bs["B1BaBaSun01"])); v.update({"id": "B1BaBaSunB01", "model": "exbloke3dyb.pie", "alternativeModels": ["exbloke3dnb.pie"], "name": "Sunpunk RPG"}); bs["B1BaBaSunB01"] = v
t = json.loads(json.dumps(tp["BaBaSun"])); t.update({"id": "BaBaSunB", "name": "Sunpunk RPG", "body": "B1BaBaSunB01", "available": True, "weapons": ["BabaBB"]}); tp["BaBaSunB"] = t
json.dump(bs, open(bp, "w"), indent="\t", sort_keys=True); json.dump(tp, open(tp_, "w"), indent="\t", sort_keys=True)
print("Sunpunk RPG: Bunkerbrecher fuer beide Gelb-Toene")
