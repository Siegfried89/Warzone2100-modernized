"""Stadtkarte 'Stadt' v2 (2 Spieler, 256x256 Kacheln = groesste Kartengroesse der Engine).
Erzeugt ein klassisches Karten-Paket (2c-Stadt.wz) fuer den Ordner ...\\Warzone 2100 master\\maps\\.
Aufbau (Wuensche 12./13.11.):
  * Boden wie die Landeflaeche (Stein/Beton) mit viel hellem Wuestenstaub (Arizona-Kacheln der Alpha-Kampagne).
  * Strassen: Raster (normale 3 breit, jede dritte Allee 5 breit), Ring um die Mitte, zwei dicke Highways (3 Spuren je Richtung + Mittelstreifen),
    mehrere lange schraege Strassen, Fluss mit langen Bruecken.
  * Streifenfolge von der Strasse nach aussen: Gehweg (Beton) -> Wuestenstaub -> Buesche -> Gebaeude (Hochhaeuser, Wohnviertel).
  * Mitte: Stadtplatz aus Beton, ringsum Hochhaeuser, dort die Startbasis von Spieler 0 (Scav-Gebaeude); Spieler 1 in der Wuestenecke.
  * Dicht dekoriert: Autowracks an den Strassen, Buesche, Felsen, Ruinen; Oelquellen nur rund um die Stadt (nicht darin).
citymap.py [AUSGABEORDNER]   (schreibt <AUSGABE>/2c-Stadt.wz, den entpackten Ordner <AUSGABE>/city/ und stadt_vorschau.png)"""
import os, sys, json, struct, math, zipfile, shutil
import numpy as np
from scipy import ndimage as ndi
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import roadkit

OUTDIR = sys.argv[1] if len(sys.argv) > 1 else "citymap_out"
W = H = 256
CX = CY = 128
rng = np.random.RandomState(2100)
TTP = "/tmp/claude-0/cmp/base/wrf/cam1/cam1a/ttypes.ttp"   # Arizona-Kachel-Typentabelle (waehlt den Arizona-Kachelsatz)

# ---- Kachel-Nummern (Arizona, siehe Kachelblatt)
CONCRETE = (22, 77, 22, 77, 19)       # grauer Beton (Typ Strasse = befahrbar); 19 mit roter Erde am Rand
CONCRETE_EDGE = 21                    # Beton mit Erdkante
ASPHALT = 59                          # Asphalt mit gelber Mittellinie (waagrecht)
CROSSING = 57                         # Kreuzung
DUST = (12, 1, 4, 42)                 # helle Wuestenstaub-Kacheln
DUST_BRUSH = (0, 9, 10, 11)           # Staub mit Bueschen
RED = (20, 27, 29, 41, 43, 44, 48, 53, 54)   # rote Erde
BAKED = (2, 3, 6, 36)                 # rissige Erde
WATER = 17
ROT = lambda r: (r & 3) << 12         # Drehung 0..3
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
dens_n = vnoise(20, 9)        # Dichte der Hochhaeuser (niedrig = Platz/Brache)
resid_n = vnoise(15, 11)      # Wohnviertel-Zonen

PLAZA = 30              # Stadtplatz: Tschebyschew-Abstand <= 30 (Beton)
RING = (31, 35)         # Ringstrasse (5 breit: Gehweg, 3 Fahrbahnen, Gehweg)
CITY_R = 106            # Stadtrand
PITCH = 28              # Strassenraster
RIVER_W = 20

# ---- Gelaende-Hoehe (Wert * 2 = Weltmass), Stadt flach
H0 = 150
hgt = np.full((H, W), H0, dtype=np.int32)
desert = np.clip((dist - (CITY_R + 8)) / 30.0, 0, 1)
hgt = (hgt + ((vnoise(30, 5) - 0.5) * 34 + (vnoise(11, 6) - 0.5) * 10) * desert).astype(np.int32)

P1 = (222, 32)                # Spieler 1: Wuestenecke
flat1 = np.clip((np.hypot(xx - P1[0], yy - P1[1]) - 12) / 12.0, 0, 1)
hgt = (H0 + (hgt - H0) * flat1).astype(np.int32)

# ---- Fluss (N-S, schlaengelt sich)
river_c = CX + 68 + 10 * np.sin(np.arange(H) / 23.0)
rc = river_c[yy[:, 0]][:, None]
river = np.abs(xx - rc) <= RIVER_W / 2

# ---- Strassen
O = np.zeros((H, W), np.int8)          # 0 keine, 1 Asphalt waagrecht, 2 senkrecht, 3 Kreuzung
WALK = np.zeros((H, W), bool)          # Gehweg / Randstreifen (Beton)
MED = np.zeros((H, W), bool)           # Mittelstreifen (Beton)
MAJ = np.zeros((H, W), bool)           # Teil einer Hauptstrasse (breiter Staub-/Buschstreifen)
XING = np.zeros((H, W), bool)          # darf ueber den Fluss (Bruecke)
MINA = np.zeros((H, W), bool)          # schmale Strasse (fuer Kreuzungskachel)
HWY = np.zeros((H, W), bool)           # Highway (Fahrbahn + Mittelstreifen + Rand)
DIAG = np.zeros((H, W), bool)
CLASS = np.zeros((H, W), np.int8)      # fuer Auto-Dichte: 1 klein, 2 Allee, 3 Highway, 4 Ring, 5 Diagonale


