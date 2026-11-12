"""Animierte MF-Combots mit den ECHTEN MF-Animationen (15-Knochen-Skelett). mfanim2.py OUT"""
import sys,os,json,math,glob,re;sys.path.insert(0,'/home/claude/w/mf_src');sys.path.insert(0,'/home/claude/exe');sys.path.insert(0,'/home/claude/w')
import tbd,pieout,numpy as np,mfskel as M,anim
OUT=sys.argv[1];SC=float(os.environ.get('MFCSC',1.8))
P=tbd.palettes('/home/claude/w/mf_src/GobjectPalettes.tbd')[7]
TP=os.path.join(OUT,'texpages');BD=os.path.join(OUT,'components','bodies')
bp=os.path.join(OUT,'stats','body.json');bs=json.load(open(bp));wp=os.path.join(OUT,'stats','weapons.json');ws=json.load(open(wp))
tp_=os.path.join(OUT,'stats','templates.json');tp=json.load(open(tp_));pp=os.path.join(OUT,'stats','propulsion.json');ps=json.load(open(pp))
C=np.array([[1,0,0],[0,0,1],[0,-1,0]],float)
def toE(R,p):
    RE=C@R@C.T;return RE,C@p
def solve(RE):
    b=math.asin(max(-1,min(1,RE[0,2])));cb=math.cos(b)
    if abs(cb)<1e-4:a=0.;c=math.atan2(RE[1,0],RE[1,1])
    else:a=math.atan2(-RE[1,2],RE[2,2]);c=math.atan2(-RE[0,1],RE[0,0])
    return -math.degrees(a),-math.degrees(c),-math.degrees(b)   # file rx,ry,rz
FILES={}
for fn in sorted(glob.glob('/home/claude/w/mf_src/RobotParts*.tbd')):
    f=os.path.basename(fn)[10:-4]
    if f=='Frame':continue
    try:FILES[f]=M.load_file(fn)
    except Exception as e:print('skip',f,e)
B=FILES['Basic'];BT=B['cls']['torso'];BL=B['cls']['leg'];BA=B['cls']['arm']
AB=anim.load_anims('/home/claude/w/mf_src/RobotPartsBasic.tbd')
WALK=AB[0]
BASE={a['idx']:a for a in anim.load_anims('/home/claude/an/animbase.tbd')}
IDLE=BASE[30];DEATHS=[BASE[i] for i in (50,51,52,53,54)]
def msof(a):return max(1,int(round(float(a['t'][-1])/(a['nf']-1)*1000)))
def mkspec(t,l,aR,aL,ab):
    return dict(lo=M.leg_offsets(l),to=M.torso_offsets(t),aR=M.arm_offsets(aR),aL=M.arm_offsets(aL),armbones=ab)
# Arm-Zuordnung: der Arm, der gegenphasig zum +x-Bein schwingt, ist die +x-Seite
sp=mkspec(BT[0],BL[0],BA[0],BA[0],((9,10,11),(12,13,14)))
ya=[];yl=[];yb=[]
for fr in range(WALK['nf']):
    T=M.pose(sp,WALK['ang'][fr],WALK['pos'][fr]);ya.append(T[11][1][1]);yb.append(T[14][1][1]);yl.append(T[3][1][1])
c9=np.corrcoef(ya,yl)[0,1];print('corr arm9~legA',round(c9,2))
ARMB=((9,10,11),(12,13,14)) if c9<0 else ((12,13,14),(9,10,11))
print('Arm-Knochen (+x,-x):',ARMB)
def levels_for(t,l,aR,aL):
    """Liste (bone, [(ob,key)]) -> Geometrie je Knochen"""
    lv=[];
    for b,ks,src in ((0,l['ks'][0:1],'L'),(1,l['ks'][1:2],'L'),(2,l['ks'][2:3],'L'),(3,l['ks'][3:4],'L'),(4,l['ks'][4:5],'L'),(5,l['ks'][5:6],'L'),(6,l['ks'][6:],'L'),(7,t['ks'][0:1]+t['ks'][2:],'T'),(8,t['ks'][1:2],'T')):
        if ks:lv.append((b,[(src,k) for k in ks]))
    for bones,g in ((ARMB[0],aR),(ARMB[1],aL)):
        for i,b in enumerate(bones):
            ks=g['ks'][i:i+1] if i<2 else g['ks'][2:]
            if ks:lv.append((b,[('A%d'%(0 if g is aR else 1),k) for k in ks]))
    return lv
