"""Wracks: verkohlte, aufrecht stehende Fahrzeugmodelle (Drehachse in der Mitte; die Engine kippt sie bei der Todesanimation),
Wrack-Features, Motor- und Aufprallgeraeusche.
wrecks.py OUT   -> OUT/features/wreck_*.pie, OUT/texpages/page-45x-wreck.png, Features ModWreckS0.. / ModWreckB_<Rumpf>, OUT/audio/wreck/*.ogg"""
import sys, os, re, math, json, subprocess
import numpy as np
from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else "out_u"
SRC = ["/tmp/claude-0/cmp/base", "/tmp/claude-0/cmp/mp"]
FEAT = "/home/claude/w/feat/features"
PAGE_NO = {}          # Quellseite -> Nummer der verkohlten Seite
SIDE_PX = 512         # Kantenlaenge der verkohlten Seiten (Quellen sind bis zu 1024)


def find(rel, dirs=None):
    for d in ([OUT] if dirs is None else []) + SRC:
        p = os.path.join(d, rel)
        if os.path.exists(p): return p
    # Gross-/Kleinschreibung ignorieren
    for d in [OUT] + SRC:
        dd = os.path.join(d, os.path.dirname(rel))
        if os.path.isdir(dd):
            for f in os.listdir(dd):
                if f.lower() == os.path.basename(rel).lower(): return os.path.join(dd, f)
    raise FileNotFoundError(rel)


def parse(path):
    """erste Ebene: Seite, Punkte, Flaechen (Flags als Text, UV normiert)"""
    t = open(path, encoding="latin-1").read().replace("\r", "").split("\n")
    page, tw, th = None, 0, 0
    pts, pol = [], []
    for i, l in enumerate(t):
        w = l.split()
        if not w: continue
        if w[0] == "TEXTURE" and page is None:
            page = w[2]; tw, th = float(w[3]), float(w[4])
        elif w[0] == "POINTS" and not pts:
            n = int(w[1]); pts = [tuple(float(v) for v in t[i + 1 + k].split()[:3]) for k in range(n)]
        elif w[0] == "POLYGONS" and not pol:
            n = int(w[1])
            for k in range(n):
                v = t[i + 1 + k].split(); c = int(v[1])
                idx = [int(x) for x in v[2:2 + c]]
                uv = [float(x) for x in v[2 + c:2 + 3 * c]]
                if tw and th: uv = [x / (tw if j % 2 == 0 else th) for j, x in enumerate(uv)]
                pol.append((v[0], idx, [(uv[2 * j], uv[2 * j + 1]) for j in range(c)]))
    return page, np.array(pts), pol