def paint(asph, orient, walk, med=None, major=False, over_river=False, minor=False, cls=1, hwy=False, diag=False):
    global O
    if not over_river:
        asph = asph & ~river; walk = walk & ~river
        if med is not None: med = med & ~river
    a = asph
    if minor:
        cross = a & MINA & (O > 0) & (O != orient) & (O != 3)
        O[a & ~cross] = orient; O[cross] = 3
        MINA[a] = True
    else:
        O[a] = orient
        MINA[a] = False
    WALK[walk & ~a] = True; WALK[a] = False
    if med is not None:
        MED[med] = True; O[med] = 0; WALK[med] = False
    allm = a | walk | (med if med is not None else False)
    if major: MAJ[allm] = True
    else: MAJ[allm & ~(MAJ)] = False
    if over_river: XING[allm] = True
    CLASS[allm] = cls
    if hwy: HWY[allm] = True
    if diag: DIAG[allm] = True


in_city = (dist <= CITY_R) & (cheb > RING[1] + 1)

# Raster: senkrechte und waagrechte Strassen; jede zweite Allee (5 breit), Alleen duerfen ueber den Fluss
for k in range(-4, 4):
    off = int(round((k + 0.5) * PITCH))          # Strassen bei +-14, 42, 70, 98 (keine mittig: dort liegen die Highways)
    avenue = abs(off) in (42, 98)
    for horizontal in (True, False):
        c = (dy if horizontal else dx)
        along = (np.abs(dx) if horizontal else np.abs(dy))
        ext = in_city | (avenue & (dist <= CITY_R + 14) & (cheb > RING[1] + 1))
        if avenue:
            asph = ext & (np.abs(c - off) <= 1); walk = ext & (np.abs(c - off) == 2)
            paint(asph, 1 if horizontal else 2, walk, major=True, over_river=horizontal, cls=2)
        else:
            asph = ext & (c == off); walk = ext & (np.abs(c - off) == 1)
            paint(asph, 1 if horizontal else 2, walk, minor=True, cls=1)

# Ringstrasse um den Stadtplatz
ring = (cheb >= RING[0]) & (cheb <= RING[1])
r_walk = ring & ((cheb == RING[0]) | (cheb == RING[1]))
r_asph = ring & ~r_walk
horiz_ring = np.abs(dy) > np.abs(dx)
paint(r_asph & horiz_ring, 1, r_walk, major=True, cls=4)
paint(r_asph & ~horiz_ring, 2, np.zeros((H, W), bool), major=True, cls=4)

# Highways: 3 Fahrbahnen je Richtung + Mittelstreifen (9 breit mit Randstreifen), Ost-West und Nord-Sued bis zum Kartenrand
for horizontal in (True, False):
    c = dy if horizontal else dx
    arm = (cheb >= RING[0])
    asph = arm & (np.abs(c) >= 1) & (np.abs(c) <= 3)
    walk = arm & (np.abs(c) == 4)
    med = arm & (c == 0)
    paint(asph, 1 if horizontal else 2, walk, med=med, major=True, over_river=True, cls=3, hwy=True)

# Lange schraege Strassen (45 Grad) aus den Ecken des Rings, jede knickt einmal ab (runde Boegen). Eigene Kacheln aus roadkit.py.
RC = (CX - 32.5, CY - 32.5, CX + 33.5, CY + 33.5)       # Mittellinie des Rings: links/oben, rechts/unten
ROADS = [
    roadkit.Road([(RC[0], RC[1]), (RC[0] - 30, RC[1] - 30), (-6, RC[1] - 30)], nstrip=2, R=5),                       # NW: Diagonale, knickt nach Westen
    roadkit.Road([(RC[0], RC[3]), (RC[0] - 40, RC[3] + 40), (RC[0] - 40, 262)], nstrip=2, R=5),                      # SW: Diagonale, knickt nach Sueden
    roadkit.Road([(RC[2], RC[3]), (RC[2] + 55, RC[3] + 55), (262, RC[3] + 55)], nstrip=2, R=5),                      # SO: Diagonale ueber den Fluss, knickt nach Osten
    roadkit.Road([(RC[2], RC[1]), (RC[2] + 20, RC[1] - 20), (RC[2] + 20, -6)], nstrip=2, R=5),                       # NO: kurze Diagonale, knickt nach Norden
]
spec_tiles, spec_walk, spec_images, spec_radar = roadkit.build(ROADS, O, CX, CY, bounds=(2, 2, W - 2, H - 2))
SPEC = np.zeros((H, W), bool); SPEC_ID = np.zeros((H, W), np.int32); SPECW = np.zeros((H, W), bool)
for (tx, ty), no in spec_tiles.items():
    if cheb[ty, tx] > PLAZA: SPEC[ty, tx] = True; SPEC_ID[ty, tx] = no
