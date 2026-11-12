"""Graeben und Gruben: Infanterie darf hinein (infantryPassable), tiefe Varianten senken das Gelaende ab (Engine, modoil). passable.py OUT"""
import sys, os, json
OUT = sys.argv[1]
p = os.path.join(OUT, "stats", "structure.json"); S = json.load(open(p, encoding="utf-8")); n = 0
for k in ("A0BaBaTrench", "A0BaBaTrenchCover", "A0BaBaTrenchDeep", "A0BaBaTrenchDeepCover", "A0BaBaPitSquare", "A0BaBaPitHex", "A0BaBaPitSquareDeep", "A0BaBaPitHexDeep"):
    if k in S: S[k]["infantryPassable"] = True; n += 1
json.dump(S, open(p, "w", encoding="utf-8"), indent="\t", sort_keys=True)
print("Graeben/Gruben fuer Infanterie begehbar:", n)
