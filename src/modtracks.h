// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 18.11. 15:22 "Alle Fahrzeuge hinterlassen Fahrspuren"): Ketten-, Halbketten- und Radfahrzeuge legen beim Fahren
// Abdruecke auf den Boden (je Laufwerk ein Streifen, an der echten Lage der Ketten/Raeder), die sich dem Gelaende anschmiegen und nach
// ein bis zwei Minuten verblassen. Nicht auf Wasser und Klippen, auf Strasse/Beton nur schwach. Reine Anzeige (kein Einfluss aufs Spiel).
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

namespace modtracks
{
/// einmal je gezeichnetem Bild: neue Abdruecke hinter fahrenden Fahrzeugen, dann zeichnen
void draw3D(const glm::mat4 &viewMatrix);
/// neues Spiel / Spielstand geladen
void reset();
}
