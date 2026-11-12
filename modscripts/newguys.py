"""Neues Modell 'Dreadhawk': Punk-Koerper, beidseitig schwarze Schulterplatten mit weissen Spikes, Dread-Mohawk. newguys.py OUT"""
import json, os, sys, math
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import plates
OUT = sys.argv[1]; B = os.path.join(OUT, "components", "bodies"); TP = os.path.join(OUT, "texpages")
def rd(p): return open(p, encoding="latin-1").read()
def wr(p, t): open(p, "w", encoding="latin-1", newline="").write(t)
im = Image.open(os.path.join(TP, "page-32-punk.png")).convert("RGBA")
tx = Image.new("RGBA", (256, 512), (0, 0, 0, 255)); tx.paste(im, (0, 0))
for i, c in enumerate(((0, 0, 0), (247, 247, 253), (72, 50, 30), (30, 22, 16))):
    for y in range(256, 264):
        for x in range(i * 8, i * 8 + 8): tx.putpixel((x, y), c + (255,))
tx.save(os.path.join(TP, "page-32-punk-dread.png"))
def sw(i): return ((i * 8 + 4) / 256, 260 / 512)
BLACK, WHITE, BROWN, DARK = sw(0), sw(1), sw(2), sw(3)
def scale_v(txt):
    out = []
    for l in txt.split("\n"):
        t = l.split()
        if len(t) >= 5 and t[0].isdigit() and t[1].isdigit() and int(t[1]) in (3, 4) and len(t) == 2 + 3 * int(t[1]):
            c = int(t[1]); u = t[2 + c:]
            for k in range(c): u[2 * k + 1] = "%g" % (float(u[2 * k + 1]) * 0.5)
            l = "\t" + " ".join(t[:2 + c] + u)
        out.append(l)
    return "\n".join(out)
def strip_mohawk(f):
    L = rd(f).split("\n")
    pi = next(i for i, l in enumerate(L) if l.startswith("POINTS")); n = int(L[pi].split()[1])
    gi = next(i for i, l in enumerate(L) if l.startswith("POLYGONS")); m = int(L[gi].split()[1])
    pts = [tuple(map(float, x.split())) for x in L[pi + 1:pi + 1 + n]]; keep = []
    for l in L[gi + 1:gi + 1 + m]:
        t = l.split(); c = int(t[1]); ids = list(map(int, t[2:2 + c])); uu = sum(float(x) for x in t[2 + c::2][:c]) / c
        if uu > 0.7 and ((min(pts[i][1] for i in ids) > 17.0) or (max(pts[i][1] for i in ids) > 17.0 and min(pts[i][1] for i in ids) > 15.0 and min(pts[i][2] for i in ids) < -1.2)): continue
        keep.append(l)
    L[gi + 1:gi + 1 + m] = keep; L[gi] = "POLYGONS %d" % len(keep); wr(f, "\n".join(L))
def tube(c, path, rad, uv):
    rings = []
    for k, (p, r) in enumerate(zip(path, rad)):
        q = path[min(k + 1, len(path) - 1)]; o = path[max(k - 1, 0)]
        ty, tz = q[1] - o[1], q[2] - o[2]; ln = math.hypot(ty, tz) or 1; ny, nz = -tz / ln, ty / ln
        b = c.n + len(c.newp)
        c.newp.extend([(p[0] + r, p[1], p[2]), (p[0], p[1] + ny * r, p[2] + nz * r), (p[0] - r, p[1], p[2]), (p[0], p[1] - ny * r, p[2] - nz * r)]); rings.append(b)
    for k in range(len(rings) - 1):
        for j in range(4):
            j2 = (j + 1) % 4; a, b, d, e = rings[k] + j, rings[k] + j2, rings[k + 1] + j, rings[k + 1] + j2
            c.tri(a, b, e, uv); c.tri(a, e, d, uv)
    tip = c.n + len(c.newp); p = path[-1]; c.newp.append((p[0], p[1] - 0.5, p[2] - 0.15))
    for j in range(4): c.tri(rings[-1] + j, rings[-1] + (j + 1) % 4, tip, uv)
def dreads(c):
    for i in range(12):
        r = i // 3; col = (i % 3) - 1
        z = 1.0 - r * 0.7; x = col * 0.6 + (0.15 if r % 2 else -0.15) * (1 if col == 0 else 0)
        y0 = 22.0 - 0.15 * abs(z); up = 1.1 - 0.1 * r
        path = [(x, y0 - 0.2, z), (x, y0 + up, z - 0.25), (x * 1.2, y0 + up * 0.7, z - 1.1), (x * 1.4, y0 - 0.4, max(z - 1.9, -2.3)), (x * 1.5, y0 - 1.8 - 0.3 * r, max(z - 2.2, -2.6)), (x * 1.6, y0 - 3.2 - 0.4 * r, max(z - 2.4, -2.8))]
        tube(c, path, [0.36, 0.33, 0.3, 0.26, 0.21, 0.15], BROWN if (i + r) % 2 == 0 else DARK)
for a, b in (("exbloke3d.pie", "exbloke3dd.pie"), ("runanim3d.pie", "runanim3dd.pie"), ("fireknee3d.pie", "fireknee3dd.pie")):
    t = rd(os.path.join(B, a)).replace("runanim3d.pie", "runanim3dd.pie").replace("fireknee3d.pie", "fireknee3dd.pie").replace("page-32-punk.png", "page-32-punk-dread.png")
    f = os.path.join(B, b); wr(f, scale_v(t)); strip_mohawk(f)
    print(b, plates.apply(f, [(-1, WHITE, WHITE), (1, WHITE, WHITE)], dreads))
bp = os.path.join(OUT, "stats", "body.json"); bs = json.load(open(bp)); tp_ = os.path.join(OUT, "stats", "templates.json"); tp = json.load(open(tp_))
def add(bid, tid, src_b, src_t, model, name):
    v = json.loads(json.dumps(bs[src_b])); v.update({"id": bid, "model": model, "name": name}); bs[bid] = v
    t = json.loads(json.dumps(tp[src_t])); t.update({"id": tid, "name": name, "body": bid, "available": True}); tp[tid] = t
add("B1BaBaVestS01", "BaBaVestS", "B1BaBaVest01", "BaBaVest", "exbloke3ds.pie", "Spikeguard")
add("B1BaBaDread01", "BaBaDread", "B1BaBaPunk01", "BaBaPunk", "exbloke3dd.pie", "Dreadhawk")
json.dump(bs, open(bp, "w"), indent="\t", sort_keys=True); json.dump(tp, open(tp_, "w"), indent="\t", sort_keys=True)
