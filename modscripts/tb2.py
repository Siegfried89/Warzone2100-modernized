import struct,numpy as np
from PIL import Image
from tb import load,obj_words,sgn
def px(buf,fmt):
    a=np.frombuffer(buf,'<u2').astype(np.uint32)
    if fmt=='565':R=((a>>11)&31)*255//31;G=((a>>5)&63)*255//63;B=(a&31)*255//31;A=np.full_like(a,255)
    elif fmt=='1555':A=np.where(a>>15,255,0);R=((a>>10)&31)*255//31;G=((a>>5)&31)*255//31;B=(a&31)*255//31
    elif fmt=='4444':A=((a>>12)&15)*17;R=((a>>8)&15)*17;G=((a>>4)&15)*17;B=(a&15)*17
    return np.stack([R,G,B,A],-1).astype(np.uint8)
def render(data,W,fmt,tbl=None):
    iw,ih,n=W[7],W[8],W[9]
    tb=tbl if tbl is not None else (W[10] if W[10] else None)
    return iw,ih,n,tb
def render_obj(data,T,i,fmt,base=None):
    W=obj_words(data,T,i);iw,ih,n=W[7],W[8],W[9]
    off=T[i][1]
    tb=W[10] if W[10] else off+48
    ox,oy=sgn(W[5]&0xffff),sgn(W[6]&0xffff)
    big=Image.new('RGBA',(iw+160,ih+160),(0,0,0,0));B=64
    for k in range(n):
        pos,o=struct.unpack('<II',data[tb+8*k:tb+8*k+8])
        x=sgn(pos&0xffff)-ox;y=sgn(pos>>16)-oy
        if o+2048>len(data):continue
        t=Image.fromarray(px(data[o:o+2048],fmt).reshape(32,32,4),'RGBA')
        big.alpha_composite(t,(x+B,y+B))
    im=big.crop((B,B,B+iw,B+ih))
    return im
if __name__=='__main__':
    import sys
    data,T=load('TBD/toolbarX.tbd')
    import os
    os.makedirs('png',exist_ok=True)
    for i in (3,4,1):
        for fmt in('565','1555','4444'):
            render_obj(data,T,i,fmt).save('png/x%d_%s.png'%(i,fmt))
