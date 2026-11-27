// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 01:35): Rumpf-Baukasten (siehe modhullforge.h)
//
// (Wunsch 23.11. 04:23) WZ-Stil, Maus-Kamera, Rechtsklick loescht, gelbe WZ-Waffenplaetze, natuerliche Plaetze je Rumpf.
// (Wunsch 23.11. 17:55) Ueberarbeitet:
//  - "Das soll ein reines Menue-UI-Feature sein und immer und ueberall greifbar, speicherbar (in real time) sein ... wenn ich eine Hull in
//    einer Mission speichere, soll sie auch im Fleet-Designer - Hull Forge sein. Und umgekehrt": der Baukasten laeuft im Hauptmenue ohne
//    Spiel (Flotten-Menue / Einheiten-Designer) und im Spiel (Design-Bildschirm). Beide lesen und schreiben dieselben Bauplaene
//    (userdata/composites); im Spiel ist ein gespeicherter Rumpf sofort baubar, sobald alle erforschbaren Teile erforscht sind.
//  - "In der Hull Forge SAEMTLICHE Rumpfe verfuegbar machen. Und schoen nach Fraktionen und auch Mods trennen": Reiter je Herkunft
//    (Warzone 2100, Scavengers, WZ2120, Metal Fatigue, ...) mit 3D-Bild, darunter die Rumpfe als 3D-Knoepfe in Fraktions-Gruppen.
//    Im Spiel zeigt ein Schloss, was noch nicht erforscht ist (bauen erst danach).
//  - "Die Mauszeiger-Ansicht invertieren (sie ist falsch herum)": waagerechtes Ziehen dreht jetzt wie Anfassen (der Rumpf folgt der Maus).
//  - "Jeden Button mit einem WZ-Stil schnell und einfach verstaendlichen Symbol/Icon versehen ... kindlich, gross": jeder Knopf zeigt ein
//    grosses Sinnbild (images/modforge.img, Paket forgeicons.py) im Stil der WZ-Designer-Bilder, darunter ein kurzes Wort.
//  - kurze Tooltips (Wunsch 17:59).
//
// Aufbau (virtuelles 640x480-Bild, auf den Bildschirm skaliert):
//   links   Rumpfe: Herkunfts-Reiter, darunter 3D-Knoepfe nach Fraktion (Klick = als neues Teil anbauen)
//   Mitte   3D-Ansicht des Verbundrumpfes
//   rechts  Teile-Liste (Teil 1 = Fahrgestell), Waffenplaetze, Werte
//   unten   Auswahl-Zeile, Gruppen VERSCHIEBEN / DREHEN / TEIL, UEBER-RUMPF (Name, Neu/Laden/Speichern/Loeschen), ANSICHT
// Tasten (nicht beim Schreiben des Namens): Pfeile = x / vor-zurueck, Bild hoch/runter = Hoehe, Q/E = Gieren, R/F = Nicken,
// Z/C = Rollen, Umschalt = feinste Stufe, Tab = naechstes Teil, Entf = Teil/Platz entfernen.

#include "modhullforge.h"
#include "modcompbody.h"
#include "modanimlib.h"
#include "modpanel.h"
#include "modlayer.h"
#include "modui.h"
#include "modwzhd.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/physfs_ext.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/bitimage.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/ivis_opengl/textdraw.h"
#include "lib/sound/audio.h"
#include "lib/framework/frameresource.h"
#include "lib/sound/mixer.h"
#include "lib/widget/button.h"
#include "lib/widget/editbox.h"
#include "lib/widget/form.h"
#include "lib/widget/widget.h"

#include "basedef.h"
#include "component.h"
#include "console.h"
#include "hci.h"
#include "design.h"
#include "intimage.h"
#include "stats.h"
#include "statsdef.h"

#include <glm/gtc/matrix_transform.hpp>
#include <set>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace modhullforge
{

namespace
{

using modcompbody::BodyInfo;
using modcompbody::Def;
using modcompbody::Mount;
using modcompbody::Part;

constexpr int PART_ROWS = 8;
constexpr int MOUNT_ROWS = 8;
constexpr float LIST_ROW_H = 13.f;
constexpr float BAY_DEPTH = 1200.f;
const float STEPS[3] = { 0.5f, 2.f, 8.f };
const float ANGLES[3] = { 5.f, 15.f, 90.f };

// Bereiche (virtuelle Einheiten)
constexpr float BAY_X = 166, BAY_Y = 6, BAY_W = 308, BAY_H = 330;
constexpr float CEN_W = 50, CEN_H = 14, CEN_X = BAY_X + BAY_W - CEN_W - 6, CEN_Y = BAY_Y + 6;   // (Wunsch 27.11. 20:38) schneller Zentrier-Knopf
constexpr float SUB_Y = 3, SUB_H = 12, SUB_X0 = 58, SUB_W0 = 46, SUB_X1 = 106, SUB_W1 = 54;   // SupCom-Unterreiter in der HULLS-Kopfzeile
constexpr float PARTS_Y = 20, SLOTS_HEAD_Y = 134, SLOTS_Y = 148, VALUES_Y = 282;
constexpr float ROW_A = 350, ROW_B = 418;
// Rumpf-Liste links
constexpr float TAB_X = 6, TAB_Y = 16, TAB_W = 37, TAB_H = 30, TAB_STEP_X = 39, TAB_STEP_Y = 32;
constexpr int TAB_COLS = 4, TAB_MAX = 12;   // (23.11. 23:24) drei Reihen: WZ-Fraktionen, Transporter, Scavs, Mods ...
constexpr float LIST_X = 6, LIST_Y = 114, LIST_W = 154, LIST_H = 200;
constexpr float CELL_W = 76, CELL_H = 50, HEAD_H = 13;

// Sinnbilder (Reihenfolge = forgeicons.py)
enum ForgeIcon
{
	FI_LEFT = 0, FI_RIGHT, FI_DOWN, FI_UP, FI_FWD, FI_BACK,
	FI_YAW_L, FI_YAW_R, FI_PITCH_DOWN, FI_PITCH_UP, FI_ROLL_L, FI_ROLL_R,
	FI_COPY, FI_MIRROR, FI_REMOVE, FI_RESET, FI_FLIP_X, FI_FLIP_Y, FI_FLIP_Z,
	FI_NEW, FI_LOAD_PREV, FI_LOAD_NEXT, FI_SAVE, FI_DELETE,
	FI_VIEW_FRONT, FI_VIEW_SIDE, FI_VIEW_TOP, FI_AUTO, FI_VIEW_RESET, FI_CLOSE,
	FI_SLOT_ADD, FI_SLOT_REMOVE, FI_PARTS, FI_SCROLL_UP, FI_SCROLL_DOWN,
	FI_STEP_S, FI_STEP_M, FI_STEP_L, FI_ANG_S, FI_ANG_M, FI_ANG_L,
	FI_LOCK,
	FI_COUNT
};

struct Ctx
{
	float S = 1.f;
	int ox = 0, oy = 0;
	int X(float v) const { return ox + static_cast<int>(std::lround(v * S)); }
	int Y(float v) const { return oy + static_cast<int>(std::lround(v * S)); }
	int L(float v) const { return static_cast<int>(std::lround(v * S)); }
};

Ctx ctx()
{
	Ctx c;
	const float sx = static_cast<float>(screenWidth) / 640.f, sy = static_cast<float>(screenHeight) / 480.f;
	c.S = std::max(0.5f, std::floor(std::min(sx, sy) * 4.f) / 4.f);
	c.ox = (static_cast<int>(screenWidth) - static_cast<int>(std::lround(640.f * c.S))) / 2;
	c.oy = (static_cast<int>(screenHeight) - static_cast<int>(std::lround(480.f * c.S))) / 2;
	return c;
}

// ---------------------------------------------------------------- WZ-Schrift (zwischengespeichert)
enum TextKind { TK_ROW = 0, TK_BUTTON, TK_HEAD, TK_SMALL };

iV_fonts fontFor(TextKind k, float S)
{
	if (k == TK_SMALL) { return S < 2.25f ? font_small : font_regular; }
	if (S < 1.5f) { return k == TK_HEAD ? font_regular_bold : font_small; }
	if (S < 2.25f) { return k == TK_HEAD ? font_regular_bold : font_regular; }
	return k == TK_HEAD ? font_large : font_medium;
}

std::map<std::string, WzText> s_texts;

WzText &cachedText(const std::string &s, iV_fonts f)
{
	const std::string key = std::to_string(static_cast<int>(f)) + "|" + s;
	auto it = s_texts.find(key);
	if (it != s_texts.end()) { return it->second; }
	if (s_texts.size() > 800) { s_texts.clear(); }
	WzText &t = s_texts[key];
	t.setText(WzString::fromUtf8(s), f);
	return t;
}

int textW(const std::string &s, TextKind k, float S)
{
	return s.empty() ? 0 : cachedText(s, fontFor(k, S)).width();
}

/// Text mit Oberkante bei top (WZ-Schrift)
void wzText(const std::string &s, int x, int top, PIELIGHT col, TextKind k, float S, int maxW = -1)
{
	if (s.empty()) { return; }
	WzText &t = cachedText(s, fontFor(k, S));
	t.render(static_cast<float>(x), static_cast<float>(top - t.aboveBase()), col, 0.f, maxW);
}

/// Text auf eine Breite kuerzen ("...")
std::string fitW(const std::string &s, int maxW, TextKind k, float S)
{
	if (textW(s, k, S) <= maxW) { return s; }
	std::string t = s;
	while (!t.empty() && textW(t + "...", k, S) > maxW) { t.pop_back(); }
	return t + "...";
}

// ---------------------------------------------------------------- Sinnbilder
IMAGEFILE *s_icons = nullptr;
bool s_iconsTried = false;

bool iconsReady()
{
	if (!s_iconsTried)
	{
		s_iconsTried = true;
		if (PHYSFS_exists("images/modforge.img")) { s_icons = iV_LoadImageFile("images/modforge.img"); }
	}
	return s_icons != nullptr && s_icons->imageDefs.size() >= static_cast<size_t>(FI_COUNT);
}

void freeIcons()
{
	if (s_icons != nullptr) { iV_FreeImageFile(s_icons); s_icons = nullptr; }
	s_iconsTried = false;
}

void drawIcon(int icon, int x, int y, int size, unsigned char alpha = 255)
{
	if (icon < 0 || !iconsReady()) { return; }
	iV_DrawImageFileAnisotropic(s_icons, static_cast<UWORD>(icon), x, y, Vector2f(static_cast<float>(size), static_cast<float>(size)), defaultProjectionMatrix(), alpha);
}

// ---------------------------------------------------------------- Zustand
bool s_open = false;
bool s_menu = false;            // im Hauptmenue geoeffnet (kein Spiel)
std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_FULLSCREENOVERLAY_CLICKFORM> s_root;
std::shared_ptr<W_EDITBOX> s_nameBox;

struct Btn
{
	std::shared_ptr<W_BUTTON> w;
	float x = 0, y = 0, wdt = 0, hgt = 0;
	std::string label;
	int icon = -1;
	std::function<bool()> active;   // hervorgehoben (z.B. gewaehlte Schrittweite)
};
std::vector<Btn> s_btns;

/// Waffenplatz am Teil: Lage und Blickrichtung im Modellraum des Teils (nur im Editor; beim Laden wiedererkannt)
struct Link
{
	int part = -1;
	glm::vec3 local = glm::vec3(0.f);
	float localYaw = 0.f;
};

Def s_def;
std::vector<Link> s_links;      // parallel zu s_def.mounts
int s_sel = -1;                 // gewaehltes Teil
int s_selMount = -1;            // gewaehlter Waffenplatz (>= 0: Verschieben wirkt auf ihn)
int s_selDrive = -1;            // (27.11.) gewaehlter Antriebsplatz (>= 0: Verschieben/Drehen wirkt auf ihn)
bool s_drivePick = false;       // (27.11.) naechster Klick auf einen Rumpf links: Groesse (Laufwerk-Modelle) des gewaehlten Antriebsplatzes
// MOD (Wunsch 24.11. 15:33 "die Ruempfe dicker, duenner, laenger, breiter machen - in einem Unter-Fenster, das mit einem Knopf kommt,
// bitte das jetzige sehr gute UI nicht noch komplexer machen"): Form-Fenster ueber der Ansicht, Knopf "Form" in der Gruppe PART
bool s_shapeOpen = false;
bool s_shapeAll = false;        // ganzer Rumpf statt des gewaehlten Teils
int s_shapeStep = 1;            // 5 %, 10 %, 25 %
std::vector<size_t> s_shapeBtns; // Indizes der Knoepfe des Form-Fensters in s_btns
constexpr float SH_X = 172, SH_Y = 172, SH_W = 250, SH_H = 162;   // Form-Fenster (unten links in der Ansicht; (27.11.) hoeher: Schneiden)
// MOD (Wunsch 27.11. 19:07 "die Moeglichkeit, die Textur von einem anderen Body zu nehmen ... der Knopf soll aufgeraeumt sein und keinen
// zusaetzlichen Platz wegnehmen. Der Parts-Knopf rechts reagiert gar nicht - vielleicht koennen wir unter dem ein paar Funktionen
// verstecken"): der Knopf "Parts" oeffnet ein kleines Fenster ueber der Ansicht (wie "Form") - Teile bearbeiten (bisherige Wirkung)
// und Farben eines anderen Rumpfes; Platz fuer weitere Funktionen
bool s_partsOpen = false;
bool s_texAll = false;          // Farben fuer den ganzen Rumpf statt fuer das gewaehlte Teil
bool s_texPick = false;         // naechster Klick auf einen Rumpf links waehlt die Farben (statt ein Teil anzufuegen)
std::vector<size_t> s_partsBtns;
// (Wunsch 27.11. 20:56 "A+B") Animations-Fenster: Drehpunkt, "haengt an", Bewegungs-Bausteine, Posen
constexpr float AM_X = 262, AM_Y = 126, AM_W = 206, AM_H = 208;
constexpr float ANB_X = CEN_X, ANB_Y = CEN_Y + CEN_H + 3, ANB_W = CEN_W, ANB_H = CEN_H;   // Knopf "Animate" unter "Centre"
constexpr float ALB_Y = ANB_Y + ANB_H + 3;                                                      // (27.11. 23:38) "Anim list"
constexpr float JNB_Y = ALB_Y + ANB_H + 3, SPB_Y = JNB_Y + ANB_H + 3;                          // (27.11.) "Join", "Separate"
std::vector<int> s_multi;       // (Wunsch 27.11. 21:27) Mehrfach-Auswahl (Shift-Klick), enthaelt s_sel; Verbund = gespeicherte Auswahl
bool s_animOpen = false;
bool s_pivotEdit = false;       // Verschieben wirkt auf den Drehpunkt des Teils
bool s_parentPick = false;      // naechster Klick auf ein Teil: daran haengt das gewaehlte
bool s_poseEdit = false;        // Verschieben/Drehen wirkt auf die Pose (statt auf die Ruhelage)
int s_animClip = modcompbody::CLIP_MOVING;
int s_animPoseIdx = 0;
int s_animPlay = -1;            // Vorschau: -1 aus, sonst Ablauf (Fahren/Stehen/Feuern)
uint32_t s_animPlayStart = 0;
std::vector<size_t> s_animBtns;
size_t s_axisBtn = SIZE_MAX, s_whenBtn = SIZE_MAX;   // Beschriftung zeigt den aktuellen Wert
// (Wunsch 27.11. 23:19 "alle Animationen, auch ganze Animationssets, in einer aufploppenden Liste, die man gezielt raussuchen oder rausnehmen
// kann - wie die Waffenslots oder Bodyliste" / 23:38 "auf jeden Fall speichern"): Fenster "ANIMATIONS" ueber der Ansicht, Knopf "Anim list"
// unter "Animate". Reiter "On this hull" (was der Rumpf hat - Klick waehlt, Rechtsklick nimmt heraus) und "Library" (fertige Gangarten,
// einzelne Animationen, eigene Saetze - Klick legt sie auf den Rumpf). Das ANIMATE-Fenster bleibt wie es ist.
constexpr float AL_X = 172, AL_Y = 30, AL_W = 240, AL_H = 302;
constexpr int AL_ROWS = 17;
constexpr float AL_ROW_H = 13.f, AL_LIST_Y = AL_Y + 34;
bool s_alOpen = false;
int s_alTab = 0;                // 0 = auf dem Rumpf, 1 = Bibliothek
int s_alScroll = 0;
int s_alArmed = -1;             // eigener Satz: erster Rechtsklick merkt ihn, der zweite loescht
std::vector<size_t> s_alBtns, s_alHullBtns, s_alLibBtns;
bool s_alHasUndo = false;       // eine Stufe zurueck (letzte Aenderung aus der Liste)
modcompbody::Def s_alUndo;
std::vector<modcompbody::AnimSetInfo> s_alSets;   // eigene Saetze (beim Oeffnen und nach Aenderungen gelesen)
constexpr float PM_X = 262, PM_Y = 178, PM_W = 206, PM_H = 156;   // Parts-Fenster (unten rechts in der Ansicht; (27.11.) hoeher: Antriebsplaetze)
const float SHAPE_STEPS[3] = { 0.05f, 0.10f, 0.25f };
int s_step = 1, s_angle = 1;
float s_yaw = 215.f, s_pitch = 24.f, s_zoom = 1.f;
float s_panX = 0.f, s_panY = 0.f;   // Verschiebung der Ansicht (Bildpunkte)
bool s_autoRot = false;
int s_partScroll = 0;
int s_mountScroll = 0;
int s_loadIdx = -1;
std::string s_status;
bool s_statusBad = false;
unsigned s_statusUntil = 0;
bool s_closeArmed = false;      // ungespeichert: zweiter Klick auf "Schliessen" verwirft
unsigned s_closeArmedAt = 0;

// Rumpf-Liste: Herkunfts-Reiter und Eintraege (Ueberschrift oder Zeile mit bis zu zwei Rumpfen)
std::vector<std::string> s_sources;
std::vector<size_t> s_sourceRep;     // Katalog-Index des Rumpfes fuer das Reiter-Bild
std::vector<int> s_sourceCount;
int s_source = 0;
struct ListItem { bool heading = false; std::string text; int a = -1, b = -1; };
std::vector<ListItem> s_items;
int s_listScroll = 0;
int s_listPress = -1;                // Katalog-Index unter dem Mausklick (Druecken)
size_t s_catSize = 0;

// Maus in der 3D-Ansicht
enum DragMode { DRAG_NONE = 0, DRAG_TURN, DRAG_PAN };
DragMode s_drag = DRAG_NONE;
int s_dragBtn = -1;              // 0 links, 1 rechts, 2 Mitte
int s_pressX = 0, s_pressY = 0, s_lastX = 0, s_lastY = 0;
bool s_moved = false;
bool s_dirty = false;
unsigned s_availTick = 0;
int s_lastW = 0, s_lastH = 0;
int s_pendingTip = -1;           // Katalog-Index des Rumpf-Knopfes unter der Maus (Hinweis-Kasten)

void play(const char *file)
{
	// MOD (Log 24.11. 00:55 "Unable to find data for mfui_clank.ogg type WAV"): im Titelmenue sind die Klaenge aus audio.wrf noch nicht
	// geladen (die kommen erst mit den Spieldaten) -> dann die Datei aus audio/mf direkt abspielen statt stumm zu bleiben (und ohne Assert)
	if (resPresent("WAV", file))
	{
		const int id = audio_GetTrackID(file);
		if (id >= 0) { audio_PlayTrack(id); }
		return;
	}
	const std::string path = std::string("audio/mf/") + file;
	if (PHYSFS_exists(path.c_str())) { audio_PlayStream(path.c_str(), sound_GetUIVolume(), nullptr, nullptr); }
}

void setStatus(const std::string &t, bool bad = false)
{
	s_status = t;
	s_statusBad = bad;
	s_statusUntil = realTime + 6000;
}

const char *sizeName(int s)
{
	switch (s)
	{
	case SIZE_LIGHT: return "Light";
	case SIZE_MEDIUM: return "Medium";
	case SIZE_HEAVY: return "Heavy";
	default: return "Super heavy";
	}
}

std::string bodyName(const std::string &id)
{
	const BodyInfo *b = modcompbody::bodyInfo(id);
	return (b == nullptr || b->name.empty()) ? id : b->name;
}

int teamColour()
{
	return s_menu || selectedPlayer >= MAX_PLAYERS ? 0 : static_cast<int>(getPlayerColour(selectedPlayer));
}

/// im Spiel: noch nicht erforscht (erforschbar, aber nicht verfuegbar)
bool locked(const BodyInfo &b)
{
	// (23.11. 23:24) die WZ-Fraktionsruempfe sind im Paket nicht "designable", haengen im Spiel aber trotzdem an der Forschung
	if (s_menu || !modcompbody::inGame() || b.statIdx == SIZE_MAX || (!b.designable && !modcompbody::isWzFactionBody(b.id))) { return false; }
	if (selectedPlayer >= MAX_PLAYERS || apCompLists[selectedPlayer][COMP_BODY] == nullptr) { return false; }
	return apCompLists[selectedPlayer][COMP_BODY][b.statIdx] != AVAILABLE;
}

const char *shortSource(const std::string &s)
{
	if (s == "Warzone 2100") { return "WZ Transp."; }
	if (s == "New Paradigm") { return "NP"; }
	if (s == "Collective") { return "Kollektiv"; }
	if (s == "Project") { return "Projekt"; }
	if (s == "Scavengers") { return "Scavs"; }
	if (s == "Metal Fatigue") { return "MF"; }
	if (s == "Supreme Commander") { return "SupCom"; }
	return s.c_str();
}

/// (Wunsch 27.11. 20:38 "SupCom-Gebaeude ... durch Unterreiter abgrenzen von den Einheiten"): 0 = Units, 1 = Buildings
int s_scSub = 0;
bool hasSubTabs(const std::string &src) { return src == "Supreme Commander"; }
bool isBuildingGroup(const std::string &g)
{
	auto ends = [&g](const char *t) { const size_t n = std::strlen(t); return g.size() >= n && g.compare(g.size() - n, n, t) == 0; };
	return ends(" - Buildings") || ends(" - Civilian");
}
bool subTabsShown() { return s_source >= 0 && s_source < static_cast<int>(s_sources.size()) && hasSubTabs(s_sources[static_cast<size_t>(s_source)]); }

void rebuildItems()
{
	const auto &cat = modcompbody::catalog();
	s_catSize = cat.size();
	s_items.clear();
	if (s_source < 0 || s_source >= static_cast<int>(s_sources.size())) { s_listScroll = 0; return; }
	const std::string &src = s_sources[static_cast<size_t>(s_source)];
	std::string group;
	bool open = false;   // Zeile mit einem freien Platz
	const bool sub = hasSubTabs(src);
	for (size_t i = 0; i < cat.size(); ++i)
	{
		const BodyInfo &b = cat[i];
		if (!b.listed || b.source != src) { continue; }
		if (sub && isBuildingGroup(b.group) != (s_scSub == 1)) { continue; }   // (27.11.) SupCom: Einheiten | Gebaeude
		if (b.group != group || s_items.empty())
		{
			group = b.group;
			ListItem h; h.heading = true; h.text = group;
			s_items.push_back(h);
			open = false;
		}
		if (open) { s_items.back().b = static_cast<int>(i); open = false; }
		else { ListItem r; r.a = static_cast<int>(i); s_items.push_back(r); open = true; }
	}
	s_listScroll = 0;
}

void rebuildSources()
{
	const auto &cat = modcompbody::catalog();
	const std::string keep = (s_source >= 0 && s_source < static_cast<int>(s_sources.size())) ? s_sources[static_cast<size_t>(s_source)] : std::string();
	s_sources.clear(); s_sourceRep.clear(); s_sourceCount.clear();
	for (size_t i = 0; i < cat.size(); ++i)
	{
		if (!cat[i].listed) { continue; }
		auto it = std::find(s_sources.begin(), s_sources.end(), cat[i].source);
		if (it == s_sources.end())
		{
			if (s_sources.size() >= static_cast<size_t>(TAB_MAX)) { continue; }
			s_sources.push_back(cat[i].source); s_sourceRep.push_back(i); s_sourceCount.push_back(1);
		}
		else { ++s_sourceCount[static_cast<size_t>(it - s_sources.begin())]; }
	}
	s_source = 0;
	for (size_t k = 0; k < s_sources.size(); ++k) { if (s_sources[k] == keep) { s_source = static_cast<int>(k); } }
	rebuildItems();
}

float itemH(const ListItem &it) { return it.heading ? HEAD_H : CELL_H; }

int maxListScroll()
{
	float h = 0.f;
	for (int i = static_cast<int>(s_items.size()) - 1; i >= 0; --i)
	{
		h += itemH(s_items[static_cast<size_t>(i)]);
		if (h > LIST_H - 4.f) { return std::min(static_cast<int>(s_items.size()) - 1, i + 1); }
	}
	return 0;
}

struct Cell { int cat = -1; float x = 0, y = 0; };
struct Head { std::string text; float y = 0; };

void listLayout(std::vector<Cell> &cells, std::vector<Head> &heads)
{
	float y = LIST_Y + 2.f;
	for (size_t i = static_cast<size_t>(std::max(0, s_listScroll)); i < s_items.size(); ++i)
	{
		const ListItem &it = s_items[i];
		if (y + itemH(it) > LIST_Y + LIST_H) { break; }
		if (it.heading) { heads.push_back({ it.text, y }); }
		else
		{
			if (it.a >= 0) { cells.push_back({ it.a, LIST_X + 1.f, y }); }
			if (it.b >= 0) { cells.push_back({ it.b, LIST_X + 2.f + CELL_W, y }); }
		}
		y += itemH(it);
	}
}

/// Katalog-Index des Rumpf-Knopfes unter dem Bildpunkt (-1 = keiner)
int cellAt(int mx, int my)
{
	const Ctx c = ctx();
	std::vector<Cell> cells; std::vector<Head> heads;
	listLayout(cells, heads);
	for (const Cell &k : cells)
	{
		if (mx >= c.X(k.x) && mx < c.X(k.x + CELL_W) && my >= c.Y(k.y) && my < c.Y(k.y + CELL_H)) { return k.cat; }
	}
	return -1;
}

// ---------------------------------------------------------------- Geometrie-Hilfen
/// Huelle eines Teils (Eckpunkte der Modell-Grenzen, in Verbund-Koordinaten)
bool partCorners(const Part &p, glm::vec3 out[8])
{
	const BodyInfo *b = modcompbody::bodyInfo(p.body);
	if (b == nullptr || b->model == nullptr) { return false; }
	const glm::mat4 m = modcompbody::partMatrix(p);
	const Vector3i mn = b->model->min, mx = b->model->max;
	for (int k = 0; k < 8; ++k)
	{
		const glm::vec4 q = m * glm::vec4((k & 1) ? mx.x : mn.x, (k & 2) ? mx.y : mn.y, (k & 4) ? mx.z : mn.z, 1.f);
		out[k] = glm::vec3(q);
	}
	return true;
}

float compositeExtent()
{
	float ext = 20.f;
	for (const Part &p : s_def.parts)
	{
		glm::vec3 c[8];
		if (!partCorners(p, c)) { continue; }
		for (const glm::vec3 &v : c) { ext = std::max({ ext, std::fabs(v.x), std::fabs(v.y - 10.f), std::fabs(v.z) }); }
	}
	for (const Mount &m : s_def.mounts) { ext = std::max({ ext, std::fabs(m.pos.x), std::fabs(m.pos.z) }); }
	return ext;
}

float topOfComposite()
{
	float top = 0.f;
	bool any = false;
	for (const Part &p : s_def.parts)
	{
		glm::vec3 c[8];
		if (!partCorners(p, c)) { continue; }
		for (const glm::vec3 &v : c) { top = any ? std::max(top, v.y) : v.y; any = true; }
	}
	return top;
}

glm::vec3 dirFromYaw(float yawDeg)
{
	const float r = glm::radians(yawDeg);
	return glm::vec3(std::sin(r), 0.f, -std::cos(r));
}

bool yawFromDir(const glm::vec3 &d, float &yawDeg)
{
	if (std::fabs(d.x) + std::fabs(d.z) < 1e-4f) { return false; }
	yawDeg = glm::degrees(std::atan2(d.x, -d.z));
	return true;
}

/// natuerliche Waffenplaetze eines Rumpfes (Modellraum des Teils: x, Hoehe, Tiefe) - wie im WZ-Designer: so viele wie der Rumpf Plaetze hat
std::vector<glm::vec3> naturalSlots(const std::string &bodyId)
{
	std::vector<glm::vec3> out;
	const BodyInfo *b = modcompbody::bodyInfo(bodyId);
	if (b == nullptr || b->model == nullptr) { return out; }
	const size_t n = std::min<size_t>(std::max<unsigned>(1u, b->weaponSlots), b->model->connectors.size());
	for (size_t i = 0; i < n; ++i)
	{
		const Vector3i &c = b->model->connectors[i];   // x, Tiefe, Hoehe
		out.emplace_back(static_cast<float>(c.x), static_cast<float>(c.z), static_cast<float>(c.y));
	}
	return out;
}

bool linkValid(size_t k)
{
	return k < s_links.size() && s_links[k].part >= 0 && s_links[k].part < static_cast<int>(s_def.parts.size());
}

/// Waffenplatz k an seinem Teil neu ausrichten (nach Verschieben/Drehen/Spiegeln des Teils)
void applyLink(size_t k)
{
	if (!linkValid(k) || k >= s_def.mounts.size()) { return; }
	const Link &l = s_links[k];
	const glm::mat4 M = modcompbody::partMatrix(s_def.parts[static_cast<size_t>(l.part)]);
	Mount &m = s_def.mounts[k];
	m.pos = glm::vec3(M * glm::vec4(l.local, 1.f));
	float y = m.yaw;
	if (yawFromDir(glm::mat3(M) * dirFromYaw(l.localYaw), y)) { m.yaw = std::remainder(y, 360.f); }
}

/// nach eigenem Verschieben/Drehen eines Waffenplatzes: Lage am Teil neu merken
void relink(size_t k)
{
	if (!linkValid(k) || k >= s_def.mounts.size()) { return; }
	Link &l = s_links[k];
	const glm::mat4 Mi = glm::inverse(modcompbody::partMatrix(s_def.parts[static_cast<size_t>(l.part)]));
	const Mount &m = s_def.mounts[k];
	l.local = glm::vec3(Mi * glm::vec4(m.pos, 1.f));
	float y = l.localYaw;
	if (yawFromDir(glm::mat3(Mi) * dirFromYaw(m.yaw), y)) { l.localYaw = y; }
}

void syncPart(int partIdx)
{
	for (size_t k = 0; k < s_links.size(); ++k) { if (s_links[k].part == partIdx) { applyLink(k); } }
}

/// nach dem Laden: Waffenplaetze, die genau auf einem natuerlichen Platz eines Teils liegen, haengen wieder an diesem Teil
void inferLinks()
{
	s_links.assign(s_def.mounts.size(), Link());
	for (size_t k = 0; k < s_def.mounts.size(); ++k)
	{
		for (size_t i = 0; i < s_def.parts.size() && s_links[k].part < 0; ++i)
		{
			const glm::mat4 M = modcompbody::partMatrix(s_def.parts[i]);
			for (const glm::vec3 &loc : naturalSlots(s_def.parts[i].body))
			{
				if (glm::length(glm::vec3(M * glm::vec4(loc, 1.f)) - s_def.mounts[k].pos) < 0.75f)
				{
					s_links[k].part = static_cast<int>(i);
					relink(k);
					break;
				}
			}
		}
	}
	for (size_t k = 0; k < s_def.mounts.size(); ++k)   // (27.11.) sonst das gespeicherte Teil (Animation)
	{
		const int mp = s_def.mounts[k].part;
		if (s_links[k].part < 0 && mp >= 0 && mp < static_cast<int>(s_def.parts.size())) { s_links[k].part = mp; relink(k); }
	}
}

void addSlot(const Mount &m, const Link &l)
{
	s_def.mounts.push_back(m);
	s_links.push_back(l);
}

void removeSlot(size_t k)
{
	if (k >= s_def.mounts.size()) { return; }
	s_def.mounts.erase(s_def.mounts.begin() + static_cast<std::ptrdiff_t>(k));
	if (k < s_links.size()) { s_links.erase(s_links.begin() + static_cast<std::ptrdiff_t>(k)); }
	if (s_selMount == static_cast<int>(k)) { s_selMount = -1; }
	else if (s_selMount > static_cast<int>(k)) { --s_selMount; }
	s_dirty = true;
}

/// die natuerlichen Waffenplaetze des Teils i anlegen; Rueckgabe: wie viele
int addNaturalSlots(int partIdx)
{
	if (partIdx < 0 || partIdx >= static_cast<int>(s_def.parts.size())) { return 0; }
	int n = 0;
	for (const glm::vec3 &loc : naturalSlots(s_def.parts[static_cast<size_t>(partIdx)].body))
	{
		if (s_def.mounts.size() >= modcompbody::MAX_SLOTS) { break; }
		Link l; l.part = partIdx; l.local = loc; l.localYaw = 0.f;
		addSlot(Mount(), l);
		applyLink(s_def.mounts.size() - 1);
		++n;
	}
	return n;
}

// ---------------------------------------------------------------- Befehle
std::vector<int> groupOf(int i)
{
	std::vector<int> g;
	if (i < 0 || i >= static_cast<int>(s_def.parts.size())) { return g; }
	const int id = s_def.parts[static_cast<size_t>(i)].group;
	if (id == 0) { g.push_back(i); return g; }
	for (size_t j = 0; j < s_def.parts.size(); ++j) { if (s_def.parts[j].group == id) { g.push_back(static_cast<int>(j)); } }
	return g;
}

void selectPart(int i)
{
	if (s_def.parts.empty()) { s_sel = -1; s_multi.clear(); return; }
	s_sel = std::clamp(i, 0, static_cast<int>(s_def.parts.size()) - 1);
	s_multi = groupOf(s_sel);   // (27.11.) ein Klick waehlt den ganzen Verbund
	if (s_multi.size() < 2) { s_multi.clear(); }
	s_selMount = -1;
	s_selDrive = -1;
	if (s_sel < s_partScroll) { s_partScroll = s_sel; }
	if (s_sel >= s_partScroll + PART_ROWS) { s_partScroll = s_sel - PART_ROWS + 1; }
}

// ---------------------------------------------------------------- (Wunsch 27.11. 20:56 "A+B") Animation
bool partSelected();

std::vector<int> groupOf(int i);
/// (27.11.) Animation eines Verbunds sitzt an seinem ersten Teil (Fuehrer)
int animTarget()
{
	const std::vector<int> g = groupOf(s_sel);
	return g.empty() ? s_sel : g.front();
}

/// Pose k des aktuellen Ablaufs fuer Teil i (Liste wird bei Bedarf aufgefuellt); nullptr = keine Pose
modcompbody::PoseKey *poseKey(int i)
{
	const int n = s_def.poseCount[s_animClip];
	if (i < 0 || i >= static_cast<int>(s_def.parts.size()) || n <= 0 || s_animPoseIdx < 0 || s_animPoseIdx >= n) { return nullptr; }
	auto &keys = s_def.parts[static_cast<size_t>(i)].keys[s_animClip];
	if (keys.size() < static_cast<size_t>(n)) { keys.resize(static_cast<size_t>(n)); }
	return &keys[static_cast<size_t>(s_animPoseIdx)];
}

/// haengt parent (mittelbar) schon an child? (Kreis)
bool wouldCycle(int child, int parent)
{
	int q = parent, guard = 0;
	while (q >= 0 && q < static_cast<int>(s_def.parts.size()) && guard++ < 64)
	{
		if (q == child) { return true; }
		q = s_def.parts[static_cast<size_t>(q)].parent;
	}
	return false;
}

void setParent(int child, int parent)
{
	if (child < 0 || child >= static_cast<int>(s_def.parts.size())) { return; }
	if (parent == child) { setStatus("A part cannot hang on itself.", true); return; }
	if (parent >= 0 && wouldCycle(child, parent)) { setStatus("Part " + std::to_string(parent + 1) + " already hangs on part " + std::to_string(child + 1) + ".", true); return; }
	s_def.parts[static_cast<size_t>(child)].parent = parent;
	s_dirty = true;
	play("mfclick_move.ogg");
	setStatus(parent < 0 ? "Part " + std::to_string(child + 1) + " hangs on the hull itself." :
	          "Part " + std::to_string(child + 1) + " now hangs on part " + std::to_string(parent + 1) + " " + bodyName(s_def.parts[static_cast<size_t>(parent)].body) + " - it moves with it.");
}

void selectPart(int i);
/// Klick auf ein Teil (Liste oder Ansicht): beim Waehlen des "haengt an"-Teils dieses setzen, sonst das Teil waehlen
/// (27.11.) aktuelle Auswahl (gewaehltes Teil, mit Shift-Klick dazugenommene, Verbund)
std::vector<int> selSet()
{
	std::vector<int> v;
	if (!partSelected()) { return v; }
	if (std::find(s_multi.begin(), s_multi.end(), s_sel) != s_multi.end())
	{
		for (int k : s_multi) { if (k >= 0 && k < static_cast<int>(s_def.parts.size())) { v.push_back(k); } }
	}
	if (v.empty()) { v.push_back(s_sel); }
	std::sort(v.begin(), v.end());
	v.erase(std::unique(v.begin(), v.end()), v.end());
	return v;
}

void clickPart(int i)
{
	if (s_parentPick && s_animOpen && partSelected())
	{
		s_parentPick = false;
		setParent(animTarget(), groupOf(i).empty() ? i : groupOf(i).front());
		return;
	}
	const bool add = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT) || keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL);
	if (add && partSelected() && i >= 0 && i < static_cast<int>(s_def.parts.size()))
	{
		std::vector<int> cur = selSet();
		const std::vector<int> g = groupOf(i);
		const bool all = std::all_of(g.begin(), g.end(), [&cur](int k) { return std::find(cur.begin(), cur.end(), k) != cur.end(); });
		if (all && cur.size() > g.size())   // schon gewaehlt: wieder abwaehlen
		{
			cur.erase(std::remove_if(cur.begin(), cur.end(), [&g](int k) { return std::find(g.begin(), g.end(), k) != g.end(); }), cur.end());
			s_sel = cur.front();
		}
		else if (!all)
		{
			cur.insert(cur.end(), g.begin(), g.end());
			s_sel = i;
		}
		s_multi = cur;
		s_selMount = -1; s_selDrive = -1;
		const size_t n = selSet().size();
		setStatus(std::to_string(n) + " parts selected - they move and turn together. \"Join\" (Ctrl+J) makes them one piece.");
		return;
	}
	selectPart(i);
}

