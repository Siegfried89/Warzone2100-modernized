// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 20:44): "Nachts sollen viele schoene Ambient-Lichter leuchten (wie die Landezonen, nur nicht blinkend, etwas kleiner
// und schoen warmweiss) an Kraenen, Tuermen, Kontrolltuermen, Masten, anderen Tuermen und was sonst an Gebaeuden Sinn macht."
// Je nach Tageszeit (modweather) blenden warmweisse Lichtpunkte auf den Spitzen von Masten, Kraenen, Tuermen, Bohrtuermen usw. und an den
// Dachecken grosser Gebaeude auf; die naechsten zur Kamera werfen zusaetzlich warmes Licht auf die Umgebung. Reine Anzeige.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

namespace modnightlights
{
/// nach den Objekten im 3D-Bild aufrufen (wie die Landezonen-Lichter)
void draw3D(const glm::mat4 &viewMatrix);
}
