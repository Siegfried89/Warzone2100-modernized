import sys,os,itertools,importlib;sys.path.insert(0,'/home/claude/w');sys.path.insert(0,'/home/claude/exe')
import numpy as np
res=[]
for o in itertools.permutations('xyz'):
  for s in itertools.product((1,-1),repeat=3):
    os.environ['MFORD']=''.join(o);os.environ['MFSGN']=','.join(map(str,s))
    import mfskel as M;importlib.reload(M)
    import anim,prevskel as P;importlib.reload(P)
    F=P.F;A=anim.load_anims('/home/claude/w/mf_src/RobotPartsBasic.tbd')[0]
    t=F['cls']['torso'][0];l=F['cls']['leg'][2];aR=F['cls']['arm'][0];aL=F['cls']['arm'][3]
    sp=P.mkspec(t,l,aR,aL,((9,10,11),(12,13,14)))
    ups=[];fz=[]
    for f in range(A['nf']):
        T=M.pose(sp,A['ang'][f],A['pos'][f])
        ups.append((T[7][0]@np.array([0,0,1.]))[2])
        fz.append(min(T[3][1][2],T[6][1][2]))
    res.append((np.mean(ups),np.std(fz),''.join(o),s))
res.sort(key=lambda r:-r[0]+r[1]*0.05)
for r in res[:8]:print(r)