/// (27.11.) Drehung als Gieren/Nicken/Rollen (Part::rot) aus einer Drehmatrix (Reihenfolge wie partMatrix: y, x, z)
glm::vec3 eulerYXZ(const glm::mat3 &m)
{
	const float sp = std::clamp(-m[2][1], -1.f, 1.f);
	const float pitch = std::asin(sp);
	float yaw, roll;
	if (std::fabs(std::cos(pitch)) > 1e-5f) { yaw = std::atan2(m[2][0], m[2][2]); roll = std::atan2(m[0][1], m[1][1]); }
	else { yaw = std::atan2(-m[0][2], m[0][0]); roll = 0.f; }
	return glm::vec3(glm::degrees(yaw), glm::degrees(pitch), glm::degrees(roll));
}

void syncPart(int partIdx);
/// (27.11.) Join: die gewaehlten Teile werden ein Verbund (zaehlen als ein Stueck)
void joinSelected()
{
	const std::vector<int> v = selSet();
	if (v.size() < 2) { setStatus("Shift+click more parts first, then Join.", true); return; }
	int id = 0;
	for (const Part &p : s_def.parts) { id = std::max(id, p.group); }
	++id;
	for (int k : v) { s_def.parts[static_cast<size_t>(k)].group = id; }
	s_multi = v;
	s_dirty = true;
	play("mfui_clank.ogg");
	setStatus("Joined " + std::to_string(v.size()) + " parts into one piece - a click selects them all. \"Separate\" (P) splits them again.");
}

void separateSelected()
{
	const std::vector<int> v = selSet();
	bool any = false;
	for (int k : v)
	{
		const int id = s_def.parts[static_cast<size_t>(k)].group;
		if (id == 0) { continue; }
		for (Part &p : s_def.parts) { if (p.group == id) { p.group = 0; any = true; } }
	}
	if (!any) { setStatus("These parts are not joined.", true); return; }
	s_multi.clear();
	s_dirty = true;
	play("mfui_detach.ogg");
	setStatus("Separated - every part on its own again.");
}

// ---------------------------------------------------------------- (Wunsch 27.11. 21:26) Schneiden
void cutSet(int mode)
{
	if (s_shapeAll) { setStatus("Cutting works on one part - switch to \"Part\".", true); return; }
	if (!partSelected()) { setStatus("Select a part first.", true); return; }
	Part &p = s_def.parts[static_cast<size_t>(s_sel)];
	p.cut = std::clamp(mode, 0, 3);
	if (mode == 0) { p.cutAngle = 0.f; p.cutPos = 0.f; p.cutFlip = false; }
	syncPart(s_sel);
	s_dirty = true;
	play("mfclick_move.ogg");
	static const char *const T[4] = { "Whole again.", "Cut lengthwise - one side stays (\"Other half\" swaps).", "Cut across - front or back stays.", "Cut flat - top or bottom stays." };
	setStatus(T[std::clamp(mode, 0, 3)]);
}

void cutChange(int what, float sign)
{
	if (s_shapeAll || !partSelected()) { setStatus("Select a part first (\"Part\").", true); return; }
	Part &p = s_def.parts[static_cast<size_t>(s_sel)];
	if (p.cut == 0) { setStatus("Choose Length, Width or Flat first.", true); return; }
	const bool fine = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	if (what == 0) { p.cutAngle = std::remainder(p.cutAngle + (fine ? 5.f : 15.f) * sign, 360.f); }
	else if (what == 1) { p.cutPos += (fine ? 0.5f : 2.f) * sign; }
	else { p.cutFlip = !p.cutFlip; }
	s_dirty = true;
}

void animSetMotion(int type)
{
	if (!partSelected()) { setStatus("Select a part first.", true); return; }
	Part &p = s_def.parts[static_cast<size_t>(animTarget())];
	p.motion = modcompbody::defaultMotion(type);
	if (glm::length(p.pivot) < 1e-4f)   // noch kein Drehpunkt: passender Vorschlag
	{
		if (type == modcompbody::MOT_LEG || type == modcompbody::MOT_SWAY) { p.pivot = modcompbody::pivotPreset(p, 2); }
		else if (type == modcompbody::MOT_SPIN) { p.pivot = modcompbody::pivotPreset(p, 1); }
	}
	s_dirty = true;
	if (s_animPlay < 0 && type != modcompbody::MOT_NONE) { s_animPlay = p.motion.when == modcompbody::WHEN_ALWAYS ? modcompbody::CLIP_STANDING : p.motion.when - 1; s_animPlayStart = realTime; }
	play("mfclick_move.ogg");
	static const char *const TIP[modcompbody::MOT_COUNT] = { "No motion.", "Leg: swings back and forth while moving (pivot at the top = hip).",
		"Spin: turns all the time (radar, rotor).", "Bob: goes up and down while moving.", "Pump: slides back and forth when firing (piston, recoil).",
		"Sway: rocks slowly while standing." };
	setStatus(TIP[std::clamp(type, 0, modcompbody::MOT_COUNT - 1)]);
}

void animAddPose()
{
	int &n = s_def.poseCount[s_animClip];
	if (n >= modcompbody::MAX_POSES) { setStatus("At most " + std::to_string(modcompbody::MAX_POSES) + " poses.", true); return; }
	const int at = n == 0 ? 0 : s_animPoseIdx + 1;
	for (Part &p : s_def.parts)
	{
		auto &keys = p.keys[s_animClip];
		if (keys.size() < static_cast<size_t>(n)) { keys.resize(static_cast<size_t>(n)); }
		const modcompbody::PoseKey copy = (n > 0 && s_animPoseIdx < static_cast<int>(keys.size())) ? keys[static_cast<size_t>(s_animPoseIdx)] : modcompbody::PoseKey();
		keys.insert(keys.begin() + at, copy);   // neue Pose = Kopie der aktuellen (dann aendern)
	}
	++n;
	s_animPoseIdx = at;
	s_poseEdit = true; s_pivotEdit = false; s_animPlay = -1;
	s_dirty = true;
	play("mfui_clank.ogg");
	setStatus("Pose " + std::to_string(at + 1) + " - now move and turn the parts with the buttons (they turn around their pivot).");
}

void animRemovePose()
{
	int &n = s_def.poseCount[s_animClip];
	if (n <= 0) { return; }
	const int k = std::clamp(s_animPoseIdx, 0, n - 1);
	for (Part &p : s_def.parts)
	{
		auto &keys = p.keys[s_animClip];
		if (k < static_cast<int>(keys.size())) { keys.erase(keys.begin() + k); }
	}
	--n;
	s_animPoseIdx = std::clamp(k, 0, std::max(0, n - 1));
	if (n == 0) { s_poseEdit = false; }
	s_dirty = true;
	play("mfui_detach.ogg");
	setStatus(n == 0 ? std::string("No poses left in this action.") : "Pose removed - " + std::to_string(n) + " left.");
}

/// Lage jedes Teils fuer die Ansicht (Pose bearbeiten: diese Pose; Vorschau: laufender Ablauf); leer = Ruhelage
std::vector<glm::mat4> bayAnimW()
{
	std::vector<glm::mat4> W;
	if ((!s_animOpen && !s_alOpen) || s_def.parts.empty()) { return W; }   // (27.11. 23:19) auch die Animations-Liste spielt vor
	using namespace modcompbody;
	const int n = s_def.poseCount[s_animClip];
	if (s_poseEdit && n > 0)
	{
		Def tmp = s_def;
		for (Part &p : tmp.parts) { p.motion.type = MOT_NONE; }
		for (int c = 0; c < CLIP_COUNT; ++c) { if (c != s_animClip) { tmp.poseCount[c] = 0; } }
		AnimCtx c;
		const float secs = std::max(0.1f, tmp.poseSeconds[s_animClip]);
		const int k = std::clamp(s_animPoseIdx, 0, n - 1);
		if (s_animClip == CLIP_MOVING) { c.moveW = 1.f; c.moveClock = secs * static_cast<float>(k) / static_cast<float>(n); }
		else if (s_animClip == CLIP_STANDING) { c.moveW = 0.f; c.standClock = secs * static_cast<float>(k) / static_cast<float>(n); }
		else { c.fireW = 1.f; c.fireAge = secs * (static_cast<float>(k + 1) + 0.0001f) / static_cast<float>(n + 1); }
		evalAnim(tmp, c, W);
		return W;
	}
	if (s_animPlay < 0) { return W; }
	AnimCtx c;
	const float t = static_cast<float>(realTime - s_animPlayStart) / 1000.f;
	c.time = t;
	if (s_animPlay == CLIP_MOVING) { c.moveW = 1.f; c.moveClock = t; }   // wie 128 Einheiten je Sekunde
	else if (s_animPlay == CLIP_STANDING) { c.moveW = 0.f; c.standClock = t; }
	else
	{
		const float period = std::max(1.2f, s_def.poseSeconds[CLIP_FIRING] + 0.4f);   // ein Schuss je Takt
		c.fireAge = std::fmod(t, period);
		c.fireW = c.fireAge < 0.15f ? 1.f : std::max(0.f, 1.f - (c.fireAge - 0.15f) / 0.5f);
		c.fireClock = t;
		c.standClock = t;
	}
	evalAnim(s_def, c, W);
	return W;
}

void selectMount(int k)
{
	if (k < 0 || k >= static_cast<int>(s_def.mounts.size())) { return; }
	s_selMount = k;
	s_selDrive = -1;
	if (s_selMount < s_mountScroll) { s_mountScroll = s_selMount; }
	if (s_selMount >= s_mountScroll + MOUNT_ROWS) { s_mountScroll = s_selMount - MOUNT_ROWS + 1; }
}

// ---------------------------------------------------------------- (Wunsch 27.11. 19:16) Antriebsplaetze
bool driveSelected()
{
	return s_selDrive >= 0 && s_selDrive < static_cast<int>(s_def.drives.size());
}

void selectDrive(int k)
{
	if (k < 0 || k >= static_cast<int>(s_def.drives.size())) { return; }
	s_selDrive = k;
	s_selMount = -1;
}

/// Rumpf, dessen Laufwerk ein Antriebsplatz traegt (leer = Fahrgestell)
std::string driveBodyId(const modcompbody::Drive &dr)
{
	if (!dr.body.empty()) { return dr.body; }
	return s_def.parts.empty() ? std::string() : s_def.parts[0].body;
}

const iIMDShape *driveShape(const modcompbody::Drive &dr)
{
	const BodyInfo *b = modcompbody::bodyInfo(driveBodyId(dr));
	if (b == nullptr || b->driveModel == nullptr) { return nullptr; }
	return b->driveModel->displayModel();
}

