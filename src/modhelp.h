// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Hilferuf (nach Metal Fatigue "BroadCastHelp"). Wird eine Einheit beschossen, bekommen untaetige verbuendete Einheiten im
// Umkreis den Angreifer als Ziel. Laeuft im Spielschritt, deterministisch (Reihenfolge der Objektlisten), kein Zufall.
#pragma once

struct DROID;
struct PROJECTILE;

namespace modhelp
{
/// Einheit wurde getroffen; psProj darf nullptr sein
void onDroidHit(DROID *psVictim, const PROJECTILE *psProj);
/// bei neuem Spiel
void reset();
}
