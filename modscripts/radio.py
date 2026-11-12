"""radio.py OUT  -> OUT/audio/radio/atc_*.ogg (4 Stuecke Flugsicherungs-Funkgebrabbel), Eintrag in audio.json und wrf/audio.wrf.
Unverstaendlich mit Absicht: kuenstliche Silben (Formant-Synthese) mit zufaelliger Tonhoehe/Vokalfarbe, Zischlaute, Wortpausen,
dazu Funk-Klang (Bandbegrenzung 350-2800 Hz, Uebersteuerung, Rauschen) und Sendetasten-Klicks ("Squelch") am Anfang und Ende jeder Durchsage.
Zufallszahl fest -> jedes Mal dasselbe Ergebnis. Abgespielt von der Engine (modradio.cpp), wenn die Kamera nahe an einem Kommandozentrum
oder Tower-Gebaeude ist."""
import sys, os, json, subprocess
import numpy as np

OUT = sys.argv[1]
SR = 22050
FILES = ["atc_a.ogg", "atc_b.ogg", "atc_c.ogg", "atc_d.ogg"]

# Vokal-Tabelle (F1, F2, F3) in Hz
VOW = [(730, 1090, 2440), (660, 1720, 2410), (270, 2290, 3010), (570, 840, 2410), (300, 870, 2240), (520, 1190, 2390), (440, 1020, 2240), (400, 1900, 2550)]


def butter_filt(x, lo, hi):
    from scipy.signal import butter, lfilter
    lo = max(lo, 40.0); hi = min(hi, SR / 2.0 - 100)
    b, a = butter(2, [lo / (SR / 2.0), hi / (SR / 2.0)], btype="band"); return lfilter(b, a, x)


def highpass(x, fc):
    from scipy.signal import butter, lfilter
    b, a = butter(2, fc / (SR / 2.0), btype="high"); return lfilter(b, a, x)


def lowpass(x, fc):
    from scipy.signal import butter, lfilter
    b, a = butter(2, fc / (SR / 2.0)); return lfilter(b, a, x)


def syllable(rng, f0a, f0b, dur, fscale):
    """eine Silbe: Stimmquelle mit Tonhoehenverlauf, durch drei Formantfilter, mit Huellkurve"""
    n = int(dur * SR)
    f0 = np.linspace(f0a, f0b, n) * (1 + 0.012 * np.sin(2 * np.pi * 5.5 * np.arange(n) / SR + rng.rand() * 6))
    ph = np.cumsum(f0) / SR
    src = ((ph % 1.0) * 2 - 1)
    src = np.diff(src, prepend=0) * 0.5 + src * 0.5      # etwas hellere Quelle
    v = VOW[rng.randint(len(VOW))]
    y = np.zeros(n)
    for fm, w, bw in zip(v, (1.0, 0.75, 0.4), (90, 120, 160)):
        fc = fm * fscale * (0.97 + 0.06 * rng.rand())
        y += butter_filt(src, fc - bw, fc + bw) * w
    t = np.arange(n) / SR
    att = np.clip(t / 0.018, 0, 1); rel = np.clip((dur - t) / 0.045, 0, 1)
    return y * att * rel * (0.6 + 0.4 * rng.rand()) * 4.0


def consonant(rng, kind):
    """Zischlaut / Verschlusslaut"""
    if kind == 0:   # s
        d = 0.05 + 0.06 * rng.rand(); n = int(d * SR)
        y = highpass(rng.randn(n), 2800) * np.hanning(n) * 0.12
    elif kind == 1:  # t/k
        d = 0.018 + 0.01 * rng.rand(); n = int(d * SR)
        y = butter_filt(rng.randn(n), 1200, 4500) * np.exp(-np.arange(n) / (n * 0.3)) * 0.3
    else:           # f/h
        d = 0.04 + 0.04 * rng.rand(); n = int(d * SR)
        y = butter_filt(rng.randn(n), 900, 3500) * np.hanning(n) * 0.2
    return y


def transmission(rng, f0, fscale, nwords, syl_rate=5.0):
    """eine Durchsage aus nwords Woertern mit je 1-4 Silben"""
    parts = []
    base = f0
    for w in range(nwords):
        ns = rng.randint(1, 5)
        for s in range(ns):
            if rng.rand() < 0.45: parts.append(consonant(rng, rng.randint(3)))
            dur = (0.07 + 0.13 * rng.rand()) * (5.0 / syl_rate)
            a = base * (0.92 + 0.2 * rng.rand()); b = a * (0.88 + 0.22 * rng.rand())
            parts.append(syllable(rng, a, b, dur, fscale))
            base *= 0.995
        if rng.rand() < 0.3: parts.append(consonant(rng, 1))
        parts.append(np.zeros(int((0.10 + 0.22 * rng.rand()) * SR)))    # Wortpause
        if rng.rand() < 0.25: base = f0 * (0.95 + 0.15 * rng.rand())    # Satzmelodie neu ansetzen
    y = np.concatenate(parts)
    # Funk-Klang
    y = butter_filt(y, 350, 2800)
    y = np.tanh(3.2 * y / (np.abs(y).max() + 1e-9))
    y = butter_filt(y, 350, 2800)                                       # nach der Uebersteuerung erneut bandbegrenzen (Funk-Klang)
    y += 0.06 * butter_filt(rng.randn(len(y)), 400, 3000)               # Rauschen in der Durchsage
    return y / (np.abs(y).max() + 1e-9) * 0.8


