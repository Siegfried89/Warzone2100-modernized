"""Scav-Schiffscontainer als Mauern (Typ WALL, verbinden sich mit allen anderen Mauern): rot, blau (Farbton wie das blaue Wrackauto), gelb, gruen.
Ein Container = ein Feld lang (124 von 128 Einheiten, 52 breit, 55 hoch, 4 Einheiten Luecke zum naechsten). Modelle je Farbe: gerade, Kreuz, T, L
(Arme = Container-Enden mit Tuerseite). Textur komplett selbst erzeugt (Wellblech-Rippen, Rost, Kratzer, Schablonenschrift): page-433-container.png.
Eintraege: structs/ctr_<farbe>_{h,x,t,l}.pie, stats/structure.json (A0BaBaContainerR/B/Y/G), Forschung R-Sys-Spade1Mk1.
container.py OUT [VORSCHAU.png]"""
import sys, os, json, math, copy
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = sys.argv[1]; PREV = sys.argv[2] if len(sys.argv) > 2 else None
PAGE = "page-433-container.png"; SZ = 1024.0
COLORS = {"r": ("Red", (158, 44, 34)), "b": ("Blue", (56, 72, 128)), "y": ("Yellow", (206, 162, 40)), "g": ("Green", (64, 104, 70))}
ORDER = ["r", "b", "y", "g"]
# Aufteilung je Farbblock (Hoehe 256): Seite (0,0,512,192), Tuerseite (512,0,704,192), Dach (704,0,1024,134), Unterseite (0,192,64,256)
SIDE = (0, 0, 512, 192); END = (512, 0, 704, 192); ROOF = (704, 0, 1024, 134); UNDER = (0, 192, 64, 256)
HALF = 62.0; WID = 26.0; HGT = 55.0


