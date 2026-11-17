// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 13:11): Infanterie schnell in Deckung bringen.
//
// - Shift-Klick der gewaehlten Infanterie auf einen eigenen Graben/eine Grube: die Gruppe verteilt sich auf das ganze
//   zusammenhaengende Grabennetz (alle angrenzenden Graben-/Grubenfelder), statt sich auf ein Feld zu draengen.
//   Erst bekommt jedes Feld einen Mann, dann einen zweiten; jeder Infanterist geht zum naechsten freien Platz.
// - Knopf "Take cover" (Schlachtplan): die gewaehlte Infanterie - oder, wenn keine gewaehlt ist, alle eigene Infanterie - bemannt
//   von selbst die naechsten Graeben, Gruben, Sandsackstellungen und besetzbaren Haeuser in der Umgebung (je Feld zwei Mann).
// Die Befehle gehen wie normale Spielerbefehle ueber das Netz (gleiches Ergebnis auf allen Rechnern).
#pragma once

#include <vector>

struct DROID;
struct STRUCTURE;

namespace modcover
{
/// Shift-Klick auf einen Graben: gewaehlte Infanterie aufs Grabennetz verteilen. false = nichts zu tun (normaler Befehl).
bool distributeOnNetwork(unsigned player, const STRUCTURE *start);
/// Knopf "Take cover": Infanterie bemannt die naechsten Stellungen. Gibt die Zahl der losgeschickten Infanteristen zurueck.
int takeCover(unsigned player);
/// (17.11.) "Raus aus der Deckung": die gewaehlte Infanterie (sonst alle eigene Infanterie in Deckung) verlaesst Graeben, Gruben,
/// Krater und Haeuser und sammelt sich gut zwei Felder dahinter (zur eigenen Basis hin). Gibt die Zahl der Infanteristen zurueck.
int leaveCover(unsigned player);
}
