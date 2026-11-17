// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 18:13): Minen fuer den Minenfeld-Marker (modpaint).
//
// Bauwerk "A0Mine" (Mod-Paket bunkermine.py: kleiner, in die Erde versunkener Betonbunker, "passable" = ueberfahrbar):
//  - fuer Gegner unsichtbar: nach jeder Sichtberechnung wird die Mine fuer fremde Spieler wieder versteckt - ausser eine ihrer
//    Einheiten steht direkt davor (1,5 Felder) oder ein Sensor-Fahrzeug sucht in 4 Feldern Umkreis (Minensuche);
//  - explodiert, sobald eine feindliche Bodeneinheit darueberfaehrt: grosse Explosion, schwerer Schaden in knapp einem Feld Umkreis
//    (nur Bodeneinheiten und Bauwerke), die Mine ist danach weg.
// Alles in der Spielzeit (gameTime, Spielobjekt-Reihenfolge) und damit im Mehrspieler synchron.
#pragma once

namespace modmines
{
/// jeden Spielschritt direkt nach processVisibility(): Minen verstecken, Ausloeser pruefen
void tick();
void reset();
}
