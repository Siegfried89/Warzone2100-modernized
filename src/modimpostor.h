// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 18:02): "Saemtliche Einheiten erscheinen als Sprites bei hoher Zoomstufe. Nicht nur die Infanterie."
//
// Detailstufe wie in Rome Total War: weit herausgezoomt wird jede Einheit nicht mehr aus ihren 3D-Teilen (Antrieb, Rumpf, Halterung,
// Waffe, animierte Glieder) zusammengesetzt, sondern als ein vorgerendertes Bild gezeichnet. Das Mod-Paket (impostors.py) rendert jede
// Zusammenstellung aus 8 Blickrichtungen; hier wird das Bild zur Blickrichtung der Kamera gewaehlt (relativ zur Fahrtrichtung der
// Einheit) und als zur Kamera gedrehtes Viereck gezeichnet - in der Spielerfarbe (Teamfarben-Maske), ohne Schatten. Nur Anzeige.
// Combots aus Einzelteilen (modcombot) und Einheiten ohne passendes Bild bleiben 3D.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include "lib/ivis_opengl/pietypes.h"
#include <glm/fwd.hpp>

struct DROID;

namespace modimpostor
{
/// ab dieser Kamera-Entfernung Sprites (Start 2600, ganz weit 5000)
constexpr float LOD_FAR = 3800.f;
/// zeichnet die Einheit als Sprite, wenn weit herausgezoomt und ein Bild vorhanden ist; true = gezeichnet (3D-Teile ueberspringen)
bool draw(const DROID *psDroid, const glm::mat4 &modelMatrix, const glm::mat4 &viewMatrix, int teamColour, PIELIGHT brightness);
/// beim Spielende (Zwischenspeicher leeren)
void reset();
}
