"""Vor dem Generator: Scav-Klone fuer Rumpfe/Antriebe/Cyborgs, Forschung + Vorlagen. sc_pre.py IN OUT"""
import json, os, sys, re
IN, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
J = lambda f: json.load(open(os.path.join(IN, f + ".json"), encoding="utf-8"))
st, bd, pr, wp, tp, rs = J("structure"), J("body"), J("propulsion"), J("weapons"), J("templates"), J("research")
S = "-Scav"
TANK = {"Body1REC": ("exbuggy.pie", "Buggy"), "Body4ABT": ("extrike.pie", "Trike"), "Body2SUP": ("exjeep.pie", "Jeep"),
        "Body3MBT": ("exjeep.pie", "Raider"), "Body5REC": ("camper.pie", "Camper"), "Body6SUPP": ("icevan.pie", "Icevan"),
        "Body8MBT": ("scavtruck.pie", "Truck"), "Body7ABT": ("scavnexus.pie", "Hauler"), "Body9REC": ("nextrack.pie", "Crawler"),
        "Body11ABT": ("exschool.pie", "Bus"), "Body12SUP": ("exfire.pie", "Firetruck"), "Body10MBT": ("nextrack.pie", "Juggernaut"),
        "Body13SUP": ("tractor.pie", "Tractor"), "Body14SUP": ("scavtruck.pie", "Big Rig")}
OLDNAME = {"Body1REC": "Viper", "Body4ABT": "Bug", "Body2SUP": "Leopard", "Body3MBT": "Retaliation", "Body5REC": "Cobra",
           "Body6SUPP": "Panther", "Body8MBT": "Scorpion", "Body7ABT": "Retribution", "Body9REC": "Tiger", "Body11ABT": "Python",
           "Body12SUP": "Mantis", "Body10MBT": "Vengeance", "Body13SUP": "Wyvern", "Body14SUP": "Dragon"}
KEEP_X = {"V-Tol", "V-Tol02", "V-Tol03", "Helicopter", "Naval"}
bmap = {}
for k, (m, nm) in TANK.items():
    if k not in bd: continue
    v = json.loads(json.dumps(bd[k])); v.update({"id": k + S, "model": m, "class": "Babas", "name": "%s" % nm})
    v["propulsionExtraModels"] = {a: b for a, b in (v.get("propulsionExtraModels") or {}).items() if a in KEEP_X}
    for key, f in (("buildPower", 0.5), ("hitpoints", 0.8)):
        if isinstance(v.get(key), (int, float)): v[key] = max(1, int(round(v[key] * f)))
    if k == "Body14SUP": v["weaponSlots"] = 3
    bd[k + S] = v; bmap[k] = k + S
for k, v0 in list(bd.items()):
    if v0.get("class") == "Cyborgs" and "NX" not in k and not k.endswith(S):
        v = json.loads(json.dumps(v0)); v.update({"id": k + S, "model": "exbloke3d.pie", "name": "*Scav Infantry*"})
        v.pop("propulsionExtraModels", None)
        bd[k + S] = v; bmap[k] = k + S
pmap = {}
for k, v0 in list(pr.items()):
    if v0.get("type") in ("Wheeled", "Half-Tracked", "Tracked", "Hover", "Legged") and k not in ("ZNULLPROP", "BaBaProp", "BaBaLegs"):
        v = json.loads(json.dumps(v0)); v["id"] = k + S; v.pop("model", None)
        if v0.get("type") != "Legged":
            for key, f in (("buildPower", 0.5),):
                if isinstance(v.get(key), (int, float)): v[key] = max(1, int(round(v[key] * f)))
        if isinstance(v.get("speed"), (int, float)): v["speed"] = int(v["speed"] * 2)   # gleicht --speed-factor 0.5 aus: Scav-Fahrzeuge flott
        if isinstance(v.get("weight"), (int, float)): v["weight"] = max(10, int(v["weight"] * 0.3))
        pr[k + S] = v; pmap[k] = k + S
