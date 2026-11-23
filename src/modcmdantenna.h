// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 04:23): "Antennen machen (von Kommandozentralen oben auf dem Dach). Elektrische Animation, das Rot soll leuchten und
// ab und zu blinken ... Jedes Fahrzeug in einer Commander-gefuehrten Gruppe braucht eine Antenne hinten."
//  - Jedes Fahrzeug, das einem Kommandeur zugeteilt ist (nicht der Kommandeur selbst, keine Infanterie/Cyborgs/Transporter), traegt
//    hinten links eine schlanke Fassung der Dach-Antenne der Kommandozentrale (components/weapons/modcmdantenna.pie, Paket cmdantenna.py).
//  - Die rote Lampe leuchtet ruhig und blinkt alle 3-7 Sekunden zweimal hell auf (Leucht-Ebene "glowlvl-blink", modglowlevel.cpp);
//    beim Blinken knistert ein kleiner elektrischer Funke an der Spitze und ein schwaches rotes Licht faellt auf die Umgebung.
// Nur Anzeige (rand(), graphicsTime) - kein Einfluss auf den Spielzustand.
#pragma once

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/ivis_opengl/piedef.h"

#include <glm/fwd.hpp>

struct DROID;
struct iIMDShape;

namespace modcmdantenna
{
/// component.cpp, nach dem Rumpf: Antenne zeichnen, falls das Fahrzeug einem Kommandeur zugeteilt ist
bool draw(const DROID *d, const iIMDShape *body, const glm::mat4 &bodyMatrix, const glm::mat4 &view, UDWORD colour, PIELIGHT brightness,
          int pieFlag, int pieData, float heightAboveMap);
}
