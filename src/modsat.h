// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 21:34): "Die Lasersatelliten muss man selber bauen. Baue sie aus den Solarpaneelen und vielleicht Faessern oder anderen
// Parts (WZ-Intro / Menuebild als Anlehnung, grau mit roter Aufschrift NASDA). Erst wenn die Satelliten fertig gebaut sind, kann man mit der
// Satellitenkommunikationszentrale (die die Scavs jetzt auch bauen koennen) diese Faehigkeit einsetzen." Bauweg: Raketensilo UND Orbit-Insel.
//
// Bauwerk "A0LaserSatellite" (NASDA-Satellit in der Montagewiege, Forschung R-Sys-LaserSatellite):
//  - an der Oberflaeche gebaut: ein eigenes Raketensilo mit bereitstehender Rakete (hoechstens 8 Felder entfernt) laedt ihn ein und startet;
//    nach dem Flug ist der Satellit in der Umlaufbahn,
//  - auf einer Orbit-Insel gebaut (Cyborg-Pioniere): startet nach 6 s von selbst in die Umlaufbahn.
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
/// Raketensilo mit bereitstehender Rakete: einen fertigen Satelliten in der Naehe einladen (true = geladen, Silo startet)
bool loadOnto(const STRUCTURE *psSilo);
/// die Rakete mit dem Satelliten ist oben angekommen
void addSatellite(int player);
/// (21:55) der Laser wurde abgefeuert: die Satelliten des Spielers gluehen am Orbit-Himmel (Linsenring)
void fired(int player);
/// jeden Spielschritt aus der Spielschleife (nach modrocket::tick)
void tick();
/// Start von der Orbit-Insel, Satelliten am Orbit-Himmel
void draw3D(const glm::mat4 &viewMatrix);
void reset();
}
