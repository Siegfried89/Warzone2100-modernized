# Combot-Mod: Gesamtplan (Stand 12.11. abends, nach Texturen-/Ebenen-Paket)

## AKTUELLE GESAMT-OFFEN-LISTE (Stand 12.11. 18:50, zusammengeführt aus allen Abschnitten unten)

**1. Wartet auf dich (Test/Installation)**
- Neue Programmdatei (Windows, Build zum Commit b2ececc, fertig und erfolgreich) installieren und testen; danach die Mod-Datei MIT Ton statt "stumm" und die Logzeile "MOD build menu" lesen.
- Neue Mod-Inhalte im Spiel ansehen (scale_test81): Landfill 4/6/9, Außenposten (2 Dach-Varianten), Sandsack-Mauer, Ölpipeline (+ zerstörte Teile), Schützengräben (4 Arten), Gruben (quadratisch/sechseckig), Fass-Gruppen (5er/10er), MG-Einschlageffekt.
- Von dir: eigene Ring-Grafiken (Sensor/Waffe), restliche MF-Sounds über die Klangliste zuordnen, später eigene Combot-Modelle/Texturen.

**2. Nur Mod-Daten (ich, ohne neue Programmdatei)**
- Raffinerie (Kitbash aus den schwarzen Lagerhäusern), Öltransporter-Fahrzeug (Tanker-Wrack aufrecht), Lkw ohne Bauturm.
- Baubare Scav-Strommasten (wie Mauern ziehbar, Pylon-Feature), Wasserturm-Funktion vorbereiten.
- Fass-Entscheidung (5 pro Kachel / Stapelmesh / Viertel-Kacheln) – Modelle 5er/10er sind da.
- MG-Einschlag nach Zielart unterscheiden (Fels/Metall/Boden/Wasser) – bisher ein Effekt für alles.
- Alte Kleinpunkte prüfen/abschließen: Features halbieren, Scav-VTOL-Fabrik ausrichten, Scav-Infanterie Stehgröße/PS1-Sprite, v15 (Kamm mittig, Haaransatz, Kopf abflachen, Hautton, sanftes Blau), animbase.tbd entschlüsseln, Mauer-Selbstausrichtung/Mausbelegung.

**3. Programmdatei-Paket "Wirtschaft + Netz" (groß, wird mit den Ebenen gebündelt)**
- Ölkreislauf: Ölturm erzeugt Fässer, Lkw-/Tanker-Autopilot, Raffinerie, Wasserturm-Lager (+10 %, Lager-zuerst-Regel), Kraftwerk (Fass = Energie + 3 Min Netz).
- Stromnetz: aktiv/inaktiv, Masten mit 3,5-Felder-Radius, Leitungen, 5. Sensoranzeige "Stromnetz", Außenposten nur mit Strom aktiv.
- Pipeline: Verbindungsprüfung Ölturm-Raffinerie, Zerstörung mit Feuerexplosion + schwarzem Rauch (Broken-Modell), Reparatur, Durchgangs-/Brückenstück.
- Fass-Falle: 1 HP, große Explosion mit Kettenreaktion, viel Thermal + etwas kinetisch, Schaden und Wert nach Fassanzahl.
- ~~Mauer-Familien~~ ERLEDIGT/ENTFÄLLT (12.11. 18:55): Gräben, Sandsack-Mauern, Pipelines und Scav-Mauern sollen sich WEITERHIN alle untereinander verbinden. Keine Änderung nötig.
- Gräben/Gruben: Infanterie hineinsetzen (Besetzen), eingegrabene Gräben (Gelände absenken).

**4. Drei Ebenen (nächstes großes Paket)**
- Orbit (Plattformen vor weichgezeichneter Oberflächen-Textur, nur Flieger), Untergrund (eigene Höhlenkarte), Schacht als Portal, Bohr-/Grabfunktion, zerstörbares Terrain.

**5. Weitere Entwicklung**
- Flug und Sprung (Jump-Cyborgs/-Combots, Jetpack, Sonar, Aufklärung) mit Animationen.
- Viertel-Kachel-Wegfindung (riskant).
- Hauptmenü mit mehreren speicherbaren Flotten (in Kampagne/Mehrspieler mitnehmbar).
- Supreme-Commander-Elemente: experimentelle Einheiten.
- Restliche MF-Inhalte: Fahrzeuge, Effekte, Fuß-/Todes-/Treffer-Sounds, Kran beim Bauen.
- Combot-Designer: Feinschliff mit Screenshots nach dem ersten Test.
- Skirmish und Kampagne gleich spielen lassen; Gegner-Fraktionen später ebenfalls umwandeln.
- Waffenmodus-Knöpfe im Netzwerk-Mehrspieler gesperrt (Synchronität), Designer-Sounds nur geraten (Feinzuordnung).
- **Ganz zuletzt:** Kampagne als Scavengers ab Mission 2 (Mission 1 bleibt wie sie ist).

(Details je Punkt stehen in den Abschnitten unten.)


## Fertig (Syntax gegen echte Engine-Header geprüft, im Spiel noch nicht getestet)
- Combots mit echten MF-Animationen (Gehen, Schießen, Idle, Sterben), 195 Teile modular exportiert
- Modularer Zusammenbau zur Laufzeit: Torso = Rumpf, Beine = Antrieb, Arme = Waffen; Animation läuft mit fehlenden Gliedern weiter
- Teile abschlagen (Treffer auf zufälliges Teil); abgeschlagene Teile liegen als Features auf dem Boden (bergbar)
- Combot-Designer: eigener Button (hochformatig, blau, weiße Silhouette) im Design-Menü, eigene Teileliste (Torso/Beine/Arme/Fertige),
  MF-nahe Oberfläche mit drehender 3D-Montagehalle in Echtzeit, Anbring-/Abnehm-/Klapper-Sounds, speichert die Vorlage direkt als baubare Einheit
- Blaues Combot-Diagramm im Auswahlpanel (Teilzustand in %), MF-Befehlsknöpfe: MELEE/ALL/RANGED, STAY/TETHER/PURSUE,
  GUARD/ATTACK/MOVE, PATROL/RETFIRE/STOP
- Fähigkeiten: Geschwindigkeitswerte, Tarnung (Transparenz), Schild-Blockbonus, Selbstreparatur
- Kamera, Balken, Platzieren ohne Abstandsregeln, Sensorbutton + Ringe, Formationen mit Icons, Duell-System, Combot-Klicksounds, Staub beim Bauen

## Neu in diesem Paket (noch nicht im Spiel getestet)
- "cheated"-Meldung komplett entfernt (Speicherstände, Punkte, Missionstexte, Mehrspielerliste)
- WASD = Kamera, Kameratempo 2400; alte Konfiguration/keymap.json wird einmalig automatisch umgestellt (kein Handeingriff)
- Texturen: jedes Teil/Gebäude bekommt seine eigene Textur, Teamfarbe über Maske, weich geglättet
- Baumenü: MF-Gebäude passen sich automatisch in den Knopf ein
- Scavenger-Variante in knalligem Gelb und New-Paradigm-Gelb, kommen abwechselnd aus der Fabrik (Engine-Schlüssel "alternativeModels")
- Turm-MG-Schütze steht hinter dem MG, nur Arme nach vorn, gelbe Hose
- Vanilla-Entwurfsbildschirm zeigt keine Scavenger-Antriebe ohne Modell mehr (vorher 50000 Fehlermeldungen im Log)

