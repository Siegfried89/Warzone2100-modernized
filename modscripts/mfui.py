"""MF-Oberflaeche (toolbarX/Menu/misc) -> PNGs + images/mfui.img.  mfui.py OUT"""
import sys,os;sys.path.insert(0,'/home/claude/w')
import numpy as np
from PIL import Image
from tb import load,obj_words
from tb2 import render_obj
OUT=sys.argv[1]
D=os.path.join(OUT,'images','mfui');os.makedirs(D,exist_ok=True)
names=[]
def keyed(im,key=True):
    a=np.array(im)
    if key:
        m=(a[...,0]==0)&(a[...,1]==0)&(a[...,2]==0)
        a[m,3]=0
    return Image.fromarray(a,'RGBA')
def put(name,im,key=True):
    keyed(im,key).save(os.path.join(D,name+'.png'));names.append(name)
X,TX=load('/home/claude/w/mf_ui/toolbarX.tbd')
M,TM=load('/home/claude/w/mf_ui/Menu.tbd')
S,TS=load('/home/claude/w/mf_ui/misc.tbd')
R=lambda d,t,i:render_obj(d,t,i,'565')
put('panel',R(X,TX,20))
for n,i in zip(('tab_arms','tab_legs','tab_torso','tab_all'),(27,28,29,30)):put(n,R(X,TX,i))
for k,n in enumerate(('melee','all','ranged','stay','tether','pursue')):
    put(n+'_off',R(X,TX,31+2*k));put(n+'_on',R(X,TX,32+2*k))
for n,i in zip(('attack','guard','stop','move','patrol','repair','raze','drop','drill','invade','evac','evaccrew','deploy','launch','send','primary','retfire','clear','rally','sell'),range(43,63)):put('b_'+n,R(X,TX,i))
put('prebuild',R(X,TX,64))
for n,i in zip(('stat_a','stat_b','stat_c','stat_d','resbar','disp_a','disp_b'),(23,24,25,26,22,19,21)):put(n,R(X,TX,i))
for n,i in zip(('arr_up','arr_up_hl','arr_dn','arr_dn_hl'),(132,133,135,136)):put(n,R(S,TS,i))
_mp=R(M,TM,245);put('metalpanel',_mp.crop(_mp.getbbox()),key=False)
open(os.path.join(OUT,'images','mfui.img'),'w').write(''.join('0,0,%s.png\n'%n for n in names))
print('MF-UI Bilder',len(names))