def burn(im, seed):
    """Brandschwaerzung: entsaettigen, abdunkeln, rusige Rauchflecken"""
    im = im.convert("RGBA")
    if im.size[0] > SIDE_PX: im = im.resize((SIDE_PX, int(im.size[1] * SIDE_PX / im.size[0])), Image.LANCZOS)
    a = np.asarray(im).astype(np.float32) / 255.0
    rgb, al = a[..., :3], a[..., 3:]
    h, w = rgb.shape[:2]
    rng = np.random.RandomState(seed)
    n = np.zeros((h, w), np.float32)
    for sc, wt in ((6, 0.45), (18, 0.33), (48, 0.22)):
        small = (rng.rand(max(2, h // sc), max(2, w // sc)) * 255).astype(np.uint8)
        n += wt * np.asarray(Image.fromarray(small).resize((w, h), Image.BICUBIC), np.float32) / 255.0
    soot = np.clip(0.22 + 1.1 * n, 0.2, 1.0)
    # schwarze Brandflecken (grossflaechig) und feine Russstreifen
    big = np.zeros((h, w), np.float32)
    for sc, wt in ((14, 0.6), (40, 0.4)):
        small = (rng.rand(max(2, h // sc), max(2, w // sc)) * 255).astype(np.uint8)
        big += wt * np.asarray(Image.fromarray(small).resize((w, h), Image.BICUBIC), np.float32) / 255.0
    patch = np.clip((big - 0.52) * 5.0, 0.0, 1.0)                 # 0 = unversehrt, 1 = schwarz verbrannt
    lum = rgb.mean(2, keepdims=True)
    grey = rgb * 0.28 + lum * 0.72                                # stark entsaettigt
    out = grey * 0.34 * soot[..., None] * (1.0 - 0.80 * patch[..., None]) + 0.010
    out[..., 0] *= 1.12; out[..., 2] *= 0.86                      # warmer Russton
    res = np.concatenate([np.clip(out, 0, 1), al], axis=2)
    return Image.fromarray((res * 255).astype(np.uint8), "RGBA")


def burnt_page(page, out_dir):
    """gibt den Dateinamen der verkohlten Fassung einer Textur-Seite zurueck (wird bei Bedarf erzeugt)"""
    key = page.lower()
    if key in PAGE_NO: return PAGE_NO[key]
    n = 450 + len(PAGE_NO)
    name = "page-%d-wreck.png" % n
    src = find("texpages/" + page)
    burn(Image.open(src), n).save(os.path.join(out_dir, name), optimize=True)
    PAGE_NO[key] = name
    return name


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def write_pie(path, pts, pol, page):
    out = ["PIE 3", "TYPE 200", "TEXTURE 0 %s 0 0" % page, "LEVELS 1", "LEVEL 1", "POINTS %d" % len(pts)]
    out += ["\t%.2f %.2f %.2f" % tuple(p) for p in pts]
    out.append("POLYGONS %d" % len(pol))
    for fl, idx, uv in pol:
        out.append("\t%s %d %s %s" % (fl, len(idx), " ".join(str(i) for i in idx), " ".join("%.5f %.5f" % u for u in uv)))
    out.append("")
    open(path, "w", encoding="latin-1", newline="").write("\n".join(out))


def make_model(src_path, dst, page_dir, scale=1.0, flip=False, target_len=None):
    page, pts, pol = parse(src_path)
    # Langachse = z
    ext = pts.max(0) - pts.min(0)
    if ext[0] > ext[2] * 1.05: pts = (rot_y(math.pi / 2) @ pts.T).T
    if flip: pts = (rot_y(math.pi) @ pts.T).T
    ext = pts.max(0) - pts.min(0)
    if target_len: scale = target_len / ext[2]
    pts = pts * scale
    c = (pts.max(0) + pts.min(0)) / 2.0            # Drehachse: Mitte des Modells
    pts = pts - c
    ext = pts.max(0) - pts.min(0)
    pol = [(fl, idx, uv) for fl, idx, uv in pol]
    wp = burnt_page(page, page_dir)
    write_pie(dst, pts, pol, wp)
    return ext


# ------------------------------------------------------------------ Modelle
VEH_STEMS = ["exjeep", "exjeeprk", "exbuggy", "extrike", "camper", "icevan", "scavtruck", "scavpick", "scavlkw", "scavtrak",
             "scavlader", "scavtanker", "scavnexus", "tractor", "exschool", "exfire"]
# allgemeine Wracks nach Groesse: (Quelle, Anteil an der Referenzlaenge)
GENERIC = {
    "S": [("mijeep.pie", 1.0), ("miblucar.pie", 1.0), ("mipickup.pie", 1.0), ("michevy.pie", 1.0), ("micamper.pie", 0.95)],
    "M": [("micamper.pie", 1.1), ("icevan", 1.0), ("miarthov.pie", 0.95), ("mipickup.pie", 1.15)],
    "L": [("scavtruck", 1.0), ("miarthov.pie", 1.1), ("mitanker_straight", 1.0)],
}
TARGET = {"S": 50.0, "M": 80.0, "L": 118.0}


def body_src(stem):
    return find("components/bodies/%s.pie" % stem)


def build_models(page_dir, feat_dir):
    made = {}
    for stem in VEH_STEMS:
        try: p = body_src(stem)
        except FileNotFoundError: print("   fehlt:", stem); continue
        dst = os.path.join(feat_dir, "wreck_%s.pie" % stem)
        ext = make_model(p, dst, page_dir)
        made[stem] = ext
    ref = TARGET
    gen = {}
    for cls, lst in GENERIC.items():
        for i, (name, fac) in enumerate(lst):
            if name.endswith(".pie"): path = os.path.join(FEAT, name)
            elif name == "mitanker_straight": path = os.path.join(OUT, "features", "mitanker_straight.pie") if os.path.exists(os.path.join(OUT, "features", "mitanker_straight.pie")) else os.path.join(FEAT, "mitanker_straight.pie")
            else: path = body_src(name)
            dst = os.path.join(feat_dir, "wreck_%s%d.pie" % (cls.lower(), i))
            ext = make_model(path, dst, page_dir, target_len=ref.get(cls, 60.0) * fac)
            gen["ModWreck%s%d" % (cls, i)] = ("wreck_%s%d.pie" % (cls.lower(), i), ext)
    return made, gen


def build_stats(made, gen):
    fp = os.path.join(OUT, "stats", "features.json")
    feats = json.load(open(fp, encoding="utf-8"))
    base = {"armour": 1, "breadth": 1, "width": 1, "hitpoints": 100, "damageable": 0, "lineOfSight": 1, "startVisible": 0, "tileDraw": 1, "type": "VEHICLE"}
    for fid, (model, ext) in gen.items():
        d = dict(base); d.update({"id": fid, "model": model, "name": "*Wrack*"}); feats[fid] = d
    bp = os.path.join(OUT, "stats", "body.json")
    bodies = json.load(open(bp, encoding="utf-8"))
    n = 0
    for bid, b in bodies.items():
        stem = os.path.splitext(os.path.basename(str(b.get("model", ""))))[0].lower()
        if stem in made:
            fid = "ModWreckB_" + bid
            d = dict(base); d.update({"id": fid, "model": "wreck_%s.pie" % stem, "name": "*Wrack*"}); feats[fid] = d; n += 1
    json.dump(feats, open(fp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Wrack-Features: %d allgemein, %d fuer bestimmte Rumpfe" % (len(gen), n))


# ------------------------------------------------------------------ Sounds
SR = 22050


def lowpass(x, fc):
    from scipy.signal import butter, lfilter
    b, a = butter(2, fc / (SR / 2.0)); return lfilter(b, a, x)


def bandpass(x, lo, hi):
    from scipy.signal import butter, lfilter
    b, a = butter(2, [lo / (SR / 2.0), hi / (SR / 2.0)], btype="band"); return lfilter(b, a, x)


def encode(y, dst):
    y = y / (np.abs(y).max() + 1e-9) * 0.9
    r = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "f32le", "-ar", str(SR), "-ac", "1", "-i", "-", "-c:a", "libvorbis", "-q:a", "4", "-fflags", "+bitexact", "-flags:a", "+bitexact", dst],
                       input=y.astype("<f4").tobytes(), capture_output=True)
    if r.returncode != 0: raise RuntimeError(r.stderr.decode())


def engine_rev(rng):
    """heulender Motor: Drehzahl steigt schnell, kurz vor Ende bricht sie ein"""
    T = 2.1; n = int(T * SR); t = np.arange(n) / SR
    f = 55 + 150 * (1 - np.exp(-t * 2.2)); f = np.where(t > 1.55, f * (1 - 0.55 * np.clip((t - 1.55) / (T - 1.55), 0, 1) ** 1.5), f)
    ph = 2 * np.pi * np.cumsum(f) / SR
    y = np.zeros(n)
    for h in range(1, 16): y += np.sin(h * ph + h * 0.7) / (h ** 0.85)
    y = np.tanh(2.6 * y / 3.0)
    y *= 0.7 + 0.3 * np.sign(np.sin(ph * 0.5))                     # Zuendungsrhythmus
    nz = lowpass(rng.randn(n), 2600) * (0.15 + 0.35 * np.clip(t / 1.3, 0, 1))
    y = y + nz
    env = np.clip(t / 0.06, 0, 1) * np.where(t < T - 0.5, 1.0, np.clip((T - t) / 0.5, 0, 1) ** 1.5)
    return lowpass(y * env, 4800)


def impact(n, t0, strength, rng, out):
    i0 = int(t0 * SR)
    L = min(n - i0, int(0.9 * SR))
    if L <= 0: return
    t = np.arange(L) / SR
    thump = np.sin(2 * np.pi * (80 - 45 * np.minimum(t / 0.25, 1.0)) * t) * np.exp(-t * 14)
    clang = sum(np.sin(2 * np.pi * fr * t + rng.rand() * 6) * np.exp(-t * d) * a for fr, d, a in ((380, 9, 0.35), (617, 12, 0.3), (1130, 16, 0.22), (1790, 22, 0.15), (2630, 30, 0.1)))
    crunch = bandpass(rng.randn(L), 500, 4500) * np.exp(-t * 18) * 0.7
    out[i0:i0 + L] += strength * (thump * 1.2 + clang * 0.8 + crunch)


def crash(variant, rng):
    """Aufprall(e) und Schleifen: A kippt (1 Aufprall), B ueberschlaegt (3), C doppelt (5)"""
    T = {"a": 1.4, "b": 2.0, "c": 2.9}[variant]; n = int(T * SR)
    y = np.zeros(n)
    hits = {"a": [(0.05, 1.0)], "b": [(0.05, 0.8), (0.55, 1.0), (1.05, 0.55)], "c": [(0.05, 0.7), (0.5, 0.9), (1.0, 1.0), (1.55, 0.8), (2.05, 0.5)]}[variant]
    for t0, st in hits: impact(n, t0, st, rng, y)
    # Schleifgeraeusch (Blech auf Boden), ungleichmaessig
    t = np.arange(n) / SR
    mod = np.clip(lowpass(rng.randn(n), 9) * 6 + 0.6, 0, 1)
    end = hits[-1][0] + 0.35
    scr = bandpass(rng.randn(n), 700, 3200) * mod * np.where(t < end, 1.0, np.exp(-(t - end) * 6))
    y += scr * 0.22 * np.exp(-t / (T * 0.8))
    # Nachklappern
    for k in range(5):
        impact(n, hits[-1][0] + 0.45 + 0.13 * k + 0.05 * rng.rand(), 0.07 / (k + 1), rng, y)
    return y


def make_sounds():
    rng = np.random.RandomState(2100)
    d = os.path.join(OUT, "audio", "wreck"); os.makedirs(d, exist_ok=True)
    encode(engine_rev(rng), os.path.join(d, "wreckrev.ogg"))
    for v in "abc": encode(crash(v, rng), os.path.join(d, "wreckcrash_%s.ogg" % v))
    ap = os.path.join(OUT, "audio", "audio.json"); A = json.load(open(ap, encoding="utf-8"))
    A["WreckSounds"] = {"_name": "Wreck sounds", "data": [
        {"fileName": "wreckrev.ogg", "loop": False, "range": 2200, "volume": 85, "z_comment": "Motorheulen beim Ausrollen"},
        {"fileName": "wreckcrash_a.ogg", "loop": False, "range": 2000, "volume": 100, "z_comment": "Kippen"},
        {"fileName": "wreckcrash_b.ogg", "loop": False, "range": 2000, "volume": 100, "z_comment": "Ueberschlag"},
        {"fileName": "wreckcrash_c.ogg", "loop": False, "range": 2200, "volume": 100, "z_comment": "Doppelter Ueberschlag"}]}
    json.dump(A, open(ap, "w"), indent=1)
    wp = os.path.join(OUT, "wrf", "audio.wrf"); w = open(wp, "rb").read().decode("latin-1")
    if 'audio/wreck' not in w:
        marker = 'directory\t"audio"\r\nfile\t\tAUDIOCFG\t"audio.json"'
        assert w.count(marker) == 1, "AUDIOCFG-Block nicht gefunden"
        block = 'directory\t"audio/wreck"\r\n' + ''.join('file\t\tWAV\t"%s"\r\n' % f for f in ("wreckrev.ogg", "wreckcrash_a.ogg", "wreckcrash_b.ogg", "wreckcrash_c.ogg"))
        open(wp, "wb").write(w.replace(marker, block + marker).encode("latin-1"))
    print("Wrack-Sounds erzeugt")


def main():
    page_dir = os.path.join(OUT, "texpages"); feat_dir = os.path.join(OUT, "features")
    os.makedirs(page_dir, exist_ok=True); os.makedirs(feat_dir, exist_ok=True)
    made, gen = build_models(page_dir, feat_dir)
    build_stats(made, gen)
    make_sounds()
    for k, v in sorted(made.items()): print("  %-10s Laenge %.0f Breite %.0f Hoehe %.0f" % (k, v[2], v[0], v[1]))
    for k, (m, v) in sorted(gen.items()): print("  %-12s %s Laenge %.0f Breite %.0f Hoehe %.0f" % (k, m, v[2], v[0], v[1]))
    print("verkohlte Seiten:", sorted(PAGE_NO.values()))


main()
