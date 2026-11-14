// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Die drei Ebenen von Metal Fatigue (Orbit / Oberflaeche / Untergrund) als Knoepfe mit weichem Wechsel.
// Vorerst nur die Bedienoberflaeche und der Zustand: ein Wechsel blendet weich ueber, danach liegt ein leichter Farbton
// ueber dem Bild. Die eigentliche Spielmechanik (getrennte Karten-Ebenen, Schacht, Orbit) kommt spaeter und liest current().
//
// Spielzustand (Ebene je Objekt, deterministisch aus der Spielschleife): Einheiten und Bauwerke liegen auf der Oberflaeche (Standard), im
// Untergrund oder im Orbit. Objekte auf verschiedenen Ebenen sehen, beschiessen und stoeren sich nicht. Untergrund-Einheiten laufen wie
// "Geister" unter allen Bauwerken und Haengen hindurch (Wegfindung wie Lufteinheiten). Zwischen Oberflaeche und Untergrund wechselt eine
// Einheit, indem sie zu einem eigenen Schacht (A0LayerShaft) faehrt. Ohne Objekte ausserhalb der Oberflaeche aendert sich im Spiel nichts.
#pragma once

#include <cstdint>
#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include "lib/framework/frame.h"
#include "statsdef.h"

struct SIMPLE_OBJECT;
struct BASE_OBJECT;
struct DROID;
struct STRUCTURE;
struct PROJECTILE;

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

// ---- Spielzustand ----
/// Ebene eines Objekts (Standard Oberflaeche)
Layer layerOf(const SIMPLE_OBJECT *obj);
/// legt die Ebene eines Objekts fest (z. B. Lavafelder im Untergrund); das Objekt muss in der Karte dieser Ebene angelegt worden sein
void assignLayer(const SIMPLE_OBJECT *obj, Layer l);
/// gibt es ueberhaupt Objekte ausserhalb der Oberflaeche?
bool active();
/// koennen die beiden Objekte einander sehen / beschiessen / behindern? (gleiche Ebene)
bool canInteract(const SIMPLE_OBJECT *a, const SIMPLE_OBJECT *b);
/// kann dieses Geschoss das Ziel treffen? (Ebene der Quelle, sonst des Ziels)
bool projectileCanHit(const PROJECTILE *proj, const SIMPLE_OBJECT *target);
/// Ebene, in der ein Geschoss fliegt (Ebene der Quelle, sonst des Ziels, sonst Oberflaeche)
Layer projectileLayer(const PROJECTILE *proj);
/// Anzeige: soll dieses Objekt in der gerade angezeigten Ebene verborgen sein? (Schaechte sieht man in jeder Ebene)
bool hiddenInView(const BASE_OBJECT *obj);
/// Wegfindung / Blockierung: Untergrund-Einheiten verhalten sich wie Lufteinheiten (nichts versperrt den Weg)
PROPULSION_TYPE effectivePropulsion(const DROID *psDroid, PROPULSION_TYPE realType);
/// steht auf diesem Feld ein fertiger, fuer diesen Spieler nutzbarer Schacht?
bool isPortalTile(int player, int tileX, int tileY);
/// jeden Spielschritt aus der Spielschleife: Einheiten, die einen Schacht erreichen, wechseln die Ebene
void gameTick();
/// Ebenen-Knopf gedrueckt: die ausgewaehlten Flieger (Hoverjets, Drohnen, VTOLs) steigen in den Orbit bzw. kehren zur Oberflaeche zurueck
/// (der Wechsel selbst geschieht im naechsten Spielschritt; in den Untergrund koennen Flieger nicht)
void sendSelectedFliers(Layer target);
/// Flieger? (schwebende Antriebe wie Balldrohne/Hoverjet und VTOLs)
bool isFlier(const DROID *psDroid);
/// beim Baubeginn: das Bauwerk entsteht auf der Ebene des Baumeisters
void inheritFromBuilder(const STRUCTURE *psStruct, const DROID *psBuilder);
/// eigenes Objekt in einer nicht angezeigten Ebene wurde getroffen: Meldung "Under attack: Orbit/Underground/Surface" (nur Anzeige, nicht synchronisiert)
void onUnderAttack(const BASE_OBJECT *victim);
/// beim Zerstoeren eines Objekts
void forget(uint32_t id);
/// Spielzustand loeschen (Spielende / Laden)
void resetState();
}
