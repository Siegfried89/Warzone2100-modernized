// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch MF-Gebaeudefunktionen, 16.11.): Punktverteidigung wie in Metal Fatigue - die MF-Stellung "Point Defense" schiesst
// anfliegende Raketen, Granaten, Moerser- und Haubitzengeschosse und Bomben ab, bevor sie einschlagen.
// Je Stellung hoechstens ein Abschuss alle 1,2 s, Trefferchance 60 %, Reichweite = Reichweite ihrer Waffe, nur in der eigenen Ebene.
// (Wunsch 23.11. 03:05) Raketenabwehr (AMS): jede Waffe mit "ams": 1 (Fahrzeug oder Bauwerk) schiesst feindliche Artillerie-Raketen
// (Raketen/Lenkflugkoerper im Steilfeuer) in ihrer Reichweite ab: Turm dreht sich hin, Leuchtspur-Salve (bzw. Laserpuls) fliegt zum
// Geschoss, trifft sie ("amsChance" %), zerplatzt das Geschoss bei ihrer Ankunft in der Luft; danach "amsReload" ms Pause.
// Auf Verbundrumpfen nur im 180-Grad-Bereich des Waffenplatzes.
// Spiellogik (gameRand), daher in allen Mehrspieler-Rechnern gleich; Leuchtspuren/Klaenge nur Anzeige.
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
