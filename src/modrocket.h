// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 20:56): "Es gibt noch eine Moeglichkeit in den Orbit zu kommen: Raketen, die in einem Raketensilo gebaut werden, die dann
// gestartet werden und Bauarbeiter dort oben abladen, um dort eine Orbitbasis mit Solarpaneelen zu bauen." Entscheidung: Scav-Eigenbau-Rakete,
// Bauarbeiter = Cyborg-Pioniere (mit Sprung/Flugfaehigkeit).
//
// Raketensilo (Bauwerk "A0RocketSilo", Forschung): baut von selbst eine Rakete (Kosten beim Beginn, 75 s, nur mit Strom - PowerDown haelt an);
// sie faehrt dabei aus dem Silo hoch. Steht die Rakete bereit, steigen Cyborg-Pioniere ein, die man zum Silo schickt (hoechstens 3). Der erste
// Einstieg startet den Countdown (8 s, Dampf), dann hebt die Rakete ab (Flamme, Rauchsaeule) und die Pioniere landen auf der Orbit-Insel ueber
// dem Silo. Danach baut das Silo die naechste Rakete. Spiellogik nur aus Spielzeit und Spielzustand (mehrspielersicher); Anzeige getrennt.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

struct DROID;
struct STRUCTURE_STATS;

namespace modrocket
{
bool isSilo(const STRUCTURE_STATS *psStats);
/// jeden Spielschritt aus der Spielschleife
void tick();
/// sitzt die Einheit gerade in einer Rakete (nicht zeichnen, nicht waehlbar)?
bool aboard(const DROID *psDroid);
/// Rakete, Dampf, Startflamme (nach den Objekten im 3D-Bild)
void draw3D(const glm::mat4 &viewMatrix);
void reset();
}
