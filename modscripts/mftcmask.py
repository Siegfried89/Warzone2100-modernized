"""Die neue Engine verlangt zu jeder Modell-Texturseite page-N.png eine Maske page-N_tcmask.png (Spielerfarbe).
Fehlende Masken werden als komplett schwarze (= keine Spielerfarbe) Bilder in gleicher Groesse angelegt. mftcmask.py OUT"""
import sys, os, re
from PIL import Image
OUT = sys.argv[1]
tp = os.path.join(OUT, 'texpages')
have = set(); pages = {}
for f in sorted(os.listdir(tp)):
    m = re.match(r'page-(\d+)(?:[-_.](.*))?\.png$', f)
    if not m: continue
    n = int(m.group(1))
    if f == 'page-%d_tcmask.png' % n: have.add(n); continue
    if re.search(r'_(tcmask|normal|specular)\.png$', f): continue
    pages.setdefault(n, f)
made = 0
for n, f in pages.items():
    if n in have: continue
    w, h = Image.open(os.path.join(tp, f)).size
    Image.new('RGBA', (w, h), (0, 0, 0, 0)).save(os.path.join(tp, 'page-%d_tcmask.png' % n), optimize=True)
    made += 1
print('tcmask-Masken erzeugt:', made, 'vorhanden:', len(have))
