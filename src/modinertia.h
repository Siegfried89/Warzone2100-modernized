// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 19:45): "Beim Auswaehlen soll zuerst die Einheit sich melden, dann klicke ich auf Bewegen - der Panzer startet den
// Motor und der Panzer schuettelt kurz den Rumpf (die Ketten nicht so) - und DANN erst faehrt er los. Es soll ein kleines bisschen
// Traegheit simuliert werden. Aber nicht zu traege bitte. Medium Panzer sind etwas traege, schwere Panzer am traegsten."
//
//  - Anlassen: faehrt ein mittlerer/schwerer Ketten- oder Halbkettenpanzer aus dem Stand los, laeuft zuerst der Motor an (Startgeraeusch
//    gab es schon), der Rumpf schuettelt sich kurz (modrecoil::engineStart, die Ketten nur wenig) und erst nach einer kurzen Pause
//    (mittel 0,35 s, schwer 0,6 s, superschwer 0,75 s) rollt er los.
//  - Traegheit: Beschleunigen und Bremsen der Bodenfahrzeuge (Ketten, Halbketten, Raeder) haengt von der Rumpfgroesse ab: leichte wie
//    bisher, mittlere etwas, schwere und superschwere deutlich langsamer (aber nicht zaeh).
// Spielrelevant und fuer alle Rechner gleich (Spielzeit, Einheitennummer).
#pragma once

struct DROID;

namespace modinertia
{
/// Beschleunigung in Prozent der Antriebswerte (100 = unveraendert)
int accelPercent(const DROID *psDroid);
/// Bremsen in Prozent der Antriebswerte (100 = unveraendert)
int decelPercent(const DROID *psDroid);
/// aus moveUpdateDroid(): started = faehrt in diesem Schritt aus dem Stand an. true = der Motor laeuft noch an - noch nicht fahren.
bool holdStart(DROID *psDroid, bool started);
void reset();
}