# ------------------------------------------------------------------ Textur
def noise(w, h, sigma, rng):
    n = Image.fromarray((rng.rand(h, w) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(sigma))
    a = np.asarray(n, float); a = (a - a.min()) / max(1e-6, a.max() - a.min()); return a


def stencil(img, xy, text, scale, alpha, rng, color=(235, 232, 220)):
    f = ImageFont.load_default()
    t = Image.new("L", (len(text) * 7 + 4, 12), 0); ImageDraw.Draw(t).text((2, 0), text, fill=255, font=f)
    t = t.resize((t.width * scale, t.height * scale), Image.NEAREST); a = np.asarray(t, float) / 255.0
    x, y = xy; H, W = img.shape[:2]
    h = min(a.shape[0], H - y); w = min(a.shape[1], W - x)
    if h <= 0 or w <= 0: return
    wear = 0.55 + 0.45 * noise(w, h, 1.2, rng)
    m = a[:h, :w] * alpha * wear
    for k in range(3): img[y:y + h, x:x + w, k] = img[y:y + h, x:x + w, k] * (1 - m) + color[k] * m


def finish(img, rng, rust_bias=0.5):
    """Rost, Schmutzstreifen, Kratzer, Dellen auf ein float-Bild (h, w, 3, 0..255)"""
    h, w = img.shape[:2]
    low = noise(w, h, 6.0, rng); mid = noise(w, h, 2.0, rng)
    img *= (0.82 + 0.3 * low)[..., None]
    streak = np.asarray(Image.fromarray((rng.rand(1, w) * 255).astype(np.uint8)).resize((w, h), Image.NEAREST).filter(ImageFilter.GaussianBlur(1.0)), float) / 255.0
    ramp = np.linspace(0.0, 1.0, h)[:, None]
    dirt = np.clip((streak - 0.55) * 2.2, 0, 1) * (0.25 + 0.6 * ramp)
    img *= (1 - 0.35 * dirt)[..., None]
    edge = np.minimum(np.minimum(np.arange(w)[None, :], w - 1 - np.arange(w)[None, :]), np.minimum(np.arange(h)[:, None], h - 1 - np.arange(h)[:, None])) / 14.0
    bias = rust_bias * (0.4 + 0.9 * np.clip(1 - edge, 0, 1) + 0.7 * ramp)
    rust = np.clip((low * 0.65 + mid * 0.45 + bias * 0.25 - 0.78) * 4.0, 0, 1)
    rc = np.array([118, 58, 26], float)[None, None, :] * (0.7 + 0.5 * mid)[..., None]
    img[:] = img * (1 - rust[..., None] * 0.85) + rc * (rust[..., None] * 0.85)
    pil = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)); d = ImageDraw.Draw(pil, "RGBA")
    for _ in range(max(6, w * h // 4000)):                                      # Kratzer
        x = rng.randint(0, w); y = rng.randint(0, h); L = rng.randint(4, 18); a = rng.uniform(-0.5, 0.5)
        d.line([(x, y), (x + L * math.cos(a), y + L * math.sin(a))], fill=(230, 215, 190, int(rng.randint(40, 110))), width=1)
    img[:] = np.asarray(pil, float)


def make_side(col, rng):
    w, h = SIDE[2] - SIDE[0], SIDE[3] - SIDE[1]
    base = np.array(col, float)
    img = np.zeros((h, w, 3)) + base
    u = np.arange(w)[None, :]
    rib = 0.5 + 0.5 * np.cos(2 * math.pi * u / 9.6)                             # Wellblech-Rippen
    img *= (0.70 + 0.36 * rib)[..., None] * np.ones((h, 1, 1))
    top, bot, post = 14, 14, 11
    img[:top] = base * 0.86; img[h - bot:] = base * 0.80; img[:, :post] = base * 0.90; img[:, w - post:] = base * 0.90
    img[top - 1] = base * 0.45; img[h - bot] = base * 0.45; img[:, post - 1] = base * 0.50; img[:, w - post] = base * 0.50
    finish(img, rng)
    stencil(img, (26, 30), "SCAV %d %03d-%d" % (rng.randint(100, 999), rng.randint(0, 999), rng.randint(0, 9)), 3, 0.78, rng)
    stencil(img, (300, 150), "MAX GR %d KG" % (rng.randint(20, 32) * 1000), 2, 0.55, rng)
    return img


def make_end(col, rng):
    w, h = END[2] - END[0], END[3] - END[1]
    base = np.array(col, float); img = np.zeros((h, w, 3)) + base
    img *= 0.95
    img[:12] = base * 0.80; img[h - 12:] = base * 0.74; img[:, :12] = base * 0.84; img[:, w - 12:] = base * 0.84
    cx = w // 2
    for dx0, dx1 in ((14, cx - 2), (cx + 2, w - 14)):                           # zwei Tuerfluegel mit leichter Rippung
        u = np.arange(dx1 - dx0)[None, :]; rib = 0.88 + 0.12 * np.cos(2 * math.pi * u / 7.0)
        img[14:h - 14, dx0:dx1] = base[None, None, :] * rib[..., None] * 0.97
    img[14:h - 14, cx - 2:cx + 2] = base * 0.30                                  # Mittelfuge
    steel = np.array([128, 130, 134], float)
    for bx in (cx - 62, cx - 28, cx + 22, cx + 56):                              # vier senkrechte Verschlussstangen
        img[18:h - 18, bx:bx + 5] = steel * (0.7 + 0.3 * np.linspace(0, 1, 5))[None, :, None]
        img[18:h - 18, bx + 5:bx + 6] = steel * 0.35
        for by in (22, h - 30):
            img[by:by + 8, bx - 3:bx + 8] = steel * 0.55                         # Halter oben / unten
        img[h // 2 - 8:h // 2 + 10, bx + 5:bx + 12] = steel * 0.45               # Griff
    for hy in (34, h // 2, h - 40):                                              # Scharniere
        for hx in (16, w - 24):
            img[hy:hy + 7, hx:hx + 8] = steel * 0.5
    finish(img, rng, 0.6)
    img[h - 40:h - 22, w - 60:w - 22] = np.array([200, 196, 180]) * 0.7           # Typenschild
    stencil(img, (w - 58, h - 38), "%d" % rng.randint(1000, 9999), 1, 0.9, rng, (30, 30, 30))
    return img


def make_roof(col, rng):
    w, h = ROOF[2] - ROOF[0], ROOF[3] - ROOF[1]
    base = np.array(col, float) * 0.88; img = np.zeros((h, w, 3)) + base
    v = np.arange(h)[:, None]; rib = 0.5 + 0.5 * np.cos(2 * math.pi * v / 7.5)   # Rippen quer zur Laenge
    img *= (0.80 + 0.24 * rib)[..., None]
    img[:6] = base * 0.7; img[h - 6:] = base * 0.7; img[:, :8] = base * 0.7; img[:, w - 8:] = base * 0.7
    finish(img, rng, 0.9)
    return img


def make_page():
    page = np.zeros((1024, 1024, 4), np.uint8); page[..., 3] = 255
    for ci, key in enumerate(ORDER):
        rng = np.random.RandomState(100 + ci); col = COLORS[key][1]; y0 = ci * 256
        for rect, fn in ((SIDE, make_side), (END, make_end), (ROOF, make_roof)):
            im = np.clip(fn(col, rng), 0, 255).astype(np.uint8)
            page[y0 + rect[1]:y0 + rect[1] + im.shape[0], rect[0]:rect[0] + im.shape[1], :3] = im
        page[y0 + UNDER[1]:y0 + UNDER[3], UNDER[0]:UNDER[2], :3] = (np.array(col) * 0.35).astype(np.uint8)
        # Rest des Blocks neutral (verhindert Ausbluten): dunkle Fuellung
    return page


# ------------------------------------------------------------------ Modelle
class Model:
    def __init__(self):
        self.pts = []; self.pol = []
    def quad(self, bl, br, tr, tl, uv, centre):
        """uv = ((u,v) bl, br, tr, tl) in Pixeln der Seite; Windung wird nach aussen gerichtet"""
        ids = []
        for p in (bl, br, tr, tl): self.pts.append(p); ids.append(len(self.pts) - 1)
        for tri in ((0, 1, 2), (0, 2, 3)):
            a, b, c = (ids[i] for i in tri); ua, ub, uc = (uv[i] for i in tri)
            A, B, C = (np.array(self.pts[i]) for i in (a, b, c))
            if np.cross(B - A, C - A) @ ((A + B + C) / 3.0 - centre) < 0: b, c, ub, uc = c, b, uc, ub
            self.pol.append((a, b, c, ua, ub, uc))
    def text(self):
        out = ["PIE 3", "TYPE 200", "TEXTURE 0 %s 0 0" % PAGE, "LEVELS 1", "LEVEL 1", "POINTS %d" % len(self.pts)]
        out += ["\t%.3f %.3f %.3f" % tuple(p) for p in self.pts]
        out.append("POLYGONS %d" % len(self.pol))
        for a, b, c, ua, ub, uc in self.pol:
            out.append("\t200 3 %d %d %d %.6f %.6f %.6f %.6f %.6f %.6f" % (a, b, c, ua[0] / SZ, ua[1] / SZ, ub[0] / SZ, ub[1] / SZ, uc[0] / SZ, uc[1] / SZ))
        out += ["CONNECTORS 1", "\t0 0 %d" % int(HGT), ""]
        return "\n".join(out)


def sub(rect, y0, fu0, fu1, fv0=0.0, fv1=1.0):
    """Teilbereich eines Rechtecks (Anteile 0..1; u nach rechts, v nach unten) in Pixeln, mit halbem Texel Rand"""
    x0, ys, x1, ye = rect; ys += y0; ye += y0; w = x1 - x0; h = ye - ys
    return (x0 + 0.5 + fu0 * (w - 1), ys + 0.5 + fv0 * (h - 1), x0 + 0.5 + fu1 * (w - 1), ys + 0.5 + fv1 * (h - 1))


def frac(s):                      # Lage entlang des Containers (-62..62) -> 0..1
    return (min(max(s, -HALF), HALF) + HALF) / (2 * HALF)


def build(arms, ci):
    y0 = ci * 256
    m = Model()
    def side_quad(axis, fixed, a0, a1, flip):
        """Seitenwand einer Teilstrecke a0..a1 entlang 'axis' bei Position 'fixed' der anderen Achse; flip = Textur laeuft in -axis-Richtung"""
        f0, f1 = frac(a0), frac(a1)
        if flip: f0, f1 = 1 - f0, 1 - f1
        r = sub(SIDE, y0, min(f0, f1), max(f0, f1))
        # links/rechts aus Sicht von aussen: u waechst nach rechts
        ul, ur = r[0], r[2]
        uvs = ((ul, r[3]), (ur, r[3]), (ur, r[1]), (ul, r[1]))
        if axis == "x":
            # linke Kante bei a0 (flip=False) bzw. a1 (flip=True)
            pl, pr = (a0, a1) if not flip else (a1, a0)
            quad = ((pl, 0, fixed), (pr, 0, fixed), (pr, HGT, fixed), (pl, HGT, fixed))
        else:
            pl, pr = (a0, a1) if not flip else (a1, a0)
            quad = ((fixed, 0, pl), (fixed, 0, pr), (fixed, HGT, pr), (fixed, HGT, pl))
        return quad, uvs
    def add_side(axis, fixed, a0, a1, flip, cx, cz):
        quad, uvs = side_quad(axis, fixed, a0, a1, not flip)      # Engine-Raum ist linkshaendig: Blick nach +z hat +x rechts
        m.quad(*quad, uvs, np.array([cx, HGT / 2, cz]))
    def end_face(axis, fixed, lo, hi, cx, cz):
        r = sub(END, y0, 0, 1)
        uvs = ((r[0], r[3]), (r[2], r[3]), (r[2], r[1]), (r[0], r[1]))
        if axis == "x": q = ((fixed, 0, lo), (fixed, 0, hi), (fixed, HGT, hi), (fixed, HGT, lo)) if fixed > 0 else ((fixed, 0, hi), (fixed, 0, lo), (fixed, HGT, lo), (fixed, HGT, hi))
        else: q = ((hi, 0, fixed), (lo, 0, fixed), (lo, HGT, fixed), (hi, HGT, fixed)) if fixed > 0 else ((lo, 0, fixed), (hi, 0, fixed), (hi, HGT, fixed), (lo, HGT, fixed))
        m.quad(*q, uvs, np.array([cx, HGT / 2, cz]))
    def roof(x0, x1, z0, z1, along):
        """Dach: Rippen quer zur Laenge; Textur-u laeuft entlang der Laenge"""
        if along == "x":
            f0, f1 = frac(x0), frac(x1); r = sub(ROOF, y0, f0, f1)
            uvs = ((r[0], r[3]), (r[2], r[3]), (r[2], r[1]), (r[0], r[1]))
            q = ((x0, HGT, z1), (x1, HGT, z1), (x1, HGT, z0), (x0, HGT, z0))
        else:
            f0, f1 = frac(z0), frac(z1); r = sub(ROOF, y0, f0, f1)
            uvs = ((r[0], r[1]), (r[0], r[3]), (r[2], r[3]), (r[2], r[1]))
            q = ((x0, HGT, z1), (x1, HGT, z1), (x1, HGT, z0), (x0, HGT, z0))
        m.quad(*q, uvs, np.array([(x0 + x1) / 2, HGT / 2, (z0 + z1) / 2]))
    W = WID
    # ---- Mittelstueck
    xs = ("x" in "".join(arms)) and True
    # Seitenwaende des Mittelstuecks, wo kein Arm ist (Seiten des Laufs in x-Richtung bzw. z-Richtung)
    has = {a: (a in arms) for a in ("-x", "+x", "+z", "-z")}
    if not has["+z"]: add_side("x", W, -W, W, False, 0, W)
    if not has["-z"]: add_side("x", -W, -W, W, True, 0, -W)
    if not has["+x"]:
        if has["+z"] or has["-z"]: add_side("z", W, -W, W, True, W, 0)        # Flanke eines z-Laufs
        else: end_face("x", W, -W, W, W, 0)
    if not has["-x"]:
        if has["+z"] or has["-z"]: add_side("z", -W, -W, W, False, -W, 0)
        else: end_face("x", -W, -W, W, -W, 0)
    roof(-W, W, -W, W, "x")
    # ---- Arme
    if has["+x"]:
        add_side("x", W, W, HALF, False, 0, W); add_side("x", -W, W, HALF, True, 0, -W); end_face("x", HALF, -W, W, HALF, 0); roof(W, HALF, -W, W, "x")
    if has["-x"]:
        add_side("x", W, -HALF, -W, False, 0, W); add_side("x", -W, -HALF, -W, True, 0, -W); end_face("x", -HALF, -W, W, -HALF, 0); roof(-HALF, -W, -W, W, "x")
    if has["+z"]:
        add_side("z", W, W, HALF, True, W, 0); add_side("z", -W, W, HALF, False, -W, 0); end_face("z", HALF, -W, W, 0, HALF); roof(-W, W, W, HALF, "z")
    if has["-z"]:
        add_side("z", W, -HALF, -W, True, W, 0); add_side("z", -W, -HALF, -W, False, -W, 0); end_face("z", -HALF, -W, W, 0, -HALF); roof(-W, W, -HALF, -W, "z")
    return m


MODELS = {"h": ("-x", "+x"), "x": ("-x", "+x", "+z", "-z"), "t": ("-x", "+x", "+z"), "l": ("-x", "+z")}


def main():
    tp = os.path.join(OUT, "texpages"); os.makedirs(tp, exist_ok=True)
    page = make_page(); Image.fromarray(page).save(os.path.join(tp, PAGE), optimize=True)
    sd = os.path.join(OUT, "structs"); os.makedirs(sd, exist_ok=True)
    for ci, key in enumerate(ORDER):
        for sfx, arms in MODELS.items():
            open(os.path.join(sd, "ctr_%s_%s.pie" % (key, sfx)), "w", encoding="latin-1", newline="").write(build(arms, ci).text())
    sp = os.path.join(OUT, "stats", "structure.json"); st = json.load(open(sp, encoding="utf-8"))
    base = st["A0BaBaHorizontalWall"]
    for key in ORDER:
        e = copy.deepcopy(base)
        e.update({"id": "A0BaBaContainer" + key.upper(), "name": "Container (%s)" % COLORS[key][0], "structureModel": ["ctr_%s_%s.pie" % (key, s) for s in ("h", "x", "t", "l")],
                  "hitpoints": 520, "armour": 14, "thermal": 12, "buildPower": 28, "buildPoints": 24, "strength": "HARD", "height": 2})
        st[e["id"]] = e
    json.dump(st, open(sp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    rp = os.path.join(OUT, "stats", "research.json"); rs = json.load(open(rp, encoding="utf-8"))
    res = rs["R-Sys-Spade1Mk1"]["resultStructures"]
    for key in ORDER:
        if "A0BaBaContainer" + key.upper() not in res: res.append("A0BaBaContainer" + key.upper())
    json.dump(rs, open(rp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Container: 4 Farben x 4 Modelle, Strukturen A0BaBaContainerR/B/Y/G")
    if PREV:
        sys.path.insert(0, os.path.dirname(os.path.abspath(sys.argv[0])))
        import pieview
        ims = []
        for key in ORDER:
            for sfx, yaw in (("h", 35), ("h", 215), ("l", 125)):
                pts, polys, tex, _ = pieview.load_pie(OUT, os.path.join(sd, "ctr_%s_%s.pie" % (key, sfx)))
                ims.append(pieview.render(pts, polys, tex, yaw, -28, 300))
        W = Image.new("RGB", (300 * 6, 300 * 2))
        for k, im in enumerate(ims[:12]): W.paste(im, (300 * (k % 6), 300 * (k // 6)))
        W.save(PREV)


main()
