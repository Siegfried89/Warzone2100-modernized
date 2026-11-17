// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 15:46, Punkt 5): "Jede Fraktion nutzt nur ihre eigene Technik (wie StarCraft) - wie in der Original-Kampagne."
// Die vier WZ-Fraktionen bauen nur ihre eigenen Ruempfe (so wie sie in der Original-Kampagne auftreten):
//   Project       Viper, Cobra, Python, Wyvern, Dragon
//   New Paradigm  Bug, Scorpion, Mantis
//   Kollektiv     Leopard, Panther, Tiger
//   Nexus         Retaliation, Retribution, Vengeance
// Scavengers (und Spieler ohne gewaehlte Fraktion, z. B. Kampagnen-Gegner) bleiben unbeschraenkt.
// Wirkt im Designer (Rumpfliste) und bei allen Vorlagen (Fabrikliste, KI-Bau ueber researchedTemplate) - fuer alle Spieler gleich.
// Dazu (Punkt 6/7): Originalfarben je Fraktion und zufaellige Fraktion fuer Computer-Gegner (multiint.cpp, Spielstart des Hosts).
#pragma once

#include <cstdint>

struct BODY_STATS;

namespace modfactech
{
/// darf dieser Spieler (nach seiner Fraktion) diesen Rumpf bauen?
bool bodyAllowed(unsigned player, const BODY_STATS *psBody);
/// Originalfarbe der Fraktion (WZ-Farbnummer): Scavengers 1 (orange), Nexus 3 (schwarz), Kollektiv 2 (grau), New Paradigm 8 (gelb), Project 0 (gruen)
int factionColour(int faction);
/// nur der Host, beim Spielstart (multiint.cpp, vor sendOptions): Computer-Gegner ohne gewaehlte Fraktion bekommen zufaellig eine der
/// fuenf Fraktionen (Wunsch Punkt 7); jeder Spieler mit WZ-Fraktion bekommt die Originalfarbe seiner Fraktion, sofern kein anderer
/// Spieler sie schon traegt (Punkt 6). Wird an alle Mitspieler verteilt.
void prepareStart();
}
