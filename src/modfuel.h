// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. 18:33/19:07): Treibstoff fuer Fahrzeuge. Jedes Fahrzeug (Raeder, Ketten, Halbketten, Schweber - keine Infanterie,
// Cyborgs, Combots, Flieger, Transporter) hat einen Tank, der fuer eine lange Strecke reicht (FUEL_RANGE Kacheln Fahrweg). Verbraucht wird
// nur beim Fahren. Mit leerem Tank faehrt das Fahrzeug nur noch mit Mindesttempo und faehrt von selbst zur naechsten eigenen Tankstelle
// (Reparatureinrichtung - nicht das Combot-Reparaturgeruest), tankt dort voll und kehrt an die Stelle zurueck, wo der Tank
// leer wurde. Ein neuer Befehl des Spielers bricht die Fahrt ab. Anzeige: dritter, blauer Balken ganz unten unter den Einheiten.
// Gilt fuer ALLE Spieler (auch die KI). Getankt wird aus dem Vorrat der Tankstelle (Oelfaesser, die Lkw aus der Oelproduktion bringen;
// 1 Fass = 2 volle Tanks). KI ohne Tankstelle tankt an Fabrik/HQ. Alles im Spielschritt, deterministisch.
#pragma once

#include <vector>

struct DROID;
struct GameWorld;
struct STRUCTURE;

namespace modfuel
{
/// braucht die Einheit Treibstoff? (Spieler-Fahrzeug)
bool usesFuel(const DROID *psDroid);
/// Fuellstand 0..1000 (Promille); -1, wenn die Einheit keinen Treibstoff braucht
int permille(const DROID *psDroid);
/// Tempo-Faktor in Prozent (100 = normal, Mindesttempo bei leerem Tank)
int speedPercent(const DROID *psDroid);
/// ist das Bauwerk eine Tankstelle?
bool isStation(const STRUCTURE *psStructure);
/// Verbrauch, Tanken, Selbstversorgung (alle 500 ms)
void tick(GameWorld &world);
void reset();
/// Spielstand: Tankfuellung je Einheit (Paare id, Fahrweg)
void saveState(std::vector<int> &out);
void loadState(const std::vector<int> &in);
}
