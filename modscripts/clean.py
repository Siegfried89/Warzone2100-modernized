"""Aufraeumen: Sprite/Zivilist raus, KI-Vorlagen verstecken, Bagger nur einer, saubere kurze Namen. clean.py OUT"""
import json, os, re, sys
OUT = sys.argv[1]; ST = os.path.join(OUT, "stats")
def J(n): return json.load(open(os.path.join(ST, n + ".json")))
def S(n, d): json.dump(d, open(os.path.join(ST, n + ".json"), "w"), indent="\t", sort_keys=True)
T, B, P, W, Sx = J("templates"), J("body"), J("propulsion"), J("weapons"), J("structure")
def clean(n):
    n = re.sub(r"\*|\(Scav\)|\bBaBa\s*|\bBaba\s*", "", n or "")
    n = re.sub(r"\s*Cyborg\b", "", n)
    return re.sub(r"\s+", " ", n).strip()
# 1) Sprite-Infanterie, Zivilist, Vorlagen ohne Waffe (ausser Bau/Reparatur/Sensor) raus
for k in ("BaBaPeople", "BaBaCivilian", "BaBaPeopleR", "BabaPickUp"):
    if k in T: T[k]["available"] = False
# 2) KI-Vorlagen (Kollektiv/Nexus/NP/Skirmish-KI) nicht in der Spielerliste
AI = ("CO-", "NX-", "NP-", "SK-", "P0")
for k, t in T.items():
    if isinstance(t, dict) and t.get("available") and (k.startswith(AI) or str(t.get("name", "")).startswith("*")): t["available"] = False
# 3) Bagger: nur eine Vorlage (Raeder)
SCAVTRUCKS = ("ScavPickup", "ScavLkw", "ScavTraktor", "ScavRadlader", "ScavTanker")   # Fracht-Lkw bleiben alle verfuegbar
cons = [k for k, t in T.items() if isinstance(t, dict) and t.get("available") and t.get("type") == "CONSTRUCT" and k not in SCAVTRUCKS]
keep = next((k for k in sorted(cons, key=len) if "heel" in T[k].get("propulsion", "") or T[k].get("propulsion", "").startswith("wheeled01")), None)
for k in cons:
    if k != keep: T[k]["available"] = False
# 4) Namen
SUF = {"Half-Tracked": " HT", "Tracked": " TR", "Hover": " HV"}
INF = {"BaBaVest": "Marauder", "BaBaPunk": "Punk", "BaBaPunkR": "Hellpunk", "BaBaPunkW": "Wildmane", "BaBaSun": "Sunpunk", "BaBaVestS": "Spikeguard", "BaBaDread": "Dreadhawk", "BaBaDreadB": "Dreadhawk RPG", "BaBaVestSB": "Spikeguard RPG", "BaBaDreadV": "Dread Raider"}
for k, t in T.items():
    if not isinstance(t, dict): continue
    if k in INF: t["name"] = INF[k]; continue
    n = clean(t.get("name", k)); typ = P.get(t.get("propulsion"), {}).get("type")
    n = re.sub(r"\b(Half[- ]?tracks?|Tracks?|Wheels|Hover)\b", "", n, flags=re.I); n = re.sub(r"\s+", " ", n).strip()
    if typ in SUF: n += SUF[typ]
    t["name"] = n
if "Cyb-ComEng-Scav" in T: T["Cyb-ComEng-Scav"]["name"] = "Pionier"
for d in (B, P, W):
    for k, v in d.items():
        if isinstance(v, dict) and "name" in v: v["name"] = clean(v["name"]) or v["name"]
for k, v in B.items():
    if isinstance(v, dict) and k in ("B1BaBaPerson01", "B1BaBaPerson01-Ultimate"): v["name"] = "Scav"
    if k == "B1BaBaPunk01" or k == "B1BaBaPunkR01": v["name"] = "Punk"
    if k == "B1BaBaVest01": v["name"] = "Marauder"
for k, v in Sx.items():
    if isinstance(v, dict) and "name" in v: v["name"] = clean(v["name"]) or v["name"]
for n, d in (("templates", T), ("body", B), ("propulsion", P), ("weapons", W), ("structure", Sx)): S(n, d)
# 5) Dateien mit Grossbuchstaben (Engine sucht klein) entfernen
rem = 0
for root, _, fs in os.walk(os.path.join(OUT, "components")):
    for f in fs:
        if f != f.lower() and f.lower().endswith(".pie") and not os.path.exists(os.path.join(root, f.lower())): os.rename(os.path.join(root, f), os.path.join(root, f.lower())); rem += 1
print("Aufgeraeumt; verfuegbare Vorlagen:", sum(1 for t in T.values() if isinstance(t, dict) and t.get("available")), "Dateien klein benannt:", rem)