def build(nm,page,pos,H,objsrc,lv,spec,anm,N,ms,events,sel=None):
    """anm: Anim-Dict oder None (Idle: Frame 0 der Walk-Anim)"""
    frames=range(N)
    Ts=[]
    for fr in frames:
        a=anm if anm is not None else WALK
        ff=fr if anm is not None else 0
        ff=min(ff,a['nf']-1)
        Ts.append(M.pose(spec,a['ang'][ff],a['pos'][ff]))
    L=['PIE 3','TYPE 10200','INTERPOLATE 0','TEXTURE 0 %s 0 0'%page]
    for i,e in events:L.append('EVENT %d %s'%(i,e))
    L+=['LEVELS %d'%len(lv)]
    for li,(b,items) in enumerate(lv,1):
        parts=[(objsrc[it],it,(0,0,0),False) for it in items]
        pts,polys=pieout._lvl_geom(parts,pos,H,page,(0,0,0),SC,0)
        L+=['LEVEL %d'%li,'POINTS %d'%len(pts)]+['\t%.2f %.2f %.2f'%p for p in pts]
        pl=[]
        for tri,q in polys:
            pl.append('\t200 3 %d %d %d %.5f %.5f %.5f %.5f %.5f %.5f'%(tri[0],tri[1],tri[2],q[0][0],q[0][1],q[1][0],q[1][1],q[2][0],q[2][1]))
            pl.append('\t200 3 %d %d %d %.5f %.5f %.5f %.5f %.5f %.5f'%(tri[0],tri[2],tri[1],q[0][0],q[0][1],q[2][0],q[2][1],q[1][0],q[1][1]))
        L+=['POLYGONS %d'%len(pl)]+pl
        if li==1:
            R,p=Ts[0][spec['armbones'][0][2] if sel else ARMB[0][2]]
            h=C@p*SC-C@Ts[0][0][1]*SC
            L+=['CONNECTORS 1','\t%.2f %.2f %.2f'%tuple(h)]
        L.append('ANIMOBJECT %d 0 %d'%(ms,N))
        for fr in frames:
            R,p=Ts[fr][b];RE,pE=toE(R,p);rx,ry,rz=solve(RE)
            L.append('          %d %d %d %d %d %d %d 1 1 1'%(fr,round(pE[0]*SC*1000),round(pE[2]*SC*1000),round(pE[1]*SC*1000),round(rx*1000),round(ry*1000),round(rz*1000)))
    return '\n'.join(L)+'\n'
