// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. Punkt 5): Vorrang fuer Spieler-Befehle. Nach jedem eigenen Befehl (Klick, Taste, Schlachtplan, Knoepfe) gehoert die
// Einheit dem Spieler: solange sie den Befehl ausfuehrt und noch 45 Sekunden danach handelt sie NICHT selbststaendig (kein Ausweichen,
// kein Verteilen/Eingraben, keine Munitionsfahrt, kein Rueckzug unter Beschuss). Danach wird sie wieder autonom.
// Gilt fuer alle Spieler gleich (Befehle kommen ueber das Netz, Zeit = gameTime).
#pragma once

#include <cstdint>

struct DROID;

namespace modcontrol
{
constexpr unsigned GRACE_MS = 45000;
/// multibot.cpp: ein Befehl des Spielers fuer diese Einheit wurde ausgefuehrt
void onPlayerOrder(const DROID *psDroid);
/// steht die Einheit gerade unter Spieler-Kontrolle? (dann keine Autonomie)
bool manual(const DROID *psDroid);
/// (18.11.) Spielzeit seit dem letzten Spieler-Befehl an diese Einheit (sehr gross, wenn keiner gemerkt ist)
unsigned sincePlayerOrder(const DROID *d);
void reset();
}
