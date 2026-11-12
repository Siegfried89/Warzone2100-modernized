"""roadkit.py - Strassen-Kachel-Set fuer Diagonalen und Knicke (Karte 'Stadt').
Die Engine kennt nur waagrechte/senkrechte Asphaltkacheln (tile-59). Hier werden neue 128x128-Kacheln (RGBA, mit Transparenz am Rand wie die
Originale) aus der Originalkachel 'in Strassenrichtung gedreht' errechnet:
  * Strassen sind Linienzuege (Kachel-Koordinaten), Knicke werden zu runden Boegen gerundet (fillet),
  * jedes Pixel bekommt seine Lage zur Strassenachse (a = Weg entlang, s = Abstand quer) und holt sich damit das Pixel der Originalkachel
    (gleiche Streifen, gelbe Mittellinie, helle Randlinien, ausgefranste Raender),
  * mehrspurige Strassen = mehrere Streifen nebeneinander (jeder Streifen wie eine Originalkachel, abwechselnd gespiegelt),
  * Kachelinhalt wird ueberabtastet (2x2) und dedupliziert; gleiche Kacheln bekommen die gleiche Nummer (ab 78).
Ausgabe: <KIT>/tile-NN.png und <KIT>/manifest.json; citytiles.py traegt sie samt Textlisten in den Mod ein.
"""
import os, math, json, hashlib
import numpy as np
from PIL import Image

SRC = "/tmp/claude-0/cmp/base/texpages/tertilesc1hw-128/"
KIT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "citytiles")
FIRST = 78
SS = 2
N = 128 * SS
T59 = np.asarray(Image.open(SRC + "tile-59.png").convert("RGBA")).astype(np.float32)
T57 = np.asarray(Image.open(SRC + "tile-57.png").convert("RGBA")).astype(np.float32)
SQ2 = math.sqrt(2.0)


def fillet(pts, R, step=0.35):
    """Linienzug mit gerundeten Knicken (Kreisboegen mit Radius R)"""
    out = [tuple(map(float, pts[0]))]
    for i in range(1, len(pts) - 1):
        p0 = np.array(pts[i - 1], float); v = np.array(pts[i], float); p1 = np.array(pts[i + 1], float)
        u1 = v - p0; u1 /= np.linalg.norm(u1); u2 = p1 - v; u2 /= np.linalg.norm(u2)
        h1 = math.atan2(u1[1], u1[0]); h2 = math.atan2(u2[1], u2[0])
        phi = (h2 - h1 + math.pi) % (2 * math.pi) - math.pi
        if abs(phi) < 1e-6:
            out.append(tuple(v)); continue
        t = R * math.tan(abs(phi) / 2)
        T1 = v - u1 * t
        s = 1 if phi > 0 else -1
        C = T1 + R * s * np.array([-math.sin(h1), math.cos(h1)])
        m = max(3, int(abs(phi) * R / step))
        for k in range(m + 1):
            h = h1 + phi * k / m
            out.append(tuple(C - R * s * np.array([-math.sin(h), math.cos(h)])))
    out.append(tuple(map(float, pts[-1])))
    return out


class Road:
    def __init__(self, pts, nstrip=1, R=4.0):
        self.pts = fillet(pts, R) if R else [tuple(map(float, p)) for p in pts]
        self.k = nstrip
        P = np.array(self.pts)
        d = np.diff(P, axis=0); L = np.hypot(d[:, 0], d[:, 1])
        keep = L > 1e-6
        self.p0 = P[:-1][keep]; self.d = d[keep]; self.L = L[keep]
        self.u = self.d / self.L[:, None]
        self.cum = np.concatenate([[0], np.cumsum(self.L)[:-1]])
        diag = np.abs(np.abs(self.u[:, 0]) - np.abs(self.u[:, 1])) < 1e-3
        self.scale = np.where(diag, SQ2, 1.0)
        hw = nstrip / 2.0 + 0.05
        self.bb = np.stack([np.minimum(self.p0[:, 0], self.p0[:, 0] + self.d[:, 0]) - hw, np.minimum(self.p0[:, 1], self.p0[:, 1] + self.d[:, 1]) - hw,
                            np.maximum(self.p0[:, 0], self.p0[:, 0] + self.d[:, 0]) + hw, np.maximum(self.p0[:, 1], self.p0[:, 1] + self.d[:, 1]) + hw], 1)

    def layer(self, tx, ty):
        """RGBA (N,N,4) der Strasse in Kachel (tx,ty); Alpha 0 ausserhalb"""
        idx = np.nonzero((self.bb[:, 0] < tx + 1) & (self.bb[:, 2] > tx) & (self.bb[:, 1] < ty + 1) & (self.bb[:, 3] > ty))[0]
        out = np.zeros((N, N, 4), np.float32)
        if len(idx) == 0: return out, False
        xs = tx + (np.arange(N) + 0.5) / N; ys = ty + (np.arange(N) + 0.5) / N
        X, Y = np.meshgrid(xs, ys)
        best = np.full(X.shape, 1e9); a = np.zeros(X.shape); s = np.zeros(X.shape)
        for i in idx:
            ux, uy = self.u[i]
            rx = X - self.p0[i, 0]; ry = Y - self.p0[i, 1]
            proj = np.clip(rx * ux + ry * uy, 0, self.L[i])
            d = np.hypot(rx - proj * ux, ry - proj * uy)
            m = d < best
            if not m.any(): continue
            best = np.where(m, d, best)
            a = np.where(m, (self.cum[i] + proj) / self.scale[i], a)
            s = np.where(m, rx * (-uy) + ry * ux, s)
        k = self.k
        inside = best <= k / 2.0
        if not inside.any(): return out, False
        sp = s + k / 2.0
        si = np.clip(np.floor(sp), 0, k - 1)
        loc = sp - si - 0.5
        loc = np.where(si % 2 == 1, -loc, loc)
        V = np.clip(np.floor((loc + 0.5) * 128), 0, 127).astype(int)
        U = (np.floor(np.round((a % 1.0) * 1024) / 1024 * 128).astype(int)) % 128
        rgba = T59[V, U].copy()
        rgba[..., 3] *= inside
        return rgba, True


