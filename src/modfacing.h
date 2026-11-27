// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 21.11. 20:22): Einheiten ausrichten und in der Verfolgerkamera selbst fahren.
//  - Ausrichten per Ziehen: eine bereits gewaehlte Einheit anklicken und die Maus in eine Richtung ziehen -> alle gewaehlten Einheiten
//    drehen sich (auf der Stelle) in diese Richtung. Faehrt eine Einheit gerade, dreht sie sich nach der Ankunft.
//  - Taste "Ausrichten" (L): die gewaehlten Einheiten drehen sich zum Punkt unter dem Mauszeiger.
//  - Verfolgerkamera (Leertaste): W/S fahren vor/zurueck, A/D lenken (allein: auf der Stelle drehen) - statt die Kamera zu schieben.
//    (27.11.) Die gefahrene Einheit gehoert ganz den Tasten (keine anderen Bewegungen, kein Anhalten durch andere Logik); linke Maustaste
//    halten und ziehen = umschauen (beim Loslassen wieder geradeaus).
#pragma once

#include "lib/framework/vector.h"

struct DROID;

namespace modfacing
{
/// display.cpp processMouseClickInput: true = die Maus gehoert gerade dem Ausrichten (kein Auswahlrahmen, kein Klick)
bool processMouse(bool overUi, const Vector2i &world);
/// (Wunsch 26.11.) Rechtsklick-Befehle: rechte Taste auf freiem Boden druecken und ziehen = Fahrbefehl zum Druckpunkt, danach drehen sich
/// die Einheiten in die Ziehrichtung; true = die Maus gehoert gerade diesem Ziehen
bool processRightDrag(bool overUi, bool rightClickOrders, const Vector2i &world);
/// display3d.cpp: Richtungspfeile waehrend des Ziehens
void draw();
/// Taste "Ausrichten": gewaehlte Einheiten schauen zum Punkt unter der Maus
void faceCursor();
/// display.cpp handleCameraScrolling: Kamera-Tasten in der Verfolgerkamera; true = als Fahrbefehl verbraucht
bool drive(int forward, int turn);
/// loop.cpp: ausstehende Drehungen anwenden, Fahren beenden, wenn keine Taste mehr gehalten wird
void tick();
/// (Wunsch 27.11. "WASD Kontrolle bei einer Einheit laesst sich zu 100% vom Spieler steuern. Sie haelt nicht mehr ab und zu an"):
/// faehrt diese Einheit gerade per Tasten? Dann ruhen ihre Befehlslogik (Bewachen, Rueckkehr zum Wachpunkt ...) und das Anlassen.
bool isDriving(const DROID *psDroid);
/// move.cpp: true = Bewegungsbefehl an die gefahrene Einheit verwerfen (nur die Tastensteuerung selbst darf sie bewegen/anhalten)
bool blocksMove(const DROID *psDroid);
/// warcam.cpp: Blick nach links/rechts in der Verfolgerkamera (16-Bit-Winkel, 0 = genau hinter dem Fahrzeug)
int lookYaw();
/// display.cpp: linke Maustaste gedrueckt halten und ziehen = in der Verfolgerkamera nach links/rechts schauen; true = Maus verbraucht
bool processLook(bool overUi);
void reset();
}
