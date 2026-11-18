// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 18.11. 00:56): Atomraketen-Silo A0NukeSilo (Daten: nukesilo.py).
//  - Zielwahl wie beim Lasersatelliten (Silo anwaehlen, Ziel anklicken); das Silo "baut" die Rakete in der Nachladezeit der Waffe NukeMissile
//    (die erste Rakete ab Fertigstellung des Silos), der Start kostet LAUNCH_COST Energie.
//  - Die Rakete steigt aus dem Silo, fliegt im Bogen zum Ziel und schlaegt ein: Waffe NukeBlast (grosser Flaechenschaden + Brand).
//    Spielwirkung (Start, Einschlagzeit, Schaden) nur im Spielschritt mit gameTime -> fuer alle Rechner gleich; Flug und Atompilz sind Anzeige.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

#include "lib/framework/types.h"
#include "lib/ivis_opengl/pietypes.h"

struct STRUCTURE;
struct STRUCTURE_STATS;
struct BASE_OBJECT;
struct DROID;

namespace modicbm
{
bool isNukeSilo(const STRUCTURE_STATS *psStats);
/// Startbefehl (aus recvLasSat); true = Silo ist ein Atomraketen-Silo (behandelt, auch wenn abgelehnt)
bool launch(STRUCTURE *psSilo, const BASE_OBJECT *psTarget);
/// Meldung "Atomrakete bereit" statt "Lasersatellit bereit"
const char *readyText(const STRUCTURE *psStruct);
/// Atompilz an dieser Stelle (x, y, Bodenhoehe) - auch fuer den mobilen Werfer (Waffe NukeTEL, Einschlag in projectile.cpp)
void cloudAt(const Vector3i &groundPos);
/// (18.11. 20:46) mobiler Werfer (Waffe NukeTEL): aus combFire - true = behandelt (kein normales Geschoss); beginnt im Stand das
/// Aufrichten, der Start folgt im Spielschritt; 6 min Nachladen
bool telIntercept(DROID *psDroid, const BASE_OBJECT *psTarget);
/// steht fest (Stuetzen aus, richtet sich auf, legt sich hin)
bool telHalted(const DROID *psDroid);
/// Anzeige: Winkel des Aufrichters (statt des Zielwinkels)
uint16_t telPitch(const DROID *psDroid, uint16_t aimPitch, UDWORD time);
/// ist eine Rakete auf dem Fahrzeug (nicht abgeschossen / nachgeladen)?
bool telRocketLoaded(const DROID *psDroid);
/// Anzeige: Rakete auf dem Aufrichter (gleiche Lage wie das Waffenmodell)
bool drawTelRocket(const DROID *psDroid, const glm::mat4 &weaponMatrix, const glm::mat4 &viewMatrix, int colour, PIELIGHT brightness, int pieFlag, int pieFlagData, float heightAboveMap);
/// Anzeige: vier Stuetzen am Rahmen (bodyMatrix = Lage des Rumpfs)
bool drawTelJacks(const DROID *psDroid, const glm::mat4 &bodyMatrix, const glm::mat4 &viewMatrix, int colour, PIELIGHT brightness, int pieFlag, float heightAboveMap);
/// jeden Spielschritt: erste Rakete nach Fertigstellung beginnen, Einschlaege ausloesen
void tick();
/// Rakete im Flug, Abgasfahne, Atompilz (Anzeige)
void draw3D(const glm::mat4 &viewMatrix);
void reset();
}
