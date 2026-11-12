import struct,numpy as np,sys
from PIL import Image
def load(fn):
    d=open(fn,'rb').read();p=12;ch={}
    while p+8<=len(d):
        cid=d[p:p+4];sz=struct.unpack('<I',d[p+4:p+8])[0];ch[cid]=(p+8,sz);p+=8+sz+(sz&1)
    o,s=ch[b'DATA'];data=d[o:o+s];o,s=ch[b'TYPE'];n=struct.unpack('<I',d[o:o+4])[0]
    T=[struct.unpack('<II',d[o+4+8*k:o+12+8*k]) for k in range(n)]
    return data,T
def px(buf,fmt):
    a=np.frombuffer(buf,'<u2').astype(np.uint32)
    if fmt=='565':r=(a>>11)&31;g=(a>>5)&63;b=a&31;R=r*255//31;G=g*255//63;B=b*255//31;A=np.full_like(a,255)
    elif fmt=='1555':A=np.where(a>>15,255,0);R=((a>>10)&31)*255//31;G=((a>>5)&31)*255//31;B=(a&31)*255//31
    elif fmt=='4444':A=((a>>12)&15)*17;R=((a>>8)&15)*17;G=((a>>4)&15)*17;B=(a&15)*17
    return np.stack([R,G,B,A],-1).astype(np.uint8).reshape(-1,4)
def obj_words(data,T,i):
    S=sorted(set(o for h,o in T))+[len(data)];h,o=T[i];nxt=min(x for x in S if x>o);b=data[o:nxt]
    return struct.unpack('<%dI'%(len(b)//4),b)
def sgn(v):return v-(1<<16) if v&0x8000 else v
def render(data,w,i,T,fmt):
    W=obj_words(data,T,i);iw=W[7];ih=W[8];n=W[9]
    # Kachelliste: ab Wort 12, Paare (pos, offset)
    im=Image.new('RGBA',(iw,ih),(255,0,255,0))
    k=12
    cnt=0
    while k+1<len(W) and cnt<n:
        pos,off=W[k],W[k+1];x=sgn(pos&0xffff);y=sgn(pos>>16)
        if off+2048<=len(data):
            t=Image.fromarray(px(data[off:off+2048],fmt).reshape(32,32,4))
            im.alpha_composite(t,(max(x,0),max(y,0)),(max(-x,0),max(-y,0))) if x<iw and y<ih else None
        k+=2;cnt+=1
    return im
if __name__=='__main__':
    data,T=load('TBD/toolbarX.tbd')
    for i in (3,0):
        for fmt in ('565','1555','4444'):
            render(data,0,i,T,fmt).save('t_%d_%s.png'%(i,fmt))