def orth_layer(o, tx, ty, cx, cy):
    """Waagrechte/senkrechte/Kreuzungs-Originalkachel (mit der gleichen Spiegelung wie in der Karte) hochgerechnet auf N x N"""
    if o == 1:
        img = T59
    elif o == 2:
        img = T59.transpose(1, 0, 2)
    else:
        img = T57
    return np.repeat(np.repeat(img, SS, 0), SS, 1)


def over(top, bot):
    ta = top[..., 3:4] / 255.0; ba = bot[..., 3:4] / 255.0
    oa = ta + ba * (1 - ta)
    rgb = (top[..., :3] * ta + bot[..., :3] * ba * (1 - ta)) / np.maximum(oa, 1e-6)
    return np.concatenate([rgb, oa * 255.0], -1)


def downsample(img):
    a = img[..., 3:4] / 255.0
    pm = img[..., :3] * a
    pm = pm.reshape(128, SS, 128, SS, 3).mean((1, 3)); al = a.reshape(128, SS, 128, SS, 1).mean((1, 3))
    rgb = pm / np.maximum(al, 1e-6)
    return np.clip(np.concatenate([rgb, al * 255.0], -1) + 0.5, 0, 255).astype(np.uint8)


def build(roads, O, cx, cy, walk_extra=1.1, bounds=(0, 0, 256, 256)):
    """roads: Liste von Road. O: Feld der Orthogonal-Strassen (0 keine, 1 waagrecht, 2 senkrecht, 3 Kreuzung).
    Rueckgabe: tiles {(tx,ty): Nr}, walk {(tx,ty)}, images[list], radar[list]"""
    os.makedirs(KIT, exist_ok=True)
    for f in os.listdir(KIT):
        if f.startswith("tile-") or f == "manifest.json": os.remove(os.path.join(KIT, f))
    cand = set()
    walk = set()
    for r in roads:
        for i in range(len(r.p0)):
            steps = max(1, int(r.L[i] / 0.5))
            for t in np.linspace(0, 1, steps + 1):
                px, py = r.p0[i] + r.d[i] * t
                R_ = int(math.ceil(r.k / 2.0 + walk_extra)) + 1
                for ddx in range(-R_, R_ + 1):
                    for ddy in range(-R_, R_ + 1):
                        tx, ty = int(math.floor(px)) + ddx, int(math.floor(py)) + ddy
                        if bounds[0] <= tx < bounds[2] and bounds[1] <= ty < bounds[3]:
                            cand.add((tx, ty))
    tiles = {}; images = []; keys = {}; radar = []
    special = set()
    for (tx, ty) in sorted(cand):
        top = None
        for r in roads:
            lay, ok = r.layer(tx, ty)
            if ok and lay[..., 3].max() > 0:
                top = lay if top is None else over(lay, top)
        if top is None or top[..., 3].max() <= 0: continue
        o = int(O[ty, tx])
        if o > 0: top = over(top, orth_layer(o, tx, ty, cx, cy))
        img = downsample(top)
        if img[..., 3].max() < 8: continue
        key = hashlib.md5(img.tobytes()).hexdigest()
        if key not in keys:
            keys[key] = FIRST + len(images); images.append(img)
            a = img[..., 3:4] / 255.0
            m = (img[..., :3] * a + 120 * (1 - a)).reshape(-1, 3).mean(0)
            radar.append("%02x%02x%02x" % tuple(int(c) for c in m))
        tiles[(tx, ty)] = keys[key]; special.add((tx, ty))
    # Randstreifen (Gehweg): Kacheln dicht an einer Strasse, die keine Spezialkachel sind
    for r in roads:
        for (tx, ty) in cand:
            if (tx, ty) in special: continue
            c = np.array([tx + 0.5, ty + 0.5])
            rel = c[None, :] - r.p0
            proj = np.clip((rel * r.u).sum(1), 0, r.L)
            near = r.p0 + r.u * proj[:, None]
            dmin = np.hypot(*(c[None, :] - near).T).min()
            if dmin <= r.k / 2.0 + walk_extra: walk.add((tx, ty))
    assert FIRST + len(images) <= 255, "zu viele Strassenkacheln: %d" % len(images)
    for i, img in enumerate(images):
        Image.fromarray(img, "RGBA").save(os.path.join(KIT, "tile-%02d.png" % (FIRST + i)))
    json.dump({"first": FIRST, "count": len(images), "radar": radar}, open(os.path.join(KIT, "manifest.json"), "w"))
    return tiles, walk, images, radar
