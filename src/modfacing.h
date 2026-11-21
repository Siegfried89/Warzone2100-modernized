// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 21.11. 20:22): Einheiten ausrichten und in der Verfolgerkamera selbst fahren.
//  - Ausrichten per Ziehen: eine bereits gewaehlte Einheit anklicken und die Maus in eine Richtung ziehen -> alle gewaehlten Einheiten
//    drehen sich (auf der Stelle) in diese Richtung. Faehrt eine Einheit gerade, dreht sie sich nach der Ankunft.
//  - Taste "Ausrichten" (L): die gewaehlten Einheiten drehen sich zum Punkt unter dem Mauszeiger.
//  - Verfolgerkamera (Leertaste): W/S fahren vor/zurueck, A/D lenken (allein: auf der Stelle drehen) - statt die Kamera zu schieben.
#pragma once

#include "lib/framework/vector.h"

struct DROID;

namespace modfacing
{
/// display.cpp processMouseClickInput: true = die Maus gehoert gerade dem Ausrichten (kein Auswahlrahmen, kein Klick)
bool processMouse(bool overUi, const Vector2i &world);
/// display3d.cpp: Richtungspfeile waehrend des Ziehens
void draw();
/// Taste "Ausrichten": gewaehlte Einheiten schauen zum Punkt unter der Maus
void faceCursor();
/// display.cpp handleCameraScrolling: Kamera-Tasten in der Verfolgerkamera; true = als Fahrbefehl verbraucht
bool drive(int forward, int turn);
/// loop.cpp: ausstehende Drehungen anwenden, Fahren beenden, wenn keine Taste mehr gehalten wird
void tick();
void reset();
}