## Bewusst unvollständig / zurückgestellt (ehrlich)
1. Flug / Sprung / Sonar / Aufklärung: nur Werte, noch keine echte Flug- oder Sprungbewegung
2. Drei Ebenen (Untergrund / Oberfläche / Orbit): drei Knöpfe mit weichem Überblenden und Farbton sind drin (nur Ansicht).
   - Wunsch vom 12.11. 16:12: Wie in Metal Fatigue soll unter den schwebenden Orbit-Plattformen die verschwommene Oberflächen-Textur (das Grün) der darunterliegenden Karte sichtbar sein; die drei Ebenen sollen sich wie im Original verhalten; zerstörbares Terrain, mit Bohrern Korridore durch den Untergrund graben. Umsetzungsidee: Orbit = eigene Karte aus Plattformen/Asteroiden, dahinter eine große, weichgezeichnete, langsam wandernde Fläche mit den Terrain-Texturen der Oberflächenkarte; Untergrund = eigene Höhlenkarte, Schacht-Gebäude als Portal; Graben über setTileHeight/Tile-Wechsel + Wegesuche neu berechnen. Echte Ebenen (eigene Karte je Ebene, Schacht als Portal, Tunnel graben, Orbit nur für Flieger) kommen später; der Zustand liegt schon in modlayer::current()
3. Viertel-Kachel-Wegfindung: zurückgestellt (riskant für das ganze Spiel)
4. Restliche MF-Fahrzeuge/-Effekte, Fuß-/Todes-/Treffer-Sounds, Kran beim Bauen: offen
5. Klangliste: Designer-Sounds (Nr. 8/47/11/48) und Combot-Klicksounds sind akustisch geraten, nicht angehört → Feinzuordnung später
6. Waffenmodus-Knöpfe (MELEE/ALL/RANGED) sind im Netzwerk-Mehrspieler gesperrt (Synchronität)
7. Designer-Optik (Größe/Position der Montagehalle, Textpositionen) nur per Syntax geprüft → nach dem ersten Test Feinschliff mit Screenshots
8. Kampagne als Scavengers: NICHT gestrichen, nur ganz nach hinten gestellt. Mission 1 bleibt unverändert als Scavengers (so wie sie jetzt ist, "schon perfekt").
   Alle weiteren Missionen als Scavengers kommen als letzter Punkt, nach allem anderen.

## Gesamt-Offen-Liste (Durchsicht vom 12.11. 16:50, alle Wünsche seit Projektbeginn gegen den Stand geprüft)

### A. Wartet auf deinen Test mit der neuen Programmdatei (Build zum Commit b2ececc)
Kameratempo 1800 (stellt sich einmal automatisch um), Baumenü ohne Festungen/Original-Kopien/Doppelte, Antriebs- und Körperliste ohne leere/doppelte Einträge,
Mod-Waffensounds (dafür die Datei scale_test72.wz.zip statt der "stumm"-Version), Turm-Schütze, NP-Kamm, Ebenen-Knöpfe, Combot-Designer, MF-Duelle, abgeschlagene Teile, Formationen, Sensor-/Waffenringe.
Die Programmdatei schreibt jetzt jede Baumenü-Liste in die Logdatei (Zeile "MOD build menu"), damit ich sofort sehe, was angeboten wird.

### B. Von dir ausstehend
- eigene Ring-Grafiken für Sensor-/Waffenringe (blau/gold bzw. rot) – bisher laufen Platzhalter
- Zuordnung der übrigen MF-Sounds über die Klangliste (klangliste.html); Sound 64 = Bewegen, 105 = Angriff sind festgelegt
- später: eigene Combot-Modelle/-Texturen

### C. Offen in der Entwicklung (Reihenfolge nach Plan)
1. Drei Ebenen echt: Orbit (Plattformen vor weichgezeichneter Oberflächen-Textur, nur Flieger), Untergrund (eigene Höhlenkarte), Schacht als Portal, Bohr-/Grabfunktion – NÄCHSTES großes Paket
2. Flug und Sprung (Jump-Cyborgs/-Combots, Jetpack, Sonar, Aufklärung): bisher nur Werte; dazu MF-Leuchtanimationen und Flug-Animationen
3. Viertel-Kachel-Wegfindung (riskant, deshalb nach den Ebenen)
4. Hauptmenü mit mehreren speicherbaren Flotten, mitnehmbar in Kampagne/Mehrspieler (noch nicht begonnen)
5. Supreme-Commander-Elemente: experimentelle Einheiten (Marker/Ringe sind da)
6. Restliche MF-Inhalte: weitere Fahrzeuge, Effekte, Fuß-/Todes-/Treffer-Sounds, Kran beim Bauen
7. Combot-Designer: Feinschliff mit Screenshots nach dem ersten Test (Originallayout)
8. Skirmish und Kampagne gleich spielen lassen (Datei-Abhängigkeit aushebeln); Gegner-Fraktionen später ebenfalls umwandeln
9. Kampagne als Scavengers ab Mission 2 (ganz zuletzt, nicht gestrichen)

### D. Alte Kleinpunkte, im Spiel zu prüfen (waren in der Aufgabenliste noch als offen markiert)
- Features (Deko) halbiert? Scav-VTOL-Fabrik ausgerichtet?
- Mauern richten sich beim Bauen selbst aus? Mausbelegung (Rechtsklick/Linksklick, Ziehen zum Drehen)?
- stufenloser Zoom ohne Versinken im Boden (Kamera-Drehpunkt sinkt beim Zoomen auf Bodenhöhe: eingebaut)
- Waffenmodus-Knöpfe (MELEE/ALL/RANGED) im Netzwerk-Mehrspieler gesperrt (Synchronität)

## NEU 12.11. 16:45 – Ideen von dir (alle in den Plan aufgenommen)

### E. Stromnetz / Aktiv-Inaktiv-Zustand (Engine + Modelle)
- Gebäude bekommen einen Zustand "aktiv / inaktiv". Inaktiv: kein Sensorbild, Antennen stehen still, Gebäude wirkt dunkel.
- Quelle: Kraftwerk. Knoten: Strommasten. Verbraucher: Außenposten (und weitere "coole" Gebäude wie ein Ölturm).
- Strommasten: Die Masten aus den Features (Modell "Pylon") bekommen eine Funktion; außerdem baubare Scav-Strommasten, die man wie Mauern in einer Linie ziehen kann ("dynamisch gewachsene Scav-Infrastruktur").
- Jeder Mast hat einen Verbindungsradius (Vorschlag: ca. 3 bis 3,5 Kacheln: eng genug für eine erkennbare Linie, weit genug, dass sie nicht einzeln stehen). Die Radien überlappen sichtbar; zwischen verbundenen Masten werden durchhängende Leitungen gezeichnet (aktiv = hell, getrennt = dunkel).
- Fünfte Sensor-Anzeige im Sensorknopf: "Stromnetz" zeigt Radien, Leitungen und welche Gebäude gerade Strom haben.
- Außenposten: Modell aus dem Scav-Bunker/der Scav-Fabrik (kein Wellblech-Verschlag), doppelt so groß, mit sich drehender Sensor-Antenne auf dem Dach. Nur mit Stromanschluss aktiv; dann sendet er Sensordaten (Sensorreichweite), sonst blind.
- Wasserturm bekommt eine echte Funktion (Vorschlag: Kühlung: ein Kraftwerk im Netz neben einem angeschlossenen Wasserturm liefert mehr Leistung; später ggf. Wasser für Scav-Fabriken).
- Reihenfolge: Außenposten-Modell (nur Mod) → Netz-System in der Programmdatei, gemeinsam mit den drei Ebenen in einem Build.

