// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 03:05): "Mehr Funkeneffekte, wenn z. B. eine Kanone auf einen Rumpf trifft - deutlich sichtbar. Und auch metallisch
// laut! Klapperndes Metall, das durchschossen wurde (die Panzerung)."
//  - Kanonen, Gauss, Raketen und Lenkflugkoerper auf ein Fahrzeug (kein Infanterist/Cyborg): ein heller Lichtblitz, 14-22 leuchtende
//    Funken (effects/modspark.pie, GRAVITON_TYPE_SPARK) fliegen vom Trefferpunkt weg und fallen, dazu laut klapperndes Metall
//    (audio/mf/armorhit_0..5.ogg, eines zufaellig).
//  - Maschinengewehre auf Fahrzeuge: zwei, drei kleine Funken (ohne Klang - die Querschlaeger klingen schon).
// Nur Anzeige und Ton (rand(), gfxVisible) - kein Einfluss auf den Spielzustand.
#pragma once

#include "lib/framework/vector.h"

struct PROJECTILE;
struct WEAPON_STATS;

namespace modarmorhit
{
/// projectile.cpp: das Geschoss hat sein Ziel getroffen (sichtbar); pos = Effekt-Koordinaten (x, Hoehe, Karten-y)
void onHit(const PROJECTILE *proj, const WEAPON_STATS *stats, const Vector3i &pos);
/// Funken an einer Stelle (auch fuer andere Module): count Funken, speed = Grundgeschwindigkeit (Welt/s)
void sparks(const Vector3i &pos, int count, float speed);
}