void addDrive()
{
	if (s_def.parts.empty()) { setStatus("Add a hull first.", true); return; }
	if (s_def.drives.size() >= modcompbody::MAX_DRIVES) { setStatus("At most " + std::to_string(modcompbody::MAX_DRIVES) + " drives.", true); return; }
	modcompbody::Drive dr;
	if (!s_def.drives.empty())
	{
		// neben den letzten setzen: erst gespiegelt, dann dahinter
		const modcompbody::Drive &last = s_def.drives.back();
		dr = last;
		bool mirrorFree = std::fabs(last.pos.x) > 2.f;
		for (const modcompbody::Drive &o : s_def.drives) { if (glm::length(o.pos - glm::vec3(-last.pos.x, last.pos.y, last.pos.z)) < 2.f) { mirrorFree = false; } }
		if (mirrorFree) { dr.pos.x = -last.pos.x; dr.yaw = -last.yaw; }
		else
		{
			const iIMDShape *sh = driveShape(last);
			const float len = sh != nullptr ? static_cast<float>(sh->max.z - sh->min.z) * last.scale : 30.f;
			dr.pos.z += std::max(8.f, std::round(len));
		}
	}
	s_def.drives.push_back(dr);
	selectDrive(static_cast<int>(s_def.drives.size()) - 1);
	s_dirty = true;
	play("mfui_clank.ogg");
	setStatus("Drive " + std::to_string(s_def.drives.size()) + " added - move it with MOVE, turn it with Turn; each drive gets its own propulsion in the Unit Designer.");
}

void removeDrive(int k)
{
	if (k < 0 || k >= static_cast<int>(s_def.drives.size())) { return; }
	s_def.drives.erase(s_def.drives.begin() + k);
	if (s_def.drives.empty()) { s_selDrive = -1; }
	else if (s_selDrive >= 0) { s_selDrive = std::min(s_selDrive > k ? s_selDrive - 1 : s_selDrive, static_cast<int>(s_def.drives.size()) - 1); }
	s_dirty = true;
	play("mfui_detach.ogg");
	setStatus(s_def.drives.empty() ? std::string("Last drive removed - the hull uses one drive under the chassis again.") : std::string("Drive removed."));
}

void scaleDrive(float f)
{
	if (!driveSelected()) { setStatus("Select a drive first (SLOTS list or click it).", true); return; }
	modcompbody::Drive &dr = s_def.drives[static_cast<size_t>(s_selDrive)];
	dr.scale = std::clamp(std::round(dr.scale * f * 20.f) / 20.f, 0.25f, 4.f);
	s_dirty = true;
}

void addPart(size_t catIdx)
{
	const auto &cat = modcompbody::catalog();
	if (catIdx >= cat.size()) { return; }
	const BodyInfo &b = cat[catIdx];
	Part p;
	p.body = b.id;
	if (!s_def.parts.empty())
	{
		// oben auf den bisherigen Aufbau setzen (danach frei verschieben)
		const float bottom = b.model != nullptr ? static_cast<float>(b.model->min.y) : 0.f;
		p.pos = glm::vec3(0.f, std::round((topOfComposite() - bottom) * 2.f) / 2.f, 0.f);
	}
	s_def.parts.push_back(p);
	const int idx = static_cast<int>(s_def.parts.size()) - 1;
	const int n = addNaturalSlots(idx);   // (04:23) wie im WZ-Designer: der Rumpf bringt seine Waffenplaetze mit
	selectPart(idx);
	s_dirty = true;
	play("mfui_clank.ogg");
	std::string t = s_def.parts.size() == 1 ? "Chassis: " + b.name : "Added: " + b.name;
	if (n > 0) { t += " (+" + std::to_string(n) + (n == 1 ? " slot)" : " slots)"); }
	if (locked(b)) { t += " - needs research to build"; }
	setStatus(t);
}

void move(int axis, float sign)
{
	const bool fine = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	const float d = (fine ? STEPS[0] : STEPS[s_step]) * sign;
	if (s_animOpen && partSelected() && (s_pivotEdit || s_poseEdit))   // (27.11.) Drehpunkt bzw. Pose statt Ruhelage
	{
		Part &p = s_def.parts[static_cast<size_t>(animTarget())];
		if (s_pivotEdit)
		{
			glm::vec3 wd(0.f); wd[axis] = d;
			p.pivot += glm::inverse(glm::mat3(modcompbody::partMatrix(p))) * wd;
		}
		else if (modcompbody::PoseKey *k = poseKey(animTarget())) { k->pos[axis] += d; }
		s_dirty = true;
		return;
	}
	if (driveSelected())   // (27.11.)
	{
		s_def.drives[static_cast<size_t>(s_selDrive)].pos[axis] += d;
		s_dirty = true;
		return;
	}
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		s_def.mounts[static_cast<size_t>(s_selMount)].pos[axis] += d;
		relink(static_cast<size_t>(s_selMount));
	}
	else if (s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size()))
	{
		for (int k : selSet()) { s_def.parts[static_cast<size_t>(k)].pos[axis] += d; syncPart(k); }   // (27.11.) Auswahl/Verbund gemeinsam
	}
	else { return; }
	s_dirty = true;
}

void rotate(int axis, float sign)
{
	const bool fine = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	if (s_animOpen && partSelected() && s_pivotEdit) { setStatus("The pivot only moves - switch \"Move\" off to turn the part.", true); return; }
	if (s_animOpen && partSelected() && s_poseEdit)   // (27.11.) Pose: um den Drehpunkt drehen
	{
		if (modcompbody::PoseKey *k = poseKey(animTarget()))
		{
			float &a = k->rot[axis];
			a = std::remainder(a + (fine ? 1.f : ANGLES[s_angle]) * sign, 360.f);
			s_dirty = true;
		}
		return;
	}
	if (driveSelected())   // (27.11.) Antriebsplatz: nur um die Hochachse
	{
		if (axis != 0) { setStatus("Drives only turn left/right.", true); return; }
		modcompbody::Drive &dr = s_def.drives[static_cast<size_t>(s_selDrive)];
		dr.yaw = std::remainder(dr.yaw - (fine ? 1.f : ANGLES[s_angle]) * sign, 360.f);
		s_dirty = true;
		return;
	}
	// (Wunsch 23.11. 03:05) Waffenplatz: Gieren dreht seine Blickrichtung - die Waffe schwenkt 180 Grad um sie
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		// (Wunsch 27.11. 18:48 "Auch Weapon Slots sollen in jegliche Richtung rotieren koennen"): Drehen = Blickrichtung, Nase/Rollen = Neigung
		// des Platzes (wie bei den Teilen); die Waffe schwenkt im Spiel um die geneigte Achse
		Mount &mt = s_def.mounts[static_cast<size_t>(s_selMount)];
		const float stepDeg = (fine ? 1.f : ANGLES[s_angle]) * sign;
		if (axis == 0) { mt.yaw = std::remainder(mt.yaw - stepDeg, 360.f); }
		else if (axis == 1) { mt.pitch = std::remainder(mt.pitch + stepDeg, 360.f); }
		else { mt.roll = std::remainder(mt.roll + stepDeg, 360.f); }
		relink(static_cast<size_t>(s_selMount));
		s_dirty = true;
		return;
	}
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size())) { return; }
	{
		const std::vector<int> set = selSet();
		if (set.size() > 1)   // (27.11.) Auswahl/Verbund: gemeinsam um ihre Mitte (Welt-Achsen)
		{
			glm::vec3 mid(0.f);
			for (int k : set) { mid += s_def.parts[static_cast<size_t>(k)].pos; }
			mid /= static_cast<float>(set.size());
			static const glm::vec3 AX[3] = { glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f, 0.f, 1.f) };
			const glm::mat4 Rd = glm::rotate(glm::mat4(1.f), glm::radians((fine ? 1.f : ANGLES[s_angle]) * sign), AX[axis]);
			for (int k : set)
			{
				Part &q = s_def.parts[static_cast<size_t>(k)];
				q.pos = mid + glm::vec3(Rd * glm::vec4(q.pos - mid, 0.f));
				glm::mat4 r(1.f);
				r = glm::rotate(r, glm::radians(q.rot.x), glm::vec3(0.f, 1.f, 0.f));
				r = glm::rotate(r, glm::radians(q.rot.y), glm::vec3(1.f, 0.f, 0.f));
				r = glm::rotate(r, glm::radians(q.rot.z), glm::vec3(0.f, 0.f, 1.f));
				q.rot = eulerYXZ(glm::mat3(Rd * r));
				syncPart(k);
			}
			s_dirty = true;
			return;
		}
	}
	float &a = s_def.parts[static_cast<size_t>(s_sel)].rot[axis];
	a = std::fmod(a + (fine ? 1.f : ANGLES[s_angle]) * sign + 720.f, 360.f);
	if (a > 180.f) { a -= 360.f; }
	syncPart(s_sel);
	s_dirty = true;
}

void duplicatePart(bool mirror)
{
	if (driveSelected())   // (27.11.)
	{
		if (s_def.drives.size() >= modcompbody::MAX_DRIVES) { setStatus("At most " + std::to_string(modcompbody::MAX_DRIVES) + " drives.", true); return; }
		modcompbody::Drive dr = s_def.drives[static_cast<size_t>(s_selDrive)];
		if (mirror) { dr.pos.x = -dr.pos.x; dr.yaw = -dr.yaw; } else { dr.pos.z += STEPS[s_step] * 2.f; }
		s_def.drives.push_back(dr);
		selectDrive(static_cast<int>(s_def.drives.size()) - 1);
		s_dirty = true;
		play("mfui_clank.ogg");
		setStatus(mirror ? "Drive mirrored." : "Drive copied.");
		return;
	}
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		if (s_def.mounts.size() >= modcompbody::MAX_SLOTS) { setStatus("At most " + std::to_string(modcompbody::MAX_SLOTS) + " weapon slots.", true); return; }
		Mount m = s_def.mounts[static_cast<size_t>(s_selMount)];
		if (mirror) { m.pos.x = -m.pos.x; m.yaw = -m.yaw; m.roll = -m.roll; } else { m.pos.x += STEPS[s_step]; }
		addSlot(m, Link());
		selectMount(static_cast<int>(s_def.mounts.size()) - 1);
		s_dirty = true;
		return;
	}
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size())) { return; }
	if (selSet().size() > 1)   // (27.11.) Auswahl/Verbund als Ganzes kopieren/spiegeln (Verbund bleibt Verbund)
	{
		const std::vector<int> set = selSet();
		int newGroup = 0;
		for (const Part &q : s_def.parts) { newGroup = std::max(newGroup, q.group); }
		std::map<int, int> grpMap, idxMap;
		std::vector<int> copies;
		for (int k : set)
		{
			Part q = s_def.parts[static_cast<size_t>(k)];
			if (mirror)
			{
				q.pos.x = -q.pos.x; q.rot.x = -q.rot.x; q.rot.z = -q.rot.z; q.flip ^= 1;
				using namespace modcompbody;
				if (q.motion.type == MOT_SPIN) { q.motion.speed = -q.motion.speed; }
				else if (q.motion.type != MOT_NONE) { q.motion.phase = std::fmod(q.motion.phase + 0.5f, 1.f); }
				for (auto &keys : q.keys) { for (PoseKey &pk : keys) { pk.rot.x = -pk.rot.x; pk.rot.z = -pk.rot.z; pk.pos.x = -pk.pos.x; } }
			}
			else { q.pos.x += 8.f; }
			if (q.group != 0)
			{
				if (grpMap.find(q.group) == grpMap.end()) { grpMap[q.group] = ++newGroup; }
				q.group = grpMap[q.group];
			}
			s_def.parts.push_back(q);
			const int dst = static_cast<int>(s_def.parts.size()) - 1;
			idxMap[k] = dst;
			copies.push_back(dst);
			const size_t nm = s_def.mounts.size();
			for (size_t m = 0; m < nm; ++m)
			{
				if (m >= s_links.size() || s_links[m].part != k || s_def.mounts.size() >= modcompbody::MAX_SLOTS) { continue; }
				Link l = s_links[m]; l.part = dst;
				addSlot(s_def.mounts[m], l);
				applyLink(s_def.mounts.size() - 1);
			}
		}
		for (int dst : copies)   // haengt an einem mitkopierten Teil: an dessen Kopie
		{
			Part &q = s_def.parts[static_cast<size_t>(dst)];
			const auto it = idxMap.find(q.parent);
			if (it != idxMap.end()) { q.parent = it->second; }
		}
		s_sel = copies.front();
		s_multi = copies;
		s_selMount = -1; s_selDrive = -1;
		s_dirty = true;
		play("mfui_clank.ogg");
		setStatus(std::string(mirror ? "Mirrored " : "Copied ") + std::to_string(copies.size()) + " parts.");
		return;
	}
	const int src = s_sel;
	Part p = s_def.parts[static_cast<size_t>(src)];
	if (mirror) { p.pos.x = -p.pos.x; p.rot.x = -p.rot.x; p.rot.z = -p.rot.z; p.flip ^= 1; }   // echte Spiegelung an der Mittelebene
	else { p.pos.x += 8.f; }
	if (mirror)   // (27.11.) Animation gespiegelt: das andere Bein tritt im Gegentakt, Drehungen laufen andersherum
	{
		using namespace modcompbody;
		if (p.motion.type == MOT_SPIN) { p.motion.speed = -p.motion.speed; }
		else if (p.motion.type != MOT_NONE) { p.motion.phase = std::fmod(p.motion.phase + 0.5f, 1.f); }
		for (auto &keys : p.keys) { for (PoseKey &k : keys) { k.rot.x = -k.rot.x; k.rot.z = -k.rot.z; k.pos.x = -k.pos.x; } }
	}
	s_def.parts.push_back(p);
	const int dst = static_cast<int>(s_def.parts.size()) - 1;
	// die Waffenplaetze des Teils wandern mit (gespiegelt ueber die gespiegelte Teil-Lage)
	const size_t nm = s_def.mounts.size();
	int copied = 0;
	for (size_t k = 0; k < nm; ++k)
	{
		if (k >= s_links.size() || s_links[k].part != src) { continue; }
		if (s_def.mounts.size() >= modcompbody::MAX_SLOTS) { break; }
		Link l = s_links[k]; l.part = dst;
		addSlot(s_def.mounts[k], l);
		applyLink(s_def.mounts.size() - 1);
		++copied;
	}
	selectPart(dst);
	s_dirty = true;
	play("mfui_clank.ogg");
	setStatus(std::string(mirror ? "Mirrored" : "Copied") + (copied > 0 ? " (+" + std::to_string(copied) + (copied == 1 ? " slot)" : " slots)") : std::string()));
}

void removePart(int i)
{
	if (i < 0 || i >= static_cast<int>(s_def.parts.size())) { return; }
	// zuerst die Waffenplaetze dieses Teils
	for (size_t k = s_def.mounts.size(); k-- > 0;)
	{
		if (k < s_links.size() && s_links[k].part == i) { removeSlot(k); }
	}
	for (Link &l : s_links) { if (l.part > i) { --l.part; } }
	{
		const int up = s_def.parts[static_cast<size_t>(i)].parent;   // (27.11.) Kinder haengen danach am Eltern-Teil des geloeschten
		for (Part &q : s_def.parts)
		{
			if (q.parent == i) { q.parent = up; }
			if (q.parent > i) { --q.parent; }
		}
	}
	s_parentPick = false;
	s_multi.clear();
	const std::string name = bodyName(s_def.parts[static_cast<size_t>(i)].body);
	s_def.parts.erase(s_def.parts.begin() + i);
	if (i == 0 && !s_def.parts.empty()) { setStatus("Removed " + name + " - new chassis: " + bodyName(s_def.parts[0].body)); }
	else { setStatus("Removed: " + name); }
	if (s_def.parts.empty()) { s_sel = -1; }
	else { selectPart(std::min(std::max(0, s_sel > i ? s_sel - 1 : s_sel), static_cast<int>(s_def.parts.size()) - 1)); }
	s_selMount = -1;
	s_dirty = true;
	play("mfui_detach.ogg"); /* MOD: mfui_rattle.ogg gab es nie */
}

void deleteSelected()
{
	if (driveSelected()) { removeDrive(s_selDrive); return; }   // (27.11.)
	if (partSelected() && selSet().size() > 1)   // (27.11.) Auswahl/Verbund ganz
	{
		std::vector<int> set = selSet();
		const size_t n = set.size();
		for (size_t k = set.size(); k-- > 0;) { removePart(set[k]); }
		s_multi.clear();
		setStatus("Removed " + std::to_string(n) + " parts.");
		return;
	}
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		const int k = s_selMount;
		removeSlot(static_cast<size_t>(k));
		if (!s_def.mounts.empty()) { selectMount(std::min(k, static_cast<int>(s_def.mounts.size()) - 1)); }
		play("mfui_detach.ogg"); /* MOD: mfui_rattle.ogg gab es nie */
		return;
	}
	removePart(s_sel);
}

void flipSelected(int axisBit)
{
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size()) || s_selMount >= 0 || driveSelected()) { return; }
	s_def.parts[static_cast<size_t>(s_sel)].flip ^= axisBit;
	syncPart(s_sel);
	s_dirty = true;
	play("mfui_clank.ogg");
}

void resetSelected()
{
	if (driveSelected())   // (27.11.) Antriebsplatz: geradeaus, Originalgroesse, auf den Boden
	{
		modcompbody::Drive &dr = s_def.drives[static_cast<size_t>(s_selDrive)];
		dr.yaw = 0.f; dr.scale = 1.f; dr.pos.y = 0.f;
		s_dirty = true;
		return;
	}
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		const size_t k = static_cast<size_t>(s_selMount);
		if (linkValid(k))
		{
			// Platz am Teil: zurueck auf den naechsten natuerlichen Platz des Rumpfes, Blick nach vorn
			const std::vector<glm::vec3> nat = naturalSlots(s_def.parts[static_cast<size_t>(s_links[k].part)].body);
			float best = 1e9f;
			for (const glm::vec3 &n : nat) { const float d = glm::length(n - s_links[k].local); if (d < best) { best = d; s_links[k].local = n; } }
			s_links[k].localYaw = 0.f;
			applyLink(k);
		}
		else { s_def.mounts[k].pos = glm::vec3(0.f, 20.f, 0.f); s_def.mounts[k].yaw = 0.f; }
		s_def.mounts[k].pitch = 0.f; s_def.mounts[k].roll = 0.f;   // (27.11.) Neigung zuruecksetzen
		s_dirty = true;
		return;
	}
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size())) { return; }
	Part &p = s_def.parts[static_cast<size_t>(s_sel)];
	p.rot = glm::vec3(0.f);
	p.flip = 0;
	if (s_sel == 0) { p.pos = glm::vec3(0.f); }
	syncPart(s_sel);
	s_dirty = true;
}

/// Form aendern: Achse 0 = Breite (x), 1 = Hoehe (y), 2 = Laenge (z); sign +1 groesser, -1 kleiner (Faktor 1 + Schritt bzw. sein Kehrwert)
void shapeChange(int axis, float sign)
{
	const float f = sign > 0.f ? 1.f + SHAPE_STEPS[s_shapeStep] : 1.f / (1.f + SHAPE_STEPS[s_shapeStep]);
	if (!s_shapeAll)
	{
		if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size())) { setStatus("Select a part first (or switch to the whole hull).", true); return; }
		Part &p = s_def.parts[static_cast<size_t>(s_sel)];
		p.stretch[axis] = std::clamp(p.stretch[axis] * f, 0.25f, 4.f);
		syncPart(s_sel);
	}
	else
	{
		if (s_def.parts.empty()) { return; }
		// ganzer Rumpf: Lagen strecken und jedes Teil entlang der Welt-Achse (ueber seine Drehung auf die Teil-Achsen verteilt)
		glm::vec3 F(1.f); F[axis] = f;
		std::vector<bool> linked(s_def.mounts.size(), false);
		for (size_t k = 0; k < s_def.mounts.size(); ++k) { linked[k] = linkValid(k); }
		for (size_t i = 0; i < s_def.parts.size(); ++i)
		{
			Part &p = s_def.parts[i];
			p.pos[axis] *= f;
			glm::mat4 r(1.f);
			r = glm::rotate(r, glm::radians(p.rot.x), glm::vec3(0.f, 1.f, 0.f));
			r = glm::rotate(r, glm::radians(p.rot.y), glm::vec3(1.f, 0.f, 0.f));
			r = glm::rotate(r, glm::radians(p.rot.z), glm::vec3(0.f, 0.f, 1.f));
			for (int j = 0; j < 3; ++j)
			{
				glm::vec3 e(0.f); e[j] = 1.f;
				const glm::vec3 w = glm::vec3(r * glm::vec4(e, 0.f));
				p.stretch[j] = std::clamp(p.stretch[j] * glm::length(F * w), 0.25f, 4.f);
			}
			syncPart(static_cast<int>(i));
		}
		for (size_t k = 0; k < s_def.mounts.size(); ++k) { if (!linked[k]) { s_def.mounts[k].pos[axis] *= f; } }
	}
	s_dirty = true;
}

void shapeReset()
{
	auto whole = [](Part &q) { q.stretch = glm::vec3(1.f); q.cut = 0; q.cutAngle = 0.f; q.cutPos = 0.f; q.cutFlip = false; };   // (27.11.) samt Schnitt
	if (s_shapeAll) { for (size_t i = 0; i < s_def.parts.size(); ++i) { whole(s_def.parts[i]); syncPart(static_cast<int>(i)); } }
	else if (s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size())) { whole(s_def.parts[static_cast<size_t>(s_sel)]); syncPart(s_sel); }
	else { return; }
	s_dirty = true;
	play("mfui_clank.ogg");
}

void partsShow(bool on);
void animShow(bool on);
void alistShow(bool on);
void shapeShow(bool on)
{
	if (on && s_partsOpen) { partsShow(false); }   // (27.11.) nur eines der beiden Fenster
	if (on && s_animOpen) { animShow(false); }
	if (on && s_alOpen) { alistShow(false); }
	s_shapeOpen = on;
	for (size_t k : s_shapeBtns) { if (k < s_btns.size()) { if (on) { s_btns[k].w->show(); } else { s_btns[k].w->hide(); } } }
}

void partsShow(bool on)
{
	if (on && s_shapeOpen) { shapeShow(false); }
	if (on && s_animOpen) { animShow(false); }
	if (on && s_alOpen) { alistShow(false); }
	s_partsOpen = on;
	if (!on) { s_texPick = false; s_drivePick = false; }
	for (size_t k : s_partsBtns) { if (k < s_btns.size()) { if (on) { s_btns[k].w->show(); } else { s_btns[k].w->hide(); } } }
}

void animShow(bool on)
{
	if (on && s_shapeOpen) { shapeShow(false); }
	if (on && s_partsOpen) { partsShow(false); }
	if (on && s_alOpen) { alistShow(false); }
	s_animOpen = on;
	if (!on) { s_pivotEdit = false; s_parentPick = false; s_poseEdit = false; s_animPlay = -1; }
	for (size_t k : s_animBtns) { if (k < s_btns.size()) { if (on) { s_btns[k].w->show(); } else { s_btns[k].w->hide(); } } }
}

// ---------------------------------------------------------------- (27.11. 23:19) Animations-Liste
void alistRefresh()
{
	auto vis = [](const std::vector<size_t> &v, bool on) { for (size_t k : v) { if (k < s_btns.size()) { if (on) { s_btns[k].w->show(); } else { s_btns[k].w->hide(); } } } };
	vis(s_alBtns, s_alOpen);
	vis(s_alHullBtns, s_alOpen && s_alTab == 0);
	vis(s_alLibBtns, s_alOpen && s_alTab == 1);
}

void alistShow(bool on)
{
	if (on && s_shapeOpen) { shapeShow(false); }
	if (on && s_partsOpen) { partsShow(false); }
	if (on && s_animOpen) { animShow(false); }
	s_alOpen = on;
	s_alArmed = -1;
	if (on) { s_alSets = modcompbody::animSets(); s_alScroll = 0; }
	else if (!s_animOpen) { s_animPlay = -1; }
	alistRefresh();
}

enum { AR_TEXT = 0, AR_CLIP, AR_KEYS, AR_MOTHEAD, AR_MOTION, AR_SECTION, AR_GAIT, AR_SINGLE, AR_SET };
struct ARow { int kind = AR_TEXT; int part = -1; int clip = -1; int idx = -1; std::string a, b; };

const char *clipTitle(int clip)
{
	return clip == modcompbody::CLIP_MOVING ? "Walk  (moving)" : (clip == modcompbody::CLIP_STANDING ? "Stand" : "Fire");
}