### F. Landfill (Dekoobjekt, nur Mod-Daten, keine neue Programmdatei nötig)
- Ein großes Einzelmodell aus allen vorhandenen Autowrack-Features (BlueCar, Wreck0-5, WreckedSuzukiJeep, WreckedVertCampVan, WreckedTankerV, WreckedDroidHub, hoverwreck, …), von Scav-Mauern umschlossen.
- Haufen, teils mehrlagig gestapelt, Kreiswege durch die Anlage.
- Drei Versionen: 4 Kacheln (2x2), 6 Kacheln (2x3) und 9 Kacheln (3x3). Zuerst nur Deko (später ggf. Ressourcen/Bergung).
- **Stand 12.11. 16:55: gebaut** (scale_test73): Feature-Namen Landfill4 / Landfill6 / Landfill9 (Namen im Spiel "*Landfill 4/6/9*"). Mauerring, Mittelhaufen, Kreisweg, Autostapel an den Mauern. Im Spiel zu prüfen: Größe/Aussehen, ob Einheiten das Feld als Hindernis erkennen, Platzierbarkeit im Karteneditor.


### G. Neues Wirtschaftssystem: Ölkreislauf (NEU 12.11. 16:55 und 17:00, Konzeptbild + Diagramm als Seite veröffentlicht)
- Fass: 100 Energie (Engine-Standard, Kampagne Beta 150, Gamma 200). Ölturm stellt Fässer ab (max. 3 am Turm), statt Energie direkt zu zahlen.
- Lkw (Projekt-Lkw mit Ladefläche, ohne Bauturm): dauerhafter Lieferant mit Autopilot, holt Fässer, fährt zur Raffinerie (ohne Raffinerie direkt zum Kraftwerk). Fällt als Baueinheit weg.
- Raffinerie (Kitbash aus den schwarzen Lagerhäusern + weiteren Gebäuden, Auswahl noch festzulegen): veredelt Rohöl-Fass zu Treibstoff-Fass (Vorschlag 150 statt 100 Energie).
- Öltransporter (Modell des umgekippten Tanker-Wracks als aufrechtes Fahrzeug): übernimmt ab der Raffinerie, bringt Treibstoff-Fässer zum Kraftwerk.
- Wasserturm: Zwischenlager (Vorschlag bis 8 Fässer) mit kleinem Bonus (Vorschlag +10 %) auf gelieferte Fässer. **Regel (17:20): Alles, vor allem Überproduktion, geht zuerst ins Lager, bekommt dort den Bonus und erst dann ins Kraftwerk. Das Kraftwerk zieht nur aus dem Lager. Ohne Wasserturm direkt ins Kraftwerk ohne Bonus.**
- Kraftwerk: Fass wird Energie, 1 Fass hält das Stromnetz 3 Minuten am Laufen (Vorschlag).
- Gegner können Fässer am Boden einsammeln (Engine-Verhalten) und Lkw/Tanker abschießen.
- Umsetzung: großer Umbau in der Programmdatei (Fass-Erzeugung, Lkw-/Tanker-Autopilot, Lager, Netz) im selben Build wie Stromnetz (E) und drei Ebenen. Davor nur Mod-Daten: Außenposten-Modell, Raffinerie-Kitbash, Öltransporter-Fahrzeug.

- **Stand 12.11. 17:15:** Landfill nutzt jetzt alle Deko-Fahrzeuge (Jeep, Camper, blaues Auto, Chevy, Pickup, Hover-Wrack, Eiswagen, Tanker-Wrack, Lkw, Schulbus, Feuerwehr). Außenposten-Modell fertig (Mod-Paket scale_test75): Struktur "Scavenger Outpost" (A0BaBaOutpost, 2x2 Felder, Bunker x2 mit Technikhaus, Eckmasten, drehender Sensor-Antenne), freigeschaltet durch dieselbe Forschung wie die Scav-Verteidigungen. Noch ohne Stromabhängigkeit (kommt mit der Programmdatei, siehe E). Im Spiel zu prüfen: Größe/Aussehen, Antenne dreht sich, Sensorreichweite.
- Offen aus G: Raffinerie-Kitbash (Auswahl der Lagerhäuser), Öltransporter als Fahrzeug (Modell des umgekippten Wracks aufrecht stellen), Lkw ohne Bauturm.


### H. Neu 12.11. 17:26 bis 17:28 (merken!)
- **Fässer / Platz:** Auf einem Tile sollen 5 Fässer platzierbar sein (Würfel-Fünf-Muster: vier Ecken + Mitte) ODER ein Mesh, in dem etliche Fässer zusammenstehen und gemeinsam einen höheren Wert haben (einfachere Variante: ein Feature "Fass-Haufen" = 1 Objekt, z. B. 5 Fässer = 500 Energie). Langfristig lieber die Kacheln vierteln (größerer Umbau, gehört zum Viertel-Tile-Wegfindungs-Projekt). Entscheidung offen; bei Umsetzung der Fass-Erzeugung (G) mitplanen: statt "max 3 Fässer am Turm" dann "bis 5 Plätze am Turm".
- **Außenposten:** Sensorturm in normaler Größe (nicht 0,8), links oben. Technikhaus statt gelb: Scav-Mauertextur oder Scav-Fabriktextur, beide zeigen, dann entscheiden.
- **Sandsack-Mauer (neu):** Mauer-Mesh mit der Sandsack-Textur der Mörserstellung (Scav). Ähnlich wie die Scav-Mauern, nur dicker und etwas niedriger/kleiner. Als neue Mauer-Struktur (Typ WALL) wie "Scav Horizontal Wall" mit eigenen Ecken/T-Stücken.
- **Stand 12.11. 17:32 (scale_test77, auf dem PC installiert):** Sandsack-Mauer gebaut (Struktur "Sandbag Wall", Mauer-Typ mit 4 Formen gerade/Ecke/T/Kreuz, 220 Trefferpunkte, freigeschaltet mit den übrigen Scav-Verteidigungen). Außenposten: Sensorturm in normaler Größe links oben, Technikhaus vorerst Variante A (Scav-Mauer-Backstein); Variante B (Scav-Fabrik, Beton + Fensterwand) per Umgebungsvariable HOUSE=factory in outpost.py. Entscheidung des Nutzers steht aus.

- **17:40:** Ölkreislauf-Seite komplett auf Englisch neu gebaut (gleiche URL): eine Reihe von links nach rechts, 8 Stationen, Lager-zuerst-Regel, Wert je Weg (100/110/150/165), Stromketten-Zustände, Außenposten-Nahaufnahme, Tabelle "was wenn etwas fehlt", offene Punkte.

