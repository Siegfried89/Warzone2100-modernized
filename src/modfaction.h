// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. Punkt 8): animierte Fraktionssymbole aus den Zwischensequenzen (Mod-Daten images/factanim.img + factanim.txt)
// in drei Groessen (S 44 px, L 96 px, X 224 px) und ein Auswahl-Klang je Fraktion (faction_<n>.ogg).
// Ohne die Mod-Daten bleibt alles beim Alten (die Funktionen geben false zurueck). Reine Anzeige.
#pragma once

#include <cstdint>

namespace modfaction
{
enum Size { SIZE_S = 0, SIZE_L = 1, SIZE_X = 2 };

/// Animationsdaten vorhanden?
bool ready();
/// Bild der Fraktion zeichnen. animate = false: Ruhebild (Endbild bzw. erstes Bild der Schleife)
bool draw(int faction, Size size, int x, int y, int w, int h, unsigned elapsedMs, bool animate, uint8_t alpha = 255);
/// Fraktion angeklickt: grosses Symbol erscheint kurz in der Bildmitte + Klang
void splash(int faction);
/// grosses Symbol zeichnen (falls aktiv) - jedes Bild im Hauptmenue
void drawSplash();
/// Bilder freigeben (Spielende)
void shutdown();
/// MOD (16.11. 18:00): grosses Symbol laeuft gerade (Menue ist dann ausgeblendet und nimmt keine Klicks an)
bool splashActive();
/// Symbol sofort beenden (Klick/Esc)
void endSplash();
}
