// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Modell-Leiter fuer Scavenger-Infanterie ("Ausbildung").
//  * Die Fabrik wuerfelt beim Ausbilden das Modell: gewoehnliche Modelle haeufig, Modelle mit Schulterplatten selten; seltene Modelle
//    kommen mit Erfahrungsbonus (Startrang) aus der Fabrik.
//  * Beim Aufleveln wechselt die Einheit in das Modell der naechsten Stufe.
// Gesteuert ueber Felder im Koerper (body.json): variantGroup (Gruppenname), variantWeight (Gewicht beim Wuerfeln, 0 = nie),
// variantRank (Stufe, ab der das Modell "zu Hause" ist = Startrang), variantRoll (true = Vorlagen mit diesem Koerper wuerfeln).
// Deterministisch (Einheiten-ID als Zufallsquelle, kein gameRand), auf allen Rechnern gleich.
#pragma once

struct DROID;

namespace modvariant
{
/// direkt nach dem Bau einer Einheit in der Fabrik (structPlaceDroid)
void onFactoryDroid(DROID *psDroid);
/// beim Aufstieg in einen hoeheren Rang (droidIncreaseExperience)
void onRankGained(DROID *psDroid, unsigned newRank);
}
