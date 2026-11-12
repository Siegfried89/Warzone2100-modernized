"""MG-Sounds aus dem Spielordner 'Mayday conflict earth' (vom Spieler bereitgestellt, C:\\Mayday\\sound).
  MGDroid.wav -> Standard-MG Tier 0 (MG1Mk1, MG1-Tower, MG1-Pillbox)
  MGJeep.wav  -> MGs der Scavenger-Jeeps/Buggys/Trikes (einfache Ausfuehrung)
  MGHeli.wav  -> MGs der VTOLs (MG1/MG2/MG3-VTOL, MG1-VTOL-SCAVS)
Die Dateien werden nach audio/mf/mayday_*.ogg umgewandelt (44,1 kHz, Lautheit angeglichen), in audio.json eingetragen und in
weapons.json als weaponWav gesetzt. Danach bekommen sie in hall.py ihren Hall und werden in mfwrf.py in die Ladeliste eingetragen.
mayday.py OUT [QUELLORDNER]"""
import sys, os, json, subprocess, wave
import numpy as np
from scipy.signal import butter, sosfilt

OUT = sys.argv[1]
SRC = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "mayday_src")
SR = 44100
TARGET_RMS = 0.15

MAP = {
    "mayday_mg_droid.ogg": ("MGDroid.wav", ["MG1Mk1", "MG1-Tower", "MG1-Pillbox"], 80),
    "mayday_mg_jeep.ogg": ("MGJeep.wav", ["BJeepMG", "BabaJeepMG", "BuggyMG", "BabaBuggyMG", "bTrikeMG", "BabaTrikeMG"], 85),
    "mayday_mg_heli.ogg": ("MGHeli.wav", ["MG1-VTOL", "MG1-VTOL-SCAVS", "MG2-VTOL", "MG3-VTOL"], 85),
}


def load_u8(path):
    w = wave.open(path)
    assert w.getsampwidth() == 1 and w.getnchannels() == 1, path
    d = np.frombuffer(w.readframes(w.getnframes()), np.uint8).astype(np.float64)
    return (d - 128.0) / 128.0, w.getframerate()


def resample(x, sr):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "f32le", "-ar", str(sr), "-ac", "1", "-i", "-", "-ar", str(SR), "-f", "f32le", "-"],
                       input=x.astype("<f4").tobytes(), capture_output=True, check=True)
    return np.frombuffer(r.stdout, dtype="<f4").astype(np.float64)


def encode(y, dst):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "f32le", "-ar", str(SR), "-ac", "1", "-i", "-", "-c:a", "libvorbis", "-q:a", "5",
                        "-fflags", "+bitexact", "-flags:a", "+bitexact", dst], input=y.astype("<f4").tobytes(), capture_output=True)
    if r.returncode != 0: raise RuntimeError(r.stderr.decode())


def main():
    wp = os.path.join(OUT, "stats", "weapons.json"); W = json.load(open(wp, encoding="utf-8"))
    ap = os.path.join(OUT, "audio", "audio.json"); A = json.load(open(ap, encoding="utf-8"))
    ad = os.path.join(OUT, "audio", "mf"); os.makedirs(ad, exist_ok=True)
    new = []; n = 0
    for dst, (src, weapons, vol) in MAP.items():
        p = os.path.join(SRC, src)
        if not os.path.exists(p): print("   Quelle fehlt:", p); continue
        x, sr = load_u8(p)
        x = x - np.mean(x)                                     # Gleichanteil weg
        x = sosfilt(butter(2, 70 / (sr / 2), btype="high", output="sos"), x)   # Brummen unter 70 Hz
        y = resample(x, sr)
        # Stille am Ende abschneiden, kurzes Einblenden (Knacken), Ausblenden
        env = np.abs(y); idx = np.where(env > 0.01)[0]
        if len(idx): y = y[max(0, idx[0] - 200): idx[-1] + int(SR * 0.05)]
        y[: int(SR * 0.003)] *= np.linspace(0, 1, int(SR * 0.003))
        f = int(SR * 0.06); y[-f:] *= np.linspace(1, 0, f)
        rms = np.sqrt((y ** 2).mean() + 1e-12); y *= TARGET_RMS / rms
        pk = np.abs(y).max()
        if pk > 0.95: y *= 0.95 / pk
        encode(y, os.path.join(ad, dst))
        new.append({"fileName": dst, "loop": False, "range": 1800, "volume": vol, "z_comment": "Mayday conflict earth: %s" % src})
        for k in weapons:
            if k in W and isinstance(W[k], dict): W[k]["weaponWav"] = dst; n += 1
        print("  %s -> %s (%.2f s), %d Waffen" % (src, dst, len(y) / SR, len(weapons)))
    A["MaydaySounds"] = {"_name": "MG-Sounds aus Mayday conflict earth", "data": new}
    json.dump(A, open(ap, "w", encoding="utf-8"), indent=1)
    json.dump(W, open(wp, "w", encoding="utf-8"), indent="\t", sort_keys=True)
    print("Mayday: %d Sounds, %d Waffen umgeleitet" % (len(new), n))


main()
