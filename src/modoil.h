// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Oelkreislauf. Oeltuerme erzeugen Faesser (bis 5 am Turm) statt direkt Energie, Scav-Lkw mit Ladeflaeche (Autopilot) bringen sie zum
// Kraftwerk (oder zuerst ins Lager = Wasserturm, +10 %), jedes Fass haelt das Kraftwerk (und damit das Stromnetz) 3 Minuten am Laufen.
// Oelpipelines verbinden Oelturm und Kraftwerk/Lager ohne Lkw. Zerstoerte Pipeline-Stuecke werden zum Bruchstueck (Feuer, Rauch) und
// durch Reparatur wieder heil. Fass-Haufen (OilDrum5 / OilDrum10) und beladene Lkw explodieren mit Kettenreaktion.
// Gilt nur im Mehrspieler/Gefecht fuer menschliche Spieler und nur, wenn die Mod-Daten (A0BaBaPipeline) geladen sind.
#pragma once

#include <cstdint>
#include <vector>

#include <cstdint>
#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

struct BASE_OBJECT;
struct STRUCTURE;
struct FEATURE;
struct DROID;
struct WorldMapState;

namespace modoil
{
/// gilt die Fass-Wirtschaft fuer diesen Spieler?
bool manages(int player);
/// (17.11. 20:35) Einnahmen-Buch: jede Spielminute steht im Log, woher die Energie kam ("MOD-Wirtschaft ...")
enum IncomeKind { INC_BARRELS = 0, INC_SOLAR, INC_LAVA, INC_SALVAGE, INC_COUNT };
void noteIncome(int player, int kind, int amount);
/// (17.11. 20:35) Gebaeude fertig gebaut: Kraftwerk -> ein Oeltanker und ein Fracht-Lkw stehen sofort daneben, Raffinerie -> ein Fracht-Lkw
void onBuilt(STRUCTURE *psStruct);
/// aus power.cpp: Oelpumpe foerdert (amountFP Energie in Festkomma mit fpOne = 1,0) -> wird zu Fass-Fortschritt am Turm
void extract(const STRUCTURE *psDerrick, int64_t amountFP, int64_t fpOne);
/// jeden Spielschritt aus der Spielschleife (Explosionen sofort, Logistik einmal je Spielsekunde)
void tick();
/// laeuft das Kraftwerk (hat es Brennstoff)? Immer wahr fuer Spieler ohne Fass-Wirtschaft.
bool generatorRunning(const STRUCTURE *psGen);
/// Echte Infanterie (Beine UND Personen-/Cyborg-Typ). Kampfroboter (Combots) haben auch Beine, zaehlen aber nicht.
bool isInfantry(const DROID *psDroid);
/// nur echte Infanterie (Personen) darf besetzbare Haeuser betreten; Cyborgs nicht (Graeben/Gruben duerfen beide)
bool isHouseOccupant(const DROID *psDroid);
/// steht auf diesem Feld ein besetzbares Haus (Feature mit infantryPassable)?
bool tileHasHouse(const WorldMapState &mapState, int tileX, int tileY);
/// steht auf diesem Feld ein fertiges Bauwerk mit "infantryPassable" (Graben, Grube) oder ein besetzbares Haus?
bool tilePassableForInfantry(const WorldMapState &mapState, int tileX, int tileY);
/// ist das ein eigener, fertiger Graben/eine Grube, in die die gewaehlte Infanterie (Beine) einruecken kann? (Mauszeiger "bemannen")
bool trenchForInfantry(const BASE_OBJECT *obj, const DROID *dominantSelected);
/// steht die Infanterie (Beine) im Graben / in der Grube? (Schadensminderung)
bool droidInTrench(const DROID *psDroid);
/// (Wunsch "Container-Mauern bemannbar", 17.11.) Infanterie auf einer Container-Mauer steht auf dem Dach: Hoehe ueber dem Boden, sonst 0
int roofHeight(const DROID *psDroid);
/// ist das ein Lkw im Fass-Autopilot? (Ebenen-Schaechte duerfen ihn nicht umschalten)
bool isCargo(uint32_t droidId);
/// faehrt der Lkw gerade Oel (Auftrag laeuft oder Faesser geladen)?
bool isBusy(uint32_t droidId);
/// Hooks beim Zerstoeren (vor dem Entfernen aufrufen)
void onStructureDestroyed(const STRUCTURE *psDel);
void onFeatureDestroyed(const FEATURE *psDel);
void onDroidDestroyed(const DROID *psDel);
/// (16.11.) Tankstelle (Reparatureinrichtung): Vorrat in Promille-Faessern (1000 = 1 Fass, hoechstens stationCapMilli()).
/// Lkw bringen Oelfaesser aus der Oelproduktion hin. Spieler ohne Oel-Wirtschaft (KI): immer voll.
int stationCapMilli();
int stationMilli(const STRUCTURE *psStation);
/// Treibstoff entnehmen; gibt die entnommene Menge (milli) zurueck
int stationTake(const STRUCTURE *psStation, int milli);
/// ganze/angebrochene Faesser an der Tankstelle (Anzeige)
int stationBarrels(const STRUCTURE *psStation);
/// Faesser-Anzeige (Anzeigemodus 4) und Fass-Fenster der gewaehlten Tankstelle, nach dem 3D-Bild aufrufen
void drawOverlay(const glm::mat4 &perspectiveViewMatrix);
/// beim Spielende / Ladevorgang
void reset();
/// Spielstand: Faesser an Oeltuermen, Lagern, Raffinerien, Kraftwerken und Tankstellen (Tripel Art, id, Wert)
void saveState(std::vector<int64_t> &out);
void loadState(const std::vector<int64_t> &in);
}
