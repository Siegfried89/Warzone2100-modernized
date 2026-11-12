"""Schacht (1x1 Feld): Zugang zum Untergrund. Betonkragen mit dunkler Oeffnung, Warnmarkierungen, Foerdergeruest mit Seilwinde.
Struktur A0LayerShaft (Typ GENERIC), freigeschaltet mit R-Sys-Spade1Mk1. Engine-Teil: modlayer.cpp (Einheit faehrt hinein -> Ebene wechselt).
shaft.py OUT"""
import sys, os, json, math, copy
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "refinery.py"), encoding="utf-8").read().split("def build():")[0]
exec(src)


def build_shaft():
    m = Model()
    m.box(-62, 62, 0, 3, -62, 62, DARKP, DARKP)                              # Bodenplatte
    m.prism(0.0, 0.0, 52.0, 3, 24, 8, CORR, STEEL)                           # Kragen aus Wellblech/Stahl
    m.prism(0.0, 0.0, 34.0, 24, 26, 8, DARKP, DARKP)                         # dunkle Schachtoeffnung
    for sx in (-1, 1):
        for sz in (-1, 1):
            m.box(sx * 52 - 4, sx * 52 + 4, 3, 30, sz * 52 - 4, sz * 52 + 4, ORANGE, ORANGE)   # Warnpfosten
    for sz in (-1, 1):
        m.box(-56, -48, 3, 112, sz * 40 - 3, sz * 40 + 3, STEEL, STEEL)       # Geruestpfosten links
        m.box(48, 56, 3, 112, sz * 40 - 3, sz * 40 + 3, STEEL, STEEL)         # Geruestpfosten rechts
    for sz in (-1, 1):
        m.box(-56, 56, 108, 116, sz * 40 - 3, sz * 40 + 3, TRUSS, TRUSS, front=TRUSS, back=TRUSS)   # Laengsbalken
    for sx in (-1, 1):
        m.box(sx * 36 - 3, sx * 36 + 3, 90, 98, -43, 43, STEEL, STEEL)                             # Winden-Lagerbalken
    m.cyl_x(98.0, 0.0, 9.0, -30.0, 30.0, 8, CORR, STEEL)                      # Seilwinde
    m.box(-1.5, 1.5, 28, 98, -1.5, 1.5, DARKP, DARKP)                         # Seil
    m.box(-9, 9, 20, 30, -9, 9, STEEL, STEEL)                                 # Foerderkorb
    return m


def main():
    os.makedirs(os.path.join(OUT, "structs"), exist_ok=True)
    m = build_shaft()
    open(os.path.join(OUT, "structs", "scavshaft.pie"), "w", encoding="latin-1", newline="").write(m.text("0 0 116"))
    sp = os.path.join(OUT, "stats", "structure.json"); S = json.load(open(sp, encoding="utf-8"))
    e = copy.deepcopy(S["A0PowerPole"])
    e.update({"id": "A0LayerShaft", "name": "Shaft", "type": "GENERIC", "structureModel": ["scavshaft.pie"], "width": 1, "breadth": 1,
              "hitpoints": 500, "buildPoints": 30, "buildPower": 120, "armour": 10, "thermal": 6, "height": 4})
    e.pop("sensorID", None)
    S["A0LayerShaft"] = e
    json.dump(S, open(sp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    rp = os.path.join(OUT, "stats", "research.json"); R = json.load(open(rp, encoding="utf-8"))
    rs = R["R-Sys-Spade1Mk1"].setdefault("resultStructures", [])
    if "A0LayerShaft" not in rs: rs.append("A0LayerShaft")
    json.dump(R, open(rp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Schacht: A0LayerShaft angelegt (%d Punkte, %d Flaechen)" % (len(m.pts), len(m.pol)))


main()
