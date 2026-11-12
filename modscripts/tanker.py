"""Original-Tanker (features/mitanker.pie) geradegebogen und mit den Original-Texturen weiterverwendet.
Der Original-Mesh besteht aus einem geraden, nur schief gedrehten 5-Eck-Tank (3 Ringe), einem Fuehrerhaus (B2) und zwei Achsbloecken
(A, B1), die verstreut herumliegen. Hier werden alle Teile wieder in eine Achse gesetzt; die Zuordnung Eckpunkt -> Textur bleibt erhalten.
  tanker.py OUT geo     -> features/mitanker.pie (Wrack, gerade, auf der Seite liegend), feat/features/mitanker_straight.pie (fuer landfill.py)
  tanker.py OUT stats   -> Rumpf Body19TRK-Scav (aufrecht, fahrbar), Aufbau ScavTank (leer) / ScavTankB (mein altes Modell), Vorlagen
  tanker.py OUT preview PNG -> Vergleichsbild"""
import sys, os, math, json, copy
import numpy as np

OUT = sys.argv[1]; MODE = sys.argv[2] if len(sys.argv) > 2 else "geo"
W_ = os.path.dirname(os.path.abspath(sys.argv[0]))
SRC = "/tmp/claude-0/cmp/base/features/mitanker.pie"
PAGE = "page-29-features-arizona.png"
R_TANK = 18.0
YT = R_TANK * math.sin(math.radians(72))        # Tankmitte, damit die unterste Kante auf dem Boden liegt


def load_orig():
    t = open(SRC, encoding="latin-1").read().replace("\r", "").split("\n")
    pts, pol, tw, th = [], [], 256.0, 256.0
    for i, l in enumerate(t):
        w = l.split()
        if not w: continue
        if w[0] == "TEXTURE": tw, th = float(w[3]), float(w[4])
        elif w[0] == "POINTS":
            n = int(w[1]); pts = [[float(v) for v in t[i + 1 + k].split()[:3]] for k in range(n)]
        elif w[0] == "POLYGONS":
            n = int(w[1])
            for k in range(n):
                v = t[i + 1 + k].split(); c = int(v[1]); assert c == 3
                idx = [int(x) for x in v[2:5]]; uv = [float(x) for x in v[5:11]]
                pol.append((idx, [(uv[0] / tw, uv[1] / th), (uv[2] / tw, uv[3] / th), (uv[4] / tw, uv[5] / th)]))
    return np.array(pts), pol


def deskew(X, normals, lo_hi_axes=None):
    """schiefen Quader (8 Ecken) -> achsparalleler Quader. Gibt (Vorzeichen-Tripel je Ecke, Masse) zurueck."""
    ax = []
    for n in normals:
        if not any(abs(np.dot(a, n)) > 0.9 for a in ax): ax.append(n)
    assert len(ax) == 3, len(ax)
    fr = []
    for w in range(3):
        best = max(ax, key=lambda a: abs(a[w])); fr.append(best if best[w] > 0 else -best)
    ex = fr[0] / np.linalg.norm(fr[0])
    ey = fr[1] - ex * np.dot(fr[1], ex); ey /= np.linalg.norm(ey)
    ez = np.cross(ex, ey)
    E = np.stack([ex, ey, ez], 1)
    c = X.mean(0); L = (X - c) @ E
    sg = L > 0
    dims = []
    for w in range(3):
        dims.append(L[sg[:, w], w].mean() - L[~sg[:, w], w].mean())
    assert len({tuple(s) for s in sg.tolist()}) == 8
    return sg, np.array(dims)


