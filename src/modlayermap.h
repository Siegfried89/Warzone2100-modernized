// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Eigene Karte je Ebene (Untergrund / Orbit) fuer die drei Ebenen von Metal Fatigue.
//
// Aufbau: Jede Ebene ist eine vollstaendige Karte (Kacheln, Blockier- und Hilfskarten). Es liegt immer genau eine davon in
// gameWorld.map ("aktive Karte"); die beiden anderen liegen hier im Zwischenspeicher. Das Umschalten tauscht nur die Zeiger
// (kostet nichts). Regeln:
//   - Die Spielschleife (Nachrichten + Spielschritt) laeuft immer mit der Oberflaechen-Karte; Einheiten einer anderen Ebene werden
//     nur waehrend ihres eigenen Schrittes (Bewegung, Sicht, Wegfindung) in ihre Karte "hineingeschaltet" (Scope).
//   - Zeichnen und Mauseingaben laufen mit der angezeigten Ebene (setView), damit Gelaende, Hoehen, Mausauswahl und Minikarte stimmen.
//   - Solange niemand eine andere Ebene benutzt (kein Objekt ausserhalb der Oberflaeche, Anzeige = Oberflaeche), ist alles ein No-Op.
//
// Die Zusatzkarten werden deterministisch aus der Oberflaechenkarte erzeugt (gleiche Karte -> gleiche Hoehlen auf jedem Rechner).
#pragma once

#include <cstdint>
#include "modlayer.h"

struct WorldMapState;

namespace modlayermap
{
using modlayer::Layer;

/// Zusatzkarten erzeugen (idempotent). Muss mit der Oberflaechenkarte in gameWorld.map aufgerufen werden, nach dem Kartenladen.
/// Gibt true zurueck, wenn die Karten bereitstehen.
bool ensureWorlds();
/// gibt es die Zusatzkarten?
bool worldsExist();
/// Ebene, deren Karte gerade in gameWorld.map liegt
Layer resident();
/// angezeigte Ebene (die Karte, die ausserhalb der Spielschleife aktiv ist)
Layer viewLayer();

/// Die Karte der Ebene in gameWorld.map legen (wenn noetig die bisherige in den Zwischenspeicher). Nur ueber Scope/setView benutzen.
void bring(Layer l);

/// Waehrend der Lebensdauer liegt die Karte der Ebene in gameWorld.map; danach wieder die vorherige.
/// Ohne Zusatzkarten ist das ein No-Op.
class Scope
{
public:
	explicit Scope(Layer l);
	~Scope();
	Scope(const Scope &) = delete;
	Scope &operator=(const Scope &) = delete;
private:
	Layer prev_;
	bool active_;
};

/// Schaltet die Ebene um, die ausserhalb der Spielschleife angezeigt wird (Gelaende, Beleuchtung, Minikarte werden neu aufgebaut).
/// Wird beim Ueberblenden in der Mitte aufgerufen.
void setView(Layer l);
/// einmal pro Bild (Zeichnen): erledigt aufgeschobene Aufgaben (Gelaende neu aufbauen nach Grabungen)
void tick();

/// Hoehe des Bodens der Ebene an einem Weltpunkt (unabhaengig davon, welche Karte gerade aktiv ist)
int32_t heightAt(Layer l, int worldX, int worldY);
/// Feld ist massiver Fels / Leere (nicht begehbar) in dieser Ebene
bool isSolid(Layer l, int tileX, int tileY);

/// Im Untergrund eine Kammer (Radius rad, Felder) um (tx,ty) freilegen, an das Gangnetz anbinden und fuer den Spieler (und Verbuendete)
/// im Umkreis revealRad als erkundet markieren. Fuer Schaechte und die Kammer unter jeder Basis. Deterministisch.
void openSite(int tileX, int tileY, int player, int rad, int revealRad);

/// alles freigeben (Spielende, neue Karte)
void reset();
}