### I. Neu 12.11. 18:03 (scale_test78 auf dem PC)
- **Außenposten:** Variante A gewählt. Hauptdach jetzt aus den braunen Wellblechen der Scav-Fabrik, Technikhaus mit Satteldach aus braunem Wellblech (Backstein-Giebel). Sensorturm normal groß, links oben.
- **Sandsack-Mauer überarbeitet:** dünner (unten 24, oben 15 statt 32/20), 26 hoch, Säcke liegen waagerecht, Oberseite in quadratischen Feldern entlang der Mauer (Struktur "Sandbag Wall", 200 Trefferpunkte).
- **Ölpipeline (neu, nur Mod-Daten):** Struktur "Oil Pipeline" (A0BaBaPipeline, Mauer-Typ, wie Mauern ziehbar, 260 Trefferpunkte). 4 Formen: gerade, Kreuz, T, Ecke (Maße der blauen Pipeline-Features PipeFull/PipeEnd/PipeCollapsed: 128 lang, Rohrmitte auf 41, Staender + Bodenplatte), Textur der grauen Rost-Pipeline. Dazu je Form eine zerstörte Variante (abgerissenes, eingeknicktes Rohr, Öllache, verbrannte Textur) als nicht baubare Struktur "A0BaBaPipelineBroken". Freigeschaltet mit den Scav-Verteidigungen.
- **Engine-Teil Pipeline (kommt in den großen Build, gehört zu E/G):** (1) Pipeline verbindet Ölturm und Raffinerie: Ölturm/Raffinerie gelten als verbunden, wenn eine ununterbrochene Pipeline-Kette dazwischen liegt (Ersatz oder Ergänzung zum Lkw). (2) Wird ein Pipeline-Stück zerstört: große feurige Explosion + dauerhafter großer schwarzer Rauch, es bleibt als "Broken"-Modell stehen (nicht entfernt), die Kette ist unterbrochen; Reparatur durch Bautrupp stellt das intakte Modell wieder her. Effektpunkt (Rauch/Feuer) ist als Anschluss in jedem Modell schon eingetragen (Mitte, Rohrhöhe). (3) Bekanntes Verhalten: Mauer-Typ blockiert Einheiten; später ggf. ein Durchfahrts-/Brückenstück (Rohr auf Bögen) einbauen. Pipelines verbinden sich im Bauraster auch mit Scav-Mauern (gleicher Mauer-Typ), falls das stört, eigener Typ in der Engine.


### J. Fass-Falle (Nutzer-Nachricht 12.11. 16:59, erst 18:15 nachgetragen, war verloren gegangen)
- Ölfässer sollen auch als **Falle** nutzbar sein: Lkw nimmt sie auf, lädt sie ab; erleiden sie genug Schaden, explodieren sie sehr schnell.
- Explosion groß, wie wenn eine große Scav-Basis zerstört wird und alle Mauern gleichzeitig hochgehen.
- **Kettenreaktion** zwischen Fässern ist möglich.
- Lebenspunkte: 1. Schaden: viel Thermalschaden und einiges an kinetischem Schaden.
- Größere gelagerte Fass-Meshes, die aus mehreren Fässern bestehen, machen mehr Schaden.
- Pro Tile wieder 5 Fässer im Würfel-Fünf-Muster platzierbar (siehe H).
- Umsetzung: Fass-Feature/Struktur mit 1 HP und Explosionswaffe (Flächen-Schaden, Thermal + kinetisch), Kettenreaktion über Flächenschaden auf Nachbar-Fässer; Fass-Haufen = Mesh mit gestaffeltem Schaden. Teil des Wirtschafts-Umbaus (G).

### K. Schützengräben (Nutzer-Nachricht 12.11. 16:59, erst 18:15 nachgetragen)
- **Graben von Schützengräben möglich** (wie Mauern ziehbar).
- Zwei Varianten der Gräben: **offen** und **mit Abdeckung** aus weißgrauem Tuch, das an zwei sehr dünnen Holzstäben an den Sandsackwänden befestigt ist.
- Aufbau: zwei parallel laufende Sandsack-Barrieren (dünn, wie die überarbeitete Sandsack-Mauer) mit dunkler Dreck-Textur in der Mitte (Landfill-Matsch-Textur oder etwas, das gut passt); der Boden innen ganz bedeckt.
- Wenn möglich in 3D ein Stück tiefer in den Boden gegraben, besser zwei Varianten: eine an der Oberfläche, eine tief im Boden (Hinweis Umsetzung: tief im Boden braucht eine Geländeabsenkung in der Engine, ein Modell unter der Erde wird vom Gelände verdeckt).
- Mehrere Modelle nötig: gerade, Ecke, T, Kreuz (alle Richtungen), je offen und abgedeckt.
- **Gruben** (wie die Mörsergrube, aber mit Dreckboden und rundum von Sandsäcken geschützt): eine **quadratische** Variante (Mörsergrube) und eine **sechseckige** wie die Scav-Raketenstellungen; Sandsack-Textur der Mörsergrube (oder des Scav-Bunkers).
- Alle Gräben und Gruben sollen **leer** sein, damit sie später in der Engine mit Infanterie besetzt werden können (Besetzungs-System = Engine-Teil, gehört zu den Verteidigungen).

