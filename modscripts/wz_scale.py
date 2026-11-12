#!/usr/bin/env python3
"""
wz_scale.py - skaliert Warzone-2100-Modelle (.pie) und passt Gebaeude-Footprints an.

Aufruf (Beispiel, Windows):
  python wz_scale.py --data "C:\\WZ-Mod work\\base" "C:\\WZ-Mod work\\mp" ^
      --structure-json "C:\\WZ-Mod work\\mp\\stats\\structure.json" ^
      --out "C:\\WZ-Mod work\\out"

--data: Ordner mit structs\\ und components\\ (base zuerst, mp danach; mp ueberschreibt base).
Es werden nur veraenderte Dateien in --out geschrieben (gleiche Unterordner wie im Spiel).
"""
import argparse, glob, json, os, re, sys, csv

UNIT_FACTOR = 0.5        # Fahrzeuge, Cyborgs, Waffen, Antriebe
SMALL_FACTOR = 0.5       # 1x1-Gebaeude (ausser Waende/Tore/Tile-fuellende Tuerme)
BIG_TO_1X1 = 0.45        # 2x2 -> 1x1
FACTORY_FACTOR = (0.36, 0.5, 0.36)   # Fabriken 3x3 -> 1x1 (schmaler, Hoehe 50 %)
THIN_SMALL = 0.84        # normale Tuerme: Breite/Tiefe zusaetzlich x0.84
THIN_WALLTOWER = 0.80    # Wandtuerme (Hardpoints) im Tile: Breite/Tiefe x0.80
WALL_THICK = 0.60        # Betonmauern: Dicke x0.60
WALL_HEIGHT = 0.90       # Betonmauern: Hoehe x0.90
REPAIR_FACTOR = (0.6, 1.0, 0.6)      # Reparaturstation: Hoehe bleibt, Breite/Tiefe x0.6
RESEARCH_FACTOR = (0.55, 0.70, 0.55)  # Forschung: schmaler, etwas hoeher
HQ_FACTOR = (0.37, 0.55, 0.37)        # Hauptquartier
TOWER_HEIGHT = 0.9       # Betontuerme: Hoehe nur leicht unter Original
SCAV_KEEP = {"bbaatow", "ScavRepairCentre"}   # Scavenger-Gebaeude bleiben Originalgroesse
SCAV_SMALL = set()   # alle Scavenger-Gebaeude bleiben Originalgroesse
# Effekt-Modelle (Ordner effects): Namensmuster -> Faktor
EFFECT_RULES = [
    (r"blades|rotor|^fxheli|^fxvtl", 0.5),   # Hubschrauber-Rotoren, VTOL-Triebwerke (wie die Rumpfe)
]
TOWER_MODELS = {"blguard1.pie", "blguard2.pie", "blguard3.pie", "blguardn.pie", "blguardr.pie", "blgrdnex.pie"}
KEEP = 1.0
SCAV_WEAPONS = {"BabaFlame", "BusCannon", "BJeepMG", "bTrikeMG", "BaBaMG", "BabaRocket", "BuggyMG", "BabaPitRocket", "BabaPitRocketAT", "BabaCannon", "BaBaCannon", "bbaawep"}
EXTRA_HALF = {"CoolingTower", "NX-ANTI-SATSite"}   # Deko/inoffizielle Gebaeude, die bisher Originalgroesse hatten -> 50 %
SCAV_INFANTRY_FACTOR = 0.5   # Scav-Infanterie (Stehmodell) = Groesse der Rennanimation (runanim.pie, 0.5)
WALL = "WALL"   # Marker: Mauer-Modell (nur Dicke/Hoehe aendern)

WALL_TYPES = {"WALL", "CORNER WALL", "GATE"}


