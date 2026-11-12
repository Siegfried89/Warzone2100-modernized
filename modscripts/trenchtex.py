"""Textur-Atlas fuer Schuetzengraeben und Gruben (page-431-trench.png, 512 x 512):
  Sandsack  : Ausschnitt der Moerserstellung (page-7-barbarians-arizona, x 874..918, y 274..386), unveraendert uebernommen
  Dreck     : dunkler Matsch aus der Spiel-Bodenkachel 'a_mud' (rissige Erde), abgedunkelt und nahtlos gemacht (128 x 128)
  Tuch      : weissgraues, leicht schmutziges Leinen (128 x 128, nahtlos)
  Holz      : dunkle Holzmaserung fuer die duennen Staebe (32 x 128)
trenchtex.py OUT.png"""
import sys, os
import numpy as np
from PIL import Image, ImageFilter

SRC7 = "/tmp/claude-0/cmp/base/texpages/page-7-barbarians-arizona.png"
MUD_SRC = "/tmp/claude-0/cmp/base/texpages/tertilesc1hw-128/tile-06.png"
MUD2_SRC = "/tmp/claude-0/cmp/base/texpages/tertilesc1hw-128/tile-54.png"

# Lage im Atlas (Pixel)
SB_SRC = (874, 274, 918, 386)       # Quell-Ausschnitt (x0, y0, x1, y1), genau das Sandsack-Muster
SB_AT = (6, 6)                      # Ziel (links oben)
MUD_AT = (80, 6)
CLOTH_AT = (220, 6)
WOOD_AT = (360, 6)
SZ = 512


def seamless(a):
    """kachelbar machen: Bild mit sich selbst um die halbe Groesse verschoben ueberblenden"""
    h, w = a.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    wx = 0.5 - 0.5 * np.cos(2 * np.pi * (xx + 0.5) / w)
    wy = 0.5 - 0.5 * np.cos(2 * np.pi * (yy + 0.5) / h)
    m = (wx * wy)[..., None]
    s = np.roll(np.roll(a, h // 2, 0), w // 2, 1)
    return a * (1 - (1 - m)) + s * (1 - m) if False else a * m + s * (1 - m)


def mud():
    a = np.array(Image.open(MUD_SRC).convert("RGB").resize((128, 128), Image.LANCZOS)).astype(float)
    b = np.array(Image.open(MUD2_SRC).convert("RGB").resize((128, 128), Image.LANCZOS)).astype(float)
    a = 0.65 * a + 0.35 * b                                   # Kieselchen aus der Kachel daruntermischen
    lum = a.mean(2, keepdims=True)
    a = lum + (a - lum) * 0.75                                # etwas weniger Saettigung
    a = a * np.array([0.50, 0.42, 0.36])[None, None, :] * 1.35  # dunkles, feuchtes Braun
    rng = np.random.RandomState(7)
    n = rng.rand(16, 16)
    n = np.array(Image.fromarray((n * 255).astype(np.uint8)).resize((128, 128), Image.BICUBIC)).astype(float) / 255.0
    a = a * (0.80 + 0.40 * n)[..., None]                      # grosse feuchte/trockene Flecken
    a = seamless(a)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGB")


def cloth():
    rng = np.random.RandomState(11)
    yy, xx = np.mgrid[0:128, 0:128]
    base = np.array([208.0, 205.0, 193.0])
    weave = 0.5 + 0.5 * np.sin((xx + 0.5) * np.pi / 2) * np.sin((yy + 0.5) * np.pi / 2)   # Gewebe, Periode 4 px
    thread = 0.5 + 0.5 * np.sin(2 * np.pi * (xx * 16) / 128.0 + 1.3 * np.sin(2 * np.pi * yy / 128.0))
    n = np.array(Image.fromarray((rng.rand(8, 8) * 255).astype(np.uint8)).resize((128, 128), Image.BICUBIC)).astype(float) / 255.0
    fold = np.abs(np.sin(2 * np.pi * (xx * 3 + yy * 1) / 128.0))         # diagonale Falten
    img = base[None, None, :] * (0.86 + 0.10 * weave[..., None] + 0.05 * thread[..., None])
    img *= (0.88 + 0.16 * n)[..., None]
    img *= (0.93 + 0.07 * fold)[..., None]
    # Flecken (Staub, Brandspuren)
    for _ in range(9):
        cx, cy, r = rng.randint(0, 128), rng.randint(0, 128), rng.randint(5, 14)
        for dx in (-128, 0, 128):
            for dy in (-128, 0, 128):
                d = np.sqrt((xx - cx - dx) ** 2 + (yy - cy - dy) ** 2)
                img *= (1 - 0.18 * np.clip(1 - d / r, 0, 1))[..., None] * np.array([1.0, 0.99, 0.95])[None, None, :] if False else (1 - 0.16 * np.clip(1 - d / r, 0, 1))[..., None]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")


def wood():
    rng = np.random.RandomState(3)
    yy, xx = np.mgrid[0:128, 0:32]
    grain = 0.5 + 0.5 * np.sin(xx * 1.9 + 3.0 * np.sin(yy / 17.0) + rng.rand() * 6)
    n = np.array(Image.fromarray((rng.rand(16, 4) * 255).astype(np.uint8)).resize((32, 128), Image.BICUBIC)).astype(float) / 255.0
    base = np.array([112.0, 80.0, 50.0])
    img = base[None, None, :] * (0.70 + 0.30 * grain[..., None]) * (0.85 + 0.25 * n[..., None])
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")


def build():
    at = Image.new("RGBA", (SZ, SZ), (60, 48, 36, 255))
    sb = Image.open(SRC7).convert("RGBA").crop(SB_SRC)
    w, h = sb.size
    # Rand um den Sandsack-Ausschnitt mit den Randpixeln auffuellen (Mip-Map-Schutz)
    pad = 5
    big = Image.new("RGBA", (w + 2 * pad, h + 2 * pad))
    big.paste(sb, (pad, pad))
    arr = np.array(big)
    arr[:pad, pad:pad + w] = arr[pad:pad + 1, pad:pad + w]
    arr[pad + h:, pad:pad + w] = arr[pad + h - 1:pad + h, pad:pad + w]
    arr[:, :pad] = arr[:, pad:pad + 1]
    arr[:, pad + w:] = arr[:, pad + w - 1:pad + w]
    at.paste(Image.fromarray(arr), (SB_AT[0] - pad, SB_AT[1] - pad))
    for img, pos, ext in ((mud(), MUD_AT, 128), (cloth(), CLOTH_AT, 128), (wood(), WOOD_AT, 32)):
        iw, ih = img.size
        at.paste(img.convert("RGBA"), pos)
    return at


if __name__ == "__main__":
    build().save(sys.argv[1])
    print("Textur gespeichert:", sys.argv[1])
