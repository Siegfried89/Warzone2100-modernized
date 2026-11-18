// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 18.11. 00:56): Atomraketen-Silo A0NukeSilo (Daten: nukesilo.py).
//  - Zielwahl wie beim Lasersatelliten (Silo anwaehlen, Ziel anklicken); das Silo "baut" die Rakete in der Nachladezeit der Waffe NukeMissile
//    (die erste Rakete ab Fertigstellung des Silos), der Start kostet LAUNCH_COST Energie.
//  - Die Rakete steigt aus dem Silo, fliegt im Bogen zum Ziel und schlaegt ein: Waffe NukeBlast (grosser Flaechenschaden + Brand).
//    Spielwirkung (Start, Einschlagzeit, Schaden) nur im Spielschritt mit gameTime -> fuer alle Rechner gleich; Flug und Atompilz sind Anzeige.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

struct STRUCTURE;
struct STRUCTURE_STATS;
struct BASE_OBJECT;

namespace modicbm
{
bool isNukeSilo(const STRUCTURE_STATS *psStats);
/// Startbefehl (aus recvLasSat); true = Silo ist ein Atomraketen-Silo (behandelt, auch wenn abgelehnt)
bool launch(STRUCTURE *psSilo, const BASE_OBJECT *psTarget);
/// Meldung "Atomrakete bereit" statt "Lasersatellit bereit"
const char *readyText(const STRUCTURE *psStruct);
/// jeden Spielschritt: erste Rakete nach Fertigstellung beginnen, Einschlaege ausloesen
void tick();
/// Rakete im Flug, Abgasfahne, Atompilz (Anzeige)
void draw3D(const glm::mat4 &viewMatrix);
void reset();
}