for (tx, ty) in spec_walk:
    if cheb[ty, tx] > PLAZA and not SPEC[ty, tx] and O[ty, tx] == 0: SPECW[ty, tx] = True
WALK[SPECW] = True
for m_ in (SPEC, SPECW):
    CLASS[m_] = 5; DIAG[m_] = True; XING[m_] = True; MAJ[m_] = True
print("Strassenkacheln: %d neue Kacheln, %d Spezial-Felder, %d Gehweg-Felder" % (len(spec_images), int(SPEC.sum()), int(SPECW.sum())))

road_asph = (O > 0) | SPEC
roadwalk = road_asph | WALK | MED
deck = river & roadwalk & XING
# Flussbett tiefer, Daemme/Bruecken auf Strassenhoehe
hgt[river & ~deck] -= 14
hgt[(np.abs(xx - rc) <= RIVER_W / 2 + 1) & ~river] -= 5   # Uferboeschung

# ---- Streifen neben den Strassen: Staub, dann Buesche
def dfield(classes):
    m = roadwalk & np.isin(CLASS, classes)
    return ndi.distance_transform_edt(~m) if m.any() else np.full((H, W), 99.0)
d3 = dfield((3, 4))        # Highway, Ring: Staub 3, Buesche 3
d2 = dfield((2, 5))        # Alleen, Diagonalen: Staub 2, Buesche 2
d1 = dfield((1,))          # kleine Strassen: Staub 2, Buesche 1
plaza = cheb <= PLAZA
free_ground = ~roadwalk & ~river & ~plaza
dust_zone = free_ground & ((d3 <= 3) | (d2 <= 2) | (d1 <= 2))
bush_zone = free_ground & ~dust_zone & ((d3 <= 6) | (d2 <= 4) | (d1 <= 3))
build_ok = free_ground & (d3 > 6) & (d2 > 4) & (d1 > 3) & (dist <= CITY_R - 4) & (cheb > RING[1] + 5)

