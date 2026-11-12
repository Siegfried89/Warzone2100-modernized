import json
f = "out_u/stats/templates.json"; tp = json.load(open(f)); seen = {}; n = 0
for k in sorted(tp, key=lambda x: (len(x), x)):
    t = tp[k]
    if not isinstance(t, dict) or not t.get("available") or t.get("type") == "TRANSPORTER": continue
    sig = (t.get("body"), t.get("propulsion"), tuple(t.get("weapons", [])), t.get("construct"), t.get("sensor"), t.get("repair"), t.get("ecm"), t.get("brain"))
    if sig in seen: t["available"] = False; n += 1
    else: seen[sig] = k
json.dump(tp, open(f, "w"), indent="\t", sort_keys=True); print("Duplikat-Vorlagen versteckt:", n, "verfuegbar:", len(seen))