# --- Forschung: Scav-Klone zusaetzlich freischalten ---
n_r = 0
for k, r in rs.items():
    rc = r.get("resultComponents")
    if not rc: continue
    add = [bmap[c] for c in rc if c in bmap] + [pmap[c] for c in rc if c in pmap]
    if add: r["resultComponents"] = rc + [a for a in add if a not in rc]; n_r += 1
# --- Vorlagen: Scav-Klone, fertig als Liste in der Fabrik ---
n_t = 0
new = {}
for k, t in tp.items():
    if not isinstance(t, dict) or k.endswith(S): continue
    b, p = t.get("body"), t.get("propulsion")
    if b not in bmap: continue
    c = json.loads(json.dumps(t)); c["id"] = k + S; c["body"] = bmap[b]; c["propulsion"] = pmap.get(p, p)
    nm = c.get("name", k)
    if b in OLDNAME: nm = re.sub(OLDNAME[b], TANK[b][1], nm)
    else: nm = nm + " (Scav)" if "Scav" not in nm else nm
    c["name"] = nm; c["available"] = True
    if t.get("construct") and "B2crane1" in bd: c["body"] = "B2crane2"; c["construct"] = "scavCrane2"; c["name"] = "Bagger"   # Bagger/Kran mit Arm
    new[k + S] = c
    if t.get("available") and t.get("type") != "TRANSPORTER": t["available"] = False
    n_t += 1
tp.update(new)
for d_, m_ in ((bd, bmap), (pr, pmap)):      # Project-Originale nicht mehr im Editor waehlbar
    for k_ in m_:
        if k_ in d_ and "designable" in d_[k_]: d_[k_]["designable"] = 0
# Spieler-0-Startvorlagen der Kampagne direkt (ohne Kartenaenderung) auf Scav-Teile umstellen, wenn Gegner sie nicht nutzen
import glob, struct
P0, EN = set(), set()
for f in glob.glob("/tmp/claude-0/cmp/base/wrf/**/dinit.bjo", recursive=True):
    o = open(f, "rb").read()
    for i in range(struct.unpack("<I", o[8:12])[0]):
        b = o[12 + i * 76:12 + (i + 1) * 76]; nm = b[:60].split(b"\0")[0].decode()
        (P0 if struct.unpack("<I", b[60:64])[0] == 0 else EN).add(nm)
n_in = 0
for k in sorted(P0 - EN):
    if k in new or k + S in new:
        src = tp[k + S]; t = tp[k]
        t["body"], t["propulsion"] = src["body"], src["propulsion"]
        if src.get("construct"): t["construct"] = src["construct"]
        t["name"] = src["name"]; t["available"] = tp[k].get("available", False)
        n_in += 1
print("Start-Vorlagen direkt umgestellt:", n_in, sorted(P0 - EN))
# Duplikate in der Fabrikliste entfernen: gleiche Teile -> nur eine Vorlage verfuegbar
seen = {}
for k_ in sorted(tp, key=lambda x: (len(x), x)):
    t_ = tp[k_]
    if not isinstance(t_, dict) or not t_.get("available") or t_.get("type") == "TRANSPORTER": continue
    sig = (t_.get("body"), t_.get("propulsion"), tuple(t_.get("weapons", [])), t_.get("construct"), t_.get("sensor"), t_.get("repair"), t_.get("ecm"), t_.get("brain"))
    if sig in seen: t_["available"] = False
    else: seen[sig] = k_
for nm, d in (("structure", st), ("body", bd), ("propulsion", pr), ("templates", tp), ("research", rs)):
    json.dump(d, open(os.path.join(OUT, nm + ".json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
for f in ("weapons", "sensor", "features", "repair"):
    json.dump(J(f), open(os.path.join(OUT, f + ".json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
print("Klone: Rumpfe", len(bmap), "Antriebe", len(pmap), "Forschungen", n_r, "Vorlagen", n_t)
