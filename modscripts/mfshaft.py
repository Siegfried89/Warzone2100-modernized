"""MF-Aufzug/Schacht ("Elevator", 3DStructuresZ, Gruppen 63 und 64, 16-Bit-Texturen) als Struktur A0LayerShaft.
mfshaft.py OUT [VORSCHAUDIR]   - schreibt structs/mfshaft_a.pie (Sockel), mfshaft_b.pie (Turm), mfshaft_ab.pie (beides), texpages/page-843-mfshaft.png"""
import sys, os, json, copy
sys.path.insert(0, '/home/claude/w/mf_src')
import tbd, pieout, numpy as np
OUT = sys.argv[1]
P = tbd.palettes('/home/claude/w/mf_src/GobjectPalettes.tbd')[7]
TP = os.path.join(OUT, 'texpages'); SD = os.path.join(OUT, 'structs')
os.makedirs(TP, exist_ok=True); os.makedirs(SD, exist_ok=True)
fn = '/home/claude/w/mf_src/3DStructuresZ.tbd'
data, T = tbd.load(fn); O = tbd.objects(data, T)
G = []; cur = None
for k, (h, o) in enumerate(T):
    if h == 3642917960: cur = [k]; G.append(cur)
    elif h == 3557481044 and cur is not None: cur.append(k)
KA = [k for k in G[63][1:] if k in O]; KB = [k for k in G[64][1:] if k in O]
keys = sorted(set(KA + KB))
im, pos, H = pieout.atlas(O, P, keys)
page = 'page-843-mfshaft.png'
im.save(os.path.join(TP, page))
SC = 4.6   # MF-Sockel 22,4 Einheiten breit -> ca. 103 Spieleinheiten (ein Feld = 128)
def write(name, ks):
    parts = [(O[k], k, (0, 0, 0), False) for k in ks]
    V = np.array([v[:3] for k in ks for x in O[k]['meshes'] for v in x['V']])
    mn, mx = V.min(0), V.max(0)
    txt, W = pieout.pie_parts(parts, pos, H, page, SC, origin=((mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, mn[2]))
    open(os.path.join(SD, name), 'w').write(txt)
    return max(p[1] for p in W)
ha = write('mfshaft_a.pie', KA); hb = write('mfshaft_b.pie', KB); hab = write('mfshaft_ab.pie', KA + KB)
print('Schacht-Modelle: Sockel %.0f, Turm %.0f, beide %.0f Einheiten hoch' % (ha, hb, hab))
