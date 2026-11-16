// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. Punkt 3): Spielerfarbe auch im Einzelspieler waehlen - im Hauptmenue (Farbfelder neben den Fraktionen) und
// jederzeit im Spiel ueber das Menue (Esc -> "Unit Colour"). Nicht in echten Netzwerkspielen (dort waehlt man in der Lobby).
#pragma once

#include <functional>
#include <memory>

class WIDGET;

namespace modcolour
{
/// darf diese Farbe gewaehlt werden? (Kampagne: nicht die Farben der Gegner; im Spiel: keine Farbe, die ein anderer Spieler traegt)
bool allowed(int colour, bool inGame);
/// aktuelle eigene Farbe
int current(bool inGame);
/// Farbe setzen (wird gespeichert; im Spiel sofort sichtbar)
void apply(int colour, bool inGame);
/// Farbfelder (wie die WZ-Farbwahl) an ein Formular haengen: cols Spalten, Feldgroesse size px. Gibt die Hoehe zurueck.
int addSwatches(WIDGET *parent, int x, int y, int cols, int size, int gap, bool inGame, const std::function<void()> &afterPick = nullptr);
}
