// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 01:35): Verbundrumpfe ("Ueber-Rumpfe") aus dem Rumpf-Baukasten.
// Ein Verbundrumpf besteht aus mehreren vorhandenen Rumpfen (Teile), jeder frei verschoben und gedreht (Durchdringen erlaubt), und frei
// gesetzten Waffenplaetzen. Er wird als ganz normaler Rumpf (BODY_STATS) angemeldet: Werte zusammengerechnet, Modell aus den Teilen
// erzeugt (PIE 4 mit eigener Textur je Ebene), Laufwerke wie beim ersten Teil (Fahrgestell, folgen dessen Lage).
// Bauplaene: Vorlagen im Mod-Paket (composites/*.json) und eigene im Benutzerordner (userdata/composites/*.json, haben Vorrang).
#pragma once

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/framework/wzstring.h"
#include <glm/mat4x4.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct COMPONENT_STATS;
struct BODY_STATS;
struct BASE_OBJECT;
struct iIMDShape;
struct iIMDBaseShape;

namespace modcompbody
{
/// (23.11. 23:24) einer der 14 Fraktionsruempfe von Warzone 2100 (Viper ... Dragon)?
bool isWzFactionBody(const std::string &id);


/// Waffenplaetze je Verbundrumpf (Wunsch 23.11. 02:16: bis 30 - Turm-Plaetze-Leiste im Design-Bildschirm, MAX_WEAPONS)
constexpr unsigned MAX_SLOTS = 30;

// ---------------------------------------------------------------- (Wunsch 27.11. 20:38/20:56 "A+B") Animation ohne Skelett
// Jedes Teil haengt an einem anderen Teil (parent) und dreht sich um seinen Drehpunkt (pivot) - ein unsichtbares Skelett aus starren Teilen.
// B: Bewegungs-Bausteine je Teil (Bein, Drehen, Wippen, Pumpen, Pendeln); A: Posen je Ablauf (Fahren, Stehen, Feuern), weich ineinander
// uebergeblendet. Alles nur Anzeige - Werte und Spiellogik bleiben unberuehrt.
enum AnimClip { CLIP_MOVING = 0, CLIP_STANDING, CLIP_FIRING, CLIP_COUNT };
enum MotionType { MOT_NONE = 0, MOT_LEG, MOT_SPIN, MOT_BOB, MOT_PUMP, MOT_SWAY, MOT_COUNT };
enum MotionWhen { WHEN_ALWAYS = 0, WHEN_MOVING, WHEN_STANDING, WHEN_FIRING, WHEN_COUNT };
constexpr int MAX_POSES = 8;
struct Motion
{
	int type = MOT_NONE;
	int axis = 0;            ///< 0 = x (quer: nicken), 1 = y (hoch: drehen), 2 = z (laengs: rollen) - bei Wippen/Pumpen die Schieberichtung
	int when = WHEN_MOVING;  ///< MotionWhen
	float amount = 0.f;      ///< Ausschlag in Grad (Bein/Pendeln) bzw. Weg in Modell-Einheiten (Wippen/Pumpen); Drehen: unbenutzt
	float speed = 1.f;       ///< Takte je Sekunde (beim Fahren: je 128 Einheiten Weg); Drehen: Umdrehungen
	float phase = 0.f;       ///< Versatz im Takt (0..1) - z.B. 0,5 fuer das gegenueberliegende Bein
};
struct PoseKey
{
	glm::vec3 rot = glm::vec3(0.f);    ///< Grad wie Part::rot: Gieren (um y), Nicken (um x), Rollen (um z) - um den Drehpunkt
	glm::vec3 pos = glm::vec3(0.f);    ///< Verschiebung (Modellachsen)
};

struct Part
{
	std::string body;                 ///< ID des Teil-Rumpfes
	glm::vec3 pos = glm::vec3(0.f);    ///< Lage (x, y = oben, z = hinten; vorn ist -z)
	glm::vec3 rot = glm::vec3(0.f);    ///< Drehung in Grad: Gieren (um y), Nicken (um x), Rollen (um z)
	float scale = 1.f;
	int flip = 0;                      ///< Spiegelung der Teil-Geometrie: Bit 0 = x, Bit 1 = y, Bit 2 = z (Wunsch 23.11. 02:16 "Flip")
	/// (Wunsch 24.11. 15:33 "die Ruempfe dicker, duenner, laenger, breiter machen - das Mesh an sich"): Streckung je Achse im Teil-Raum
	/// (x = Breite, y = Hoehe/Dicke, z = Laenge), zusaetzlich zu scale
	glm::vec3 stretch = glm::vec3(1.f);
	/// (Wunsch 27.11. 19:07 "die Moeglichkeit, die Textur von einem anderen Body zu nehmen"): ID des Rumpfes, dessen Farben das Teil traegt
	/// ((27.11. 23:58) UV-Uebertragung: das Teil zeigt Textur, Muster und Teamfarbe des anderen Rumpfes an den passenden Stellen); leer = eigene Textur
	std::string texFrom;
	// (27.11.) Animation
	int parent = -1;                   ///< Teil, an dem dieses haengt (-1 = am Rumpf selbst)
	glm::vec3 pivot = glm::vec3(0.f);  ///< Drehpunkt im Raum des Teils (wandert mit Verschieben/Drehen/Groesse des Teils mit)
	Motion motion;
	std::vector<PoseKey> keys[CLIP_COUNT];   ///< Posen je Ablauf (fehlende = Ruhelage)
	/// (Wunsch 27.11. 21:26 "einen Rumpf der Laenge oder Breite nach (oder einen bestimmten Winkel einstellend) entzwei schneiden"):
	/// 0 = ganz, 1 = laengs (Schnitt entlang der Laengsachse: linke/rechte Haelfte), 2 = quer (vorn/hinten), 3 = waagerecht (oben/unten).
	/// Die Schnittflaeche wird geschlossen.
	int cut = 0;
	float cutAngle = 0.f;              ///< Grad: laengs/quer um die Hochachse gedreht, waagerecht nach vorn geneigt
	float cutPos = 0.f;                ///< Versatz der Schnittebene von der Mitte (Teil-Raum)
	bool cutFlip = false;              ///< die andere Haelfte behalten
	int group = 0;                     ///< (Wunsch 27.11. 21:27 "Parts joinen wie in Blender") Verbund-Nummer; 0 = einzeln
};

struct Mount
{
	glm::vec3 pos = glm::vec3(0.f);    ///< Waffenplatz (Modellkoordinaten)
	float yaw = 0.f;                   ///< Blickrichtung in Grad (0 = vorn, 90 = rechts; Wunsch 23.11. 03:05: Waffen schwenken nur 180 Grad um sie)
	// (Wunsch 27.11. 18:48 "Auch Weapon Slots sollen in jegliche Richtung rotieren koennen"): Neigung um die Querachse (Nase, wie bei den
	// Teilen) und um die Laengsachse (Rollen) des Platzes in Grad - die Waffe sitzt schraeg/seitlich/kopfueber und schwenkt um die geneigte Achse
	float pitch = 0.f;
	float roll = 0.f;
	int part = -1;                     ///< (27.11.) Teil, an dem der Platz sitzt (bewegt sich mit dessen Animation); -1 = unbekannt
};

/// (Wunsch 27.11. 19:16 "um so eine Einheit wie den Fatboy zu machen, braeuchten wir mehrere Propulsion-Slots ... bis zu 8"): Antriebsplaetze
constexpr unsigned MAX_DRIVES = 8;
struct Drive
{
	glm::vec3 pos = glm::vec3(0.f);    ///< Lage des Laufwerks (Modellkoordinaten; y = Unterkante des Laufwerk-Modells, 0 = Boden)
	float yaw = 0.f;                   ///< Drehung um die Hochachse in Grad (0 = vorn)
	float scale = 1.f;                 ///< Groesse
	std::string body;                  ///< Rumpf, dessen Laufwerk-Modelle (Groesse) es traegt; leer = Fahrgestell (Teil 0)
};

/// Schwenkbereich der Waffen auf Verbundrumpfen: halber Winkel um die Blickrichtung des Platzes (Wunsch 23.11. 03:05: "maximal 180 Grad")
constexpr float ARC_HALF_DEG = 90.f;

struct Def
{
	std::string id;                   ///< Rumpf-ID ("CompBody_...")
	std::string name;                 ///< Anzeigename
	std::vector<Part> parts;          ///< Teil 0 = Fahrgestell (Laufwerke, Klasse)
	std::vector<Mount> mounts;        ///< Waffenplaetze (bis MAX_SLOTS) - jeder ein Turm-Platz fuer eine Waffe oder einen Systemturm
	/// (27.11.) Antriebsplaetze (bis MAX_DRIVES): mit ihnen zeichnet das Spiel statt des einen Laufwerks unter dem Fahrgestell je Platz ein
	/// Laufwerk (eigener Antrieb je Platz moeglich: modslots driveProp); leer = wie bisher
	std::vector<Drive> drives;
	int poseCount[CLIP_COUNT] = { 0, 0, 0 };          ///< (27.11.) Posen je Ablauf
	float poseSeconds[CLIP_COUNT] = { 1.f, 3.f, 0.6f };   ///< (27.11.) Dauer eines Durchlaufs (Fahren: je 128 Einheiten Weg)
	bool preset = false;              ///< Vorlage aus dem Mod-Paket (noch nicht im Benutzerordner)
	bool deleted = false;             ///< geloescht: bleibt fuer alte Spielstaende angemeldet, aber nicht mehr baubar
};

struct Totals
{
	unsigned hp = 0, armour = 0, thermal = 0, weight = 0, power = 0, cost = 0, buildPoints = 0;
	int size = 0;                     ///< BODY_SIZE
	unsigned slots = 0;
};

// ---------------------------------------------------------------- Stats-Laden (stats.cpp)
/// Bauplaene von Platte lesen und die Zahl der zusaetzlichen Rumpf-Plaetze liefern (gespeicherte + freie fuer neue Entwuerfe)
size_t prepare();
/// nach dem Laden aller normalen Rumpfe (samt Laufwerk-Modellen): die Plaetze first .. first+count-1 fuellen.
/// lookup(stats, alteID) traegt die (neue) ID in die Such-Tabellen der Stats ein.
void registerAll(size_t first, size_t count, const std::function<void(COMPONENT_STATS *, const WzString &)> &lookup);
/// beim Entladen der Stats
void shutdown();

// ---------------------------------------------------------------- Laufzeit
bool isComposite(size_t bodyIdx);
/// Lage des Fahrgestells (Teil 0) fuer die Laufwerk-Modelle; nullptr = keine Verschiebung
const glm::mat4 *propOffset(size_t bodyIdx);
/// (24.11.) so weit steht der Rumpf (mit Tuermen) ueber dem Laufwerk, wenn Teile tiefer liegen als das Fahrgestell; 0 = gar nicht
float propLift(size_t bodyIdx);
/// Blickrichtung des Platzes (Anschluss) eines Verbundrumpfes in Grad; false = kein Verbundrumpf (Waffe dreht frei)
bool mountYaw(size_t bodyIdx, int connector, float &yawDeg);
/// (27.11.) Neigung des Platzes (Anschluss) eines Verbundrumpfes; false = kein Verbundrumpf oder nicht geneigt
bool mountTilt(size_t bodyIdx, int connector, float &yawDeg, float &pitchDeg, float &rollDeg);
/// (27.11.) Drehung der Neigung als Matrix (Zeichnen: nach dem Verschieben zum Anschluss, vor dem Schwenken der Waffe)
glm::mat4 tiltMatrix(float yawDeg, float pitchDeg, float rollDeg);
/// (27.11.) Zielrichtung der Waffe (Schwenk/Hoehe relativ zum Rumpf, 16-Bit-Winkel) in die Achsen des geneigten Platzes umrechnen, damit das
/// Rohr auf einem geneigten Platz trotzdem genau aufs Ziel zeigt; false = Platz nicht geneigt (Winkel unveraendert)
bool tiltedAim(size_t bodyIdx, int connector, uint16_t &dir, uint16_t &pitch);
/// (27.11.) Antriebsplaetze eines Verbundrumpfes (0 = keine: ein Laufwerk wie bisher)
size_t driveCount(size_t bodyIdx);
/// (27.11.) Antriebsplatz i: Lage (Modellkoordinaten des Rumpfes) und der Rumpf, dessen Laufwerk-Modelle er traegt (asBodyStats-Index)
bool driveInfo(size_t bodyIdx, size_t i, glm::mat4 &m, size_t &sizeBodyIdx);
/// (27.11.) Lage eines Antriebsplatzes als Matrix
glm::mat4 driveMatrix(const Drive &dr);
/// Waffe weaponSlot eines Fahrzeugs auf einem Verbundrumpf (kein VTOL): Blickrichtung ihres Platzes als 16-Bit-Winkel (relativ zum Rumpf).
/// false = Waffe dreht frei wie bisher
bool weaponArc(const BASE_OBJECT *psObj, int weaponSlot, uint16_t &yaw);
/// Verfuegbarkeit fuer einen Spieler: baubar, wenn alle Teil-Rumpfe erforscht sind (nicht in Netzwerkspielen)
void refreshAvailability(unsigned player);

// ---------------------------------------------------------------- Rumpf-Katalog (Wunsch 23.11. 17:55)
/// "Das soll ein reines Menue-UI-Feature sein ... in der Hull Forge SAEMTLICHE Rumpfe verfuegbar machen. Und schoen nach Fraktionen und
/// auch Mods trennen": alle Rumpfe, die als Teil taugen - im Spiel aus den geladenen Stats, im Hauptmenue aus stats/body.json.
struct BodyInfo
{
	std::string id, name;
	const iIMDBaseShape *model = nullptr;   ///< Modell (nullptr = keins)
	unsigned hp = 0, armour = 0, thermal = 0, weight = 0, power = 0, cost = 0, buildPoints = 0, weaponSlots = 1;
	int size = 0;                           ///< BODY_SIZE
	bool designable = false;
	std::string source;                     ///< Herkunft: "Warzone 2100", "Scavengers", "WZ2120", "Metal Fatigue", ...
	std::string group;                      ///< Fraktion/Gruppe innerhalb der Herkunft
	int sourceRank = 0, groupRank = 0;
	size_t statIdx = SIZE_MAX;              ///< im Spiel: Platz in asBodyStats
	const iIMDBaseShape *driveModel = nullptr;   ///< (27.11.) Laufwerk fuer die Vorschau der Antriebsplaetze (Ketten, sonst Halbketten/Raeder/Hover)
	bool listed = true;                     ///< in der Liste (Rumpfe mit demselben Modell nur einmal)
};
/// alle Teil-Rumpfe, sortiert nach Herkunft, Gruppe, Groesse, Name
const std::vector<BodyInfo> &catalog();
/// Rumpf zu einer ID (auch nicht gelistete Dubletten); nullptr = unbekannt
const BodyInfo *bodyInfo(const std::string &id);
/// laufendes Spiel (Stats geladen) - sonst Hauptmenue
bool inGame();
/// Bauplaene neu von Platte lesen (im Hauptmenue vollstaendig; im Spiel nur neu hinzugekommene - Wunsch 17:55: "wenn ich eine Hull in
/// einer Mission speichere, soll sie auch im Fleet-Designer - Hull Forge sein. Und umgekehrt")
void refreshDefs();
/// Modell eines Verbundrumpfes fuer die Vorschau im Hauptmenue (Einheiten-Designer) anlegen; Rueckgabe: Modellname ("" = keins)
std::string previewModel(const Def &d);
/// Groesse als Stats-Text ("LIGHT", "MEDIUM", "HEAVY", "SUPER HEAVY")
const char *sizeText(int size);

// ---------------------------------------------------------------- Rumpf-Baukasten
const std::vector<Def> &defs();
const Def *find(const std::string &id);
glm::mat4 partMatrix(const Part &p, bool withFlip = true);
/// Hub des Rumpfes ueber dem Laufwerk (unterstes Teil sitzt auf dem Laufwerk wie sonst das Fahrgestell), siehe propLift
float liftOf(const Def &d);
/// (Wunsch 28.11. 17:34 "Own Legs"): hat der Rumpf eine Lauf-Animation (Posen beim Fahren oder ein Baustein, der beim Fahren laeuft)?
bool hasWalkAnim(const Def &d);
/// (28.11.) Hub fuer den Antrieb "Own Legs": der tiefste Punkt des Rumpfes steht auf dem Boden
float legLiftOf(const Def &d);
float legLift(size_t bodyIdx);
bool walks(size_t bodyIdx);
/// (28.11.) Teil je Ebene des Vorschau-Modells (previewModel) - fuer die animierte Vorschau im Einheiten-Designer
std::vector<int> previewLevelParts(const Def &d);
/// gespiegelte und/oder in fremder Textur gezeigte Fassung eines Rumpf-Modells fuer die Vorschau im Baukasten (Umlaufsinn richtig); nullptr = keine
/// (zeichnen mit partMatrix(p, false))
const iIMDShape *bakedShape(const std::string &bodyId, int flipMask, const std::string &texFrom);
/// (27.11.) dasselbe fuer ein Teil samt Schnitt
const iIMDShape *bakedShape(const Part &p);
/// (27.11.) Schnittebene eines Teils im Raum des Teils: Normale n (zeigt auf die bleibende Haelfte), Punkt c; false = nicht geschnitten
bool cutPlane(const Part &p, glm::vec3 &n, glm::vec3 &c);
Totals totals(const Def &d);
/// speichern (Datei im Benutzerordner) und - im Spiel - sofort als Rumpf anmelden bzw. aktualisieren; d.id wird bei neuen Entwuerfen vergeben
bool save(Def &d, std::string &err);
/// loeschen (Datei weg, Rumpf nicht mehr baubar)
bool remove(const std::string &id, std::string &err);
/// (Wunsch 27.11. 23:19 "alle Animationen, auch ganze Animationssets, in einer aufploppenden Liste"): eigene Saetze im Benutzerordner
/// (userdata/animsets/<name>.json) - die ganze Animation eines Rumpfes (je Teil: haengt an, Drehpunkt, Baustein, Posen), Teile wie beim Speichern
struct AnimSetInfo { std::string name, path; size_t parts = 0; };
std::vector<AnimSetInfo> animSets();
bool saveAnimSet(const Def &d, const std::string &name, std::string &err);
bool loadAnimSet(const std::string &path, Def &out);
bool deleteAnimSet(const std::string &path);
/// (Wunsch 27.11. 19:16 "das Modell in der Hull Forge sollte am Ende zentriert werden, dass es fuer den Unit Designer passt"): Grundriss
/// der Teile in die Mitte schieben (Teile, Waffen- und Antriebsplaetze gemeinsam, nur waagerecht); true = verschoben
bool centerDef(Def &d);
/// Standard-Waffenplaetze aus den Anschluessen des Fahrgestells
std::vector<Mount> defaultMounts(const Part &chassis);

// ---------------------------------------------------------------- (27.11.) Animation
/// Zustand einer Einheit fuer die Animation (Sekunden bzw. Takte)
struct AnimCtx
{
	float time = 0.f;        ///< laufende Zeit
	float moveW = 0.f;       ///< 0 = steht, 1 = faehrt
	float moveClock = 0.f;   ///< gefahrener Weg / 128
	float standClock = 0.f;  ///< Zeit im Stand
	float fireW = 0.f;       ///< 1 kurz nach einem Schuss, klingt ab
	float fireClock = 0.f;   ///< Zeit beim Feuern (laeuft nur waehrend fireW > 0)
	float fireAge = 1e9f;    ///< Sekunden seit dem letzten Schuss
};
bool hasAnim(const Def &d);
/// Drehpunkt des Teils im Modellraum (Ruhelage)
glm::vec3 pivotOf(const Part &p);
/// Drehpunkt-Vorgabe im Raum des Teils: 0 = unten Mitte, 1 = Mitte, 2 = oben Mitte (Mitte des Grundrisses)
glm::vec3 pivotPreset(const Part &p, int where);
/// Bewegungs-Baustein mit sinnvollen Vorgaben
Motion defaultMotion(int type);
const char *motionName(int type);
const char *whenName(int when);
/// Lage jedes Teils (Modellraum: Ruhelage -> animiert) fuer den Zustand c
void evalAnim(const Def &d, const AnimCtx &c, std::vector<glm::mat4> &partW);
/// im Spiel: animierter Verbundrumpf? partW je Teil, dazu Teil je Modell-Ebene und je Anschluss
bool animated(size_t bodyIdx);
bool animate(size_t bodyIdx, const AnimCtx &c, std::vector<glm::mat4> &partW);
int levelPart(size_t bodyIdx, size_t level);
int mountPart(size_t bodyIdx, size_t connector);

}
