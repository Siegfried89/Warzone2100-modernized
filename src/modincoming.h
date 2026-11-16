// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. Punkte 1-3): Artilleriegranate im Anflug
//
// 1. Kurz vor dem Einschlag einer Steilfeuer-Granate (Bahn MM_INDIRECT) erklingt am Zielpunkt das Anflug-Pfeifen (incoming_*.ogg,
//    endet genau beim Einschlag). Einheiten in der Naehe rufen "Incoming!" / "Get down!" / "Spread out!".
// 2. Die bedrohten Einheiten verteilen sich (nur wer gerade frei ist: kein ausdruecklicher Fahr-/Baubefehl, nicht "Position halten",
//    nicht schon in Deckung). Der Spieler kann seine Auswahl jederzeit auch selbst verteilen ("Spread out" im Schlachtplan-Feld).
// 3. Infanterie, die sich verteilt hat, graebt sich danach am neuen Platz ein: eine Erdmulde ohne Sandsaecke (modterrain::dig),
//    die Deckung gibt wie ein Krater.
// Spielwirkung (Fahrbefehle, Graben) rechnet nur mit gameTime, Ganzzahlen und fester Reihenfolge -> auf allen Rechnern gleich.
// Ton und Rufe sind reine Anzeige.
#pragma once

#include <vector>

struct DROID;
struct PROJECTILE;

namespace modincoming
{
/// aus proj_InFlightFunc fuer Bahn MM_INDIRECT, jeden Spielschritt; timeSoFar/flightTime in Spielzeit-ms
void onIndirectFlight(const PROJECTILE *psProj, int timeSoFar, int flightTime);
/// jeden Spielschritt (loop.cpp)
void tick();
/// neues Spiel
void reset();

/// Spieler-Befehl "Spread out": verteilt die Einheiten um ihren gemeinsamen Mittelpunkt (Befehle ueber das Netz)
void spreadUnits(const std::vector<DROID *> &units);
}
