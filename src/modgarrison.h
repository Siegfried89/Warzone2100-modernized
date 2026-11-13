// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Haeuser/Hochhaeuser besetzen (Wunsch 24)
//
// Gebaeude-Features mit "infantryPassable": true duerfen von Infanterie (Beine) betreten werden (Pfadsuche + Klick-Befehl, siehe modoil/fpath/display).
// Die Besatzung steht im Gebaeude, schiesst durch die Fenster und bekommt die Deckungs-Schadensminderung (modoil::droidInTrench).
// Wird das Gebaeude zerstoert, springen/sterben alle Besetzer an den Fenstern: sie werden deterministisch (gameRand) an den Rand des
// Gebaeudes versetzt und mit der normalen Sterbeanimation getoetet. Rechnet nur mit gameTime/gameRand, ist also netzwerkgleich.
#pragma once

struct FEATURE;
struct GameWorld;

namespace modgarrison
{
/// Aufruf aus removeFeature(), bevor das Feature von der Karte genommen wird
void onFeatureRemoved(FEATURE *psFeature, GameWorld &world);
}
