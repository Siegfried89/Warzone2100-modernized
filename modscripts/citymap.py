"""Stadtkarte 'Stadt' (2 Spieler, 256x256 Kacheln = groesste Kartengroesse der Engine).
Erzeugt ein klassisches Karten-Paket (2c-Stadt.wz) fuer den Ordner ...\\Warzone 2100 master\\maps\\.
Aufbau (Wunsch 12.11.): Boden wie die Landefflaeche (Stein/Beton), viel heller Wuestenstaub (Alpha-Kampagne = Arizona-Kacheln),
riesige, flache, aufgelockerte Stadt mit allen Hochhausmodellen, die NICHT bis zum Rand geht; Strassen/Highways, lange Bruecken ueber einen Fluss;
Mitte offen (Lager/Zufluchtsort mit Landfill-Kulissen, Spieler 0); Spieler 1 in der Wuestenecke. Keine Unterfuehrungen.
citymap.py [AUSGABEORDNER]   (schreibt <AUSGABE>/2c-Stadt.wz und den entpackten Ordner <AUSGABE>/city/ zum Pruefen)"""
import os, sys, json, struct, math, zipfile, shutil
import numpy as np

OUTDIR = sys.argv[1] if len(sys.argv) > 1 else "citymap_out"
W = H = 256
CX = CY = 128
rng = np.random.RandomState(2100)
TTP = "/tmp/claude-0/cmp/base/wrf/cam1/cam1a/ttypes.ttp"   # Arizona-Kachel-Typentabelle (waehlt den Arizona-Kachelsatz)

# ---- Kachel-Nummern (Arizona, siehe Kachelblatt)
CONCRETE = (19, 22, 77)              # grauer Beton (Typ Strasse = befahrbar)
CONCRETE_EDGE = 21                   # Beton mit Erdkante
ASPHALT = 59                         # Asphalt mit gelber Mittellinie (waagrecht)
DUST = (12, 1, 4, 42)                # helle Wuestenstaub-Kacheln
DUST_BRUSH = (0, 9, 10, 11)          # Staub mit Bueschen
RED = (20, 27, 29, 41, 43, 44, 48, 53, 54)   # rote Erde
BAKED = (2, 3, 6, 36)                # rissige Erde
WATER = 17
ROT = lambda r: (r & 3) << 12        # Drehung 0..3
XF, YF = 0x8000, 0x4000


def vnoise(scale, seed):
    r = np.random.RandomState(seed)
    n = max(2, int(W / scale) + 2)
    g = r.rand(n, n)
    ys = np.linspace(0, n - 2.001, H); xs = np.linspace(0, n - 2.001, W)
    y0 = ys.astype(int); x0 = xs.astype(int); fy = (ys - y0)[:, None]; fx = (xs - x0)[None, :]
    fy = fy * fy * (3 - 2 * fy); fx = fx * fx * (3 - 2 * fx)
    a = g[np.ix_(y0, x0)]; b = g[np.ix_(y0, x0 + 1)]; c = g[np.ix_(y0 + 1, x0)]; d = g[np.ix_(y0 + 1, x0 + 1)]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


yy, xx = np.mgrid[0:H, 0:W]
dx = xx - CX; dy = yy - CY
dist = np.hypot(dx, dy)
cheb = np.maximum(np.abs(dx), np.abs(dy))
dust_n = 0.55 * vnoise(24, 1) + 0.30 * vnoise(9, 2) + 0.15 * vnoise(4, 3)   # Staubverteilung 0..1
var_n = vnoise(5, 4)

PLAZA_R = 26            # offener Platz in der Mitte
RING = (31, 36)         # Ringstrasse (Tschebyschew-Abstand), 6 breit
CITY_R = 106            # Stadtrand
PITCH = 16

# ---- Gelaende-Hoehe (Wert * 2 = Weltmass), Stadt flach
H0 = 150
hgt = np.full((H, W), H0, dtype=np.int32)
desert = np.clip((dist - (CITY_R + 8)) / 30.0, 0, 1)
hgt = (hgt + ((vnoise(30, 5) - 0.5) * 34 + (vnoise(11, 6) - 0.5) * 10) * desert).astype(np.int32)

# ---- Fluss (N-S, schlaengelt sich) mit Bruecken
RIVER_W = 14
river_c = CX + 68 + 10 * np.sin(np.arange(H) / 23.0)
river = np.zeros((H, W), dtype=bool)
for y in range(H):
    c = river_c[y]
    river[y, :] = np.abs(np.arange(W) - c) <= RIVER_W / 2
