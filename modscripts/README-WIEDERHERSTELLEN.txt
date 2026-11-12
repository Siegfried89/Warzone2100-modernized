BACKUP Combot-/Scavenger-Mod, Stand 20261112-2019 (UTC)

Inhalt
- moddata-sources-20261112-2019.tar.gz : Ordner w/ = ALLE Mod-Bau-Skripte (*.py, bu2.sh, pack.sh), Quelldaten (base, mp, extra, uni*, tex, feat, mf_src, mf_ui, sprites, ...). Ohne alte Zips und Zwischenstaende.
- gamedata-extracted-cmp-20261112-2019.tar.gz : entpackte Spieldaten (base/mp), die bu2.sh unter /tmp/claude-0/cmp erwartet.
- engine-worktree-20261112-2019.tar.gz : kompletter Engine-Quelltext (Arbeitsstand, ohne .git). Der Git-Verlauf liegt zusaetzlich auf GitHub: Siegfried89/Warzone2100-modernized (Branch master).
- scale_test86.wz.zip / scale_test86_stumm.wz.zip : fertige Mod-Pakete (stumm = MF-Sounds stumm).
- PLAN-offen.md : Gesamtplan und Wunschliste.
- SHA256SUMS.txt : Pruefsummen (zum Pruefen: sha256sum -c SHA256SUMS.txt)

Wiederherstellen (Linux):
  tar xzf moddata-sources-20261112-2019.tar.gz            # ergibt w/
  mkdir -p /tmp/claude-0 && tar xzf gamedata-extracted-cmp-20261112-2019.tar.gz -C /tmp/claude-0   # ergibt /tmp/claude-0/cmp
  tar xzf engine-worktree-20261112-2019.tar.gz            # ergibt warzone2100-modernized/
  cd w && bash pack.sh scale_testNN            # baut die Mod neu

Mod installieren (Windows): eine der .wz.zip nach
  C:\Users\MJ\AppData\Roaming\Warzone 2100 Project\Warzone 2100 master\mods\autoload\
kopieren (nur eine Mod-Zip dort lassen).

Hinweis: enthaelt Metal-Fatigue-Material (mf_src, mf_ui) - nicht oeffentlich weitergeben.
