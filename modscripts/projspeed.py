"""Fast sofortige Projektile: alle Direkt-Waffen ausser Raketen/Flammen/Bomben bekommen flightSpeed 15000
(= 1500 Einheiten pro Spiel-Tick, mehr als jede Reichweite -> Einschlag im ersten Tick nach dem Schuss, Ton und Treffer fallen zusammen).
Ausgenommen: ROCKET, SLOW ROCKET, MISSILE (sichtbare Flugbahn), FLAME (der Flammenstrahl IST das fliegende Projektil),
BOMB (wird abgeworfen), LAS_SAT, alle INDIRECT-Waffen (Moerser/Haubitzen fliegen im Bogen).
Die Engine sucht dafuer entlang der ganzen Flugstrecke nach Zielen (projectile.cpp, Mod-Aenderung).
projspeed.py OUT"""
import sys, os, json

OUT = sys.argv[1]
SPEED = int(os.environ.get("PROJSPEED", "5000"))  # 5000 ist mit der alten Engine sicher; 15000 erst mit der neuen Engine (Sweep-Suche)
SKIP_SUB = {"ROCKET", "SLOW ROCKET", "MISSILE", "FLAME", "BOMB", "LAS_SAT"}
SKIP_NAME = {"ZNULLWEAPON", "bbaawep", "Superweapon"}


def main():
    wp = os.path.join(OUT, "stats", "weapons.json"); W = json.load(open(wp, encoding="utf-8"))
    n = 0; by = {}
    for k, v in W.items():
        if not isinstance(v, dict) or k in SKIP_NAME: continue
        if v.get("weaponSubClass") in SKIP_SUB: continue
        if v.get("movement") not in ("DIRECT", "HOMING-DIRECT"): continue
        v["flightSpeed"] = SPEED
        n += 1; by[v.get("weaponSubClass")] = by.get(v.get("weaponSubClass"), 0) + 1
    json.dump(W, open(wp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Projektile sofort: %d Waffen auf flightSpeed %d  %s" % (n, SPEED, by))


main()
