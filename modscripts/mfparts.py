"""Modulare Combot-Teile: je Teilgruppe eigene PIEs + Atlas, Skelett-Offsets und echte MF-Animationen als Daten.
mfparts.py OUT  ->  OUT/components/bodies/mfp_*.pie, OUT/texpages/page-9xx-*.png, OUT/mfmodular/parts.json, anims.json,
                    OUT/features/mfp_*.pie (abgeschlagene Teile), OUT/components/bodies/mfproxy.pie (Huellkoerper)"""
import sys,os,json,glob,struct,re,math;sys.path.insert(0,'/home/claude/w/mf_src');sys.path.insert(0,'/home/claude/exe');sys.path.insert(0,'/home/claude/w')
import numpy as np,tbd,pieout,mfskel as M,anim
OUT=sys.argv[1];SC=1.8
P=tbd.palettes('/home/claude/w/mf_src/GobjectPalettes.tbd')[7]
PD=os.path.join(OUT,'components','bodies');TP=os.path.join(OUT,'texpages');MD=os.path.join(OUT,'mfmodular');FD=os.path.join(OUT,'features')
for d in (PD,TP,MD,FD):os.makedirs(d,exist_ok=True)
v3=lambda v:[round(float(x),4) for x in v]
C=np.array([[1,0,0],[0,0,1],[0,-1,0]],float)   # MF (x,y,z) -> Modell (x,z,-y)
pretty=lambda f:re.sub(r'(?<!^)(?=[A-Z])',' ',f)

# ---------- Arm-Knochenzuordnung (+x/-x) wie in mfanim2.py ----------
_B=M.load_file('/home/claude/w/mf_src/RobotPartsBasic.tbd');_AB=anim.load_anims('/home/claude/w/mf_src/RobotPartsBasic.tbd');_W=_AB[0]
def _spec(t,l,a,ab):return dict(lo=M.leg_offsets(l),to=M.torso_offsets(t),aR=M.arm_offsets(a),aL=M.arm_offsets(a),armbones=ab)
_sp=_spec(_B['cls']['torso'][0],_B['cls']['leg'][0],_B['cls']['arm'][0],((9,10,11),(12,13,14)))
_ya=[];_yl=[]
for fr in range(_W['nf']):
    T=M.pose(_sp,_W['ang'][fr],_W['pos'][fr]);_ya.append(T[11][1][1]);_yl.append(T[3][1][1])
ARMB=((9,10,11),(12,13,14)) if np.corrcoef(_ya,_yl)[0,1]<0 else ((12,13,14),(9,10,11))
print('Arm-Knochen (+x,-x):',ARMB)

# ---------- Werte je Teil ----------
MELEE={'Axe':110,'KatanaSword':90,'LaserSword':100,'HammerHand':130,'PowerFist':120,'BladeArm':80,'MultiClaw':85,'RotaryBlade':95,'ElectroGrip':70}
RANGED={'Basic':(20,'BabaMG'),'GattlingGun':(14,'BabaHeavyMG'),'HomingMissile':(45,'BabaRocket'),'LongMissile':(60,'BabaRocket'),'DrunkMissile':(35,'BabaRocket'),
        'Howitzer':(75,'BabaCannon'),'Flak':(30,'BabaCannon'),'CarpetBomb':(55,'BabaCannon'),'PlasmaCannon':(70,'BabaCannon'),'EnergyBlaster':(40,'BabaMG'),
        'Laser':(38,'BabaMG'),'SniperLaser':(85,'BabaMG'),'PowerGun':(30,'BabaMG'),'PowerPulse':(36,'BabaMG'),'ProtoBlast':(40,'BabaMG'),'BlastPulse':(44,'BabaMG'),
        'Tracer':(22,'BabaMG'),'EMP':(15,'BabaMG')}
SHIELD={'Shield':25,'PowerShield':40,'EnergyShield':35,'ForceField':50}
ABIL={'SelfRepair':'repair','JetPack':'fly','JumpJet':'jump','Camo':'cloak','Sonar':'sonar','Recon':'recon','HTHupgrade':'hth','Steady':'steady'}
LEGSPEED={'HighSpeed':110,'JetPack':100,'JumpJet':95,'HighStrength':62,'Armor':66}
def vol(g):
    d=g['mx']-g['mn'];return float(abs(d[0]*d[1]*d[2]))**(1/3)
