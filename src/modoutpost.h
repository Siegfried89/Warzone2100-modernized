// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (MF-Gebaeudefunktionen, 16.11.): Aussenposten-Teleport wie in Metal Fatigue (COutpost: Launcher -> Destination).
// Schickt man eine eigene Bodeneinheit auf einen eigenen MF-Aussenposten ("Outpost ..."), wird sie beim Ankommen zum naechsten
// eigenen Aussenposten versetzt (bei zwei Posten: zum anderen; bei mehr: reihum in Bau-Reihenfolge). Je Posten ein Sprung
// alle 4 s, je Einheit hoechstens alle 10 s. Lichtblitz an beiden Enden. Spiellogik (mehrspielersicher).
#pragma once

struct GameWorld;
struct STRUCTURE;

namespace modoutpost
{
/// eigener MF-Aussenposten (fertig gebaut)?
bool isOutpost(const STRUCTURE *s);
void tick(GameWorld &world);
void reset();
}
