// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (18.11. 19:09): "Die Walker-Artillerie-Kanonen sollen der GDI-Walker-Artillerie (C&C3 Juggernaut) entsprechen: halten zum Feuern -
// Stuetzklappen an den Beinen fahren aus - Torso hebt sich leicht - Kanonenrohre gehen nach oben in den Steilfeuermodus."
// (19:20 "den Mech aus dem WZ-Mod als Basis") Der GDI Juggernaut (Mech-Beine, Scorpion-Aufbau; Waffen MechGDIArtillery = drei lange Rohre
// der mittleren Panzerkanone, MechGDIHowitzer = drei kurze Haubitzen-Stummel) feuert nur verankert und nur im Stand:
//   anhalten -> je Bein klappt hinten eine Hydraulikstuetze herunter und stemmt sich in den Boden -> Aufbau hebt sich -> Rohre schwenken
//   in den Steilfeuer-Winkel (zusammen ca. 2 s) -> Feuer. Soll er weiter, faehrt er alles in umgekehrter Reihenfolge ein (ca. 1,4 s).
// Spielzustand (anhalten/feuern) im Spielschritt -> netzwerkgleich; Stuetzen, Anheben und Rohrwinkel sind reine Anzeige.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include "lib/framework/types.h"
#include "lib/ivis_opengl/pietypes.h"
#include <glm/fwd.hpp>

struct DROID;
struct iIMDShape;

namespace modgdiart
{
/// GDI Juggernaut (feuert nur verankert)?
bool isGdi(const DROID *psDroid);
/// ist das eine Juggernaut-Waffe?
bool isGdiWeapon(uint32_t weaponStat);
/// aus combFire: darf jetzt gefeuert werden? Wenn nicht verankert, beginnt hier das Verankern
bool readyToFire(DROID *psDroid);
/// aus der Bewegung: steht fest (verankert, beim Verankern oder Einfahren)
bool halted(const DROID *psDroid);
/// jeden Spielschritt: soll er laufen, wird eingefahren
void tick();
/// Anzeige: Verankerung 0 (Marsch) .. 1 (verankert)
float deployFraction(const DROID *psDroid, UDWORD time);
/// Anzeige: um so viel hebt sich der Rumpf (Modell-Einheiten)
float torsoLift(const DROID *psDroid, UDWORD time);
/// Anzeige: Rohrwinkel (WZ-Winkel) aus dem Zielwinkel des Spiels und dem Stand der Verankerung
uint16_t displayPitch(const DROID *psDroid, uint16_t aimPitch, UDWORD time);
/// Anzeige: Stuetzen an den Beinen zeichnen (bodyMatrix = Lage des Rumpf-Modells)
bool drawStruts(const DROID *psDroid, const glm::mat4 &bodyMatrix, const glm::mat4 &viewMatrix, int colour, PIELIGHT brightness, int pieFlag, float heightAboveMap);
/// Anzeige: Turm (mech_gditurret.pie, ohne Neigung) am Drehpunkt der Rohre zeichnen (mountMatrix = Lage vor dem Neigen der Rohre)
bool drawHousing(const DROID *psDroid, const glm::mat4 &mountMatrix, const glm::mat4 &viewMatrix, int colour, PIELIGHT brightness, int pieFlag, int pieFlagData, float heightAboveMap);
/// Spielende / Laden
void reset();
}
