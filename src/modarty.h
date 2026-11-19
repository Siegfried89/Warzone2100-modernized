// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Artillerie-Modus fuer Panzer und Laeufer mit normalen Kanonen (Wunsch 14.11., Punkt 21); seit 19.11. 22:04 auch Battle Mechs.
//  - Neuer Knopf im Befehlsmenue (Zweitbefehl DSO_ARTILLERY). Eingeschaltet richtet der Panzer das Rohr steil auf wie ein
//    Artilleriepanzer und schiesst im Bogen (indirekt) mit groesserer Reichweite, langsamerer Feuerfolge und mehr Streuung.
//    Die Geschosse sind die normalen, sichtbaren WZ-Kanonengeschosse; der Rueckstoss (modrecoil) bleibt.
//  - Technisch: zu jeder geeigneten Kanone liefert der Mod eine zweite Waffe "<ID>_ARTY" (stats/weapons.json, nicht im Designer).
//    Der Modus tauscht nur die Waffennummer der Einheit zwischen beiden Fassungen - deterministisch, im Spielstand gespeichert,
//    ueber den normalen Zweitbefehl auch im Mehrspieler synchron. Gilt fuer alle Spieler gleich.
//  - (Wunsch 19.11. 19:43) Steilfeuer: Bogen steiler als ein Moerser, sehr grosse Reichweite (wie Langstrecken-Artillerie),
//    stark verminderte Feuerrate, schlechtere Treffsicherheit (arty.py). Im Modus steht der Panzer (wie "Stellung halten");
//    ein Fahrbefehl schaltet den Modus ab, nach ca. 1 s (Rohr herunter) faehrt er los.
#pragma once

#include <cstdint>

struct DROID;

namespace modarty
{
/// Unterstuetzt diese Einheit den Artillerie-Modus? (Bodenfahrzeug/Laeufer mit mindestens einer geeigneten Kanone)
bool supported(const DROID *psDroid);
/// Waffennummern aller Waffen passend zum Zweitbefehl-Zustand setzen (an = Artillerie-Fassung, aus = normale Kanone)
void apply(DROID *psDroid, bool on);
/// nach Aenderungen an den Waffen (z. B. neu aufgesetzter Turm): Waffen wieder an den gespeicherten Zustand angleichen
void refresh(DROID *psDroid);
/// ist diese Waffennummer eine Artillerie-Fassung?
bool isArtyWeapon(uint32_t weaponStat);
/// Ruhe-Neigung des Rohrs (16-Bit-Winkel) fuer diese Waffe: Artillerie-Fassung zeigt steil nach oben, sonst 0
uint16_t restPitch(uint32_t weaponStat);
/// (Wunsch 19.11. 22:04) Battle Mech (Laeufer-Rumpf mit gaitStride): kann den Steilfeuermodus auch; seine Waffen heben sich dabei
/// nicht ganz so stark an wie ein Panzerrohr (etwa die Haelfte des Abschusswinkels) - der Bogen der Geschosse bleibt gleich steil
bool isMech(const DROID *psDroid);
/// Ruhe-Neigung fuer eine Waffe dieser Einheit (Mech: flacher)
uint16_t restPitch(const DROID *psDroid, uint32_t weaponStat);
/// angezeigte Neigung beim Zielen im Steilfeuermodus (Panzer: Abschusswinkel; Mech: etwa die Haelfte)
uint16_t aimPitch(const DROID *psDroid, int dist, int longRange);
/// Steilfeuermodus an? (Zweitbefehl gesetzt und eine Artillerie-Fassung montiert)
bool active(const DROID *psDroid);
/// steht die Einheit wegen des Steilfeuermodus still? (Modus an - oder gerade abgeschaltet, solange das Rohr herunterfaehrt)
bool halted(const DROID *psDroid);
/// Abschusswinkel (16-Bit) des Bogenschusses: nah 75 Grad, an der Reichweitengrenze 65 Grad - steiler als ein Moerser (45 Grad)
int shotAngle(int dist, int longRange);
/// faehrt die Einheit mit diesem Befehl (DROID_ORDER) los? Dann verlaesst sie vorher den Steilfeuermodus
bool isMoveOrder(int orderType);
/// der Modus wurde gerade ein- oder ausgeschaltet (ausgeschaltet: noch ca. 1 s Stillstand, das Rohr faehrt herunter)
void noteChanged(const DROID *psDroid, bool on);
/// Tabellen verwerfen (nach dem Neuladen der Werte)
void reset();
}