HWY_E = (dy >= -3) & (dy <= 2); HWY_N = (dx >= -3) & (dx <= 2)
BRIDGE_Y = [CY - 50, CY + 50]   # zwei weitere Bruecken ueber den Fluss (Streifen 6 breit)
bridge = np.zeros((H, W), dtype=bool)
for by in BRIDGE_Y:
    bridge[by - 3: by + 3, :] = True
bridge &= river | (np.abs(xx - river_c[yy[:, 0]][:, None]) <= RIVER_W / 2 + 2)
deck = river & ((HWY_E) | bridge)           # Fahrbahn ueber dem Fluss
# Flussbett tiefer, Daemme/Bruecken auf Strassenhoehe
hgt[river & ~deck] -= 14
hgt[(np.abs(xx - river_c[yy[:, 0]][:, None]) <= RIVER_W / 2 + 1) & ~river] -= 5   # Uferboeschung

# ---- Strassenraster der Stadt
in_city = (dist <= CITY_R) & (cheb > RING[1] + 1)
sx = ((dx + 8) % PITCH)       # 0..2 = senkrechte Strasse
sy = ((dy + 8) % PITCH)
street_v = in_city & (sx < 3) & ~river
street_h = in_city & (sy < 3) & ~river
street = street_v | street_h
ring = (cheb >= RING[0]) & (cheb <= RING[1])
highway = (HWY_E | HWY_N | ring) & (cheb >= RING[0])
inside_ring = cheb < RING[0]

tex = np.zeros((H, W), dtype=np.int32)
for y in range(H):
    for x in range(W):
        v = var_n[y, x]; d = dist[y, x]; n = dust_n[y, x]
        r = int(rng.randint(0, 4)); fl = int(rng.randint(0, 4)) << 14
        if river[y, x] and not deck[y, x]:
            t = WATER
        elif deck[y, x] or highway[y, x]:
            if HWY_E[y, x] and not (ring[y, x] and abs(dx[y, x]) > abs(dy[y, x])): horizontal = True
            elif HWY_N[y, x] and not HWY_E[y, x]: horizontal = False
            else: horizontal = abs(dy[y, x]) > abs(dx[y, x])
            t = ASPHALT | (0 if horizontal else ROT(1))
            if horizontal and int(dy[y, x]) % 2 == 0: t |= YF
            if (not horizontal) and int(dx[y, x]) % 2 == 0: t |= XF
        elif street[y, x]:
            vert = bool(street_v[y, x]); k = (sx[y, x] if vert else sy[y, x])
            if k == 1:   # Fahrbahn in der Mitte
                t = ASPHALT | (ROT(1) if vert else 0)
            else:
                t = int(rng.choice(CONCRETE)) | fl
                if n > 0.70 and rng.rand() < 0.5: t = int(rng.choice(DUST)) | fl
        elif inside_ring[y, x]:
            # Platz: Beton mit viel Staub, aussen (Richtung Ring) mehr Beton
            t = int(rng.choice(CONCRETE)) | fl
            if n > 0.52 and rng.rand() < 0.75: t = int(rng.choice(DUST)) | fl
            elif n > 0.46: t = CONCRETE_EDGE | ROT(r)
        elif d <= CITY_R:
            t = int(rng.choice(CONCRETE)) | fl
            if n > 0.55: t = int(rng.choice(DUST)) | fl if rng.rand() < 0.8 else int(rng.choice(DUST_BRUSH)) | fl
            elif n > 0.47: t = CONCRETE_EDGE | ROT(r)
        else:
            # Wueste: Staub, nach aussen mehr rote Erde und Risse
            k = (d - CITY_R) / 60.0
            if n > 0.5 - 0.15 * (1 - min(1, k)):   # Staubfelder
                t = int(rng.choice(DUST + DUST_BRUSH)) | fl
            elif v > 0.55 + 0.1 * min(1, k):
                t = int(rng.choice(BAKED)) | fl
            else:
                t = int(rng.choice(RED)) | fl
            if d < CITY_R + 14 and n < 0.42 and rng.rand() < 0.35: t = int(rng.choice(CONCRETE)) | fl   # ausfransender Stadtrand
        tex[y, x] = t

