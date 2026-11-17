// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 18:48): "Klick auf die Minimap - grosse Ansicht der Minimap, Aussehen wie in Supreme Commander. Bildschirmteilung
// moeglich wie in Supreme Commander (am besten genau gleich). Und auch dort das Zeichnen der Befehle zulassen."
//
// Strategische Karte wie in Supreme Commander:
//  - Klick auf die Minikarte oeffnet die grosse Karte (ganzer Bildschirm); Knopf "Split": linke Bildhaelfte Karte, rechte Haelfte die
//    normale 3D-Ansicht (deren Mitte rueckt in die rechte Haelfte); "Close" oder erneuter Klick auf die Minikarte schliesst.
//  - Gelaende aus den Kachelfarben mit Hoehenschattierung, Nebel des Krieges (unerforscht dunkel), Gitter, Kamera-Sichtfenster,
//    Bauwerke und Einheiten als taktische Symbole (modtactical).
//  - Mausrad zoomt um den Mauszeiger, mittlere Maustaste verschiebt. Linksklick waehlt eine Einheit (Umschalt = dazu), Ziehen = Rahmen-
//    auswahl, Doppelklick = Kamera dorthin; Rechtsklick = Befehl an die Auswahl (auf Gegner: Angriff, sonst Bewegung; Umschalt = anhaengen).
//  - Schlachtplan-Zeichnen (modpaint) funktioniert auch auf der grossen Karte.
//  - (17.11. 19:42) Anzeigearten wie in Supreme Commander (Knopf "View"): Gelaende, kartografisch (Papierkarte mit Hoehenlinien),
//    Hoehen (Hoehenschichtfarben mit Hoehenlinien), strategisch (dunkles Relief); Knopf "Resources" blendet die Oelquellen ein.
// Knoepfe "Map" und "Symbols" ueber der Minikarte (Symbols = taktische Symbole im 3D-Bild ein/aus).
#pragma once

namespace modbigmap
{
/// einmal pro Bild aus intRunWidgets(): Knoepfe an der Minikarte
void tick();
/// aus processMouseClickInput(): true = die Maus gehoert der grossen Karte (overUi: Maus ueber Bedienelementen - dann nicht)
bool processMouse(bool overUi);
/// Zeichnen (nach der 3D-Szene, vor den Bedienelementen)
void draw();
/// Mausrad-/Zoomtaste: true = die grosse Karte hat gezoomt (die Kamera nicht)
bool zoom(int multiplier);
/// Klick auf die Minikarte (Kachel tx/ty): grosse Karte oeffnen bzw. schliessen
void radarClicked(int tileX, int tileY);
/// Bildmitte der 3D-Ansicht (bei geteiltem Bildschirm in der rechten Haelfte)
int viewCenterX(int screenW);
bool isOpen();
void shutdown();
}
