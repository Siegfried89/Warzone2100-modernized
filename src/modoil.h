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
struct iIMDShape;
#include "lib/ivis_opengl/pietypes.h"   // PIELIGHT

namespace modoil
{
/// gilt die Fass-Wirtschaft fuer diesen Spieler?
bool manages(int player);
/// (Wunsch 21.11. 15:49 "den Abholbefehl zusaetzlich selber geben koennen, falls sich dennoch mal nichts tut"): Pickup/Oeltanker
/// waehlen und auf ein eigenes Oel-Gebaeude klicken = dort abholen bzw. abliefern, danach weiter in der Dauerschleife.
constexpr uint16_t CODE_OIL = 97;   // in DORDER_FORTIFY.direction
bool isOilTarget(const DROID *psDroid, const STRUCTURE *psStruct);
/// order.cpp DORDER_FORTIFY: true = Oel-Befehl (uebernommen oder verworfen)
bool startOrder(DROID *psDroid, int x, int y, uint16_t code);
/// (17.11. 20:35) Einnahmen-Buch: jede Spielminute steht im Log, woher die Energie kam ("MOD-Wirtschaft ...")
enum IncomeKind { INC_BARRELS = 0, INC_SOLAR, INC_LAVA, INC_SALVAGE, INC_NUCLEAR, INC_COUNT };
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
/// (Wunsch 18.11. 02:44) geladene Faesser/Kisten sichtbar auf der Ladeflaeche (Scav-Lkw ScavBed*, nicht beim Oeltanker mit geschlossenem Tank).
/// bed = Modell der Ladeflaeche, m = ihre Matrix (wie beim Zeichnen des Bauteils), Rest wie pie_Draw3DShape. Nur Anzeige.
void drawCargo(const DROID *d, const iIMDShape *bed, const glm::mat4 &m, const glm::mat4 &viewMatrix, int team, PIELIGHT brightness, int pieFlag,
               int pieFlagData, float stretchDepth);
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
/// (Wunsch 18.11. 12:45 "es gibt keinen Ladebalken am Oellagerturm") Faesser im Oellager (roh + veredelt); -1 = kein Oellager. cap = Plaetze
int storeBarrels(const STRUCTURE *psStructure, int &cap);
/// ganze/angebrochene Faesser an der Tankstelle (Anzeige)
int stationBarrels(const STRUCTURE *psStation);
/// Faesser-Anzeige (Anzeigemodus 4) und Fass-Fenster der gewaehlten Tankstelle, nach dem 3D-Bild aufrufen
void drawOverlay(const glm::mat4 &perspectiveViewMatrix);
/// (20.11.) Strompflicht: erst ab dem ersten Fass, das ein Kraftwerk dieses Spielers bekommen hat (vorher laeuft alles ohne Strom)
bool dutyStarted(int player);
/// (20.11.) eine echte Stromquelle (Kraftwerk mit Oel, Atomkraftwerk, Solar am Umspannwerk) hat das Netz gespeist -> Strompflicht beginnt
void noteDuty(int player);
/// (20.11.) Fracht-Lkw oder Oeltanker (Fahrzeug mit Ladeflaeche/Tank fuer Faesser)?
bool isCargoVehicle(const DROID *psDroid);
/// (20.11. 19:06) Fortschritt des laufenden Fasses in der Raffinerie 1..1000; 0 = arbeitet gerade nicht; -1 = keine Raffinerie
int refineryProgress(const STRUCTURE *psStruct);
/// (20.11.) geladene Faesser eines Fracht-Lkw / Oeltankers; cap = Plaetze (0 = kein Frachtfahrzeug)
int cargoBarrels(const DROID *psDroid, int &cap);
/// (20.11.) Vorrat des Kraftwerks in Sekunden
int generatorFuel(const STRUCTURE *psGen);
/// (20.11. Wunsch "UI-Grafik fuer den Oelwirtschaftskreislauf") Zahlen fuer das Oel-Fenster
struct EconomyView
{
	bool active = false;                       // Spieler hat die Fass-Wirtschaft
	int derricks = 0, derrickBarrels = 0, derrickCap = 0, derricksFull = 0;
	int pickups = 0, pickupBarrels = 0, pickupCap = 0, pipeDerricks = 0;
	int refineries = 0, refIn = 0, refOut = 0, refCap = 0, refNoPower = 0;
	int tankers = 0, tankerBarrels = 0, tankerCap = 0;
	int stores = 0, storeBarrels = 0, storeCap = 0;
	int gens = 0, gensRunning = 0, gensStandby = 0, gensHungry = 0, minFuel = -1;
	int stations = 0, stationBarrels = 0, stationCap = 0;
	int power = 0, powerMax = 0, incomeLastMinute = 0, wastedLastMinute = 0;
	int buildingsDown = 0;
};
EconomyView economyView(int player);
/// (20.11.) Kraftwerk mit wenig Oel (hoechstens 3 Minuten) - fuer die Warnanzeige
bool generatorHungry(const STRUCTURE *psGen);
/// beim Spielende / Ladevorgang
void reset();
/// Spielstand: Faesser an Oeltuermen, Lagern, Raffinerien, Kraftwerken und Tankstellen (Tripel Art, id, Wert)
void saveState(std::vector<int64_t> &out);
void loadState(const std::vector<int64_t> &in);
}
