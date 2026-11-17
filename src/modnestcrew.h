// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 14.11. Punkt 12, Schlachtfeld-Autonomie): leere MG-Stellungen werden selbststaendig neu besetzt.
//
// - MG-Nester (Sandsack-Gruben mit MG-Schuetze, "A0BaBaMGNest*") haben einen Schuetzen. Schwere Treffer koennen ihn toeten: faellt die
//   Stellung unter 60 % Lebenspunkte, stirbt er bei jedem weiteren Treffer mit 40 % Wahrscheinlichkeit, unter 30 % sicher.
// - Ohne Schuetze schweigt das MG (kein Zielen, kein Feuer) und der Schuetze ist nicht mehr zu sehen.
// - Freie eigene Infanterie in der Naehe (12 Felder, kein laufender Befehl, nicht gerade unter Spielerkontrolle) laeuft von selbst hin und
//   besetzt das MG neu: der Infanterist wird zum neuen Schuetzen (die Einheit geht in der Stellung auf).
// - Findet sich niemand, kommt nach 3 Minuten Ersatz aus der Basis (die Stellung ist dann wieder besetzt).
// Spielwirkung nur ueber gameTime/gameRand in fester Reihenfolge (auf allen Rechnern gleich). Nicht im Spielstand (nach dem Laden besetzt).
#pragma once

#include <cstdint>

struct STRUCTURE;

namespace modnestcrew
{
/// aus structureDamage(): die Stellung wurde getroffen und hat ueberlebt
void onDamaged(STRUCTURE *psStructure);
/// ist die Stellung gerade ohne Schuetze? (dann kein Feuer, Anzeige ohne Schuetze)
bool unmanned(const STRUCTURE *psStructure);
/// jeden Spielschritt
void tick();
void reset();
}
