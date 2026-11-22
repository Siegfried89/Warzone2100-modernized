// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 20:50 / 21:19): Ruinen zerstoerter Bauwerke.
//  - "Jede zerstoerte Struktur hinterlaesst eine Ruine (wie die Scavenger-Wracks). Diese bleiben auf dem Feld (grauschwarz mit Russspuren
//    und rauchen wenige Minuten). Diese verbleiben ungefaehr 15 Minuten. Dann verschwinden sie."
//    -> Merkmal "ModRuin_<B>x<T>" in der Grundflaeche des Bauwerks (Mod-Paket: Vanilla-Modell der Fabrikruine "171 Ruined Factory",
//       blfactrd, passend verkleinert/vergroessert), 4 Minuten dichter schwarzer Rauch, nach 15 Minuten weg.
//    Keine Ruine: Strassen, Gleise, Bruecken (die hinterlassen ihr Wrack), Minen, Module, Bohrtuerme (die Oelquelle kehrt zurueck),
//    passierbare Bauwerke, Siedlungshaeuser (Zivil-Ruinen) und Ruinen selbst.
//  - "In diesen 15 Minuten kann man ganz einfach auf eine Ruine 'bauen' klicken - das baut das vorherige Gebaeude wieder auf (ohne Module)."
//    -> Baufahrzeug + Rechtsklick auf die Ruine = Bauauftrag fuer das alte Bauwerk an derselben Stelle und in derselben Drehung (sofern es
//       fuer den Spieler baubar ist). Auf Ruinen darf allgemein gebaut werden; beim Baubeginn wird die Ruine weggeraeumt.
//  - Merkmale zaehlen nicht als Bauwerke: keine Auswirkung auf Sieg/Niederlage oder Kampagnen-Basen.
// Spielzustand: angelegt im naechsten Spielschritt (Warteschlange), feste Reihenfolge, keine Zufallszahlen - auf allen Rechnern gleich.
#pragma once

#include "lib/framework/vector.h"
#include "orderdef.h"

struct BASE_OBJECT;
struct DROID;
struct FEATURE_STATS;
struct MAPTILE;
struct STRUCTURE;
struct STRUCTURE_STATS;

namespace modruin
{
/// structure.cpp destroyStruct, VOR removeStruct: merkt sich, ob und wo eine Ruine entsteht
void onDestroyed(const STRUCTURE *psDel);
/// jeden Spielschritt: Ruinen anlegen, Rauch, Ablauf
void tick();
void reset();
bool isRuinStats(const FEATURE_STATS *s);
/// liegt auf diesem Feld eine Ruine? (validLocation: darauf darf gebaut werden)
bool isRuinTile(const MAPTILE *t);
/// order.cpp chooseOrderObj: Baufahrzeug klickt auf eine Ruine -> Bauauftrag fuer das alte Bauwerk
bool rebuildOrder(const DROID *psDroid, const BASE_OBJECT *psObj, DroidOrder &out);
/// droid.cpp droidStartBuild: Ruinen in der Grundflaeche wegraeumen
void clearSite(STRUCTURE_STATS *psStats, Vector2i pos, uint16_t direction);
}