def build():
    """gibt (Punkte, Polygone) des geraden Tankers zurueck, liegend (Rad-Seite nach +-y, Unterseite nach +x), Front = -z"""
    P0, pol0 = load_orig()
    pts = []; pol = []
    def newpt(p): pts.append([float(v) for v in p]); return len(pts) - 1
    # ---- Quader: A = Polygone 0-11, B1 = 12-23, B2 (Fuehrerhaus) = 24-35 ----
    boxes = (("A", range(0, 12)), ("B1", range(12, 24)), ("B2", range(24, 36)))
    # Platzierung (Mitte, Masse werden gemessen); Unterseiten (+x) alle bei x = 28.2
    place = {"A": dict(z=75.0, xb=28.2), "B1": dict(z=-18.0, xb=28.2), "B2": dict(z=-57.5, xb=28.2)}
    remap = {}
    for name, rng in boxes:
        ids = sorted({i for k in rng for i in pol0[k][0]})
        X = P0[ids]
        normals = []
        for k in rng:
            idx = pol0[k][0]; Q = P0[idx]; n = np.cross(Q[1] - Q[0], Q[2] - Q[0]); normals.append(n / np.linalg.norm(n))
        sg, d = deskew(X, normals)
        pl = place[name]; cx = pl["xb"] - d[0] / 2; cy = YT; cz = pl["z"]
        if name == "B2":
            cz = -38.0 - d[2] / 2 - 1.0
        mp = {}
        for j, i in enumerate(ids):
            s = np.where(sg[j], 0.5, -0.5)
            mp[i] = newpt((cx + s[0] * d[0], cy + s[1] * d[1], cz + s[2] * d[2]))
        remap[name] = mp
        for k in rng:
            pol.append(([mp[i] for i in pol0[k][0]], pol0[k][1]))
        print("  Quader %s: %.1f x %.1f x %.1f" % (name, d[0], d[1], d[2]))
    # ---- Tank: Ringe (Winkel je Eckpunkt aus der Topologie), Kappen mit Mittelpunkt ----
    ang = {24: 0, 25: 72, 28: 144, 27: 216, 26: 288, 38: 0, 37: 72, 35: 144, 36: 216, 39: 288, 31: 0, 30: 72, 34: 144, 33: 216, 32: 288}
    zring = {24: -35.0, 25: -35.0, 28: -35.0, 27: -35.0, 26: -35.0, 38: 44.0, 37: 44.0, 35: 44.0, 36: 44.0, 39: 44.0,
             31: 96.0, 30: 96.0, 34: 96.0, 33: 96.0, 32: 96.0}
    mp = {}
    for i, a in ang.items():
        r = math.radians(a)
        mp[i] = newpt((R_TANK * math.cos(r), YT + R_TANK * math.sin(r), zring[i]))
    mp[23] = newpt((0.0, YT, -37.0))
    mp[29] = newpt((0.0, YT, 98.0))
    for k in range(36, 66):
        pol.append(([mp[i] for i in pol0[k][0]], pol0[k][1]))
    return np.array(pts), pol


def centred(pts, rot=None):
    P = pts.copy()
    if rot == "upright":     # (x,y,z) -> (y - YT, 28.2 - x, z): Unterseite nach unten, Raeder an die Seiten
        P = np.stack([P[:, 1] - YT, 28.2 - P[:, 0], P[:, 2]], 1)
    mn, mx = P.min(0), P.max(0)
    P[:, 0] -= (mn[0] + mx[0]) / 2; P[:, 2] -= (mn[2] + mx[2]) / 2; P[:, 1] -= mn[1]
    return P