**Stand 12.11. 18:30 (Mod-Daten fertig, scale_test79):**
- J (Fässer): Modelle `oildrum5.pie` (5 Fässer im Würfel-Fünf-Muster) und `oildrum10.pie` (zweilagig, 10 Fässer) als Features `OilDrum5` / `OilDrum10` (1 Lebenspunkt, zerstörbar) liegen im Skript `drums.py`; sie kommen mit dem nächsten Build ins Paket. Noch offen (Programmdatei): Explosion mit Kettenreaktion, Schaden nach Fassanzahl, Wert nach Fassanzahl beim Einsammeln.
- K (Gräben/Gruben): fertig als Mod-Daten. Gräben `A0BaBaTrench` (offen), `A0BaBaTrenchCover` (mit Tuch), `A0BaBaTrenchDeep` / `A0BaBaTrenchDeepCover` (eingegraben), je 4 Modelle (gerade, Kreuz, T, Ecke), Typ Mauer, in der Forschung bei den Mauern. Gruben `A0BaBaPitSquare`, `A0BaBaPitHex`, je auch `...Deep`, Typ Verteidigung ohne Waffe = leer.
- K, Programmdatei offen: (1) **eingegrabene Gräben:** das Gelände verdeckt den Boden, solange die Engine das Gelände unter dem Graben nicht absenkt; bis dahin sieht der „Deep“-Graben wie ein niedriger Graben ohne sichtbaren Boden aus. Absenken gehört zu den Ebenen/Bohr-Terrain (#32). (2) ~~Verbindung nur mit gleicher Familie~~ ENTFÄLLT: alle Mauer-Typen sollen sich weiter alle miteinander verbinden (Nutzer 12.11. 18:55). (3) **Besetzen:** Infanterie in Graben oder Grube setzen (Schutz, Schussrichtung, Rein-/Raus-Befehl). (4) Grube soll sich später drehen/ausrichten lassen wie die Mörsergrube (R90/R180/R270 entfällt, Grube ist symmetrisch).

### L. Neu 12.11. 18:31 bis 18:50 (scale_test81, auf dem PC installiert)
- **Außenposten, zwei Varianten (beide bleiben):** `A0BaBaOutpost` = „Scavenger Outpost (brown roof)“ (großes Dach braun, Haus braun) und `A0BaBaOutpostG` = „Scavenger Outpost (grey roof)“ (großes Dach grau, Haus braun). Beide mit Sensorturm links oben, gleiche Werte.
- **MG-Einschlag (universal):** Alle echten Maschinengewehre (MG1–MG4, Rotary, Turm, VTOL, Zyborg, Scav-MGs, Gatling-Arm) haben kein Flug-Geschoss mehr (unsichtbar, Treffer läuft intern weiter). Bei Treffer und Fehlschuss erscheint der Effekt `fxmgimp.pie`: kleiner Schwall hellbrauner Staubpixel plus ein paar gelbe Funken (Pixelbilder `page-440-mgimpact.png`, 5 Bilder à 60 ms). Gilt für alle Ziele gleich; spätere Verfeinerung: andere Effekte je Zielart (Fels, Metall, Boden, Wasser, Fleisch).
- **Ölkreislauf-Bild (Version 3):** Pipeline-Abschnitt (verbunden / ein Stück gesprengt / repariert, Modellbilder der 4 Formen intakt und zerstört) und beide Außenposten-Dächer ergänzt, Tabellen und offene Punkte nachgezogen.
- Skripte: `mgimpact.py`, `outpost.py` (zwei Modelle), `pack.sh NAME` (baut alles, zippt NAME und NAME_stumm).

### M. Neu 12.11. 18:55 (Nutzer-Nachricht, wörtlich zusammengefasst; Auftrag: Punkte 1 bis 4 der Gesamtliste selbstständig abarbeiten, Nutzer ist weg, nicht nachfragen, bei Zweifel mehrere Vorschläge machen und ganz zum Schluss abfragen; Zeit egal)
1. **Projektilgeschwindigkeit:** Alle Nicht-Raketen-Projektile DRASTISCH schneller, fast sofort, damit der Einschlag beim Ziel mit dem Abschuss zusammenfällt (sonst passt der Schuss-Sound nicht zum Einschlag).
2. **Hall auf alle Schusssounds** ("episch"): auf die Liste, oder früher, wenn ich es schon kann.
3. **Einschlag für leichte, mittlere, schwere Geschütze (Kanonen):** ebenfalls Pixel-Partikeleffekt, nur mit mehr Feuer, größer und mit Rauch.
4. **Lkw:** NICHT "ohne Bauturm" gemeint. Die Ladefläche des Systemturms brauchen wir, um einen Lkw zu simulieren, aber der Python-Rumpf soll weg. Stattdessen typischer **Scavenger-Lkw** (Ladefläche vom Systemturm, aber eher Scav-Modelle). **3 Vorschläge** machen. Dazu ein **GROSSER** Lkw (transportiert mehr), wie ein großer Radlader (evtl. Scav-Raupen-Antrieb).
5. **Scav-VTOL-Fabrik:** ausrichten und etwas kleiner machen.
6. **Strommasten:** baubar, wie Mauern in einer Linie ziehbar; bei stehender Verbindung soll ein **schwarzes, durchhängendes Kabel** sichtbar sein.
7. **Mauer-Verbindung:** Gräben, Sandsack-Mauern, Pipelines und Scav-Mauern sollen sich WEITERHIN ALLE miteinander verbinden (nicht nach Familien trennen!). Damit kann man z. B. eine Scav-Mauer von niedrigeren Sandsack-Abschnitten unterbrechen und mit Sandsack-Stellungen dekorieren.


### N. Fortschritt zu M (12.11. ab 19:00, scale_test82)
- M1 Projektile: `projspeed.py` (alle Direktwaffen ausser Rakete/Flamme/Bombe/LasSat: flightSpeed 5000 = 500 Einheiten je Tick, mit der ALTEN Engine sicher). **Engine-Änderung in projectile.cpp** (Suche entlang der ganzen Flugstrecke statt nur am Endpunkt) ist im Repo, noch nicht gebaut/gepusht. **Sobald die neue Exe installiert ist: Umgebungsvariable PROJSPEED=15000 beim Bauen** (dann Einschlag im ersten Tick). Mörser/Haubitzen (Bogenflug) und Flammenwerfer, Bomben, Raketen unverändert (Frage ans Ende).
- M2 Hall: `hall.py` (Faltungshall, 4 Voreinstellungen: MG kurz, Kanone mittel, Rakete/Mörser lang, Schwer sehr lang). 64 Dateien `<name>_h<N>.ogg`, 267 Waffen umgeleitet. Flammenwerfer und Bomben ohne Hall. In der „stumm“-Fassung bleiben die MF-Sounds (mfs*) stumm.
- M3 Kanonen-Einschlag: `cannonimpact.py` (15 leicht, 14 mittel, 6 schwer; Feuerball, Trümmer, Funken, Rauchsäule, Staubring). Nicht angefasst: Flak, Plasmakanone, Superwaffe 2, Railguns.
- M4 Scav-Lkw: `scavtruck.py`, 4 Fahrzeuge (Vorschau scav_lkw_vorschlaege.png): A Pickup, B Pritschen-Lkw, C Traktor, D Radlader (grosses Raupenfahrwerk + Schaufel). Ladefläche = Systemturm-Modell trlcon.pie mit Scav-Rostblech. Freigeschaltet ueber R-Sys-Spade1Mk1. Ladekapazität (Fässer) kommt mit dem Ölkreislauf: Pickup 2, Traktor 3, Lkw 4, Radlader 6 (Vorschlag).
- M5 VTOL-Fabrik: 20 % kleiner, mittig auf dem 2x2-Feld (wz_scale.py). Falls sie im Spiel noch schief wirkt: Rueckmeldung noetig.
- M6 Strommasten (Engine), M7 Verbindung (nichts zu tun, siehe oben).


### O. Fortschritt 12.11. ab 19:30 (Arbeit am Auftrag M, Nutzer weg; scale_test83 bis scale_test86)
**Ölkreislauf (Engine `modoil.cpp`, Mod-Daten `power.py`, `refinery.py`, `scavtruck.py`, `drums.py`, `pipeline.py`):**
- Ölturm erzeugt Fässer (bis 5 am Turm). Scav-Lkw mit Ladefläche fahren im **Autopilot** (nur wenn der Lkw keinen Befehl hat): Fass holen, zum Kraftwerk oder erst ins Lager (Wasserturm, +10 %). Ein Fass hält das Kraftwerk (und damit das Stromnetz) 3 Minuten am Laufen.
- **Raffinerie** `A0BaBaRefinery` (3x3, eigenes Modell): macht aus einem rohen Fass (100) ein Treibstoff-Fass (150, hält 4,5 Minuten). **Öltransporter** `ScavTanker` (Tankaufbau) fährt Treibstoff-Fässer (Kapazität 10).
- **Pipelines** verbinden Ölturm, Raffinerie, Lager und Kraftwerk ohne Lkw. Ein zerstörtes Stück wird zum **Bruchstück** (Feuer, Rauch) und durch Reparatur wieder heil.
- **Fass-Haufen** (OilDrum5 / OilDrum10) und beladene Lkw **explodieren mit Kettenreaktion** (Radius und Schaden nach Fassanzahl).
- Gilt nur im Mehrspieler/Gefecht für menschliche Spieler und nur, wenn die Mod-Daten da sind (Kampagne unverändert). Anzeigemodus 4 (Knopf „Reichweiten“ weiterschalten) zeigt Stromnetz + Fässer.
- **Bekannte Grenze:** der Zustand (Fässer, Ebenen) wird **nicht in Spielständen gespeichert**; nach dem Laden stehen alle Lkw und Fässer wieder auf Anfang.
**Stromnetz:** Masten `A0PowerPole` (1x1, ziehbar wie Mauern), schwarzes durchhängendes Kabel (grau wenn ohne Strom), Außenposten brauchen Strom.
**Gräben/Gruben:** Infanterie (Beine) darf hinein (`infantryPassable`), bekommt dort 55 % weniger Schaden; eingegrabene Gräben senken das Gelände ab (ungetestet). Noch offen: ausdrücklicher Rein-/Raus-Befehl und Schussrichtung.
**Fehler gefunden und behoben:** (1) `loop.cpp` hatte die Einbindung von `modgrid.h` im Debug-Block (Bau-Fehler „modoil has not been declared“), (2) `clean.py` hat alle Scav-Lkw bis auf einen ausgeblendet, (3) Klammerfehler in `modgrid.cpp`.
**Drei Ebenen, Version 1 (Engine `modlayer.cpp`, Mod-Daten `shaft.py`), in scale_test86 + Engine-Commit nach dem Pushen:**
- Neues Bauwerk **Schacht** `A0LayerShaft` (1x1, Fördergerüst mit Seilwinde), baubar mit dem Spaten (wie Mast, Raffinerie).
- Einheit **anklicken-Befehl auf den Schacht** (Rechtsklick auf den Schacht oder auf den Boden daneben) = hinfahren; kommt sie an (näher als 1,9 Felder), wechselt sie die Ebene: Oberfläche ↔ Untergrund. Wartezeit 4 s gegen Hin-und-Her. VTOLs, Transporter und Fass-Lkw im Autopilot wechseln nicht.
- Objekte auf **verschiedenen Ebenen** sehen sich nicht (Sichtsystem), schießen nicht aufeinander (Zielwahl, Schusslinie, Geschoss-Treffer, Flächenschaden, Brandschaden) und behindern sich nicht beim Fahren.
- Untergrund-Einheiten fahren wie „Geister“ unter allen Bauwerken, Felsen und Wasser durch (Wegfindung wie Lufteinheiten; Gelände bremst sie weiter). Sie schießen durch Bauwerke der Oberfläche.
- Neue Bauwerke entstehen auf der **Ebene des Baumeisters**. Die Anzeige (Knöpfe Orbit/Oberfläche/Untergrund oben rechts) zeigt **nur Objekte der gewählten Ebene** (Schächte immer); nach dem Umschalten werden ausgewählte Einheiten der anderen Ebene abgewählt; wechselt eine ausgewählte Einheit die Ebene, folgt die Anzeige. Untergrund hat einen kräftigen braunen Schleier.
- Ohne Objekte außerhalb der Oberfläche ändert sich im Spiel nichts (ein Prüfwert, sonst kostenlos).
- **Offen / Vorschläge (bitte entscheiden):** Orbit-Ebene (siehe Fragen), echte Höhlenkarte, Bohren und zerstörbares Gelände, unscharfe Oberfläche in der Orbit-Ansicht, Ebenen im Spielstand speichern, Aufzug-/Schacht-Animation, Schacht für Fahrzeuge vs. nur Infanterie.


### P. Neue Wünsche 12.11. 21:17 und 21:20 (Nutzer-Nachricht + 7 Bilder; Auftrag: „das Machbare davon umsetzen, alles umsetzen, Engine umbauen falls nötig, aber hinten anstellen“)
Reihenfolge: 1 Backup (ERLEDIGT) → 2 Schacht → 3 Tanker → danach die Liste unten von leicht nach schwer.
**Erledigt (scale_test87 / scale_test88):**
- P1 Backup: Mod-Daten und Engine an 3 Stellen (Ablage `backup/`, PC-Ordner `backup_combot_20261112`, GitHub-Zweig `backup/mod-scripts-20261112`).
- P2 **Schacht** = Original-MF-Aufzug (Sockel mit Luken und Gitterplatte, `mfshaft.py`, 3DStructuresZ Gruppe 63; Gruppe 64 = hohe Lichtsäule ist für die spätere Orbit-Verbindung vorgemerkt), **von Spielbeginn an baubar** ohne Forschung (Skirmish `gameinit.js` und Kampagne `rules.js`: `enableStructure`). Mein Fördergerüst bleibt als Ersatzmodell `scavshaft.pie` in der Datei.
- P3 **Tanker**: Original-Mesh `mitanker.pie` war nur verstreut und schief gedreht (gerader 5-Eck-Tank + Kabine + 2 Achsblöcke). `tanker.py` setzt die Teile in eine Achse, Original-Texturen bleiben: (a) Wrack gerade, auf der Seite liegend (ersetzt `features/mitanker.pie`, auch im Landfill), (b) **fahrbarer Öltransporter** aufrecht, Rumpf `Body19TRK-Scav` (Vorlage `ScavTanker`), (c) mein altes Tankmodell als Variante B (`ScavTankerB`, Aufbau `ScavTankB`, Engine zählt beide als Tanker mit Kapazität 10). Vorschau `tanker_vorschau.png`.
- Betrachter `pieview.py` repariert (alte PIE2-Dateien, Hex-Flags). ACHTUNG: positive Kamera-Neigung schaut von UNTEN, negative von oben.
**Noch offen, in dieser Reihenfolge angegangen (leicht → schwer):**
1. Scav-Container als Mauern (rot, blau wie das blaue Wrackauto, gelb …), später bemannbar.
2. Dritte Außenposten-Variante (graues Wellblechdach, rote Backstein-Untermauer, schwarze „Löcher“ wo bisher die schwarzen Stellen sind).
3. New-Paradigm-Gelb (sanft und knallgelb): je eine Bunkerbrecher-Variante. Farbprüfung: ist das Gelb vom Bild = NP-Gelb? Falls heller: helleres Wüstengelb als Standard.
4. Zugewiesene Artillerie: weißes Sternchen viel größer oder Sensor-Symbol (weißlicher, kräftiger).
5. MG-Sounds aus „Mayday conflict earth“ für die einfachste MG-Stufe (braucht Zugriff auf deinen Ordner, Pfad nötig).
   -> ERLEDIGT (scale_test91, 12.11. 23:47): MGDroid.wav -> MG Tier 0 (MG1Mk1, MG1-Tower, MG1-Pillbox); MGJeep.wav -> einfache Scavenger-Jeep/Buggy/Trike-MGs; MGHeli.wav -> VTOL-MGs. Skript mayday.py (Quellen in w/mayday_src), Hall-Fassung folgt automatisch (hall.py). Fund beim Einbau: die Hall-Sounds (*_h1.ogg) standen nicht in wrf/audio.wrf -> mfwrf.py läuft jetzt NACH hall.py (Fehler behoben, wäre im Spiel ein Ladefehler/Stummschaltung gewesen). losfahr*/motorstart* wurden angehört (Analyse): gleichmäßiger Motor, kein Aufheulen; für Wrack-Motorheulen daher vorerst der synthetische Sound behalten (Frage für den Schluss).
6. Wracks statt Zerplatzen: 80 % Chance, dass Fahrzeuge als Wrack (wie Autowracks) auf der Seite liegen bleiben, mit schwarzen Brandspuren und schwarzem Rauch (Engine: `droid.cpp`, Feature-Erzeugung).
7. Kampfroboter-Befehle (Besetzen/Invade, Abreißen/Raze …) aus MF exklusiv für Combots.
8. Gräben/Gruben: Symbol/Mauszeiger „hier bemannen“ (mehrere Pfeile), ausdrücklicher Rein/Raus-Befehl, Schussrichtung.
9. Tanker/Transporter halbautomatisch: nehmen Aufgabe selbst wieder auf, auch nach verlorenem Befehl; kurze Handsteuerung bleibt möglich (Autopilot-Pause 20 s existiert schon); Ausbildung startet automatisch, wenn das Gebäude fertig ist, oder Einheiten spawnen einfach.
10. Fabrik wechselt ausgespuckte Modelle (Favoriten mit höherer Chance, Schulterplatten, Erfahrungsbonus); beim Aufleveln wechselt das Modell in ein anderes Scav-Modell.
11. Drei-Ebenen-Knöpfe (eigene, schnell), weicher Wechsel (z. B. Milagro-Flieger in die Stratosphäre); Studie, wie MF die drei Ebenen und den weichen Übergang macht (aus den MF-Daten ablesen).
12. Zivilisten, kleine Busse, Eiswagenbusse (Modelle `icevan`, Bus vorhanden).
13. Wallpaper + Banner „Rise of the scavengers“: **Das kann ich nicht zeichnen** (keine Illustrationen). Ich liefere einen fertigen, ausführlichen Bild-Auftrag (Prompt) für ein Bild-KI-Werkzeug.
14. GDI-Walker (alle laut kylarsroleplay-Wiki), GDI-Walker-Artillerie mit geradem Rohr-Rückstoß, Olrox-Mehrfach-Turm-Rumpf (Forum t=4890), blaue NTW-Rümpfe (Fisch/Delfin/Wal) als Schiffe: Bildquellen müssen erst geprüft werden; Modelle komplett neu zu bauen ist sehr aufwendig.
15. Terrain feiner (Kacheln und Gitter geviertelt, Gebäudegrößen bleiben), zerstörbares Terrain wie MF beim Graben; Studie MF-Daten (`mfmap.py`). Engine-Umbau groß.
16. Stadtkarte (riesig, Hochhäuser, Highways, lange Brücken, offene Mitte als Lager), Engine-Kartenbegrenzung aufheben; Zivilisten für die Stadtmission. Danach die Scav-Kampagne ab Mission 2 (zuletzt).
17. Ein Wunsch ohne Aufwand: 360°-Varianten (Mörsergruben usw.) sind mit Ziehen-zum-Ausrichten nicht mehr nötig → werden nicht mehr gebaut. Rote Oberfläche (NTW-Bild) merke ich für die spätere Milagro-Fraktion vor.

### Q. Paket-Aufteilung (12.11. 21:48)
Das Paket ist über 25 MiB (Übertragungslimit zum PC) gewachsen und wird deshalb ab scale_test89 in zwei Dateien geteilt, beide im PC-Ordner `...\mods\autoload\`:
- `scale_mf_assets.wz.zip` (15 MB: große MF-Texturseiten + MF-Ton, ändert sich fast nie, wird nur bei Änderung neu installiert),
- `scale_test68.wz.zip` (der „Kern“, 11 MB; wird bei jeder neuen Version überschrieben; kommt aus `scale_testNN_core_stumm.wz.zip`).
Erledigt in dieser Runde: Gelb-Bunkerbrecher (Sunpunk RPG), Spikeguard-Modell wiederhergestellt (war durch Namensgleichheit gelb überschrieben), dritte Außenposten-Variante `A0BaBaOutpostB` (rote Backstein-Untermauer, graues Wellblechdach).

### R. Wracks mit Todesanimation (12.11. 23:30) — Punkt 6 oben, erweitert durch deinen Wunsch
- 80 % der zerstörten Bodenfahrzeuge (nicht Infanterie/Cyborgs/Transporter/VTOL, nicht im Wasser/auf Klippen/auf Gebäuden) werden zu einem **Wrack-Feature**, die übrigen 20 % zerplatzen wie bisher. Wrack = reine Deko (blockiert keine Kachel, nicht beschießbar), max. 80 gleichzeitig, Mindestabstand ~1/3 Kachel, 5 Minuten Lebensdauer; Qualm 2 Min jede Sekunde, danach bis 4 Min seltener; Brandherd 90 s.
- **Todesanimation** (nur wenn das Fahrzeug fuhr; steht es, kippt es an Ort und Stelle): Das Fahrzeug existiert nicht mehr, rollt aber mit Restgeschwindigkeit weiter und bremst dabei ab.
  - Variante A (60 %): 1 Kachel (50 %), 2 (40 %) oder 3 (10 %), kippt auf die Seite.
  - Variante B (28 %): 1 Kachel, leichter Schräglauf, ein Überschlag (landet auf Seite oder Dach).
  - Variante C (12 %): bis 3 Kacheln, zwei Überschläge (landet auf Seite oder Dach).
  - Weg wird auf Hindernisse (Gebäude, Klippen, Wasser, steile Hänge) geprüft, bleibt dort früher liegen. Rauchspur + Funken beim Rollen, Motorheulen am Anfang, Aufprallgeräusch passend zur Variante (A/B/C je ein eigener Sound, selbst erzeugt).
- Modelle: eigenes Wrack für jeden Scav-Rumpf (Jeep, Buggy, Trike, Camper, Eiswagen, Lkw, Pritschen-Lkw, Tanker, Bus, Feuerwehr, …) mit seiner Originaltextur, stark verkohlt (Brandflecken, Russ). Alle anderen Fahrzeuge (auch Combots): allgemeine Wracks nach Größe (S/M/L) aus den Landfill-Autos.
- Bekannte Grenzen: Wrack nach dem Laden eines Spielstands hat keine Ablaufzeit/keinen Qualm; Combot-Wracks sind noch generische Autowracks (später: echtes Combot-Teilemodell umgelegt); Aufprallgeräusche sind synthetisch, sagst du mir, was dir fehlt, ersetze ich sie (oder du nennst mir MF-Sounds, die passen).

### S. Neue Wünsche (12.11. 21:34) — in die Warteschlange hinter Punkt 6
- **Mech-Beine (Forum t=11005)** als neue Antriebsart für zusätzliche Einheiten (Thread muss erst gelesen werden: Modell-Format, Animation, Lizenz).

### T. Stand 12.11. nach Mitternacht (Commit 9d3b988, CI läuft; alles Engine-Teile brauchen die neue Programmdatei)
- **6 Wracks**: Bau-Fehler aus dem ersten CI-Lauf behoben (fehlender Header `ivisdef.h` in modwreck.cpp, sonst nichts). Wartet auf grünen Lauf.
- **8 Gräben "hier bemannen"**: Mauszeiger (Einsteigen-Symbol) über eigenem Graben, wenn Infanterie ausgewählt ist; Linksklick schickt die Infanterie in den Graben (`modoil::trenchForInfantry`, display.cpp). Im Spiel noch nicht getestet.
- **9 Auto-Ausbildung**: Transporter/Lkw-Autopilot gab es schon; neu `autoTrain` (modoil.cpp): Ausbildungs-Gebäude beginnen von selbst mit der Ausbildung, wenn sie fertig sind.
- **10 Modell-Leiter** (Engine `modvariant.cpp`, Daten `scavladder.py`, Felder `variantGroup/variantWeight/variantRank/variantRoll` im Körper):
  - Neue Fabrik-Einträge „Scavenger“ (MG) und „Scavenger Bazooka“: das Modell wird beim Ausbilden gewürfelt (Einheiten-ID als Zufall, auf allen Rechnern gleich).
  - Häufig: Scav 60, Scav-rot 28, Punk 20, Punk-rot 16, Wildmane 16, Sunpunk 16, Marauder 12. Selten (Schulterplatten): Spikeguard 6, Dread Raider 3, Dreadhawk 2. Bazooka: Sunpunk-RPG 60, Spikeguard-RPG 6, Dreadhawk-RPG 2.
  - Erfahrungsbonus: seltene Modelle starten mit ihrem Rang (Spikeguard Rang 3, Dread Raider 4, Dreadhawk 5).
  - Aufleveln: Modell wechselt in das Modell, dessen Rang dem neuen Rang entspricht (Rang 1: Punk-Familie, 2: Marauder, 3: Spikeguard, 4: Dread Raider, 5: Dreadhawk).
  - Die Einzelmodelle bleiben vorerst im Fabrikmenü (mit der alten Programmdatei gäbe es sonst nur das Grundmodell). Mit der neuen Exe kann man sie verstecken (`HIDE_SPECIFIC=1`).
  - Interpretation deiner Worte: seltene Modelle = Schulterplatten = Erfahrungsbonus. Falls du „Favoriten“ anders meintest (z. B. pro Fabrik ein Lieblingsmodell mit höherer Chance), sag es am Ende, die Gewichte stehen in `scavladder.py`.
- **5 MG-Sounds**: fertig (siehe oben). Fehler gefunden und behoben: Hall-Sounds standen nicht in der Ladeliste.

### U. Stadtkarte (13.11. nach Mitternacht) — Punkt 16, Karte v1 fertig und auf deinem PC installiert
- **Datei**: `C:\Users\MJ\AppData\Roaming\Warzone 2100 Project\Warzone 2100 master\maps\2c-Stadt.wz` (Karte „Stadt“, 2 Spieler, im Spiel unter Skirmish/Mehrspieler, Kartenliste, Tileset Arizona). Erzeugt von `citymap.py` (Zufallszahl fest 2100, also jedes Mal dieselbe Karte; Vorschau `stadt_vorschau.png`).
- **Größe 256×256 Kacheln** = Obergrenze der Engine (`MAP_MAXWIDTH/HEIGHT`). Eine größere Karte braucht Engine-Umbau (siehe offene Fragen).
- **Boden**: grauer Beton wie die Landefläche, dazu viel heller Wüstenstaub (Arizona-Kacheln der Alpha-Kampagne), Büsche und rote/rissige Erde nur außerhalb.
- **Stadt**: flach, aufgelockert (Raster 16 Kacheln, große Abstände), alle Hochhausmodelle verteilt (669 Objekte + 40 Ölquellen), reicht bis Radius ~106 Kacheln und **nicht** bis zum Rand; Wüstenstreifen außen.
- **Straßen**: Asphalt mit gelber Mittellinie, Ring-Highway um die Mitte (Radius 31–36), Hauptstraßen im Raster.
- **Fluss** (14 Kacheln breit) mit **zwei langen Brücken** (bei y=78 und y=178).
- **Mitte**: offener Platz (Radius 26) als Lager/Zufluchtsort mit Landfill-Kulissen; **Spieler 0** startet dort, **Spieler 1** in der Wüstenecke oben rechts (222|32).
- **Keine Unterführungen** (wie von dir erlaubt: wenn nicht möglich, weglassen; die Engine hat nur eine Höhenebene pro Kachel).
- **Geprüft**: mit der Kartenbibliothek des Spiels (`wzmaplib`) als Ordner und als Karte geladen → gültig. Im Spiel selbst noch nicht gestartet.
- **Noch offen für die Stadtmission**: Zivilisten + kleine Busse/Eiswagen (Punkt 12), Scav-Kampagne ab Mission 2 (zuletzt), ggf. größere Karte.

### V. Stadtkarte v2 (13.11. 00:30) — dichte Deko, Stadtplatz, Autobahnen, Diagonal-Straßen mit eigenen Kacheln (installiert, im Spiel ungetestet)
Deine Wünsche von 00:08/00:18 sind eingearbeitet. **Wichtig: Karte und Mod gehören zusammen** (beide aus demselben `citymap.py`-Lauf: die Karte benutzt 160 neue Kachelnummern 78–237, die nur im Mod `scale_test92` (Kern-Datei) existieren). Ohne den Mod zeigt die Karte falsche Bodenbilder.
- **Datei**: `...\Warzone 2100 master\maps\2c-Stadt.wz` (überschreibt v1), 256×256, 2 Spieler. Vorschau `stadt_vorschau.png`.
- **Dichte Deko**: 9341 Objekte (278 Hochhäuser, 109 Mittelbauten, 532 Häuser, dazu Autowracks an den Straßen, Büsche, Felsen, Kleinkram), nicht mehr leer. Reihenfolge nach deinem Wunsch: Straße → Staubstreifen links/rechts → Büsche → Gebäude/Wohnviertel.
- **Mitte = Stadtplatz** (Beton, Hochhäuser rundherum, kein Park). **Spieler 0 (Scav) startet dort** mit Basis; Spieler 1 in der Ecke oben rechts. 28 Start-Gebäude.
- **Ölquellen (50) nur rund um die Stadt**, keine in der Stadt.
- **Straßen**: normale Straßen, Hauptstraßen (Avenuen), **Autobahnen mit 3+3 Spuren** (nur wenige, nicht alle Straßen dick), Staub auf beiden Seiten jeder Straße.
- **4 lange schräge Alleen mit Knicken**: 45°-Diagonalen mit runden Bögen (Radius 5 Kacheln), die auf Ring/Raster treffen und nach außen aus der Karte laufen. Dafür habe ich **neue Straßenkacheln errechnet** (`roadkit.py`): aus der Original-Asphaltkachel (tile-59) wird jedes Pixel in Straßenrichtung gedreht gelesen, also gleiche Streifen, gelbe Mittellinie und ausgefranste Ränder; Kreuzungen/Einmündungen sind mit den Originalkacheln überblendet. Ergebnis: 160 eindeutige Kacheln (Engine-Grenze 255 Kacheltypen, also noch 17 Reserve), eingetragen durch `citytiles.py` in `tileset/arizonadecals.txt`, `arizonaground.txt` und die Radarfarbe (`tertilesc1hw.radar`). Nur additiv: bestehende Arizona-Karten bleiben unverändert.
- **Fluss + 2 Brücken**, keine Unterführungen (wie erlaubt).
- **Geprüft**: `wzmaplib`-Lader sagt „MAP OK“; Kachelbilder an einem Straßenknick mit `qa_tiles.py` angesehen (runder Bogen, Kreuzungen sauber). Offen: im Spiel prüfen, ob die neuen Kacheln wirklich geladen werden (Listen-Format) und ob ~9300 Objekte flüssig laufen.
- **Zivilisten/Busse (Punkt 12)**: `civilians.py` → Skript `gameinit.js` (cityLife) im Mod; läuft nur auf der Karte „Stadt“, Zivilisten als verbündeter Slot 9 laufen zwischen Plätzen, Busse/Eiswagen fahren die Straßen entlang. Ungetestet (offen: Allianz/Sichtteilung).
- **clean.py-Fehler behoben**: Scav-Leiter-Vorlagen (BaBaPeople) wurden wieder versteckt bzw. BaBaSunB umbenannt; jetzt Ausnahmen drin.
- **Neue Dateien (Skripte)**: `citymap.py`, `roadkit.py`, `citytiles.py`, `qa_tiles.py`, `civilians.py` (alle im Ausgabeordner).

### W. Funkgebrabbel am Kommandoturm / ATC-Turm (Wunsch 13.11. 00:18) — fertig, Commit a2a3810, braucht die neue Programmdatei
- **Ton** (`radio.py`, 4 Stücke 5–9 s in `audio/radio/atc_a..d.ogg`, in audio.json + Ladeliste): abgehackte, bewusst unverständliche Funkstimmen (künstliche Silben, tiefe Tower-Stimme im Wechsel mit höherer Pilot-Stimme), Zischlaute, Wortpausen, Funk-Klang (350–2800 Hz, Übersteuerung, Rauschen) und Sendetasten-Klicks. Lautstärke 55 %, Reichweite 1800 Einheiten. Nur am Spektrum geprüft, nicht angehört.
- **Engine** (`modradio.cpp/.h`, Aufruf in `loop.cpp`, Reset in `hci.cpp`): alle 6–17 s (eigener Zufall, kein gameRand) wählt es eine Quelle im Umkreis von 14 Kacheln um die Kamera – eigenes/sichtbares Kommandozentrum (REF_HQ) oder Feature „AirTrafficControl“ – und spielt dort ein Stück ab (nie zweimal dasselbe hintereinander). Nur bei sichtbarem Feld; reine Anzeige, ändert nichts am Spielzustand.
- Karte „Stadt“ enthält ATC-Tower (Feature `AirTrafficControl` ist bei den Spezialobjekten dabei) und das Kommandozentrum auf dem Platz.
- Windows-Build vom Stand 9d3b988 war rot wegen Warnung C4701 (`wreckPose` nicht initialisiert, display3d.cpp) → in a2a3810 behoben; macOS-Fehler war nur „Runner nicht verfügbar“ (Infrastruktur).
