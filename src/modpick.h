// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 20.11. 16:52 "Select Hitbox fuer SAEMTLICHE Einheiten deutlich praeziser, nicht groesser als noetig, auch bei schraegem
// tiefem Kamerawinkel; nicht von Einheiten davor abgelenkt; Marschbefehle nicht durch das Auswahlsymbol einer Einheit im Vordergrund
// abgelenkt"): Beim Zeichnen jeder Einheit merkt sich die Anzeige, welche Modellteile (Wanne, Antrieb, Turm, Waffen, Beine ...) mit welcher
// Lage gezeichnet wurden. Ein Klick trifft eine Einheit nur, wenn die Maus auf einem ihrer echten Dreiecke im Bild liegt (plus 2 Bildpunkte
// Rand, damit winzige Soldaten in grosser Hoehe treffbar bleiben). Liegen mehrere Einheiten unter der Maus, gewinnt die, deren Oberflaeche
// der Kamera am naechsten ist - also die, die man an dieser Stelle tatsaechlich sieht. Auswahlring und Balken zaehlen nicht mehr zur
// Trefferflaeche. Nur Anzeige, kein Einfluss auf den Spielablauf.
#pragma once

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include <glm/fwd.hpp>

struct DROID;

namespace modpick
{
/// vor dem Zeichnen einer Einheit: ab jetzt jedes gezeichnete Modellteil fuer diese Einheit merken
void beginDroid(const DROID *psDroid, const glm::mat4 &perspectiveViewMatrix);
/// nach dem Zeichnen der Einheit
void endDroid();

/// Ergebnis einer Trefferpruefung
enum Hit
{
	PICK_NO_DATA = -1,   ///< nichts gemerkt (z. B. Radar-Echo) - der Aufrufer nimmt die alte Kastenpruefung
	PICK_MISS = 0,
	PICK_NEAR = 1,       ///< knapp daneben (innerhalb des Randes)
	PICK_EXACT = 2       ///< auf einem Dreieck des Modells
};
/// liegt der Bildpunkt (mx, my) auf der Einheit? depth = Entfernung der getroffenen Oberflaeche zur Kamera, dist = Abstand in Bildpunkten (NEAR)
Hit test(const DROID *psDroid, int mx, int my, float &depth, float &dist);

void reset();
}