def write_pie(path, pts, pol, conn=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    out = ["PIE 3", "TYPE 200", "TEXTURE 0 %s 0 0" % PAGE, "LEVELS 1", "LEVEL 1", "POINTS %d" % len(pts)]
    out += ["\t%.3f %.3f %.3f" % tuple(p) for p in pts]
    out.append("POLYGONS %d" % len(pol))
    for idx, uv in pol:
        out.append("\t200 3 %d %d %d %.6f %.6f %.6f %.6f %.6f %.6f" % (idx[0], idx[1], idx[2], uv[0][0], uv[0][1], uv[1][0], uv[1][1], uv[2][0], uv[2][1]))
    if conn is not None: out += ["CONNECTORS 1", "\t%g %g %g" % conn]
    out.append("")
    open(path, "w", encoding="latin-1", newline="").write("\n".join(out))


def tcmask():
    from PIL import Image
    tp = os.path.join(OUT, "texpages"); os.makedirs(tp, exist_ok=True)
    f = os.path.join(tp, "page-29_tcmask.png")
    if not os.path.exists(f): Image.new("RGBA", (1024, 1024), (0, 0, 0, 0)).save(f, optimize=True)


def geo():
    pts, pol = build()
    P = centred(pts)
    write_pie(os.path.join(OUT, "features", "mitanker.pie"), P, pol)
    write_pie(os.path.join(W_, "feat", "features", "mitanker_straight.pie"), P, pol)
    print("Tanker-Wrack gerade: %d Punkte, %d Flaechen, Laenge %.0f, Hoehe %.0f" % (len(P), len(pol), np.ptp(P[:, 2]), np.ptp(P[:, 1])))


def stats():
    pts, pol = build()
    P = centred(pts, "upright")
    P[:, 2] -= 10.0
    write_pie(os.path.join(OUT, "components", "bodies", "scavtanker.pie"), P, pol, conn=(0, float(P[:, 1].max()) - 4, 0))
    tcmask()
    # leerer Aufbau (der Tank gehoert zum Rumpf); Dreieck von 0,1 Groesse
    write_pie(os.path.join(OUT, "components", "weapons", "scavtank_leer.pie"), np.array([[0, 0, 0], [0.1, 0, 0], [0, 0.1, 0]]),
              [([0, 1, 2], [(0.0, 0.0), (0.001, 0.0), (0.0, 0.001)])], conn=(0, 0, 0))
    sp = os.path.join(OUT, "stats")
    B = json.load(open(os.path.join(sp, "body.json"), encoding="utf-8"))
    v = copy.deepcopy(B["Body16TRK-Scav"]); v.update({"id": "Body19TRK-Scav", "name": "Oeltanker", "model": "scavtanker.pie", "hitpoints": 260, "weight": 3600})
    B["Body19TRK-Scav"] = v
    json.dump(B, open(os.path.join(sp, "body.json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
    C = json.load(open(os.path.join(sp, "construction.json"), encoding="utf-8"))
    cb = copy.deepcopy(C["ScavTank"]); cb.update({"id": "ScavTankB", "name": "Tankaufbau (Variante B)"})
    C["ScavTankB"] = cb                                       # mein altes Tankmodell (scavtank.pie) bleibt als Variante B
    C["ScavTank"]["sensorModel"] = "scavtank_leer.pie"; C["ScavTank"]["name"] = "Tank (im Rumpf)"
    json.dump(C, open(os.path.join(sp, "construction.json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
    T = json.load(open(os.path.join(sp, "templates.json"), encoding="utf-8"))
    T["ScavTanker"] = {"id": "ScavTanker", "name": "Scav-Oeltransporter", "available": True, "body": "Body19TRK-Scav", "construct": "ScavTank",
                       "propulsion": "wheeled01-Scav", "type": "CONSTRUCT"}
    T["ScavTankerB"] = {"id": "ScavTankerB", "name": "Scav-Oeltransporter B", "available": True, "body": "Body16TRK-Scav", "construct": "ScavTankB",
                        "propulsion": "wheeled01-Scav", "type": "CONSTRUCT"}
    json.dump(T, open(os.path.join(sp, "templates.json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
    R = json.load(open(os.path.join(sp, "research.json"), encoding="utf-8"))
    rc = R["R-Sys-Spade1Mk1"].setdefault("resultComponents", [])
    for k in ("Body19TRK-Scav", "ScavTankB"):
        if k not in rc: rc.append(k)
    json.dump(R, open(os.path.join(sp, "research.json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Oeltanker: Rumpf Body19TRK-Scav (Original-Tanker aufrecht), Variante B = ScavTankB")


def preview(path):
    sys.path.insert(0, W_)
    import pieview
    from PIL import Image
    tk = "/tmp/tk"
    os.makedirs(tk + "/texpages", exist_ok=True)
    import shutil
    shutil.copy("/tmp/claude-0/cmp/base/texpages/" + PAGE, tk + "/texpages/" + PAGE)
    imgs = []
    def one(f, yaw, pitch, mod=tk):
        pts, polys, tex, page = pieview.load_pie(mod, f)
        return pts, polys, tex
    for f, yaw in ((SRC, 200), (os.path.join(OUT, "features", "mitanker.pie"), 200), (os.path.join(OUT, "components", "bodies", "scavtanker.pie"), 215)):
        pts, polys, tex = one(f, yaw, 25)
        imgs.append(pieview.render(pts, polys, tex, yaw, -28, 380))
    # Variante B: Lkw + mein Tankaufbau
    mod2 = "/tmp/pv/all"
    p1, q1, t1, _ = pieview.load_pie(mod2, os.path.join(OUT, "components", "bodies", "scavlkw.pie"))
    p2, q2, t2, _ = pieview.load_pie(mod2, os.path.join(OUT, "components", "weapons", "scavtank.pie"))
    p2 = p2 + np.array([0, 17, 18]); n1 = len(p1)
    q2 = [([i + n1 for i in idx], uv) for idx, uv in q2]
    imgs.append(pieview.render(np.vstack([p1, p2]), q1 + q2, t1, 215, -28, 380))
    from PIL import ImageDraw
    W = Image.new("RGB", (380 * len(imgs), 410), (40, 44, 70))
    d = ImageDraw.Draw(W)
    for k, (im, lab) in enumerate(zip(imgs, ("Original (verbogen)", "Wrack gerade (liegend)", "Tanker aufrecht (fahrbar)", "Variante B (mein Tankaufbau)"))):
        W.paste(im, (380 * k, 30)); d.text((380 * k + 10, 8), lab, fill=(255, 255, 255))
    W.save(path)


if MODE == "geo": geo()
elif MODE == "stats": stats()
elif MODE == "preview": preview(sys.argv[3])
