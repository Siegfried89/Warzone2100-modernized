// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Die drei Ebenen von Metal Fatigue (Orbit / Oberflaeche / Untergrund) als Knoepfe mit weichem Wechsel.
// Vorerst nur die Bedienoberflaeche und der Zustand: ein Wechsel blendet weich ueber, danach liegt ein leichter Farbton
// ueber dem Bild. Die eigentliche Spielmechanik (getrennte Karten-Ebenen, Schacht, Orbit) kommt spaeter und liest current().
#pragma once

namespace modlayer
{
enum Layer
{
	UNDERGROUND = 0,
	SURFACE = 1,
	ORBIT = 2,
};

/// aktuell angezeigte Ebene (waehrend der Ueberblendung erst nach der halben Zeit umgeschaltet)
Layer current();
/// Wechsel anfordern (weiche Ueberblendung ca. 0,7 s); tut nichts, wenn schon aktiv oder gerade ein Wechsel laeuft
void request(Layer target);
/// einmal pro Bild aus intRunWidgets() aufrufen
void tick();
/// waehrend der Designer offen ist, bleiben die Knoepfe unsichtbar
void setSuppressed(bool suppressed);
/// beim Beenden der Oberflaeche aufrufen; setzt die Ebene auf Oberflaeche zurueck
void shutdown();
}
