"""Nach dem Generator: Spieler-Gebaeude auf Scav-Modelle, Kollektiv/Nexus-Varianten (_col/_nex) bleiben erhalten. sc_post.py OUT"""
import json, os, sys, glob, shutil
OUT = sys.argv[1]
SP = os.path.join(OUT, "stats", "structure.json")
st = json.load(open(SP, encoding="utf-8")); import copy; json.dump(st, open(os.path.join(OUT, "stats", "structure_orig.json"), "w")); wp = json.load(open(os.path.join(OUT, "stats", "weapons.json"), encoding="utf-8"))
SRC = ["base/structs", "mp/structs", "extra/structs", "repl"]
def find(name):
    n = name.lower()
    for d in [os.path.join(OUT, "structs")] + SRC:
        for p in glob.glob(os.path.join(d, "*")):
            if os.path.basename(p).lower() == n: return p
def find_orig(name):          # Original-Modell (Spiel-Daten, unskaliert) fuer Varianten, die der Generator nicht ausgegeben hat
    n = name.lower()
    for d in SRC:
        for p in glob.glob(os.path.join(d, "*")):
            if os.path.basename(p).lower() == n: return p
ENEMY = ("CO-", "NX-", "Collective", "NEXUS", "Sys-NEXUS", "Sys-NX", "A0CommandCentreCO", "A0CommandCentreNE", "A0CommandCentreNP")
MODEL = {"FACTORY": "BLBRBFAC.PIE", "FACTORY MODULE": "BLBRBFAC.PIE", "POWER GENERATOR": "BLBRBGEN.PIE", "POWER MODULE": "BLBRBGEN.PIE",
         "RESEARCH": "scavlab.pie", "RESEARCH MODULE": "scavlab.pie", "HQ": "blhq1.pie", "CYBORG FACTORY": "BLBRBFAC.PIE",
         "VTOL FACTORY": "scavvtol.pie", "REARM PAD": "blvtolpd.pie", "REPAIR FACILITY": "repair.pie", "COMMAND RELAY": "bbaatower.pie"}
DEF = {"MACHINE GUN": "BLBRBTW1.PIE", "FLAME": "Blbrtowf.pie", "MORTARS": "BLBRMRTP.PIE", "HOWITZERS": "BLBRMRTP.PIE",
       "ROCKET": "EXROCKET.PIE", "SLOW ROCKET": "EXROCKET.PIE", "MISSILE": "EXROCKET.PIE", "A-A GUN": "bbaatower.pie"}
FP = {"FACTORY": (2, 1), "FACTORY MODULE": (2, 1), "CYBORG FACTORY": (2, 1), "VTOL FACTORY": (2, 2)}
EMPTY = "PIE 3\nTYPE 200\nINTERPOLATE 0\nTEXTURE 0 page-7-barbarians-arizona.png 0 0\nLEVELS 1\nLEVEL 1\nPOINTS 3\n\t0 0 0\n\t1 0 0\n\t0 0 1\nPOLYGONS 1\n\t200 3 0 1 2 0 0 0 0 0 0\n"
od = os.path.join(OUT, "structs"); os.makedirs(od, exist_ok=True)
def put(src, dst):
    shutil.copyfile(src, os.path.join(od, dst))
def variants(orig, new):
    n = 0
    stem, ext = os.path.splitext(orig)
    for suf in ("_col", "_nex"):
        p = find(stem + suf + ext)
        if p:
            s2, e2 = os.path.splitext(new); put(p, s2 + suf + e2); n += 1
    return n
n_s = n_v = 0
for k, v in st.items():
    if "type" not in v or k.startswith(ENEMY) or k.lower().startswith(("a0baba", "scavrepair", "bbaatow")): continue
    t = v["type"]; m = None
    if t in MODEL: m = MODEL[t]
    elif t == "DEFENSE":
        ws = v.get("weapons", []); sc = wp.get(ws[0], {}).get("weaponSubClass") if ws else None
        if not ws: continue
        m = DEF.get(sc, "BLBRTOWR.PIE")
    if not m: continue
    srcm = find(m)
    if not srcm: print("FEHLT Scav-Modell", m, "fuer", k); continue
    olds = list(v.get("structureModel", [])); news = []
    for o in olds:
        nn = "sc_" + os.path.splitext(m.lower())[0] + "_" + o.lower()
        put(srcm, nn); n_v += variants(o, nn); news.append(nn)
    v["structureModel"] = news or ["sc_" + m.lower()]
    if v.get("baseModel"):
        bn = "sc_" + v["baseModel"].lower()
        open(os.path.join(od, bn), "w").write(EMPTY); n_v += variants(v["baseModel"], bn); v["baseModel"] = bn
    if t in MODEL: v["width"], v["breadth"] = FP.get(t, (1, 1))
    elif t == "DEFENSE": v["width"] = v["breadth"] = 1
    for key, f in (("buildPower", 0.5), ("hitpoints", 0.7)):
        if isinstance(v.get(key), (int, float)): v[key] = max(1, int(round(v[key] * f)))
    if isinstance(v.get("buildPoints"), (int, float)) and t != "DEFENSE": v["buildPoints"] = max(1, int(round(v["buildPoints"] * 0.3)))
    n_s += 1
W = ["BLBRBWLH.PIE", "blbrbwlx.pie", "blbrbwlt.pie", "blbrbwll.pie"]
if "A0HardcreteMk1Wall" in st: st["A0HardcreteMk1Wall"]["structureModel"] = W
if "A0HardcreteMk1CWall" in st: st["A0HardcreteMk1CWall"]["structureModel"] = ["blbrbwlx.pie"]
# Scav-Verteidigung von Anfang an freischalten (Forschung R-Sys-Spade1Mk1 gibt die Strukturen frei)
RP = os.path.join(OUT, "stats", "research.json"); rs = json.load(open(RP, encoding="utf-8"))
free = [k for k in ("A0BaBaBunker", "A0BaBaFlameTower", "A0BaBaGunTower", "A0BaBaGunTowerEND", "A0BaBaHorizontalWall", "A0BaBaMortarPit", "A0BaBaRocketPit", "A0BaBaRocketPitAT", "A0TankTrap", "TankTrapC", "bbaatow") if k in st]
rs["R-Sys-Spade1Mk1"]["resultStructures"] = sorted(set(rs["R-Sys-Spade1Mk1"].get("resultStructures", []) + free))
json.dump(rs, open(RP, "w", encoding="utf-8"), indent="\t", sort_keys=True); print("Frei ab Start:", free)
json.dump(st, open(SP, "w", encoding="utf-8"), indent="\t", sort_keys=True)
print("Gebaeude umgestellt:", n_s, "Varianten (_col/_nex) behalten:", n_v)
