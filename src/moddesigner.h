// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Combot-Designer im Metal-Fatigue-Stil (Montagehalle mit langsam drehender 3D-Ansicht, Teileliste, Werte, Bauen).
#pragma once

#include "lib/framework/frame.h"
#include "lib/widget/widget.h"

namespace moddesigner
{
/// Designer oeffnen (aus dem Design-Bildschirm); false = Combot-Daten fehlen
bool open();
void close();
bool isOpen();
/// einmal pro Bild aus intRunWidgets() aufrufen (Animation, Mausrad, Anordnung)
void tick();
/// beim Beenden der Oberflaeche aufrufen
void shutdown();
/// Anzeigefunktion der hohen blauen Combot-Taste im Design-Bildschirm
void displayEntryButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset);
}
