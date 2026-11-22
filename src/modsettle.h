// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 20:50): "Die Haeuser 191 Ruin bis 199 Ruin wie die Solarpaneele mit Steuerung als Feld baubar machen. Die Modelle sollen
// sich dabei abwechseln und es sollen alle Modelle benutzt werden und nie zwei gleiche daneben stehen. So kann man eine kleine Siedlung bauen.
// Ebenso alle entsprechenden Versionen dieser fuer Winterkarten und Stadtkarten (Hochhaeuser). 210, 206 und 224 auch noch fuers Wuestenset."
//  - Im Baumenue steht je Kartensatz eine Siedlung (Mod-Paket: A0SettleDesert / A0SettleWinter / A0SettleCity, nur die zum Kartensatz
//    passende ist freigeschaltet). Gebaut wird einzeln, als Linie oder als Feld (Strg + Ziehen) wie jedes Gebaeude.
//  - Beim Baubeginn waehlt die Engine das Haus: eines der Varianten-Bauwerke "A0SettleV-<D|W|C>-..." (Vanilla-Modelle der Haeuser,
//    eigene Grundflaeche), das an dieser Stelle Platz hat, keinem Haus im Umkreis von 2 Feldern gleicht und in der Gegend am seltensten
//    steht (so kommen alle Modelle dran). Groessere Haeuser wachsen vom Bauplatz nach rechts/unten.
// Spielzustand: nur aus Spielzustand und Lage (kein Zufall) - auf allen Rechnern gleich.
#pragma once

#include "lib/framework/vector.h"
#include <cstdint>

struct STRUCTURE_STATS;

namespace modsettle
{
/// Siedlungs-Bauwerk (Auswahl im Baumenue)?
bool isSet(const STRUCTURE_STATS *s);
/// droid.cpp droidStartBuild: Variante fuer diesen Bauplatz waehlen; pos wird auf die Mitte der Variante gesetzt. Gibt set zurueck,
/// wenn es keine Siedlung ist oder keine Variante passt.
STRUCTURE_STATS *chooseVariant(STRUCTURE_STATS *set, unsigned player, Vector2i &pos, uint16_t direction);
}