# ---- Objekte
feats = []     # (name, tx, ty, w, b, rot)
occ = np.zeros((H, W), dtype=bool)   # belegte Kacheln (Objekte)
bad = river | highway | street | inside_ring | (cheb <= RING[1] + 1)


def free(tx, ty, w, b, margin=0):
    if tx - margin < 1 or ty - margin < 1 or tx + w + margin >= W - 1 or ty + b + margin >= H - 1: return False
    s = (slice(ty - margin, ty + b + margin), slice(tx - margin, tx + w + margin))
    return not occ[s].any() and not river[s].any() and not highway[s].any() and not street[s].any()


def put(name, tx, ty, w, b, rot=0, margin=0):
    if not free(tx, ty, w, b, margin): return False
    occ[ty:ty + b, tx:tx + w] = True; feats.append((name, tx, ty, w, b, rot)); return True


TOWERS = [("building1", 2, 2), ("building2", 2, 2), ("building3", 2, 2), ("building7", 2, 2), ("building8", 2, 2), ("building11", 2, 2),
          ("building10", 2, 1), ("building12", 2, 1), ("bigcooltowr", 2, 2), ("WaterBuilding", 2, 2), ("WaterBuilding2", 2, 2), ("WaterBuilding3", 2, 2)]
WRECKED = [("WreckedBuilding9", 2, 2), ("WreckedBuilding16", 2, 2), ("WreckedBuilding17", 2, 2)]
# Hochhaeuser: je Block 1 bis 3, grosse Abstaende (aufgelockert), nach aussen mehr Ruinen
for by in range(-8, 9):
    for bx in range(-8, 9):
        x0 = CX - 8 + bx * PITCH + 3; y0 = CY - 8 + by * PITCH + 3   # Blockinnenraum beginnt hinter der Strasse (3 breit), 13x13
        mx, my = x0 + 6, y0 + 6
        d = math.hypot(mx - CX, my - CY)
        if d > CITY_R - 6 or max(abs(mx - CX), abs(my - CY)) <= RING[1] + 6: continue
        if rng.rand() < 0.12: continue    # freie Bloecke (Plaetze, Brachen)
        n = 2 + (rng.rand() < 0.6) + (rng.rand() < 0.35)
        outer = d / CITY_R
        for _ in range(int(n)):
            for _try in range(20):
                name, w, b = (WRECKED[rng.randint(len(WRECKED))] if rng.rand() < 0.04 + 0.28 * max(0, outer - 0.55) else TOWERS[rng.randint(len(TOWERS))])
                tx = x0 + 1 + rng.randint(0, 13 - w - 1); ty = y0 + 1 + rng.randint(0, 13 - b - 1)
                if put(name, tx, ty, w, b, 0, margin=1 if rng.rand() < 0.6 else 2): break

# Mitte: Lager/Zufluchtsort (Landfill-Kulissen am Platzrand, Fassgruppen, Wracks); Mitte bleibt frei
for a in np.linspace(0, 2 * math.pi, 30, endpoint=False):
    r = PLAZA_R - 3 + rng.rand() * 2
    tx = int(CX + r * math.cos(a)) - 1; ty = int(CY + r * math.sin(a)) - 1
    nm, w, b = [("Landfill4", 2, 2), ("Landfill6", 2, 3), ("Landfill9", 3, 3)][rng.randint(3)]
    if rng.rand() < 0.75: put(nm, tx, ty, w, b, int(rng.randint(0, 4)) * 90, margin=1)
for _ in range(14):
    a = rng.rand() * 2 * math.pi; r = 8 + rng.rand() * 14
    tx = int(CX + r * math.cos(a)); ty = int(CY + r * math.sin(a))
    put(["OilDrum5", "OilDrum10", "Crate", "BlueCar", "Chevy", "WreckedSuzukiJeep", "WreckedVertCampVan", "WreckedTankerV", "Pickup"][rng.randint(9)], tx, ty, 1, 1, int(rng.randint(0, 4)) * 90)
for _ in range(10):
    a = rng.rand() * 2 * math.pi; r = 20 + rng.rand() * 5
    put(["Boulder1", "Boulder2", "Boulder3", "Wreck1", "Wreck2", "Wreck3"][rng.randint(6)], int(CX + r * math.cos(a)), int(CY + r * math.sin(a)), 1, 1, int(rng.randint(0, 4)) * 90)

