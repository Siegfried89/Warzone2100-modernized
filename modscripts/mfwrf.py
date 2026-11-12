"""Traegt die MF-Sounds (audio/mf/*.ogg) in die Ladeliste wrf/audio.wrf ein (sonst findet die Engine sie nicht). mfwrf.py OUT"""
import sys, os
OUT = sys.argv[1]
base = open('/tmp/claude-0/cmp/base/wrf/audio.wrf', 'rb').read().decode('latin-1')
files = sorted(f for f in os.listdir(os.path.join(OUT, 'audio', 'mf')) if f.endswith('.ogg'))
block = 'directory\t"audio/mf"\r\n' + ''.join('file\t\tWAV\t"%s"\r\n' % f for f in files)
marker = 'directory\t"audio"\r\nfile\t\tAUDIOCFG\t"audio.json"'
assert base.count(marker) == 1, 'AUDIOCFG-Block nicht gefunden'
new = base.replace(marker, block + marker)
os.makedirs(os.path.join(OUT, 'wrf'), exist_ok=True)
open(os.path.join(OUT, 'wrf', 'audio.wrf'), 'wb').write(new.encode('latin-1'))
print('wrf/audio.wrf:', len(files), 'MF-Sounds eingetragen')
