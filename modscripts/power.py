"""Stromnetz-Daten: Strommast (Holzmast mit Querbalken und drei Isolatoren, 1x1 Feld), Struktur-Typ GENERIC (ziehbar wie eine Mauer, Luecke von einem Feld),
Kennung A0PowerPole*; Engine-Teil modgrid.cpp verbindet Masten, zeichnet das Kabel und versorgt Gebaeude mit "requiresPower".
Aussenposten (A0BaBaOutpost, A0BaBaOutpostG) bekommen requiresPower = true.
power.py OUT"""
import sys, os, json, copy
import numpy as np

OUT = sys.argv[1]
PAGE = "page-431-trench.png"
SIZE = 512.0
WOOD = (360 + 8, 6 + 4)
WOOD_DK = (360 + 22, 6 + 24)
MUD = (80 + 30, 6 + 30)


class Model:
    def __init__(self):
        self.pts = []; self.pol = []
    def p(self, v):
        self.pts.append(tuple(float(c) for c in v)); return len(self.pts) - 1
    def tri(self, a, b, c, uv, centre):
        A, B, C = (np.array(self.pts[i]) for i in (a, b, c))
        n = np.cross(B - A, C - A)
        if n @ ((A + B + C) / 3.0 - centre) < 0: b, c = c, b
        self.pol.append((a, b, c, uv))
    def quad(self, v, uv, centre):
        ids = [self.p(x) for x in v]
        self.tri(ids[0], ids[1], ids[2], uv, centre); self.tri(ids[0], ids[2], ids[3], uv, centre)
    def box(self, x0, x1, y0, y1, z0, z1, tex):
        c = np.array([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2]); u = ((tex[0] + 0.5) / SIZE, (tex[1] + 0.5) / SIZE)
        for q in (((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)), ((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)),
                  ((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)), ((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)),
                  ((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0))):
            self.quad(q, u, c)
    def text(self):
        out = ["PIE 3", "TYPE 200", "TEXTURE 0 %s 0 0" % PAGE, "LEVELS 1", "LEVEL 1", "POINTS %d" % len(self.pts)]
        out += ["\t%.3f %.3f %.3f" % p for p in self.pts]
        out.append("POLYGONS %d" % len(self.pol))
        for a, b, c, uv in self.pol:
            out.append("\t200 3 %d %d %d %.6f %.6f %.6f %.6f %.6f %.6f" % (a, b, c, uv[0], uv[1], uv[0], uv[1], uv[0], uv[1]))
        out += ["CONNECTORS 1", "\t0 0 0", ""]
        return "\n".join(out)


def pole():
    m = Model()
    m.box(-9, 9, 0, 5, -9, 9, MUD)                 # Fuss
    m.box(-3.5, 3.5, 5, 150, -3.5, 3.5, WOOD)      # Mast
    m.box(-22, 22, 128, 135, -2.5, 2.5, WOOD)      # Querbalken
    m.box(-16, -13, 135, 147, -1.5, 1.5, WOOD_DK)  # Isolatoren
    m.box(-1.5, 1.5, 150, 160, -1.5, 1.5, WOOD_DK)
    m.box(13, 16, 135, 147, -1.5, 1.5, WOOD_DK)
    m.box(-12, -6, 112, 118, -1.5, 1.5, WOOD)      # Streben
    m.box(6, 12, 112, 118, -1.5, 1.5, WOOD)
    return m


def main():
    os.makedirs(os.path.join(OUT, "structs"), exist_ok=True)
    open(os.path.join(OUT, "structs", "scavpole.pie"), "w", encoding="latin-1", newline="").write(pole().text())
    sp = os.path.join(OUT, "stats", "structure.json"); S = json.load(open(sp, encoding="utf-8"))
    e = copy.deepcopy(S["A0BaBaSandbagWall"])
    e.update({"id": "A0PowerPole", "name": "Power Pole", "type": "GENERIC", "structureModel": ["scavpole.pie"], "width": 1, "breadth": 1,
              "hitpoints": 90, "buildPoints": 8, "buildPower": 8, "armour": 2, "thermal": 2, "height": 4})
    e.pop("sensorID", None)
    S["A0PowerPole"] = e
    n = 0
    for k in ("A0BaBaOutpost", "A0BaBaOutpostG"):
        if k in S: S[k]["requiresPower"] = True; n += 1
    json.dump(S, open(sp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    rp = os.path.join(OUT, "stats", "research.json"); R = json.load(open(rp, encoding="utf-8"))
    rs = R["R-Sys-Spade1Mk1"].setdefault("resultStructures", [])
    if "A0PowerPole" not in rs: rs.append("A0PowerPole")
    json.dump(R, open(rp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Stromnetz: Mast A0PowerPole angelegt, %d Aussenposten brauchen Strom" % n)


main()
