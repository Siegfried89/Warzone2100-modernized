"""Schulterplatten mit 3 Spikes (Reihe quer von vorn sichtbar). plates.py: apply(f, plates, extra=None)
plates = [(seite -1/+1, (u,v) Platte, (u,v) Spikes)]; extra(ctx) darf weitere Geometrie anhaengen."""
import math
class Ctx:
    def __init__(s, f):
        s.f = f; s.L = open(f, encoding="latin-1").read().split("\n")
        L = s.L
        s.pi = next(i for i, l in enumerate(L) if l.startswith("POINTS")); s.n = int(L[s.pi].split()[1])
        s.gi = next(i for i, l in enumerate(L) if l.startswith("POLYGONS")); s.m = int(L[s.gi].split()[1])
        s.newp = []; s.newq = []
    def tri(s, a, b, c, uv):
        U, V = uv
        s.newq.append("\t200 3 %d %d %d %s %s %s %s %s %s" % (a, b, c, U, V, U, V, U, V))
        s.newq.append("\t200 3 %d %d %d %s %s %s %s %s %s" % (a, c, b, U, V, U, V, U, V))
    def cone(s, cx, cy, cz, r, h, uv, tx=0.0, tz=0.0, seg=8):
        b = s.n + len(s.newp)
        for k in range(seg):
            a = 2 * math.pi * k / seg; s.newp.append((cx + math.cos(a) * r, cy, cz + math.sin(a) * r))
        for k in range(seg):
            a = 2 * math.pi * k / seg; s.newp.append((cx + math.cos(a) * r * 0.55 + tx * h * 0.3, cy + h * 0.55, cz + math.sin(a) * r * 0.55 + tz * h * 0.3))
        top = s.n + len(s.newp); s.newp.append((cx + tx * h, cy + h, cz + tz * h))
        for k in range(seg):
            k2 = (k + 1) % seg
            s.tri(b + k, b + k2, b + seg + k2, uv); s.tri(b + k, b + seg + k2, b + seg + k, uv); s.tri(b + seg + k, b + seg + k2, top, uv)
    def shell(s, cx, cy, cz, rl, ry, rz, uv, ph0=-15, ph1=195, ns=9, nt=7):
        """Kappe: Laenge rl entlang x, Bogen ueber die Schulter in y/z (rund, an den Enden zugespitzt)"""
        base = s.n + len(s.newp)
        for j in range(nt):
            t = -1 + 2 * j / (nt - 1); sc = max(0.22, math.sqrt(max(0.0, 1 - t * t)))
            for i in range(ns):
                ph = math.radians(ph0 + (ph1 - ph0) * i / (ns - 1))
                s.newp.append((cx + t * rl, cy + math.sin(ph) * ry * (0.55 + 0.45 * sc), cz + math.cos(ph) * rz * sc))
        for j in range(nt - 1):
            for i in range(ns - 1):
                a, b_, c, d = base + j * ns + i, base + j * ns + i + 1, base + (j + 1) * ns + i, base + (j + 1) * ns + i + 1
                s.tri(a, b_, d, uv); s.tri(a, d, c, uv)
    def save(s):
        L = s.L; gi, pi, n, m = s.gi, s.pi, s.n, s.m
        end = next(i for i in range(gi + 1, len(L)) if not L[i].startswith("\t200") and not L[i].strip().startswith("200"))
        L[end:end] = s.newq; L[gi] = "POLYGONS %d" % (m + len(s.newq))
        L[pi + 1 + n:pi + 1 + n] = ["\t%.4f %.4f %.4f" % p for p in s.newp]; L[pi] = "POINTS %d" % (n + len(s.newp))
        open(s.f, "w", encoding="latin-1", newline="").write("\n".join(L))
def apply(f, plates, extra=None):
    c = Ctx(f)
    for side, uvs, uvc in plates:
        CX, CY, CZ = 3.5 * side, 15.4, -0.3
        c.shell(CX, CY, CZ, 1.9, 1.8, 1.9, uvs)
        for xo in (-1.15, 0.0, 1.15):
            s = math.sqrt(max(0.0, 1 - (xo / 2.0) ** 2)); top = CY + 1.9 * (0.55 + 0.45 * s)
            c.cone(CX + xo, top - 0.5, CZ, 0.7, 3.2, uvc, tx=0.1 * side * (1 if xo else 0.6))
    if extra: extra(c)
    c.save(); return len(c.newp), len(c.newq)
