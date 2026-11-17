// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 21:34): "Die Lasersatelliten muss man selber bauen. Baue sie aus den Solarpaneelen und vielleicht Faessern oder anderen
// Parts (WZ-Intro / Menuebild als Anlehnung, grau mit roter Aufschrift NASDA). Erst wenn die Satelliten fertig gebaut sind, kann man mit der
// Satellitenkommunikationszentrale (die die Scavs jetzt auch bauen koennen) diese Faehigkeit einsetzen." Bauweg: Raketensilo UND Orbit-Insel.
//
// (22:13) "Nicht auf dem Erdboden. Nur im Orbit, oder er wird mit der Rakete hochgeschossen - als Modul an die Rakete angehaengt."
//  - Bauwerk "A0LaserSatellite" (NASDA-Satellit in der Montagewiege) nur auf einer Orbit-Insel (Cyborg-Pioniere): startet nach 6 s von selbst,
//  - "A0SatelliteModule": Modul auf dem Raketensilo (Forschung R-Sys-LaserSatellite); die naechste fertige Rakete nimmt den Satelliten mit.
// Hoechstens 3 Satelliten je Spieler. Die Uplink-Zentrale (A0Sat-linkCentre, Waffe LasSatUplink) feuert den Laser wie die
// Lasersatelliten-Kommandozentrale (Zentrale waehlen, Ziel anklicken), aber nur mit mindestens einem Satelliten in der Umlaufbahn.
// Nachladezeit: 1 Satellit 300 s, 2 Satelliten 200 s, 3 Satelliten 150 s. Spiellogik nur aus Spielzeit und Spielzustand (mehrspielersicher).
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

struct STRUCTURE;
struct STRUCTURE_STATS;

namespace modsat
{
constexpr int MAX_SATELLITES = 3;

bool isSatellite(const STRUCTURE_STATS *psStats);
/// Satelliten des Spielers in der Umlaufbahn
int satellites(int player);
/// darf diese Lasersatelliten-Station jetzt feuern? (Uplink-Zentrale: nur mit Satelliten und nach der Nachladezeit; sonst wie bisher)
bool canFire(const STRUCTURE *psStruct);
/// Schuss abgelehnt: Hinweis fuer den eigenen Spieler (kein Satellit / laedt noch)
void denied(const STRUCTURE *psStruct);
/// (22:13) Satelliten-Modul "A0SatelliteModule" (wie ein Fabrikmodul auf das Raketensilo gesetzt; die naechste Rakete nimmt den Satelliten mit)
bool isModule(const STRUCTURE_STATS *psStats);
/// Index des Satelliten-Moduls in asStructureStats (-1 = nicht geladen)
int moduleStatIndex();
/// die Rakete mit dem Satelliten ist oben angekommen
void addSatellite(int player);
/// (21:55) der Laser wurde abgefeuert: die Satelliten des Spielers gluehen am Orbit-Himmel (Linsenring)
void fired(int player, int targetX, int targetY);
/// jeden Spielschritt aus der Spielschleife (nach modrocket::tick)
void tick();
/// Start von der Orbit-Insel, Satelliten am Orbit-Himmel
void draw3D(const glm::mat4 &viewMatrix);
void reset();
}
