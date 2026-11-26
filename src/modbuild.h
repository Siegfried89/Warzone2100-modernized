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

struct DroidOrder;

#include <vector>
#include "lib/framework/vector.h"

struct DROID;
struct GameWorld;
struct STRUCTURE_STATS;
struct STRUCTURE;

struct MAPTILE;

namespace modbuild
{
struct Placement
{
	STRUCTURE_STATS *stats = nullptr;
	Vector2i tile{0, 0};          // Kachel (Mitte der Struktur)
	Vector2i tile2{0, 0};         // bei Linienbau Endkachel, sonst gleich tile
	bool line = false;      // Mauerlinie (DORDER_LINEBUILD)
	unsigned direction = 0; // 0..65535
};

/// (18.11.) fremder Bauplan als Vorschau (z. B. Artilleriebasis aus dem Schlachtplan); nullptr = keiner. Der Zeiger muss gueltig bleiben.
void setExternalPreview(const std::vector<Placement> *plan);

/// (26.11.) Teile eines Bauplans ausserhalb der baubaren Karte entfernen (Kartenrand/Scroll-Grenzen + TOO_NEAR_EDGE wie validLocation);
/// Mauerlinien werden auf ihr baubares Stueck gekuerzt. unit = Weltlaenge einer Plan-Kachel (TILE_UNITS bzw. DIST_TILE des Schlachtplans).
void clipToMap(std::vector<Placement> &plan, int unit);

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
/// Anzeige (16.11. 19:54): Bauplan-Vorschau am Mauszeiger, solange der Kreis zu sehen ist (gruen gezeichnet), sonst nullptr
const std::vector<Placement> *previewPlan();
/// Weltpunkt (Mitte) einer Plan-Kachel (Placement::tile) - der Plan rechnet in Original-Kacheln, auch auf dem feinen Raster
Vector2i planTileWorld(const Vector2i &planTile);

/// Baukran-Bonus: Baupunkte pro Sekunde eines Baufahrzeugs, das im Umkreis (9 Kacheln) eines fertigen eigenen/verbuendeten Krans (A0BaBaCrane) arbeitet, +25 %
unsigned craneBoost(const DROID *psDroid, unsigned points);
/// (24.11. 16:42 "was ist der blaue Kreis um den Kran? ... warum ist der so klein?"): Umkreis (Welt), in dem der Kran schneller bauen laesst
int craneRadius();

/// nur Anzeige: Arbeitsbewegung des Baggerarms (Schwenken + Nicken) bzw. Pendeln der Abrissbirne, solange das Fahrzeug baut/abreisst/repariert.
/// Liefert true und Winkel in Grad (um die Hochachse / Querachse). anim = Grafikzeit (Phase der Bewegung).
bool armWork(const DROID *psDroid, unsigned anim, float &yawDeg, float &pitchDeg);
/// Kampf-Infanterie mit Spaten (echte Infanterie mit Waffe UND Bauteil): zaehlt als Baufahrzeug, baut aber nur "infantryBuildable"-Bauwerke
bool isInfantryBuilder(const DROID *psDroid);
/// MOD (16.11.): Spieler-Baubefehl fuer Infanterie (multibot, auf allen Rechnern gleich): aus einer einzelnen Stellung (MG-Nest/Grube)
/// wird ein kleiner Komplex - Schuetzengraben links und rechts, je eine weitere Grube an den Enden, Munitionskiste dahinter.
void onPlayerBuildOrder(DROID *psDroid, const DroidOrder &order, bool queued);
/// darf dieses Fahrzeug dieses Bauwerk bauen? (Kampf-Infanterie nur Graeben/Gruben/Sandsack/MG-Nester; alle anderen Baufahrzeuge alles)
bool mayBuild(const DROID *psDroid, const STRUCTURE_STATS *psStats);
/// (Wunsch 26.11. 22:19 "Bagger koennen Hindernisse wie Steine entfernen ... das herumbauen um Hindernisse bei templates soll minimiert
/// werden") Bagger = Baufahrzeug mit Kran-/Baggerarm (scavCrane*)
bool isExcavator(const DROID *psDroid);
/// besitzt der Spieler mindestens einen Bagger?
bool hasExcavator(unsigned player);
/// Anzeige/Bedienung: ist unter den gewaehlten Fahrzeugen des Spielers ein Bagger?
bool selectedExcavator(unsigned player);
/// kleines Hindernis auf der Kachel, das ein Bagger wegraeumt: Steine/Truemmer/Panzersperren-Reste (1x1 BOULDER) und Baeume/Buesche;
/// NICHT: Lavafelder, Riesen-Felsen (2x2/3x3), Ruinen, Wracks, Oel
bool isClearableObstacle(const MAPTILE *psTile);
/// solange ein Scope mit on=true lebt, zaehlen raeumbare Hindernisse in validLocation nicht als belegt (Plaene, Bagger am Bauplatz)
class ObstacleScope
{
public:
	explicit ObstacleScope(bool on);
	~ObstacleScope();
	ObstacleScope(const ObstacleScope &) = delete;
	ObstacleScope &operator=(const ObstacleScope &) = delete;
private:
	bool prev_;
};
bool obstaclesIgnored();
/// Bagger beginnt zu bauen: raeumbare Hindernisse auf dem Bauplatz entfernen (Staubwolke). Krater unter dem Bauplatz schuettet ohnehin
/// jeder Bau zu (nur von Spielern verursachte Mulden - natuerliche Unebenheiten der Karte bleiben, siehe modterrain).
void clearObstacles(const STRUCTURE_STATS *psStats, Vector2i pos, uint16_t direction);
/// Abrissfaktor des Fahrzeugs (Abrissbirne reisst 4x so schnell ab, sonst 1)
int demolishFactor(const DROID *psDroid, const STRUCTURE *psTarget = nullptr);
/// (18.11.) Pendelwinkel der Abrissbirne in Grad (nur Anzeige)
float ballSwing(const DROID *psDroid, unsigned anim);
/// (16.11. 19:55) Baufahrzeuge gewaehlt + Klick auf einen eigenen geraden Mauerabschnitt: dort ein Tor bauen (die Mauer wird ersetzt).
/// Scav-Mauern bekommen das Scav-Tor (A0BaBaGate), andere Mauern das Beton-Tor. true, wenn der Befehl erteilt wurde.
bool gateFromWall(unsigned player, const STRUCTURE *psWall, bool add);

/// MOD (Wunsch 20.11.): Grabennetz entlang einer gezeichneten Verteidigungslinie (Weltpunkte in Zeichenreihenfolge). side = Seite der
/// Querstriche (+1/-1 wie modpaint::defendSide: Normale (-ty, tx) * side zeigt zum Feind). Hauptgraben lueckenlos entlang der Linie,
/// Gruben darin (abwechselnd bewaffnet), Verbindungsgraeben nach hinten mit Reservegrube und Munitionskiste; alles blickt zum Feind.
/// Alle Eintraege sind Linienbau (line = true, Einzelstuecke mit tile == tile2), Kacheln = Plan-Kacheln (Welt = Kachel * DIST_TILE + DIST_TILE / 2).
/// Rein lesend (veraendert nichts, kein Zufall) - fuer Vorschau und Bauauftraege.
std::vector<Placement> planDefenseNet(unsigned player, const std::vector<Vector2i> &strokeWorld, int side);

/// MOD (21.11., Zuege): Gleisstrecke entlang einer gezeichneten Linie - Felder in 8er-Nachbarschaft (schraege Gleise erlaubt), um
/// Hindernisse herum, vorhandene eigene Gleise werden mitbenutzt (nicht neu gebaut). Linienbau-Eintraege (gerade oder schraege Stuecke).
std::vector<Placement> planRailLine(unsigned player, const std::vector<Vector2i> &strokeWorld);

/// MOD (Wunsch 24.11. 15:33 "Die Trenches sollen sich mit den ueberdachten Trenches ab und zu bei ein paar Stuecken abwechseln (mal 1, 2
/// oder 3)"): beim Linienbau eines offenen Grabens (A0BaBaTrench / A0BaBaTrenchDeep) wird an dieser Stelle ab und zu die ueberdachte Fassung
/// gebaut - in etwa jedem Abschnitt von 8 Feldern ein Stueck aus 1 bis 3 ueberdachten Feldern. Nur aus der Lage (kein Zufall, auf allen
/// Rechnern gleich). Gibt st zurueck, wenn es kein offener Graben ist oder die ueberdachte Fassung nicht verfuegbar ist.
STRUCTURE_STATS *trenchVariant(STRUCTURE_STATS *st, unsigned player, const Vector2i &worldPos);

/// jeden Spielschritt
void tick(GameWorld &world);
/// (Wunsch 22.11. 21:19 "Generals-Baubefehl 'restore' - Kreis zeichnen, in dem alle Krater wiederhergestellt werden und das Terrain wieder
/// auf Missionsbeginn ... (mit Ausnahme von Tunneln und Durchbruechen)"): diese Bauer schuetten gemeinsam alle Krater und Loecher in den
/// Feldern zu (naechstes zuerst, jeder ein eigenes); zugeschuettet ist die Kachel wieder wie zu Missionsbeginn (Originaltextur, eben)
void startRestore(const std::vector<DROID *> &builders, const std::vector<Vector2i> &tiles);
/// bei neuem Spiel
void reset();
}
