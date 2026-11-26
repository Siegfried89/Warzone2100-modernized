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
/// MOD (21.11.): Breite, die die Ebenen-Knoepfe am rechten Rand oben belegen (0 = verborgen) - andere Anzeigen weichen nach links aus
int reservedRight();
/// (26.11.) Rechteck der Ebenen-Knoepfe oben rechts; false = verborgen
bool screenRect(int &x0, int &y0, int &x1, int &y1);
/// beim Beenden der Oberflaeche aufrufen; setzt die Ebene auf Oberflaeche zurueck
void shutdown();

// ---- Spielzustand ----
/// Ebene eines Objekts (Standard Oberflaeche)
Layer layerOf(const SIMPLE_OBJECT *obj);
/// legt die Ebene eines Objekts fest (z. B. Lavafelder im Untergrund); das Objekt muss in der Karte dieser Ebene angelegt worden sein
void assignLayer(const SIMPLE_OBJECT *obj, Layer l);
/// Ebene eines Fahrzeugs sofort wechseln (Bohrfahrzeug, das sich durch eine Klippe in den Untergrund bohrt); setzt die Hoehe neu
void moveToLayer(DROID *d, Layer next);
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
/// Flieger? (Hoverjets, VTOLs, Transporter mit Luftantrieb und Combots mit Sprungduesen/Jetpack)
bool isFlier(const DROID *psDroid);
/// (18.11.) Transporter entlaedt: die Fracht liegt in der Ebene des Transporters
void onUnloaded(const DROID *psTransporter, const DROID *psCargo);
/// Anzeige: Hoehenversatz beim fliessenden Ebenenwechsel (steigt aus dem Bild / taucht aus der verschwommenen Tiefe auf), 0 sonst
float transitionOffset(const DROID *psDroid, UDWORD time);
/// Anzeige (Antwort 18.11. 17:52 "das fast senkrechte Aufsteigen von Flugeinheiten in den Orbit"): Lage beim Ebenenwechsel - Hoehe,
/// Versatz in Flugrichtung und Nickwinkel (WZ-Winkel, + = Nase hoch). Steigflug mit ca. 75 Grad, Nase hoch; Sinkflug mit der Nase nach unten.
struct FlightPose
{
	float up = 0.f;
	float forward = 0.f;
	int pitch = 0;
};
FlightPose transitionPose(const DROID *psDroid, UDWORD time);
/// wechselt die Einheit gerade die Ebene?
bool inTransition(const DROID *psDroid);
/// Spielschritt (z. B. KI): Flieger fliessend in eine andere Ebene schicken (Orbit <-> Oberflaeche)
void requestLayer(DROID *psDroid, Layer target);
/// beim Baubeginn: das Bauwerk entsteht auf der Ebene des Baumeisters
void inheritFromBuilder(const STRUCTURE *psStruct, const DROID *psBuilder);
/// eigenes Objekt in einer nicht angezeigten Ebene wurde getroffen: Meldung "Under attack: Orbit/Underground/Surface" (nur Anzeige, nicht synchronisiert)
void onUnderAttack(const BASE_OBJECT *victim);
/// beim Zerstoeren eines Objekts
void forget(uint32_t id);
/// Spielzustand loeschen (Spielende / Laden)
void resetState();
/// (Wunsch 17.11.: Untergrund/Orbit im Spielstand) Ebene je Objekt (id, Ebene ...) und die gebohrten Untergrund-Felder
void saveState(std::vector<int64_t> &objs, std::vector<int> &drills);
/// nach dem Laden: Zustand merken; eingesetzt wird er im ersten Spielschritt, sobald die Zusatzkarten stehen (applyLoadedState)
void loadState(const std::vector<int64_t> &objs, const std::vector<int> &drills);
/// aus der Spielschleife nach modlayermap::ensureWorlds(): Felder nachbohren, Objekte in die Karte ihrer Ebene umtragen
void applyLoadedState();
}