std::vector<ARow> alRows()
{
	using namespace modcompbody;
	std::vector<ARow> v;
	auto add = [&v](int kind, std::string a, std::string b = std::string(), int part = -1, int clip = -1, int idx = -1) {
		ARow r; r.kind = kind; r.a = std::move(a); r.b = std::move(b); r.part = part; r.clip = clip; r.idx = idx; v.push_back(r);
	};
	if (s_alTab == 0)
	{
		for (int c = 0; c < CLIP_COUNT; ++c)
		{
			if (s_def.poseCount[c] <= 0) { continue; }
			char b[48];
			snprintf(b, sizeof(b), "%d pose%s  %.2f", s_def.poseCount[c], s_def.poseCount[c] == 1 ? "" : "s", static_cast<double>(s_def.poseSeconds[c]));
			add(AR_CLIP, clipTitle(c), b, -1, c);
			for (size_t i = 0; i < s_def.parts.size(); ++i)
			{
				const Part &p = s_def.parts[i];
				if (p.keys[c].empty()) { continue; }
				const std::string d = modanimlib::describeKeys(p, c);
				if (d == "still") { continue; }
				add(AR_KEYS, std::to_string(i + 1) + "  " + bodyName(p.body), d, static_cast<int>(i), c);
			}
		}
		bool anyMot = false;
		for (const Part &p : s_def.parts) { anyMot = anyMot || p.motion.type != MOT_NONE; }
		if (anyMot)
		{
			add(AR_MOTHEAD, "Motions");
			for (size_t i = 0; i < s_def.parts.size(); ++i)
			{
				const Part &p = s_def.parts[i];
				if (p.motion.type == MOT_NONE) { continue; }
				add(AR_MOTION, std::to_string(i + 1) + "  " + bodyName(p.body), modanimlib::describeMotion(p.motion), static_cast<int>(i));
			}
		}
		if (v.empty()) { add(AR_TEXT, s_def.parts.empty() ? "No hull yet." : "No animation yet - pick one in the Library."); }
	}
	else
	{
		add(AR_SECTION, "Whole sets - they find the legs themselves");
		for (int g = 0; g < modanimlib::GAIT_COUNT; ++g) { add(AR_GAIT, modanimlib::gaitName(g), modanimlib::gaitInfo(g), -1, -1, g); }
		add(AR_SECTION, "Single animations - on the selected part");
		for (int k = 0; k < modanimlib::ONE_COUNT; ++k) { add(AR_SINGLE, modanimlib::singleName(k), modanimlib::singleInfo(k), -1, -1, k); }
		add(AR_SECTION, "Your sets");
		for (size_t k = 0; k < s_alSets.size(); ++k) { add(AR_SET, s_alSets[k].name, std::to_string(s_alSets[k].parts) + " parts", -1, -1, static_cast<int>(k)); }
		if (s_alSets.empty()) { add(AR_TEXT, "(none yet - \"Save as set\" on the first tab)"); }
	}
	return v;
}

void alBackup()
{
	s_alUndo = s_def;
	s_alHasUndo = true;
}

void alUndo()
{
	if (!s_alHasUndo || s_alUndo.parts.size() != s_def.parts.size()) { s_alHasUndo = false; setStatus("Nothing to undo.", true); return; }
	s_def = s_alUndo;
	s_alHasUndo = false;
	s_dirty = true;
	s_animPlay = -1;
	setStatus("Undone.");
	play("mfclick_move.ogg");
}

void alPlay(int clip)
{
	s_animPlay = clip;
	s_animPlayStart = realTime;
}

void alClick(int row, bool right)
{
	using namespace modcompbody;
	const std::vector<ARow> rows = alRows();
	const int i = s_alScroll + row;
	if (i < 0 || i >= static_cast<int>(rows.size())) { return; }
	const ARow &r = rows[static_cast<size_t>(i)];
	std::string msg;
	switch (r.kind)
	{
	case AR_CLIP:
		if (right)
		{
			alBackup();
			s_def.poseCount[r.clip] = 0;
			for (Part &p : s_def.parts) { p.keys[r.clip].clear(); }
			s_dirty = true; s_animPlay = -1;
			setStatus(std::string("Removed: ") + clipTitle(r.clip) + " (Undo brings it back).");
		}
		else { s_animClip = r.clip; alPlay(r.clip); }
		break;
	case AR_KEYS:
		if (right)
		{
			alBackup();
			s_def.parts[static_cast<size_t>(r.part)].keys[r.clip].clear();
			s_dirty = true;
			setStatus("Removed from part " + std::to_string(r.part + 1) + " (Undo brings it back).");
		}
		else { selectPart(r.part); s_animClip = r.clip; alPlay(r.clip); }
		break;
	case AR_MOTION:
		if (right)
		{
			alBackup();
			s_def.parts[static_cast<size_t>(r.part)].motion = Motion();
			s_dirty = true;
			setStatus("Motion removed from part " + std::to_string(r.part + 1) + " (Undo brings it back).");
		}
		else
		{
			selectPart(r.part);
			const int w = s_def.parts[static_cast<size_t>(r.part)].motion.when;
			alPlay(w == WHEN_MOVING ? CLIP_MOVING : (w == WHEN_FIRING ? CLIP_FIRING : CLIP_STANDING));
		}
		break;
	case AR_GAIT:
		if (right) { return; }
		alBackup();
		if (modanimlib::applyGait(s_def, r.idx, msg)) { s_dirty = true; setStatus(msg); alPlay(CLIP_MOVING); play("mfui_clank.ogg"); }
		else { s_alHasUndo = false; setStatus(msg, true); }
		break;
	case AR_SINGLE:
		if (right) { return; }
		alBackup();
		if (modanimlib::applySingle(s_def, partSelected() ? animTarget() : -1, r.idx, msg)) { s_dirty = true; setStatus(msg); alPlay(modanimlib::singleClip(r.idx)); play("mfclick_move.ogg"); }
		else { s_alHasUndo = false; setStatus(msg, true); }
		break;
	case AR_SET:
	{
		if (r.idx < 0 || r.idx >= static_cast<int>(s_alSets.size())) { return; }
		const AnimSetInfo info = s_alSets[static_cast<size_t>(r.idx)];
		if (right)
		{
			if (s_alArmed != i) { s_alArmed = i; setStatus("Right-click again to delete your set \"" + info.name + "\".", true); return; }
			s_alArmed = -1;
			if (modcompbody::deleteAnimSet(info.path)) { setStatus("Set \"" + info.name + "\" deleted."); }
			else { setStatus("Could not delete the set.", true); }
			s_alSets = modcompbody::animSets();
			return;
		}
		Def set;
		if (!modcompbody::loadAnimSet(info.path, set)) { setStatus("Could not read the set.", true); return; }
		alBackup();
		if (modanimlib::applySet(s_def, set, msg)) { s_dirty = true; setStatus(msg); alPlay(s_def.poseCount[CLIP_MOVING] > 0 ? CLIP_MOVING : CLIP_STANDING); play("mfui_clank.ogg"); }
		else { s_alHasUndo = false; setStatus(msg, true); }
		break;
	}
	default: break;
	}
}

bool inShapePanel(int x, int y)
{
	const Ctx c = ctx();
	if (s_alOpen && x >= c.X(AL_X) && x <= c.X(AL_X + AL_W) && y >= c.Y(AL_Y) && y <= c.Y(AL_Y + AL_H)) { return true; }   // (27.11. 23:19) Animations-Liste
	if (s_animOpen && x >= c.X(AM_X) && x <= c.X(AM_X + AM_W) && y >= c.Y(AM_Y) && y <= c.Y(AM_Y + AM_H)) { return true; }   // (27.11.) Animations-Fenster
	if (s_partsOpen && x >= c.X(PM_X) && x <= c.X(PM_X + PM_W) && y >= c.Y(PM_Y) && y <= c.Y(PM_Y + PM_H)) { return true; }   // (27.11.) Parts-Fenster
	if (!s_shapeOpen) { return false; }
	return x >= c.X(SH_X) && x <= c.X(SH_X + SH_W) && y >= c.Y(SH_Y) && y <= c.Y(SH_Y + SH_H);
}

bool partSelected();
/// (27.11.) Farben eines anderen Rumpfes fuer das gewaehlte Teil (oder alle Teile); leere ID = eigene Farben
void setTexFrom(const std::string &bodyId, const std::string &label)
{
	if (s_def.parts.empty()) { setStatus("Add a hull first.", true); return; }
	if (!s_texAll && !partSelected()) { setStatus("Select a part first (or choose Whole hull).", true); return; }
	for (size_t i = 0; i < s_def.parts.size(); ++i)
	{
		if (!s_texAll && static_cast<int>(i) != s_sel) { continue; }
		Part &p = s_def.parts[i];
		p.texFrom = (bodyId.empty() || bodyId == p.body) ? std::string() : bodyId;
	}
	s_dirty = true;
	const std::string who = s_texAll ? "All parts" : "Part " + std::to_string(s_sel + 1);
	setStatus(bodyId.empty() ? who + ": own colours again" : who + " now wear the colours of " + label);
	play("mfui_clank.ogg");
}

void addMount()
{
	if (s_def.mounts.size() >= modcompbody::MAX_SLOTS) { setStatus("At most " + std::to_string(modcompbody::MAX_SLOTS) + " weapon slots.", true); return; }
	Mount m;
	m.pos = glm::vec3(0.f, std::round(topOfComposite()), 0.f);
	Link l;
	if (s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size())) { l.part = s_sel; }   // haengt am gewaehlten Teil
	addSlot(m, l);
	relink(s_def.mounts.size() - 1);
	selectMount(static_cast<int>(s_def.mounts.size()) - 1);
	s_dirty = true;
	setStatus("Weapon slot added - move it with the MOVE buttons.");
}

void newHull()
{
	s_def = Def();
	s_def.name = "New hull";
	s_links.clear();
	s_sel = -1; s_selMount = -1; s_selDrive = -1; s_drivePick = false; s_partScroll = 0; s_mountScroll = 0; s_loadIdx = -1;
	s_pivotEdit = false; s_parentPick = false; s_poseEdit = false; s_animPlay = -1; s_animPoseIdx = 0; s_multi.clear();   // (27.11.)
	s_alHasUndo = false;   // (27.11. 23:19)
	if (s_nameBox) { s_nameBox->setString(WzString::fromUtf8(s_def.name)); }
	s_dirty = false;
	setStatus("Click a hull on the left - the first one is the chassis.");
}

void loadCycle(int dir)
{
	std::vector<int> idx;
	const auto &all = modcompbody::defs();
	for (size_t i = 0; i < all.size(); ++i) { if (!all[i].deleted) { idx.push_back(static_cast<int>(i)); } }
	if (idx.empty()) { setStatus("No saved super hulls yet.", true); return; }
	int pos = 0;
	for (size_t k = 0; k < idx.size(); ++k) { if (idx[k] == s_loadIdx) { pos = static_cast<int>(k); break; } }
	if (s_loadIdx >= 0) { pos = (pos + dir + static_cast<int>(idx.size())) % static_cast<int>(idx.size()); }
	s_loadIdx = idx[static_cast<size_t>(pos)];
	s_def = all[static_cast<size_t>(s_loadIdx)];
	s_pivotEdit = false; s_parentPick = false; s_poseEdit = false; s_animPlay = -1; s_animPoseIdx = 0; s_multi.clear();   // (27.11.)
	s_alHasUndo = false;   // (27.11. 23:19)
	// alte Entwuerfe ohne eigene Plaetze: die des Fahrgestells (wie im Spiel)
	if (s_def.mounts.empty() && !s_def.parts.empty()) { s_def.mounts = modcompbody::defaultMounts(s_def.parts[0]); }
	inferLinks();
	s_sel = s_def.parts.empty() ? -1 : 0; s_selMount = -1; s_selDrive = -1; s_drivePick = false; s_partScroll = 0; s_mountScroll = 0;
	if (s_nameBox) { s_nameBox->setString(WzString::fromUtf8(s_def.name)); }
	s_dirty = false;
	setStatus(std::string("Loaded: ") + s_def.name + (s_def.preset ? " (preset)" : "") + "  (" + std::to_string(pos + 1) + "/" + std::to_string(idx.size()) + ")");
}

void doSave()
{
	if (s_nameBox) { s_def.name = s_nameBox->getString().toStdString(); }
	std::string err;
	const bool centred = modcompbody::centerDef(s_def);   // (Wunsch 27.11. 19:16) Grundriss mittig - passt im Einheiten-Designer
	for (size_t k = 0; k < s_def.mounts.size(); ++k) { s_def.mounts[k].part = linkValid(k) ? s_links[k].part : -1; }   // (27.11.) Animation
	const bool ok = modcompbody::save(s_def, err);
	if (!err.empty()) { setStatus(err, !ok); }
	else if (ok) { setStatus("Saved: \"" + s_def.name + (s_menu ? "\" - ready in every game." : "\" - ready to build.") + (centred ? " (centred)" : "")); }
	else { setStatus("Could not save.", true); }
	if (ok) { s_dirty = false; play("mfui_clank.ogg"); }
	const auto &all = modcompbody::defs();
	for (size_t i = 0; i < all.size(); ++i) { if (all[i].id == s_def.id) { s_loadIdx = static_cast<int>(i); break; } }
}

void doDeleteHull()
{
	if (s_def.id.empty()) { newHull(); return; }
	std::string err;
	if (modcompbody::remove(s_def.id, err)) { setStatus("Deleted: " + s_def.name); newHull(); }
	else { setStatus(err, true); }
}

void requestClose()
{
	if (s_dirty && !s_def.parts.empty() && !(s_closeArmed && realTime - s_closeArmedAt < 4000))
	{
		s_closeArmed = true;
		s_closeArmedAt = realTime;
		setStatus("Not saved! Click Close again to discard.", true);
		return;
	}
	s_closeArmed = false;
	close();
}

// ---------------------------------------------------------------- Zeichnen
/// WZ-Fensterrahmen (wie die Formulare im Spiel)
void wzFrame(int x, int y, int w, int h)
{
	RenderWindowFrame(FRAME_NORMAL, static_cast<uint32_t>(std::max(0, x)), static_cast<uint32_t>(std::max(0, y)), static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
}

/// Gruppen-Rahmen mit Ueberschrift
void groupFrame(const Ctx &c, float x, float y, float w, float h, const std::string &title)
{
	wzFrame(c.X(x), c.Y(y), c.L(w), c.L(h));
	wzText(title, c.X(x + 6), c.Y(y + 4), pal_RGBA(255, 214, 110, 255), TK_HEAD, c.S);
}

/// Listenfeld: dunkler Grund mit WZ-Kante
void listFrame(int x, int y, int w, int h)
{
	pie_UniTransBoxFill(x, y, x + w, y + h, pal_RGBA(0, 6, 20, 210));
	iV_Box(x, y, x + w - 1, y + h - 1, WZCOL_FORM_LIGHT);
}

const iIMDShape *nullTurret()
{
	static iIMDBaseShape *s = nullptr;
	static unsigned gen = 0;
	if (modelCacheStale(gen)) { s = modelGet("znullturret.pie"); }
	return s ? s->displayModel() : nullptr;
}

const iIMDShape *nullGun()
{
	static iIMDBaseShape *s = nullptr;
	static unsigned gen = 0;
	if (modelCacheStale(gen)) { s = modelGet("znullgun.pie"); }
	return s ? s->displayModel() : nullptr;
}

/// Bildpunkte je Modelleinheit in der Tiefe BAY_DEPTH (aus der UI-Projektion)
float pxPerUnit()
{
	const glm::mat4 &proj = pie_UIPerspectiveGet();
	auto ndcX = [&](float wx) { const glm::vec4 c = proj * glm::vec4(wx, 0.f, BAY_DEPTH, 1.f); return c.x / c.w; };
	return std::max(1e-4f, std::abs(ndcX(100.f) - ndcX(0.f)) * 0.5f * static_cast<float>(pie_GetVideoBufferWidth()) / 100.f);
}

/// kleines 3D-Bild eines Rumpfes, mittig bei (cx, cy), passend in boxW x boxH Bildpunkte
void drawThumb(const iIMDBaseShape *m, int cx, int cy, int boxW, int boxH, float yawDeg, int team)
{
	if (m == nullptr || m->displayModel() == nullptr) { return; }
	const float ext = std::max({ 8.f, static_cast<float>(std::abs(m->min.x)), static_cast<float>(std::abs(m->max.x)),
	                             static_cast<float>(std::abs(m->min.z)), static_cast<float>(std::abs(m->max.z)) });
	const float radius = std::max(ext, static_cast<float>(m->max.y - m->min.y) * 0.75f);
	const float midY = static_cast<float>(m->max.y + m->min.y) * 0.5f;
	const float sc = 0.44f * static_cast<float>(std::min(boxW, boxH)) / (pxPerUnit() * radius);
	pie_SetGeometricOffset(cx, cy);
	const glm::mat4 base = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, BAY_DEPTH))
	                       * glm::rotate(glm::mat4(1.f), glm::radians(-22.f), glm::vec3(1.f, 0.f, 0.f))
	                       * glm::rotate(glm::mat4(1.f), glm::radians(yawDeg), glm::vec3(0.f, 1.f, 0.f))
	                       * glm::scale(glm::mat4(1.f), glm::vec3(sc))
	                       * glm::translate(glm::mat4(1.f), glm::vec3(0.f, -midY, 0.f));
	pie_Draw3DShape(m->displayModel(), 0, team, WZCOL_WHITE, pie_BUTTON, 0, base, glm::mat4(1.f));
}

glm::mat4 bayBase(const Ctx &c, float &scaleOut)
{
	const float ext = compositeExtent();
	const float sc = 155.f * c.S / std::max(10.f, ext) * s_zoom;
	scaleOut = sc;
	return glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, BAY_DEPTH))
	       * glm::rotate(glm::mat4(1.f), glm::radians(-s_pitch), glm::vec3(1.f, 0.f, 0.f))
	       * glm::rotate(glm::mat4(1.f), glm::radians(s_yaw), glm::vec3(0.f, 1.f, 0.f))
	       * glm::scale(glm::mat4(1.f), glm::vec3(sc))
	       * glm::translate(glm::mat4(1.f), glm::vec3(0.f, -12.f, 0.f));
}

void setBayOffset(const Ctx &c)
{
	pie_SetGeometricOffset(c.X(BAY_X) + c.L(BAY_W) / 2 + static_cast<int>(s_panX), c.Y(BAY_Y) + c.L(BAY_H) / 2 + c.L(30) + static_cast<int>(s_panY));
}

void resetOffset()
{
	pie_SetGeometricOffset(static_cast<int>(screenWidth) / 2, static_cast<int>(screenHeight) / 2);
}

bool project(const glm::mat4 &base, const glm::vec3 &p, Vector2i &out)
{
	const glm::mat4 m = pie_UIPerspectiveGet() * base * glm::translate(glm::mat4(1.f), p);
	const Vector3i zero(0, 0, 0);
	return pie_RotateProjectWithPerspective(&zero, m, &out) > 0 && out.x != LONG_WAY;
}

/// Bildschirmlage aller Waffenplaetze (fuer Klicks); -1 = keiner in der Naehe
int mountAt(int mx, int my)
{
	const Ctx c = ctx();
	setBayOffset(c);
	float sc = 1.f;
	const glm::mat4 base = bayBase(c, sc);
	int best = -1;
	int bestD = std::max(8, c.L(9)) * std::max(8, c.L(9));
	for (size_t i = 0; i < s_def.mounts.size(); ++i)
	{
		Vector2i sp;
		if (!project(base, s_def.mounts[i].pos + glm::vec3(0.f, 3.f, 0.f), sp)) { continue; }
		const int d = (sp.x - mx) * (sp.x - mx) + (sp.y - my) * (sp.y - my);
		if (d < bestD) { bestD = d; best = static_cast<int>(i); }
	}
	resetOffset();
	return best;
}

/// (27.11.) Antriebsplatz unter dem Mauszeiger (Bildschirm-Huelle seines Laufwerks); -1 = keiner
int driveAt(int mx, int my)
{
	const Ctx c = ctx();
	setBayOffset(c);
	float sc = 1.f;
	const glm::mat4 base = bayBase(c, sc);
	int best = -1;
	long bestA = 0;
	for (size_t i = 0; i < s_def.drives.size(); ++i)
	{
		const modcompbody::Drive &dr = s_def.drives[i];
		const iIMDShape *sh = driveShape(dr);
		const glm::vec3 mn = sh != nullptr ? glm::vec3(sh->min.x, sh->min.y, sh->min.z) : glm::vec3(-6.f, 0.f, -6.f);
		const glm::vec3 mxv = sh != nullptr ? glm::vec3(sh->max.x, sh->max.y, sh->max.z) : glm::vec3(6.f, 8.f, 6.f);
		int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
		bool ok = true;
		for (int k = 0; k < 8 && ok; ++k)
		{
			const glm::vec3 v((k & 1) ? mxv.x : mn.x, (k & 2) ? mxv.y : mn.y, (k & 4) ? mxv.z : mn.z);
			Vector2i sp;
			ok = project(base, glm::vec3(modcompbody::driveMatrix(dr) * glm::vec4(v, 1.f)), sp);
			x0 = std::min(x0, sp.x); y0 = std::min(y0, sp.y); x1 = std::max(x1, sp.x); y1 = std::max(y1, sp.y);
		}
		if (!ok || mx < x0 || mx > x1 || my < y0 || my > y1) { continue; }
		const long a = static_cast<long>(x1 - x0) * static_cast<long>(y1 - y0);
		if (best < 0 || a < bestA) { best = static_cast<int>(i); bestA = a; }
	}
	resetOffset();
	return best;
}

/// Teil unter dem Mauszeiger (kleinste Bildschirm-Huelle, die den Punkt enthaelt); -1 = keines
int partAt(int mx, int my)
{
	const Ctx c = ctx();
	setBayOffset(c);
	float sc = 1.f;
	const glm::mat4 base = bayBase(c, sc);
	int best = -1;
	long bestA = 0;
	for (size_t i = 0; i < s_def.parts.size(); ++i)
	{
		glm::vec3 cr[8];
		if (!partCorners(s_def.parts[i], cr)) { continue; }
		int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
		bool ok = true;
		for (const glm::vec3 &v : cr)
		{
			Vector2i sp;
			if (!project(base, v, sp)) { ok = false; break; }
			x0 = std::min(x0, sp.x); y0 = std::min(y0, sp.y); x1 = std::max(x1, sp.x); y1 = std::max(y1, sp.y);
		}
		if (!ok || mx < x0 || mx > x1 || my < y0 || my > y1) { continue; }
		const long a = static_cast<long>(x1 - x0) * static_cast<long>(y1 - y0);
		if (best < 0 || a < bestA) { best = static_cast<int>(i); bestA = a; }
	}
	resetOffset();
	return best;
}