def torso_st(f,g):
    v=vol(g);hp=int(240+v*20);ak=12+int(v);ah=10+int(v*0.8)
    if f=='HighStrength':hp=int(hp*1.4)
    if f=='Armor':ak+=14;ah+=10
    if f in SHIELD:ak+=SHIELD[f]//5
    return dict(hp=hp,ak=ak,ah=ah)
def leg_st(f,g):
    return dict(spd=LEGSPEED.get(f,75),hp=50)
def arm_st(f):
    if f in MELEE:return dict(dmg=MELEE[f],rng=192,melee=1,base='BabaMG')
    if f in SHIELD:return dict(dmg=15,rng=192,melee=1,base='BabaMG',blk=SHIELD[f])
    d,b=RANGED.get(f,(20,'BabaMG'));return dict(dmg=d,rng=384,melee=0,base=b)

# ---------- Teile exportieren ----------
parts=[];page_no=900;ANIMS={};first_page=None
def pie_text(pts,polys,page):
    L=['PIE 3','TYPE 10200','INTERPOLATE 0','TEXTURE 0 %s 0 0'%page,'LEVELS 1','LEVEL 1','POINTS %d'%len(pts)]+['\t%.2f %.2f %.2f'%tuple(p) for p in pts]
    pl=[]
    for tri,q in polys:
        pl.append('\t200 3 %d %d %d %.5f %.5f %.5f %.5f %.5f %.5f'%(tri[0],tri[1],tri[2],q[0][0],q[0][1],q[1][0],q[1][1],q[2][0],q[2][1]))
        pl.append('\t200 3 %d %d %d %.5f %.5f %.5f %.5f %.5f %.5f'%(tri[0],tri[2],tri[1],q[0][0],q[0][1],q[2][0],q[2][1],q[1][0],q[1][1]))
    return '\n'.join(L+['POLYGONS %d'%len(pl)]+pl)+'\n'
def pack(A):
    return [dict(idx=a['idx'],nf=a['nf'],t=round(float(a['t'][-1]),3),pos=[v3(p) for p in a['pos']],
                 w=[[int(x) for x in row] for row in a['raw']]) for a in A]
def rest_offsets(kind,d,n):
    """Ruhelage der Einzelteile (Modellraum) fuer das am Boden liegende Teil"""
    z=np.zeros(3);o=[z]*n
    if kind=='leg':
        hR=np.array(d['hipR']);hL=np.array(d['hipL']);kA=np.array(d['kneeA']);aA=np.array(d['ankA']);kB=np.array(d['kneeB']);aB=np.array(d['ankB'])
        pos=[z,hR,hR+kA,hR+kA+aA,hL,hL+kB,hL+kB+aB]
        o=[pos[min(i,6)] for i in range(n)]
    elif kind=='arm':
        ch=[np.array(c) for c in d['chain']]+[z,z,z]
        pos=[z,ch[0],ch[0]+ch[1]]
        o=[pos[min(i,2)] for i in range(n)]
    return [(C@p)*SC for p in o]
def feature_pie(kind,d,geoms,page):
    """geoms: [(pts,polys)] je Einzelteil -> ein PIE, am Boden liegend"""
    offs=rest_offsets(kind,d,len(geoms));pts=[];polys=[]
    for (p,pl),off in zip(geoms,offs):
        base=len(pts);pts+= [np.array(x,float)+off for x in p]
        for tri,q in pl:polys.append(([i+base for i in tri],q))
    A=np.array(pts)
    if kind!='torso':   # um 90 Grad um X kippen -> liegt am Boden
        A=np.stack([A[:,0],-A[:,2],A[:,1]],1)
    A[:,1]-=A[:,1].min()
    return pie_text(A,polys,page)

