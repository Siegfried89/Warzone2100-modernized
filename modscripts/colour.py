"""Standardfarbe 1 fuer den Spieler: patcht rules.js (Kampagne) und gameinit.js (Skirmish). colour.py OUT"""
import os, sys
OUT = sys.argv[1]
def patch(src, dst, anchor, add):
    t = open(src, encoding="utf-8").read(); assert anchor in t, anchor
    t = t.replace(anchor, anchor + add, 1); os.makedirs(os.path.dirname(dst), exist_ok=True); open(dst, "w", encoding="utf-8").write(t)
ADD = "\n\ttry { changePlayerColour(selectedPlayer, 1); } catch (e) {}\n"
patch("/tmp/claude-0/cmp/base/script/rules.js", os.path.join(OUT, "script", "rules.js"), "function eventGameInit()\n{", ADD)
patch("/tmp/claude-0/cmp/mp/multiplay/script/rules/events/gameinit.js", os.path.join(OUT, "multiplay", "script", "rules", "events", "gameinit.js"), "function eventGameInit()\n{", ADD)
print("Farbe 1 gepatcht")
