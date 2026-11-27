// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Wracks. Zerstoerte Fahrzeuge zerplatzen nicht immer, sondern bleiben mit 80 % Wahrscheinlichkeit als verkohltes Wrack auf der
// Seite liegen (Feature "ModWreckS*/M*/L*" je nach Rumpfgroesse, Modelle aus den Landfill-Autowracks mit Brandspuren). Das Wrack qualmt
// eine Weile, brennt am Anfang kurz und verschwindet nach etwa 25 Minuten. Nach der Landung blockiert es seine Kachel (Fahrzeuge muessen drumherum).
// Nur wenn die Mod-Daten (Feature ModWreckS0) geladen sind; deterministisch (gameRand), gilt fuer alle Spieler gleich.
#pragma once

#include <cstdint>
#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <vector>
#include "modslots.h"

struct DROID;
struct FEATURE;
struct PROJECTILE;
struct FEATURE_STATS;
struct GameWorld;

namespace modwreck
{
/// ist das ein Wrack-Feature? (Namen beginnen mit "ModWreck")
bool isWreckStats(const FEATURE_STATS *stats);
/// Anzeige: Lage des Wracks waehrend der Todesanimation (renderTime = graphicsTime). false = Wrack liegt (Lage steht im Feature).
struct Placement { int x, y, z, pitch, roll; };   // Winkel als 16-Bit-Werte
bool pose(const FEATURE *f, unsigned renderTime, Placement &out);
/// aus destroyDroid() vor dem Entfernen aufrufen. Gibt true zurueck, wenn ein Wrack entstanden ist (dann entfallen die Truemmer-Effekte).
bool onDroidDestroyed(const DROID *psDel, GameWorld &world);
/// (Wunsch 23.11. 03:05) droidDamage(): der toedliche Treffer, kurz vor destroyDroid(). Eine starke Explosion (Artillerie, Bomben,
/// Lenkflugkoerper, Munitions-/Oelexplosion, grosser Wirkradius - mindestens ein Viertel der Lebenspunkte auf einmal) zerlegt das
/// Fahrzeug: Turm und Laufwerk fliegen weg, der Rumpf wird hochgeschleudert, dreht sich, faellt herab und bleibt als bergbares Wrack liegen.
void noteKill(const DROID *psDroid, unsigned damage, const PROJECTILE *psProjectile, int weaponSubClass, bool isDamagePerSecond);
/// Anzeige: Rumpf-Wrack (zeigt den echten Rumpf, verkohlt dunkler)
bool charred(const FEATURE *f);
/// jeden Spielschritt aus der Spielschleife (Qualm und Ablauf einmal je Spielsekunde)
void tick(GameWorld &world);
/// beim Spielende / Ladevorgang
void reset();
/// (Wunsch 27.11. 22:05) Kachel ist nur von Wracks gesperrt: Fahrzeuge (Rad/Kette/Halbkette/Luftkissen) duerfen hinein und schieben
/// das Wrack beiseite (modwreck::tick); Infanterie und Cyborgs bleiben ausgesperrt. Nur fuer die Oberflaechenkarte.
bool pushableTile(int tileX, int tileY);

/// (17.11., Wunsch 25 "Bergen") Bauplan des zerstoerten Fahrzeugs, das im Wrack steckt - ein Abschlepp-Lkw kann es zur Reparaturstation
/// bringen, dort wird daraus wieder ein (stark beschaedigtes) Fahrzeug fuer den Spieler, der es geborgen hat.
struct WreckInfo
{
	uint8_t droidType = 0;
	uint8_t numWeaps = 0;
	uint32_t parts[16] = {};     // Bauteile je COMP_* (wie DROID::asBits, DROID_MAXCOMP Eintraege)
	uint32_t weaps[MAX_WEAPONS] = {};   // Waffen
	ModSlots slots;              // (23.11. 02:16) Turm-Belegung (Anschluesse, Systemtuerme neben Waffen)
	const FEATURE_STATS *stats = nullptr;   // Wrack-Modell (Anzeige auf dem Abschlepp-Lkw)
};
/// gelandetes Wrack mit bekanntem Bauplan? (Wracks aus alten Spielstaenden haben keinen)
bool wreckInfo(const FEATURE *f, WreckInfo &out);
/// (Wunsch 27.11.) Wracks liegen frei (keiner Kachel zugeordnet) - die Maus fand sie darum nie: gelandetes Wrack auf dieser Kachel
/// (nullptr = keins). Fuer Mauszeiger und Klick (Angriff mit allen Waffen, Bergen).
FEATURE *wreckAtTile(GameWorld &world, int tileX, int tileY);
/// liegt dieses Wrack schon (Todesanimation vorbei)? Wracks ohne eigenen Eintrag (Karte) gelten als liegend.
bool landed(const FEATURE *f);
/// (Wunsch 27.11. "durch die Bagger Wracks bergen") Wrack wegraeumen: Kachel-Sperre aufheben, Feature entfernen. Gibt den Schrottwert
/// zurueck (ein Zehntel der Baukosten des Fahrzeugs, ohne Bauplan 15), -1 = kein liegendes Wrack.
int clearWreck(FEATURE *f, GameWorld &world);
/// Wrack wird aufgeladen: Bauplan holen, Kachel-Sperre aufheben, Feature entfernen. false = kein bergbares Wrack
bool takeWreck(FEATURE *f, GameWorld &world, WreckInfo &out);

/// (17.11.) Spielstand (modsave): Wracks mit Alter, Lage und Bauplan; nach dem Laden laufen Lebensdauer/Qualm weiter und sie bleiben bergbar
struct SavedWreck
{
	uint32_t featureId = 0;
	unsigned age = 0;
	int xe = 0, ye = 0, halfH = 12, halfW = 12, rollTotal = 90;
	bool hasInfo = false;
	bool hull = false;           // (23.11.) hochgeschleuderter Rumpf: zeigt das Rumpf-Modell (aus info.parts)
	WreckInfo info;
};
std::vector<SavedWreck> saveState();
void loadState(const std::vector<SavedWreck> &wrecks, GameWorld &world);
}
