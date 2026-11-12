"""Komplette animierte Metal-Fatigue-Combots (Lauf- und Feuer-Animation). mfanim.py OUT"""
import sys,os,json,math,glob,re;sys.path.insert(0,'/home/claude/w/mf_src')
import tbd,pieout,asm,numpy as np
OUT=sys.argv[1];SC=float(os.environ.get('MFCSC',1.8))
P=tbd.palettes('/home/claude/w/mf_src/GobjectPalettes.tbd')[7]
TP=os.path.join(OUT,'texpages');BD=os.path.join(OUT,'components','bodies')
bp=os.path.join(OUT,'stats','body.json');bs=json.load(open(bp));wp=os.path.join(OUT,'stats','weapons.json');ws=json.load(open(wp))
tp_=os.path.join(OUT,'stats','templates.json');tp=json.load(open(tp_));pp=os.path.join(OUT,'stats','propulsion.json');ps=json.load(open(pp))
def okt(tt,k):
    v=tt.get(k);return None if v is None or not np.all(np.isfinite(v)) or np.abs(v).max()>30 else v
def groups(T):
    G=[];cur=None
    for k,(h,o) in enumerate(T):
        if h==3642917960:cur=[k];G.append(cur)
        elif h==3557481044 and cur is not None:cur.append(k)
    return G
F={}
for fn in sorted(glob.glob('/home/claude/w/mf_src/RobotParts*.tbd')):
    f=os.path.basename(fn)[10:-4]
    if f=='Frame':continue
    data,T=tbd.load(fn);O=tbd.objects(data,T);S=[o for h,o in T]+[len(data)]
    for k in list(O):
        if O[k]['bpp']!=8:del O[k]
    tt={k:asm.tailtriple(data,S,O,k) for k in O}
    cls={'torso':[],'leg':[],'arm':[]}
    for g in groups(T):
        ks=[k for k in g[1:] if k in O]
        if not ks:continue
        V=np.array([v[:3] for k in ks for x in O[k]['meshes'] for v in x['V']]);mn,mx=V.min(0),V.max(0);w=mx[0]-mn[0]
        if w>=8 and mx[2]>5 and mn[2]>-3 and len(ks)<=7:c='torso'
        elif len(ks)>=7 and mn[2]<=-9 and w<=8:c='leg'
        else:c='arm'
        cls[c].append(dict(ks=ks,mn=mn,mx=mx))
    F[f]=dict(O=O,tt=tt,cls=cls)
BT=F['Basic']['cls']['torso'];BL=F['Basic']['cls']['leg']
print('Basis',len(BT),len(BL))
def walk_fn(side,kind,phase):
    A=26 if kind=='leg' else 22
    def fn(i,N=24):
        a=math.sin(2*math.pi*i/N+phase)
        if kind=='leg':return (0,0,0),(side*A*a,0,0)
        if kind=='arm':return (0,0,0),(-side*A*a,0,0)
        return (0,0.12*abs(math.cos(2*math.pi*i/N))*SC,0),(0,side*3*a,0)   # Torso: Wippen
    return fn
def fire_fn(kind,side):
    def fn(i,N=8):
        k=[0,1,2,3,3,2,1,0][i%N]/3.0
        if kind=='rarm' :return (0,0,-0.5*SC*(1 if i in(3,4) else 0)),(-75*k,0,0)
        if kind=='larm' :return (0,0,0),(-30*k,0,0)
        if kind=='torso':return (0,0,0.25*SC*(1 if i==3 else 0)),(-4*k,8*k*side,0)
        return (0,0,0),(0,0,0)
    return fn
n_total=0;names=[]
ws_base=ws['BabaMG']
ps['MFWalker']=dict(json.loads(json.dumps(ps['BaBaLegs'])),id='MFWalker',name='Walker',speed=75,turnSpeed=80,spinSpeed=300,hitpointPctOfBody=50,weight=100,designable=1)
done_files=set();cnt=0
for f,d in F.items():
    for ai,ag in enumerate(d['cls']['arm'],1):
        t=BT[(cnt)%len(BT)];lg=BL[(cnt)%len(BL)];cnt+=1
        # kombinierte Objekte + Atlas
        objs={};
        for k in t['ks']:objs[('T',k)]=F['Basic']['O'][k]
        for k in lg['ks']:objs[('L',k)]=F['Basic']['O'][k]
        for k in ag['ks']:objs[('A',k)]=d['O'][k]
        keys=list(objs);im,pos,H=pieout.atlas(objs,P,keys)
        pg='page-%d-mfw%s%d.png'%(700+len(done_files)*0+cnt,f.lower()[:6],ai);im.save(os.path.join(TP,pg))
        feet=min(v[2] for k in lg['ks'] for x in F['Basic']['O'][k]['meshes'] for v in x['V'])
        xs=max(abs(t['mn'][0]),abs(t['mx'][0]))*0.92;zsh=t['mx'][2]*0.72;ys=1.3
        tparts=[(objs[('T',k)],('T',k),(0,0,0),False) for k in t['ks']]
        def legparts(s):return [(objs[('L',k)],('L',k),(2.6*s,0,0),s<0) for k in lg['ks']]
        def armparts(s):
            o=np.array([xs*s,ys,zsh]);out=[]
            for k in ag['ks']:
                out.append((objs[('A',k)],('A',k),tuple(o),s<0));tv=okt(d['tt'],k)
                if tv is not None:o=o+np.array([tv[0]*s,tv[1],tv[2]])
            return out
        def build(mode):
            if mode=='idle':fn=lambda kind,s:(lambda i:((0,0,0),(0,0,0)));N=1;ms=1000
            elif mode=='run':N=24;ms=70
            else:N=8;ms=130
            def mk(kind,s):
                if mode=='idle':return lambda i:((0,0,0),(0,0,0))
                if mode=='run':return walk_fn(s,kind if kind!='torso' else 'torso',0 if kind!='torso' else 0) if kind!='larm' and kind!='rarm' else walk_fn(s,'arm',0)
                return fire_fn({'torso':'torso','leg':'leg','larm':'larm','rarm':'rarm'}[kind],s)
            lv=[dict(parts=tparts,joint=(0,0,0),anim=mk('torso',1)),
                dict(parts=legparts(1),joint=(2.6,0,0),anim=(mk('leg',1) if mode!='fire' else (lambda i:((0,0,0),(0,0,0))))),
                dict(parts=legparts(-1),joint=(-2.6,0,0),anim=(mk('leg',-1) if mode!='fire' else (lambda i:((0,0,0),(0,0,0))))),
                dict(parts=armparts(1),joint=(xs,ys,zsh),anim=(mk('rarm',1) if mode!='run' else walk_fn(-1,'arm',0))),
                dict(parts=armparts(-1),joint=(-xs,ys,zsh),anim=(mk('larm',-1) if mode!='run' else walk_fn(1,'arm',0)))]
            ev=[(1,'%s_run.pie'%nm),(2,'%s_fire.pie'%nm)] if mode=='idle' else []
            return pieout.pie_anim(lv,pos,H,pg,SC,feet,N,ms,ev,connectors=[((xs)*SC,(zsh-feet)*SC,-8.0*SC*0.4)])
        nm='mfw_%s_%d'%(f.lower(),ai)
        for mode,suffix in (('idle',''),('run','_run'),('fire','_fire')):
            open(os.path.join(BD,nm+suffix+'.pie'),'w').write(build(mode))
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
