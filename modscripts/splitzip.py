"""Teilt das Mod-Paket in zwei Zip-Dateien (Geraete-Limit ~25 MiB je Datei):
  scale_mf_assets.wz.zip : grosse, selten geaenderte MF-Texturseiten + MF-Ton (einmal installieren, bleibt unveraendert)
  NAME_core_stumm.wz.zip : alles andere (stumm-Fassung)
splitzip.py NAME   (liest /tmp/out_s = stumm-Fassung und out_u)"""
import sys, os, re, zipfile, hashlib
NAME = sys.argv[1]; W = "/home/claude/w"
def is_asset(rel):
    b = os.path.basename(rel)
    if rel.startswith("audio/mf/"): return True
    if rel.startswith("texpages/"):
        m = re.match(r"page-(\d+)(?:[-_.](.*))?\.png$", b)
        if not m: return False
        n = int(m.group(1)); rest = m.group(2) or ""
        return n in (840, 841, 842) or rest.startswith("mf") or (n == 601)
    return False
def build(src, core, assets):
    zc = zipfile.ZipFile(core, "w", zipfile.ZIP_DEFLATED, compresslevel=9)
    za = zipfile.ZipFile(assets, "w", zipfile.ZIP_DEFLATED, compresslevel=9) if assets else None
    for root, _, fs in os.walk(src):
        for f in sorted(fs):
            p = os.path.join(root, f); rel = os.path.relpath(p, src).replace(os.sep, "/")
            (za if (za and is_asset(rel)) else zc).write(p, rel)
    zc.close()
    if za: za.close()
build("/tmp/out_s", "%s/%s_core_stumm.wz.zip" % (W, NAME), "%s/scale_mf_assets.new.zip" % W)
h = lambda f: hashlib.sha1(open(f, "rb").read()).hexdigest()
a_new = "%s/scale_mf_assets.new.zip" % W; a_old = "%s/scale_mf_assets.wz.zip" % W
# Inhalt vergleichen (nicht die Zip-Zeitstempel): Hash ueber Namen+CRC
def sig(f):
    z = zipfile.ZipFile(f); return hashlib.sha1(repr(sorted((i.filename, i.CRC) for i in z.infolist())).encode()).hexdigest()
changed = (not os.path.exists(a_old)) or sig(a_new) != sig(a_old)
if changed: os.replace(a_new, a_old)
else: os.remove(a_new)
print("Paket-Teile: %s_core_stumm %.1f MB, scale_mf_assets %.1f MB (%s)" % (NAME, os.path.getsize("%s/%s_core_stumm.wz.zip" % (W, NAME)) / 1e6, os.path.getsize(a_old) / 1e6, "NEU" if changed else "unveraendert"))
