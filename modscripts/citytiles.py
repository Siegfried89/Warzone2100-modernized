"""citytiles.py OUT - traegt die von citymap.py/roadkit.py errechneten Strassenkacheln (citytiles/) in den Mod ein:
  texpages/tertilesc1hw-128/tile-78..N.png, tileset/arizonadecals.txt, tileset/arizonaground.txt, texpages/tertilesc1hw.radar
(alle Arizona-Karten bekommen die zusaetzlichen Kacheln; ohne Verweis in einer Karte ist nichts sichtbar)"""
import sys, os, json, shutil
OUT = sys.argv[1]
KIT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "citytiles")
BASE = "/tmp/claude-0/cmp/base"
man = json.load(open(os.path.join(KIT, "manifest.json")))
first, count = man["first"], man["count"]
td = os.path.join(OUT, "texpages", "tertilesc1hw-128"); os.makedirs(td, exist_ok=True)
for i in range(count): shutil.copyfile(os.path.join(KIT, "tile-%02d.png" % (first + i)), os.path.join(td, "tile-%02d.png" % (first + i)))
os.makedirs(os.path.join(OUT, "tileset"), exist_ok=True)
# Decals: alle neuen Kacheln sind Decals (durchsichtiger Rand, darunter der Bodenuntergrund)
L = open(os.path.join(BASE, "tileset", "arizonadecals.txt"), encoding="latin-1").read().replace("\r", "").split("\n")
head, n = L[0].split(","); n = int(n)
body = [l for l in L[1:] if l.strip()]
assert len(body) == n, (len(body), n)
body += [str(first + i) for i in range(count)]
open(os.path.join(OUT, "tileset", "arizonadecals.txt"), "w", newline="\r\n").write("\n".join(["%s,%d" % (head, len(body))] + body) + "\n")
# Untergrund (Ecken): Beton
L = open(os.path.join(BASE, "tileset", "arizonaground.txt"), encoding="latin-1").read().replace("\r", "").split("\n")
head, n = L[0].split(","); n = int(n)
body = [l for l in L[1:] if l.strip()]
assert len(body) == n, (len(body), n)
assert n == first, (n, first)
body += ["a_concrete,a_concrete,a_concrete,a_concrete"] * count
open(os.path.join(OUT, "tileset", "arizonaground.txt"), "w", newline="\r\n").write("\n".join(["%s,%d" % (head, len(body))] + body) + "\n")
# Radar-Farben
R = open(os.path.join(BASE, "texpages", "tertilesc1hw.radar"), encoding="latin-1").read().replace("\r", "").split("\n")
R = [r for r in R if r.strip()]
assert len(R) >= first, len(R)
R = R[:first] + man["radar"]
open(os.path.join(OUT, "texpages", "tertilesc1hw.radar"), "w", newline="\r\n").write("\n".join(R) + "\n")
print("Strassenkacheln: %d neue Kacheln ab Nr. %d in den Mod eingetragen" % (count, first))
