// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 18:27 "Bruecken"): Bruecken ueber Wasser.
//  - Bauwerke "A0Bridge..." (Strassenbruecke) und "A0RailTrackBridge" (Eisenbahnbruecke, zaehlt fuer modrail als Gleis): passierbar, auf
//    Wasser- und Landfeldern baubar, wie Mauern ziehbar. Die Gleis-Zeichenhilfe (modbuild planRailLine) setzt ueber Wasser von selbst
//    Eisenbahnbruecken.
//  - Fertige Brueckenfelder ueber Wasser sind fuer Bodeneinheiten befahrbar (Sperrbit WATER_BLOCKED faellt weg, Kontinente neu); Schiffe
//    fahren weiter darunter durch.
//  - Fahrbahn auf Hoehe des umgebenden Landes (Wasseroberflaeche + 1/3 Feld); Einheiten und Zuege fahren darauf.
//  - Darstellung (22.11. 20:26) mit den Vanilla-Modellen: Brueckenmitte mit Pfeiler (mibridge) ueber Wasser, Brueckenende (mibridg1) am Ufer;
//    eine zerstoerte Bruecke hinterlaesst das Vanilla-Wrack (Merkmal A0BridgeWreck, mibrdfuk).
// Spielzustand nur aus Spielzustand (fertige Bauwerke, feste Reihenfolge) - auf allen Rechnern gleich.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include "lib/ivis_opengl/pietypes.h"
#include <glm/fwd.hpp>
#include <climits>

struct STRUCTURE;
struct STRUCTURE_STATS;
struct GameWorld;

namespace modbridge
{
bool isBridgeStats(const STRUCTURE_STATS *s);
bool isBridge(const STRUCTURE *s);
/// fertige Bruecke auf diesem Feld (Spielfeld-Koordinaten)
bool bridgedTile(int tx, int ty);
/// Fahrbahnhoehe an einer Weltstelle, INT_MIN wenn dort keine fertige Bruecke ueber Wasser liegt
int deckHeight(int wx, int wy);
/// Fahrbahnhoehe fuer die Darstellung eines Brueckenbauwerks (auch im Bau)
int structureDeckHeight(const STRUCTURE *s);
/// Sperrbits und Kontinente nachfuehren (jeden Spielschritt; billig, solange sich nichts aendert)
void tick();
void reset();
/// Bruecke zeichnen (Vanilla-Mitte/-Ende); true = fertig, Bauwerksmodell nicht zeichnen
bool drawBridge(const STRUCTURE *s, PIELIGHT brightness, const glm::mat4 &viewMatrix);
/// structure.cpp destroyStruct (nach removeStruct): zerstoerte Bruecke hinterlaesst Wracks
void onDestroyed(const STRUCTURE *s, GameWorld &world);
}
