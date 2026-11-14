// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: KI-Schwierigkeit getrennt skalieren (MF: "Schwierigkeit wirkt nur auf KI-Spieler")
//
// In Metal Fatigue veraendert die Schwierigkeit nicht nur das Einkommen der KI, sondern drei Werte getrennt: Schaden der KI-Waffen, Kosten
// (Bau/Forschung/Produktion) der KI und Produktionstempo der KI-Fabriken. Menschliche Spieler sind nie betroffen.
// MF-Werte fuer die 3 Stufen Leicht / Normal / Schwer:  Schaden 0,42 / 1,0 / 1,44;  Kosten 2,0 / 1,2 / 1,0;  Produktionstempo 0,25 / 0,5 / 1,0.
// WZ hat 5 Stufen: SUPEREASY = MF Leicht, MEDIUM = MF Normal, INSANE = MF Schwer; EASY und HARD liegen dazwischen.
// Wirksam nur in Mehrspieler-/Gefechtspartien und nur fuer Spieler, die als KI eingetragen sind (Lobby-Einstellung, auf allen Rechnern gleich).
#pragma once

namespace modaidiff
{
/// Ist dieser Spieler ein KI-Spieler, dessen Werte skaliert werden?
bool isScaled(unsigned player);
/// Schaden / Kosten / Produktionstempo in Prozent (100 = unveraendert; 100 fuer Menschen)
int damagePercent(unsigned player);
int costPercent(unsigned player);
int productionPercent(unsigned player);
/// Hilfsfunktionen
int scaleDamage(int damage, unsigned player);
int scaleCost(int cost, unsigned player);
}