for fn in sorted(glob.glob('/home/claude/w/mf_src/RobotParts*.tbd')):
    f=os.path.basename(fn)[10:-4]
    if f=='Frame':continue
    try:F=M.load_file(fn)
    except Exception as e:print('skip',f,e);continue
    O=F['O']
    for kind in ('torso','leg','arm'):
        for gi,g in enumerate(F['cls'][kind]):
            page_no+=1
            objs={k:O[k] for k in g['ks']};im,pos,H=pieout.atlas(objs,P,list(objs))
            page='page-%d-mfp%s.png'%(page_no,'%s%s%d'%(f.lower()[:5],kind[0],gi));im.save(os.path.join(TP,page))
            if first_page is None:first_page=page
            pid='%s_%s%d'%(f,kind[0].upper(),gi);files=[];geoms=[]
            for j,k in enumerate(g['ks']):
                name='mfp_%s_%d.pie'%(pid.lower(),j)
                pts,polys=pieout._lvl_geom([(O[k],k,(0,0,0),False)],pos,H,page,(0,0,0),SC,0)
                open(os.path.join(PD,name),'w').write(pie_text(pts,polys,page));files.append(name);geoms.append((pts,polys))
            d=dict(id=pid,file=f,kind=kind,pies=files,side=g.get('side',0))
            if kind=='leg':
                o=M.leg_offsets(g);d.update({kk:v3(vv) for kk,vv in o.items()});d['st']=leg_st(f,g)
            elif kind=='torso':
                o=M.torso_offsets(g);d.update({kk:v3(vv) for kk,vv in o.items()});d['st']=torso_st(f,g)
            else:
                d['chain']=[v3(x) for x in M.arm_offsets(g)];d['st']=arm_st(f)
            d['name']='%s %s'%(pretty(f),{'torso':'Torso','leg':'Legs','arm':'Arm'}[kind]+('' if kind!='arm' else (' R' if d['side']>0 else ' L')))+('' if gi==0 else ' %d'%(gi+1))
            ab=[ABIL[f]] if f in ABIL else []
            if f in SHIELD:ab.append('shield')
            d['abil']=ab
            d['feature']='MFP_'+pid
            open(os.path.join(FD,'mfp_%s.pie'%pid.lower()),'w').write(feature_pie(kind,d,geoms,page))
            parts.append(d)
    ANIMS[f]=None

AN={}
for fn in sorted(glob.glob('/home/claude/w/mf_src/RobotParts*.tbd')):
    f=os.path.basename(fn)[10:-4]
    A=anim.load_anims(fn)
    if A:AN[f]=pack(A)
AN['_base']=pack(anim.load_anims('/home/claude/an/animbase.tbd'))
json.dump(dict(scale=SC,order=M.ORDER,signs=list(M.SIGNS),armb=[list(ARMB[0]),list(ARMB[1])],parts=parts),open(os.path.join(MD,'parts.json'),'w'))
json.dump(AN,open(os.path.join(MD,'anims.json'),'w'),separators=(',',':'))
print('Teile',len(parts),'Animationsdateien',len(AN),'Seiten',page_no-900)

# ---------- Huellkoerper (Auswahlrahmen, Waffenanker); wird nie gezeichnet ----------
pxy=[(-18,0,-12),(18,0,-12),(18,0,12),(-18,0,12),(-18,55,-12),(18,55,-12),(18,55,12),(-18,55,12)]
fcs=[(0,1,2),(0,2,3),(4,6,5),(4,7,6),(0,4,5),(0,5,1),(1,5,6),(1,6,2),(2,6,7),(2,7,3),(3,7,4),(3,4,0)]
L=['PIE 3','TYPE 10200','INTERPOLATE 0','TEXTURE 0 %s 0 0'%first_page,'LEVELS 1','LEVEL 1','POINTS 8']+['\t%d %d %d'%p for p in pxy]
L+=['POLYGONS %d'%len(fcs)]+['\t200 3 %d %d %d 0 0 0.01 0 0 0.01'%f for f in fcs]
L+=['CONNECTORS 3','\t17.00 27.00 8.00','\t-17.00 27.00 8.00','\t0.00 50.00 0.00']
# Todes-Ereignis: die Dauer (Einzelbild-ms x Zyklen) bestimmt, wie lange der Engine-Todeseffekt laeuft; gezeichnet wird er von modcombot
_dmax=max(a['t'][-1] for a in anim.load_anims('/home/claude/an/animbase.tbd') if a['idx'] in (50,51,52,53,54))
_cyc=int(math.ceil(_dmax*1000/33.0))+3
LD=list(L)
LD[LD.index('LEVELS 1')]='LEVELS 1'
LD+=['ANIMOBJECT 33 %d 2'%_cyc,'          0 0 0 0 0 0 0 1 1 1','          1 0 0 0 0 0 0 1 1 1']
open(os.path.join(PD,'mfproxy_die.pie'),'w').write('\n'.join(LD)+'\n')
L.insert(L.index('LEVELS 1'),'EVENT 3 mfproxy_die.pie')
open(os.path.join(PD,'mfproxy.pie'),'w').write('\n'.join(L)+'\n')