void drawBay(const Ctx &c)
{
	const int bx = c.X(BAY_X), by = c.Y(BAY_Y), bw = c.L(BAY_W), bh = c.L(BAY_H);
	wzFrame(bx, by, bw, bh);
	pie_UniTransBoxFill(bx + 4, by + 4, bx + bw - 4, by + bh - 4, pal_RGBA(0, 4, 14, 150));
	const int team = teamColour();
	setBayOffset(c);
	float sc = 1.f;
	const glm::mat4 base = bayBase(c, sc);
	const glm::mat4 view(1.f);
	// Bodenraster und Pfeil "vorn"
	{
		const float g = std::ceil(compositeExtent() / 16.f) * 16.f;
		for (float t = -g; t <= g + 0.1f; t += 16.f)
		{
			Vector2i a, b2;
			if (project(base, glm::vec3(t, 0.f, -g), a) && project(base, glm::vec3(t, 0.f, g), b2)) { iV_Line(a.x, a.y, b2.x, b2.y, pal_RGBA(40, 120, 170, 70)); }
			if (project(base, glm::vec3(-g, 0.f, t), a) && project(base, glm::vec3(g, 0.f, t), b2)) { iV_Line(a.x, a.y, b2.x, b2.y, pal_RGBA(40, 120, 170, 70)); }
		}
		Vector2i a, b2;
		if (project(base, glm::vec3(0.f, 0.f, -g * 0.6f), a) && project(base, glm::vec3(0.f, 0.f, -g - 10.f), b2)) { iV_Line(a.x, a.y, b2.x, b2.y, pal_RGBA(120, 255, 170, 200)); }
	}
	// (27.11.) Animation: Lage je Teil (Pose bearbeiten oder Vorschau); leer = Ruhelage
	const std::vector<glm::mat4> animW = bayAnimW();
	auto AW = [&animW](int i) { return (i >= 0 && i < static_cast<int>(animW.size())) ? animW[static_cast<size_t>(i)] : glm::mat4(1.f); };
	for (size_t i = 0; i < s_def.parts.size(); ++i)
	{
		const Part &p = s_def.parts[i];
		const BodyInfo *b = modcompbody::bodyInfo(p.body);
		if (b == nullptr || b->model == nullptr || b->model->displayModel() == nullptr) { continue; }
		const glm::mat4 pw = base * AW(static_cast<int>(i));
		const iIMDShape *flipped = modcompbody::bakedShape(p);   // (27.11.) gespiegelt, umgefaerbt und/oder geschnitten
		if (flipped != nullptr) { pie_Draw3DShape(flipped, 0, team, WZCOL_WHITE, pie_BUTTON, 0, pw * modcompbody::partMatrix(p, false), view); }
		else { pie_Draw3DShape(b->model->displayModel(), 0, team, WZCOL_WHITE, pie_BUTTON, 0, pw * modcompbody::partMatrix(p), view); }
	}
	// gewaehltes Teil: Rahmen ((27.11.) alle gewaehlten; das aktive pulsiert)
	for (int fsel : (partSelected() ? selSet() : std::vector<int>()))
	{
		glm::vec3 cr[8];
		if (fsel != s_sel)
		{
			if (!partCorners(s_def.parts[static_cast<size_t>(fsel)], cr)) { continue; }
			static const int edges2[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
			Vector2i sp2[8];
			bool ok2 = true;
			const glm::mat4 pw2 = base * AW(fsel);
			for (int k = 0; k < 8; ++k) { ok2 = ok2 && project(pw2, cr[k], sp2[k]); }
			if (ok2) { for (const auto &e : edges2) { iV_Line(sp2[e[0]].x, sp2[e[0]].y, sp2[e[1]].x, sp2[e[1]].y, pal_RGBA(255, 200, 90, 140)); } }
			continue;
		}
		if (partCorners(s_def.parts[static_cast<size_t>(s_sel)], cr))
		{
			const unsigned char a = static_cast<unsigned char>(170 + 80 * std::sin(static_cast<float>(realTime) * 0.008f));
			const PIELIGHT col = s_poseEdit && s_animOpen ? pal_RGBA(120, 220, 255, a) : pal_RGBA(255, 220, 90, a);
			static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
			Vector2i sp[8];
			bool ok = true;
			const glm::mat4 pw = base * AW(s_sel);
			for (int k = 0; k < 8; ++k) { ok = ok && project(pw, cr[k], sp[k]); }
			if (ok) { for (const auto &e : edges) { iV_Line(sp[e[0]].x, sp[e[0]].y, sp[e[1]].x, sp[e[1]].y, col); } }
		}
	}
	// (27.11.) Skelett: Drehpunkte und "haengt an"-Linien (nur mit offenem Animations-Fenster)
	if (s_animOpen)
	{
		for (size_t i = 0; i < s_def.parts.size(); ++i)
		{
			const Part &p = s_def.parts[i];
			const glm::vec3 pv = glm::vec3(AW(static_cast<int>(i)) * glm::vec4(modcompbody::pivotOf(p), 1.f));
			Vector2i a2;
			if (!project(base, pv, a2)) { continue; }
			if (p.parent >= 0 && p.parent < static_cast<int>(s_def.parts.size()))
			{
				const glm::vec3 qv = glm::vec3(AW(p.parent) * glm::vec4(modcompbody::pivotOf(s_def.parts[static_cast<size_t>(p.parent)]), 1.f));
				Vector2i b2;
				if (project(base, qv, b2)) { iV_Line(a2.x, a2.y, b2.x, b2.y, pal_RGBA(120, 220, 255, 190)); }
			}
			const bool sel = static_cast<int>(i) == s_sel && s_selMount < 0 && s_selDrive < 0;
			const int r = sel ? c.L(5) : c.L(2);
			const PIELIGHT pc = sel ? (s_pivotEdit ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(120, 255, 255, 255)) : pal_RGBA(120, 220, 255, 220);
			iV_Line(a2.x - r, a2.y, a2.x + r, a2.y, pc);
			iV_Line(a2.x, a2.y - r, a2.x, a2.y + r, pc);
			if (sel) { iV_Box(a2.x - r, a2.y - r, a2.x + r, a2.y + r, pc); }
		}
	}
	// (27.11.) Antriebsplaetze: das Laufwerk (Ketten) des gewaehlten Rumpfes; gewaehlter Platz mit gruenem Rahmen
	for (size_t i = 0; i < s_def.drives.size(); ++i)
	{
		const modcompbody::Drive &dr = s_def.drives[i];
		const glm::mat4 dm = base * modcompbody::driveMatrix(dr);
		const iIMDShape *sh = driveShape(dr);
		if (sh != nullptr) { pie_Draw3DShape(sh, 0, team, WZCOL_WHITE, pie_BUTTON, 0, dm, view); }
		const bool dsel = static_cast<int>(i) == s_selDrive;
		Vector2i sp;
		if (sh != nullptr && dsel)
		{
			const unsigned char a = static_cast<unsigned char>(170 + 80 * std::sin(static_cast<float>(realTime) * 0.008f));
			static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
			Vector2i cp[8];
			bool ok = true;
			for (int k = 0; k < 8 && ok; ++k)
			{
				const glm::vec3 v((k & 1) ? sh->max.x : sh->min.x, (k & 2) ? sh->max.y : sh->min.y, (k & 4) ? sh->max.z : sh->min.z);
				ok = project(base, glm::vec3(modcompbody::driveMatrix(dr) * glm::vec4(v, 1.f)), cp[k]);
			}
			if (ok) { for (const auto &e : edges) { iV_Line(cp[e[0]].x, cp[e[0]].y, cp[e[1]].x, cp[e[1]].y, pal_RGBA(120, 255, 170, a)); } }
		}
		if (project(base, dr.pos + glm::vec3(0.f, sh != nullptr ? static_cast<float>(sh->max.y) * dr.scale + 2.f : 4.f, 0.f), sp))
		{
			if (sh == nullptr) { pie_BoxFill(sp.x - 4, sp.y - 4, sp.x + 4, sp.y + 4, pal_RGBA(120, 255, 170, 255)); }
			wzText("D" + std::to_string(i + 1), sp.x + c.L(5), sp.y - c.L(9), dsel ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(140, 240, 180, 255), TK_ROW, c.S);
		}
	}
	// Waffenplaetze: der gelbe WZ-Platzhalter-Turm (znullturret + znullgun) in der Blickrichtung des Platzes
	const iIMDShape *nt = nullTurret(), *ng = nullGun();
	for (size_t i = 0; i < s_def.mounts.size(); ++i)
	{
		const Mount &m = s_def.mounts[i];
		const bool sel = static_cast<int>(i) == s_selMount;
		const glm::mat4 mbase = base * AW(linkValid(i) ? s_links[i].part : -1);   // (27.11.) folgt der Animation seines Teils
		const glm::mat4 mm = mbase * glm::translate(glm::mat4(1.f), m.pos) * glm::rotate(glm::mat4(1.f), glm::radians(-m.yaw), glm::vec3(0.f, 1.f, 0.f))
		                     * glm::rotate(glm::mat4(1.f), glm::radians(m.pitch), glm::vec3(1.f, 0.f, 0.f))   // (27.11.) geneigter Platz
		                     * glm::rotate(glm::mat4(1.f), glm::radians(m.roll), glm::vec3(0.f, 0.f, 1.f));
		if (nt != nullptr)
		{
			pie_Draw3DShape(nt, 0, team, WZCOL_WHITE, pie_BUTTON, 0, mm, view);
			glm::mat4 gm = mm;
			if (!nt->connectors.empty()) { gm *= glm::translate(glm::mat4(1.f), glm::vec3(nt->connectors[0].xzy())); }
			if (ng != nullptr) { pie_Draw3DShape(ng, 0, team, WZCOL_WHITE, pie_BUTTON, 0, gm, view); }
		}
		Vector2i sp;
		if (!project(mbase, m.pos + glm::vec3(0.f, 3.f, 0.f), sp)) { continue; }
		if (nt == nullptr)
		{
			const int r = sel ? 6 : 4;
			pie_BoxFill(sp.x - r, sp.y - r, sp.x + r, sp.y + r, pal_RGBA(255, 210, 40, 255));
		}
		// gewaehlt: Blickrichtung und die Grenzen des 180-Grad-Schwenkbereichs (Wunsch 23.11. 03:05)
		if (sel)
		{
			const glm::vec3 fwd = dirFromYaw(m.yaw), side(-fwd.z, 0.f, fwd.x);
			Vector2i a, l, rr;
			if (project(mbase, m.pos + fwd * 18.f, a)) { iV_Line(sp.x, sp.y, a.x, a.y, pal_RGBA(255, 240, 120, 255)); }
			if (project(mbase, m.pos + side * 14.f, rr) && project(mbase, m.pos - side * 14.f, l))
			{
				iV_Line(sp.x, sp.y, rr.x, rr.y, pal_RGBA(255, 240, 120, 120));
				iV_Line(sp.x, sp.y, l.x, l.y, pal_RGBA(255, 240, 120, 120));
			}
			const int r = c.L(9);
			iV_Box(sp.x - r, sp.y - r, sp.x + r, sp.y + r, pal_RGBA(255, 255, 255, 200));
		}
		wzText(std::to_string(i + 1), sp.x + c.L(7), sp.y - c.L(9), sel ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(255, 226, 120, 255), TK_ROW, c.S);
	}
	resetOffset();
	// Hinweis und Meldungen
	wzText("Drag: turn   Right drag: move   Wheel: zoom", bx + c.L(8), by + bh - c.L(14), pal_RGBA(150, 195, 225, 220), TK_ROW, c.S, bw - c.L(12));
	if (s_def.parts.empty()) { wzText("<  Click a hull on the left", bx + c.L(60), by + c.L(150), pal_RGBA(120, 230, 255, 255), TK_BUTTON, c.S); }
	if (!s_status.empty() && realTime < s_statusUntil)
	{
		const std::string t = fitW(s_status, bw - c.L(16 + CEN_W + 8), TK_ROW, c.S);   // (27.11.) Platz fuer den Zentrier-Knopf
		const int tw = textW(t, TK_ROW, c.S);
		pie_UniTransBoxFill(bx + c.L(6), by + c.L(6), bx + c.L(14) + tw, by + c.L(20), pal_RGBA(0, 0, 0, 150));
		wzText(t, bx + c.L(10), by + c.L(9), s_statusBad ? pal_RGBA(255, 130, 100, 255) : pal_RGBA(120, 255, 170, 255), TK_ROW, c.S);
	}
}

/// Rumpf-Liste links: Reiter (3D-Bild + Kurzname) und Rumpf-Knoepfe nach Fraktion
void drawHullList(const Ctx &c, int &hoverCat)
{
	const float S = c.S;
	const int team = teamColour();
	const int mx = mouseX(), my = mouseY();
	groupFrame(c, 2, 2, 162, 336, "HULLS");
	// Reiter
	for (size_t k = 0; k < s_sources.size(); ++k)
	{
		const float tx = TAB_X + static_cast<float>(k % TAB_COLS) * TAB_STEP_X, ty = TAB_Y + static_cast<float>(k / TAB_COLS) * TAB_STEP_Y;
		const int x0 = c.X(tx), y0 = c.Y(ty), w = c.L(TAB_W), h = c.L(TAB_H);
		const bool sel = static_cast<int>(k) == s_source;
		const bool hover = mx >= x0 && mx < x0 + w && my >= y0 && my < y0 + h;
		modwzhd::textButton(x0, y0, w, h, std::string(), sel, hover, false);
		const auto &cat = modcompbody::catalog();
		if (k < s_sourceRep.size() && s_sourceRep[k] < cat.size())
		{
			drawThumb(cat[s_sourceRep[k]].model, x0 + w / 2, y0 + c.L(11), w - c.L(4), c.L(18), (sel || hover) ? static_cast<float>(realTime % 7200u) * 0.05f : 215.f, team);
		}
		const std::string lab = fitW(shortSource(s_sources[k]), w - 2, TK_SMALL, S);
		wzText(lab, x0 + (w - textW(lab, TK_SMALL, S)) / 2, y0 + h - c.L(10), sel ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(190, 196, 236, 255), TK_SMALL, S);
	}
	if (subTabsShown())   // (27.11.) Unterreiter "Units | Buildings" rechts neben "HULLS"
	{
		for (int k = 0; k < 2; ++k)
		{
			const int x0 = c.X(k == 0 ? SUB_X0 : SUB_X1), y0 = c.Y(SUB_Y), w = c.L(k == 0 ? SUB_W0 : SUB_W1), h = c.L(SUB_H);
			const bool sel = s_scSub == k;
			const bool hover = mx >= x0 && mx < x0 + w && my >= y0 && my < y0 + h;
			modwzhd::textButton(x0, y0, w, h, std::string(), sel, hover, false);
			const std::string lab = k == 0 ? "Units" : "Buildings";
			wzText(lab, x0 + (w - textW(lab, TK_SMALL, S)) / 2, y0 + c.L(2), sel ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(190, 196, 236, 255), TK_SMALL, S);
		}
	}
	resetOffset();
	// Liste
	listFrame(c.X(LIST_X), c.Y(LIST_Y), c.L(LIST_W), c.L(LIST_H));
	std::vector<Cell> cells; std::vector<Head> heads;
	listLayout(cells, heads);
	for (const Head &h : heads)
	{
		wzText(h.text, c.X(LIST_X + 4), c.Y(h.y + 1), pal_RGBA(255, 214, 110, 255), TK_SMALL, S);
		const int lx = c.X(LIST_X + 8) + textW(h.text, TK_SMALL, S);
		pie_UniTransBoxFill(lx, c.Y(h.y + 7), c.X(LIST_X + LIST_W - 4), c.Y(h.y + 7) + 1, pal_RGBA(90, 130, 210, 170));
	}
	const auto &cat = modcompbody::catalog();
	for (const Cell &k : cells)
	{
		if (k.cat < 0 || static_cast<size_t>(k.cat) >= cat.size()) { continue; }
		const BodyInfo &b = cat[static_cast<size_t>(k.cat)];
		const int x0 = c.X(k.x), y0 = c.Y(k.y), w = c.L(CELL_W - 1), h = c.L(CELL_H - 2);
		const bool hover = mx >= x0 && mx < x0 + w && my >= y0 && my < y0 + h;
		if (hover) { hoverCat = k.cat; }
		modwzhd::textButton(x0, y0, w, h, std::string(), false, hover, hover && s_listPress == k.cat);
		const bool lock = locked(b);
		drawThumb(b.model, x0 + w / 2, y0 + c.L(19), w - c.L(6), c.L(34), hover ? static_cast<float>(realTime % 7200u) * 0.05f : 215.f, team);
		const std::string n = fitW(b.name, w - c.L(4), TK_SMALL, S);
		wzText(n, x0 + (w - textW(n, TK_SMALL, S)) / 2, y0 + h - c.L(11), lock ? pal_RGBA(150, 150, 170, 255) : (hover ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(200, 206, 240, 255)), TK_SMALL, S);
		if (lock)
		{
			resetOffset();
			drawIcon(FI_LOCK, x0 + w - c.L(14), y0 + c.L(2), c.L(12));
		}
	}
	resetOffset();
	if (s_items.empty()) { wzText("No hulls found", c.X(LIST_X + 8), c.Y(LIST_Y + 10), WZCOL_TEXT_MEDIUM, TK_ROW, S); }
	// Bildlauf-Anzeige
	const int maxS = maxListScroll();
	if (maxS > 0)
	{
		const float frac = static_cast<float>(s_listScroll) / static_cast<float>(maxS);
		const int ty = c.Y(LIST_Y + 2) + static_cast<int>(frac * static_cast<float>(c.L(LIST_H - 24)));
		pie_UniTransBoxFill(c.X(LIST_X + LIST_W - 3), ty, c.X(LIST_X + LIST_W - 1), ty + c.L(20), pal_RGBA(140, 200, 255, 200));
	}
}

/// Hinweis-Kasten am Mauszeiger fuer den Rumpf-Knopf darunter
void drawHullTip(const Ctx &c, int catIdx)
{
	const auto &cat = modcompbody::catalog();
	if (catIdx < 0 || static_cast<size_t>(catIdx) >= cat.size()) { return; }
	const BodyInfo &b = cat[static_cast<size_t>(catIdx)];
	char buf[160];
	snprintf(buf, sizeof(buf), "%s  -  %u %s  -  HP %u", sizeName(b.size), b.weaponSlots, b.weaponSlots == 1 ? "slot" : "slots", b.hp);
	const std::string l1 = b.name, l2 = buf, l3 = locked(b) ? "Needs research to build" : (s_def.parts.empty() ? "Click: use as chassis" : "Click: add");
	const int w = std::max({ textW(l1, TK_BUTTON, c.S), textW(l2, TK_ROW, c.S), textW(l3, TK_ROW, c.S) }) + c.L(12);
	const int h = c.L(40);
	int x = mouseX() + c.L(14), y = mouseY() + c.L(12);
	if (x + w > static_cast<int>(screenWidth)) { x = mouseX() - w - c.L(6); }
	if (y + h > static_cast<int>(screenHeight)) { y = mouseY() - h - c.L(6); }
	pie_UniTransBoxFill(x, y, x + w, y + h, pal_RGBA(6, 10, 30, 235));
	iV_Box(x, y, x + w - 1, y + h - 1, pal_RGBA(103, 103, 163, 255));
	wzText(l1, x + c.L(6), y + c.L(3), pal_RGBA(255, 255, 255, 255), TK_BUTTON, c.S);
	wzText(l2, x + c.L(6), y + c.L(16), pal_RGBA(190, 200, 235, 255), TK_ROW, c.S);
	wzText(l3, x + c.L(6), y + c.L(27), locked(b) ? pal_RGBA(255, 200, 90, 255) : pal_RGBA(120, 255, 170, 255), TK_ROW, c.S);
}

void drawScreen(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	(void)psWidget; (void)xOffset; (void)yOffset;
	const Ctx c = ctx();
	const float S = c.S;
	pie_BoxFill(0, 0, static_cast<int>(screenWidth), static_cast<int>(screenHeight), pal_RGBA(4, 8, 18, 255));

	// ---- links: Rumpfe
	int hoverCat = -1;
	drawHullList(c, hoverCat);

	// ---- rechts: Teile
	groupFrame(c, 476, 2, 162, 128, "PARTS");
	wzText("1 = chassis", c.X(536), c.Y(6), pal_RGBA(150, 195, 225, 230), TK_ROW, S);
	listFrame(c.X(480), c.Y(PARTS_Y), c.L(154), c.L(PART_ROWS * LIST_ROW_H + 2));
	for (int i = 0; i < PART_ROWS; ++i)
	{
		const int idx = s_partScroll + i;
		if (idx >= static_cast<int>(s_def.parts.size())) { break; }
		const int ry = c.Y(PARTS_Y + 1.f + i * LIST_ROW_H);
		const bool sel = idx == s_sel && s_selMount < 0 && s_selDrive < 0;
		const std::vector<int> cur = partSelected() ? selSet() : std::vector<int>();
		const bool inSel = !sel && std::find(cur.begin(), cur.end(), idx) != cur.end();   // (27.11.) mit Shift dazugenommen / Verbund
		if (sel) { pie_UniTransBoxFill(c.X(481), ry, c.X(633), ry + c.L(LIST_ROW_H) - 1, pal_RGBA(40, 110, 190, 170)); }
		else if (inSel) { pie_UniTransBoxFill(c.X(481), ry, c.X(633), ry + c.L(LIST_ROW_H) - 1, pal_RGBA(40, 90, 150, 110)); }
		int nSlots = 0;
		for (const Link &l : s_links) { if (l.part == idx) { ++nSlots; } }
		const Part &lp = s_def.parts[static_cast<size_t>(idx)];
		std::string t = std::to_string(idx + 1) + "  " + bodyName(lp.body) + (nSlots > 0 ? "  [" + std::to_string(nSlots) + "]" : "");
		if (lp.group != 0) { t += "  J" + std::to_string(lp.group); }
		if (lp.cut != 0) { t += "  cut"; }
		wzText(fitW(t, c.L(146), TK_ROW, S), c.X(484), ry + c.L(1), sel ? pal_RGBA(255, 255, 255, 255) : WZCOL_FORM_TEXT, TK_ROW, S);
	}

	// ---- rechts: Waffenplaetze
	const int nRows = static_cast<int>(s_def.mounts.size() + s_def.drives.size());   // (27.11.) danach die Antriebsplaetze
	if (nRows > 0) { s_mountScroll = std::max(0, std::min(s_mountScroll, nRows - MOUNT_ROWS)); }
	else { s_mountScroll = 0; }
	groupFrame(c, 476, SLOTS_HEAD_Y - 4, 162, 150, "SLOTS " + std::to_string(s_def.mounts.size()) + "/" + std::to_string(modcompbody::MAX_SLOTS)
	           + (s_def.drives.empty() ? std::string() : "  DRIVES " + std::to_string(s_def.drives.size())));
	listFrame(c.X(480), c.Y(SLOTS_Y), c.L(154), c.L(MOUNT_ROWS * LIST_ROW_H + 2));
	for (int r = 0; r < MOUNT_ROWS; ++r)
	{
		const int i = s_mountScroll + r;
		if (i >= nRows) { break; }
		const int ry = c.Y(SLOTS_Y + 1.f + r * LIST_ROW_H);
		if (i >= static_cast<int>(s_def.mounts.size()))
		{
			const int di = i - static_cast<int>(s_def.mounts.size());
			const bool dsel = di == s_selDrive;
			if (dsel) { pie_UniTransBoxFill(c.X(481), ry, c.X(633), ry + c.L(LIST_ROW_H) - 1, pal_RGBA(30, 140, 90, 170)); }
			const modcompbody::Drive &dr = s_def.drives[static_cast<size_t>(di)];
			std::string t = "Drive " + std::to_string(di + 1) + "  " + bodyName(driveBodyId(dr)) + " size";
			if (std::fabs(dr.scale - 1.f) > 1e-3f) { t += " " + std::to_string(static_cast<int>(std::lround(dr.scale * 100.f))) + "%"; }
			wzText(fitW(t, c.L(146), TK_ROW, S), c.X(484), ry + c.L(1), dsel ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(140, 240, 180, 255), TK_ROW, S);
			continue;
		}
		const bool sel = i == s_selMount;
		if (sel) { pie_UniTransBoxFill(c.X(481), ry, c.X(633), ry + c.L(LIST_ROW_H) - 1, pal_RGBA(150, 110, 20, 170)); }
		std::string t = "Slot " + std::to_string(i + 1);
		if (linkValid(static_cast<size_t>(i))) { t += "  on " + std::to_string(s_links[static_cast<size_t>(i)].part + 1) + " " + bodyName(s_def.parts[static_cast<size_t>(s_links[static_cast<size_t>(i)].part)].body); }
		else { t += "  (free)"; }
		wzText(fitW(t, c.L(146), TK_ROW, S), c.X(484), ry + c.L(1), sel ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(255, 226, 140, 255), TK_ROW, S);
	}

	// ---- rechts unten: Werte
	{
		groupFrame(c, 476, VALUES_Y - 2, 162, 58, "VALUES");
		const modcompbody::Totals t = modcompbody::totals(s_def);
		char buf[160];
		snprintf(buf, sizeof(buf), "HP %u   Armour %u/%u", t.hp, t.armour, t.thermal);
		wzText(buf, c.X(482), c.Y(VALUES_Y + 13), WZCOL_TEXT_BRIGHT, TK_ROW, S, c.L(150));
		snprintf(buf, sizeof(buf), "Weight %u   Engine %u", t.weight, t.power);
		wzText(buf, c.X(482), c.Y(VALUES_Y + 25), WZCOL_TEXT_BRIGHT, TK_ROW, S, c.L(150));
		snprintf(buf, sizeof(buf), "Cost %u  Build %u  %s", t.cost, t.buildPoints, sizeName(t.size));
		wzText(buf, c.X(482), c.Y(VALUES_Y + 37), WZCOL_TEXT_BRIGHT, TK_ROW, S, c.L(150));
	}

	drawBay(c);

	// ---- (24.11.) Form-Fenster (die Knoepfe zeichnen sich selbst darueber)
	if (s_shapeOpen)
	{
		pie_UniTransBoxFill(c.X(SH_X), c.Y(SH_Y), c.X(SH_X + SH_W), c.Y(SH_Y + SH_H), pal_RGBA(6, 12, 28, 235));
		groupFrame(c, SH_X, SH_Y, SH_W, SH_H, "SHAPE");
		const bool havePart = s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size());
		const std::string who = s_shapeAll ? "whole hull" : (havePart ? "part " + std::to_string(s_sel + 1) + " " + bodyName(s_def.parts[static_cast<size_t>(s_sel)].body) : "no part selected");
		wzText(fitW(who, c.L(SH_W - 60), TK_ROW, S), c.X(SH_X + 50), c.Y(SH_Y + 3), pal_RGBA(150, 195, 225, 230), TK_ROW, S);
		static const char *names[3] = { "Length", "Width", "Height" };
		static const int axes[3] = { 2, 0, 1 };
		for (int r = 0; r < 3; ++r)
		{
			const float ry = SH_Y + 34 + r * 18;
			wzText(names[r], c.X(SH_X + 6), c.Y(ry + 2), WZCOL_TEXT_BRIGHT, TK_ROW, S);
			std::string v = "-";
			if (!s_shapeAll && havePart)
			{
				char buf[32];
				snprintf(buf, sizeof(buf), "%d %%", static_cast<int>(std::lround(s_def.parts[static_cast<size_t>(s_sel)].stretch[axes[r]] * 100.f)));
				v = buf;
			}
			wzText(v, c.X(SH_X + 126) - textW(v, TK_ROW, S) / 2, c.Y(ry + 2), pal_RGBA(255, 226, 140, 255), TK_ROW, S);
		}
		// (27.11.) Schneiden
		wzText("Cut", c.X(SH_X + 6), c.Y(SH_Y + 94), WZCOL_TEXT_BRIGHT, TK_ROW, S);
		wzText("Angle", c.X(SH_X + 6), c.Y(SH_Y + 112), WZCOL_TEXT_BRIGHT, TK_ROW, S);
		wzText("Place", c.X(SH_X + 118), c.Y(SH_Y + 112), WZCOL_TEXT_BRIGHT, TK_ROW, S);
		if (!s_shapeAll && havePart && s_def.parts[static_cast<size_t>(s_sel)].cut != 0)
		{
			const Part &cp = s_def.parts[static_cast<size_t>(s_sel)];
			char cb[32];
			snprintf(cb, sizeof(cb), "%.0f\xc2\xb0", cp.cutAngle);
			wzText(cb, c.X(SH_X + 78) - textW(cb, TK_ROW, S) / 2, c.Y(SH_Y + 112), pal_RGBA(255, 226, 140, 255), TK_ROW, S);
			snprintf(cb, sizeof(cb), "%.1f", cp.cutPos);
			wzText(cb, c.X(SH_X + 188) - textW(cb, TK_ROW, S) / 2, c.Y(SH_Y + 112), pal_RGBA(255, 226, 140, 255), TK_ROW, S);
		}
		else if (s_shapeAll) { wzText("(cuts: one part at a time)", c.X(SH_X + 122), c.Y(SH_Y + 129), pal_RGBA(150, 195, 225, 200), TK_ROW, S); }
	}

	// ---- (27.11.) Parts-Fenster (die Knoepfe zeichnen sich selbst darueber)
	if (s_partsOpen)
	{
		pie_UniTransBoxFill(c.X(PM_X), c.Y(PM_Y), c.X(PM_X + PM_W), c.Y(PM_Y + PM_H), pal_RGBA(6, 12, 28, 235));
		groupFrame(c, PM_X, PM_Y, PM_W, PM_H, "PARTS");
		const bool havePart = s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size());
		const std::string who = s_texAll ? "whole hull" : (havePart ? "part " + std::to_string(s_sel + 1) + " " + bodyName(s_def.parts[static_cast<size_t>(s_sel)].body) : "no part selected");
		wzText(fitW(who, c.L(PM_W - 60), TK_ROW, S), c.X(PM_X + 50), c.Y(PM_Y + 3), pal_RGBA(150, 195, 225, 230), TK_ROW, S);
		std::string col = "-";
		if (havePart && !s_texAll)
		{
			const Part &p = s_def.parts[static_cast<size_t>(s_sel)];
			col = p.texFrom.empty() ? std::string("own") : bodyName(p.texFrom);
		}
		else if (s_texAll && !s_def.parts.empty())
		{
			std::set<std::string> used;
			for (const Part &p : s_def.parts) { used.insert(p.texFrom); }
			col = used.size() == 1 ? (s_def.parts[0].texFrom.empty() ? std::string("own") : bodyName(s_def.parts[0].texFrom)) : std::string("mixed");
		}
		wzText("Colours:", c.X(PM_X + 6), c.Y(PM_Y + 36), WZCOL_TEXT_BRIGHT, TK_ROW, S);
		wzText(fitW(col, c.L(PM_W - 64), TK_ROW, S), c.X(PM_X + 58), c.Y(PM_Y + 36), pal_RGBA(255, 226, 140, 255), TK_ROW, S);
		if (s_texPick) { wzText("click a hull on the left", c.X(PM_X + 6), c.Y(PM_Y + 72), pal_RGBA(120, 255, 170, 255), TK_ROW, S); }
		// (27.11.) Antriebsplaetze
		std::string dl = "Drives " + std::to_string(s_def.drives.size()) + "/" + std::to_string(modcompbody::MAX_DRIVES);
		if (driveSelected()) { dl += "   drive " + std::to_string(s_selDrive + 1) + ": " + bodyName(driveBodyId(s_def.drives[static_cast<size_t>(s_selDrive)])) + " size"; }
		else if (s_def.drives.empty()) { dl += "   (none: one drive under the chassis)"; }
		wzText(fitW(dl, c.L(PM_W - 12), TK_ROW, S), c.X(PM_X + 6), c.Y(PM_Y + 92), WZCOL_TEXT_BRIGHT, TK_ROW, S);
		if (s_drivePick) { wzText("click a hull on the left: its drive size", c.X(PM_X + 6), c.Y(PM_Y + 124), pal_RGBA(120, 255, 170, 255), TK_ROW, S); }
	}

	// ---- (27.11. 23:19) Animations-Liste (Knoepfe und Zeilen-Rahmen zeichnen sich selbst darueber)
	if (s_alOpen)
	{
		pie_UniTransBoxFill(c.X(AL_X), c.Y(AL_Y), c.X(AL_X + AL_W), c.Y(AL_Y + AL_H), pal_RGBA(6, 12, 28, 240));
		groupFrame(c, AL_X, AL_Y, AL_W, AL_H, "ANIMATIONS");
		const std::string hn = s_nameBox ? s_nameBox->getString().toStdString() : s_def.name;
		const std::string hf = fitW(hn, c.L(AL_W - 90), TK_ROW, S);
		wzText(hf, c.X(AL_X + AL_W - 6) - textW(hf, TK_ROW, S), c.Y(AL_Y + 3), pal_RGBA(150, 195, 225, 230), TK_ROW, S);
		listFrame(c.X(AL_X + 4), c.Y(AL_LIST_Y), c.L(AL_W - 8), c.L(AL_ROWS * AL_ROW_H + 2));
		const std::vector<ARow> rows = alRows();
		s_alScroll = std::clamp(s_alScroll, 0, std::max(0, static_cast<int>(rows.size()) - AL_ROWS));
		const PIELIGHT gold = pal_RGBA(255, 214, 110, 255), dim = pal_RGBA(150, 170, 205, 230);
		for (int i = 0; i < AL_ROWS; ++i)
		{
			const int ri = s_alScroll + i;
			if (ri >= static_cast<int>(rows.size())) { break; }
			const ARow &r = rows[static_cast<size_t>(ri)];
			const int ry = c.Y(AL_LIST_Y + 1.f + i * AL_ROW_H);
			const int x0 = c.X(AL_X + 5), x1 = c.X(AL_X + AL_W - 5);
			bool hi = false;
			if (r.kind == AR_CLIP) { hi = s_animPlay == r.clip; }
			else if (r.kind == AR_KEYS || r.kind == AR_MOTION) { hi = r.part == s_sel && s_selMount < 0 && s_selDrive < 0; }
			if (r.kind == AR_SET && ri == s_alArmed) { pie_UniTransBoxFill(x0, ry, x1, ry + c.L(AL_ROW_H) - 1, pal_RGBA(150, 90, 20, 170)); }
			else if (hi) { pie_UniTransBoxFill(x0, ry, x1, ry + c.L(AL_ROW_H) - 1, pal_RGBA(40, 110, 190, 170)); }
			const int bw = r.b.empty() ? 0 : textW(r.b, TK_ROW, S);
			if (bw > 0) { wzText(r.b, x1 - c.L(4) - bw, ry + c.L(1), dim, TK_ROW, S); }
			const bool head = r.kind == AR_CLIP || r.kind == AR_MOTHEAD || r.kind == AR_SECTION;
			const int ix = c.X(AL_X + ((r.kind == AR_KEYS || r.kind == AR_MOTION) ? 20.f : 9.f));
			const PIELIGHT col = head ? gold : (r.kind == AR_TEXT ? dim : WZCOL_FORM_TEXT);
			wzText(fitW(r.a, x1 - ix - bw - c.L(8), TK_ROW, S), ix, ry + c.L(1), col, TK_ROW, S);
		}
		const char *hint = s_alTab == 0 ? "Click: select   Right-click: remove" : "Click: put it on this hull   Right-click: delete own set";
		wzText(fitW(hint, c.L(AL_W - 12), TK_ROW, S), c.X(AL_X + 6), c.Y(AL_Y + AL_H - 34), dim, TK_ROW, S);
	}

	// ---- (27.11.) Animations-Fenster (die Knoepfe zeichnen sich selbst darueber)
	if (s_animOpen)
	{
		using namespace modcompbody;
		pie_UniTransBoxFill(c.X(AM_X), c.Y(AM_Y), c.X(AM_X + AM_W), c.Y(AM_Y + AM_H), pal_RGBA(6, 12, 28, 235));
		groupFrame(c, AM_X, AM_Y, AM_W, AM_H, "ANIMATE");
		const bool havePart = partSelected();
		const Part *p = havePart ? &s_def.parts[static_cast<size_t>(animTarget())] : nullptr;
		const std::string who = havePart ? "part " + std::to_string(animTarget() + 1) + " " + bodyName(p->body) + (p->group != 0 ? " (joined)" : "") : std::string("select a part");
		wzText(fitW(who, c.L(AM_W - 70), TK_ROW, S), c.X(AM_X + 66), c.Y(AM_Y + 3), pal_RGBA(150, 195, 225, 230), TK_ROW, S);
		const PIELIGHT lab = WZCOL_TEXT_BRIGHT, val = pal_RGBA(255, 226, 140, 255);
		wzText("Pivot", c.X(AM_X + 6), c.Y(AM_Y + 18), lab, TK_ROW, S);
		wzText("Hangs on", c.X(AM_X + 6), c.Y(AM_Y + 36), lab, TK_ROW, S);
		std::string par = "-";
		if (p != nullptr) { par = p->parent >= 0 && p->parent < static_cast<int>(s_def.parts.size()) ? std::to_string(p->parent + 1) + " " + bodyName(s_def.parts[static_cast<size_t>(p->parent)].body) : std::string("the hull"); }
		if (s_parentPick) { par = "click a part..."; }
		wzText(fitW(par, c.L(72), TK_ROW, S), c.X(AM_X + 54), c.Y(AM_Y + 36), s_parentPick ? pal_RGBA(120, 255, 170, 255) : val, TK_ROW, S);
		char buf[64];
		if (p != nullptr && p->motion.type != MOT_NONE)
		{
			const bool rotType = p->motion.type == MOT_LEG || p->motion.type == MOT_SWAY || p->motion.type == MOT_SPIN;
			static const char *const axR[3] = { "nod", "turn", "roll" }, *const axT[3] = { "side", "up", "fwd" };
			if (s_axisBtn < s_btns.size()) { s_btns[s_axisBtn].label = rotType ? axR[std::clamp(p->motion.axis, 0, 2)] : axT[std::clamp(p->motion.axis, 0, 2)]; }
			if (s_whenBtn < s_btns.size()) { s_btns[s_whenBtn].label = whenName(p->motion.when); }
			if (p->motion.type == MOT_SPIN) { snprintf(buf, sizeof(buf), "-"); }
			else { snprintf(buf, sizeof(buf), (p->motion.type == MOT_BOB || p->motion.type == MOT_PUMP) ? "%.0f" : "%.0f\xc2\xb0", p->motion.amount); }
			wzText(buf, c.X(AM_X + 165) - textW(buf, TK_ROW, S) / 2, c.Y(AM_Y + 72), val, TK_ROW, S);
			snprintf(buf, sizeof(buf), "%.2f", p->motion.speed);
			wzText(buf, c.X(AM_X + 68) - textW(buf, TK_ROW, S) / 2, c.Y(AM_Y + 90), val, TK_ROW, S);
			snprintf(buf, sizeof(buf), "%.2f", p->motion.phase);
			wzText(buf, c.X(AM_X + 170) - textW(buf, TK_ROW, S) / 2, c.Y(AM_Y + 90), val, TK_ROW, S);
		}
		else
		{
			if (s_axisBtn < s_btns.size()) { s_btns[s_axisBtn].label = "Axis"; }
			if (s_whenBtn < s_btns.size()) { s_btns[s_whenBtn].label = "When"; }
		}
		wzText("Speed", c.X(AM_X + 6), c.Y(AM_Y + 90), lab, TK_ROW, S);
		wzText("Phase", c.X(AM_X + 106), c.Y(AM_Y + 90), lab, TK_ROW, S);
		wzText("POSES", c.X(AM_X + 6), c.Y(AM_Y + 110), pal_RGBA(255, 214, 110, 255), TK_ROW, S);
		const int n = s_def.poseCount[s_animClip];
		snprintf(buf, sizeof(buf), n > 0 ? "Pose %d/%d" : "no poses", std::clamp(s_animPoseIdx + 1, 1, std::max(1, n)), n);
		wzText(buf, c.X(AM_X + 51) - textW(buf, TK_ROW, S) / 2, c.Y(AM_Y + 128), val, TK_ROW, S);
		wzText(s_animClip == CLIP_FIRING ? "Once" : "Loop", c.X(AM_X + 74), c.Y(AM_Y + 146), lab, TK_ROW, S);
		snprintf(buf, sizeof(buf), "%.1fs", s_def.poseSeconds[s_animClip]);
		wzText(buf, c.X(AM_X + 141) - textW(buf, TK_ROW, S) / 2, c.Y(AM_Y + 146), val, TK_ROW, S);
		std::string hint;
		if (s_pivotEdit) { hint = "Move buttons move the pivot (white cross)."; }
		else if (s_poseEdit) { hint = "Move/turn buttons change this pose of the part."; }
		else if (s_animPlay >= 0) { hint = "Playing - press the button again to stop."; }
		else { hint = "Parts hang on parts: children follow."; }
		wzText(fitW(hint, c.L(AM_W - 12), TK_ROW, S), c.X(AM_X + 6), c.Y(AM_Y + 167), pal_RGBA(150, 195, 225, 230), TK_ROW, S);
		wzText("Play", c.X(AM_X + 6), c.Y(AM_Y + 190), lab, TK_ROW, S);
	}

	// ---- Auswahl-Zeile
	{
		std::string t;
		char buf[200];
		if (driveSelected())   // (27.11.)
		{
			const modcompbody::Drive &dr = s_def.drives[static_cast<size_t>(s_selDrive)];
			snprintf(buf, sizeof(buf), "Drive %d (%s size)    x %.1f   y %.1f   z %.1f    turn %.0f    size %d %%", s_selDrive + 1, bodyName(driveBodyId(dr)).c_str(),
			         dr.pos.x, dr.pos.y, dr.pos.z, dr.yaw, static_cast<int>(std::lround(dr.scale * 100.f)));
			t = buf;
		}
		else if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
		{
			const glm::vec3 &p = s_def.mounts[static_cast<size_t>(s_selMount)].pos;
			const Mount &mt = s_def.mounts[static_cast<size_t>(s_selMount)];
			snprintf(buf, sizeof(buf), "Weapon slot %d    x %.1f   y %.1f   z %.1f    facing %.0f   nose %.0f   roll %.0f", s_selMount + 1, p.x, p.y, p.z, mt.yaw, mt.pitch, mt.roll);
			t = buf;
		}
		else if (s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size()))
		{
			const Part &p = s_def.parts[static_cast<size_t>(s_sel)];
			snprintf(buf, sizeof(buf), "Part %d %s    x %.1f   y %.1f   z %.1f    turn %.0f   nose %.0f   roll %.0f%s%s%s", s_sel + 1, bodyName(p.body).c_str(), p.pos.x, p.pos.y, p.pos.z, p.rot.x, p.rot.y, p.rot.z,
			         (p.flip & 1) ? "   flip X" : "", (p.flip & 2) ? "   flip Y" : "", (p.flip & 4) ? "   flip Z" : "");
			t = buf;
			if (glm::length(p.stretch - glm::vec3(1.f)) > 1e-3f)
			{
				snprintf(buf, sizeof(buf), "   shape L%d W%d H%d %%", static_cast<int>(std::lround(p.stretch.z * 100.f)), static_cast<int>(std::lround(p.stretch.x * 100.f)), static_cast<int>(std::lround(p.stretch.y * 100.f)));
				t += buf;
			}
		}
		else { t = "Nothing selected - click a part or a slot"; }
		wzText(fitW(t, c.L(628), TK_ROW, S), c.X(6), c.Y(339), pal_RGBA(255, 226, 140, 255), TK_ROW, S);
	}

	// ---- Gruppen unten
	groupFrame(c, 2, ROW_A, 204, 66, "MOVE");
	groupFrame(c, 206, ROW_A, 238, 66, "ROTATE");
	groupFrame(c, 444, ROW_A, 194, 66, "PART");
	groupFrame(c, 2, ROW_B, 442, 58, "SUPER HULL");
	if (s_dirty) { wzText("not saved", c.X(12), c.Y(ROW_B + 38), pal_RGBA(255, 190, 90, 255), TK_ROW, S); }
	groupFrame(c, 444, ROW_B, 194, 58, "VIEW");

	// Hinweis-Kasten fuer den Rumpf-Knopf unter der Maus (zuletzt, damit er ueber der Liste liegt)
	s_pendingTip = hoverCat;
	if (hoverCat >= 0) { drawHullTip(c, hoverCat); }
}

