// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Antwort 18.11. 17:52 auf die Frage zum Transporter-Flug: "so dass die Dogfights von Flugeinheiten gegenseitig moeglich sind, das fast
// senkrechte Aufsteigen von Flugeinheiten in den Orbit"): Luftkampf zwischen allen Fliegern.
//  - Flieger in der Luft (VTOLs, Hubschrauber, schwebende Drohnen, Transporter mit Bordwaffe) schiessen auf feindliche Flieger, die sie
//    erfassen koennen, auch waehrend sie unterwegs sind: feste Bordwaffen, sobald der Gegner vor der Nase ist (Begegnung im Gegenkurs,
//    Verfolgung), drehbare Bordwaffen ringsum. Der Flieger weicht dafuer nicht von seinem Kurs ab.
//  - Untaetige Flieger in der Luft (kein Befehl oder Wache) greifen feindliche Flieger in Reichweite selbst an - auch die des Spielers
//    (WZ laesst Spieler-VTOLs sonst nie selbst Ziele suchen). Der Angriff laeuft ueber die normalen VTOL-Angriffsanfluege, d. h. zwei
//    Flieger, die sich gegenseitig angreifen, kurven umeinander.
//  - Transporter mit Bordwaffe (Kampagnen-Transporter: MG) verteidigen sich gegen Flieger.
// Bodenziele bleiben wie bisher. Alles im Spielschritt aus Spielzustand (Lage, Sicht, Befehle, Munition) -> netzwerkgleich.
#pragma once

struct DROID;

namespace modairwar
{
/// Flieger in der Luft? (Luftantrieb und unterwegs, oder immer schwebend)
bool airborne(const DROID *psDroid);
/// jeden Spielschritt aus der Spielschleife
void tick();
/// Spielende / Laden
void reset();
}
