// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Combot-Auswahlfeld im Metal-Fatigue-Stil (blaues Diagramm mit Teilzustand + 12 Befehlstasten).
#pragma once

#include <string>

namespace modpanel
{
/// einmal pro Bild aus intRunWidgets() aufrufen (legt das Feld an / zeigt / versteckt es)
void tick();
/// beim Beenden der Oberflaeche aufrufen
void shutdown();
/// Blaues Combot-Diagramm samt Textzeilen und Feldern in das Panel-Bild zeichnen (px,py = Oberkante des Bildes, s = Skalierung).
/// pct = Zustand Torso, rechter Arm, linker Arm, Beine (0..100, -1 = fehlt); names = Namen derselben Teile.
void drawBlueprint(int px, int py, float s, const int pct[4], const std::string names[4], int sizeClass = 0);
/// waehrend der Designer offen ist, bleibt das Feld unsichtbar
void setSuppressed(bool suppressed);
}