def fmt(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def fmt_like(tok, v):
    """Ganze Zahlen im Original bleiben ganze Zahlen (Connectors/Animationen verlangen das)."""
    if re.fullmatch(r"-?\d+", tok):
        return str(int(v + 0.5) if v >= 0 else -int(-v + 0.5))
    return fmt(v)


def pie_parse(text):
    """Einfaches PIE-Parsing (1 Level): liefert (kopfzeilen, punkte, polygone, connectors)."""
    lines = text.replace("\r", "").split("\n")
    head, pts, polys, conns, i = [], [], [], [], 0
    while i < len(lines):
        l = lines[i].strip()
        m = re.match(r"(POINTS|POLYGONS|CONNECTORS)\s+(\d+)", l)
        if m:
            n = int(m.group(2)); blk = [x.split() for x in lines[i + 1:i + 1 + n]]
            {"POINTS": pts, "POLYGONS": polys, "CONNECTORS": conns}[m.group(1)].extend(blk); i += n
        elif not pts and not polys and l:
            head.append(l)
        i += 1
    return head, pts, polys, conns


def pie_write(head, pts, polys, conns):
    out = list(head) + [f"POINTS {len(pts)}"] + ["\t" + " ".join(p) for p in pts]
    out += [f"POLYGONS {len(polys)}"] + ["\t" + " ".join(p) for p in polys]
    if conns:
        out += [f"CONNECTORS {len(conns)}"] + ["\t" + " ".join(c) for c in conns]
    return "\n".join(out) + "\n"


def rot_xz(pts, k, ycol=1):
    """Drehung um die Hochachse in 90-Grad-Schritten: (x,z)->(z,-x). pts-Spalten: x y z (Connectors: x z y)."""
    zc = 2 if ycol == 1 else 1
    res = []
    for p in pts:
        x, z = float(p[0]), float(p[zc])
        for _ in range(k % 4):
            x, z = z, -x
        q = list(p); q[0] = fmt_like(p[0], x) if re.fullmatch(r"-?\d+", p[0]) and abs(x - round(x)) < 1e-6 else fmt(x)
        q[zc] = fmt_like(p[zc], z) if re.fullmatch(r"-?\d+", p[zc]) and abs(z - round(z)) < 1e-6 else fmt(z)
        res.append(q)
    return res


def pie_rotate(text, k):
    head, pts, polys, conns = pie_parse(text)
    return pie_write(head, rot_xz(pts, k), polys, rot_xz(conns, k, ycol=2) if conns else conns)


def pie_clamp(pts, axis, keep_sign):
    """Punkte auf einer Seite (axis 0=x,2=z) an 0 klemmen: keep_sign=+1 behaelt >=0, -1 behaelt <=0."""
    res = []
    for p in pts:
        v = float(p[axis]); q = list(p)
        if v * keep_sign < 0:
            q[axis] = "0"
        res.append(q)
    return res


def pie_merge(*models):
    head = models[0][0]; pts, polys = [], []
    for _, p, pl, _c in models:
        off = len(pts); pts += p
        for poly in pl:
            n = int(poly[1]); q = list(poly)
            for j in range(n):
                q[2 + j] = str(int(poly[2 + j]) + off)
            polys.append(q)
    return head, pts, polys, []


def make_scav_wall_models(src_dir, out_dir):
    """Selbstausrichtende Scavenger-Mauer: Reihenfolge wie Hardcrete: [gerade, Kreuz, T, L]."""
    rd = lambda n: open(os.path.join(src_dir, n), encoding="latin-1", newline="").read()
    S = pie_parse(rd("blbrbwlh.pie")); C = pie_parse(rd("blbrbcr1.pie"))
    head = S[0]
    # Arm nach +z: gerade Mauer um 270 Grad drehen (x,z)->(-z,x) = dreimal (z,-x), dann z<0 an 0 klemmen
    Rf = (head, rot_xz(S[1], 3), S[2], [])
    H = (head, pie_clamp(Rf[1], 2, +1), S[2], [])
    cross = pie_merge(S, Rf)
    tee = pie_merge(S, H)
    ell = (C[0], rot_xz(C[1], 1), C[2], [])     # Original-Ecke (Arme -x,-z) -> Arme -x,+z wie Hardcrete-L
    os.makedirs(out_dir, exist_ok=True)
    for name, m in (("blbrbwlx.pie", cross), ("blbrbwlt.pie", tee), ("blbrbwll.pie", ell)):
        with open(os.path.join(out_dir, name), "w", encoding="latin-1", newline="") as fh:
            fh.write(pie_write(*m))


def rot_any(text, deg):
    """Beliebige Drehung um die Hochachse (nur fuer Modelle ohne Connectors)."""
    import math
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    head, pts, polys, conns = pie_parse(text)
    res = []
    for p in pts:
        x, z = float(p[0]), float(p[2])
        res.append([fmt(x * c - z * s), p[1], fmt(x * s + z * c)])
    return pie_write(head, res, polys, conns)


def levels_of(text):
    out = []
    head, pts, polys, conns = pie_parse(text)
    return [[tuple(float(x) for x in p[:3]) for p in pts]] if pts else out


def make_bloodfall():
    """Blutige Sterbeanimation aus den PS1-Sprites (Splatter, Torso- und Beinreste). 4 Stufen wie flamfall.pie."""
    TW = 1024.0
    # Sprite-Rechtecke (x0, y0, x1, y1) in page-7-barbarians-*.png
    R = {"burst": (204, 940, 248, 1020), "torso": (532, 610, 568, 660), "legs": (508, 703, 588, 777), "chunk": (616, 643, 676, 673)}
    pts, polys = [], []
    def quad(name, cx, y0, h, planes=("xy", "zy")):
        x0, y0p, x1, y1 = R[name]; w = h * (x1 - x0) / float(y1 - y0p)
        u0, u1, v0, v1 = x0 / TW, x1 / TW, y0p / TW, y1 / TW
        for pl in planes:
            if pl == "xy": c = [(cx - w / 2, y0, 0), (cx + w / 2, y0, 0), (cx + w / 2, y0 + h, 0), (cx - w / 2, y0 + h, 0)]
            else:          c = [(cx, y0, -w / 2), (cx, y0, w / 2), (cx, y0 + h, w / 2), (cx, y0 + h, -w / 2)]
            base = len(pts); pts.extend(c)
            uv = [(u0, v1), (u1, v1), (u1, v0), (u0, v0)]
            for (i1, i2, i3) in ((0, 1, 2), (0, 2, 3), (0, 2, 1), (0, 3, 2)):
                polys.append("200 3 %d %d %d %s" % (base + i1, base + i2, base + i3, " ".join("%.5f %.5f" % uv[i] for i in (i1, i2, i3))))
    levels = []
    stages = [[("burst", 0, 0, 23)], [("burst", 0, 0, 27)],
              [("torso", -5, 3, 12), ("legs", 6, 0, 15)],
              [("legs", 5, 0, 13), ("chunk", -6, 0, 5)]]
    out = ["PIE 3", "TYPE 200", "INTERPOLATE 0", "TEXTURE 0 page-7-scavspr.png 0 0", "LEVELS 4"]
    for li, stage in enumerate(stages):
        pts.clear(); polys.clear()
        for (nm, cx, y0, h) in stage: quad(nm, cx, y0, h)
        out.append("LEVEL %d" % (li + 1))
        out.append("POINTS %d" % len(pts))
        out += ["\t%.3f %.3f %.3f" % p for p in pts]
        out.append("POLYGONS %d" % len(polys))
        out += ["\t" + p for p in polys]
        out.append("ANIMOBJECT 500 1 4")
        for fr in range(4):
            s = 1.0 if fr == li else -1.0
            out.append("\t%d\t0 0 0 0 0 0 %.1f %.1f %.1f" % (fr, s, s, s))
    return "\n".join(out) + "\n"


def make_sprite_scav(walk_text, f=0.5, mode="bob"):
    """PS1-aehnliche Sprite-Infanterie aus dem Original-Modell walkanim.pie (Original-Ebenen/UVs, ohne Textur-Animation).
    Liefert {'exbloke.pie','runanim.pie','fireknee.pie'}."""
    lines = walk_text.replace("\r", "").split("\n")
    levels, cur, i = [], None, 0
    while i < len(lines):
        p = lines[i].split()
        if p and p[0] == "LEVEL": cur = {"pts": [], "poly": []}; levels.append(cur)
        elif p and p[0] == "POINTS":
            n = int(p[1])
            for k in range(n): cur["pts"].append(tuple(float(v) * f for v in lines[i + 1 + k].split()[:3]))
            i += n
        elif p and p[0] == "POLYGONS":
            n = int(p[1])
            for k in range(n):
                q = lines[i + 1 + k].split()
                fl = int(q[0]); cnt = int(q[1]); idx = q[2:2 + cnt]; rest = q[2 + cnt:]
                if fl & 0x4000 or fl == 4200: rest = rest[4:]
                uv = [float(v) / 256.0 for v in rest[:2 * cnt]]
                if all(int(t) <= 3 for t in idx): continue
                cur["poly"].append("200 %d %s %s" % (cnt, " ".join(idx), " ".join("%.5f" % v for v in uv)))
            i += n
        i += 1
    def lvl(lv, anim=None, nlv=4, li=0, conn=False):
        o = ["LEVEL %d" % (li + 1), "POINTS %d" % len(lv["pts"])]
        o += ["\t%.3f %.3f %.3f" % p for p in lv["pts"]]
        o.append("POLYGONS %d" % len(lv["poly"])); o += ["\t" + q for q in lv["poly"]]
        if conn: o += ["CONNECTORS 1", "\t0 0 16"]
        if anim:
            o.append("ANIMOBJECT %d 0 %d" % (anim, nlv))
            for fr in range(nlv):
                s = 1.0 if fr == li else 0.01
                o.append("\t%d\t0 0 0 0 0 0 %.2f %.2f %.2f" % (fr, s, s, s))
        return o
    hdr = ["PIE 3", "TYPE 200", "INTERPOLATE 0", "TEXTURE 0 page-7-scavspr.png 0 0"]
    ex = hdr + ["EVENT 1 runanim.pie", "EVENT 2 fireknee.pie", "EVENT 3 bloodfall.pie", "LEVELS 1"] + lvl(levels[0], conn=True)
    if mode == "bob":
        run = hdr + ["LEVELS 1"] + lvl(levels[0], conn=True)
        run += ["ANIMOBJECT 260 0 4"]
        for fr, hz in enumerate((0, 1200, 0, 1200)):
            run.append("\t%d\t0 0 %d 0 0 %d 1.00 1.00 1.00" % (fr, hz, (-2000, 0, 2000, 0)[fr]))
    else:
        run = hdr + ["LEVELS 4"]
        for li, lv in enumerate(levels[:4]): run += lvl(lv, anim=120, nlv=4, li=li)
    # Schuss-Pose: zurueckgelehnt, aus der Hueften (Atlas-Sprite px 503..553 x 885..939)
    U0, U1, V0, V1 = 503 / 1024.0, 553 / 1024.0, 885 / 1024.0, 939 / 1024.0
    wd, ht = (U1 - U0) * 1024 * 0.354 * f, (V1 - V0) * 1024 * 0.516 * f
    x0 = -7.0 * f; x1 = x0 + wd
    zc = (x0 + x1) / 2.0; hw = 2.6 * f / 0.5 * 0.5
    fp = [(x0, 0, 0), (x1, 0, 0), (x1, ht, 0), (x0, ht, 0), (zc, 0, -hw), (zc, 0, hw), (zc, ht, hw), (zc, ht, -hw)]
    # Vorderseite: Bild gespiegelt (Waffe nach +x), Rueckseite: Bild direkt
    fl = ["POINTS 8"] + ["\t%.3f %.3f %.3f" % p for p in fp]
    fl += ["POLYGONS 8",
           "\t200 3 0 1 2 %.5f %.5f %.5f %.5f %.5f %.5f" % (U1, V1, U0, V1, U0, V0),
           "\t200 3 0 2 3 %.5f %.5f %.5f %.5f %.5f %.5f" % (U1, V1, U0, V0, U1, V0),
           "\t200 3 2 1 0 %.5f %.5f %.5f %.5f %.5f %.5f" % (U0, V0, U0, V1, U1, V1),
           "\t200 3 3 2 0 %.5f %.5f %.5f %.5f %.5f %.5f" % (U1, V0, U0, V0, U1, V1),
           "\t200 3 4 5 6 %.5f %.5f %.5f %.5f %.5f %.5f" % (U0, V1, U1, V1, U1, V0),
           "\t200 3 4 6 7 %.5f %.5f %.5f %.5f %.5f %.5f" % (U0, V1, U1, V0, U0, V0),
           "\t200 3 6 5 4 %.5f %.5f %.5f %.5f %.5f %.5f" % (U1, V0, U1, V1, U0, V1),
           "\t200 3 7 6 4 %.5f %.5f %.5f %.5f %.5f %.5f" % (U0, V0, U1, V0, U0, V1),
           "CONNECTORS 1", "\t0 0 16"]
    fire = hdr + ["LEVELS 1", "LEVEL 1"] + fl
    j = lambda L: "\n".join(L) + "\n"
    return {"exbloke.pie": j(ex), "runanim.pie": j(run), "fireknee.pie": j(fire)}


def shape_head(text):
    """Kopf oben vorne/hinten abflachen (Ecken nach innen und etwas nach unten ziehen)."""
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.replace("\r\n", "\n").split("\n")
    i = next(k for k, l in enumerate(lines) if l.strip() == "LEVEL 1")
    j = i + 1
    while not lines[j].strip().startswith("POINTS"):
        j += 1
    n = int(lines[j].split()[1])
    for q in range(j + 1, j + 1 + n):
        tok = lines[q].split()
        x, y, z = (float(v) for v in tok[0:3])
        if 40.3 < y < 41.9 and abs(x) > 2.3:          # die vier oberen Kopfecken
            x *= 0.76; y -= 0.55
        elif y > 42.0 and z > 3.0:                   # vorderer Spitz
            y -= 0.35; z -= 0.4
        else:
            continue
        tok[0:3] = [f"{x:.5f}", f"{y:.5f}", f"{z:.5f}"]
        lines[q] = "\t" + " ".join(tok)
    return nl.join(lines)


def add_mohawk(text):
    """Wilder Irokese: 8 nach hinten schwingende Straehnen (3 Lagen je Strahne), UV = roter Verlauf in page-32-soldier.png."""
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.replace("\r\n", "\n").split("\n")
    i = next(k for k, l in enumerate(lines) if l.strip() == "LEVEL 1")
    j = i + 1
    while not lines[j].strip().startswith("POINTS"):
        j += 1
    n = int(lines[j].split()[1])
    k = j + 1 + n
    while not lines[k].strip().startswith("POLYGONS"):
        k += 1
    m = int(lines[k].split()[1])
    new_pts, new_polys = [], []
    UV_BASE, UV_TIP = (0.866, 0.345), (0.866, 0.308)
    def pt(x, y, z):
        new_pts.append((x, y, z)); return n + len(new_pts) - 1
    def tri(a, b, c, uva, uvb, uvc, both=True):
        for (p1, p2, p3, u1, u2, u3) in ([(a, b, c, uva, uvb, uvc), (a, c, b, uva, uvc, uvb)] if both else [(a, b, c, uva, uvb, uvc)]):
            new_polys.append(f"200 3 {p1} {p2} {p3} " + " ".join(f"{u[0]} {u[1]}" for u in (u1, u2, u3)))
    M = (0.866, 0.327)
    # Sockel: durchgehender Streifen vom Stirnansatz ueber die Kuppe bis in den Nacken (3 Lagen)
    # Knoten: (z, y_unten, y_oben); Nacken folgt der Hinterkopf-Kurve (x=0: z -3.65@39.4, -4.2@36.75, -2.8@32.4)
    path = [(2.5, 40.6, 42.7), (0.6, 40.6, 42.7), (-0.9, 40.6, 42.7), (-2.3, 40.6, 42.7),
            (-3.55, 38.9, 41.2), (-4.15, 36.6, 38.9), (-3.9, 34.2, 36.2), (-3.2, 32.6, 34.4)]
    for xp in (-0.8, 0.0, 0.8):
        col = [(pt(xp, yb, z), pt(xp, yt, z)) for (z, yb, yt) in path]
        for q in range(len(path) - 1):
            b1, t1 = col[q]; b2, t2 = col[q + 1]
            tri(b1, t1, t2, UV_BASE, UV_BASE, UV_BASE); tri(b1, t2, b2, UV_BASE, UV_BASE, UV_BASE)
    def spike(base, tang, tip, dx, hw):
        """base=(z,y) Mitte, tang=(z,y) Einheitsrichtung der Basis, tip=(z,y)."""
        for xp in (-0.8, 0.0, 0.8):
            bz, by = base; tz_, ty_ = tang
            bl = pt(xp, by - ty_ * hw, bz - tz_ * hw); br = pt(xp, by + ty_ * hw, bz + tz_ * hw)
            mz, my = (bz + tip[0]) / 2, (by + tip[1]) / 2
            ml = pt(xp + dx * 0.4, my - ty_ * hw * 0.55, mz - tz_ * hw * 0.55)
            mr = pt(xp + dx * 0.4, my + ty_ * hw * 0.55, mz + tz_ * hw * 0.55)
            tl = pt(xp + dx, tip[1] - ty_ * 0.12, tip[0] - tz_ * 0.12); tr = pt(xp + dx, tip[1] + ty_ * 0.18, tip[0] + tz_ * 0.18)
            tri(bl, ml, br, UV_BASE, M, UV_BASE); tri(br, ml, mr, UV_BASE, M, M)
            tri(ml, tl, mr, M, UV_TIP, M); tri(mr, tl, tr, M, UV_TIP, UV_TIP)
    # Kuppe (Basis waagerecht): (z_basis, z_spitze, y_spitze, x_versatz)
    for (zb, tz, ty, dx) in [(2.0, 2.8, 45.0, 0.6), (1.1, 1.3, 48.0, -0.6), (0.2, -0.6, 50.3, 0.6),
                             (-0.7, -2.0, 50.4, -0.6), (-1.5, -3.4, 48.0, 0.5), (-2.2, -4.4, 45.4, -0.4)]:
        spike((zb, 42.4), (1.0, 0.0), (tz, ty), dx, 0.85)
    # Hinterkopf / Nacken: Straehnen laufen nach hinten, Basis entlang der Kurve (senkrecht)
    for (base, tang, tip, dx) in [((-3.6, 41.0), (0.6, 0.8), (-5.9, 42.0), 0.5),
                                  ((-4.4, 38.9), (0.1, 1.0), (-6.2, 38.0), -0.5),
                                  ((-4.6, 36.6), (0.0, 1.0), (-6.1, 34.9), 0.5),
                                  ((-4.4, 34.4), (-0.1, 1.0), (-5.7, 32.3), -0.4),
                                  ((-3.8, 32.7), (-0.2, 1.0), (-4.7, 30.6), 0.3)]:
        spike(base, tang, tip, dx, 0.95)
    pts_txt = [f"\t{x:.4f} {y:.4f} {z:.4f}" for x, y, z in new_pts]
    lines[k + 1 + m:k + 1 + m] = ["\t" + p for p in new_polys]
    lines[k] = f"POLYGONS {m + len(new_polys)}"
    lines[j + 1 + n:j + 1 + n] = pts_txt
    lines[j] = f"POINTS {n + len(new_pts)}"
    return nl.join(lines)


def scale_wall(text):
    """Mauer duenner: die Achse quer zur Mauer (kleinerer Betrag) wird WALL_THICK, Hoehe WALL_HEIGHT.
    Die Laenge bis zur Tile-Kante bleibt, damit Mauern weiter aneinander anschliessen."""
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.replace("\r\n", "\n").split("\n")
    out, i = [], 0
    while i < len(lines):
        out.append(lines[i])
        m = re.match(r"^POINTS\s+(\d+)", lines[i].strip())
        if m:
            n = int(m.group(1))
            for j in range(i + 1, i + 1 + n):
                tok = lines[j].split()
                x, y, z = (float(v) for v in tok[0:3])
                if abs(x) > abs(z):
                    z *= WALL_THICK
                elif abs(z) > abs(x):
                    x *= WALL_THICK
                else:
                    x *= WALL_THICK; z *= WALL_THICK
                tok[0:3] = [fmt(x), fmt(y * WALL_HEIGHT), fmt(z)]
                out.append("\t" + " ".join(tok))
            i += n
        i += 1
    return nl.join(out)


def patch_python_connectors(text):
    """Python-Rumpf: zwei Waffenanschluesse (vorn und hinten, wie beim Dragon)."""
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.replace("\r\n", "\n").split("\n")
    for i, l in enumerate(lines):
        if l.strip().startswith("CONNECTORS"):
            lines[i + 1] = "\t0 26 31"
            lines[i + 2] = "\t0 -22 31"
            break
    return nl.join(lines)


def scale_pie(text, f):
    """Skaliert POINTS, CONNECTORS und die Positionsspalten von ANIMOBJECT.
    f ist eine Zahl (gleichmaessig) oder ein Tupel (fx, fy, fz); y ist die Hoehe."""
    fx, fy, fz = (f, f, f) if not isinstance(f, tuple) else f
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.replace("\r\n", "\n").split("\n")
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        out.append(line)
        st = line.strip()
        m = re.match(r"^(POINTS|CONNECTORS)\s+(\d+)", st)
        a = re.match(r"^ANIMOBJECT\s+\S+\s+\S+\s+(\d+)", st)
        if m or a:
            n = int((m or a).group(2 if m else 1))
            for j in range(i + 1, i + 1 + n):
                tok = lines[j].split()
                if m and len(tok) >= 3:
                    if m.group(1) == "POINTS":
                        tok[0:3] = [fmt(float(x) * k) for x, k in zip(tok[0:3], (fx, fy, fz))]
                    else:   # CONNECTORS stehen in der Datei als x z y (Hoehe zuletzt!)
                        tok[0:3] = [fmt_like(x, float(x) * k) for x, k in zip(tok[0:3], (fx, fz, fy))]
                    out.append("\t" + " ".join(tok))
                elif a and len(tok) >= 4:
                    tok[1:4] = [fmt_like(x, float(x) * k) for x, k in zip(tok[1:4], (fx, fy, fz))]
                    out.append("          " + " ".join(tok))
                else:
                    out.append(lines[j])
            i += n
        i += 1
    return nl.join(out)


def bbox_xz(text):
    lines = text.replace("\r", "").split("\n")
    xs, zs = [], []
    for i, l in enumerate(lines):
        m = re.match(r"^POINTS\s+(\d+)", l.strip())
        if m:
            for j in range(i + 1, i + 1 + int(m.group(1))):
                t = lines[j].split()
                xs.append(float(t[0])); zs.append(float(t[2]))
            break  # nur LEVEL 1
    return (max(xs) - min(xs), max(zs) - min(zs)) if xs else (0, 0)


def height_y(text):
    lines = text.replace("\r", "").split("\n")
    for i, l in enumerate(lines):
        m = re.match(r"^POINTS\s+(\d+)", l.strip())
        if m:
            ys = [float(lines[j].split()[1]) for j in range(i + 1, i + 1 + int(m.group(1)))]
            return max(ys) - min(ys)
    return 0


def collect(dirs, sub):
    files = {}
    for d in dirs:
        for f in glob.glob(os.path.join(d, sub, "**", "*.pie"), recursive=True):
            files[os.path.relpath(f, d).replace("\\", "/")] = f
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", nargs="+", required=True)
    ap.add_argument("--structure-json", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--effects", nargs="*", default=[], help="Ordner, die einen Unterordner effects\\ enthalten")
    ap.add_argument("--templates-json"); ap.add_argument("--features-json"); ap.add_argument("--replace-comps", help="Ordner mit fertigen .pie fuer components/bodies (unskaliert)"); ap.add_argument("--features", help="Ordner mit features\\*.pie"); ap.add_argument("--body-json"); ap.add_argument("--weapons-json"); ap.add_argument("--research-json"); ap.add_argument("--propulsion-json"); ap.add_argument("--sprite-scav", action="store_true"); ap.add_argument("--punk", action="store_true"); ap.add_argument("--sprite-run", default="bob"); ap.add_argument("--sprite-size", type=float, default=0.62); ap.add_argument("--sensor-json"); ap.add_argument("--range-factor", type=float, default=1.0); ap.add_argument("--speed-factor", type=float, default=1.0)
    ap.add_argument("--replace-structs", help="Ordner mit fertigen .pie-Dateien, die Gebaeudemodelle ersetzen (unskaliert)")
    a = ap.parse_args()

    stats = json.load(open(a.structure_json, encoding="utf-8"))
    structs = collect(a.data, "structs")
    comps = collect(a.data, "components")
    by_name = {os.path.basename(k).lower(): (k, v) for k, v in structs.items()}

    # ---- Scavenger-Fahrzeuge behalten Originalgroesse: Modelle, die nur Scavs benutzen
    scav_only = set()
    body_stats = json.load(open(a.body_json, encoding="utf-8")) if a.body_json else {}
    if a.body_json and a.weapons_json:
        wstats = json.load(open(a.weapons_json, encoding="utf-8"))
        def body_models(v):
            out = {v.get("model", "").lower()}
            for d in v.get("propulsionExtraModels", {}).values():
                out |= {x.lower() for x in d.values()}
            return out
        def weap_models(v):
            return {str(v.get(k, "")).lower() for k in ("model", "mountModel", "muzzleGfx", "flightGfx", "hitGfx")}
        scav_m, other_m = set(), set()
        for v in body_stats.values():
            (scav_m if v.get("class") == "Babas" else other_m).update(body_models(v))
        for k, v in wstats.items():
            isscav = "baba" in k.lower() or "scav" in k.lower() or k in SCAV_WEAPONS
            (scav_m if isscav and "VTOL" not in k else other_m).update(weap_models(v))
        scav_only = {m for m in scav_m - other_m if m.endswith(".pie")}
        # Scav-Waffen, deren Modell auch von normalen Waffen benutzt wird: eigene unskalierte Kopie
        scav_wcopy, wout = {}, json.loads(json.dumps(wstats))
        for k in SCAV_WEAPONS:
            if k not in wstats:
                continue
            for fld in ("model", "mountModel"):
                m = str(wstats[k].get(fld, ""))
                if m.lower().endswith(".pie") and m.lower() in other_m:
                    nn = m[:-4] + "_scav.pie"
                    scav_wcopy[m.lower()] = nn
                    wout[k][fld] = nn
        print("Scav-Waffenmodelle mit eigener Kopie:", sorted(set(scav_wcopy.values())))
        print(f"Scavenger-eigene Modelle (bleiben Originalgroesse): {len(scav_only)}")

    # ---- Gebaeude: Faktor je Modell + neuer Footprint
    model_factor, conflicts, new_stats = {}, [], json.loads(json.dumps(stats))
    report = []
    for sid, s in stats.items():
        if "type" not in s: continue
        w, b = s.get("width", 1), s.get("breadth", 1)
        models = list(s.get("structureModel", [])) + ([s["baseModel"]] if "baseModel" in s else [])
        mfirst = (s.get("structureModel") or [""])[0].lower()
        hit_s = by_name.get(mfirst)
        barb = bool(hit_s and "barbarian" in open(hit_s[1], encoding="latin-1").read(600).lower())
        scav = (sid.startswith("A0BaBa") or sid in SCAV_KEEP or barb) and sid not in SCAV_SMALL
        if scav or sid == "WreckedTransporter":
            f, nw, nb = KEEP, w, b
        elif sid in EXTRA_HALF:
            f, nw, nb = 0.5, w, b
        elif s["type"] == "GATE" or "TankTrap" in sid:
            f, nw, nb = KEEP, w, b
        elif s["type"] in WALL_TYPES:
            f, nw, nb = WALL, w, b
        elif scav or sid == "WreckedTransporter":
            f, nw, nb = KEEP, w, b
        elif s["type"] in ("FACTORY", "FACTORY MODULE", "VTOL FACTORY") and w == 3 and b == 3:
            f, nw, nb = FACTORY_FACTOR, 1, 1
        elif s["type"] == "REPAIR FACILITY":
            f, nw, nb = REPAIR_FACTOR, w, b
        elif s["type"] == "HQ":
            f, nw, nb = HQ_FACTOR, 1, 1
        elif s["type"].startswith("RESEARCH"):
            f, nw, nb = RESEARCH_FACTOR, 1, 1
        elif w >= 2 or b >= 2:
            f, nw, nb = (BIG_TO_1X1 if (w >= 2 and b >= 2) else SMALL_FACTOR), 1, 1
        else:  # 1x1
            f, nw, nb = SMALL_FACTOR, 1, 1
            for m in models:
                hit = by_name.get(m.lower())
                if hit:
                    bw, bd = bbox_xz(open(hit[1], encoding="latin-1").read())
                    if bw >= 110 and bd >= 110:   # fuellt das Tile -> Wandturm
                        f = KEEP
        # Betontuerme: schlanker (Breite/Tiefe), Hoehe nur leicht unter Original
        hit0 = by_name.get(mfirst)
        tall = (s["type"] == "DEFENSE" and hit0 is not None and not scav
                and sid not in ("CoolingTower", "LookOutTower", "UplinkCentre") and not mfirst.startswith(("blbr", "bbaa"))
                and height_y(open(hit0[1], encoding="latin-1").read()) >= 90)
        if (mfirst in TOWER_MODELS or tall) and not scav:
            u = f if not isinstance(f, tuple) else f[0]
            thick = bbox_xz(open(hit0[1], encoding="latin-1").read())[0] > 50 if hit0 else True
            k = u * (THIN_WALLTOWER if u >= 0.99 else THIN_SMALL) if thick else u   # schlanke Tuerme bleiben
            f = (k, TOWER_HEIGHT, k)
        if (nw, nb) != (w, b):
            new_stats[sid]["width"], new_stats[sid]["breadth"] = nw, nb
        for m in models:
            k = m.lower()
            if k in model_factor and model_factor[k] != f:
                conflicts.append((m, model_factor[k], f, sid))
                pass
            else:
                model_factor[k] = f
        report.append((sid, f"{w}x{b}", f"{nw}x{nb}", (f if not isinstance(f, tuple) else "/".join(str(round(x, 3)) for x in f)), ",".join(models)))

    # Varianten (_col/_nex) und Basismodelle uebernehmen den Faktor ihres Originals
    manual = {"blbcfact": "blcfact1", "blbdrdcm": "bldrdcm0", "blbfact": "blfact0", "blbhq": "blhq",
              "blbpower": "blpower0", "blbresch": "blresch0",
              "miupdish": "miupbase", "miuptrim": "miupbase",
              "blwallc": "blwallc1", "blguard": "blguard2", "mibcool": "micool"}
    unresolved = []
    for rel, path in structs.items():
        base = os.path.basename(rel).lower()
        if base in model_factor:
            continue
        stem = re.sub(r"_(col|nex)(?=\.pie$)", "", base[:-4] + ".pie")[:-4]
        stem2 = re.sub(r"_(col|nex)$", "", base[:-4])
        stem2 = re.sub(r"_anim$", "", stem2)          # Animationsdateien wie blderik_anim
        cand = None
        for c in (stem2, manual.get(stem2, "")):
            if c and c + ".pie" in model_factor:
                cand = model_factor[c + ".pie"]; break
        if cand is None and re.match(r"bl(adv|gyr|hev|ind|las|nan|pow|rot)lab|blaerolb", stem2):
            cand = RESEARCH_FACTOR   # Forschungs-/Lab-Modelle
        if cand is None and re.match(r"blrpair\d", stem2):
            cand = REPAIR_FACTOR   # Reparaturstation (weitere Stufen)
        if cand is None:
            unresolved.append(rel); continue
        model_factor[base] = cand

    # ---- schreiben
    os.makedirs(a.out, exist_ok=True)
    n_s = n_c = 0
    for rel, path in structs.items():
        f = model_factor.get(os.path.basename(rel).lower())
        if f is None or f == KEEP or f == (1, 1, 1):
            continue
        dst = os.path.join(a.out, rel); os.makedirs(os.path.dirname(dst), exist_ok=True)
        src_text = open(path, encoding="latin-1", newline="").read()
        with open(dst, "w", encoding="latin-1", newline="") as fh:
            fh.write(scale_wall(src_text) if f == WALL else scale_pie(src_text, f))
        n_s += 1
    punk_models = {}
    for rel, path in comps.items():
        bn = os.path.basename(rel).lower()
        if bn in scav_only and bn != "exbloke.pie":
            continue
        txt = open(path, encoding="latin-1", newline="").read()
        if bn == "drhbod11.pie":
            txt = patch_python_connectors(txt)
        if bn in ("exbloke.pie", "runanim.pie", "fireknee.pie"):
            txt = add_mohawk(shape_head(txt))
        if bn == "exbloke.pie":
            txt = txt.replace("EVENT 3 flamfall.pie", "EVENT 3 bloodfall.pie")
        dst = os.path.join(a.out, rel); os.makedirs(os.path.dirname(dst), exist_ok=True)
        sc_txt = scale_pie(txt, SCAV_INFANTRY_FACTOR if bn == "exbloke.pie" else UNIT_FACTOR)
        with open(dst, "w", encoding="latin-1", newline="") as fh:
            fh.write(sc_txt)
        if bn in ("exbloke.pie", "runanim.pie", "fireknee.pie"):
            punk_models[bn] = sc_txt
        n_c += 1
    with open(os.path.join(a.out, "components", "bodies", "bloodfall.pie"), "w", newline="") as fh:
        fh.write(scale_pie(make_bloodfall(), a.sprite_size / 0.5) if a.sprite_scav else make_bloodfall())
    if a.punk:
        for bn, tx in punk_models.items():
            nm = bn.replace(".pie", "3d.pie")
            tx = tx.replace("runanim.pie", "runanim3d.pie").replace("fireknee.pie", "fireknee3d.pie").replace("page-32-soldier.png", "page-32-punk.png")
            with open(os.path.join(a.out, "components", "bodies", nm), "w", encoding="latin-1", newline="") as fh:
                fh.write(tx)
            tv = punk_models[bn].replace("runanim.pie", "runanim3dv.pie").replace("fireknee.pie", "fireknee3dv.pie")
            with open(os.path.join(a.out, "components", "bodies", bn.replace(".pie", "3dv.pie")), "w", encoding="latin-1", newline="") as fh:
                fh.write(tv)
    if a.sprite_scav:
        wp = next(p for r, p in comps.items() if os.path.basename(r).lower() == "walkanim.pie")
        for nm, tx in make_sprite_scav(open(wp, encoding="latin-1", newline="").read(), a.sprite_size, a.sprite_run).items():
            with open(os.path.join(a.out, "components", "bodies", nm), "w", newline="") as fh:
                fh.write(tx)
    if a.replace_comps:
        import shutil as _s2
        for f in glob.glob(os.path.join(a.replace_comps, "*.pie")):
            _s2.copyfile(f, os.path.join(a.out, "components", "bodies", os.path.basename(f)))
    n_e = 0
    if a.effects:
        for rel, path in collect(a.effects, "effects").items():
            name = os.path.basename(rel).lower()
            fac = next((k for pat, k in EFFECT_RULES if re.search(pat, name)), None)
            if fac is None or name in scav_only:
                continue
            dst = os.path.join(a.out, rel); os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "w", encoding="latin-1", newline="") as fh:
                fh.write(scale_pie(open(path, encoding="latin-1", newline="").read(), fac))
            n_e += 1
        print(f"Effekt-Modelle skaliert: {n_e}")
    if a.replace_structs:
        import shutil
        for f in glob.glob(os.path.join(a.replace_structs, "*.pie")):
            shutil.copyfile(f, os.path.join(a.out, "structs", os.path.basename(f)))
    # ---- Scavenger: selbstausrichtende Mauer, manuelle Moerserstellungs-Varianten
    import tempfile, shutil as _sh
    tmp = tempfile.mkdtemp()
    for n in ("blbrbwlh.pie", "blbrbcr1.pie"):
        _sh.copyfile(by_name[n][1], os.path.join(tmp, n))
    make_scav_wall_models(tmp, os.path.join(a.out, "structs"))
    if "A0BaBaHorizontalWall" in new_stats:
        new_stats["A0BaBaHorizontalWall"]["structureModel"] = ["BLBRBWLH.PIE", "blbrbwlx.pie", "blbrbwlt.pie", "blbrbwll.pie"]
    if "A0BabaCornerWall" in new_stats:
        new_stats["A0BabaCornerWall"]["structureModel"] = ["blbrbwlx.pie"]
    # Manuelle Mauerteile (Fallback, falls die Selbstausrichtung nicht greift): L, T, gerade in allen Drehungen
    if "A0BabaCornerWall" in new_stats:
        base_w = dict(new_stats["A0BabaCornerWall"])
        pieces = {"L": "blbrbwll.pie", "T": "blbrbwlt.pie", "H": "BLBRBWLH.PIE"}
        for key, nm in pieces.items():
            src = open(by_name[nm.lower()][1] if nm.lower() in by_name else os.path.join(a.out, "structs", nm.lower()),
                       encoding="latin-1", newline="").read()
            for k in ((0, 1, 2, 3) if key != "H" else (1,)):
                mn = f"scavwall_{key.lower()}{k * 90}.pie"
                with open(os.path.join(a.out, "structs", mn), "w", encoding="latin-1", newline="") as fh:
                    fh.write(pie_rotate(src, k) if k else src)
                v = dict(base_w); sid2 = f"A0BaBaWall{key}{k * 90}"
                v.update({"id": sid2, "structureModel": [mn],
                          "name": {"L": "Scavenger Wall Corner", "T": "Scavenger Wall T-Junction", "H": "Scavenger Wall (turned)"}[key] + f" {k * 90}"})
                new_stats[sid2] = v
    mt = open(by_name["blbrmrtp.pie"][1], encoding="latin-1", newline="").read()
    for k in (1, 2, 3):
        nm = f"blbrmrtp_r{k * 90}.pie"
        with open(os.path.join(a.out, "structs", nm), "w", encoding="latin-1", newline="") as fh:
            fh.write(pie_rotate(mt, k))
        if "A0BaBaMortarPit" in new_stats:
            v = dict(new_stats["A0BaBaMortarPit"])
            v.update({"id": f"A0BaBaMortarPitR{k * 90}", "name": f"Scavenger Mortar Pit (rotated {k * 90})", "structureModel": [nm]})
            new_stats[v["id"]] = v
    # Scavenger-Flugzeugfabrik: schraeg stehendes Modell achsenparallel ausrichten (Nord-Sued)
    if "scavvtol.pie" in by_name:
        vt = open(by_name["scavvtol.pie"][1], encoding="latin-1", newline="").read()
        with open(os.path.join(a.out, "structs", "scavvtol.pie"), "w", encoding="latin-1", newline="") as fh:
            vt2 = scale_pie(rot_any(vt, -26.4), 0.8)   # 20 % kleiner
            # Grundflaeche mittig auf das 2x2-Feld setzen (Mitte der Huelle auf x = z = 0)
            L_ = vt2.replace("\r\n", "\n").split("\n"); ip_ = [k for k, l_ in enumerate(L_) if l_.strip().startswith("POINTS")][0]
            n_ = int(L_[ip_].split()[1]); P_ = [L_[ip_ + 1 + k].split() for k in range(n_)]
            cx_ = (min(float(q[0]) for q in P_) + max(float(q[0]) for q in P_)) / 2; cz_ = (min(float(q[2]) for q in P_) + max(float(q[2]) for q in P_)) / 2
            for k_, q in enumerate(P_):
                L_[ip_ + 1 + k_] = "\t%s %s %s" % (fmt(float(q[0]) - cx_), q[1], fmt(float(q[2]) - cz_))
            fh.write("\n".join(L_))
    # Features (Deko): Modelle mit Labor-Textur (futuristischer Look) halbieren
    if a.features:
        n_f = 0
        for f in glob.glob(os.path.join(a.features, "*.pie")):
            txt = open(f, encoding="latin-1", newline="").read()
            if "laboratories" in txt[:400].lower() and os.path.basename(f).lower() != "micapsul.pie":
                dst = os.path.join(a.out, "features", os.path.basename(f)); os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "w", encoding="latin-1", newline="") as fh:
                    fh.write(scale_pie(txt, 0.5))
                n_f += 1
        print("Feature-Modelle halbiert:", n_f)
        # Hochhaeuser (SKYSCRAPER): auf ca. 190 Einheiten Grundflaeche verkleinern (ungefaehr wie building10)
        if a.features_json:
            fj = json.load(open(a.features_json, encoding="utf-8")); n_h = 0
            for fid, fv in fj.items():
                if fv.get("type") != "SKYSCRAPER":
                    continue
                mp_ = os.path.join(a.features, fv["model"].lower())
                if not os.path.exists(mp_):
                    continue
                txt = open(mp_, encoding="latin-1", newline="").read()
                L = levels_of(txt)
                if not L:
                    continue
                xs = [p[0] for p in L[0]]; zs = [p[2] for p in L[0]]
                ext = max(max(xs) - min(xs), max(zs) - min(zs))
                if ext > 205:
                    k = 190.0 / ext
                    dst = os.path.join(a.out, "features", os.path.basename(mp_)); os.makedirs(os.path.dirname(dst), exist_ok=True)
                    with open(dst, "w", encoding="latin-1", newline="") as fh:
                        fh.write(scale_pie(txt, k))
                    if fv.get("width", 1) > 2: fv["width"] = 2
                    if fv.get("breadth", 1) > 2: fv["breadth"] = 2
                    n_h += 1
            os.makedirs(os.path.join(a.out, "stats"), exist_ok=True)
            with open(os.path.join(a.out, "stats", "features.json"), "w", encoding="utf-8") as fh:
                json.dump(fj, fh, indent="\t", sort_keys=True)
            print("Hochhaus-Modelle verkleinert:", n_h)
    if a.templates_json:
        tp = json.load(open(a.templates_json, encoding="utf-8"))
        n_t = 0
        if a.punk and "BaBaPeople" in tp:
            pt = json.loads(json.dumps(tp["BaBaPeople"])); pt.update({"id": "BaBaPunk", "name": "BaBaPunk", "body": "B1BaBaPunk01"})
            tp["BaBaPunk"] = pt
            vt = json.loads(json.dumps(tp["BaBaPeople"])); vt.update({"id": "BaBaVest", "name": "BaBaVest", "body": "B1BaBaVest01"})
            tp["BaBaVest"] = vt
        for k, v in tp.items():
            if isinstance(v, dict) and (str(v.get("body", "")).startswith(("B1", "B2", "B3", "B4", "Bus", "Fire"))
                                        or str(v.get("propulsion", "")).startswith("BaBa")):
                v["available"] = True; n_t += 1
        os.makedirs(os.path.join(a.out, "stats"), exist_ok=True)
        with open(os.path.join(a.out, "stats", "templates.json"), "w", encoding="utf-8") as fh:
            json.dump(tp, fh, indent="\t", sort_keys=True)
        print("Scavenger-Vorlagen freigeschaltet:", n_t)
    os.makedirs(os.path.join(a.out, "stats"), exist_ok=True)
    # Verstaerkte Panzersperre (Betonpyramiden) + Forschung
    heavy = dict(new_stats["A0TankTrap"])
    heavy.update({"id": "A0TankTrapHeavy", "name": "Reinforced Tank Traps", "structureModel": ["tank-traps.pie"],
                  "hitpoints": 900, "armour": 30, "thermal": 25, "buildPoints": 120, "buildPower": 35})
    new_stats["A0TankTrapHeavy"] = heavy
    if a.research_json:
        res = json.load(open(a.research_json, encoding="utf-8"))
        res["R-Defense-TankTrap02"] = {"iconID": "IMAGE_RES_DEFENCE", "id": "R-Defense-TankTrap02",
            "msgName": "RES_TTRAP2", "name": "Reinforced Tank Traps",
            "requiredResearch": ["R-Defense-TankTrap01", "R-Defense-HardcreteWall"],
            "researchPoints": 480, "researchPower": 14, "resultStructures": ["A0TankTrapHeavy"],
            "statID": "A0TankTrapHeavy"}
        with open(os.path.join(a.out, "stats", "research.json"), "w", encoding="utf-8") as fh:
            json.dump(res, fh, indent="\t", sort_keys=True)
    if a.body_json and a.weapons_json and scav_wcopy:
        for orig, nn in scav_wcopy.items():
            src = next((p for r, p in comps.items() if os.path.basename(r).lower() == orig), None)
            if src:
                dst = os.path.join(a.out, "components", "weapons", nn.lower()); os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "w", encoding="latin-1", newline="") as fh:
                    fh.write(open(src, encoding="latin-1", newline="").read())
        RF = a.range_factor
        if RF != 1.0:
            for v in wout.values():
                for key in ("shortRange", "longRange", "minRange", "flightSpeed", "radius", "periodicalDamageRadius", "empRadius"):
                    if key in v and isinstance(v[key], (int, float)) and v[key]:
                        v[key] = max(1, int(round(v[key] * RF)))
        with open(os.path.join(a.out, "stats", "weapons.json"), "w", encoding="utf-8") as fh:
            json.dump(wout, fh, indent="\t", sort_keys=True)
    if a.propulsion_json and a.speed_factor != 1.0:
        pj = json.load(open(a.propulsion_json, encoding="utf-8"))
        for v in pj.values():
            for key in ("speed",):
                if key in v and isinstance(v[key], (int, float)):
                    v[key] = max(1, int(round(v[key] * a.speed_factor)))
        with open(os.path.join(a.out, "stats", "propulsion.json"), "w", encoding="utf-8") as fh:
            json.dump(pj, fh, indent="\t", sort_keys=True)
    if a.sensor_json and a.range_factor != 1.0:
        sj = json.load(open(a.sensor_json, encoding="utf-8"))
        for v in sj.values():
            for key in ("range", "location"):
                if key in v and isinstance(v[key], (int, float)) and key == "range":
                    v[key] = max(1, int(round(v[key] * a.range_factor)))
        with open(os.path.join(a.out, "stats", "sensor.json"), "w", encoding="utf-8") as fh:
            json.dump(sj, fh, indent="\t", sort_keys=True)
    if body_stats:
        bs = json.loads(json.dumps(body_stats)); bs["Body11ABT"]["weaponSlots"] = 2
        if a.punk and "B1BaBaPerson01" in bs:
            pb = json.loads(json.dumps(bs["B1BaBaPerson01"])); pb.update({"id": "B1BaBaPunk01", "name": "*Punk Body*", "model": "exbloke3d.pie"})
            bs["B1BaBaPunk01"] = pb
            vb = json.loads(json.dumps(bs["B1BaBaPerson01"])); vb.update({"id": "B1BaBaVest01", "name": "*Vest Body*", "model": "exbloke3dv.pie"})
            bs["B1BaBaVest01"] = vb
        with open(os.path.join(a.out, "stats", "body.json"), "w", encoding="utf-8") as fh:
            json.dump(bs, fh, indent="\t", sort_keys=True)
    with open(os.path.join(a.out, "stats", "structure.json"), "w", encoding="utf-8") as fh:
        json.dump(new_stats, fh, indent="\t", sort_keys=True)
    with open(os.path.join(a.out, "report_structures.csv"), "w", newline="", encoding="utf-8") as fh:
        csv.writer(fh, delimiter=";").writerows([("id", "alt", "neu", "faktor", "modelle")] + report)

    print(f"Gebaeude-Modelle skaliert: {n_s}, Komponenten-Modelle skaliert: {n_c}")
    print("Footprints geaendert:", sum(1 for k in stats if (stats[k].get('width'), stats[k].get('breadth')) !=
                                       (new_stats[k].get('width'), new_stats[k].get('breadth'))))
    if conflicts:
        print("Gemeinsam genutzte Modelle mit unterschiedlichem Faktor (erster Wert bleibt):", conflicts)
    if unresolved:
        print("Nicht zugeordnet (unveraendert gelassen):", unresolved)


if __name__ == "__main__":
    sys.exit(main())
