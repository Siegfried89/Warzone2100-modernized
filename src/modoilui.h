// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 20.11. 17:46): Anzeigen zur Oel-Wirtschaft im WZ-Stil
//  - "Es muss einen deutlicheren Indikator (sofort sichtbaren) geben, wenn ein Gebaeude im PowerDown ist": ueber jedem abgeschalteten
//    eigenen Gebaeude blinkt ein WZ-Blitz-Sinnbild (gelber Blitz, rot durchgestrichen, in einer dunklen Plakette mit rotem Rand).
//  - Punkt 3 "deutlich sichtbaren Indikator": ueber einem Kraftwerk mit wenig Oel (hoechstens 3 Minuten) blinkt ein Oelfass-Sinnbild
//    (orange; rot und schneller, wenn es leer ist).
//  - Punkt 8 "UI-Grafik fuer den Oelwirtschaftskreislauf, um das alles zu monitoren": Fenster oben rechts mit allen Stufen des
//    Kreislaufs (Oeltuerme -> Transport -> Raffinerie -> Tanker -> Lager -> Kraftwerke / Tankstellen -> Energie), je Stufe ein
//    Ampelpunkt und die Zahlen; Knopf darueber (WZ-Befehlsknopf mit Blitz) schaltet es ein/aus. Nur Anzeige.
#pragma once

#include "lib/framework/vector.h"
#include <glm/fwd.hpp>

namespace modoilui
{
/// nach dem 3D-Bild: Sinnbilder ueber den Gebaeuden
void drawWorldIcons(const glm::mat4 &perspectiveViewMatrix);
/// jedes Bild aus intRunWidgets: Oel-Fenster
void tick();
void shutdown();
}
