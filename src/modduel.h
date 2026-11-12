// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Metal-Fatigue-Duellsystem fuer Combots (Nahkampf mit Block, Ausweichen, Teile abschlagen)
#pragma once

struct DROID;

namespace modduel
{
bool isCombot(const DROID *psDroid);
/// Einmal pro Spielschritt und Droide aufrufen (aus droidUpdate).
void update(DROID *psDroid);
/// Laufanimation fuer Droiden mit Beinen ohne Cyborg-Bewegungsmodell (Walk/Feuer-Ereignisse).
void updateLeggedAnimation(DROID *psDroid, int moveSpeed);
}