/// WZ-Knopf (Rahmen und Flaeche wie die Knoepfe des WZ-Einheiteneditors) mit grossem Sinnbild und kurzem Wort
void btnDisplay(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const size_t k = psWidget->UserData;
	if (k >= s_btns.size()) { return; }
	const Btn &bt = s_btns[k];
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const bool active = bt.active ? bt.active() : false;
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	modwzhd::textButton(x0, y0, w, h, std::string(), active, hl, down);
	const float S = ctx().S;
	const int pad = std::max(1, static_cast<int>(std::lround(2.f * S)));
	const bool haveIcon = bt.icon >= 0 && iconsReady();
	const PIELIGHT tc = (hl || active) ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(196, 202, 238, 255);
	const int dy = down ? 1 : 0;
	if (!haveIcon)
	{
		const TextKind tk = textW(bt.label, TK_BUTTON, S) > w - 2 ? TK_SMALL : TK_BUTTON;   // (24.11.) schmale Knoepfe
		WzText &t = cachedText(bt.label, fontFor(tk, S));
		t.render(static_cast<float>(x0 + std::max(1, (w - t.width()) / 2)), static_cast<float>(y0 + (h - t.lineSize()) / 2 - t.aboveBase() + dy), tc, 0.f, w - 2);
		return;
	}
	const unsigned char ia = (hl || active || down) ? 255 : 228;
	if (h >= w * 3 / 4)
	{
		// hoch: Sinnbild oben, Wort darunter (passt das Wort nicht, nur das Sinnbild)
		const std::string cap = bt.label.empty() ? std::string() : fitW(bt.label, w - 2, TK_SMALL, S);
		const bool showCap = !cap.empty() && cap.find("...") == std::string::npos;
		const int capH = showCap ? cachedText(cap, fontFor(TK_SMALL, S)).lineSize() : 0;
		const int sz = std::max(4, std::min(w - 2 * pad, h - capH - 2 * pad));
		drawIcon(bt.icon, x0 + (w - sz) / 2, y0 + pad + dy, sz, ia);
		if (showCap) { wzText(cap, x0 + (w - textW(cap, TK_SMALL, S)) / 2, y0 + h - capH - pad / 2 + dy, tc, TK_SMALL, S); }
	}
	else
	{
		// breit: Sinnbild links, Wort rechts daneben
		const int sz = std::max(4, h - 2 * pad);
		const std::string cap = bt.label.empty() ? std::string() : fitW(bt.label, w - sz - 3 * pad, TK_SMALL, S);
		const int tw = cap.empty() ? 0 : textW(cap, TK_SMALL, S);
		const int total = sz + (tw > 0 ? pad + tw : 0);
		const int ix = x0 + std::max(pad, (w - total) / 2);
		drawIcon(bt.icon, ix, y0 + pad + dy, sz, ia);
		if (tw > 0)
		{
			WzText &t = cachedText(cap, fontFor(TK_SMALL, S));
			wzText(cap, ix + sz + pad, y0 + (h - t.lineSize()) / 2 + dy, tc, TK_SMALL, S);
		}
	}
}

void hotDisplay(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int x1 = x0 + psWidget->width() - 1, y1 = y0 + psWidget->height() - 1;
	if ((b->getState() & WBUT_HIGHLIGHT) != 0) { iV_Box(x0, y0, x1, y1, WZCOL_FORM_HILITE); }
	if ((b->getState() & WBUT_DOWN) != 0) { pie_UniTransBoxFill(x0, y0, x1, y1, pal_RGBA(255, 255, 255, 50)); }
}

/// unsichtbar (die Reiter zeichnet drawHullList)
void noDisplay(WIDGET *, UDWORD, UDWORD) {}

// ---------------------------------------------------------------- Aufbau
/// onClick bekommt true bei Rechtsklick (nur wenn secondary gesetzt ist)
std::shared_ptr<W_BUTTON> addBtn(const std::shared_ptr<WIDGET> &parent, float x, float y, float w, float h, int icon, const std::string &label, const char *tip,
                                 std::function<void(bool)> onClick, std::function<bool()> active = nullptr, WIDGET_DISPLAY display = nullptr, bool secondary = false)
{
	Btn b;
	b.x = x; b.y = y; b.wdt = w; b.hgt = h; b.label = label; b.icon = icon; b.active = std::move(active);
	b.w = std::make_shared<W_BUTTON>();
	parent->attach(b.w);
	if (secondary) { b.w->style |= WBUT_SECONDARY; }
	b.w->displayFunction = display != nullptr ? display : btnDisplay;
	b.w->UserData = static_cast<UDWORD>(s_btns.size());
	if (tip != nullptr) { b.w->setTip(tip); }
	b.w->addOnClickHandler([onClick](W_BUTTON &btn) {
		const bool right = btn.getOnClickButtonPressed() == WKEY_SECONDARY;
		widgScheduleTask([onClick, right]() { onClick(right); });
	});
	s_btns.push_back(b);
	return b.w;
}

void addBtn0(const std::shared_ptr<WIDGET> &parent, float x, float y, float w, float h, int icon, const std::string &label, const char *tip, std::function<void()> onClick,
             std::function<bool()> active = nullptr)
{
	addBtn(parent, x, y, w, h, icon, label, tip, [onClick](bool) { onClick(); }, std::move(active));
}

void layout()
{
	if (!s_root) { return; }
	const Ctx c = ctx();
	for (Btn &b : s_btns) { b.w->setGeometry(c.X(b.x), c.Y(b.y), std::max(1, c.L(b.wdt)), std::max(1, c.L(b.hgt))); }
	if (s_nameBox) { s_nameBox->setGeometry(c.X(10), c.Y(ROW_B + 15), c.L(150), c.L(18)); }
	s_lastW = static_cast<int>(screenWidth);
	s_lastH = static_cast<int>(screenHeight);
}

bool partSelected()
{
	return s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size()) && s_selMount < 0 && s_selDrive < 0;
}

