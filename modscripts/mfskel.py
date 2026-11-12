"""MF-Skelett: Teile klassifizieren, Verbindungen lesen, Vorwaertskinematik (FK) fuer die 15 Gelenke"""
import sys,os,struct,glob,math;sys.path.insert(0,'/home/claude/w/mf_src');sys.path.insert(0,'/home/claude/exe')
import numpy as np,tbd,asm,anim
ORDER=os.environ.get('MFORD','zxy');SIGNS=tuple(int(x) for x in os.environ.get('MFSGN','1,1,1').split(','))
def Rx(a):c,s=np.cos(a),np.sin(a);return np.array([[1,0,0],[0,c,-s],[0,s,c]])
def Ry(a):c,s=np.cos(a),np.sin(a);return np.array([[c,0,s],[0,1,0],[-s,0,c]])
def Rz(a):c,s=np.cos(a),np.sin(a);return np.array([[c,-s,0],[s,c,0],[0,0,1]])
RM={'x':Rx,'y':Ry,'z':Rz}
def euler(a,order=None,signs=None):
    order=order or ORDER;signs=signs or SIGNS
    a=np.radians(a)*np.array(signs);R=np.eye(3)
    for ax in order:R=R@RM[ax](a['xyz'.index(ax)])
    return R
def quads(data,S,O,k):
    ob=O[k];end=max(x['vptr']+32*len(x['V']) for x in ob['meshes'])+ob['w']*ob['h']*ob['bpp']//8
    t=data[end:S[k+1]];n=len(t)//4
    if n<5:return []
    w=struct.unpack('<%dI'%n,t[:n*4]);f=struct.unpack('<%df'%n,t[:n*4])
    sane=lambda i:w[i]==0 or (1e-3<abs(f[i])<200)
    i=n-1
    while i>=0 and w[i]<0x200000:i-=1
    q=[]
    while i>=3 and not sane(i) and sane(i-1) and sane(i-2) and sane(i-3):
        q.append(np.array([f[i-3],f[i-2],f[i-1]]));i-=4
    return q[::-1]
def groups(T):
    G=[];cur=None
    for k,(h,o) in enumerate(T):
        if h==3642917960:cur=[k];G.append(cur)
        elif h==3557481044 and cur is not None:cur.append(k)
    return G
def load_file(fn):
    data,T=tbd.load(fn);O=tbd.objects(data,T);S=[o for h,o in T]+[len(data)]
    for k in list(O):
        if O[k]['bpp']!=8:del O[k]
    cls={'torso':[],'leg':[],'arm':[]}
    for g in groups(T):
        ks=[k for k in g[1:] if k in O]
        if not ks or len(ks)!=len(g)-1:continue
        V=np.array([v[:3] for k in ks for x in O[k]['meshes'] for v in x['V']]);mn,mx=V.min(0),V.max(0);w=mx[0]-mn[0]
        if w>=8 and mx[2]>5 and mn[2]>-3 and len(ks)<=7:c='torso'
        elif len(ks)>=7 and mn[2]<=-9 and w<=8:c='leg'
        else:c='arm'
        d=dict(ks=ks,mn=mn,mx=mx,q={k:quads(data,S,O,k) for k in ks})
        if c=='arm':
            V0=np.array([v[:3] for x in O[ks[0]]['meshes'] for v in x['V']]);d['side']=1 if V0[:,0].mean()>0 else -1
        cls[c].append(d)
    return dict(O=O,cls=cls,data=data,T=T)
def leg_offsets(g):
    ks=g['ks'];q=g['q']
    pq=q[ks[0]]
    hipR=hipL=None
    for v in pq:
        if abs(v[0])>1:
            if v[0]>0:hipR=v
            else:hipL=v
    if hipR is None and hipL is not None:hipR=hipL*[-1,1,1]
    if hipL is None and hipR is not None:hipL=hipR*[-1,1,1]
    if hipR is None:hipR=np.array([2.35,-2.3,-1.2]);hipL=hipR*[-1,1,1]
    mount=pq[-1] if pq and abs(pq[-1][0])<1 else np.zeros(3)
    def ch(k):
        v=q.get(k);return v[-1] if v else np.zeros(3)
    return dict(hipR=hipR,hipL=hipL,mount=mount,kneeA=ch(ks[1]),ankA=ch(ks[2]),kneeB=ch(ks[4]) if q.get(ks[4]) else ch(ks[1])*[-1,1,1],ankB=ch(ks[5]) if q.get(ks[5]) else ch(ks[2])*[-1,1,1])
def torso_offsets(g):
    ks=g['ks'];q=g['q'][ks[0]];shR=shL=None
    for v in q:
        if v[0]>2 and shR is None:shR=v
        if v[0]<-2 and shL is None:shL=v
    if shR is None:shR=shL*[-1,1,1] if shL is not None else np.array([5.3,1.6,6.7])
    if shL is None:shL=shR*[-1,1,1]
    return dict(shR=shR,shL=shL)
def arm_offsets(g):
    ks=g['ks'];q=g['q']
    o=[(q[k][-1] if q.get(k) else np.zeros(3)) for k in ks]
    return o
def pose(spec,ang,root,sgnA=1):
    """liefert 15 (R,p) Weltlage-Paare in MF-Koordinaten"""
    E=lambda b:euler(ang[b])
    lo,to,aR,aL=spec['lo'],spec['to'],spec['aR'],spec['aL']
    T=[None]*15
    R0=E(0);p0=np.array(root,float);T[0]=(R0,p0)
    for (b,hip,kn,an) in (((1,2,3),lo['hipR'],lo['kneeA'],lo['ankA']),((4,5,6),lo['hipL'],lo['kneeB'],lo['ankB'])):
        R=R0@E(b[0]);p=p0+R0@hip;T[b[0]]=(R,p)
        p=p+R@kn;R=R@E(b[1]);T[b[1]]=(R,p)
        p=p+R@an;R=R@E(b[2]);T[b[2]]=(R,p)
    R7=R0@E(7);p7=p0+R0@lo['mount'];T[7]=(R7,p7)
    T[8]=(R7@E(8),p7.copy())
    for (b,sh,off) in ((spec['armbones'][0],to['shR'],aR),(spec['armbones'][1],to['shL'],aL)):
        R=R7@E(b[0]);p=p7+R7@sh;T[b[0]]=(R,p)
        for i in (1,2):
            p=p+R@off[i-1];R=R@E(b[i]);T[b[i]]=(R,p)
    return T
