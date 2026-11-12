"""Hall auf alle Schusssounds: je Waffen-Sound eine nachbearbeitete Kopie <name>_h<N>.ogg (Faltungshall mit synthetischer Raumantwort:
fruehe Reflexionen + abklingendes Rauschen, Hoehen gedaempft), in audio.json eingetragen, weaponWav der Waffen umgeleitet.
Voreinstellungen je Waffenart: 1 MG (kurz, leise), 2 Kanonen/Strahl (mittel), 3 Raketen/Moerser/Haubitzen (lang), 4 Schwer (sehr lang, 'episch').
Nicht bearbeitet: Flammenwerfer (Dauerton), Bomben.
hall.py OUT"""
import sys, os, json, subprocess, zlib
import numpy as np
from scipy.signal import fftconvolve, butter, lfilter

OUT = sys.argv[1]
SR = 44100
BASE_AUDIO = "/tmp/claude-0/cmp/base/audio"
PRESETS = {1: dict(rt=0.60, wet=0.20, pre=0.012, damp=5200), 2: dict(rt=1.10, wet=0.30, pre=0.018, damp=4200),
           3: dict(rt=1.60, wet=0.34, pre=0.024, damp=3600), 4: dict(rt=2.10, wet=0.42, pre=0.030, damp=3000)}
SKIP_SUB = {"FLAME", "BOMB"}
HEAVY_NAMES = {"CannonSuper", "MassDriver", "Superweapon2", "RailGun3Mk1", "Laser4-PlasmaCannon"}
HEAVY_WAV = ("lrgcan", "hwtzflgt")


def preset_for(k, v):
    sub = v.get("weaponSubClass"); wav = str(v.get("weaponWav", "")).lower()
    if sub in SKIP_SUB: return None
    if k in HEAVY_NAMES or any(h in wav for h in HEAVY_WAV) or sub == "HOWITZERS": return 4
    if sub in ("MORTARS", "ROCKET", "SLOW ROCKET", "MISSILE"): return 3
    if sub == "MACHINE GUN": return 1
    if k.startswith("MFA_Howitzer") or k.startswith("MFA_Flak"): return 4
    return 2


def find(name):
    for root in (os.path.join(OUT, "audio"), BASE_AUDIO):
        for dp, _dn, fn in os.walk(root):
            if name in fn: return os.path.join(dp, name)
    return None


def decode(path):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"], capture_output=True, check=True)
    return np.frombuffer(r.stdout, dtype="<f4").astype(np.float64)


def impulse(P, seed):
    rnd = np.random.RandomState(seed)
    n = int(SR * (P["rt"] * 1.15 + P["pre"]))
    t = np.arange(n) / SR
    ir = np.zeros(n)
    p0 = int(SR * P["pre"])
    noise = rnd.randn(n - p0)
    # Hoehen klingen schneller ab: Rauschen in drei Baendern mit verschiedenen Abklingzeiten
    lo = lfilter(*butter(2, 700 / (SR / 2)), noise)
    mid = lfilter(*butter(2, [700 / (SR / 2), P["damp"] / (SR / 2)], btype="band"), noise)
    tt = t[: n - p0]
    ir[p0:] = lo * np.exp(-6.9 * tt / (P["rt"] * 1.15)) * 1.0 + mid * np.exp(-6.9 * tt / (P["rt"] * 0.75)) * 0.8
    # fruehe Reflexionen
    for d, g in ((0.011, 0.55), (0.019, 0.42), (0.031, 0.34), (0.047, 0.27), (0.066, 0.2)):
        i = int(SR * (d + P["pre"] * 0.4))
        if i < n: ir[i] += g * (1 if rnd.rand() > 0.5 else -1) * np.abs(ir).max() * 0.9
    ir /= np.sqrt((ir ** 2).sum()) + 1e-9
    return ir


def process(x, P, seed):
    ir = impulse(P, seed)
    wet = fftconvolve(x, ir)
    n = len(wet)
    dry = np.zeros(n); dry[: len(x)] = x
    ref = np.sqrt((x ** 2).mean() + 1e-12) ; wref = np.sqrt((wet[: len(x)] ** 2).mean() + 1e-12)
    wet *= (ref / wref) * P["wet"] * 3.0
    y = dry + wet
    # Ende sanft ausblenden
    fade = int(SR * 0.25); y[-fade:] *= np.linspace(1, 0, fade)
    # Gesamtlautheit hoechstens 1,2 x Original (Hall soll nicht lauter machen), danach Spitzen begrenzen
    r = np.sqrt((y[: len(x)] ** 2).mean() + 1e-12) / ref
    if r > 1.2: y *= 1.2 / r
    pk = np.abs(y).max()
    if pk > 0.97: y *= 0.97 / pk
    return y.astype(np.float32)


def encode(y, dst):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "f32le", "-ar", str(SR), "-ac", "1", "-i", "-", "-c:a", "libvorbis", "-q:a", "4", dst],
                       input=y.astype("<f4").tobytes(), capture_output=True)
    if r.returncode != 0: raise RuntimeError(r.stderr.decode())


def main():
    wp = os.path.join(OUT, "stats", "weapons.json"); W = json.load(open(wp, encoding="utf-8"))
    ap = os.path.join(OUT, "audio", "audio.json"); A = json.load(open(ap, encoding="utf-8"))
    ad = os.path.join(OUT, "audio", "mf"); os.makedirs(ad, exist_ok=True)
    entries = {}
    for g, grp in A.items():
        if isinstance(grp, dict):
            for e in grp.get("data", []): entries.setdefault(e.get("fileName"), e)
    need = {}
    for k, v in W.items():
        if not isinstance(v, dict) or not v.get("weaponWav") or v["weaponWav"] in ("-1", ""): continue
        pr = preset_for(k, v)
        if pr is None: continue
        need.setdefault((v["weaponWav"], pr), []).append(k)
    # eine Datei, die von Waffen verschiedener Klasse genutzt wird: jede Klasse bekommt ihre Fassung
    made = {}; new = []
    for (name, pr), keys in sorted(need.items()):
        src = find(name)
        if not src: print("   Quelle fehlt:", name); continue
        stem = os.path.splitext(name)[0]; nn = "%s_h%d.ogg" % (stem, pr)
        x = decode(src)
        if len(x) < 100: continue
        y = process(x, PRESETS[pr], zlib.crc32(name.encode()) & 0xffff)
        encode(y, os.path.join(ad, nn))
        e = dict(entries.get(name, {"loop": False, "range": 1800, "volume": 100})); e["fileName"] = nn; e["z_comment"] = "Hall (Voreinstellung %d) auf %s" % (pr, name)
        new.append(e); made[(name, pr)] = nn
    A["ReverbSounds"] = {"_name": "Schusssounds mit Hall", "data": new}
    json.dump(A, open(ap, "w", encoding="utf-8"), indent=1)
    n = 0
    for (name, pr), keys in need.items():
        if (name, pr) not in made: continue
        for k in keys: W[k]["weaponWav"] = made[(name, pr)]; n += 1
    json.dump(W, open(wp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Hall: %d Sounddateien, %d Waffen umgeleitet" % (len(made), n))


main()
