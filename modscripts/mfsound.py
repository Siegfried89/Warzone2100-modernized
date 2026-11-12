"""Metal-Fatigue-Sounds (sound.tbd) -> ogg, audio.json-Eintraege, Waffen-Sounds. mfsound.py OUT"""
import sys,os,re,json,struct,wave,subprocess,numpy as np
OUT=sys.argv[1];SD='/home/claude/w/mf_src/snd';AD=os.path.join(OUT,'audio','mf');os.makedirs(AD,exist_ok=True)
d=open('/home/claude/w/mf_src/sound.tbd','rb').read()
pos=[m.start() for m in re.finditer(b'RIFF',d)];feat=[];n=0
import io
for p in pos[1:]:
    if d[p+8:p+12]!=b'WAVE':continue
    sz=struct.unpack('<I',d[p+4:p+8])[0];blob=d[p:p+8+sz];tag=struct.unpack('<H',blob[20:22])[0]
    if tag!=1:n+=1;continue
    w=wave.open(io.BytesIO(blob));sr=w.getframerate();x=np.frombuffer(w.readframes(w.getnframes()),'<i2').astype(float)/32768
    if len(x)<100:n+=1;continue
    env=np.abs(x);pk=env.max();ip=int(np.argmax(env));att=ip/sr;win=int(sr*0.01)+1;sm=np.convolve(env,np.ones(win)/win,'same')
    b=np.where(sm[ip:]<0.1*pk)[0];dec=(b[0]/sr) if len(b) else len(x)/sr-att
    spec=np.abs(np.fft.rfft(x));fr=np.fft.rfftfreq(len(x),1/sr);cen=(spec*fr).sum()/spec.sum()
    # Sprachanteil: Huellkurve schwankt stark ueber Zeit
    fl=int(sr*0.05);fr_=np.array([env[i:i+fl].mean() for i in range(0,len(env)-fl,fl)]) if len(env)>fl*4 else np.array([1,1])
    mod=fr_.std()/(fr_.mean()+1e-9)
    feat.append(dict(n=n,dur=len(x)/sr,att=att,dec=dec,cen=cen,mod=mod,blob=blob));n+=1
print(len(feat),'PCM-Sounds')
def cvt(s):
    wavf=os.path.join(AD,'mfs%03d.wav'%s['n']);open(wavf,'wb').write(s['blob'])
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',wavf,'-ac','1','-c:a','libvorbis','-q:a','3',os.path.join(AD,'mfs%03d.ogg'%s['n'])],check=True);os.remove(wavf)
shots=sorted([s for s in feat if s['dur']<=0.9 and s['att']<0.1 and s['cen']>1300 and s['mod']<2.0],key=lambda s:-s['cen'])
heavy=sorted([s for s in feat if 0.4<=s['dur']<=2.5 and s['att']<0.15 and s['dec']>0.3 and s['cen']<1500 and s['mod']<2.0],key=lambda s:s['cen'])
print('Schuss-Kandidaten',[s['n'] for s in shots][:20],'Explosion-Kandidaten',[s['n'] for s in heavy][:20])
use=set(s['n'] for s in shots[:24]+heavy[:24]);
for s in feat:
    if s['n'] in use:cvt(s)
# audio.json erweitern (Kopie der Basis)
a=json.load(open('/tmp/claude-0/cmp/base/audio/audio.json'))
a['MFSounds']={'_name':'Metal Fatigue sounds','data':[{'fileName':'mfs%03d.ogg'%n,'loop':False,'range':1800,'volume':100,'z_comment':'MF sample %d'%n} for n in sorted(use)]}
# Combot-Sounds (vom Nutzer zugeordnet): 64 = Bewegungsbefehl (auch Auswahl), 105 = Angriffsbefehl
import shutil
a['MFSounds']['data']=[e for e in a['MFSounds']['data'] if not e['fileName'].startswith(('mfclick','mfui'))]
# Combot-Designer (Zuordnung nach Klanganalyse): 8 = kurzes Klacken (Teil einsetzen), 47 = Rasseln (Teil entfernen), 11 = Klonk, 48 = Fertig
for src_n,dst in ((64,'mfclick_move.ogg'),(105,'mfclick_attack.ogg'),(8,'mfui_attach.ogg'),(47,'mfui_detach.ogg'),(11,'mfui_clank.ogg'),(48,'mfui_done.ogg')):
    src=os.path.join(AD,'mfs%03d.ogg'%src_n)
    if not os.path.exists(src):
        for s_ in feat:
            if s_['n']==src_n:cvt(s_)
    shutil.copy(src,os.path.join(AD,dst))
    a['MFSounds']['data'].append({'fileName':dst,'loop':False,'range':1800,'volume':100,'z_comment':'Combot '+dst})
os.makedirs(os.path.join(OUT,'audio'),exist_ok=True);json.dump(a,open(os.path.join(OUT,'audio','audio.json'),'w'),indent=1)
# Waffen-Sounds zuordnen
ws_p=os.path.join(OUT,'stats','weapons.json');ws=json.load(open(ws_p))
KIND={'Axe':'melee','BladeArm':'melee','KatanaSword':'melee','LaserSword':'melee','HammerHand':'melee','PowerFist':'melee','MultiClaw':'melee','RotaryBlade':'melee','ElectroGrip':'melee',
 'GattlingGun':'gun','PowerGun':'gun','Tracer':'gun','Basic':'gun','Armor':'gun','Camo':'gun',
 'Howitzer':'heavy','Flak':'heavy','CarpetBomb':'heavy','PlasmaCannon':'heavy','BlastPulse':'heavy','PowerPulse':'heavy','ProtoBlast':'heavy',
 'HomingMissile':'missile','LongMissile':'missile','DrunkMissile':'missile',
 'Laser':'energy','EnergyBlaster':'energy','SniperLaser':'energy','EMP':'energy'}
gi=0
def pickl(lst,i):return 'mfs%03d.ogg'%lst[i%len(lst)]['n']
sh=[s for s in shots[:24]];hv=[s for s in heavy[:24]]
for k,v in list(ws.items()):
    m=re.match(r'MF[WA]_([A-Za-z]+)',k)
    if not m:continue
    kind=KIND.get(m.group(1),'gun');gi+=1
    if kind=='gun':v['weaponWav']=pickl(sh[:12],gi);
    elif kind=='energy':v['weaponWav']=pickl(sh[:8],gi)
    elif kind=='melee':v['weaponWav']=pickl(sh[12:24] or sh,gi)
    elif kind=='heavy':v['weaponWav']=pickl(hv[:12],gi);v['explosionWav']=pickl(hv[12:24] or hv,gi)
    elif kind=='missile':v['weaponWav']=pickl(hv[:8],gi);v['explosionWav']=pickl(hv[8:16] or hv,gi)
json.dump(ws,open(ws_p,'w'),indent='\t',sort_keys=True);print('Waffen mit Sound',gi)
