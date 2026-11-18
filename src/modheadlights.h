// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 18.11. 17:09): "Alle Fahrzeuge bekommen nachts zwei Scheinwerfer." Vorbild: die Art-Revolution-Mod (nach den
// Original-Zwischensequenzen gebaut) hat an vielen Rumpfen Scheinwerfer auf der Textur (Cobra, Scorpion, Mantis, Tiger, Jeep ...);
// deren Lage steht je Rumpf in body.json ("headlight": [x, y, z], headlights.py). Je nach Tageszeit (modweather) leuchten an jedem
// Bodenfahrzeug zwei Scheinwerfer (Lichthof an der Lampe) und werfen zwei Lichtkegel nach vorn auf den Boden (echte Kegel-Lichter
// der Engine, die naechsten zur Kamera). (17:30 "schau dir unbedingt das Video an - die weissen Lichtkegel sind da gut zu sehen", WZ-Intro):
// dazu ein sichtbarer weisser Lichtkegel in der Luft vor jeder Lampe und zwei rote Ruecklichter hinten. Reine Anzeige.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

namespace modheadlights
{
/// nach den Objekten im 3D-Bild aufrufen (wie die Nachtlichter)
void draw3D(const glm::mat4 &viewMatrix);
}
