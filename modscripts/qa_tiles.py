"""qa_tiles.py TEX.npy X0 Y0 X1 Y1 OUT.png : zeichnet einen Kartenausschnitt mit den echten Kachelbildern (Originale + neue Strassenkacheln)"""
import sys, numpy as np
from PIL import Image
tex = np.load(sys.argv[1]); x0, y0, x1, y1 = map(int, sys.argv[2:6]); out = sys.argv[6]
S = 48
SRC = "/tmp/claude-0/cmp/base/texpages/tertilesc1hw-128/"; KIT = "/home/claude/w/citytiles/"
cache = {}
def tile(n):
    if n not in cache:
        p = (KIT if n >= 78 else SRC) + "tile-%02d.png" % n
        cache[n] = Image.open(p).convert("RGBA").resize((S, S), Image.LANCZOS)
    return cache[n]
base = tile(22).convert("RGBA")
im = Image.new("RGBA", ((x1 - x0) * S, (y1 - y0) * S))
for y in range(y0, y1):
    for x in range(x0, x1):
        t = int(tex[y, x]); n = t & 0x1FF; rot = (t >> 12) & 3
        img = tile(n)
        if n < 78:
            if rot: img = img.rotate(-90 * rot)
            if t & 0x4000: img = img.transpose(Image.FLIP_TOP_BOTTOM)
            if t & 0x8000: img = img.transpose(Image.FLIP_LEFT_RIGHT)
        c = Image.new("RGBA", (S, S))
        if n >= 78 or n in (57, 59, 37, 47): c.alpha_composite(base)
        c.alpha_composite(img)
        im.paste(c, ((x - x0) * S, (y - y0) * S))
im.convert("RGB").save(out)
