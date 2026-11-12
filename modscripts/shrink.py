"""Verlustfreie Verkleinerung: PNGs mit hoechstens 256 verschiedenen Farben (inkl. Alpha) werden zu Palettenbildern (die Engine kann das, siehe Basisdaten).
Das Geraete-Paketlimit liegt bei 25 MiB. shrink.py OUT"""
import sys, os
import numpy as np
from PIL import Image
OUT = sys.argv[1]; saved = 0; n = 0
for root, _, fs in os.walk(os.path.join(OUT, "texpages")):
    for f in fs:
        if not f.lower().endswith(".png"): continue
        p = os.path.join(root, f); im = Image.open(p)
        if im.mode != "RGBA": continue
        a = np.array(im); h, w, _ = a.shape
        u, inv = np.unique(a.reshape(-1, 4), axis=0, return_inverse=True)
        if len(u) > 256: continue
        # voll transparente Pixel vereinheitlichen ist nicht noetig (verlustfrei bleiben wir)
        pal = Image.new("P", (w, h)); pal.putdata(inv.reshape(-1).astype(np.uint8).tolist())
        pal.putpalette([int(v) for c in u for v in c[:3]])
        before = os.path.getsize(p)
        pal.save(p + ".tmp", format="PNG", optimize=True, transparency=bytes(int(c[3]) for c in u))
        if os.path.getsize(p + ".tmp") < before: os.replace(p + ".tmp", p); saved += before - os.path.getsize(p); n += 1
        else: os.remove(p + ".tmp")
print("PNG verkleinert: %d Dateien, %.1f MB gespart" % (n, saved / 1e6))
