"""Niedriger brauner Kamm-Mohawk (wie bei Soldaten) statt der Stachel-Frisur. Funktioniert fuer alle Posen (Idle/Lauf/Feuer):
die Kamm-Punkte werden mit einer starren Transformation vom Kopf der Grundpose in den Kopf der jeweiligen Pose uebertragen.
combhair.make_comb_text(base_text, text, hair_uv) -> neuer PIE-Text"""
import math
import numpy as np

HAIR_RECT = (205, 68, 232, 96)       # Textur-Bereich der Haar-Flaechen (Mitte x,y in Pixeln der 256er Seite)
HMAX = 2.0                           # Kammhoehe ueber der Kopfhaut (alte Stacheln waren ca. 4,2 hoch)
WB, WT = 0.50, 0.38                  # halbe Breite unten / oben
# Kopfhaut-Linie in der Mitte (z, y) aus den Hautpunkten des Kopfes
SCALP = [(-2.10, 18.38), (-1.83, 19.72), (-1.03, 20.65), (0.53, 21.05), (1.44, 20.93)]


def _level1(text):
    L = text.split("\n")
    ip = next(i for i, l in enumerate(L) if l.startswith("POINTS"))
    n = int(L[ip].split()[1])
    pts = np.array([[float(v) for v in L[ip + 1 + k].split()[:3]] for k in range(n)])
    iq = next(i for i in range(ip + n, len(L)) if L[i].startswith("POLYGONS"))
    m = int(L[iq].split()[1])
    pol = [L[iq + 1 + k].split() for k in range(m)]
    return L, ip, n, pts, iq, m, pol


def _is_hair(p):
    uv = [float(x) for x in p[5:11]]
    cu = sum(uv[0::2]) / 3 * 256
    cv = sum(uv[1::2]) / 3 * 256
    return HAIR_RECT[0] <= cu <= HAIR_RECT[2] and HAIR_RECT[1] <= cv <= HAIR_RECT[3]


def _head_idx(pts, pol):
    """Indizes der Haut-Punkte des Kopfes (nicht von Haar-Flaechen, hoeher als y=17.5)"""
    skin = set()
    for p in pol:
        if not _is_hair(p):
            for v in p[2:5]:
                skin.add(int(v))
    return sorted(i for i in skin if pts[i][1] > 17.5)


def _kabsch(A, B):
    ca, cb = A.mean(0), B.mean(0)
    H = (A - ca).T @ (B - cb)
    U, S, Vt = np.linalg.svd(H)
    d = np.sign(np.linalg.det(Vt.T @ U.T))
    R = Vt.T @ np.diag([1, 1, d]) @ U.T
    return R, cb - R @ ca


def _scalp_y(z):
    zs = [a for a, _ in SCALP]
    ys = [b for _, b in SCALP]
    return float(np.interp(z, zs, ys))


def comb_geometry():
    """Kamm in der Grundpose: Punkte (Nx3) und Dreiecke (Indexlisten, Aussenseite nach aussen)"""
    z0, z1 = 1.25, -1.95
    N = 11
    pts = []
    secs = []
    for i in range(N):
        t = i / (N - 1)
        z = z0 + (z1 - z0) * t
        ys = _scalp_y(z)
        h = HMAX * (0.40 + 0.60 * math.sin(math.pi * t) ** 0.8)
        base = len(pts)
        pts += [(-WB, ys - 0.15, z), (WB, ys - 0.15, z), (-WT, ys + h, z), (WT, ys + h, z)]
        secs.append(base)
    tris = []

    def quad(a, b, c, d):   # a,b,c,d gegen den Uhrzeigersinn von aussen gesehen
        tris.append((a, b, c))
        tris.append((a, c, d))
    for i in range(N - 1):
        a, b = secs[i], secs[i + 1]
        # Punkte je Schnitt: 0 links unten, 1 rechts unten, 2 links oben, 3 rechts oben
        quad(a + 2, a + 0, b + 0, b + 2)        # linke Seite
        quad(a + 1, a + 3, b + 3, b + 1)        # rechte Seite
        quad(a + 2, b + 2, b + 3, a + 3)        # Oberseite
    a = secs[0]
    quad(a + 0, a + 1, a + 3, a + 2)            # Stirnseite
    b = secs[-1]
    quad(b + 1, b + 0, b + 2, b + 3)            # Hinterseite
    return np.array(pts), tris


def make_comb_text(base_text, text, hair_uv):
    """base_text = Grundpose (exbloke3d.pie), text = Pose, in die der Kamm eingebaut wird; hair_uv = (u, v) im braunen Haarbereich"""
    _, _, _, bpts, _, _, bpol = _level1(base_text)
    L, ip, n, pts, iq, m, pol = _level1(text)
    head = _head_idx(bpts, bpol)
    R, t = _kabsch(bpts[head], pts[head])
    cp, tris = comb_geometry()
    cp = (R @ cp.T).T + t                       # in die Pose uebertragen
    # Aussenseite pruefen: Windung so, dass (b-a)x(c-a) vom Kopfmittelpunkt weg zeigt (wie bei den Hautflaechen des Modells)
    ctr = pts[head].mean(0)
    fixed = []
    for a, b, c in tris:
        nrm = np.cross(cp[b] - cp[a], cp[c] - cp[a])
        cen = (cp[a] + cp[b] + cp[c]) / 3 - ctr
        fixed.append((a, b, c) if nrm @ cen >= 0 else (a, c, b))
    keep = [p for p in pol if not _is_hair(p)]
    u, v = hair_uv
    uvs = "%.6f %.6f " % (u, v)
    newp = ["\t" + " ".join(str(x) for x in p) for p in keep]
    base_idx = n
    newp += ["\t200 3 %d %d %d %s" % (a + base_idx, b + base_idx, c + base_idx, (uvs * 3).strip()) for a, b, c in fixed]
    out = L[:ip]
    out.append("POINTS %d" % (n + len(cp)))
    out += L[ip + 1:ip + 1 + n]
    out += ["\t%.4f %.4f %.4f" % tuple(q) for q in cp]
    out += L[ip + 1 + n:iq]
    out.append("POLYGONS %d" % len(newp))
    out += newp
    out += L[iq + 1 + m:]
    return "\n".join(out)
