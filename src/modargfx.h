// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 18.11. 01:58 / 02:07): Grafik "Art Revolution" umschaltbar - im Hauptmenue per Knopf und mitten im Spiel per Tastendruck
// (Einfg), ohne Menuewechsel ("Bei Blizzard geht das auch").
//
// Das Paket mods/grafik/ARGrafik.wz (Benutzerdaten) enthaelt die Modelle der Art-Revolution-Mod, auf die Groessen des Mods gebracht, und
// ihre Texturen (umbenannt, damit sie keine Original-Seite verdecken). Es wird am Einhaengepunkt "graphics_overrides/curr" eingehaengt:
// diesen Ort fragt die Engine schon von sich aus zuerst nach Anzeige-Modellen und Texturen (die Spielwerte der Modelle bleiben die
// des Mods). Beim Umschalten werden nur die Anzeige-Modelle neu geladen (debugReloadAllDisplayModels) - Zeiger bleiben gueltig.
#pragma once

#include <string>

namespace modargfx
{
/// liegt das Paket bei den Benutzerdaten?
bool installed();
/// beim Aufbau des Suchpfads (init.cpp rebuildSearchPath): Paket einhaengen, wenn die AR-Grafik eingeschaltet ist
void applyMount();
/// umschalten (Hauptmenue oder im Spiel); inGame = Anzeige-Modelle sofort neu laden
void toggle(bool inGame);
}
