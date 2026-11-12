"""Kampagnen-Kartendaten: Spieler-0-Einheiten -> Scav-Vorlagen, Gegner-Gebaeude -> Original-Duplikate (-Orig). sc_maps.py WRF_DIR OUT"""
import json, os, sys, glob, struct, copy
WRF, OUT = sys.argv[1], sys.argv[2]
st = json.load(open(os.path.join(OUT, "stats", "structure.json"), encoding="utf-8"))
tp = json.load(open(os.path.join(OUT, "stats", "templates.json"), encoding="utf-8"))
orig = json.load(open(os.path.join(OUT, "stats", "structure_orig.json"), encoding="utf-8"))
reskinned = {k for k in st if k in orig and st[k].get("structureModel") != orig[k].get("structureModel")}
# Original-Duplikate
n_dup = 0
for k in reskinned:
    d = copy.deepcopy(orig[k]); d["id"] = k + "-Orig"; st[k + "-Orig"] = d; n_dup += 1
json.dump(st, open(os.path.join(OUT, "stats", "structure.json"), "w", encoding="utf-8"), indent="\t", sort_keys=True)
n_d = n_s = 0
miss = set()
def setname(buf, o, new):
    b = new.encode(); assert len(b) < 60
    buf[o:o + 60] = b + b"\0" * (60 - len(b))
for f in glob.glob(os.path.join(WRF, "**", "*.bjo"), recursive=True):
    base = os.path.basename(f).lower()
    if base not in ("struct.bjo",): continue
    d = bytearray(open(f, "rb").read()); n = struct.unpack("<I", d[8:12])[0]; stride = 76 if base == "dinit.bjo" else 120
    if 12 + n * stride != len(d): print("Unerwartete Groesse", f); continue
    ch = 0
    for i in range(n):
        o = 12 + stride * i; nm = bytes(d[o:o + 60]).split(b"\0")[0].decode(); pl = struct.unpack("<I", d[o + 60:o + 64])[0]
        if base == "dinit.bjo":
            continue
        if False:
            if nm + "-Scav" in tp: setname(d, o, nm + "-Scav"); ch += 1; n_d += 1
            else: miss.add(nm)
        elif base == "struct.bjo" and pl != 0 and nm in reskinned:
            setname(d, o, nm + "-Orig"); ch += 1; n_s += 1
    if ch:
        rel = os.path.relpath(f, os.path.dirname(WRF.rstrip("/")))
        dst = os.path.join(OUT, rel); os.makedirs(os.path.dirname(dst), exist_ok=True); open(dst, "wb").write(bytes(d))
print("Duplikate:", n_dup, "Spieler-Fahrzeuge umbenannt:", n_d, "Gegner-Gebaeude umbenannt:", n_s, "ohne Klon:", sorted(miss))
