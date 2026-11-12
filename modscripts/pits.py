"""Stellungen in 4 Richtungen: pits.py OUT"""
import json, os, re, sys
OUT = sys.argv[1]; ST = os.path.join(OUT, "stats")
BASE = "/tmp/claude-0/cmp/base/structs/exrocket.pie"
def rot(src, k):  # k*90 Grad: x'=z, z'=-x (wie beim Moerser)
    out = []; sect = None
    for ln in open(src).read().split("\n"):
        t = ln.strip().split()
        if t and t[0] in ("POINTS", "CONNECTORS"): sect = t[0]; out.append(ln); continue
        if t and t[0] in ("POLYGONS", "LEVEL", "LEVELS"): sect = None
        if sect and len(t) == 3:
            x, y, z = map(float, t)
            for _ in range(k): x, z = z, -x
            ln = "\t%g %g %g" % (x, y, z)
        out.append(ln)
    return "\n".join(out)
for k, a in ((1, 90), (2, 180), (3, 270)):
    open(os.path.join(OUT, "structs", "exrocket_r%d.pie" % a), "w").write(rot(BASE, k))
S = json.load(open(os.path.join(ST, "structure.json")))
for base in ("A0BaBaRocketPit", "A0BaBaRocketPitAT"):
    for a in (90, 180, 270):
        n = "%sR%d" % (base, a); d = dict(S[base]); d["id"] = n; d["structureModel"] = ["exrocket_r%d.pie" % a]; S[n] = d
for base, nm in (("A0BaBaMortarPit", "Mortar Pit"), ("A0BaBaRocketPit", "Rocket Pit"), ("A0BaBaRocketPitAT", "AT Rocket Pit")):
    S[base]["name"] = nm + " 0"
    for a in (90, 180, 270): S["%sR%d" % (base, a)]["name"] = "%s %d" % (nm, a)
json.dump(S, open(os.path.join(ST, "structure.json"), "w"), indent="\t", sort_keys=True)
R = json.load(open(os.path.join(ST, "research.json")))
r = R["R-Sys-Spade1Mk1"]["resultStructures"]
for base in ("A0BaBaMortarPit", "A0BaBaRocketPit", "A0BaBaRocketPitAT"):
    for a in (90, 180, 270):
        if base + "R%d" % a not in r: r.append(base + "R%d" % a)
json.dump(R, open(os.path.join(ST, "research.json"), "w"), indent="\t", sort_keys=True)
print("Stellungen gedreht")
