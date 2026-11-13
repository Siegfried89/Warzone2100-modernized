// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Befestigungsbau - Strg + Rechtsklick (Kreis wie beim Flaechenbeschuss) mit gewaehlten Baufahrzeugen.
//
// Das Baufahrzeug baut selbststaendig eine Verteidigungsanlage in 3 Groessen (klein / mittel / gross, Mausrad bei gehaltener Strg-Taste).
// Ablauf pro Fahrzeug: (1) Mulden (Krater) im Kreis zuschaufeln, (2) Plan erzeugen und als gewoehnliche Bauauftraege in die Auftragsliste
// einreihen. Der Plan legt Ringe um das Rechteck der eigenen Gebaeude im Kreis: von aussen nach innen Drachenzaehne (Beton-Panzersperren),
// Niemandsland, Panzerigel, Niemandsland, Panzerigel, wenig Niemandsland, Bunker + Flammenturm, Mauern, Moerstergruben.
// Der Plan haengt nur von Mittelpunkt, Groesse und den eigenen Gebaeuden ab (alle Rechner rechnen gleich); die Bauauftraege laufen ueber die
// vorhandene Technik (Strom, Helfer, Abbruch, Netzwerk). Alles ist allgemein gehalten: planFor() kann auch von anderen Mod-Teilen benutzt werden.
#pragma once

#include <vector>
#include "lib/framework/vector.h"

struct DROID;
struct GameWorld;
struct STRUCTURE_STATS;

namespace modbuild
{
struct Placement
{
	STRUCTURE_STATS *stats = nullptr;
	Vector2i tile;          // Kachel (Mitte der Struktur)
	Vector2i tile2;         // bei Linienbau Endkachel, sonst gleich tile
	bool line = false;      // Mauerlinie (DORDER_LINEBUILD)
	unsigned direction = 0; // 0..65535
};

/// Radius (Kacheln) der Groesse 1..3
int radiusOfTier(int tier);

/// Bauplan fuer einen Kreis (Mittelpunkt in Weltkoordinaten) und eine Groesse. Reihenfolge: von innen nach aussen.
std::vector<Placement> planFor(unsigned player, const Vector2i &worldCenter, int tier);

/// kann das Fahrzeug den Befehl bekommen? (Baufahrzeug ohne Feuerwaffe)
bool canFortify(const DROID *psDroid);
/// Befehl an alle gewaehlten Baufahrzeuge; true, wenn mindestens eines ihn bekam
bool orderSelected(unsigned player, int worldX, int worldY);
/// aus orderDroidBase: Befehl uebernehmen (Auftragsliste leeren, Arbeit anlegen)
void startOrder(DROID *psDroid, int worldX, int worldY, int tier);
/// Strg gehalten (ohne Umschalt) und mindestens ein gewaehltes Baufahrzeug?
bool keysForFortify();
/// jedes Bild: Kreis am Mauszeiger zeigen/ausblenden; Mausrad (bei Strg) wechselt die Groesse
void updatePreview(bool wanted, const Vector2i &worldPos);
/// Beschriftung am Mauszeiger, solange der Kreis zu sehen ist (sonst nullptr), z. B. "Fortify: medium (mouse wheel changes size)"
const char *activeLabel();
/// aktuell gewaehlte Groesse (1..3)
int currentTier();

/// Baukran-Bonus: Baupunkte pro Sekunde eines Baufahrzeugs, das im Umkreis (9 Kacheln) eines fertigen eigenen/verbuendeten Krans (A0BaBaCrane) arbeitet, +25 %
unsigned craneBoost(const DROID *psDroid, unsigned points);

/// nur Anzeige: Arbeitsbewegung des Baggerarms (Schwenken + Nicken) bzw. Pendeln der Abrissbirne, solange das Fahrzeug baut/abreisst/repariert.
/// Liefert true und Winkel in Grad (um die Hochachse / Querachse). anim = Grafikzeit (Phase der Bewegung).
bool armWork(const DROID *psDroid, unsigned anim, float &yawDeg, float &pitchDeg);
/// Abrissfaktor des Fahrzeugs (Abrissbirne reisst 4x so schnell ab, sonst 1)
int demolishFactor(const DROID *psDroid);

/// jeden Spielschritt
void tick(GameWorld &world);
/// bei neuem Spiel
void reset();
}
