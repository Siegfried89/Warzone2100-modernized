import json, os, sys
SB, SM, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
FILES = ["repair", "weapons", "body", "propulsion", "sensor", "structure", "templates", "research", "features"]
os.makedirs(OUT, exist_ok=True)
def L(d, f):
    p = os.path.join(d, f + ".json")
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}
U = {}
for f in FILES:
    b, m = L(SB, f), L(SM, f)
    u = json.loads(json.dumps(m)); u.update(json.loads(json.dumps(b)))      # Kampagne hat Vorrang bei gleicher ID
    if f == "research":   # Forschungsbaum: Skirmish-Baum gewinnt (zusammenhaengende Upgrade-Ketten), Kampagne-only IDs werden ergaenzt
        u = json.loads(json.dumps(b)); u.update(json.loads(json.dumps(m)))
    U[f] = u
    json.dump(u, open(os.path.join(OUT, f + ".json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print(f, "base", len(b), "mp", len(m), "union", len(u))
# --- Validierung der Querverweise ---
other = {f: {**L(SM, f), **L(SB, f)} for f in ("construction", "ecm", "repair", "brain", "sensor")}
ob = {f: (set(L(SB, f)), set(L(SM, f))) for f in ("construction", "ecm", "repair", "brain")}
bad = []
for k, t in U["templates"].items():
    if not isinstance(t, dict): continue
    if t.get("body") not in U["body"]: bad.append(("tpl-body", k, t.get("body")))
    if t.get("propulsion") not in U["propulsion"]: bad.append(("tpl-prop", k, t.get("propulsion")))
    for w in t.get("weapons", []):
        if w not in U["weapons"]: bad.append(("tpl-weap", k, w))
    for fld in ("construction", "ecm", "repair", "brain", "sensor"):
        v = t.get(fld)
        if v and v not in other.get(fld, {}) and v not in U.get(fld, {}):
            bad.append(("tpl-" + fld, k, v))
        elif v and fld in ob:
            bb, mm = ob[fld]
            if v not in bb or v not in mm: bad.append(("tpl-" + fld + "-onemode", k, v))
for k, r in U["research"].items():
    for q in r.get("requiredResearch", []):
        if q not in U["research"]: bad.append(("res-req", k, q))
    for q in r.get("resultStructures", []):
        if q not in U["structure"]: bad.append(("res-struct", k, q))
    for q in r.get("resultComponents", []):
        if not any(q in U[x] for x in ("weapons", "body", "propulsion", "sensor")) and not any(q in L(d, x) for d in (SB, SM) for x in ("construction", "ecm", "repair", "brain")): bad.append(("res-comp", k, q))
from collections import Counter
print(Counter(b[0] for b in bad))
for b in bad[:25]: print(b)
json.dump(bad, open(os.path.join(OUT, "_bad.json"), "w"), indent=1)
# --- weitere Stats-Dateien: Vereinigung (tief), damit Kampagne und Skirmish alle IDs finden ---
def deep(a, b):
    r = json.loads(json.dumps(a))
    for k, v in b.items():
        r[k] = deep(r[k], v) if isinstance(v, dict) and isinstance(r.get(k), dict) else v
    return r
for f in ("construction", "ecm", "brain", "propulsiontype", "weaponmodifier", "structuremodifier"):
    b, m = L(SB, f), L(SM, f)
    if not b and not m: continue
    u = deep(m, b)
    json.dump(u, open(os.path.join(OUT, "x_" + f + ".json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("extra", f, len(b), len(m), len(u))
