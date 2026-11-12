import sys,struct,numpy as np
sys.path.insert(0,'/home/claude/w/mf_src')
import tbd
def sg(v):v=v.astype(int);return np.where(v>=512,v-1024,v)*360/1024.0
def load_anims(fn):
    data,T=tbd.load(fn);S=sorted(o for h,o in T)+[len(data)];out=[]
    for i,(h,o) in enumerate(T):
        if h!=78887060:continue
        nxt=min(x for x in S if x>o);sz=nxt-o;b=data[o:nxt]
        w=struct.unpack('<%dI'%(sz//4),b);nf=w[8]
        hdr=sz-76*nf
        if nf<2 or hdr<0 or hdr%4:continue
        rec=np.frombuffer(b[hdr:],'<u4').reshape(nf,19)
        t=rec[:,0].view('<f4');pos=rec[:,1:4].view('<f4')
        if not (np.all(np.isfinite(t)) and abs(t[0])<1e-3 and np.all(np.diff(t)>0)):continue
        ang=np.zeros((nf,15,3))
        for c in range(15):
            wd=rec[:,4+c];ang[:,c,0]=sg(wd&0x3ff);ang[:,c,1]=sg((wd>>10)&0x3ff);ang[:,c,2]=sg((wd>>20)&0x3ff)
        out.append(dict(idx=i,nf=nf,t=t.copy(),pos=pos.copy(),ang=ang,hdr=struct.unpack('<%dI'%(hdr//4),b[:hdr])))
    return out
