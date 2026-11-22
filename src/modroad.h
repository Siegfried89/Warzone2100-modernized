// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 20:50 "Strassen baubar machen ... Uebrigens sollen die auch von Kratern zerstoert werden koennen"):
//  - Bauwerk "A0Road" (Mod-Paket): Baufahrzeuge bauen es einzeln, als Linie (ziehen wie eine Mauer) oder als Feld. Ist ein Feld fertig,
//    zeigt die Kachel die echte Strassenkachel des Kartensatzes (Vanilla-Kacheln, gedreht nach den Nachbarn): Gerade, Ende,
//    Einmuendung (T); Kurven und Kreuzungen als Beton-/Asphaltflaeche. Das Bauwerk bleibt unsichtbar stehen - anklickbar und
//    abreissbar (21:19); verschwindet es, kehrt die urspruengliche Bodenkachel zurueck.
//      Wueste  Gerade 59, Ende 47, T 57, Flaeche 22 (Beton)
//      Stadt   Gerade 40, Ende 46, T/Flaeche 49 (Asphalt)
//      Winter  Gerade 59 (abwechselnd 37), Ende 60, T 13, Flaeche 22 (Beton)
//    Strassenkacheln sind Bodenart "Strasse": Raeder/Luftkissen 150 %, Halbketten 135 %, Ketten 120 % Tempo (terraintable).
//  - Ein Krater auf einem Strassenfeld zerstoert es: die urspruengliche Bodenkachel kehrt zurueck, die Nachbarn passen sich an.
// Spielzustand nur aus Spielzustand (feste Reihenfolge) - auf allen Rechnern gleich.
#pragma once

struct STRUCTURE;

namespace modroad
{
/// jeden Spielschritt: fertige Strassen-Bauwerke in Kacheln umwandeln
void tick();
void reset();
/// modterrain addCrater: Krater auf diesem Feld - Strasse dort zerstoeren
void onCrater(int tx, int ty);
/// Strassen-Bauwerk? (display3d: fertig nicht zeichnen)
bool isRoad(const STRUCTURE *s);
}
