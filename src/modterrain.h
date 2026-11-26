// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Gelaende verformen - Krater durch Beschuss (3 Groessen), Deckung fuer Infanterie in Kratern, Zuschaufeln durch Baufahrzeuge.
//
// Das Gelaende besteht aus Eckpunkten (Hoehe je Kachelecke). Ein Krater senkt die Eckpunkte im Umkreis schalenfoermig ab und hebt
// einen kleinen Wall am Rand an. Fuer jeden veraenderten Eckpunkt merkt sich das Modul die urspruengliche Hoehe, damit Bagger die
// Mulde wieder auffuellen koennen und sich mehrere Einschlaege zu groesseren Mulden verbinden (die Tiefe ist nach unten begrenzt).
// Alles laeuft im Spielschritt und ist deterministisch (kein Zufall, keine Anzeige-Abhaengigkeit), gilt also auf allen Rechnern gleich.
// Aenderungen, die von aussen kommen (Bauwerksfundamente, Gruben), werden erkannt: stimmt die Hoehe nicht mehr mit der vom Modul
// gesetzten ueberein, wird der Eckpunkt vergessen und nie "zurueckgesetzt".
#pragma once

#include "lib/framework/vector.h"

#include <vector>

struct BASE_OBJECT;
struct DROID;
struct PROJECTILE;
struct WorldMapState;

