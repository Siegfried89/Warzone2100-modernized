import sys,os;sys.path.insert(0,'/home/claude/w');sys.path.insert(0,'/home/claude/exe')
import numpy as np,mfskel as M,anim,tbd,asm,rendfk
from PIL import Image
F=M.load_file('/home/claude/w/mf_src/RobotPartsBasic.tbd');O=F['O']
P=tbd.palettes('/home/claude/w/mf_src/GobjectPalettes.tbd')[7]
def tris(k,mir=False):return asm.part_tris(O[k],P,mirror=False)
def mkspec(t,l,aR,aL,armbones):
    ar=M.arm_offsets(aR);al=M.arm_offsets(aL)
    if not any(np.any(x) for x in al):al=[x*[-1,1,1] for x in ar]
    return dict(lo=M.leg_offsets(l),to=M.torso_offsets(t),aR=ar,aL=al,armbones=armbones,t=t,l=l,aRg=aR,aLg=aL)
def render_pose(spec,T):
    out=[]
    ks=spec['l']['ks'];tk=spec['t']['ks']
    parts=[(0,ks[0]),(1,ks[1]),(2,ks[2]),(3,ks[3]),(4,ks[4]),(5,ks[5]),(6,ks[6]),(7,tk[0]),(8,tk[1])]
    for b,k in parts:
        if k is None:continue
        R,p=T[b];out+=[((R@q.T).T+p,c) for q,c in tris(k)]
    for bones,g in ((spec['armbones'][0],spec['aRg']),(spec['armbones'][1],spec['aLg'])):
        for i,b in enumerate(bones):
            R,p=T[b];out+=[((R@q.T).T+p,c) for q,c in tris(g['ks'][i])]
    return out
if __name__=='__main__':
    A=anim.load_anims('/home/claude/w/mf_src/RobotPartsBasic.tbd')[0]
    t=F['cls']['torso'][0];l=F['cls']['leg'][2];aR=F['cls']['arm'][0];aL=F['cls']['arm'][3]
    frames=[0,8,16,24,32,40]
    rows=[]
    for ab in (((9,10,11),(12,13,14)),((12,13,14),(9,10,11))):
        spec=mkspec(t,l,aR,aL,ab)
        row=Image.new('RGB',(300*len(frames),600))
        for i,f in enumerate(frames):
            T=M.pose(spec,A['ang'][f],A['pos'][f]);tr=render_pose(spec,T)
            row.paste(rendfk.draw(tr,'side',scale=9),(i*300,0));row.paste(rendfk.draw(tr,'front',scale=9),(i*300,300))
        rows.append(row)
    W=Image.new('RGB',(300*len(frames),1200));W.paste(rows[0],(0,0));W.paste(rows[1],(0,600));W.save('/home/claude/w/skel_prev.png')
