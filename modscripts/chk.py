import sys,re,numpy as np,math
sys.path.insert(0,'/home/claude/w')
from PIL import Image,ImageDraw
fn=sys.argv[1];fr=int(sys.argv[2])
L=open(fn).read().split('\n');i=0;levels=[];cur=None
while i<len(L):
    l=L[i].split()
    if l and l[0]=='POINTS':
        n=int(l[1]);cur=dict(pts=np.array([[float(x) for x in L[i+1+k].split()] for k in range(n)]),polys=[]);levels.append(cur);i+=n
    elif l and l[0]=='POLYGONS':
        n=int(l[1]);cur['polys']=[[int(x) for x in L[i+1+k].split()[2:5]] for k in range(0,n,2)];i+=n
    elif l and l[0]=='ANIMOBJECT':
        n=int(l[3]);cur['anim']=[[float(x) for x in L[i+1+k].split()[1:7]] for k in range(n)];i+=n
    i+=1
def Rx(a):c,s=math.cos(a),math.sin(a);return np.array([[1,0,0],[0,c,-s],[0,s,c]])
def Ry(a):c,s=math.cos(a),math.sin(a);return np.array([[c,0,s],[0,1,0],[-s,0,c]])
def Rz(a):c,s=math.cos(a),math.sin(a);return np.array([[c,-s,0],[s,c,0],[0,0,1]])
W=Image.new('RGB',(600,600),'white');d=ImageDraw.Draw(W)
for lv in levels:
    a=lv['anim'][fr];px,py,pz=a[0]/1000,a[1]/1000,a[2]/1000;rx,ry,rz=[math.radians(v/1000) for v in a[3:6]]
    R=Rx(-rx)@Ry(-rz)@Rz(-ry);pos=np.array([px,pz,py])
    P=(R@lv['pts'].T).T+pos
    for t in lv['polys']:
        q=P[t];d.polygon([(300+x*8,560-y*8) for x,y,z in q],outline='black',fill=(200,120,120))
W.save('/home/claude/w/chk_%s.png'%sys.argv[3])