# Strassenmoebel: Autos und Barrieren, jede Strasse nur sparsam
CARS = ["BlueCar", "Chevy", "Pickup", "WreckedSuzukiJeep", "WreckedVertCampVan", "barrier", "WreckedTankerV", "hoverwreck"]
ys_, xs_ = np.where(street & ~highway)
for i in rng.permutation(len(ys_))[:260]:
    ty, tx = int(ys_[i]), int(xs_[i])
    if (street_v[ty, tx] and sx[ty, tx] != 1) or (street_h[ty, tx] and sy[ty, tx] != 1 and not street_v[ty, tx]):
        continue   # nur Fahrbahnmitte
    if occ[ty - 1:ty + 2, tx - 1:tx + 2].any(): continue
    occ[ty, tx] = True; feats.append((CARS[rng.randint(len(CARS))], tx, ty, 1, 1, int(rng.randint(0, 4)) * 90))

# Wueste: Felsen, Ruinen, Trockenbueschen, einzelne Bauten
for _ in range(900):
    tx = int(rng.randint(3, W - 3)); ty = int(rng.randint(3, H - 3))
    if math.hypot(tx - CX, ty - CY) < CITY_R + 5: continue
    nm = ["Boulder1", "Boulder2", "Boulder3", "arizonabush1", "arizonabush2", "arizonabush3", "arizonabush4", "arizonatree1", "arizonatree2", "Ruin1", "Ruin3", "Ruin4", "Ruin5", "Ruin6", "Wreck0", "Wreck4"][rng.randint(16)]
    put(nm, tx, ty, 1, 1, int(rng.randint(0, 4)) * 90)
for _ in range(40):
    tx = int(rng.randint(3, W - 4)); ty = int(rng.randint(3, H - 4))
    if math.hypot(tx - CX, ty - CY) < CITY_R + 12: continue
    nm, w, b = [("WaterTower", 1, 1), ("Pylon", 1, 1), ("BarbWarehouse1", 2, 1), ("BarbHUT", 1, 1), ("OldFactory", 2, 2), ("LogCabin1", 1, 1), ("WreckedBridge", 1, 2)][rng.randint(7)]
    put(nm, tx, ty, w, b, int(rng.randint(0, 4)) * 90, margin=1)

# ---- Startpunkte, Oelquellen
P0 = (CX, CY + 12)            # Spieler 0: Lager in der Mitte
P1 = (222, 32)                # Spieler 1: Wuestenecke
oils = []
def oil_ring(px, py, n, r0, r1):
    k = 0
    while k < n:
        a = rng.rand() * 2 * math.pi; r = r0 + rng.rand() * (r1 - r0)
        tx = int(px + r * math.cos(a)); ty = int(py + r * math.sin(a))
        if free(tx, ty, 1, 1, 0) and not occ[ty, tx]:
            occ[ty, tx] = True; oils.append((tx, ty)); k += 1
oil_ring(*P0, 6, 6, 14); oil_ring(*P1, 6, 6, 14)
oil_ring(CX, CY, 8, 38, 44)        # an der Ringstrasse (Stadtmitte)
for sx_, sy_ in ((-70, -70), (70, -70), (-70, 70), (60, 80), (0, -95), (0, 95), (-95, 0), (125, 10), (-120, 40), (40, 120)):
    oil_ring(CX + sx_, CY + sy_, 2, 2, 8)

# Spielerflaechen frei raeumen (keine Objekte auf den Startkacheln)
def clear_around(px, py, r):
    global feats
    keep = []
    for f in feats:
        if math.hypot(f[1] - px, f[2] - py) < r: occ[f[2]:f[2] + f[4], f[1]:f[1] + f[3]] = False
        else: keep.append(f)
    feats = keep
clear_around(*P0, 9); clear_around(*P1, 11)
oils = [o for o in oils]

# ---- Dateien schreiben
os.makedirs(OUTDIR, exist_ok=True)
D = os.path.join(OUTDIR, "city"); shutil.rmtree(D, ignore_errors=True)
MAPDIR = os.path.join(D, "multiplay", "maps", "2c-Stadt")
os.makedirs(MAPDIR, exist_ok=True)
with open(os.path.join(MAPDIR, "game.map"), "wb") as f:
    f.write(b"map " + struct.pack("<III", 10, W, H))
    for y in range(H):
        for x in range(W):
            f.write(struct.pack("<HB", int(tex[y, x]) & 0xFFFF, int(np.clip(hgt[y, x], 0, 255))))
    f.write(struct.pack("<II", 1, 0))   # keine Tore
