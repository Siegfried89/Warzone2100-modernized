// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 16:43): Panzer wie in Sudden Strike.
//
// "Panzer (hier stelle ich mir wieder Python, Ketten, grosse Kanone vor) halten bei jedem Schuss zum Feuern an. Wie in Sudden Strike.
//  Sie bekommen ausserdem ein Front-MG, das (mit gelbem Muendungsfeuer) aus dem vorderen Teil des Rumpfes schiesst. Man sieht keine Waffe,
//  sondern nur das Muendungsfeuer. Das MG kann auch nur nach vorne schiessen."
//
// - Panzer = Bodenfahrzeug (Ketten, Halbketten, Raeder) mit mittlerem oder schwerem Rumpf und einer Hauptwaffe, die kein MG ist.
// - Kanone (auch Schienen-/Gausskanone): faehrt der Panzer, bremst er erst ab und feuert, sobald er steht; nach dem Schuss bleibt er einen
//   Augenblick stehen und faehrt dann seinen Weg weiter (nur gameTime, auf allen Rechnern gleich).
// - Rumpf-MG: unsichtbare Waffe vorn im Rumpf (Zwillings-MG-Werte des Spielers, mit seinen Forschungs-Verbesserungen), feuert kurze
//   Feuerstoesse auf den naechsten sichtbaren Gegner im Bogen von +-25 Grad vor dem Bug; gelbes Muendungsfeuer vorn am Rumpf.
#pragma once

struct DROID;
struct WEAPON_STATS;

namespace modtankguns
{
/// Sudden-Strike-Panzer?
bool isTank(const DROID *psDroid);
/// aus combFire(): darf die Waffe jetzt feuern? (Kanone eines fahrenden Panzers: nein - der Panzer haelt erst an)
bool readyToFire(DROID *psDroid, const WEAPON_STATS *psStats);
/// aus combFire() nach dem Schuss: kurz stehen bleiben
void afterFire(DROID *psDroid, const WEAPON_STATS *psStats);
/// aus der Bewegung: soll der Panzer gerade stehen (Feuerhalt)?
bool halted(const DROID *psDroid);
/// jeden Spielschritt: Rumpf-MGs
void tick();
void reset();
}