ws_base=ws['BabaMG']
ps['MFWalker']=dict(json.loads(json.dumps(ps['BaBaLegs'])),id='MFWalker',name='Walker',speed=75,turnSpeed=80,spinSpeed=300,hitpointPctOfBody=50,weight=100,designable=1)
names=[];cnt=0
for f,d in FILES.items():
    FA=anim.load_anims('/home/claude/w/mf_src/RobotParts%s.tbd'%f)
    fire=FA[0] if FA else WALK
    for ai,ag in enumerate(d['cls']['arm'],1):
        t=BT[cnt%len(BT)];lg=BL[cnt%len(BL)];cnt+=1
        # Gegenarm aus Basic mit entgegengesetzter Seite
        side=ag.get('side',1);opp=[a for a in BA if a.get('side',1)==-side] or BA
        og=opp[cnt%len(opp)]
        aR,aL=(ag,og) if side>0 else (og,ag)
        spec=mkspec(t,lg,aR,aL,ARMB)
        objsrc={};
        for k in t['ks']:objsrc[('T',k)]=B['O'][k]
        for k in lg['ks']:objsrc[('L',k)]=B['O'][k]
        for k in aR['ks']:objsrc[('A0',k)]=(d if aR is ag else B)['O'][k]
        for k in aL['ks']:objsrc[('A1',k)]=(d if aL is ag else B)['O'][k]
        keys=list(objsrc);im,pos,H=pieout.atlas(objsrc,P,keys)
        pg='page-%d-mfw%s%d.png'%(700+cnt,f.lower()[:6],ai);im.save(os.path.join(TP,pg))
        lv=levels_for(t,lg,aR,aL)
        nm='mfw_%s_%d'%(f.lower(),ai)
        open(os.path.join(BD,nm+'.pie'),'w').write(build(nm,pg,pos,H,objsrc,lv,spec,IDLE,IDLE['nf'],msof(IDLE),[(1,nm+'_run.pie'),(2,nm+'_fire.pie'),(3,nm+'_die.pie')]))
        open(os.path.join(BD,nm+'_run.pie'),'w').write(build(nm,pg,pos,H,objsrc,lv,spec,WALK,WALK['nf'],msof(WALK),[]))
        open(os.path.join(BD,nm+'_fire.pie'),'w').write(build(nm,pg,pos,H,objsrc,lv,spec,fire,fire['nf'],msof(fire),[]))
        dth=DEATHS[cnt%5];open(os.path.join(BD,nm+'_die.pie'),'w').write(build(nm,pg,pos,H,objsrc,lv,spec,dth,dth['nf'],msof(dth),[]))
        spaced=re.sub(r'(?<!^)(?=[A-Z])',' ',f)
        bid='MFW_%s_%d'%(f,ai);b=json.loads(json.dumps(bs['Body5REC-Scav']))
        b.update({'id':bid,'name':'Combot %s %d'%(spaced,ai),'model':nm+'.pie','designable':1,'size':'HEAVY','hitpoints':700,'armourKinetic':30,'armourHeat':25,'weight':3500,'powerOutput':4500,'weaponSlots':1});bs[bid]=b
        names.append(bid)
        if ai==1:
            wid='MFW_'+f;w=json.loads(json.dumps(ws.get({'GattlingGun':'BabaHeavyMG','HomingMissile':'BabaRocket','LongMissile':'BabaRocket','DrunkMissile':'BabaRocket','Howitzer':'BabaCannon','Flak':'BabaCannon','CarpetBomb':'BabaCannon','PlasmaCannon':'BabaCannon'}.get(f,'BabaMG'),ws_base)))
            w.update({'id':wid,'name':spaced,'designable':1,'damage':int(w.get('damage',10)*2)});w.pop('model',None);w.pop('mountModel',None);ws[wid]=w
            x=json.loads(json.dumps(tp['BarbarianBuggy']));x.update({'id':'MFW_%s_T'%f,'name':'Combot %s'%spaced,'body':bid,'propulsion':'MFWalker','weapons':[wid],'available':True});tp[x['id']]=x
            names.append(wid)
for f,o in ((bp,bs),(wp,ws),(tp_,tp),(pp,ps)):json.dump(o,open(f,'w'),indent='\t',sort_keys=True)
js="\n\ttry { var _mw=%s; for (var _i=0;_i<_mw.length;_i++) { makeComponentAvailable(_mw[_i], %%s); } } catch (e) {}\n"%json.dumps(names+['MFWalker'])
g=os.path.join(OUT,'multiplay','script','rules','events','gameinit.js');s=open(g).read()
s=re.sub(r"\n\t\ttry \{ var _mw=.*?\} catch \(e\) \{\}\n","\n",s,flags=re.S)
s=s.replace("setupComponents(playnum);","setupComponents(playnum);"+(js%'playnum').replace('\n\t','\n\t\t'),1);open(g,'w').write(s)
r=os.path.join(OUT,'script','rules.js');s=open(r).read()
s=re.sub(r"\n\ttry \{ var _mw=.*?\} catch \(e\) \{\}\n","\n",s,flags=re.S)
s=s.replace("try { changePlayerColour(selectedPlayer, 1); } catch (e) {}","try { changePlayerColour(selectedPlayer, 1); } catch (e) {}"+(js%'selectedPlayer'),1);open(r,'w').write(s)
print('Animierte Combots',cnt)
