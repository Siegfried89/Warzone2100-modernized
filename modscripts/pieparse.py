import re,glob,os
def read(f): return open(f,encoding='latin-1',newline='').read()
def levels(text):
    lines=text.replace('\r','').split('\n'); out=[];i=0
    while i<len(lines):
        m=re.match(r'POINTS\s+(\d+)',lines[i].strip())
        if m:
            n=int(m.group(1)); out.append([tuple(float(x) for x in lines[j].split()[:3]) for j in range(i+1,i+1+n)]); i+=n
        i+=1
    return out
def bbox(p):
    xs=[a[0] for a in p];ys=[a[1] for a in p];zs=[a[2] for a in p]
    return (min(xs),max(xs),min(ys),max(ys),min(zs),max(zs))