shutil.copyfile(TTP, os.path.join(MAPDIR, "ttypes.ttp"))
fj = {}
for i, (nm, tx, ty, w, b, rot) in enumerate(feats):
    wx = (tx + w / 2.0) * 128; wy = (ty + b / 2.0) * 128
    fj["feature_%04d" % (i + 1)] = {"name": nm, "position": [int(wx), int(wy), 0], "rotation": [int(rot), 0, 0]}
n0 = len(fj)
for j, (tx, ty) in enumerate(oils):
    fj["feature_%04d" % (n0 + j + 1)] = {"name": "OilResource", "position": [tx * 128 + 64, ty * 128 + 64, 0], "rotation": [0, 0, 0]}
json.dump(fj, open(os.path.join(MAPDIR, "feature.json"), "w"), indent=1)
dj = {}; did = 1
for p, (px, py) in enumerate((P0, P1)):
    for k in range(4):
        ox = (k % 2) * 2 - 1; oy = (k // 2) * 2 - 1
        dj["droid_%04d" % did] = {"id": did, "position": [int((px + ox * 1.5) * 128 + 64), int((py + oy * 1.5) * 128 + 64), 0], "rotation": [0, 0, 0], "startpos": p, "template": "ConstructorDroid"}
        did += 1
json.dump(dj, open(os.path.join(MAPDIR, "droid.json"), "w"), indent=1)
json.dump({}, open(os.path.join(MAPDIR, "struct.json"), "w"))
with open(os.path.join(D, "multiplay", "maps", "2c-Stadt.gam"), "wb") as f:
    f.write(b"game" + struct.pack("<II", 8, 0) + struct.pack("<I", 2) + struct.pack("<IIII", 0, 0, W, H) + b"\0" * 20)
open(os.path.join(D, "Sk-Stadt-T1.addon.lev"), "w", newline="\r\n").write(
    '// Stadtkarte (Combot/Scavenger-Mod)\nlevel   Sk-Stadt-T1\nplayers 2\ntype    14\ndataset MULTI_CAM_1\ngame    "multiplay/maps/2c-Stadt.gam"\n')

zp = os.path.join(OUTDIR, "2c-Stadt.wz")
with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _d, files in os.walk(D):
        for fn in sorted(files):
            p = os.path.join(root, fn); z.write(p, os.path.relpath(p, D))
print("Karte %dx%d, Objekte %d, Oelquellen %d, Paket %s (%d KB)" % (W, H, len(feats), len(oils), zp, os.path.getsize(zp) // 1024))

# ---- Vorschau (Draufsicht): Kachelfarben + Objekte
from PIL import Image
prev = Image.new("RGB", (W * 3, H * 3))
pix = prev.load()
AVG = {}
def tile_color(tid):
    if tid not in AVG:
        p = "/tmp/claude-0/cmp/base/texpages/tertilesc1hw-128/tile-%02d.png" % tid
        AVG[tid] = tuple(int(c) for c in np.asarray(Image.open(p).convert("RGB").resize((8, 8))).reshape(-1, 3).mean(0))
    return AVG[tid]
for y in range(H):
    for x in range(W):
        c = tile_color(int(tex[y, x]) & 0x1FF)
        for j in range(3):
            for i in range(3): pix[x * 3 + i, y * 3 + j] = c
from PIL import ImageDraw
dr = ImageDraw.Draw(prev)
for nm, tx, ty, w, b, rot in feats:
    col = (70, 70, 80) if nm.startswith(("building", "Water", "bigcool")) else ((120, 60, 50) if nm.startswith("Wrecked") else (200, 120, 40))
    if w >= 2 or nm.startswith(("Landfill", "Old", "Barb")): dr.rectangle((tx * 3, ty * 3, (tx + w) * 3 - 1, (ty + b) * 3 - 1), fill=col)
    else: dr.point((tx * 3 + 1, ty * 3 + 1), fill=(240, 240, 200))
for tx, ty in oils: dr.ellipse((tx * 3 - 2, ty * 3 - 2, tx * 3 + 4, ty * 3 + 4), fill=(0, 0, 0), outline=(255, 255, 0))
for (px, py), c in ((P0, (0, 255, 0)), (P1, (255, 0, 0))): dr.ellipse((px * 3 - 6, py * 3 - 6, px * 3 + 6, py * 3 + 6), outline=c, width=2)
prev.save(os.path.join(OUTDIR, "stadt_vorschau.png"))
