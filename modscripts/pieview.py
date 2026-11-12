"""Mini-Renderer fuer PIE3-Dateien mit Texturseite (wie die Engine UV/Textur liest).
pieview.py MODDIR PIE OUT.png [yaw pitch [size]]   -> rendert eine PIE einzeln
pieview.py --sheet MODDIR OUT.png PIE1 PIE2 ...    -> mehrere nebeneinander"""
import sys, os, math, re
import numpy as np
from PIL import Image

TEAM = (0.85, 0.08, 0.08)
LVL = int(os.environ.get('LVL', '1'))

def load_pie(moddir, path):
    txt = open(path, encoding='utf-8', errors='replace').read().replace('\r', '').split('\n')
    page = None; pts = []; polys = []; i = 0; lvl = 0; ver = 3; tw = th = 1.0
    while i < len(txt):
        t = txt[i].split()
        if not t: i += 1; continue
        if t[0] == 'PIE': ver = int(t[1])
        if t[0] == 'TEXTURE' and page is None:
            page = t[2]
            if len(t) >= 5: tw, th = float(t[3]), float(t[4])
        if t[0] == 'LEVEL': lvl += 1
        if lvl == LVL and t[0] == 'POINTS':
            n = int(t[1])
            for k in range(n): pts.append([float(x) for x in txt[i+1+k].split()[:3]])
            i += n
        elif lvl == LVL and t[0] == 'POLYGONS':
            n = int(t[1])
            for k in range(n):
                v = txt[i+1+k].split()
                fl = int(v[0], 16)
                nv = int(v[1]); idx = [int(x) for x in v[2:2+nv]]
                rest = v[2+nv:]
                if fl & 0x4000 and len(rest) >= 2: rest = rest[2:]  # texanim
                uv = [float(x) for x in rest[:2*nv]]
                if len(uv) == 2*nv:
                    polys.append((idx, [(uv[2*j] / (tw if ver < 3 else 1.0), uv[2*j+1] / (th if ver < 3 else 1.0)) for j in range(nv)]))
            i += n
        i += 1
    tex = None
    if page:
        for base in (os.path.join(moddir, 'texpages'),):
            f = os.path.join(base, page)
            if os.path.exists(f):
                tex = np.array(Image.open(f).convert('RGBA'))
                mf = re.sub(r'(page-\d+)[^/\\]*\.png$', r'\1_tcmask.png', f); m = None
                if os.path.exists(mf) and TEAM is not None:
                    m = np.array(Image.open(mf).convert('L')).astype(float) / 255.0
                    if m.shape != tex.shape[:2]: m = None
                if m is not None and os.path.exists(mf) and TEAM is not None:
                    rgb = tex[..., :3].astype(float) / 255.0 + (np.array(TEAM) - 0.5)[None, None, :] * m[..., None]
                    tex[..., :3] = (np.clip(rgb, 0, 1) * 255).astype(np.uint8)
    return np.array(pts, float), polys, tex, page

def render(pts, polys, tex, yaw=30, pitch=20, size=500, bg=(40, 44, 70), pad=0.08):
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    P = (Rx @ Ry @ pts.T).T
    mn = P[:, :2].min(0); mx = P[:, :2].max(0)
    sc = (1 - 2*pad) * size / max(mx[0]-mn[0], mx[1]-mn[1], 1e-6)
    ctr = (mn + mx) / 2
    X = (P[:, 0] - ctr[0]) * sc + size/2; Y = size/2 - (P[:, 1] - ctr[1]) * sc; Z = P[:, 2]
    img = np.zeros((size, size, 3), np.uint8); img[:] = bg
    zb = np.full((size, size), 1e9)
    for idx, uv in polys:
        for a in range(1, len(idx)-1):
            tri = [0, a, a+1]
            vx = np.array([X[idx[j]] for j in tri]); vy = np.array([Y[idx[j]] for j in tri]); vz = np.array([Z[idx[j]] for j in tri])
            if uv is None:
                u = np.zeros(3); v = np.zeros(3)
            else:
                u = np.array([uv[j][0] for j in tri]); v = np.array([uv[j][1] for j in tri])
            x0, x1 = int(max(0, math.floor(vx.min()))), int(min(size-1, math.ceil(vx.max())))
            y0, y1 = int(max(0, math.floor(vy.min()))), int(min(size-1, math.ceil(vy.max())))
            if x1 < x0 or y1 < y0: continue
            den = (vy[1]-vy[2])*(vx[0]-vx[2]) + (vx[2]-vx[1])*(vy[0]-vy[2])
            if abs(den) < 1e-9: continue
            gx, gy = np.meshgrid(np.arange(x0, x1+1)+0.5, np.arange(y0, y1+1)+0.5)
            w0 = ((vy[1]-vy[2])*(gx-vx[2]) + (vx[2]-vx[1])*(gy-vy[2])) / den
            w1 = ((vy[2]-vy[0])*(gx-vx[2]) + (vx[0]-vx[2])*(gy-vy[2])) / den
            w2 = 1 - w0 - w1
            m = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
            if not m.any(): continue
            z = w0*vz[0] + w1*vz[1] + w2*vz[2]
            uu = w0*u[0] + w1*u[1] + w2*u[2]; vv = w0*v[0] + w1*v[1] + w2*v[2]
            if uv is None:
                col = np.zeros(uu.shape + (4,), np.uint8); col[..., :3] = (110, 105, 120); col[..., 3] = 255
            elif tex is not None:
                h, w = tex.shape[:2]
                tx = np.clip((uu*w).astype(int), 0, w-1); ty = np.clip((vv*h).astype(int), 0, h-1)
                col = tex[ty, tx]
            else:
                col = np.zeros(uu.shape + (4,), np.uint8); col[..., :3] = 200; col[..., 3] = 255
            sub = zb[y0:y1+1, x0:x1+1]
            ok = m & (z < sub) & (col[..., 3] > 40)
            sub[ok] = z[ok]
            im = img[y0:y1+1, x0:x1+1]; im[ok] = col[..., :3][ok]
    return Image.fromarray(img)

if __name__ == '__main__':
    if sys.argv[1] == '--sheet':
        moddir, out = sys.argv[2], sys.argv[3]; files = sys.argv[4:]
        ims = []
        for f in files:
            pts, polys, tex, page = load_pie(moddir, f)
            ims.append(render(pts, polys, tex, size=320))
        W = Image.new('RGB', (320*len(ims), 320))
        for k, im in enumerate(ims): W.paste(im, (320*k, 0))
        W.save(out)
    else:
        moddir, f, out = sys.argv[1:4]
        yaw = float(sys.argv[4]) if len(sys.argv) > 4 else 30
        pitch = float(sys.argv[5]) if len(sys.argv) > 5 else 20
        size = int(sys.argv[6]) if len(sys.argv) > 6 else 500
        pts, polys, tex, page = load_pie(moddir, f)
        print(page, len(pts), len(polys), None if tex is None else tex.shape)
        render(pts, polys, tex, yaw, pitch, size).save(out)
