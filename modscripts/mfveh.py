"""Metal-Fatigue-Fahrzeuge als Rumpf + Fabrikvorlage. mfveh.py OUT"""
import sys,os,json;sys.path.insert(0,'/home/claude/w/mf_src')
import tbd,pieout,numpy as np
OUT=sys.argv[1];B=os.path.join(OUT,'components','bodies');TP=os.path.join(OUT,'texpages')
data,T=tbd.load('/home/claude/w/mf_src/3DVehicles.tbd');O=tbd.objects(data,T);P=tbd.palettes('/home/claude/w/mf_src/GobjectPalettes.tbd')[7]
keys=[]
for k in sorted(O):
    V=np.array([v[:3] for x in O[k]['meshes'] for v in x['V']]);e=V.max(0)-V.min(0)
    if e.max()>=6 and len(V)>=8 and O[k]['bpp']==8 and O[k]['tex'] is not None:keys.append(k)
im,pos,H=pieout.atlas(O,P,keys);page='page-601-mfveh.png';im.save(os.path.join(TP,page))
bp=os.path.join(OUT,'stats','body.json');bs=json.load(open(bp));tp_=os.path.join(OUT,'stats','templates.json');tp=json.load(open(tp_))
base=bs['B3body-sml-buggy01'];tb=tp['BarbarianBuggy']
for n,k in enumerate(keys,1):
    mid='mfveh%02d'%n
    open(os.path.join(B,mid+'.pie'),'w').write(pieout.pie(O[k],pos[k],H,page,sc=float(os.environ.get('MFSC',3.5))))
    b=json.loads(json.dumps(base));b.update({'id':'B3MFV%02d'%n,'model':mid+'.pie','name':'MF %02d'%n});bs['B3MFV%02d'%n]=b
    t=json.loads(json.dumps(tb));t.update({'id':'MFV%02d'%n,'name':'MF %02d'%n,'body':'B3MFV%02d'%n,'available':True});tp['MFV%02d'%n]=t
json.dump(bs,open(bp,'w'),indent='\t',sort_keys=True);json.dump(tp,open(tp_,'w'),indent='\t',sort_keys=True)
print('MF-Fahrzeuge',len(keys),keys)
