// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: "Boden beschiessen" - Einheiten feuern auf einen Punkt im Gelaende (Strg + Rechtsklick auf freien Boden).
//
// Umsetzung: Der Befehl DORDER_ATTACKGROUND (Ort) wird beim Ausfuehren in einen gewoehnlichen Angriffsbefehl auf ein unsichtbares
// Ziel-Feature ("ModGroundTarget") umgewandelt. Dadurch laufen Anfahrt, Reichweite, Zielen und Feuern ueber die vorhandene Technik
// (auch im Netzwerk, da der Befehl wie jeder Ortsbefehl synchron uebertragen wird). Beim Schuss wird nicht auf das Feature
// gezielt, sondern auf einen Zufallspunkt im Streukreis um den Zielpunkt (Artillerie streut weit, direkte Waffen kaum).
// Zielpunkte, die kein Befehl mehr braucht, werden entfernt.
#pragma once

#include "lib/framework/vector.h"

struct BASE_OBJECT;
struct DROID;
struct FEATURE;
struct FEATURE_STATS;
struct GameWorld;
struct WEAPON_STATS;

namespace modgroundfire
{
/// ist das die Ziel-Feature-Art der Mod? (Name "ModGroundTarget")
bool isMarkerStats(const FEATURE_STATS *stats);
/// ist das Objekt ein Zielpunkt?
bool isMarker(const BASE_OBJECT *obj);

/// Zielpunkt bei pos (Weltkoordinaten) fuer den Spieler suchen oder anlegen; fuer den Spieler als sichtbar markiert. nullptr, wenn nicht moeglich.
FEATURE *markerAt(const Vector2i &pos, unsigned player);

/// kann die Einheit auf Boden feuern?
bool canFireOnGround(const DROID *psDroid);

/// Streuradius (Weltmasseinheiten) einer Waffe auf Bodenziele
int scatterRadius(const WEAPON_STATS &stats);
/// Zufallspunkt im Streukreis (synchroner Zufall)
Vector2i scatterPoint(const Vector2i &center, int radius);

/// Befehl an alle gewaehlten Einheiten, die Boden beschiessen koennen. true, wenn mindestens eine Einheit den Befehl bekam.
bool orderSelected(unsigned player, int worldX, int worldY);

/// Strg (ohne Umschalt) gehalten?
bool keysForGroundFire();
/// jedes Bild: roten Streukreis bei worldPos zeigen (wanted) bzw. ausblenden. Radius = groesster Streuradius der gewaehlten Einheiten.
void updatePreview(bool wanted, const Vector2i &worldPos);

/// Beschriftung am Mauszeiger, solange der rote Streukreis zu sehen ist (sonst nullptr): "Area Bombardment: Attack Area"
const char *activeLabel();

/// jeden Spielschritt: nicht mehr gebrauchte Zielpunkte entfernen
void tick(GameWorld &world);
/// bei neuem Spiel
void reset();
}
