"""Stellt die Spieler-Seite (Project) auf Scav-Optik um. scavify.py IN_DIR OUT_DIR"""
import json, os, sys
IN, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
J = lambda f: json.load(open(os.path.join(IN, f + ".json"), encoding="utf-8"))
st, bd, pr, wp = J("structure"), J("body"), J("propulsion"), J("weapons")
ENEMY = ("CO-", "NX-", "Collective", "NEXUS", "Sys-NEXUS", "Sys-NX", "A0CommandCentreCO", "A0CommandCentreNE", "A0CommandCentreNP")
MODEL = {"FACTORY": "BLBRBFAC.PIE", "FACTORY MODULE": "BLBRBFAC.PIE", "POWER GENERATOR": "BLBRBGEN.PIE", "POWER MODULE": "BLBRBGEN.PIE",
         "RESEARCH": "scavlab.pie", "RESEARCH MODULE": "scavlab.pie", "HQ": "blhq1.pie", "CYBORG FACTORY": "BLBRBFAC.PIE",
         "VTOL FACTORY": "scavvtol.pie", "REARM PAD": "blvtolpd.pie", "REPAIR FACILITY": "repair.pie", "COMMAND RELAY": "bbaatower.pie"}
DEF = {"MACHINE GUN": "BLBRBTW1.PIE", "FLAME": "Blbrtowf.pie", "MORTARS": "BLBRMRTP.PIE", "HOWITZERS": "BLBRMRTP.PIE",
       "ROCKET": "EXROCKET.PIE", "SLOW ROCKET": "EXROCKET.PIE", "MISSILE": "EXROCKET.PIE", "A-A GUN": "bbaatower.pie"}
DEF_DEFAULT = "BLBRTOWR.PIE"
n_s = 0
for k, v in st.items():
    if "type" not in v or k.startswith(ENEMY) or k.startswith("A0BaBa") or k.startswith("A0Baba") or k == "ScavRepairCentre": continue
    t = v["type"]; m = None
    if t in MODEL: m = MODEL[t]
    elif t == "DEFENSE":
        ws = v.get("weapons", []); sc = wp.get(ws[0], {}).get("weaponSubClass") if ws else None
        if sc is None and not ws: continue          # Sensor-/Kuehlturm usw. unveraendert
        m = DEF.get(sc, DEF_DEFAULT)
    if not m: continue
    n_lv = max(1, len(v.get("structureModel", [])))
    v["structureModel"] = [m] * n_lv if t in ("FACTORY", "RESEARCH", "POWER GENERATOR", "VTOL FACTORY") else [m]
    v.pop("baseModel", None)
    FP = {"FACTORY": (2, 1), "FACTORY MODULE": (2, 1), "CYBORG FACTORY": (2, 1), "VTOL FACTORY": (2, 2)}
    if t in MODEL: v["width"], v["breadth"] = FP.get(t, (1, 1))
    elif t == "DEFENSE": v["width"] = v["breadth"] = 1
    for key, f in (("buildPower", 0.5), ("hitpoints", 0.7)):
        if isinstance(v.get(key), (int, float)): v[key] = max(1, int(round(v[key] * f)))
    if isinstance(v.get("buildPoints"), (int, float)) and t != "DEFENSE": v["buildPoints"] = max(1, int(round(v["buildPoints"] * 0.3)))
    n_s += 1
# --- Rumpfe ---
BODY = {"Body1REC": "exbuggy.pie", "Body4ABT": "extrike.pie", "Body2SUP": "exjeep.pie", "Body3MBT": "exjeep.pie",
        "Body5REC": "camper.pie", "Body6SUPP": "icevan.pie", "Body8MBT": "scavtruck.pie", "Body7ABT": "scavnexus.pie",
        "Body9REC": "nextrack.pie", "Body11ABT": "exschool.pie", "Body12SUP": "exfire.pie", "Body10MBT": "nextrack.pie",
        "Body13SUP": "tractor.pie", "Body14SUP": "scavtruck.pie"}
KEEP_X = {"V-Tol", "V-Tol02", "V-Tol03", "Helicopter", "Naval"}
n_b = 0
for k, m in BODY.items():
    if k not in bd: continue
    v = bd[k]; v["model"] = m; v["class"] = "Babas"
    px = v.get("propulsionExtraModels")
    if px: v["propulsionExtraModels"] = {a: b for a, b in px.items() if a in KEEP_X}
    for key, f in (("buildPower", 0.5), ("hitpoints", 0.8)):
        if isinstance(v.get(key), (int, float)): v[key] = max(1, int(round(v[key] * f)))
    if k == "Body14SUP": v["weaponSlots"] = 3
    n_b += 1
# --- Antriebe: Bodenantriebe ohne eigenes Modell (Scav-Rumpf bringt Raeder mit) ---
n_p = 0
for k, v in pr.items():
    if v.get("type") in ("Wheeled", "Half-Tracked", "Tracked", "Hover") and k not in ("ZNULLPROP", "BaBaProp"):
        v.pop("model", None); n_p += 1
for nm, d in (("structure", st), ("body", bd), ("propulsion", pr)):
    json.dump(d, open(os.path.join(OUT, nm + ".json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
for f in ("weapons", "sensor", "templates", "research", "features", "repair"):
    json.dump(J(f), open(os.path.join(OUT, f + ".json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
print("Gebaeude:", n_s, "Rumpfe:", n_b, "Antriebe:", n_p)
