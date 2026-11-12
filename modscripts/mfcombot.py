"""Metal-Fatigue-Combots (alle RobotParts*.tbd): Torso = Rumpf, Beine = Antrieb, Arme = Waffen. mfcombot.py OUT"""
import sys,os,json,math,glob,re;sys.path.insert(0,'/home/claude/w/mf_src')
import tbd,pieout,asm,numpy as np
OUT=sys.argv[1]
SC=float(os.environ.get('MFCSC',1.8));HIP=13.0
P=tbd.palettes('/home/claude/w/mf_src/GobjectPalettes.tbd')[7]
TP=os.path.join(OUT,'texpages');BD=os.path.join(OUT,'components','bodies');WD=os.path.join(OUT,'components','weapons');os.makedirs(WD,exist_ok=True)
bp=os.path.join(OUT,'stats','body.json');bs=json.load(open(bp));wp=os.path.join(OUT,'stats','weapons.json');ws=json.load(open(wp))
tp_=os.path.join(OUT,'stats','templates.json');tp=json.load(open(tp_));pp=os.path.join(OUT,'stats','propulsion.json');ps=json.load(open(pp))
baseB=bs['Body5REC-Scav'];baseW=ws['BabaMG'];baseP=ps['BaBaLegs']
WMAP={'GattlingGun':'BabaHeavyMG','HomingMissile':'BabaRocket','LongMissile':'BabaRocket','DrunkMissile':'BabaRocket','Howitzer':'BabaCannon','Flak':'BabaCannon','CarpetBomb':'BabaCannon','PlasmaCannon':'BabaCannon'}
def okt(tt,k):
    v=tt.get(k)
    return None if v is None or not np.all(np.isfinite(v)) or np.abs(v).max()>30 else v
def groups(T,O):
    G=[];cur=None
    for k,(h,o) in enumerate(T):
        if h==3642917960:cur=[k];G.append(cur)
        elif h==3557481044 and cur is not None:cur.append(k)
    return G
torsos=[];legs=[];arms=[];page_no=620
for fn in sorted(glob.glob('/home/claude/w/mf_src/RobotParts*.tbd')):
    f=os.path.basename(fn)[10:-4]
    if f in ('Frame',):continue
    data,T=tbd.load(fn);O=tbd.objects(data,T);S=[o for h,o in T]+[len(data)]
    tt={k:asm.tailtriple(data,S,O,k) for k in O}
    for k in list(O):
        if O[k]['bpp']!=8:del O[k]
    keys=sorted(O)
    if not keys:continue
    im,pos,H=pieout.atlas(O,P,keys);page='page-%d-mf%s.png'%(page_no,f.lower());page_no+=1;im.save(os.path.join(TP,page))
    n_t=n_l=n_a=0
    for g in groups(T,O):
        ks=[k for k in g[1:] if k in O]
        if not ks:continue
        V=np.array([v[:3] for k in ks for x in O[k]['meshes'] for v in x['V']]);mn,mx=V.min(0),V.max(0);w=mx[0]-mn[0]
        parts0=[(O[k],k,(0,0,0),False) for k in ks]
        if w>=8 and mx[2]>5 and mn[2]>-3 and len(ks)<=7:      # Torso
            n_t+=1;xs=max(abs(mn[0]),abs(mx[0]))*0.92;zsh=mx[2]*0.72
            txt,W=pieout.pie_parts(parts0,pos,H,page,SC,origin=(0,0,-HIP),connectors=[(-xs*SC,(zsh+HIP)*SC,-1.3*SC),(xs*SC,(zsh+HIP)*SC,-1.3*SC)])
            torsos.append(dict(id='MFT_%s_%d'%(f,n_t),name='Torso %s %d'%(f,n_t),pie='mft_%s_%d.pie'%(f.lower(),n_t),txt=txt))
        elif len(ks)>=7 and mn[2]<=-9 and w<=8:   # Beine
            n_l+=1;kz=max(0.55,min(1.3,HIP/-mn[2]))
            par=[(O[k],k,(0,0,0),False) for k in ks]
            # Beide Beine: rechts normal (x+2.6), links gespiegelt (x-2.6)
            par=[(o,k,(2.6,0,0),False) for o,k,_,_ in par]+[(o,k,(-2.6,0,0),True) for o,k,_,_ in par]
            txt,W=pieout.pie_parts(par,pos,H,page,SC,origin=(0,0,mn[2]))
            # vertikal auf HIP skalieren
            L=txt.split('\n');nn=int(L[5].split()[1])
            for i in range(nn):
                x,y,z=map(float,L[6+i].split());L[6+i]='\t%.2f %.2f %.2f'%(x,y*kz,z)
            legs.append(dict(id='MFL_%s_%d'%(f,n_l),name='Legs %s %d'%(f,n_l),pie='mfl_%s_%d.pie'%(f.lower(),n_l),txt='\n'.join(L)))
        else:                                       # Arm (Kette)
            n_a+=1;parts=[];o=np.zeros(3)
            for k in ks:
                parts.append((O[k],k,tuple(o),False));t=okt(tt,k)
                if t is not None:o=o+t
            txt,W=pieout.pie_parts(parts,pos,H,page,SC,origin=(0,0,0))
            arms.append(dict(id='MFA_%s_%d'%(f,n_a),name='%s Arm %d'%(re.sub(r'(?<!^)(?=[A-Z])',' ',f),n_a),pie='mfa_%s_%d.pie'%(f.lower(),n_a),txt=txt,base=WMAP.get(f,'BabaMG')))