void buildScreen()
{
	auto screen = W_SCREEN::make();
	screen->psForm->hide();   // verborgene Wurzel = durchlaessig fuer Klicks (wie beim Combot-Designer)
	auto root = W_FULLSCREENOVERLAY_CLICKFORM::make();
	root->backgroundColor = pal_RGBA(0, 0, 0, 0);
	root->displayFunction = drawScreen;
	root->onClickedFunc = []() {};
	root->onCancelPressed = []() { widgScheduleTask([]() { if (s_texPick || s_drivePick || s_parentPick) { s_texPick = false; s_drivePick = false; s_parentPick = false; setStatus("Cancelled."); return; } requestClose(); }); };
	screen->psForm->attach(root);
	const std::shared_ptr<WIDGET> parent = root;
	s_btns.clear();

	// ---- Herkunfts-Reiter (gezeichnet von drawHullList)
	for (int k = 0; k < TAB_MAX; ++k)
	{
		const float tx = TAB_X + static_cast<float>(k % TAB_COLS) * TAB_STEP_X, ty = TAB_Y + static_cast<float>(k / TAB_COLS) * TAB_STEP_Y;
		auto b = addBtn(parent, tx, ty, TAB_W, TAB_H, -1, "", nullptr, [k](bool) {
			if (k < static_cast<int>(s_sources.size()) && k != s_source) { s_source = k; rebuildItems(); play("mfclick_move.ogg"); /* MOD: mfui_click.ogg gab es nie */ }
		}, nullptr, noDisplay);
		if (k < static_cast<int>(s_sources.size()))
		{
			const std::string tip = s_sources[static_cast<size_t>(k)] + " (" + std::to_string(s_sourceCount[static_cast<size_t>(k)]) + ")";
			b->setTip(tip);
		}
		else { b->hide(); }
	}
	for (int k = 0; k < 2; ++k)   // (27.11.) SupCom-Unterreiter (gezeichnet von drawHullList, nur im SupCom-Reiter aktiv)
	{
		addBtn(parent, k == 0 ? SUB_X0 : SUB_X1, SUB_Y, k == 0 ? SUB_W0 : SUB_W1, SUB_H, -1, "", nullptr, [k](bool) {
			if (subTabsShown() && k != s_scSub) { s_scSub = k; rebuildItems(); play("mfclick_move.ogg"); }
		}, nullptr, noDisplay);
	}
	// (Wunsch 27.11. 20:38 "noch einen zusaetzlichen schnellen Zentrier-Knopf, ohne das bisherige UI zu verschieben"): oben rechts in der Ansicht
	addBtn0(parent, CEN_X, CEN_Y, CEN_W, CEN_H, -1, "Centre", "Centre the hull on its base (saving does this too)", []() {
		if (s_def.parts.empty()) { setStatus("Nothing to centre yet.", true); return; }
		if (modcompbody::centerDef(s_def)) { s_dirty = true; setStatus("Centred on its base."); play("mfclick_move.ogg"); }
		else { setStatus("Already centred."); }
	});
	addBtn0(parent, 6, 317, 75, 18, FI_SCROLL_UP, "Up", "Scroll up", []() { s_listScroll = std::max(0, s_listScroll - 3); });
	addBtn0(parent, 85, 317, 75, 18, FI_SCROLL_DOWN, "Down", "Scroll down", []() { s_listScroll = std::min(maxListScroll(), s_listScroll + 3); });

	// ---- Teile (Links = waehlen, Rechts = entfernen)
	for (int i = 0; i < PART_ROWS; ++i)
	{
		addBtn(parent, 481, PARTS_Y + 1.f + i * LIST_ROW_H, 152, LIST_ROW_H, -1, "", "Click: select   Right-click: remove", [i](bool right) {
			const int idx = s_partScroll + i;
			if (idx >= static_cast<int>(s_def.parts.size())) { return; }
			if (right) { removePart(idx); } else { clickPart(idx); }   // (27.11.) clickPart: auch "haengt an" waehlen
		}, nullptr, hotDisplay, true);
	}
	// ---- Waffenplaetze (Links = waehlen, Rechts = entfernen)
	for (int i = 0; i < MOUNT_ROWS; ++i)
	{
		addBtn(parent, 481, SLOTS_Y + 1.f + i * LIST_ROW_H, 152, LIST_ROW_H, -1, "", "Click: select   Right-click: remove", [i](bool right) {
			const int idx = s_mountScroll + i;
			const int nm = static_cast<int>(s_def.mounts.size());
			if (idx >= nm)   // (27.11.) Antriebsplaetze unter den Waffenplaetzen
			{
				const int di = idx - nm;
				if (di >= static_cast<int>(s_def.drives.size())) { return; }
				if (right) { removeDrive(di); } else { selectDrive(di); }
				return;
			}
			if (right) { removeSlot(static_cast<size_t>(idx)); play("mfui_detach.ogg"); /* MOD: mfui_rattle.ogg gab es nie */ } else { selectMount(idx); }
		}, nullptr, hotDisplay, true);
	}
	addBtn0(parent, 598, SLOTS_HEAD_Y - 2, 17, 13, FI_SCROLL_UP, "", "Scroll up", []() { s_mountScroll = std::max(0, s_mountScroll - MOUNT_ROWS); });
	addBtn0(parent, 617, SLOTS_HEAD_Y - 2, 17, 13, FI_SCROLL_DOWN, "", "Scroll down", []() { s_mountScroll += MOUNT_ROWS; });
	const float sy = SLOTS_Y + MOUNT_ROWS * LIST_ROW_H + 5;
	addBtn0(parent, 480, sy, 50, 20, FI_SLOT_ADD, "Slot", "Add weapon slot", []() { addMount(); });
	addBtn0(parent, 532, sy, 50, 20, FI_SLOT_REMOVE, "Slot", "Remove weapon slot (or the selected drive)", []() {
		if (s_selMount >= 0 || driveSelected()) { deleteSelected(); } else if (!s_def.mounts.empty()) { removeSlot(s_def.mounts.size() - 1); }
	});
	addBtn0(parent, 584, sy, 50, 20, FI_PARTS, "Parts", "Part tools: edit parts, take the colours of another hull, drives (up to 8 propulsion slots)",
	        []() { s_selMount = -1; if (s_sel < 0 && !s_def.parts.empty()) { s_sel = 0; } partsShow(!s_partsOpen); play("mfclick_move.ogg"); }, []() { return s_partsOpen; });

	// ---- VERSCHIEBEN: Schrittweite (Kopfzeile) und sechs Richtungen
	static const char *stepLbl[3] = { "0.5", "2", "8" };
	static const char *stepTip[3] = { "Small steps (Shift: finest)", "Medium steps", "Big steps" };
	for (int k = 0; k < 3; ++k) { addBtn0(parent, 64.f + k * 47.f, ROW_A + 2, 45, 13, FI_STEP_S + k, stepLbl[k], stepTip[k], [k]() { s_step = k; }, [k]() { return s_step == k; }); }
	struct MB { int icon; const char *l; const char *tip; int axis; float sign; };
	static const MB mv[6] = {
		{ FI_LEFT, "Left", "Move left [Left]", 0, -1.f }, { FI_RIGHT, "Right", "Move right [Right]", 0, 1.f },
		{ FI_DOWN, "Down", "Move down [PgDn]", 1, -1.f }, { FI_UP, "Up", "Move up [PgUp]", 1, 1.f },
		{ FI_FWD, "Fwd", "Move forward [Up]", 2, -1.f }, { FI_BACK, "Back", "Move back [Down]", 2, 1.f },
	};
	for (int k = 0; k < 6; ++k) { const MB m = mv[k]; addBtn0(parent, 6.f + k * 33.f, ROW_A + 18, 31, 44, m.icon, m.l, m.tip, [m]() { move(m.axis, m.sign); }); }
	// ---- DREHEN
	static const char *angLbl[3] = { "5", "15", "90" };
	static const char *angTip[3] = { "Turn 5 degrees (Shift: 1)", "Turn 15 degrees", "Turn 90 degrees" };
	for (int k = 0; k < 3; ++k) { addBtn0(parent, 290.f + k * 51.f, ROW_A + 2, 49, 13, FI_ANG_S + k, angLbl[k], angTip[k], [k]() { s_angle = k; }, [k]() { return s_angle == k; }); }
	static const MB rt[6] = {
		{ FI_YAW_L, "Turn", "Turn left [Q]", 0, 1.f }, { FI_YAW_R, "Turn", "Turn right [E]", 0, -1.f },
		{ FI_PITCH_DOWN, "Nose", "Nose down [F]", 1, -1.f }, { FI_PITCH_UP, "Nose", "Nose up [R]", 1, 1.f },
		{ FI_ROLL_L, "Roll", "Roll left [Z]", 2, -1.f }, { FI_ROLL_R, "Roll", "Roll right [C]", 2, 1.f },
	};
	for (int k = 0; k < 6; ++k) { const MB m = rt[k]; addBtn0(parent, 210.f + k * 39.f, ROW_A + 18, 37, 44, m.icon, m.l, m.tip, [m]() { rotate(m.axis, m.sign); }); }
	// ---- TEIL ((24.11.) acht Knoepfe: dazu "Form" fuer das Form-Fenster)
	constexpr float PW = 22.5f, PS = 23.5f;
	addBtn0(parent, 448 + 0 * PS, ROW_A + 14, PW, 48, FI_COPY, "Copy", "Copy part", []() { duplicatePart(false); });
	addBtn0(parent, 448 + 1 * PS, ROW_A + 14, PW, 48, FI_MIRROR, "Mirror", "Copy to the other side", []() { duplicatePart(true); });
	addBtn0(parent, 448 + 2 * PS, ROW_A + 14, PW, 48, FI_REMOVE, "Del", "Remove [Del]", []() { deleteSelected(); });
	addBtn0(parent, 448 + 3 * PS, ROW_A + 14, PW, 48, FI_RESET, "Reset", "Reset rotation", []() { resetSelected(); });
	addBtn0(parent, 448 + 4 * PS, ROW_A + 14, PW, 48, FI_FLIP_X, "X", "Flip left/right", []() { flipSelected(1); }, []() { return partSelected() && (s_def.parts[static_cast<size_t>(s_sel)].flip & 1); });
	addBtn0(parent, 448 + 5 * PS, ROW_A + 14, PW, 48, FI_FLIP_Y, "Y", "Flip upside down", []() { flipSelected(2); }, []() { return partSelected() && (s_def.parts[static_cast<size_t>(s_sel)].flip & 2); });
	addBtn0(parent, 448 + 6 * PS, ROW_A + 14, PW, 48, FI_FLIP_Z, "Z", "Flip front/back", []() { flipSelected(4); }, []() { return partSelected() && (s_def.parts[static_cast<size_t>(s_sel)].flip & 4); });
	addBtn0(parent, 448 + 7 * PS, ROW_A + 14, PW, 48, -1, "Form", "Shape: make the part (or the whole hull) longer, wider, thicker or thinner",
	        []() { shapeShow(!s_shapeOpen); play("mfclick_move.ogg"); }, []() { return s_shapeOpen; });
	// ---- (24.11.) Form-Fenster (anfangs verborgen)
	{
		const size_t first = s_btns.size();
		addBtn0(parent, SH_X + 4, SH_Y + 16, 44, 14, -1, "Part", "Change the selected part", []() { s_shapeAll = false; }, []() { return !s_shapeAll; });
		addBtn0(parent, SH_X + 50, SH_Y + 16, 70, 14, -1, "Whole hull", "Change the whole hull (all parts and their places)", []() { s_shapeAll = true; }, []() { return s_shapeAll; });
		static const char *stLbl[3] = { "5%", "10%", "25%" };
		for (int k = 0; k < 3; ++k) { addBtn0(parent, SH_X + 150 + k * 32.f, SH_Y + 16, 30, 14, -1, stLbl[k], "Step size", [k]() { s_shapeStep = k; }, [k]() { return s_shapeStep == k; }); }
		static const int axes[3] = { 2, 0, 1 };
		static const char *minusTip[3] = { "Shorter", "Narrower", "Thinner (lower)" };
		static const char *plusTip[3] = { "Longer", "Wider", "Thicker (higher)" };
		for (int r = 0; r < 3; ++r)
		{
			const int ax = axes[r];
			addBtn0(parent, SH_X + 70, SH_Y + 34 + r * 18.f, 28, 16, -1, "-", minusTip[r], [ax]() { shapeChange(ax, -1.f); });
			addBtn0(parent, SH_X + 154, SH_Y + 34 + r * 18.f, 28, 16, -1, "+", plusTip[r], [ax]() { shapeChange(ax, 1.f); });
		}
		// (Wunsch 27.11. 21:26) Schneiden: laengs / quer / waagerecht, Winkel, Lage, andere Haelfte
		static const char *const cutL[4] = { "Off", "Length", "Width", "Flat" };
		static const char *const cutTip[4] = { "Whole part (no cut)", "Cut lengthwise: left or right half stays", "Cut across: front or back half stays", "Cut flat: top or bottom half stays" };
		for (int k = 0; k < 4; ++k)
		{
			addBtn0(parent, SH_X + 40 + k * 48.f, SH_Y + 92, 46, 14, -1, cutL[k], cutTip[k], [k]() { cutSet(k); },
			        [k]() { return !s_shapeAll && partSelected() && s_def.parts[static_cast<size_t>(s_sel)].cut == k; });
		}
		addBtn0(parent, SH_X + 40, SH_Y + 110, 20, 14, -1, "-", "Turn the cut (15 degrees, Shift: 5)", []() { cutChange(0, -1.f); });
		addBtn0(parent, SH_X + 96, SH_Y + 110, 20, 14, -1, "+", "Turn the cut (15 degrees, Shift: 5)", []() { cutChange(0, 1.f); });
		addBtn0(parent, SH_X + 150, SH_Y + 110, 20, 14, -1, "-", "Move the cut (Shift: fine)", []() { cutChange(1, -1.f); });
		addBtn0(parent, SH_X + 206, SH_Y + 110, 20, 14, -1, "+", "Move the cut (Shift: fine)", []() { cutChange(1, 1.f); });
		addBtn0(parent, SH_X + 40, SH_Y + 127, 76, 14, -1, "Other half", "Keep the other half", []() { cutChange(2, 1.f); },
		        []() { return !s_shapeAll && partSelected() && s_def.parts[static_cast<size_t>(s_sel)].cutFlip; });
		addBtn0(parent, SH_X + 4, SH_Y + 144, 60, 15, -1, "Reset", "Back to the original shape (also undoes the cut)", []() { shapeReset(); });
		addBtn0(parent, SH_X + SH_W - 64, SH_Y + 144, 60, 15, -1, "Done", "Close the shape window", []() { shapeShow(false); });
		s_shapeBtns.clear();
		for (size_t k = first; k < s_btns.size(); ++k) { s_shapeBtns.push_back(k); }
	}
	// ---- (27.11.) Parts-Fenster (anfangs verborgen)
	{
		const size_t first = s_btns.size();
		addBtn0(parent, PM_X + 4, PM_Y + 16, 44, 14, -1, "Part", "Colours for the selected part", []() { s_texAll = false; }, []() { return !s_texAll; });
		addBtn0(parent, PM_X + 50, PM_Y + 16, 70, 14, -1, "Whole hull", "Colours for all parts", []() { s_texAll = true; }, []() { return s_texAll; });
		addBtn0(parent, PM_X + 4, PM_Y + 52, 96, 16, -1, "From hull...", "Then click a hull on the left: the part takes its colours (Esc cancels)", []() {
			if (s_def.parts.empty()) { setStatus("Add a hull first.", true); return; }
			s_texPick = !s_texPick;
			s_drivePick = false;
			setStatus(s_texPick ? std::string("Click a hull on the left - its colours go onto ") + (s_texAll ? "all parts" : "part " + std::to_string(s_sel + 1)) + " (Esc cancels)" : "Cancelled.");
		}, []() { return s_texPick; });
		addBtn0(parent, PM_X + 104, PM_Y + 52, 60, 16, -1, "Own", "Back to the part's own colours", []() { s_texPick = false; setTexFrom(std::string(), std::string()); });
		// (Wunsch 27.11. 19:16) Antriebsplaetze: anfuegen, Groesse von einem Rumpf, kleiner/groesser
		addBtn0(parent, PM_X + 4, PM_Y + 106, 54, 16, -1, "+ Drive", "Add a drive slot (up to 8; with drives the hull stands on them instead of one drive under the chassis)", []() { addDrive(); });
		addBtn0(parent, PM_X + 60, PM_Y + 106, 84, 16, -1, "Size from hull...", "Then click a hull on the left: the selected drive gets its drive size (Esc cancels)", []() {
			if (!driveSelected()) { setStatus("Select a drive first (SLOTS list or click it).", true); return; }
			s_drivePick = !s_drivePick;
			s_texPick = false;
			setStatus(s_drivePick ? "Click a hull on the left - drive " + std::to_string(s_selDrive + 1) + " takes its drive size (Esc cancels)" : std::string("Cancelled."));
		}, []() { return s_drivePick; });
		addBtn0(parent, PM_X + 146, PM_Y + 106, 27, 16, -1, "-", "Selected drive smaller", []() { scaleDrive(1.f / 1.1f); });
		addBtn0(parent, PM_X + 175, PM_Y + 106, 27, 16, -1, "+", "Selected drive bigger", []() { scaleDrive(1.1f); });
		addBtn0(parent, PM_X + 4, PM_Y + 137, 70, 16, -1, "Edit parts", "Back to moving and turning the parts (instead of a weapon slot or drive)", []() {
			s_selMount = -1; s_selDrive = -1; if (s_sel < 0 && !s_def.parts.empty()) { s_sel = 0; }
		});
		addBtn0(parent, PM_X + PM_W - 64, PM_Y + 137, 60, 16, -1, "Done", "Close the parts window", []() { partsShow(false); });
		s_partsBtns.clear();
		for (size_t k = first; k < s_btns.size(); ++k) { s_partsBtns.push_back(k); }
	}
	// ---- (Wunsch 27.11. 21:27 "Parts joinen wie in Blender") Verbinden/Trennen
	addBtn0(parent, ANB_X, JNB_Y, ANB_W, ANB_H, -1, "Join", "Join the selected parts into one piece (Shift+click adds parts) [Ctrl+J]", []() { joinSelected(); });
	addBtn0(parent, ANB_X, SPB_Y, ANB_W, ANB_H, -1, "Separate", "Split a joined piece into its parts again [P]", []() { separateSelected(); });
	// ---- (Wunsch 27.11. 23:19) Animations-Liste (anfangs verborgen)
	addBtn0(parent, ANB_X, ALB_Y, ANB_W, ANB_H, -1, "Anim list", "All animations of this hull, and ready-made ones to put on it (Library)",
	        []() { alistShow(!s_alOpen); play("mfclick_move.ogg"); }, []() { return s_alOpen; });
	{
		using namespace modcompbody;
		size_t first = s_btns.size();
		addBtn0(parent, AL_X + 6, AL_Y + 16, 62, 14, -1, "On this hull", "What this hull has - click: select, right-click: remove",
		        []() { s_alTab = 0; s_alScroll = 0; s_alArmed = -1; alistRefresh(); }, []() { return s_alTab == 0; });
		addBtn0(parent, AL_X + 70, AL_Y + 16, 50, 14, -1, "Library", "Ready-made animations and your own sets - click puts them on this hull",
		        []() { s_alTab = 1; s_alScroll = 0; s_alArmed = -1; alistRefresh(); }, []() { return s_alTab == 1; });
		static const char *const alPl[CLIP_COUNT] = { "Move", "Stand", "Fire" };
		for (int k = 0; k < CLIP_COUNT; ++k)
		{
			addBtn0(parent, AL_X + 142 + k * 32.f, AL_Y + 16, 30, 14, -1, alPl[k], "Play the animation as if the unit moves / stands / fires",
			        [k]() { if (s_animPlay == k) { s_animPlay = -1; } else { alPlay(k); } }, [k]() { return s_animPlay == k; });
		}
		for (int i = 0; i < AL_ROWS; ++i)
		{
			addBtn(parent, AL_X + 5, AL_LIST_Y + 1.f + i * AL_ROW_H, AL_W - 10, AL_ROW_H, -1, "", "Click: select / put on the hull   Right-click: remove",
			       [i](bool right) { alClick(i, right); }, nullptr, hotDisplay, true);
		}
		addBtn0(parent, AL_X + AL_W - 82, AL_Y + AL_H - 19, 20, 15, FI_SCROLL_UP, "", "Scroll up", []() { s_alScroll = std::max(0, s_alScroll - (AL_ROWS - 2)); });
		addBtn0(parent, AL_X + AL_W - 60, AL_Y + AL_H - 19, 20, 15, FI_SCROLL_DOWN, "", "Scroll down", []() {
			s_alScroll = std::clamp(s_alScroll + (AL_ROWS - 2), 0, std::max(0, static_cast<int>(alRows().size()) - AL_ROWS)); });
		addBtn0(parent, AL_X + AL_W - 38, AL_Y + AL_H - 19, 34, 15, -1, "Done", "Close the list", []() { alistShow(false); });
		s_alBtns.clear();
		for (size_t k = first; k < s_btns.size(); ++k) { s_alBtns.push_back(k); }
		first = s_btns.size();
		addBtn0(parent, AL_X + 4, AL_Y + AL_H - 19, 56, 15, -1, "Save as set", "Keep the whole animation of this hull as your own set (Library, \"Your sets\")", []() {
			std::string err;
			const std::string nm = s_nameBox ? s_nameBox->getString().toStdString() : s_def.name;
			if (modcompbody::saveAnimSet(s_def, nm, err)) { s_alSets = modcompbody::animSets(); setStatus("Saved as set \"" + nm + "\" - in the Library under \"Your sets\"."); play("mfui_clank.ogg"); }
			else { setStatus(err, true); }
		});
		addBtn0(parent, AL_X + 62, AL_Y + AL_H - 19, 50, 15, -1, "Remove all", "Take every animation off this hull (Undo brings it back)", []() {
			alBackup(); modanimlib::clearAll(s_def); s_dirty = true; s_animPlay = -1; setStatus("All animations removed (Undo brings them back).");
		});
		addBtn0(parent, AL_X + 114, AL_Y + AL_H - 19, 34, 15, -1, "Undo", "Undo the last change made in this list", []() { alUndo(); });
		s_alHullBtns.clear();
		for (size_t k = first; k < s_btns.size(); ++k) { s_alHullBtns.push_back(k); }
		first = s_btns.size();
		addBtn0(parent, AL_X + 4, AL_Y + AL_H - 19, 34, 15, -1, "Undo", "Undo the last change made in this list", []() { alUndo(); });
		s_alLibBtns.clear();
		for (size_t k = first; k < s_btns.size(); ++k) { s_alLibBtns.push_back(k); }
	}
	// ---- (Wunsch 20:56 "A+B") Animations-Fenster (anfangs verborgen)
	addBtn0(parent, ANB_X, ANB_Y, ANB_W, ANB_H, -1, "Animate", "Animate the parts: pivot, what each part hangs on, motions and poses",
	        []() { if (s_sel < 0 && !s_def.parts.empty()) { s_sel = 0; } s_selMount = -1; s_selDrive = -1; animShow(!s_animOpen); play("mfclick_move.ogg"); }, []() { return s_animOpen; });
	{
		using namespace modcompbody;
		const size_t first = s_btns.size();
		auto selP = []() -> Part * { return partSelected() ? &s_def.parts[static_cast<size_t>(animTarget())] : nullptr; };
		// Drehpunkt
		static const char *const pvl[3] = { "Bottom", "Middle", "Top" };
		for (int k = 0; k < 3; ++k)
		{
			addBtn0(parent, AM_X + 40 + k * 42.f, AM_Y + 16, 40, 14, -1, pvl[k], "Put the pivot (the joint the part turns around) there", [k, selP]() {
				if (Part *p = selP()) { p->pivot = pivotPreset(*p, k); s_dirty = true; setStatus(std::string("Pivot: ") + pvl[k] + "."); }
			});
		}
		addBtn0(parent, AM_X + 166, AM_Y + 16, 36, 14, -1, "Move", "Move the pivot with the move buttons (Left/Right/Up/Down/Fwd/Back)", []() {
			s_pivotEdit = !s_pivotEdit; if (s_pivotEdit) { s_poseEdit = false; }
			setStatus(s_pivotEdit ? "Pivot: use the move buttons - the white cross is the joint." : "Pivot set.");
		}, []() { return s_pivotEdit; });
		// haengt an
		addBtn0(parent, AM_X + 128, AM_Y + 34, 36, 14, -1, "Pick", "Then click the part it hangs on (list on the right or in the view); Esc cancels", []() {
			if (!partSelected()) { setStatus("Select a part first.", true); return; }
			s_parentPick = !s_parentPick;
			setStatus(s_parentPick ? "Click the part that part " + std::to_string(s_sel + 1) + " hangs on (Esc cancels)." : std::string("Cancelled."));
		}, []() { return s_parentPick; });
		addBtn0(parent, AM_X + 166, AM_Y + 34, 36, 14, -1, "None", "Hangs on the hull itself", []() { if (partSelected()) { setParent(animTarget(), -1); } });
		// Bewegungs-Bausteine
		for (int k = 0; k < MOT_COUNT; ++k)
		{
			addBtn0(parent, AM_X + 4 + k * 33.5f, AM_Y + 52, 32, 14, -1, motionName(k), "Motion of the selected part", [k]() { animSetMotion(k); },
			        [k, selP]() { const Part *p = selP(); return p != nullptr && p->motion.type == k; });
		}
		s_axisBtn = s_btns.size();
		addBtn0(parent, AM_X + 4, AM_Y + 70, 48, 14, -1, "Axis", "Axis of the motion (turn around / slide along)", [selP]() {
			if (Part *p = selP()) { p->motion.axis = (p->motion.axis + 1) % 3; s_dirty = true; }
		});
		s_whenBtn = s_btns.size();
		addBtn0(parent, AM_X + 54, AM_Y + 70, 72, 14, -1, "When", "When the motion plays", [selP]() {
			if (Part *p = selP()) { p->motion.when = (p->motion.when + 1) % WHEN_COUNT; s_dirty = true; }
		});
		auto amountStep = [](const Part &p) { return (p.motion.type == MOT_BOB || p.motion.type == MOT_PUMP) ? 1.f : 5.f; };
		addBtn0(parent, AM_X + 128, AM_Y + 70, 20, 14, -1, "-", "Smaller motion", [selP, amountStep]() { if (Part *p = selP()) { p->motion.amount -= amountStep(*p); s_dirty = true; } });
		addBtn0(parent, AM_X + 182, AM_Y + 70, 20, 14, -1, "+", "Bigger motion", [selP, amountStep]() { if (Part *p = selP()) { p->motion.amount += amountStep(*p); s_dirty = true; } });
		addBtn0(parent, AM_X + 36, AM_Y + 88, 20, 14, -1, "-", "Slower", [selP]() { if (Part *p = selP()) { p->motion.speed /= 1.25f; s_dirty = true; } });
		addBtn0(parent, AM_X + 80, AM_Y + 88, 20, 14, -1, "+", "Faster", [selP]() { if (Part *p = selP()) { p->motion.speed *= 1.25f; s_dirty = true; } });
		addBtn0(parent, AM_X + 138, AM_Y + 88, 20, 14, -1, "-", "Phase: start earlier in the beat (0.5 = the opposite leg)", [selP]() {
			if (Part *p = selP()) { p->motion.phase = std::fmod(p->motion.phase + 0.75f, 1.f); s_dirty = true; } });
		addBtn0(parent, AM_X + 182, AM_Y + 88, 20, 14, -1, "+", "Phase: start later in the beat (0.5 = the opposite leg)", [selP]() {
			if (Part *p = selP()) { p->motion.phase = std::fmod(p->motion.phase + 0.25f, 1.f); s_dirty = true; } });
		// Posen
		static const char *const cl[CLIP_COUNT] = { "Moving", "Standing", "Firing" };
		static const float cx[CLIP_COUNT] = { 50, 102, 156 }, cw[CLIP_COUNT] = { 50, 52, 46 };
		for (int k = 0; k < CLIP_COUNT; ++k)
		{
			addBtn0(parent, AM_X + cx[k], AM_Y + 108, cw[k], 14, -1, cl[k], "Poses for this action", [k]() {
				s_animClip = k; s_animPoseIdx = 0; if (s_def.poseCount[k] == 0) { s_poseEdit = false; }
			}, [k]() { return s_animClip == k; });
		}
		addBtn0(parent, AM_X + 4, AM_Y + 126, 20, 14, -1, "<", "Previous pose", []() {
			const int n = s_def.poseCount[s_animClip]; if (n > 0) { s_animPoseIdx = (s_animPoseIdx + n - 1) % n; } });
		addBtn0(parent, AM_X + 78, AM_Y + 126, 20, 14, -1, ">", "Next pose", []() {
			const int n = s_def.poseCount[s_animClip]; if (n > 0) { s_animPoseIdx = (s_animPoseIdx + 1) % n; } });
		addBtn0(parent, AM_X + 102, AM_Y + 126, 48, 14, -1, "+ Pose", "New pose (a copy of this one) - then move and turn the parts", []() { animAddPose(); });
		addBtn0(parent, AM_X + 152, AM_Y + 126, 50, 14, -1, "- Pose", "Remove this pose", []() { animRemovePose(); });
		addBtn0(parent, AM_X + 4, AM_Y + 144, 64, 14, -1, "Edit pose", "On: the move and turn buttons change this pose (around the pivot) instead of the part itself", []() {
			if (s_def.poseCount[s_animClip] == 0) { animAddPose(); return; }
			s_poseEdit = !s_poseEdit; if (s_poseEdit) { s_pivotEdit = false; s_animPlay = -1; }
		}, []() { return s_poseEdit; });
		addBtn0(parent, AM_X + 104, AM_Y + 144, 20, 14, -1, "-", "Shorter loop (faster)", []() {
			float &sec = s_def.poseSeconds[s_animClip]; sec = std::max(0.2f, sec - (sec > 2.f ? 0.5f : 0.1f)); s_dirty = true; });
		addBtn0(parent, AM_X + 158, AM_Y + 144, 20, 14, -1, "+", "Longer loop (slower)", []() {
			float &sec = s_def.poseSeconds[s_animClip]; sec = std::min(20.f, sec + (sec >= 2.f ? 0.5f : 0.1f)); s_dirty = true; });
		// Vorschau
		static const char *const pl[CLIP_COUNT] = { "Move", "Stand", "Fire" };
		for (int k = 0; k < CLIP_COUNT; ++k)
		{
			addBtn0(parent, AM_X + 34 + k * 44.f, AM_Y + 188, 42, 15, -1, pl[k], "Play the whole animation as if the unit moves / stands / fires", [k]() {
				s_animPlay = s_animPlay == k ? -1 : k; s_animPlayStart = realTime; if (s_animPlay >= 0) { s_poseEdit = false; s_pivotEdit = false; }
			}, [k]() { return s_animPlay == k; });
		}
		addBtn0(parent, AM_X + AM_W - 38, AM_Y + 188, 34, 15, -1, "Done", "Close the animation window", []() { animShow(false); });
		s_animBtns.clear();
		for (size_t k = first; k < s_btns.size(); ++k) { s_animBtns.push_back(k); }
	}
	// ---- UEBER-RUMPF
	addBtn0(parent, 168, ROW_B + 13, 52, 42, FI_NEW, "New", "New hull", []() { newHull(); });
	addBtn0(parent, 222, ROW_B + 13, 52, 42, FI_LOAD_PREV, "Prev", "Previous saved hull", []() { loadCycle(-1); });
	addBtn0(parent, 276, ROW_B + 13, 52, 42, FI_LOAD_NEXT, "Next", "Next saved hull", []() { loadCycle(1); });
	addBtn0(parent, 330, ROW_B + 13, 52, 42, FI_SAVE, "Save", "Save hull", []() { doSave(); });
	addBtn0(parent, 384, ROW_B + 13, 52, 42, FI_DELETE, "Delete", "Delete hull", []() { doDeleteHull(); });
	// ---- ANSICHT
	addBtn0(parent, 448, ROW_B + 13, 30, 42, FI_VIEW_FRONT, "Front", "Front view", []() { s_yaw = 180.f; s_pitch = 8.f; });
	addBtn0(parent, 479, ROW_B + 13, 30, 42, FI_VIEW_SIDE, "Side", "Side view", []() { s_yaw = 270.f; s_pitch = 4.f; });
	addBtn0(parent, 510, ROW_B + 13, 30, 42, FI_VIEW_TOP, "Top", "Top view", []() { s_yaw = 180.f; s_pitch = 85.f; });
	addBtn0(parent, 541, ROW_B + 13, 30, 42, FI_AUTO, "Spin", "Spin view", []() { s_autoRot = !s_autoRot; }, []() { return s_autoRot; });
	addBtn0(parent, 572, ROW_B + 13, 30, 42, FI_VIEW_RESET, "Center", "Center view", []() { s_zoom = 1.f; s_panX = 0.f; s_panY = 0.f; s_yaw = 215.f; s_pitch = 24.f; });
	addBtn0(parent, 603, ROW_B + 13, 32, 42, FI_CLOSE, "Close", "Close [Esc]", []() { requestClose(); });

	// ---- Name
	s_nameBox = std::make_shared<W_EDITBOX>();
	root->attach(s_nameBox);
	s_nameBox->setBoxColours(WZCOL_FORM_LIGHT, WZCOL_FORM_HILITE, WZCOL_FORM_BACKGROUND);
	s_nameBox->setPlaceholder(WzString::fromUtf8("Hull name"));
	s_nameBox->setMaxStringSize(40);
	s_nameBox->setString(WzString::fromUtf8(s_def.name));
	s_nameBox->setTip("Hull name");
	s_nameBox->setOnEditingStoppedHandler([](W_EDITBOX &w) { s_def.name = w.getString().toStdString(); s_dirty = true; });

	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_root = root;
	shapeShow(s_shapeOpen);
	partsShow(s_partsOpen);   // (27.11.)
	animShow(s_animOpen);     // (27.11.)
	alistShow(s_alOpen);      // (27.11. 23:19)
}

