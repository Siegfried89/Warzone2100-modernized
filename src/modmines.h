// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 18:13): Minen fuer den Minenfeld-Marker (modpaint).
//
// Bauwerk "A0Mine" (Mod-Paket bunkermine.py: kleiner, in die Erde versunkener Betonbunker, "passable" = ueberfahrbar):
//  - fuer Gegner unsichtbar: nach jeder Sichtberechnung wird die Mine fuer fremde Spieler wieder versteckt - ausser eine ihrer
//    Einheiten steht direkt davor (1,5 Felder) oder ein Sensor-Fahrzeug sucht in 4 Feldern Umkreis (Minensuche);
//  - explodiert, sobald eine feindliche Bodeneinheit darueberfaehrt: grosse Explosion, schwerer Schaden in knapp einem Feld Umkreis
//    (nur Bodeneinheiten und Bauwerke), die Mine ist danach weg.
// Alles in der Spielzeit (gameTime, Spielobjekt-Reihenfolge) und damit im Mehrspieler synchron.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

namespace modmines
{
/// jeden Spielschritt direkt nach processVisibility(): Minen verstecken, Ausloeser pruefen
void tick();
/// (Wunsch 17.11. 18:48) "Mit Shift sieht man alle Minen besser, weil sie ein gelbes Icon bekommen" - nur Anzeige
void drawOverlay(const glm::mat4 &perspectiveViewMatrix);
void reset();
/// (Wunsch 17.11. 19:36) Faehigkeit "Minen raeumen": gewaehlte Infanterie (sonst alle) geht zu den bekannten feindlichen Minen und
/// entschaerft sie (Befehle laufen ueber ModeQueue = Mehrspieler-synchron); Rueckgabe: Anzahl losgeschickter Infanteristen
int clearMines(unsigned player);
}
