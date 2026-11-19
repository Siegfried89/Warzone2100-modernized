// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 19.11.: "Zone Trooper mit ihren supercoolen Anzuegen und blauen Strahl-Railguns"): die Railguns der C&C3-Infanterie
// (Zone Trooper, Kommando - Waffen-Id beginnt mit "ZoneRail" bzw. "CommandoRail") zeichnen beim Schuss einen kurzen blauen Lichtstrahl
// von der Muendung zum Ziel (additiv, blendet in ~0,3 s aus) mit hellem Kern und blauem Lichtschein. Reine Anzeige.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

struct PROJECTILE;

namespace modrailbeam
{
/// nach dem Abfeuern eines Geschosses (projectile.cpp)
void onFire(const PROJECTILE *psProj);
/// jedes Bild (display3d.cpp)
void draw3D(const glm::mat4 &viewMatrix);
void reset();
}