bool mouseInBay()
{
	const Ctx c = ctx();
	const int mx = mouseX(), my = mouseY();
	return mx >= c.X(BAY_X) && mx <= c.X(BAY_X + BAY_W) && my >= c.Y(BAY_Y) && my <= c.Y(BAY_Y + BAY_H);
}

bool inBay(int x, int y)
{
	if (inShapePanel(x, y)) { return false; }   // (24.11.) Form-Fenster liegt ueber der Ansicht
	const Ctx c = ctx();
	if (x >= c.X(CEN_X) && x <= c.X(CEN_X + CEN_W) && y >= c.Y(CEN_Y) && y <= c.Y(SPB_Y + ANB_H)) { return false; }   // (27.11.) Centre/Animate/Join/Separate
	return x >= c.X(BAY_X) && x <= c.X(BAY_X + BAY_W) && y >= c.Y(BAY_Y) && y <= c.Y(BAY_Y + BAY_H);
}

void dragTo(int x, int y)
{
	if (s_dragBtn < 0) { return; }
	if (!s_moved && (std::abs(x - s_pressX) > 3 || std::abs(y - s_pressY) > 3))
	{
		s_moved = true;
		s_drag = s_dragBtn == 0 ? DRAG_TURN : DRAG_PAN;
		s_autoRot = false;
	}
	if (s_drag == DRAG_TURN)
	{
		// (Wunsch 17:55 "die Mauszeiger-Ansicht invertieren, sie ist falsch herum"): der Rumpf folgt der Maus wie angefasst
		s_yaw -= static_cast<float>(x - s_lastX) * 0.5f;
		s_pitch = std::clamp(s_pitch + static_cast<float>(y - s_lastY) * 0.35f, -10.f, 89.f);
	}
	else if (s_drag == DRAG_PAN)
	{
		s_panX += static_cast<float>(x - s_lastX);
		s_panY += static_cast<float>(y - s_lastY);
	}
	s_lastX = x; s_lastY = y;
}

void dragEnd(int x, int y)
{
	if (!s_moved)
	{
		const int m = mountAt(x, y);
		if (s_dragBtn == 0)
		{
			if (m >= 0) { selectMount(m); }
			else
			{
				// (27.11.) Antriebsplaetze vor den Teilen (liegen meist darunter/daneben)
				const int dr = driveAt(x, y);
				if (dr >= 0) { selectDrive(dr); }
				else { const int p = partAt(x, y); if (p >= 0) { clickPart(p); } }
			}
		}
		else if (s_dragBtn == 1 && m >= 0)
		{
			removeSlot(static_cast<size_t>(m));
			play("mfui_detach.ogg"); /* MOD: mfui_rattle.ogg gab es nie */
			setStatus("Weapon slot removed.");
		}
		else if (s_dragBtn == 1) { const int dr = driveAt(x, y); if (dr >= 0) { removeDrive(dr); } }
	}
	s_dragBtn = -1;
	s_drag = DRAG_NONE;
	s_moved = false;
}

/// Maus: 3D-Ansicht (links ziehen = drehen, rechts/Mitte ziehen = verschieben; Klick links = Platz/Teil waehlen, Klick rechts auf einen
/// Platz = entfernen) und Rumpf-Knoepfe (Klick = anbauen).
/// Die Vollbild-Form leert jedes Bild den Eingabepuffer (gehaltene Tasten gelten danach als losgelassen) - darum zaehlen hier die
/// rohen Druck-/Loslass-Ereignisse dieses Bildes (inputGetClicks), gehalten wird selbst mitgefuehrt.
void handleMouse()
{
	for (const MousePress &p : inputGetClicks())
	{
		const int b = p.key == MOUSE_LMB ? 0 : (p.key == MOUSE_RMB ? 1 : (p.key == MOUSE_MMB ? 2 : -1));
		if (b < 0) { continue; }
		if (p.action == MousePress::Press)
		{
			if (s_dragBtn < 0 && inBay(p.pos.x, p.pos.y))
			{
				s_dragBtn = b; s_pressX = s_lastX = p.pos.x; s_pressY = s_lastY = p.pos.y; s_moved = false; s_drag = DRAG_NONE;
			}
			else if (b == 0) { s_listPress = cellAt(p.pos.x, p.pos.y); }
		}
		else if (p.action == MousePress::Release)
		{
			if (b == s_dragBtn)
			{
				dragTo(p.pos.x, p.pos.y);
				dragEnd(p.pos.x, p.pos.y);
			}
			else if (b == 0 && s_listPress >= 0)
			{
				const int cell = cellAt(p.pos.x, p.pos.y);
				if (cell == s_listPress)
				{
					if (s_drivePick)   // (27.11.) Laufwerk-Groesse dieses Rumpfes fuer den Antriebsplatz
					{
						const auto &cat = modcompbody::catalog();
						if (static_cast<size_t>(cell) < cat.size() && driveSelected())
						{
							const BodyInfo &bi = cat[static_cast<size_t>(cell)];
							if (bi.driveModel == nullptr) { setStatus(bi.name + " has no drive models - pick another hull.", true); }
							else
							{
								modcompbody::Drive &dr = s_def.drives[static_cast<size_t>(s_selDrive)];
								dr.body = (!s_def.parts.empty() && bi.id == s_def.parts[0].body) ? std::string() : bi.id;
								s_dirty = true;
								setStatus("Drive " + std::to_string(s_selDrive + 1) + " now has the drive size of " + bi.name);
								play("mfui_clank.ogg");
							}
						}
						s_drivePick = false;
					}
					else if (s_texPick)   // (27.11.) Farben dieses Rumpfes nehmen
					{
						const auto &cat = modcompbody::catalog();
						if (static_cast<size_t>(cell) < cat.size()) { setTexFrom(cat[static_cast<size_t>(cell)].id, cat[static_cast<size_t>(cell)].name); }
						s_texPick = false;
					}
					else { addPart(static_cast<size_t>(cell)); }
				}
				s_listPress = -1;
			}
		}
	}
	if (s_dragBtn >= 0) { dragTo(mouseX(), mouseY()); }
}

} // namespace

bool isOpen()
{
	return s_open;
}

bool open(bool inMenu)
{
	if (s_open) { return true; }
	s_menu = inMenu || !modcompbody::inGame();
	modcompbody::refreshDefs();   // (17:55) im Spiel oder im Menue gespeicherte Rumpfe sind ueberall da
	if (s_def.parts.empty() && s_def.name.empty()) { s_def.name = "New hull"; }
	// Teile, die es hier nicht gibt (anderes Paket), entfernen
	for (size_t i = s_def.parts.size(); i-- > 0;)
	{
		if (modcompbody::bodyInfo(s_def.parts[i].body) == nullptr) { s_def.parts.erase(s_def.parts.begin() + static_cast<std::ptrdiff_t>(i)); s_links.clear(); }
	}
	if (s_links.size() != s_def.mounts.size()) { inferLinks(); }
	if (s_sel >= static_cast<int>(s_def.parts.size())) { s_sel = s_def.parts.empty() ? -1 : 0; }
	rebuildSources();
	if (!s_screen) { buildScreen(); }
	s_open = true;
	s_closeArmed = false;
	s_listPress = -1;
	modpanel::setSuppressed(true);
	modlayer::setSuppressed(true);
	s_root->show(true);
	layout();
	if (s_def.parts.empty()) { setStatus("Click a hull on the left - the first one is the chassis."); }
	return true;
}

void close()
{
	if (!s_open) { return; }
	s_open = false;
	if (s_screen)
	{
		widgRemoveOverlayScreen(s_screen);
		s_screen = nullptr;
		s_root = nullptr;
		s_nameBox = nullptr;
		s_btns.clear();
	}
	s_texts.clear();
	freeIcons();
	s_dragBtn = -1;
	s_pendingTip = -1;
	modpanel::setSuppressed(false);
	modlayer::setSuppressed(false);
	if (!s_menu && intMode == INT_DESIGN) { intDesignRefreshComponents(); }   // neue/geaenderte Verbundrumpfe sofort in der Liste
}

void tick()
{
	// Verbundrumpfe: baubar, sobald alle Teil-Rumpfe erforscht sind (Fabriklisten fragen die Verfuegbarkeit ab)
	if (modcompbody::inGame() && (realTime - s_availTick > 1000 || realTime < s_availTick))
	{
		s_availTick = realTime;
		if (selectedPlayer < MAX_PLAYERS) { modcompbody::refreshAvailability(selectedPlayer); }
	}
	if (!s_open || !s_root) { return; }
	if (static_cast<int>(screenWidth) != s_lastW || static_cast<int>(screenHeight) != s_lastH) { layout(); s_texts.clear(); }
	if (modcompbody::catalog().size() != s_catSize) { rebuildSources(); }
	if (s_autoRot) { s_yaw = std::fmod(s_yaw + 0.35f, 360.f); }
	// Mausrad: in der Ansicht Zoom, sonst Listen blaettern
	const Vector2i wheel = getMouseWheelSpeed();
	const Ctx c = ctx();
	const int mx = mouseX(), my = mouseY();
	if (wheel.y != 0)
	{
		if (s_alOpen && mx >= c.X(AL_X) && mx <= c.X(AL_X + AL_W) && my >= c.Y(AL_Y) && my <= c.Y(AL_Y + AL_H))   // (27.11. 23:19) Animations-Liste
		{
			s_alScroll = std::clamp(s_alScroll + (wheel.y > 0 ? -1 : 1), 0, std::max(0, static_cast<int>(alRows().size()) - AL_ROWS));
		}
		else if (mouseInBay()) { s_zoom = std::clamp(s_zoom * (wheel.y > 0 ? 1.12f : 1.f / 1.12f), 0.3f, 6.f); }
		else if (mx >= c.X(LIST_X) && mx <= c.X(LIST_X + LIST_W) && my >= c.Y(LIST_Y) && my <= c.Y(LIST_Y + LIST_H))
		{
			s_listScroll = std::clamp(s_listScroll + (wheel.y > 0 ? -1 : 1), 0, maxListScroll());
		}
		else if (mx >= c.X(480) && my >= c.Y(PARTS_Y) && my <= c.Y(PARTS_Y + PART_ROWS * LIST_ROW_H))
		{
			s_partScroll = std::clamp(s_partScroll + (wheel.y > 0 ? -1 : 1), 0, std::max(0, static_cast<int>(s_def.parts.size()) - PART_ROWS));
		}
		else if (mx >= c.X(480) && my >= c.Y(SLOTS_Y) && my <= c.Y(SLOTS_Y + MOUNT_ROWS * LIST_ROW_H))
		{
			s_mountScroll = std::clamp(s_mountScroll + (wheel.y > 0 ? -1 : 1), 0, std::max(0, static_cast<int>(s_def.mounts.size()) - MOUNT_ROWS));
		}
	}
	handleMouse();
	// Tasten (nicht beim Schreiben des Namens)
	if (W_EDITBOX::currentEditBox() != nullptr) { return; }
	if (keyPressed(KEY_LEFTARROW)) { move(0, -1.f); }
	if (keyPressed(KEY_RIGHTARROW)) { move(0, 1.f); }
	if (keyPressed(KEY_UPARROW)) { move(2, -1.f); }
	if (keyPressed(KEY_DOWNARROW)) { move(2, 1.f); }
	if (keyPressed(KEY_PAGEUP)) { move(1, 1.f); }
	if (keyPressed(KEY_PAGEDOWN)) { move(1, -1.f); }
	if (keyPressed(KEY_Q)) { rotate(0, 1.f); }
	if (keyPressed(KEY_E)) { rotate(0, -1.f); }
	if (keyPressed(KEY_R)) { rotate(1, 1.f); }
	if (keyPressed(KEY_F)) { rotate(1, -1.f); }
	if (keyPressed(KEY_Z)) { rotate(2, -1.f); }
	if (keyPressed(KEY_C)) { rotate(2, 1.f); }
	if (keyPressed(KEY_TAB) && !s_def.parts.empty()) { selectPart((s_sel + 1) % static_cast<int>(s_def.parts.size())); }
	if (keyPressed(KEY_DELETE)) { deleteSelected(); }
	// (27.11.) wie in Blender: Strg+J verbinden, P trennen
	if (keyPressed(KEY_J) && (keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL))) { joinSelected(); }
	if (keyPressed(KEY_P)) { separateSelected(); }
}

void shutdown()
{
	s_open = false;
	if (s_screen)
	{
		widgRemoveOverlayScreen(s_screen);
		s_screen = nullptr;
	}
	s_root = nullptr;
	s_nameBox = nullptr;
	s_btns.clear();
	s_items.clear();
	s_texts.clear();
	freeIcons();
	s_pendingTip = -1;
}

// ---------------------------------------------------------------- Einstiegstaste im Design-Bildschirm
void displayEntryButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	const int lift = hl ? 26 : 0;
	pie_UniTransBoxFill(x0 + 3, y0 + 3, x0 + w + 3, y0 + h + 3, pal_RGBA(0, 0, 0, 90));
	pie_BoxFill(x0, y0, x0 + w, y0 + h, pal_RGBA(2, 10, 30, 255));
	const int in = 3;
	for (int i = in; i < h - in; ++i)
	{
		const float t = static_cast<float>(i - in) / static_cast<float>(std::max(1, h - 2 * in - 1));
		const int r = static_cast<int>(6 + 10 * t) + lift / 3;
		const int g = static_cast<int>(36 + 58 * t) + lift;
		const int bl = static_cast<int>(104 + 96 * t) + lift;
		pie_BoxFill(x0 + in, y0 + i, x0 + w - in, y0 + i + 1, pal_RGBA(static_cast<uint8_t>(std::min(255, r)), static_cast<uint8_t>(std::min(255, g)), static_cast<uint8_t>(std::min(255, bl)), 255));
	}
	const int px0 = x0 + 8, py0 = y0 + 8, px1 = x0 + w - 8, py1 = y0 + h - 36;
	pie_UniTransBoxFill(px0, py0, px1, py1, pal_RGBA(2, 14, 44, 200));
	for (int gy = py0 + 8; gy < py1; gy += 8) { pie_UniTransBoxFill(px0, gy, px1, gy + 1, pal_RGBA(40, 150, 220, 40)); }
	for (int gx = px0 + 8; gx < px1; gx += 8) { pie_UniTransBoxFill(gx, py0, gx + 1, py1, pal_RGBA(40, 150, 220, 40)); }
	// Sinnbild: drei uebereinander verschobene Rumpf-Kaesten (weiss, rechts abgedunkelt)
	struct R { int x0, y0, x1, y1; };
	static const R blocks[] = { { 3, 26, 21, 32 }, { 7, 18, 19, 24 }, { 1, 20, 6, 25 }, { 18, 20, 23, 25 }, { 10, 8, 14, 17 }, { 9, 6, 15, 9 } };
	const float k = std::min(static_cast<float>(px1 - px0 - 4) / 24.f, static_cast<float>(py1 - py0 - 4) / 36.f);
	const int ox = px0 + static_cast<int>(((px1 - px0) - 24 * k) / 2.f), oy = py0 + 2 + (down ? 1 : 0);
	const unsigned char wa = hl ? 255 : 238;
	for (const R &r : blocks)
	{
		const int ax = ox + static_cast<int>(r.x0 * k), ay = oy + static_cast<int>(r.y0 * k);
		const int bx2 = ox + static_cast<int>(r.x1 * k), by2 = oy + static_cast<int>(r.y1 * k);
		const int mxx = (ax + bx2) / 2;
		pie_BoxFill(ax, ay, mxx, by2, pal_RGBA(255, 255, 255, wa));
		pie_BoxFill(mxx, ay, bx2, by2, pal_RGBA(150, 195, 235, wa));
	}
	const PIELIGHT edgeL = pal_RGBA(170, 230, 255, 255), edgeD = pal_RGBA(0, 20, 60, 255);
	iV_Line(x0 + 1, y0 + 1, x0 + w - 2, y0 + 1, down ? edgeD : edgeL);
	iV_Line(x0 + 1, y0 + 1, x0 + 1, y0 + h - 2, down ? edgeD : edgeL);
	iV_Line(x0 + w - 2, y0 + 1, x0 + w - 2, y0 + h - 2, down ? edgeL : edgeD);
	iV_Line(x0 + 1, y0 + h - 2, x0 + w - 2, y0 + h - 2, down ? edgeL : edgeD);
	iV_Box(x0, y0, x0 + w - 1, y0 + h - 1, hl ? pal_RGBA(70, 245, 255, 255) : pal_RGBA(40, 170, 230, 255));
	if (modui::ready())
	{
		const std::string t1 = "HULL", t2 = "FORGE";
		modui::text(t1, x0 + (w - modui::textWidth(t1)) / 2, y0 + h - 31, pal_RGBA(235, 250, 255, 255));
		modui::text(t2, x0 + (w - modui::textWidth(t2)) / 2, y0 + h - 18, hl ? pal_RGBA(120, 250, 255, 255) : pal_RGBA(150, 215, 255, 255));
	}
}

}
