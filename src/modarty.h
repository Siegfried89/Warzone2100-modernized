// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Artillerie-Modus fuer Panzer und Laeufer mit normalen Kanonen (Wunsch 14.11., Punkt 21).
//  - Neuer Knopf im Befehlsmenue (Zweitbefehl DSO_ARTILLERY). Eingeschaltet richtet der Panzer das Rohr steil auf wie ein
//    Artilleriepanzer und schiesst im Bogen (indirekt) mit groesserer Reichweite, langsamerer Feuerfolge und mehr Streuung.
//    Die Geschosse sind die normalen, sichtbaren WZ-Kanonengeschosse; der Rueckstoss (modrecoil) bleibt.
//  - Technisch: zu jeder geeigneten Kanone liefert der Mod eine zweite Waffe "<ID>_ARTY" (stats/weapons.json, nicht im Designer).
//    Der Modus tauscht nur die Waffennummer der Einheit zwischen beiden Fassungen - deterministisch, im Spielstand gespeichert,
//    ueber den normalen Zweitbefehl auch im Mehrspieler synchron. Gilt fuer alle Spieler gleich.
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
/// Tabellen verwerfen (nach dem Neuladen der Werte)
void reset();
}
