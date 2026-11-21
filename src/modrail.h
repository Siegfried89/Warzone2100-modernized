// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 21.11. 00:21): Zug-Transportsystem - "Schienenbau und Zuege mit Waggons und Anhaengern, die z. B. Infanterie von einer Basis
// schnell in die andere bringen, auch wenn sie weit ueber die Karte entfernt ist." Loks nach Art des gelben Scav-Schulbusses und des
// Feuerwehrwagens, Waggons im Scav-Stil (Modelle und Werte im Mod: trains.py).
//
//  - Schienen: Bauwerk "A0RailTrack..." (1 Feld, "passable": alle Einheiten fahren darueber), von Baufahrzeugen gebaut - wie Mauern als
//    Linie gezogen oder frei als Strecke mit dem Schlachtplan-Knopf "Rail line". Benachbarte Schienenfelder verbinden sich von selbst
//    (gerade, schraeg, Kurven; schraeg nur, wenn kein gerader Weg ueber die Nachbarfelder fuehrt). Die Engine zeichnet die Gleisstuecke
//    selbst entlang der Verbindungen (Schotter, Schwellen, zwei Schienen, folgt dem Gelaende).
//  - Lokschuppen: Fabrik mit "trainDepot": true - baut nur Schienenfahrzeuge (und die normalen Fabriken keine); die neuen Fahrzeuge
//    stehen auf dem naechsten Gleis. Neue Waggons haengen sich an eine Lok in der Naehe.
//  - Zuege: Loks fahren nur auf Schienen - Befehl "Fahren" sucht den kuerzesten Weg im Gleisnetz zum Gleis naechst dem Ziel; schnell,
//    mit Anfahren und Bremsen. Waggons fahren nicht selbst: Waggon waehlen und auf eine Lok klicken = ankuppeln. Liegt das Ziel hinter
//    dem Zug, schiebt die Lok den Zug rueckwaerts. Gesperrtes/zerstoertes Gleis: der Zug haelt davor.
//  - Truppenwaggons nehmen Infanterie auf wie der Schuetzenpanzer (modapc): Infanterie auf den Waggon oder die Lok klicken = einsteigen,
//    Knopf "Unload" an der Lok oder am Waggon = alle steigen aus.
// Spielzustand nur aus Spielzustand gerechnet (ganzzahlig, feste Reihenfolge) - auf allen Rechnern gleich. Gekuppelte Zuege stehen nicht
// im Spielstand: nach dem Laden kuppeln Waggons, die direkt hinter einer Lok stehen, von selbst wieder an.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include "lib/ivis_opengl/pietypes.h"
#include <glm/fwd.hpp>
#include <cstdint>

struct DROID;
struct STRUCTURE;
struct STRUCTURE_STATS;
struct DROID_TEMPLATE;

namespace modrail
{
/// Schienen-Bauwerk?
bool isRailStats(const STRUCTURE_STATS *psStats);
bool isRail(const STRUCTURE *psStruct);
/// Schienenfahrzeug (Rumpf "Train..."), Lok ("TrainLoco..."), Waggon ("TrainWagon...")
bool isTrain(const DROID *psDroid);
bool isLoco(const DROID *psDroid);
bool isWagon(const DROID *psDroid);
/// Vorlage eines Schienenfahrzeugs (fuer die Fabrik-Auswahl)
bool isTrainTemplate(const DROID_TEMPLATE *psTempl);
/// die Lok, an der dieser Waggon haengt (0 = keine); fuer eine Lok die eigene Kennung
uint32_t locoOf(const DROID *psDroid);
/// freie Plaetze in allen Truppenwaggons des Zuges dieser Lok
int freeSeats(const DROID *psLoco);

/// move.cpp moveDroidToBase: Fahrbefehl fuer ein Schienenfahrzeug (true = Weg gefunden). Waggons geben den Befehl an ihre Lok weiter.
bool routeTrain(DROID *psDroid, int worldX, int worldY);
/// move.cpp moveUpdateDroid: Bewegung eines Schienenfahrzeugs (Loks fahren den ganzen Zug, Waggons tun selbst nichts)
void updateTrain(DROID *psDroid);
/// structure.cpp: neues Fahrzeug aus dem Lokschuppen - aufs naechste Gleis stellen, Waggons an eine Lok in der Naehe kuppeln
void onFactoryDroid(DROID *psDroid, const STRUCTURE *psFactory);
/// jeden Spielschritt (vor modapc::tick): Gleisnetz, Ankuppeln, Absitz-Knopf der Lok an die Waggons weitergeben, Aufraeumen
void tick();
void reset();

/// display3d.cpp renderStructure: Gleisstuecke eines Schienenfeldes zeichnen (true = gezeichnet, das Bauwerksmodell entfaellt)
bool drawRail(const STRUCTURE *psStruct, PIELIGHT brightness, const glm::mat4 &viewMatrix);
}
