"""Zwei neue 3D-Scavenger (Oberkoerper frei, keine Schulterplatte; gelb mit WEISSEM Mohawk, NP-gelb mit niedrigem braunem Kamm): knalliges Buggy-Gelb und weiches New-Paradigm-Gelb.
Beide als EIN Eintrag 'Sunpunk' (Koerper mit alternativeModels): die Engine laesst sie abwechselnd aus der Fabrik kommen. scavyel.py OUT"""
import sys as _s, os as _o; _s.path.insert(0, _o.path.dirname(_o.path.abspath(__file__)))
import json, os, sys
from PIL import Image
import yelcol, combhair
OUT = sys.argv[1]; B = os.path.join(OUT, "components", "bodies"); TP = os.path.join(OUT, "texpages")
def rd(p): return open(p, encoding="latin-1").read()
def wr(p, t): open(p, "w", encoding="latin-1", newline="").write(t)
base = Image.open(os.path.join(TP, "page-32-punk.png"))
yelcol.to_yellow(base, yelcol.BUGGY_YELLOW).save(os.path.join(TP, "page-32-punk-sun.png"))
yelcol.to_yellow(base, yelcol.NP_YELLOW, white_hair=False, brown_hair=True).save(os.path.join(TP, "page-32-punk-npy.png"))
for sfx, page in (("s", "page-32-punk-sun.png"), ("n", "page-32-punk-npy.png")):
    for a in ("exbloke3d", "runanim3d", "fireknee3d"):
        t = rd(os.path.join(B, a + ".pie")).replace("runanim3d.pie", "runanim3d%s.pie" % sfx).replace("fireknee3d.pie", "fireknee3d%s.pie" % sfx).replace("page-32-punk.png", page)
        if sfx == "n":   # New-Paradigm-Variante: niedriger brauner Kamm statt weisser Stacheln (in allen Posen)
            t = combhair.make_comb_text(rd(os.path.join(B, "exbloke3d.pie")), t, (219 / 256, 82 / 256))
        wr(os.path.join(B, "%s%s.pie" % (a, sfx)), t)
bp = os.path.join(OUT, "stats", "body.json"); bs = json.load(open(bp)); tp_ = os.path.join(OUT, "stats", "templates.json"); tp = json.load(open(tp_))
v = json.loads(json.dumps(bs["B1BaBaPunk01"])); v.update({"id": "B1BaBaSun01", "model": "exbloke3ds.pie", "alternativeModels": ["exbloke3dn.pie"], "name": "Sunpunk"}); bs["B1BaBaSun01"] = v
t = json.loads(json.dumps(tp["BaBaPunk"])); t.update({"id": "BaBaSun", "name": "Sunpunk", "body": "B1BaBaSun01", "available": True}); tp["BaBaSun"] = t
json.dump(bs, open(bp, "w"), indent="\t", sort_keys=True); json.dump(tp, open(tp_, "w"), indent="\t", sort_keys=True)
print("Sunpunk: gelb + NP-gelb, abwechselnd")