# ---------- Stats: Torso=Body, Beine=Propulsion, Arm=Waffe (nicht im normalen Designer sichtbar) ----------
OUTS=os.path.join(OUT,'stats')
if os.path.exists(os.path.join(OUTS,'body.json')):
    bs=json.load(open(os.path.join(OUTS,'body.json')));ps=json.load(open(os.path.join(OUTS,'propulsion.json')));ws=json.load(open(os.path.join(OUTS,'weapons.json')))
    fs=json.load(open(os.path.join(OUTS,'features.json')))
    for d in parts:
        pid='MF%s_%s'%({'torso':'T','leg':'L','arm':'A'}[d['kind']],d['id']);st=d['st']
        if d['kind']=='torso':
            b=json.loads(json.dumps(bs['Body5REC-Scav']));b.update(id=pid,name=d['name'],model='mfproxy.pie',designable=0,size='HEAVY',hitpoints=st['hp'],weaponSlots=2,armourKinetic=st['ak'],armourHeat=st['ah']);bs[pid]=b
        elif d['kind']=='leg':
            p=json.loads(json.dumps(ps['MFWalker'])) if 'MFWalker' in ps else json.loads(json.dumps(ps['BaBaLegs']));p.update(id=pid,name=d['name'],designable=0,speed=st['spd'],hitpointPctOfBody=st['hp']);p.pop('model',None);ps[pid]=p
        else:
            w=json.loads(json.dumps(ws.get(st['base'],ws['BabaMG'])));w.update(id=pid,name=d['name'],designable=0,damage=st['dmg'])
            w.pop('model',None);w.pop('mountModel',None)
            if st['melee']:
                w.update(longRange=st['rng'],shortRange=128,firePause=30,longHit=85,shortHit=95)
            ws[pid]=w
        if d['kind']!='torso':
            fs[d['feature']]=dict(id=d['feature'],name='Salvage '+d['name'],type='OIL DRUM',model='mfp_%s.pie'%d['id'].lower(),width=1,breadth=1,damageable=0,hitpoints=0,armour=0,tileDraw=0,lineOfSight=0)
    for f,o in (('body.json',bs),('propulsion.json',ps),('weapons.json',ws),('features.json',fs)):json.dump(o,open(os.path.join(OUTS,f),'w'),indent='\t',sort_keys=True)
    print('Stats ergaenzt')

# ---------- Fertige Combot-Vorlagen (aus Teilen), alle Teile sofort verfuegbar ----------
def inject(names):
    js="\n\ttry { var _mf=%s; for (var _i=0;_i<_mf.length;_i++) { makeComponentAvailable(_mf[_i], %%s); } } catch (e) {}\n"%json.dumps(names)
    g=os.path.join(OUT,'multiplay','script','rules','events','gameinit.js')
    if os.path.exists(g):
        t=open(g).read()
        t=re.sub(r"\n\t\ttry \{ var _mf=.*?\} catch \(e\) \{\}\n","\n",t,flags=re.S)
        t=t.replace("setupComponents(playnum);","setupComponents(playnum);"+(js%'playnum').replace('\n\t','\n\t\t'),1);open(g,'w').write(t)
    r=os.path.join(OUT,'script','rules.js')
    if os.path.exists(r):
        t=open(r).read()
        t=re.sub(r"\n\ttry \{ var _mf=.*?\} catch \(e\) \{\}\n","\n",t,flags=re.S)
        t=t.replace("try { changePlayerColour(selectedPlayer, 1); } catch (e) {}","try { changePlayerColour(selectedPlayer, 1); } catch (e) {}"+(js%'selectedPlayer'),1);open(r,'w').write(t)
