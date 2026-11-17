// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 20:06): "Es sollte eigentlich nur der Systemturm-Zusatz sein, der an JEDEN Rumpf drangebaut werden kann, immer vorne
// am Rumpf. Und drehen sollte er sich, nachdem bei der Einheit auf Minen raeumen geklickt wurde oder ein Minenraeumbefehl gegeben wurde."
//
// Der Systemturm "MineFlail" wird nicht auf den Turm-Anschluss gesetzt, sondern vor den Bug (Ursprung der Modelle = Bugspitze am Boden)
// und auf die Breite des Fahrzeugs skaliert. Ist der Minenraeumer eingeschaltet (modmines::flailing, Zweitbefehl DSO_MINECLEAR), dreht
// sich die Walze, die Ketten stehen ab und vor dem Bug fliegt Staub. Reine Anzeige.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include "lib/ivis_opengl/pietypes.h"
#include <glm/glm.hpp>

struct DROID;
struct iIMDShape;

namespace modflail
{
/// aus displayCompObj() (Fall DROID_ECM): false = kein Minenraeumer, normal weiterzeichnen
bool draw(const DROID *psDroid, const iIMDShape *body, const iIMDShape *prop, const glm::mat4 &hullMatrix, const glm::mat4 &viewMatrix,
          int colour, PIELIGHT brightness, int pieFlag, int pieFlagData, float heightAboveTerrain, bool button, bool &didDraw);
}