print('Torsos',len(torsos),'Beine',len(legs),'Arme',len(arms))
# Dateien + Stats
for t in torsos:
    open(os.path.join(BD,t['pie']),'w').write(t['txt'])
for l in legs:
    open(os.path.join(BD,l['pie']),'w').write(l['txt'])
    p=json.loads(json.dumps(baseP));p.update({'id':l['id'],'name':l['name'],'designable':1});ps[l['id']]=p
for a in arms:
    open(os.path.join(WD,a['pie']),'w').write(a['txt'])
    w=json.loads(json.dumps(ws.get(a['base'],baseW)));w.update({'id':a['id'],'name':a['name'],'model':a['pie'],'designable':1,'damage':int(w.get('damage',10)*2)});w.pop('mountModel',None);ws[a['id']]=w
extra={l['id']:{'left':l['pie']} for l in legs}
for t in torsos:
    b=json.loads(json.dumps(baseB));b.update({'id':t['id'],'model':t['pie'],'name':t['name'],'designable':1,'hitpoints':300,'weaponSlots':2});b['propulsionExtraModels']=extra;bs[t['id']]=b
# Vorlagen: einige fertige Kombinationen
def pick(lst,pref):
    for x in lst:
        if x['id'].startswith(pref):return x
    return lst[0]
tb=tp['BarbarianBuggy'];tpl=[]
tors=[t for t in torsos if t['id'].startswith('MFT_Basic')]+torsos[:1]
lgs=[l for l in legs if l['id'].startswith('MFL_Basic')]+legs[:1]
arm_sets=[[a for a in arms if a['id'].startswith('MFA_'+f)] for f in ('Basic','Axe','GattlingGun','Laser','Flak','HomingMissile')]
n=0
for i,t in enumerate(tors[:3]):
    for j,asets in enumerate(arm_sets):
        if not asets:continue
        n+=1;a=asets[(i+j)%len(asets)];l=lgs[(i+j)%len(lgs)]
        tid='MFCombot%02d'%n;x=json.loads(json.dumps(tb));x.update({'id':tid,'name':'Combot %d'%n,'body':t['id'],'propulsion':l['id'],'weapons':[a['id'],a['id']],'available':True});tp[tid]=x
for f,o in ((bp,bs),(wp,ws),(tp_,tp),(pp,ps)):json.dump(o,open(f,'w'),indent='\t',sort_keys=True)
# Freischalten
ids=[t['id'] for t in torsos]+[l['id'] for l in legs]+[a['id'] for a in arms]
js="\n\ttry { var _mf=%s; for (var _i=0;_i<_mf.length;_i++) { makeComponentAvailable(_mf[_i], %%s); } } catch (e) {}\n"%json.dumps(ids)
g=os.path.join(OUT,'multiplay','script','rules','events','gameinit.js');s=open(g).read()
s=re.sub(r"\n\t\ttry \{ var _mf=.*?\} catch \(e\) \{\}\n","\n",s,flags=re.S)
s=s.replace("setupComponents(playnum);","setupComponents(playnum);"+(js%'playnum').replace('\n\t','\n\t\t'),1);open(g,'w').write(s)
r=os.path.join(OUT,'script','rules.js');s=open(r).read()
s=re.sub(r"\n\ttry \{ var _mf=.*?\} catch \(e\) \{\}\n","\n",s,flags=re.S)
s=s.replace("try { changePlayerColour(selectedPlayer, 1); } catch (e) {}","try { changePlayerColour(selectedPlayer, 1); } catch (e) {}"+(js%'selectedPlayer'),1);open(r,'w').write(s)
print('fertig, Vorlagen',n)