# ---- Boden-Kacheln
tex = np.zeros((H, W), dtype=np.int32)
for y in range(H):
    for x in range(W):
        n = dust_n[y, x]; d = dist[y, x]; v = var_n[y, x]
        r = int(rng.randint(0, 4)); fl = int(rng.randint(0, 4)) << 14
        if river[y, x] and not deck[y, x]:
            t = WATER
        elif SPEC[y, x]:
            t = int(SPEC_ID[y, x])
        elif road_asph[y, x]:
            o = int(O[y, x])
            if o == 3: t = CROSSING
            else:
                t = ASPHALT | (0 if o == 1 else ROT(1))
                if o == 1 and int(dy[y, x]) % 2 == 0: t |= YF
                if o == 2 and int(dx[y, x]) % 2 == 0: t |= XF
        elif WALK[y, x] or MED[y, x]:
            t = int(rng.choice(CONCRETE)) | fl
        elif plaza[y, x]:
            # Stadtplatz: reiner Beton (kein Staub), Platten abwechselnd
            t = (22 if (x // 4 + y // 4) % 2 == 0 else 77) | fl
            if rng.rand() < 0.04: t = 19 | fl
        elif dust_zone[y, x]:
            t = int(rng.choice(DUST)) | fl
        elif bush_zone[y, x]:
            t = int(rng.choice(DUST_BRUSH if rng.rand() < 0.6 else DUST)) | fl
        elif d <= CITY_R:
            t = int(rng.choice(CONCRETE)) | fl
            if n > 0.60: t = int(rng.choice(DUST)) | fl if rng.rand() < 0.8 else int(rng.choice(DUST_BRUSH)) | fl
            elif n > 0.55: t = CONCRETE_EDGE | ROT(r)
        else:
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
BLK_GEN = river | roadwalk | plaza
BLK_PLAZA = river | roadwalk | (cheb > PLAZA)
BLK_ROAD = river | ~roadwalk


def free(tx, ty, w, b, margin=0, blk=None):
    blk = BLK_GEN if blk is None else blk
    if tx - margin < 2 or ty - margin < 2 or tx + w + margin >= W - 2 or ty + b + margin >= H - 2: return False
    s = (slice(ty - margin, ty + b + margin), slice(tx - margin, tx + w + margin))
    if occ[s].any(): return False
    s0 = (slice(ty, ty + b), slice(tx, tx + w))
    return not blk[s0].any()


def rot_for(w, b):
    if w == 1 and b == 1: return int(rng.randint(0, 4)) * 90
    if w == b: return int(rng.randint(0, 4)) * 90
    return int(rng.randint(0, 2)) * 180


def put(name, tx, ty, w=1, b=1, rot=None, margin=0, blk=None):
    if not free(tx, ty, w, b, margin, blk): return False
    occ[ty:ty + b, tx:tx + w] = True
    feats.append((name, tx, ty, w, b, rot_for(w, b) if rot is None else rot)); return True


def rnd_tile(mask):
    ys_, xs_ = np.nonzero(mask)
    i = rng.randint(len(ys_)); return int(xs_[i]), int(ys_[i])


TOWERS = [("building1", 2, 2), ("building2", 2, 2), ("building3", 2, 2), ("building7", 2, 2), ("building8", 2, 2), ("building11", 2, 2),
          ("building10", 2, 1), ("building12", 2, 1), ("bigcooltowr", 2, 2), ("WaterBuilding", 2, 2), ("WaterBuilding2", 2, 2), ("WaterBuilding3", 2, 2)]
WRECKED = [("WreckedBuilding9", 2, 2), ("WreckedBuilding16", 2, 2), ("WreckedBuilding17", 2, 2)]
MIDS = [("Powlab", 2, 2), ("Nanolab", 2, 2), ("Advmaterialslab", 2, 2), ("Heavywepslab", 2, 2), ("Indirectlab", 2, 2), ("Aerodynamicslab", 2, 2),
        ("Laseropticslab", 2, 2), ("Rotarywepslab", 2, 2), ("OldFactory", 2, 2), ("BarbWarehouse1", 2, 1), ("BarbWarehouse2", 2, 1), ("BarbTechRuin", 2, 1)]
HOUSES = [("LogCabin1", 1, 1), ("LogCabin2", 1, 1), ("LogCabin3", 1, 1), ("LogCabin4", 1, 1), ("LogCabin5", 1, 1), ("BarbHUT", 1, 1), ("BarbWarehouse3", 1, 1)]
SPECIAL = [("WaterTower", 1, 1), ("Crane", 1, 1), ("AirTrafficControl", 1, 1), ("Pylon", 1, 1), ("OilTower", 1, 1)]
RUINS = ["Ruin1", "Ruin3", "Ruin4", "Ruin5", "Ruin6", "Ruin7", "Ruin8", "Ruin9", "Ruin10", "miruin2", "WallSmashed", "WallCornerSmashed"]
BUSHES = ["arizonabush1", "arizonabush2", "arizonabush3", "arizonabush4"]
TREES = ["arizonatree1", "arizonatree2", "arizonatree3", "arizonatree4", "arizonatree5", "arizonatree6"]
ROCKS = ["Boulder1", "Boulder2", "Boulder3"]
JUNK = ["Wreck0", "Wreck1", "Wreck2", "Wreck3", "Wreck4", "Wreck5", "OilDrum5", "OilDrum10", "Pipe", "Pipe1", "Pipe2A", "Crate"]
CARS_CLEAN = ["BlueCar", "Chevy", "Pickup", "WreckedSuzukiJeep", "WreckedVertCampVan", "hoverwreck"]
CARS_BURNT = ["ModWreckS0", "ModWreckS1", "ModWreckS2", "ModWreckS3", "ModWreckS4", "ModWreckM0", "ModWreckM1", "ModWreckM2", "ModWreckM3",
              "ModWreckL0", "ModWreckL1", "ModWreckL2", "ModWreckB_BusBody", "ModWreckB_ScavIcevanBody", "ModWreckB_ScavCamperBody",
              "ModWreckB_ScavTruckBody", "ModWreckB_FireBody", "ModWreckB_B2JeepBody", "ModWreckB_B3body-sml-buggy01", "ModWreckB_Body16TRK-Scav"]
ALLCARS = CARS_CLEAN + CARS_BURNT + CARS_BURNT[:9]


def pick(lst): return lst[rng.randint(len(lst))]


# 1) Stadtplatz in der Mitte: Hochhaeuser als Kulisse ringsum (Luecken an den Einfahrten), innen frei fuer die Startbasis
RIM = 22
for t in range(-RIM, RIM + 1, 6):
    for side in range(4):
        if side == 0: gx, gy = CX + t, CY - RIM
        elif side == 1: gx, gy = CX + t, CY + RIM
        elif side == 2: gx, gy = CX - RIM, CY + t
        else: gx, gy = CX + RIM, CY + t
        if abs(t) <= 6 and True: continue            # Einfahrten der Highways (N, S, W, O)
        if rng.rand() < 0.15: continue
        nm, w, b = pick(TOWERS)
        put(nm, gx - 1 + int(rng.randint(-1, 2)), gy - 1 + int(rng.randint(-1, 2)), w, b, margin=1, blk=BLK_PLAZA)
# zweite, lockerere Reihe weiter aussen (Plattenrand)
for t in range(-27, 28, 9):
    for side in range(4):
        gx, gy = ((CX + t, CY - 27), (CX + t, CY + 27), (CX - 27, CY + t), (CX + 27, CY + t))[side]
        if abs(t) <= 8: continue
        if rng.rand() < 0.35: continue
        nm, w, b = pick(TOWERS + MIDS[:6])
        put(nm, gx - 1, gy - 1, w, b, margin=1, blk=BLK_PLAZA)
# Platzmobiliar: Tonnen, Kisten, Barrikaden, einzelne Wracks (Mitte bleibt frei)
for _ in range(900):
    tx = CX + int(rng.randint(-26, 27)); ty = CY + int(rng.randint(-26, 27))
    c = max(abs(tx - CX), abs(ty - CY))
    if c <= 13 or c > 27: continue
    if abs(tx - CX) <= 4 or abs(ty - CY) <= 4: continue    # Einfahrten frei
    r = rng.rand()
    nm = pick(["OilDrum5", "OilDrum10", "Crate", "barrier", "barrier"]) if r < 0.6 else (pick(ALLCARS) if r < 0.85 else pick(["Wreck1", "Wreck2", "Wreck3", "Pipe1", "Boulder1"]))
    put(nm, tx, ty, 1, 1, blk=BLK_PLAZA)

# 2) Wohnviertel: viele kleine Haeuser in Zonen, nach aussen haeufiger
res_zone = build_ok & (resid_n > 0.50) & (dist > 44)
ys_, xs_ = np.nonzero(res_zone)
n_house = 0
for i in rng.permutation(len(ys_)):
    ty, tx = int(ys_[i]), int(xs_[i])
    if rng.rand() > 0.42: continue
    nm, w, b = pick(HOUSES)
    if put(nm, tx, ty, w, b, margin=1): n_house += 1
# 3) Hochhaeuser in der Stadt: Dichte nach Rauschen, nach aussen weniger und mehr Ruinen
target = 0
tries = 0
n_tower = 0
while tries < 90000 and n_tower < 520:
    tries += 1
    tx, ty = rnd_tile(build_ok)
    d = dist[ty, tx]
    outer = d / CITY_R
    if rng.rand() > 0.25 + 0.9 * dens_n[ty, tx] - 0.45 * max(0, outer - 0.6): continue
    nm, w, b = (pick(WRECKED) if rng.rand() < 0.05 + 0.30 * max(0, outer - 0.55) else pick(TOWERS))
    if put(nm, tx, ty, w, b, margin=1 if rng.rand() < 0.65 else 2): n_tower += 1
n_mid = 0
tries = 0
while tries < 40000 and n_mid < 200:
    tries += 1
    tx, ty = rnd_tile(build_ok)
    nm, w, b = pick(MIDS if rng.rand() < 0.8 else SPECIAL)
    if put(nm, tx, ty, w, b, margin=1): n_mid += 1

# restliche Bauflaeche: Ruinen, Haeuser vereinzelt, Mauern
ys_, xs_ = np.nonzero(build_ok)
for i in rng.permutation(len(ys_))[: int(len(ys_) * 0.07)]:
    ty, tx = int(ys_[i]), int(xs_[i])
    if rng.rand() < 0.5: put(pick(RUINS), tx, ty, 1, 1, margin=0)
    else: put(pick(HOUSES)[0], tx, ty, 1, 1, margin=1)

# 4) Strassenmoebel: Wracks/Autos auf den Gehwegen, selten auf den Fahrbahnen grosser Strassen, Leitplanken im Mittelstreifen
def car_rot(ty, tx, jitter=18):
    o = int(O[ty, tx])
    if o == 0:
        # Gehweg: Richtung des naechsten Asphalts nehmen
        sl = O[max(0, ty - 2): ty + 3, max(0, tx - 2): tx + 3]
        vals = sl[sl > 0]
        o = int(np.bincount(vals).argmax()) if len(vals) else 1
    base = 0 if o in (1, 3) else 90
    if DIAG[ty, tx]: return int(rng.randint(0, 360))
    return (base + (180 if rng.rand() < 0.5 else 0) + int(rng.randint(-jitter, jitter + 1))) % 360


p_walk = {1: 0.07, 2: 0.07, 3: 0.04, 4: 0.06, 5: 0.07}
ys_, xs_ = np.nonzero(WALK)
for i in rng.permutation(len(ys_)):
    ty, tx = int(ys_[i]), int(xs_[i])
    if river[ty, tx] or plaza[ty, tx]: continue
    if rng.rand() < p_walk.get(int(CLASS[ty, tx]), 0.1):
        if occ[ty - 1:ty + 2, tx - 1:tx + 2].sum() > 1: continue
        occ[ty, tx] = True; feats.append((pick(ALLCARS), tx, ty, 1, 1, car_rot(ty, tx)))
# Fahrbahnen: nur Strassen mit mehreren Spuren (Highway, Ring, Allee, Diagonale); kleine Strassen bleiben frei fahrbar
ys_, xs_ = np.nonzero(road_asph & np.isin(CLASS, (2, 3, 4, 5)) & ~river & ~plaza)
for i in rng.permutation(len(ys_)):
    ty, tx = int(ys_[i]), int(xs_[i])
    if rng.rand() > 0.022: continue
    if occ[ty - 2:ty + 3, tx - 2:tx + 3].any(): continue
    occ[ty, tx] = True; feats.append((pick(ALLCARS), tx, ty, 1, 1, car_rot(ty, tx, 50)))
# Unfallstellen auf den Highways: 3 bis 5 Wracks auf engem Raum
for _ in range(16):
    for _try in range(40):
        tx, ty = rnd_tile(HWY & road_asph & ~river)
        if cheb[ty, tx] < 38: continue
        ok = 0
        for _k in range(int(rng.randint(3, 6))):
            ax = tx + int(rng.randint(-3, 4)); ay = ty + int(rng.randint(-3, 4))
            if 2 <= ax < W - 2 and 2 <= ay < H - 2 and road_asph[ay, ax] and not river[ay, ax] and not occ[ay - 1:ay + 2, ax - 1:ax + 2].any():
                occ[ay, ax] = True; feats.append((pick(ALLCARS), ax, ay, 1, 1, int(rng.randint(0, 360)))); ok += 1
        if ok: break
ys_, xs_ = np.nonzero(MED & ~river)
for i in rng.permutation(len(ys_)):
    ty, tx = int(ys_[i]), int(xs_[i])
    if (tx + ty) % 2 == 0 and rng.rand() < 0.75 and not occ[ty, tx]:
        occ[ty, tx] = True; feats.append(("barrier", tx, ty, 1, 1, 90 if dx[ty, tx] == 0 else 0))

# 5) Buesche im Buschstreifen, Felsen/Buesche/Ruinen dicht ueberall dazwischen
ys_, xs_ = np.nonzero(bush_zone & ~river)
for i in rng.permutation(len(ys_)):
    ty, tx = int(ys_[i]), int(xs_[i])
    r = rng.rand()
    if r < 0.40: put(pick(BUSHES), tx, ty, 1, 1)
    elif r < 0.44: put(pick(TREES), tx, ty, 1, 1)
    elif r < 0.47: put(pick(ROCKS), tx, ty, 1, 1)
ys_, xs_ = np.nonzero(dust_zone & ~river)
for i in rng.permutation(len(ys_)):
    ty, tx = int(ys_[i]), int(xs_[i])
    r = rng.rand()
    if r < 0.05: put(pick(ROCKS), tx, ty, 1, 1)
    elif r < 0.09: put(pick(BUSHES), tx, ty, 1, 1)
    elif r < 0.105: put(pick(JUNK), tx, ty, 1, 1)
# Innenflaechen der Stadt: verstreut Buesche, Felsen, Schrott, Autos
city_free = free_ground & (dist <= CITY_R + 4)
ys_, xs_ = np.nonzero(city_free)
for i in rng.permutation(len(ys_)):
    ty, tx = int(ys_[i]), int(xs_[i])
    r = rng.rand()
    if r < 0.045: put(pick(BUSHES), tx, ty, 1, 1)
    elif r < 0.065: put(pick(ROCKS), tx, ty, 1, 1)
    elif r < 0.077: put(pick(JUNK), tx, ty, 1, 1)
    elif r < 0.085: put(pick(ALLCARS), tx, ty, 1, 1)
    elif r < 0.090: put(pick(RUINS), tx, ty, 1, 1)
    elif r < 0.095: put(pick(TREES), tx, ty, 1, 1)

# 6) Wueste: Felsen, Buesche, Baeume, Ruinen, Wracks, einzelne Bauten
desert_free = ~roadwalk & ~river & (dist > CITY_R + 4)
ys_, xs_ = np.nonzero(desert_free)
for i in rng.permutation(len(ys_)):
    ty, tx = int(ys_[i]), int(xs_[i])
    r = rng.rand(); dn = dust_n[ty, tx]
    if r < 0.035 + 0.04 * (1 - dn): put(pick(ROCKS), tx, ty, 1, 1)
    elif r < 0.075: put(pick(BUSHES), tx, ty, 1, 1)
    elif r < 0.088: put(pick(TREES), tx, ty, 1, 1)
    elif r < 0.094: put(pick(RUINS), tx, ty, 1, 1)
    elif r < 0.099: put(pick(["Wreck0", "Wreck4", "Wreck5"] + CARS_CLEAN), tx, ty, 1, 1)
for _ in range(60):
    tx = int(rng.randint(3, W - 4)); ty = int(rng.randint(3, H - 4))
    if dist[ty, tx] < CITY_R + 12: continue
    nm, w, b = [("WaterTower", 1, 1), ("Pylon", 1, 1), ("BarbWarehouse1", 2, 1), ("BarbHUT", 1, 1), ("OldFactory", 2, 2), ("LogCabin1", 1, 1), ("WreckedBridge", 1, 2), ("Crane", 1, 1)][rng.randint(8)]
    put(nm, tx, ty, w, b, margin=1)

# ---- Startpunkte, Oelquellen
P0 = (CX, CY)                 # Spieler 0: Stadtplatz in der Mitte
oils = []
def oil_group(px, py, n, spread=3):
    k = 0; tries = 0
    while k < n and tries < 400:
        tries += 1
        tx = int(px + rng.randint(-spread, spread + 1)); ty = int(py + rng.randint(-spread, spread + 1))
        if 3 < tx < W - 4 and 3 < ty < H - 4 and not river[ty, tx] and not roadwalk[ty, tx] and not occ[ty - 1:ty + 2, tx - 1:tx + 2].any():
            occ[ty, tx] = True; oils.append((tx, ty)); k += 1
# rund um die Stadt (ausserhalb des Stadtrands), nicht darin
N_GROUPS = 16
for g in range(N_GROUPS):
    a = 2 * math.pi * (g + 0.5) / N_GROUPS + 0.12 * math.sin(g * 2.3)
    for rr in (CITY_R + 10, CITY_R + 24):
        if rr == CITY_R + 24 and g % 2 == 0: continue
        px = CX + rr * math.cos(a); py = CY + rr * math.sin(a)
        if 8 < px < W - 8 and 8 < py < H - 8:
            oil_group(px, py, 3 if rr == CITY_R + 10 else 2)
oil_group(P1[0], P1[1] + 12, 4, 4); oil_group(P1[0] - 12, P1[1], 3, 4)     # Wuestenecke

# Spielerflaechen frei raeumen (keine Objekte auf den Startkacheln und im Basisbereich)
def clear_around(px, py, rx, ry):
    global feats
    keep = []
    for f in feats:
        fx, fy = f[1] + f[3] / 2.0, f[2] + f[4] / 2.0
        if abs(fx - px) <= rx and abs(fy - py) <= ry: occ[f[2]:f[2] + f[4], f[1]:f[1] + f[3]] = False
        else: keep.append(f)
    feats = keep
clear_around(*P0, 13, 13); clear_around(*P1, 11, 11)

# ---- Startbasen (Scav-Gebaeude), Anzahl/Reihenfolge wie in Standardkarten
structs = []
def S(name, tx, ty, w=1, b=1, rot=0, player=0):
    structs.append({"name": name, "position": [int((tx + w / 2.0) * 128), int((ty + b / 2.0) * 128), 0], "rotation": [rot, 0, 0], "startpos": player, "modules": 0})
def base(px, py, player, big=True):
    S("A0CommandCentre", px - 4, py - 4, 1, 1, 0, player)
    S("A0PowerGenerator", px - 1, py - 4, 1, 1, 0, player)
    S("A0ResearchFacility", px - 4, py - 1, 1, 1, 0, player)
    S("A0LightFactory", px + 2, py - 4, 2, 1, 0, player)
    S("A0CyborgFactory", px + 2, py - 1, 2, 1, 0, player)
    S("A0RepairCentre3", px - 1, py + 2, 1, 1, 0, player)
    # Verteidigung am Rand der Basis
    rim = 9 if big else 7
    for a in (45, 135, 225, 315):
        gx = int(px + rim * math.cos(math.radians(a))); gy = int(py + rim * math.sin(math.radians(a)))
        S("A0BaBaGunTower", gx, gy, 1, 1, 0, player)
    for gx, gy in ((px - rim, py), (px + rim, py), (px, py - rim), (px, py + rim)):
        S("A0BaBaBunker", gx, gy, 1, 1, 0, player)
base(*P0, 0, True)
base(*P1, 1, False)

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
tt = open(TTP, "rb").read()
n_t = struct.unpack("<I", tt[8:12])[0]
vals = list(struct.unpack("<%dH" % n_t, tt[12:12 + 2 * n_t]))
ROADTYPE = vals[59]
n_new = roadkit.FIRST + len(spec_images)
vals += [0] * max(0, n_new - len(vals))
for i in range(len(spec_images)): vals[roadkit.FIRST + i] = ROADTYPE
open(os.path.join(MAPDIR, "ttypes.ttp"), "wb").write(tt[:8] + struct.pack("<I", len(vals)) + struct.pack("<%dH" % len(vals), *vals))
fj = {}
for i, (nm, tx, ty, w, b, rot) in enumerate(feats):
    wx = (tx + w / 2.0) * 128; wy = (ty + b / 2.0) * 128
    fj["feature_%04d" % (i + 1)] = {"name": nm, "position": [int(wx), int(wy), 0], "rotation": [int(rot), 0, 0]}
n0 = len(fj)
for j, (tx, ty) in enumerate(oils):
    fj["feature_%04d" % (n0 + j + 1)] = {"name": "OilResource", "position": [tx * 128 + 64, ty * 128 + 64, 0], "rotation": [0, 0, 0]}
json.dump(fj, open(os.path.join(MAPDIR, "feature.json"), "w"))
dj = {}; did = 1
for p, (px, py) in enumerate((P0, P1)):
    for k in range(4):
        ox = (k % 2) * 2 - 1; oy = (k // 2) * 2 - 1
        dj["droid_%04d" % did] = {"id": did, "position": [int((px + ox * 1.5) * 128 + 64), int((py + 5 + oy * 1.5) * 128 + 64), 0], "rotation": [0, 0, 0], "startpos": p, "template": "ConstructorDroid"}
        did += 1
json.dump(dj, open(os.path.join(MAPDIR, "droid.json"), "w"), indent=1)
sj = {"structure_%04d" % (i + 1): s for i, s in enumerate(structs)}
json.dump(sj, open(os.path.join(MAPDIR, "struct.json"), "w"), indent=1)
with open(os.path.join(D, "multiplay", "maps", "2c-Stadt.gam"), "wb") as f:
    f.write(b"game" + struct.pack("<II", 8, 0) + struct.pack("<I", 2) + struct.pack("<IIII", 0, 0, W, H) + b"\0" * 20)
open(os.path.join(D, "Sk-Stadt-T1.addon.lev"), "w", newline="\r\n").write(
    '// Stadtkarte (Combot/Scavenger-Mod)\nlevel   Sk-Stadt-T1\nplayers 2\ntype    14\ndataset MULTI_CAM_1\ngame    "multiplay/maps/2c-Stadt.gam"\n')

np.save(os.path.join(OUTDIR, "tex.npy"), tex)
zp = os.path.join(OUTDIR, "2c-Stadt.wz")
with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _d, files in os.walk(D):
        for fn in sorted(files):
            p = os.path.join(root, fn); z.write(p, os.path.relpath(p, D))
print("Bauflaeche %d Kacheln" % int(build_ok.sum())); print("Karte %dx%d, Objekte %d (Hochhaeuser %d, Mittelbauten %d, Haeuser %d), Oelquellen %d, Gebaeude %d, Paket %s (%d KB)" % (
    W, H, len(feats), n_tower, n_mid, n_house, len(oils), len(structs), zp, os.path.getsize(zp) // 1024))

# ---- Vorschau (Draufsicht): Kachelfarben + Objekte
from PIL import Image, ImageDraw
SC = 4
prev = Image.new("RGB", (W * SC, H * SC))
pix = prev.load()
AVG = {}
def tile_color(tid):
    if tid >= roadkit.FIRST:
        im = spec_images[tid - roadkit.FIRST].astype(float); a = im[..., 3:4] / 255.0
        return tuple(int(c) for c in (im[..., :3] * a + 120 * (1 - a)).reshape(-1, 3).mean(0))
    if tid not in AVG:
        p = "/tmp/claude-0/cmp/base/texpages/tertilesc1hw-128/tile-%02d.png" % tid
        AVG[tid] = tuple(int(c) for c in np.asarray(Image.open(p).convert("RGB").resize((8, 8))).reshape(-1, 3).mean(0))
    return AVG[tid]
for y in range(H):
    for x in range(W):
        c = tile_color(int(tex[y, x]) & 0x1FF)
        for j in range(SC):
            for i in range(SC): pix[x * SC + i, y * SC + j] = c
dr = ImageDraw.Draw(prev)
def col(nm):
    if nm.startswith(("building", "Water", "bigcool")) and not nm.startswith("WaterTower"): return (50, 50, 70)
    if nm.startswith(("Wrecked", "Ruin", "miruin", "Wall")): return (110, 70, 60)
    if nm.startswith(("arizonabush", "arizonatree", "Tree")): return (40, 120, 40)
    if nm.startswith(("Boulder", "Wreck", "Pipe")): return (150, 140, 120)
    if nm.startswith(("Log", "Barb", "Powlab", "Nano", "Adv", "Heavy", "Indirect", "Aero", "Laser", "Rotary", "Old")): return (190, 110, 60)
    return (230, 200, 60)
for nm, tx, ty, w, b, rot in feats:
    if w >= 2 or b >= 2: dr.rectangle((tx * SC, ty * SC, (tx + w) * SC - 1, (ty + b) * SC - 1), fill=col(nm))
    else: dr.rectangle((tx * SC + 1, ty * SC + 1, tx * SC + 2, ty * SC + 2), fill=col(nm))
for tx, ty in oils: dr.ellipse((tx * SC - 3, ty * SC - 3, tx * SC + 5, ty * SC + 5), fill=(0, 0, 0), outline=(255, 255, 0), width=2)
for s in structs:
    px, py = s["position"][0] / 128.0, s["position"][1] / 128.0
    dr.rectangle((px * SC - 3, py * SC - 3, px * SC + 3, py * SC + 3), fill=(0, 230, 0) if s["startpos"] == 0 else (230, 0, 0))
prev.save(os.path.join(OUTDIR, "stadt_vorschau.png"))
