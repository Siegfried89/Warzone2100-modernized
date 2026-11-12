"""Fass-Gruppen (Deko-/Fallen-Features) aus dem Spiel-Oelfass mioil.pie:
  oildrum5.pie   5 Faesser im Wuerfel-Fuenf-Muster (vier Ecken + Mitte) auf einer Kachel
  oildrum10.pie  dasselbe zweilagig gestapelt (10 Faesser)
Features OilDrum5 / OilDrum10: 1 Lebenspunkt, zerstoerbar (Explosion/Kettenreaktion folgt in der Programmdatei).
drums.py OUT"""
import sys, os, json, math
OUT = sys.argv[1]
SRC = "/tmp/claude-0/cmp/base/features/mioil.pie"
txt = open(SRC, encoding="latin-1").read().replace("\r", "").split("\n")
head = [l for l in txt[:5]]                       # PIE 3 / TYPE / TEXTURE / LEVELS / LEVEL
pts = []; pol = []
for i, l in enumerate(txt):
    if l.startswith("POINTS"):
        n = int(l.split()[1]); pts = [tuple(map(float, txt[i + 1 + k].split()[:3])) for k in range(n)]
    if l.startswith("POLYGONS"):
        n = int(l.split()[1]); pol = [txt[i + 1 + k].split() for k in range(n)]
SP = 30.0
FIVE = [(-SP, -SP), (SP, -SP), (0.0, 0.0), (-SP, SP), (SP, SP)]
def build(layers):
    P = []; Q = []
    k = 0
    for layer in range(layers):
        for (cx, cz) in FIVE:
            yaw = math.radians(15 * ((k * 7) % 5))                 # leicht verdreht: wirkt nicht wie kopiert
            c, s = math.cos(yaw), math.sin(yaw)
            base = len(P)
            for (x, y, z) in pts:
                P.append((x * c + z * s + cx, y + layer * 37.0, -x * s + z * c + cz))
            for v in pol:
                Q.append("\t%s %s %d %d %d %s" % (v[0], v[1], int(v[2]) + base, int(v[3]) + base, int(v[4]) + base, " ".join(v[5:])))
            k += 1
    out = head + ["POINTS %d" % len(P)] + ["\t%.3f %.3f %.3f" % p for p in P] + ["POLYGONS %d" % len(Q)] + Q + [""]
    return "\n".join(out)
os.makedirs(os.path.join(OUT, "features"), exist_ok=True)
for name, layers in (("oildrum5.pie", 1), ("oildrum10.pie", 2)):
    open(os.path.join(OUT, "features", name), "w", encoding="latin-1", newline="").write(build(layers))
fp = os.path.join(OUT, "stats", "features.json"); F = json.load(open(fp, encoding="utf-8"))
for fid, nm, mdl in (("OilDrum5", "Oil Drums (5)", "oildrum5.pie"), ("OilDrum10", "Oil Drums (10)", "oildrum10.pie")):
    d = dict(F["OilDrum"]); d.update({"id": fid, "name": nm, "model": mdl, "type": "BUILDING", "hitpoints": 1, "armour": 0, "damageable": 1, "width": 1, "breadth": 1})
    F[fid] = d
json.dump(F, open(fp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
print("Fass-Gruppen: oildrum5 (%d Punkte), oildrum10" % (5 * len(pts)))
