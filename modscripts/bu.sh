U=uni2
rm -rf out_u && python3 wz_scale.py --data base mp extra --structure-json $U/structure.json --out out_u --replace-structs repl --effects effsrc1 effsrc2 --body-json $U/body.json --weapons-json $U/weapons.json --propulsion-json $U/propulsion.json --sensor-json $U/sensor.json --range-factor 0.5 --speed-factor 0.5 --templates-json $U/templates.json --research-json $U/research.json --features feat/features --features-json $U/features.json --sprite-scav --punk --sprite-run levels 2>&1 | tail -12
python3 -I gen/make_tex32.py tex/texpages/page-32-soldier.png pages32 >/dev/null 2>&1; cp -r pages32/texpages out_u/; cp tex/page-7-scavspr.png pagesP/texpages/page-32-punk.png extra/texpages/* out_u/texpages/
cp extra/structs/* out_u/structs/
python3 -I - <<'P'
import json
p='out_u/stats/structure.json'; s=json.load(open(p))
W=["BLBRBWLH.PIE","blbrbwlx.pie","blbrbwlt.pie","blbrbwll.pie"]
for k in ("A0HardcreteMk1Wall","A0HardcreteMk1CWall","WallTower-HPVcannon"):
    pass
for k,v in s.items():
    if v.get("type")=="WALL" and k=="A0HardcreteMk1Wall": v["structureModel"]=W
    if v.get("type")=="CORNER WALL" and k=="A0HardcreteMk1CWall": v["structureModel"]=["blbrbwlx.pie"]
json.dump(s,open(p,'w'),indent="\t",sort_keys=True)
P
