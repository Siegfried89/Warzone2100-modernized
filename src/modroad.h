// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 20:50 "Strassen baubar machen ... Uebrigens sollen die auch von Kratern zerstoert werden koennen"):
//  - Bauwerk "A0Road" (Mod-Paket): Baufahrzeuge bauen es einzeln, als Linie (ziehen wie eine Mauer) oder als Feld. Ist ein Feld fertig,
//    verschwindet das Bauwerk und die Kachel wird zur echten Strassenkachel des Kartensatzes (Vanilla-Kacheln, gedreht nach den
//    Nachbarn): Gerade, Ende, Einmuendung (T); Kurven und Kreuzungen als Beton-/Asphaltflaeche.
//      Wueste  Gerade 59, Ende 47, T 57, Flaeche 22 (Beton)
//      Stadt   Gerade 40, Ende 46, T/Flaeche 49 (Asphalt)
//      Winter  Gerade 59 (abwechselnd 37), Ende 60, T 13, Flaeche 22 (Beton)
//    Strassenkacheln sind Bodenart "Strasse": Raeder/Luftkissen 150 %, Halbketten 135 %, Ketten 120 % Tempo (terraintable).
//  - Ein Krater auf einem Strassenfeld zerstoert es: die urspruengliche Bodenkachel kehrt zurueck, die Nachbarn passen sich an.
// Spielzustand nur aus Spielzustand (feste Reihenfolge) - auf allen Rechnern gleich.
#pragma once

namespace modroad
{
/// jeden Spielschritt: fertige Strassen-Bauwerke in Kacheln umwandeln
void tick();
void reset();
/// modterrain addCrater: Krater auf diesem Feld - Strasse dort zerstoeren
void onCrater(int tx, int ty);
}
