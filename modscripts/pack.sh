#!/bin/bash
# pack.sh NAME  -> baut out_u neu, zippt NAME.wz.zip und NAME_stumm.wz.zip nach /mnt/user-data/outputs
set -e
cd /home/claude/w
bash bu2.sh > /tmp/pv/bu2.log 2>&1
grep -E "Traceback|Error" /tmp/pv/bu2.log | head -5 || true
(cd out_u && rm -f ../$1.wz.zip && zip -qr -X ../$1.wz.zip .)
rm -rf /tmp/out_s && cp -r out_u /tmp/out_s
python3 -I - <<'Q'
import re, glob
for f in glob.glob("/tmp/out_s/stats/*.json"):
    s = open(f, encoding="utf-8").read(); t = re.sub(r'"weaponWav": "mfs[^"]*\.ogg"', '"weaponWav": "-1"', s)
    if t != s: open(f, "w", encoding="utf-8").write(t)
Q
(cd /tmp/out_s && rm -f /home/claude/w/$1_stumm.wz.zip && zip -qr -X /home/claude/w/$1_stumm.wz.zip .)
cp $1.wz.zip $1_stumm.wz.zip /mnt/user-data/outputs/
echo "fertig $1"