def squelch(rng, tail=False):
    """Sendetaste: kurzer Klick plus Rauschstoss (Ende laenger = 'kschh')"""
    d = 0.11 if tail else 0.045; n = int(d * SR)
    y = butter_filt(rng.randn(n), 600, 5000) * np.exp(-np.arange(n) / (n * (0.35 if tail else 0.2)))
    y[:int(0.002 * SR)] += np.hanning(int(0.004 * SR))[:int(0.002 * SR)] * 1.5
    return y * 0.28


def clip(rng, plan):
    """plan: Liste (f0, formantfaktor, Woerter, Pause danach in s)"""
    parts = [np.zeros(int(0.15 * SR))]
    for f0, fs, nw, gap in plan:
        parts.append(squelch(rng)); parts.append(np.zeros(int(0.05 * SR)))
        parts.append(transmission(rng, f0, fs, nw))
        parts.append(squelch(rng, True)); parts.append(np.zeros(int(gap * SR)))
    y = np.concatenate(parts)
    n = len(y)
    stat = butter_filt(rng.randn(n), 500, 3800) * 0.035 * (0.6 + 0.4 * lowpass(rng.randn(n), 3) / (np.abs(lowpass(rng.randn(n), 3)).max() + 1e-9))   # leises Grundrauschen
    stat *= np.clip(np.minimum(np.arange(n), n - np.arange(n)) / (0.2 * SR), 0, 1)
    y = y + stat
    # sanftes Ein-/Ausblenden, damit nichts klickt
    fade = int(0.03 * SR); y[:fade] *= np.linspace(0, 1, fade); y[-fade:] *= np.linspace(1, 0, fade)
    return y


def encode(y, dst):
    y = y / (np.abs(y).max() + 1e-9) * 0.9
    r = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "f32le", "-ar", str(SR), "-ac", "1", "-i", "-", "-c:a", "libvorbis", "-q:a", "4", "-fflags", "+bitexact", "-flags:a", "+bitexact", dst],
                       input=y.astype("<f4").tobytes(), capture_output=True)
    if r.returncode != 0: raise RuntimeError(r.stderr.decode())


def main():
    rng = np.random.RandomState(2100)
    d = os.path.join(OUT, "audio", "radio"); os.makedirs(d, exist_ok=True)
    CT, PI = (118, 1.0), (152, 1.12)     # Tower-Stimme tiefer, Pilot hoeher/heller
    plans = [
        [(CT[0], CT[1], 5, 0.55), (PI[0], PI[1], 3, 0.0)],
        [(CT[0], CT[1], 8, 0.0)],
        [(PI[0], PI[1], 3, 0.45), (CT[0], CT[1], 4, 0.0)],
        [(CT[0], CT[1], 4, 0.6), (PI[0], PI[1], 4, 0.6), (CT[0], CT[1], 3, 0.0)],
    ]
    for fn, plan in zip(FILES, plans):
        y = clip(rng, plan); encode(y, os.path.join(d, fn))
        print("  %s %.1f s" % (fn, len(y) / SR))
    ap = os.path.join(OUT, "audio", "audio.json"); A = json.load(open(ap, encoding="utf-8"))
    A["RadioChatter"] = {"_name": "ATC radio chatter", "data": [
        {"fileName": f, "loop": False, "range": 1800, "volume": 55, "z_comment": "unverstaendliches Funkgebrabbel (Kommandozentrum / ATC-Turm)"} for f in FILES]}
    json.dump(A, open(ap, "w"), indent=1)
    wp = os.path.join(OUT, "wrf", "audio.wrf"); w = open(wp, "rb").read().decode("latin-1")
    if "audio/radio" not in w:
        marker = 'directory\t"audio"\r\nfile\t\tAUDIOCFG\t"audio.json"'
        assert w.count(marker) == 1, "AUDIOCFG-Block nicht gefunden"
        block = 'directory\t"audio/radio"\r\n' + "".join('file\t\tWAV\t"%s"\r\n' % f for f in FILES)
        open(wp, "wb").write(w.replace(marker, block + marker).encode("latin-1"))
    print("Funkgebrabbel erzeugt")


main()
