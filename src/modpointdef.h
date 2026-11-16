// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch MF-Gebaeudefunktionen, 16.11.): Punktverteidigung wie in Metal Fatigue - die MF-Stellung "Point Defense" schiesst
// anfliegende Raketen, Granaten, Moerser- und Haubitzengeschosse und Bomben ab, bevor sie einschlagen.
// Je Stellung hoechstens ein Abschuss alle 1,2 s, Trefferchance 60 %, Reichweite = Reichweite ihrer Waffe, nur in der eigenen Ebene.
// Spiellogik (gameRand), daher in allen Mehrspieler-Rechnern gleich.
#pragma once

struct PROJECTILE;

namespace modpointdef
{
/// einmal je Spieltakt vor dem Bewegen der Geschosse: Liste der Punktverteidigungen auffrischen
void beginTick();
/// true = das Geschoss wurde abgeschossen (der Aufrufer nimmt es aus dem Spiel)
bool tryIntercept(PROJECTILE *proj);
/// neues Spiel / Spielstand geladen
void reset();
}