namespace modterrain
{
/// Krater der Groesse 1 (leicht) .. 3 (schwer) an einem Weltpunkt der aktiven Karte. false, wenn dort kein Krater moeglich ist
/// (Wasser, Klippe, Bauwerk, Kartenrand).
bool addCrater(int worldX, int worldY, int size);

/// Beim Geschoss-Einschlag aufrufen: legt je nach Waffe (Kanone, Moerser, Haubitze, Bombe) und Treffpunkt (nur Boden) einen Krater an.
void onProjectileImpact(const PROJECTILE *psProj);

/// Tiefe (Weltmasseinheiten) der Mulde auf dieser Kachel: Mittel der vier Eckpunkte gegenueber der Ausgangshoehe; 0 = eben/erhoeht.
int depthAtTile(int tileX, int tileY);
/// Bietet die Mulde auf diesem Feld Deckung (fuer Infanterie)?
bool tileGivesCover(int tileX, int tileY);
/// Steht die Einheit (Infanterie) in einer Mulde mit Deckung?
bool droidInCover(const DROID *psDroid);

/// Mulde um die Kachel ein Stueck auffuellen (amount je Eckpunkt in Hoeheneinheiten). Gibt die verbleibende Tiefe zurueck.
int fillTile(int tileX, int tileY, int amount);
/// Kachel (Mitte) der naechsten Mulde im Umkreis (Kacheln) um einen Weltpunkt mit Mindesttiefe; false, wenn keine.
bool findDent(const Vector2i &worldCenter, int radiusTiles, int minDepth, Vector2i &outTile);
/// (22.11. 21:19, Battle-Plan "Restore") Tiefe des Kraters/Lochs auf der Kachel (nur zuschuettbare Mulden; Graben-Bauwerke, Tunnel und
/// Durchbrueche zaehlen nicht), 0 = unberuehrt
int craterDepthAt(int tileX, int tileY);

/// Eine Kachel absenken (Graben) und optional links/rechts davon einen kleinen Erdwall anheben (dir: 0..65535, Laengsrichtung
/// des Grabens). Wird vom Befestigungsbau fuer reine Erdgraeben verwendet.
bool dig(int tileX, int tileY, int depth, int bermHeight, unsigned dirAngle);

/// 16.11.: Absenkung (Weltmasseinheiten) an einem Weltpunkt der aktiven Karte - Kacheln sinken mit fast senkrechten Kanten ab.
/// Ganzzahlig und deterministisch (fuer map_Height / Spiellogik); sinkAtF ist dasselbe fuer die Anzeige (Gelaendenetz).
int sinkAt(const WorldMapState &mapState, int worldX, int worldY);
float sinkAtF(const WorldMapState &mapState, float worldX, float worldY);
/// (17.11. 14:14) wie sinkAtF, aber tiefe Stellungen nur bis zur Sohle (Standhoehe der Einheiten beim Zeichnen)
float sinkStandF(const WorldMapState &mapState, float worldX, float worldY);
/// (26.11.) Weltmitte des Kraters auf diesem Feld (Feinraster: gemeinsame Ecke des 2 x 2-Kraters), sonst Feldmitte
Vector2i craterCenter(int tileX, int tileY);
/// groesste Absenkung der vier Kacheln an einer Kachelecke (Klassik-Netz ohne Unterteilung)
int sinkAtCorner(const WorldMapState &mapState, int x, int y);
/// (17.11.) zusaetzliche Absenkung des Feldmittelpunkts (Klassik-Netz ohne Unterteilung): Krater/Schuetzenloch als Trichter in
/// genau einem Feld, die Nachbarfelder bleiben eben
int centerSink(const WorldMapState &mapState, int x, int y);
/// (17.11.) Krater/Schuetzenloch auf diesem Feld: fuer alle Fahrzeuge (Raeder, Ketten, Halbketten, Luftkissen) unbefahrbar
bool blocksVehicles(const WorldMapState &mapState, int x, int y);
/// Absenkung durch ein Bauwerk (tiefer Graben) setzen; 0 = aufheben (alt: ganze Kachel - seit 24.11. ungenutzt, siehe TrenchSink)
void setStructureSink(int tileX, int tileY, int depth);

/// MOD (Wunsch 24.11. 15:33 "Die Graeben sind keine, und die tiefen Graeben versinken nicht richtig"): Graben-Bauwerke senken das
/// Gelaende nicht mehr als ganze Kachel (Kasten, in dem das schmale Grabenmodell schwebte), sondern nur entlang ihres Grabens: eine Rinne
/// durch die Feldmitte zu jeder offenen Seite (Graben an Graben durchgehend, zu einem Feld ohne Graben steigt die Rinne bis zur Feldkante
/// auf Bodenhoehe an), bei Gruben ein Becken. Ausserhalb des Feldes (128 x 128 um die Mitte) bleibt das Gelaende unberuehrt.
struct TrenchSink
{
	int x = 0, y = 0;       ///< Mitte (Welt)
	int floor = 0;          ///< Sohle des Modells unter dem Boden (Standhoehe der Einheiten in der Rinne)
	int vis = 0;            ///< Tiefe des Gelaendenetzes in der Rinne (tiefer als die Sohle - das grobe Netz stoesst nirgends durch)
	uint8_t arms = 0;       ///< Rinne zur Seite: 1 Nord, 2 Ost, 4 Sued, 8 West
	uint8_t ramps = 0;      ///< davon: steigt zur Feldkante auf Bodenhoehe an (kein absenkender Nachbar)
	bool pit = false;       ///< Grube: Becken statt Rinne
};
/// alle Graben-Absenkungen der aktiven Ebene neu setzen (aus dem Spielzustand, auf allen Rechnern gleich; Reihenfolge egal)
void setTrenchSinks(std::vector<TrenchSink> list);

/// (17.11. 14:14) "Beim Krater sieht man einen kegelfoermigen Erdhaufen. Den schuettet der Bagger dann hinein."
/// Erdhaufen am Rand jedes Kraters/Schuetzenlochs der aktiven Ebene (nur Anzeige): Weltlage, Groesse 0..1 (nach Resttiefe), Drehung
struct CraterPile
{
	int worldX = 0, worldY = 0;
	float scale = 1.f;
	int rotDeg = 0;
	float funnel = 0.f;   ///< (23.11.) Tiefe des sichtbaren Trichters an dieser Stelle (Anzeige: Fusshoehe auf der Flanke)
};
void craterPiles(std::vector<CraterPile> &out);
/// (18.11.) Kraterflecke (weicher Rand, ein Viertel Kachel) in der Kachelmitte: depth 0..1 (Resttiefe), alpha = Einblenden
struct CraterDecal
{
	int tileX = 0, tileY = 0;
	int worldX = 0, worldY = 0;
	float depth = 1.f, alpha = 1.f, scale = 1.f;
	int rotDeg = 0;
};
void craterDecals(std::vector<CraterDecal> &out);

/// jeden Spielschritt: verzoegerte Krater anlegen (unter der Staubwolke)
void tick();

/// bei neuem Spiel
void reset();

/// (16.11. 19:34, Q95) Spielstand: Krater/Schuetzenloecher der Oberflaeche als flache Zahlenliste (je 6 Werte:
/// x, y, Tiefe, Originaltextur, Textur getauscht, Original-Decal) - und wieder einlesen nach dem Laden.
void saveState(std::vector<int> &out);
void loadState(const std::vector<int> &in);
}
