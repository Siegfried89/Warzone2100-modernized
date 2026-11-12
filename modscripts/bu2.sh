U=uni3
python3 -I sc_pre.py uni uni3
rm -rf out_u && python3 wz_scale.py --data base mp extra --structure-json uni/structure.json --out out_u --replace-structs repl --effects effsrc1 effsrc2 --body-json $U/body.json --weapons-json $U/weapons.json --propulsion-json $U/propulsion.json --sensor-json $U/sensor.json --range-factor 0.5 --speed-factor 0.5 --templates-json $U/templates.json --research-json $U/research.json --features feat/features --features-json $U/features.json --sprite-scav --punk --sprite-run levels 2>&1 | tail -12 | cut -c1-200
python3 -I gen/make_tex32.py tex/texpages/page-32-soldier.png pages32 >/dev/null 2>&1; cp -r pages32/texpages out_u/; cp tex/page-7-scavspr.png pagesP/texpages/page-32-punk.png extra/texpages/* out_u/texpages/
python3 -I sc_post.py out_u
python3 -I sc_maps.py /tmp/claude-0/cmp/base/wrf out_u
rm -f out_u/stats/structure_orig.json
for f in repair; do cp uni3/$f.json out_u/stats/$f.json; done
for f in construction ecm brain propulsiontype weaponmodifier structuremodifier; do [ -f uni/x_$f.json ] && cp uni/x_$f.json out_u/stats/$f.json; done
# Dateien aus mp.wz, die in der Kampagne fehlen (Modelle, Effekte, Meldungen)
python3 -I - <<'P'
import os, glob, shutil
n = 0
for d in ("components", "structs", "effects", "anims", "messages", "audio"):
    for src in glob.glob(os.path.join("/tmp/claude-0/cmp/mp", d, "**", "*"), recursive=True):
        if not os.path.isfile(src): continue
        rel = os.path.relpath(src, "/tmp/claude-0/cmp/mp")
        if os.path.exists(os.path.join("/tmp/claude-0/cmp/base", rel)) or os.path.exists(os.path.join("out_u", rel)): continue
        os.makedirs(os.path.dirname(os.path.join("out_u", rel)), exist_ok=True); shutil.copyfile(src, os.path.join("out_u", rel)); n += 1
print("aus mp kopiert:", n)
P

# Bagger-Modelle (Ultimate Scavengers, crane2 = gelber Bagger mit massivem Arm), Textur umbenannt wegen page-111-Konflikt
M=/tmp/claude-0/mods/x_ultimatescavs-1-15.mod.wz
sed 's/page-111.png/page-411-scavcrane.png/I' $M/components/bodies/craneBody2.pie > out_u/components/bodies/cranebody2.pie
sed 's/page-111.png/page-411-scavcrane.png/I' $M/components/weapons/craneArm2.pie > out_u/components/weapons/cranearm2.pie
cp $M/texpages/page-111.png out_u/texpages/page-411-scavcrane.png

python3 -I - <<'P'
import json
f = "out_u/stats/construction.json"; c = json.load(open(f))
for k in ("Spade1Mk1", "CyborgSpade", "scavCrane1"):
    if k in c: c[k]["designable"] = 0
if "scavCrane2" in c: c["scavCrane2"]["designable"] = 1
json.dump(c, open(f, "w"), indent="\t", sort_keys=True)
P
python3 -I dedupe.py
python3 -I backfix.py out_u
python3 -I variants.py out_u
python3 -I colour.py out_u
python3 -I bbaagunner.py out_u
python3 -I wild.py out_u
python3 -I newguys.py out_u
python3 -I more.py out_u
python3 -I pits.py out_u
python3 -I scavyel.py out_u
python3 -I mfveh.py out_u
python3 -I mfparts.py out_u
python3 -I mfui.py out_u
python3 -I mfsound.py out_u
python3 -I mfwrf.py out_u
python3 -I landfill.py out_u
python3 -I outpost.py out_u
python3 -I sandbag.py out_u
python3 -I pipeline.py out_u
python3 -I trench.py out_u
python3 -I drums.py out_u
python3 -I power.py out_u
python3 -I refinery.py out_u
python3 -I shaft.py out_u
python3 -I passable.py out_u
python3 -I mgimpact.py out_u
python3 -I projspeed.py out_u
python3 -I cannonimpact.py out_u
python3 -I scavtruck.py out_u
python3 -I hall.py out_u
python3 -I mftcmask.py out_u
python3 -I mfstruct.py out_u
python3 -I clean.py out_u
