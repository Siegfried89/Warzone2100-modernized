// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Transport-Dauerschleife wie in Supreme Commander (Wunsch 14.11., Punkte 7, 18, 23).
//
// Man baut in der Heimatbasis einen Flugkontrollturm (A0BaBaATC, das WZ-Flugleitturm-Modell) mit einem Transport-Leuchtfeuer
// (A0BaBaTransportBeacon, kleiner Landeplatz mit blinkenden Lichtern) daneben, und an der Front ein weiteres Leuchtfeuer.
// Ab dann pendeln die eigenen Transporter selbststaendig: Einheiten, die am Heimat-Leuchtfeuer warten (z. B. neu gebaute Einheiten,
// deren Sammelpunkt dort liegt), steigen ein, der Transporter fliegt zum neuesten Front-Leuchtfeuer, laedt ab und kehrt zurueck.
// Das zuletzt gebaute Leuchtfeuer ist die Abladestelle (es hat damit Vorrang). Gilt fuer alle Spieler (auch KI) gleich, rechnet nur mit
// Spielzustand (synchron). In der Kampagne sind Transporter Teil der Missionen und bleiben unangetastet.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

struct GameWorld;

namespace modferry
{
/// jeden Spielschritt (rechnet selbst nur jede Sekunde)
void tick(GameWorld &world);
/// Anzeige: Transportschleife (Pfeil im Bildschirm)
void drawOverlay(const glm::mat4 &perspectiveViewMatrix);
/// (22.11.) Anzeige: SupCom-Transportpfeil zwischen zwei Weltpunkten (Angriffsplaner: Fahrschleife Leuchtfeuer -> Brueckenkopf)
void drawRoute(const Vector2i &from, const Vector2i &to, const glm::mat4 &perspectiveViewMatrix, bool hot);
/// Anzeige im 3D-Bild: Landezonen-Lichter der Leuchtfeuer, rot / blau / gruen nach Sicherheit (17.11. 18:18)
void draw3D(const glm::mat4 &viewMatrix);
/// Spielende / Laden
void reset();
}
