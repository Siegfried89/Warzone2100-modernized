import sys, os, math
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, '/home/claude/w')
import pieview
M = sys.argv[1]; names = sys.argv[3:]; out = sys.argv[2]
tiles = []
for n in names:
    pts, polys, tex, page = pieview.load_pie(M, os.path.join(M, 'features', n))
    for roll, label in ((0, 'aufrecht'), (90, 'Seite')):
        r = math.radians(roll); Rz = np.array([[math.cos(r), -math.sin(r), 0], [math.sin(r), math.cos(r), 0], [0, 0, 1]])
        P = (Rz @ pts.T).T
        im = pieview.render(P, polys, tex, yaw=-35, pitch=-22, size=300)
        d = ImageDraw.Draw(im); d.text((6, 4), n.replace('.pie', '') + ' ' + label, fill=(255, 255, 255))
        tiles.append(im)
cols = 4; rows = (len(tiles) + cols - 1) // cols
w, h = tiles[0].size
sheet = Image.new('RGB', (w * cols, h * rows), (40, 44, 70))
for i, t in enumerate(tiles): sheet.paste(t, ((i % cols) * w, (i // cols) * h))
sheet.save(out)
print(out, sheet.size)
