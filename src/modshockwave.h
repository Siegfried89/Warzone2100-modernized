// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 21:19 "Schockwelle sichtbar bei grossen Explosionen ... als zusaetzlichen Effekt bei grossen Ammodepot- und
// Oel-Explosionen. Wir haben ja schon einen Dome bei der Supreme-Commander-Schildkuppel. Vielleicht koennte man die zweckentfremden"):
//  - Die Halbkugel der Schildkuppel (gleiche Form) mit eigener Textur (Mod-Paket, shockwave.py: effects/modshockdome.pie, glatte
//    warmweisse Verdichtungs-Haut, unten am Boden am hellsten) waechst in knapp einer Sekunde blitzschnell auf und verblasst; dazu ein
//    heller Bodenring (modshockring.pie) an ihrem Fuss und drei Staubringe, die mit der Welle nach aussen laufen.
//  - Ausgeloest von grossen Oel-Explosionen (Oellager, Raffinerie, Fassstapel, Bohrturm mit vielen Faessern: ab 5 Faessern) und von
//    explodierenden Munitionsdepots / grossen Kistenhaufen (ab 3 Kisten). Groesse und Dauer wachsen mit der Ladung.
// Nur Anzeige (Echtzeit, Zufall erlaubt) - kein Einfluss auf den Spielzustand.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>

namespace modshockwave
{
/// grosse Explosion bei (Welt-x, Welt-y, Hoehe z); strength 1..10 (Faesser / Kisten), nur wenn sichtbar
void spawn(int worldX, int worldY, int worldZ, int strength);
/// MOD (Wunsch 27.11. 16:43 "der Sound muss gewaltiger sein, wenn ein Ammo-Depot oder Oeltanker oder Oellager in die Luft geht"):
/// geschichteter Explosionsklang - sofort grosser Knall + tiefer Nexus-Donner + Einsturz-Grollen, danach Nachexplosionen (Munition:
/// knatternde Detonationen und Querschlaeger, Oel: weitere Feuerbaelle). strength 1..10 wie bei spawn.
void bigBoomSound(int worldX, int worldY, int strength, bool ammo);
/// display3d: im 3D-Durchgang nach den Schildkuppeln
void draw3D(const glm::mat4 &viewMatrix);
void reset();
}
