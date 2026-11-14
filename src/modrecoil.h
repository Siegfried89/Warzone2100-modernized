// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Rueckstoss-Wackeln. Jeder Schuss laesst Panzer und Laufmaschinen kurz nachfedern: der Rumpf (mit Turm) und die Ketten/Beine
// schwingen mit unterschiedlicher Frequenz und Daempfung - also nicht im Gleichtakt. Reine Anzeige (kein Einfluss auf die Simulation).
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/glm.hpp>

struct DROID;
struct WEAPON_STATS;

namespace modrecoil
{
/// aus combFire(): ein Schuss wurde abgegeben (weaponDir = Turmrichtung relativ zum Rumpf, 16-Bit-Winkel)
void onShot(const DROID *psDroid, const WEAPON_STATS *psStats, int weaponDir);
/// Anzeige: liefert false, wenn das Fahrzeug ruhig steht. Sonst die Zusatzdrehung fuer Rumpf+Turm und fuer die Ketten/Beine.
bool shake(const DROID *psDroid, unsigned renderTime, glm::mat4 &hull, glm::mat4 &prop);
/// Anzeige: Infanterie unter Beschuss laeuft geduckt (Oberkoerper nach vorn, etwas kleiner) und richtet sich nach ein paar Sekunden wieder auf
bool duck(const DROID *psDroid, unsigned renderTime, glm::mat4 &out);
/// Anzeige: Schwebe-Wippen fuer Hover-Fahrzeuge (Auf und Ab, leichtes Nicken/Rollen, staerker in Fahrt). false = kein Hover-Antrieb.
bool hover(const DROID *psDroid, unsigned renderTime, glm::mat4 &out);
/// beim Spielende / Ladevorgang
void reset();
}
