"""Modell-Leiter der Scavenger-Infanterie (Engine: modvariant.cpp). scavladder.py OUT
Die Fabrik wuerfelt beim Ausbilden das Modell (Gewichte); seltene Modelle mit Schulterplatten kommen mit Erfahrungsbonus (Startrang = variantRank).
Beim Aufleveln wechselt die Einheit in das Modell, dessen variantRank dem neuen Rang entspricht.
Neue Fabrik-Eintraege 'Scavenger' (MG) und 'Scavenger Bazooka' wuerfeln. Die Einzelmodelle bleiben vorerst im Menue; mit HIDE_SPECIFIC=1 werden sie versteckt
(available=false, die Vorlagen existieren weiter fuer Skripte/KI)."""
import json, os, sys
OUT = sys.argv[1]
MG = "scavMG"; BB = "scavBB"
#            Koerper             Gruppe Gewicht Rang
LADDER = [("B1BaBaPerson01",   MG, 60, 0),   # Scav (Grundmodell, wuerfelt)
          ("B1BaBaPersonR01",  MG, 28, 0),   # Scav mit rotem Kamm
          ("B1BaBaPunk01",     MG, 20, 1),
          ("B1BaBaPunkR01",    MG, 16, 1),
          ("B1BaBaPunkW01",    MG, 16, 1),   # Wildmane
          ("B1BaBaSun01",      MG, 16, 1),   # Sunpunk (zwei Gelbtoene abwechselnd)
          ("B1BaBaVest01",     MG, 12, 2),   # Marauder (Weste)
          ("B1BaBaVestS01",    MG,  6, 3),   # Spikeguard (Schulterplatten)
          ("B1BaBaDreadV01",   MG,  3, 4),   # Dread Raider (Schulterplatten)
          ("B1BaBaDread01",    MG,  2, 5),   # Dreadhawk (Schulterplatten, selten)
          ("B1BaBaSunB01",     BB, 60, 0),   # Sunpunk RPG (Grundmodell Bazooka, wuerfelt)
          ("B1BaBaVestSB01",   BB,  6, 3),   # Spikeguard RPG
          ("B1BaBaDreadB01",   BB,  2, 5)]   # Dreadhawk RPG
ROLL = {"B1BaBaPerson01", "B1BaBaSunB01"}
bp = os.path.join(OUT, "stats", "body.json"); B = json.load(open(bp, encoding="utf-8"))
n = 0
for k, g, w, r in LADDER:
    if k not in B: print("   fehlt:", k); continue
    B[k].update({"variantGroup": g, "variantWeight": w, "variantRank": r})
    if k in ROLL: B[k]["variantRoll"] = True
    n += 1
json.dump(B, open(bp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
tp = os.path.join(OUT, "stats", "templates.json"); T = json.load(open(tp, encoding="utf-8"))
SHOW = {"BaBaPeople": "Scavenger", "BaBaSunB": "Scavenger Bazooka"}
HIDE = ["BaBaPeopleR", "BaBaPunk", "BaBaPunkR", "BaBaPunkW", "BaBaSun", "BaBaVest", "BaBaVestS", "BaBaDread", "BaBaDreadV", "BaBaDreadB", "BaBaVestSB"]
for k, nm in SHOW.items():
    if k in T: T[k]["available"] = True; T[k]["name"] = nm
if os.environ.get("HIDE_SPECIFIC") == "1":   # erst einschalten, wenn die neue Programmdatei (modvariant) installiert ist; sonst gaebe es ohne Wuerfeln nur noch das Grundmodell
    for k in HIDE:
        if k in T: T[k]["available"] = False
json.dump(T, open(tp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
print("Modell-Leiter: %d Koerper, Fabrikmenue: %s" % (n, ", ".join(SHOW.values())))
