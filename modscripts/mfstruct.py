"""MF-Gebaeude (3DStructuresX/Y/Z) als baubare Strukturen: mfstruct.py OUT"""
import sys,os,json,glob;sys.path.insert(0,'/home/claude/w/mf_src')
import tbd,pieout,numpy as np,collections
import mfmap
OUT=sys.argv[1]
P=tbd.palettes('/home/claude/w/mf_src/GobjectPalettes.tbd')[7]
TP=os.path.join(OUT,'texpages');SD=os.path.join(OUT,'structs')
sp=os.path.join(OUT,'stats','structure.json');S=json.load(open(sp))
rp=os.path.join(OUT,'stats','research.json');R=json.load(open(rp))
res=R['R-Sys-Spade1Mk1']['resultStructures']
WEAP=['MG3-Tower','BabaTowerMG','Mortar1Mk1','MG3-Tower','BabaTowerMG']
PRETTY={'CryoFarm':'Cryo Farm','DefRelay':'Defense Relay','Mconverter':'Matter Converter','VehFactory':'Vehicle Factory','EnergyBank':'Energy Bank','Hanger':'Aircraft Hangar','AI Facility':'AI Facility','ReconPole':'Recon Pole','Outpost':'Outpost','PointDef':'Point Defense','Platform':'Rail Gun','Prison':'Prison','NeutronBomb':'Neutron Bomb Turret','ReconSalvo':'Imaging Salvo Turret','Tectonic':'Tectonic Torpedo Turret','E M P':'EMP Turret','OrbitalBomb':'Orbital Bomb Turret','PhaseCharge':'Phase Charge Turret','Archeology':'Archeology Station','Training':'Training Facility','Research':'Research Facility','BotParts':'Combot Part Factory','SolarPanel':'Solar Panels','Assembly':'Combot Assembly Bay','Wall':'Mobile Wall','LaserTurret':'Laser Turret','UUPE':'Wall Turret','Anti-Air':'Anti-Air Turret','Elevator':'Elevator','Recon':'Recon Base','Attack':'Attack Base','Construct':'Construction Base','Defense':'Defense Base'}
ARMED={'Anti-Air':'MG3-Tower','LaserTurret':'BabaTowerMG','UUPE':'MG3-Tower','E M P':'Mortar1Mk1','NeutronBomb':'Mortar1Mk1','OrbitalBomb':'Mortar1Mk1','PhaseCharge':'Mortar1Mk1','Tectonic':'Mortar1Mk1','ReconSalvo':'Mortar1Mk1','PointDef':'BabaTowerMG','Platform':'Mortar1Mk1'}
MORE_HP={'Mconverter':3,'Wall':2}
cnt=collections.Counter()
page_no=840;made=[];ids=[]
for fn in sorted(glob.glob('/home/claude/w/mf_src/3DStructures*.tbd')):
    f=os.path.basename(fn)[12]  # X/Y/Z
    data,T=tbd.load(fn);O=tbd.objects(data,T)
    for k in list(O):
        if O[k]['bpp']!=8:del O[k]
    keys=sorted(O)
    if not keys:continue
    im,pos,H=pieout.atlas(O,P,keys);page='page-%d-mfs%s.png'%(page_no,f.lower());page_no+=1;im.save(os.path.join(TP,page))
    G=[];cur=None
    for k,(h,o) in enumerate(T):
        if h==3642917960:cur=[k];G.append(cur)
        elif h==3557481044 and cur is not None:cur.append(k)
    gm,_=mfmap.groupmap(fn);n=0
    for g in G:
        ks=[k for k in g[1:] if k in O]
        if not ks:continue
        V=np.array([v[:3] for k in ks for x in O[k]['meshes'] for v in x['V']])
        if len(V)<8:continue
        mn,mx=V.min(0),V.max(0);ext=max(mx[0]-mn[0],mx[1]-mn[1])
        if ext<1 or mx[2]-mn[2]<0.5 or ext>400:continue
        n+=1
        sc=105.0/ext if ext>0 else 1.0
        sc=min(sc,60.0)
        parts=[(O[k],k,(0,0,0),False) for k in ks]
        cx=(mn[0]+mx[0])/2;cy=(mn[1]+mx[1])/2
        txt,W=pieout.pie_parts(parts,pos,H,page,sc,origin=(cx,cy,mn[2]))
        zmax=max(p[1] for p in W)
        name='mfs_%s%d.pie'%(f.lower(),n);open(os.path.join(SD,name),'w').write(txt)
        cn=collections.Counter()
        for kk in range(g[0],(G[G.index(g)+1][0] if G.index(g)+1<len(G) else 10**9)):
            if kk in gm:cn.update(gm[kk])
        key=cn.most_common(1)[0][0] if cn else None
        sid='MFS_%s%d'%(f,n);hh=1 if zmax<110 else 2
        d=dict(id=sid,name=(PRETTY.get(key,key)+' %s%d'%(f,n)) if key else 'MF Bau %s%d'%(f,n),type='DEFENSE' if (key in ARMED or not key) else 'WALL',structureModel=[name],width=1,breadth=1,height=hh,
               armour=15,thermal=10,hitpoints=int(300+zmax*3),buildPoints=20,buildPower=60,resistance=150,
               strength='HARD',ecmID='ZNULLECM',sensorID='DefaultSensor1Mk1',weapons=[ARMED.get(key,WEAP[n%len(WEAP)])])
        if d['type']=='WALL':d.pop('weapons');d.pop('sensorID')
        S[sid]=d;res.append(sid);ids.append(sid)
print('Gebaeude',len(ids))
json.dump(S,open(sp,'w'),indent='\t',sort_keys=True)
json.dump(R,open(rp,'w'),indent='\t',sort_keys=True)
