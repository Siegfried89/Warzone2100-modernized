// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Invade (MF: Bohrfahrzeug bohrt sich durch die Wand und uebernimmt ein Gebaeude)
//
// Rechtsklick eines gewaehlten Bohrfahrzeugs (Konstruktionsteil "DrillRig") auf ein feindliches Gebaeude schickt es mit einem gewoehnlichen
// (netzwerkweit abgeglichenen) Bewegungsbefehl direkt an die Wand. Steht ein Bohrfahrzeug untaetig neben einem feindlichen Gebaeude, bohrt es
// sich hinein: nach einer Bohrzeit (haengt von der Gebaeudestaerke ab) wechselt das Gebaeude den Besitzer (giftSingleStructure). Die Bohrfahrzeuge
// muessen dabei stehen bleiben; jeder neue Befehl bricht das Bohren ab. Weil das Bohren nur vom Spielzustand abhaengt, rechnen alle Rechner gleich.
// Eroberte Hauptgebaeude (HQ, Forschung, Fabriken) zaehlen als "Verstaerker" (MF: Enhancer, +1 je Anlage, hoechstens 5; Wirkung: modinvade::enhancer).
// Raze (Abriss durch Hover-Truck, 50 % Rueckerstattung) bringt WZ schon mit: eigene Gebaeude abreissen erstattet die Haelfte.
#pragma once

#include "lib/framework/vector.h"

struct DROID;
struct BASE_OBJECT;
struct GameWorld;

namespace modinvade
{
/// Kann dieses Bohrfahrzeug dieses Objekt einnehmen? (feindliches, fertiges Gebaeude auf derselben Ebene, keine Mauer/Tor)
bool canInvade(const DROID *psDroid, const BASE_OBJECT *psObj);
/// Zielpunkt direkt an der Wand des Gebaeudes (auf der Seite des Fahrzeugs); false, wenn das Fahrzeug/Objekt nicht passt
bool approachPoint(const DROID *psDroid, const BASE_OBJECT *psObj, Vector2i &worldPos);
/// jeden Spielschritt
void tick(GameWorld &world);
/// Verstaerker eines Spielers (0..5): jeder +6 % Schaden (die Schadensformel liest ihn ueber enhancerPercent)
int enhancer(unsigned player);
int enhancerPercent(unsigned player);
/// laeuft gerade eine Bohrung dieses Fahrzeugs? (0..100 = Fortschritt, sonst -1)
int boreProgress(const DROID *psDroid);
/// neues Spiel
void reset();
}
