"""Farb-Helfer fuer die gelben Punk-Varianten (Hosen blau -> gelb, Mohawk rot -> weiss)."""
import colorsys
from PIL import Image
TROUSER_RECT = (60, 0, 215, 95)
HAIR_RECT = (210, 72, 228, 92)
BUGGY_YELLOW = (248, 216, 24)    # knalliges Scav-Buggy-Gelb
NP_YELLOW = (255, 176, 53)       # Teamfarbe 2 (New Paradigm, Spieler 1) laut data/base/palette.txt
def to_yellow(im, target, white_hair=True, trousers=True, brown_hair=False):
    im = im.convert("RGBA").copy(); px = im.load(); ht, st, vt = colorsys.rgb_to_hsv(*(c / 255 for c in target))
    if trousers:
        x0, y0, x1, y1 = TROUSER_RECT
        for y in range(y0, min(y1, im.size[1])):
            for x in range(x0, min(x1, im.size[0])):
                p = px[x, y]; r, g, b = p[:3]
                h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
                if 0.50 <= h <= 0.86 and s > 0.15 and v > 0.12:
                    nv = min(1.0, v / 0.85 * vt); ns = min(1.0, st * (0.55 + 0.45 * min(1.0, s / 0.55)))
                    r2, g2, b2 = colorsys.hsv_to_rgb(ht, ns, nv); px[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255)) + tuple(p[3:])
    if trousers:
        # grauer Schritt-Keil unter dem Guertel (V-Form, Pixel x 90..158, y 241..255) -> Hosenstoff statt grauem Leder
        for y in range(241, min(256, im.size[1])):
            for x in range(90, min(158, im.size[0])):
                p = px[x, y]
                if p[3] < 40: continue
                h, s, v = colorsys.rgb_to_hsv(p[0] / 255, p[1] / 255, p[2] / 255)
                if v > 0.2 and (s < 0.4 or 0.5 <= h <= 0.86) and not (0.0 <= h <= 0.1 and 0.15 < s < 0.6):
                    nv = min(1.0, 0.45 + v * 0.75) * vt; r2, g2, b2 = colorsys.hsv_to_rgb(ht, min(1.0, st * 0.95), min(1.0, nv))
                    px[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255)) + tuple(p[3:])
    if brown_hair:
        x0, y0, x1, y1 = HAIR_RECT
        for y in range(y0, y1):
            for x in range(x0, x1):
                p = px[x, y]
                if p[3] > 200 and p[0] > 150 and p[1] < 130 and p[2] < 130:
                    k = 0.62 + 0.38 * (p[0] - 150) / 105.0; px[x, y] = (int(110 * k), int(70 * k), int(38 * k)) + (p[3],)
    if white_hair:
        x0, y0, x1, y1 = HAIR_RECT
        for y in range(y0, y1):
            for x in range(x0, x1):
                p = px[x, y]
                if p[3] > 200 and p[0] > 150 and p[1] < 130 and p[2] < 130:
                    k = 0.82 + 0.18 * (p[0] - 150) / 105.0; px[x, y] = (int(247 * k), int(247 * k), int(253 * k)) + (p[3],)
    return im