if os.path.exists(os.path.join(OUTS,'body.json')):
    tp_=os.path.join(OUTS,'templates.json');tp=json.load(open(tp_))
    byid={d['id']:d for d in parts}
    prefix={'torso':'MFT_','leg':'MFL_','arm':'MFA_'}
    names=[prefix[d['kind']]+d['id'] for d in parts]
    files=sorted({d['file'] for d in parts if d['kind']=='arm'})
    nt=0
    for f in files:
        rs=[d for d in parts if d['file']==f and d['kind']=='arm' and d['side']>0]
        ls=[d for d in parts if d['file']==f and d['kind']=='arm' and d['side']<0]
        if not rs and not ls:continue
        ra=rs[0] if rs else None
        la=ls[0] if ls else byid.get('Basic_A3')
        ws_=[prefix['arm']+x['id'] for x in (ra,la) if x]
        legs='MFL_HighSpeed_L0' if 'HighSpeed_L0' in byid and f in ('Recon','JetPack','JumpJet') else 'MFL_Basic_L0'
        torso='MFT_%s_T0'%f if '%s_T0'%f in byid else 'MFT_Basic_T0'
        x=json.loads(json.dumps(tp['BarbarianBuggy']))
        x.update({'id':'MFP_%s'%f,'name':'Combot %s'%pretty(f),'body':torso,'propulsion':legs,'weapons':ws_,'available':True});tp[x['id']]=x;nt+=1
    json.dump(tp,open(tp_,'w'),indent='\t',sort_keys=True)
    inject(names+['MFL_Basic_L0'])
    print('Fertige Vorlagen',nt,'Teile verfuegbar',len(names))

# ---------- Montagehalle (Gestell fuer den Designer): Plattform steht in structs/mfs_x25.pie, hier das Portal drumherum ----------
def box(x0,y0,z0,x1,y1,z1):
    p=[(x0,y0,z0),(x1,y0,z0),(x1,y0,z1),(x0,y0,z1),(x0,y1,z0),(x1,y1,z0),(x1,y1,z1),(x0,y1,z1)]
    f=[(0,2,1),(0,3,2),(4,5,6),(4,6,7),(0,1,5),(0,5,4),(1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,0,4),(3,4,7)]
    return p,f
def gantry():
    pts=[];fcs=[]
    def add(b):
        p,f=b;o=len(pts);pts.extend(p);fcs.extend([(a+o,b_+o,c+o) for a,b_,c in f])
    R=58.0;H=118.0;t=5.0
    for sx in (-1,1):
        for sz in (-1,1):
            add(box(sx*R-t,0,sz*R-t,sx*R+t,H,sz*R+t))       # vier Pfosten
    for sz in (-1,1):
        add(box(-R-t,H-4,sz*R-t,R+t,H+4,sz*R+t))             # Querbalken vorn/hinten
    for sx in (-1,1):
        add(box(sx*R-t,H-4,-R-t,sx*R+t,H+4,R+t))             # Querbalken links/rechts
    add(box(-6,H-40,-6,6,H+4,6))                              # Kranhaken-Schaft
    add(box(-R,H*0.5-2,-R,R,H*0.5+2,-R+3))                    # Zwischenstrebe hinten
    return pts,fcs
_gp,_gf=gantry()
# Textur: Metallpixel der Bay-Seite (page-840-mfsx.png); flache Farbe genuegt
_gt='page-840-mfsx.png'
_L=['PIE 3','TYPE 10200','INTERPOLATE 0','TEXTURE 0 %s 256 512'%_gt,'LEVELS 1','LEVEL 1','POINTS %d'%len(_gp)]+['\t%.2f %.2f %.2f'%p for p in _gp]
_L+=['POLYGONS %d'%len(_gf)]+['\t200 3 %d %d %d 0.93 0.41 0.94 0.41 0.93 0.42'%f for f in _gf]
open(os.path.join(FD,'mfbay_gantry.pie'),'w').write('\n'.join(_L)+'\n')
print('Montagehalle-Gestell geschrieben')
