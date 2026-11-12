// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Wracks. Zerstoerte Fahrzeuge zerplatzen nicht immer, sondern bleiben mit 80 % Wahrscheinlichkeit als verkohltes Wrack auf der
// Seite liegen (Feature "ModWreckS*/M*/L*" je nach Rumpfgroesse, Modelle aus den Landfill-Autowracks mit Brandspuren). Das Wrack qualmt
// eine Weile, brennt am Anfang kurz und verschwindet nach etwa 5 Minuten. Es blockiert keine Kachel (reine Deko).
// Nur wenn die Mod-Daten (Feature ModWreckS0) geladen sind; deterministisch (gameRand), gilt fuer alle Spieler gleich.
#pragma once

#include <cstdint>
#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)

struct DROID;
struct FEATURE;
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
/// jeden Spielschritt aus der Spielschleife (Qualm und Ablauf einmal je Spielsekunde)
void tick(GameWorld &world);
/// beim Spielende / Ladevorgang
void reset();
}
