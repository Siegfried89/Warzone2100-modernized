// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: "Frei malen" - Schlachtfeld-Planung durch Zeichnen auf der Karte (siehe modpaint.h)

#include "modpaint.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/button.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"

#include "droid.h"
#include "game_world.h"
#include "hci.h"
#include "map.h"
#include "modarty.h"
#include "modbuild.h"
#include "modcombot.h"
#include "moddesigner.h"
#include "modlayer.h"
#include "modui.h"
#include "modrosette.h"
#include "multiplay.h"
#include "modincoming.h"
#include "modrecover.h"
#include "modcrates.h"
#include "modcontrol.h"
#include "console.h"
#include "feature.h"
#include "featuredef.h"
#include "mapgrid.h"
#include "objmem.h"
#include "radar.h"
#include "modcover.h"   // MOD 17.11.: Take cover
#include "modmines.h"   // MOD 17.11.: Minen raeumen
#include "modoil.h"     // MOD 19.11.: isInfantry (Minen raeumen im Gebiet)
#include "order.h"
#include "ordersource.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace modpaint
{
namespace
{
enum Mode
{
	M_MOVE = 0, M_RALLY, M_ATTACK, M_DEFEND, M_ARTYBASE, M_ARTY, M_AIR, M_COMBOT, M_SPREAD, M_RECOVER, M_COVER, M_TRAPS, M_MINES, M_OFFENSE, M_CLEARMINES, M_DEMOLISH, M_COUNT   // M_SPREAD, M_COVER = Sofort-Befehle (kein Zeichnen)
};

struct ModeInfo
{
	const char *label;
	const char *tip;
	const char *cursor;
	unsigned char r, g, b;
	bool area;   // Gebiet (geschlossene Form) statt Linie
};

const ModeInfo kModes[M_COUNT] = {
	{ "Move", "Move: draw a path - units drive along it", "Move: drag a path (right click cancels)", 60, 220, 90, false },
	{ "Rally", "Rally point: units gather here before an attack", "Rally: drag around the rally point (right click cancels)", 255, 150, 40, false },
	{ "Attack", "Attack vector: units advance along it and fight everything on the way", "Attack: drag the attack vector (right click cancels)", 235, 50, 40, false },
	{ "Defend", "Defensive line: units take up positions on it, builders dig trenches along it", "Defend: drag the defensive line (right click cancels)", 60, 130, 255, false },
	{ "Arty base", "Build an artillery base here: sensor tower, artillery emplacements, wall ring, hedgehogs in front. Small circle = 4 emplacements, medium = 8, large = 12", "Artillery base: drag a circle around the place - its size sets the base size (right click cancels)", 170, 120, 60, true },
	{ "Artillery", "Artillery: bombard this area", "Artillery: drag the target area (right click cancels)", 240, 200, 90, true },
	{ "Aircraft", "Aircraft: patrol over this area", "Aircraft: drag the patrol area (right click cancels)", 255, 235, 50, true },
	{ "Combots", "Combot area: combots patrol and fight in this area on their own", "Combots: drag the combot area (right click cancels)", 190, 90, 255, true },
	{ "Spread out", "Spread out: the selected units spread out evenly (against artillery and bombs)", "", 150, 200, 200, false },
	{ "Salvage", "Salvage: drag a circle around parts, artefacts and wrecks - an armed, escorted salvage team gathers at the base first, then recovers everything in it (wrecks are towed to the repair facility by flatbed trucks)", "Salvage: drag a circle around the salvage (right click cancels)", 120, 220, 255, true },
	{ "Take cover", "Take cover: the selected infantry (or all infantry, if none is selected) mans the nearest trenches, pits, sandbag positions and buildings", "", 200, 170, 110, false },
	// MOD (Wunsch 17.11. 18:12/18:13): Panzersperrenfeld (gruene Pyramiden) und Minenfeld
	{ "Tank traps", "Tank trap field: drag an area - builders put a staggered field of tank traps into it (vehicles cannot pass)", "Tank traps: drag the area (right click cancels)", 40, 200, 80, true },
	{ "Minefield", "Minefield: drag an area - builders lay hidden mines in it; they blow up enemy ground units that drive over them", "Minefield: drag the area (right click cancels)", 255, 90, 40, true },
	// MOD (Wunsch 17.11. 18:37): Offensivlinie (rot) - wie die Verteidigungslinie, aber auf Angriff ausgerichtet
	{ "Offense", "Offensive line: many combat units mass on the line in two ranks, ready to push forward (they pursue enemies); builders add only a few short trenches and MG posts", "Offense: drag the offensive line (right click cancels)", 240, 45, 45, false },
	// MOD (Wunsch 17.11. 19:36): Faehigkeit der Infanterie - Minen raeumen; (19.11. 17:34 "konnte keinen minenraeumkreis ziehen? der sollte
	// auch funktionieren wenn es keine Minen dort gibt"): jetzt ein Gebiet - Raeumpanzer (Walze an) und Infanterie durchkaemmen es in Bahnen
	{ "Clear mines", "Clear mines: drag a circle - mine flails (drum on) and infantry sweep the whole area lane by lane, also where no mines are known. Infantry spots mines within 2.5 tiles, disarms them (3 s each) and does not set off anti-tank mines; flails beat them off ahead", "Clear mines: drag a circle around the area to sweep (right click cancels)", 230, 200, 60, true },
	// MOD (Wunsch 19.11. 17:34 "Neue kreis ziehen = demolish alles in diesem Gebiet")
	{ "Demolish", "Demolish: drag a circle - builders tear down all your own structures in it (nearest first, spread over all builders)", "Demolish: drag a circle around what should be torn down (right click cancels)", 200, 90, 60, true },
};

constexpr int BTN_W = 66;
constexpr int BTN_H = 20;
constexpr int GAP = 3;
constexpr int PAD = 4;
constexpr int COLS = 2;
constexpr int ROWS = 8;   // (17.11.) + "Take cover", + Panzersperren / Minenfeld / Offensive, + Minen raeumen
constexpr int TOTAL_W = COLS * BTN_W + (COLS - 1) * GAP + 2 * PAD;
constexpr int TOTAL_H = ROWS * BTN_H + (ROWS - 1) * GAP + 2 * PAD + 12;   // + Titelzeile
constexpr unsigned SHOW_MS = 25000;   // so lange bleibt eine Zeichnung sichtbar
constexpr int NEAR_TILES = 16;        // ohne Auswahl: passende Einheiten in diesem Umkreis
constexpr size_t MAX_UNITS = 40;

struct Stroke
{
	Mode mode;
	std::vector<Vector2i> pts;
	unsigned born = 0;
	std::vector<Vector2i> marks;   // Panzersperren / Minen: geplante Stellen
	int side = 0;                  // Verteidigungslinie: Seite der Querstriche (zum Feind)
};

std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_root;
std::vector<std::shared_ptr<W_BUTTON>> s_buttons;
int s_mode = -1;                 // aktiver Zeichenmodus (-1 = keiner)
bool s_drawing = false;
std::vector<Vector2i> s_cur;     // Linie, die gerade gezeichnet wird
std::vector<Stroke> s_done;      // fertige Zeichnungen (verblassen)
// MOD (Wunsch 18.11. 00:59, berichtigt 02:44): "Wenn man Shift drueckt, lassen sich neue zeichnen, auch mehrere. Aber wenn man nicht Shift
// drueckt, dann wird sie ersetzt." - mit gedrueckter Umschalttaste kommt jede Zeichnung als WEITERE Linie dazu (eigener Marker, eigene
// Einheiten; der Zeichenmodus bleibt an); ohne Shift ersetzt eine neue Zeichnung die bisherigen Linien derselben Art.
bool s_append = false;           // Zeichnung mit Shift
std::vector<uint32_t> s_chain;   // Einheiten, die in der laufenden Shift-Folge schon eine Linie bekommen haben
// MOD (Wunsch 14.11., Infanterie baut): eigener WZ-Knopf direkt ueber der Rosette, sobald Infanterie mit Spaten gewaehlt ist -> oeffnet das WZ-Baumenue
std::shared_ptr<W_SCREEN> s_infScreen;
std::shared_ptr<W_CLICKFORM> s_infRoot;
std::shared_ptr<W_BUTTON> s_infBtn;

// ------------------------------------------------------------------------------------------------- Anzeige der Knoepfe

void drawRoot(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const float s = modui::uiScale();
	const int x0 = static_cast<int>(xOffset) + psWidget->x();
	const int y0 = static_cast<int>(yOffset) + psWidget->y();
	modui::wzPanel(x0, y0, x0 + static_cast<int>(TOTAL_W * s), y0 + static_cast<int>(TOTAL_H * s), "Battle plan", modui::sizeClassFor(s));   // MOD: WZ-Stil
}

void drawButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const float s = modui::uiScale();
	const int m = static_cast<int>(psWidget->UserData);
	const ModeInfo &mi = kModes[m];
	const int x0 = static_cast<int>(xOffset) + psWidget->x();
	const int y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = static_cast<int>(BTN_W * s), h = static_cast<int>(BTN_H * s);
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const bool active = s_mode == m;
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;
	// MOD (16.11. Punkt 1): WZ-Stil; die kleine Farbmarke links zeigt die Art des Befehls
	const PIELIGHT accent = pal_RGBA(mi.r, mi.g, mi.b, 255);
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	modui::wzButton(x0, y0, w, h, mi.label, active, hl, down, modui::sizeClassFor(s), &accent);
}

void ensureScreen()
{
	if (s_screen) { return; }
	auto screen = W_SCREEN::make();
	screen->psForm->hide();   // MOD-FIX: verborgene Wurzel = durchlaessig fuer Mausklicks (sonst schluckt sie JEDEN Klick im Spiel, siehe notifications.cpp)
	auto root = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(root);
	root->displayFunction = drawRoot;
	s_buttons.clear();
	for (int m = 0; m < M_COUNT; ++m)
	{
		auto b = std::make_shared<W_BUTTON>();
		root->attach(b);
		b->displayFunction = drawButton;
		b->UserData = static_cast<UDWORD>(m);
		b->setTip(kModes[m].tip);
		b->addOnClickHandler([m](W_BUTTON &) { widgScheduleTask([m]() {
			if (m == M_COVER)
			{
				// MOD (Wunsch 17.11.): Sofort-Befehl "Take cover" - Infanterie bemannt Graeben/Gruben/Haeuser in der Naehe
				if (selectedPlayer < MAX_PLAYERS) { modcover::takeCover(selectedPlayer); }
				s_mode = -1;
				s_drawing = false;
				s_cur.clear();
				return;
			}
			if (m == M_SPREAD)
			{
				// MOD (16.11. Punkt 2): Sofort-Befehl "Spread out" fuer die Auswahl
				std::vector<DROID *> sel;
				if (selectedPlayer < MAX_PLAYERS)
				{
					for (DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d->selected) { sel.push_back(d); } }
				}
				modincoming::spreadUnits(sel);
				s_mode = -1;
				s_drawing = false;
				s_cur.clear();
				return;
			}
			s_mode = (s_mode == m) ? -1 : m;
			s_drawing = false;
			s_cur.clear();
		}); });
		s_buttons.push_back(b);
	}
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_root = root;
}

void drawInfButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const float sc = modui::uiScale();
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x();
	const int y0 = static_cast<int>(yOffset) + psWidget->y();
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	const PIELIGHT accent = pal_RGBA(230, 190, 90, 255);
	modui::wzButton(x0, y0, psWidget->width(), psWidget->height(), _("Infantry: build"), intMode == INT_STAT, hl, down, modui::sizeClassFor(sc), &accent);
}

bool infantryBuilderSelected()
{
	if (selectedPlayer >= MAX_PLAYERS) { return false; }
	for (const DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d->selected && !d->died && modbuild::isInfantryBuilder(d)) { return true; }
	}
	return false;
}

void ensureInfButton()
{
	if (s_infScreen) { return; }
	auto screen = W_SCREEN::make();
	screen->psForm->hide();   // durchlaessig fuer Mausklicks
	auto root = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(root);
	auto b = std::make_shared<W_BUTTON>();
	root->attach(b);
	b->displayFunction = drawInfButton;
	b->setTip(_("Open the build menu for the selected infantry (trenches, sandbags, MG nests, ammo crates)"));
	b->addOnClickHandler([](W_BUTTON &) { widgScheduleTask([]() {
		if (!infantryBuilderSelected()) { return; }
		intResetScreen(true);
		intShowWidget(RETBUT_BUILD);
	}); });
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_infScreen = screen;
	s_infRoot = root;
	s_infBtn = b;
}

void infButtonTick(bool want)
{
	const bool show = want && infantryBuilderSelected() && (intMode == INT_NORMAL || intMode == INT_OBJECT || intMode == INT_STAT);
	if (!show && !s_infScreen) { return; }
	ensureInfButton();
	s_infRoot->show(show);
	if (!show) { return; }
	const float sc = modui::uiScale();
	const int w = static_cast<int>(RET_FORMWIDTH), h = static_cast<int>(22 * sc);
	const int y = std::max(0, static_cast<int>(RET_Y) - h - 4);
	s_infRoot->setGeometry(RET_X, y, w, h);
	s_infBtn->setGeometry(0, 0, w, h);
}

void calcRect(int &x, int &y, int &w, int &h)
{
	const float s = modui::uiScale();
	w = static_cast<int>(TOTAL_W * s); h = static_cast<int>(TOTAL_H * s);
	// (17.11.) nicht ueber der Karte unten rechts (Radar): links daneben, unten buendig
	// (18.11. 12:10) ohne Minikarte unten rechts in der Ecke - Map/Symbol-Knoepfe und Rosette stapeln sich darueber (siehe stackTop)
	x = static_cast<int>(screenWidth) - w - 4;
	y = static_cast<int>(screenHeight) - h - 8;
	const std::shared_ptr<WIDGET> radar = getRadarWidget();
	if (radar && radar->visible() && radar->width() > 0)
	{
		const int m = rotateRadar ? static_cast<int>(radar->width() * 0.21f) : 0;   // (18.11.) Abstand zur gedrehten Minikarte
		x = std::max(4, radar->x() - w - 6 - m);
		y = std::max(4, static_cast<int>(screenHeight) - h - 8);
	}
}

void layout()
{
	const float s = modui::uiScale();
	int x, y, w, h;
	calcRect(x, y, w, h);
	s_root->setGeometry(x, y, w, h);
	for (size_t i = 0; i < s_buttons.size(); ++i)
	{
		const int col = static_cast<int>(i) % COLS, row = static_cast<int>(i) / COLS;
		s_buttons[i]->setGeometry(static_cast<int>((PAD + col * (BTN_W + GAP)) * s), static_cast<int>((PAD + 12 + row * (BTN_H + GAP)) * s),
		                          static_cast<int>(BTN_W * s), static_cast<int>(BTN_H * s));
	}
}

// ------------------------------------------------------------------------------------------------- Geometrie

int64_t dist2(const Vector2i &a, const Vector2i &b)
{
	const int64_t dx = a.x - b.x, dy = a.y - b.y;
	return dx * dx + dy * dy;
}

Vector2i centroid(const std::vector<Vector2i> &p)
{
	int64_t sx = 0, sy = 0;
	for (const Vector2i &v : p) { sx += v.x; sy += v.y; }
	const int64_t n = std::max<int64_t>(1, static_cast<int64_t>(p.size()));
	return Vector2i(static_cast<int>(sx / n), static_cast<int>(sy / n));
}

/// n gleichmaessig verteilte Punkte entlang der Linie (nach Laenge)
std::vector<Vector2i> resample(const std::vector<Vector2i> &p, size_t n)
{
	std::vector<Vector2i> out;
	if (p.empty() || n == 0) { return out; }
	if (p.size() == 1) { out.assign(n, p.front()); return out; }
	if (n == 1) { out.push_back(p[p.size() / 2]); return out; }
	std::vector<double> acc(p.size(), 0.0);
	for (size_t i = 1; i < p.size(); ++i) { acc[i] = acc[i - 1] + std::sqrt(static_cast<double>(dist2(p[i], p[i - 1]))); }
	const double total = acc.back();
	size_t seg = 1;
	for (size_t k = 0; k < n; ++k)
	{
		const double t = total * static_cast<double>(k) / static_cast<double>(n - 1);
		while (seg < p.size() - 1 && acc[seg] < t) { ++seg; }
		const double l = acc[seg] - acc[seg - 1];
		const double f = l > 0.0 ? (t - acc[seg - 1]) / l : 0.0;
		out.emplace_back(static_cast<int>(p[seg - 1].x + (p[seg].x - p[seg - 1].x) * f), static_cast<int>(p[seg - 1].y + (p[seg].y - p[seg - 1].y) * f));
	}
	return out;
}

/// Spiral-Versatz fuer die i-te Einheit, damit nicht alle auf einen Punkt fahren
Vector2i spread(const Vector2i &c, size_t i)
{
	if (i == 0) { return c; }
	const int ring = 1 + static_cast<int>(std::sqrt(static_cast<double>(i)));
	const uint16_t ang = static_cast<uint16_t>(i * 10273u);
	const int r = ring * (DIST_TILE * 3 / 4);
	Vector2i p(c.x + iSinR(ang, r), c.y + iCosR(ang, r));
	p.x = std::clamp(p.x, TILE_UNITS, world_coord(gameWorld.map.width - 2));
	p.y = std::clamp(p.y, TILE_UNITS, world_coord(gameWorld.map.height - 2));
	return p;
}

// ------------------------------------------------------------------------------------------------- Einheiten-Klassen

bool isAir(const DROID *d) { return d->isVtol() || modlayer::isFlier(d); }
// (19.11.) Pickups/Oeltanker sind DROID_CONSTRUCT, bauen aber nicht (DROID::isConstructionDroid) - nie als Bauer einplanen
bool isTruck(const DROID *d) { return (d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT) && d->isConstructionDroid(); }
bool hasWeapon(const DROID *d) { return d->numWeaps > 0 && d->asWeaps[0].nStat > 0; }
bool isArty(const DROID *d)
{
	if (!hasWeapon(d)) { return false; }
	const WEAPON_STATS *ws = d->getWeaponStats(0);
	return (ws != nullptr && ws->movementModel == MM_INDIRECT) || modarty::isArtyWeapon(d->asWeaps[0].nStat);
}
bool isCombat(const DROID *d)
{
	return hasWeapon(d) && !isAir(d) && (d->droidType == DROID_WEAPON || d->droidType == DROID_CYBORG || d->droidType == DROID_CYBORG_SUPER || d->droidType == DROID_PERSON);
}
bool isBuilder(const DROID *d) { return (isTruck(d) && !isAir(d)) || modbuild::isInfantryBuilder(d); }

bool fits(Mode m, const DROID *d)
{
	if (d->isTransporter()) { return false; }
	switch (m)
	{
	case M_MOVE: return !isAir(d);
	case M_RALLY: return isCombat(d) || modcombot::isModular(d);
	case M_ATTACK: return (isCombat(d) && !isArty(d)) || modcombot::isModular(d);
	case M_DEFEND: return isCombat(d) || isBuilder(d);
	case M_ARTYBASE: return isTruck(d) && !isAir(d);
	case M_ARTY: return isArty(d);
	case M_AIR: return isAir(d) && hasWeapon(d);
	case M_COMBOT: return modcombot::isModular(d);
	case M_TRAPS:
	case M_MINES: return isBuilder(d);
	case M_OFFENSE: return isCombat(d) || isBuilder(d) || modcombot::isModular(d);
	case M_CLEARMINES: return (modmines::isFlail(d) || modoil::isInfantry(d)) && !isAir(d);
	case M_DEMOLISH: return isBuilder(d);
	default: return false;
	}
}

std::vector<DROID *> pickUnits(Mode m, const std::vector<Vector2i> &pts)
{
	std::vector<DROID *> sel, nearby;
	if (selectedPlayer >= MAX_PLAYERS) { return sel; }
	const modlayer::Layer view = modlayer::current();
	bool anySelected = false;
	for (DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d == nullptr || d->died) { continue; }
		if (d->selected) { anySelected = true; }
		if (modlayer::layerOf(d) != view || !fits(m, d)) { continue; }
		if (d->selected) { sel.push_back(d); continue; }
		// ohne Auswahl: Einheiten in der Naehe der Zeichnung
		const int64_t nearR = static_cast<int64_t>(m == M_OFFENSE ? NEAR_TILES * 3 / 2 : NEAR_TILES) * DIST_TILE;   // Offensive: mehr Einheiten
		const int64_t r2 = nearR * nearR;
		for (const Vector2i &p : pts)
		{
			if (dist2(d->pos.xy(), p) <= r2) { nearby.push_back(d); break; }
		}
	}
	if (anySelected) { return sel; }   // gewaehlte Einheiten haben Vorrang (auch wenn keine davon passt)
	const Vector2i c = centroid(pts);
	std::sort(nearby.begin(), nearby.end(), [&c](const DROID *a, const DROID *b) {
		const int64_t da = dist2(a->pos.xy(), c), db = dist2(b->pos.xy(), c);
		return da != db ? da < db : a->id < b->id;
	});
	const size_t cap = (m == M_OFFENSE) ? MAX_UNITS * 8 / 5 : MAX_UNITS;
	if (nearby.size() > cap) { nearby.resize(cap); }
	return nearby;
}

void send(DROID *d, const DroidOrder &o, bool add)
{
	sendDroidInfo(d, o, add, currentOrderSource());
}

// ------------------------------------------------------------------------------------------------- Bauwerke fuer die Artilleriebasis

bool available(int idx)
{
	return idx >= 0 && static_cast<UDWORD>(idx) < numStructureStats && selectedPlayer < MAX_PLAYERS && apStructTypeLists[selectedPlayer][idx] == AVAILABLE;
}

STRUCTURE_STATS *firstAvailable(std::initializer_list<const char *> ids)
{
	for (const char *id : ids)
	{
		const int idx = getStructStatFromName(WzString::fromUtf8(id));
		if (available(idx)) { return &asStructureStats[idx]; }
	}
	return nullptr;
}

STRUCTURE_STATS *bestArtyEmplacement()
{
	STRUCTURE_STATS *best = nullptr;
	unsigned bestRange = 0;
	for (UDWORD i = 0; i < numStructureStats; ++i)
	{
		STRUCTURE_STATS &s = asStructureStats[i];
		if (!available(static_cast<int>(i)) || s.type != REF_DEFENSE || s.numWeaps == 0 || s.psWeapStat[0] == nullptr) { continue; }
		if (s.psWeapStat[0]->movementModel != MM_INDIRECT) { continue; }
		const unsigned range = s.psWeapStat[0]->upgrade[selectedPlayer].maxRange;
		if (best == nullptr || range > bestRange) { best = &s; bestRange = range; }
	}
	return best;
}

/// Groesse der Artilleriebasis aus dem gezogenen Kreis (Wunsch 20.11. 02:13 "Die Arty base ist gut. Aber jetzt gibt es nur noch eine
/// Groesse"): mittlerer Abstand der Strichpunkte zur Mitte - klein (bis 2,5 Felder), mittel (bis 4,5), gross
int artyBaseSize(const std::vector<Vector2i> &raw)
{
	if (raw.size() < 3) { return 1; }
	const Vector2i c = centroid(raw);
	double sum = 0.0;
	for (const Vector2i &q : raw) { sum += std::sqrt(static_cast<double>(dist2(q, c))); }
	const double r = sum / static_cast<double>(raw.size()) / DIST_TILE;   // (20.11.) in Original-Kacheln (auch in der Viertel-Fassung)
	return r < 2.5 ? 1 : (r < 4.5 ? 2 : 3);
}

/// Bauplan der Artilleriebasis (Sensor in der Mitte, Stellungen dicht drumherum, Mauerring, davor ein Ring Panzerigel) - fuer den Bau
/// UND die Vorschau unter der Maus (18.11. 12:14 "beim Klicken auf Arty base kommt das falsche Layout, beim Bestaetigen das richtige")
/// size 1: 4 Stellungen, Mauer 5 x 5; size 2: 8 Stellungen, Mauer 7 x 7; size 3: 12 Stellungen, Mauer 9 x 9.
/// (20.11. "sie hat auch nicht mehr Hedgehog-Panzersperren davor"): Igel-Ring ein Feld vor der Mauer, in der Mitte jeder Seite eine Luecke
std::vector<modbuild::Placement> artyBasePlan(const Vector2i &centerWorld, int size)
{
	std::vector<modbuild::Placement> out;
	size = std::clamp(size, 1, 3);
	STRUCTURE_STATS *arty = bestArtyEmplacement();
	STRUCTURE_STATS *sensor = firstAvailable({ "Sys-SensoTowerWS", "Sys-SensoTower02", "Sys-SensoTower01", "A0BaBaOutpost" });
	STRUCTURE_STATS *wall = firstAvailable({ "A0BaBaHorizontalWall", "A0HardcreteMk1Wall", "CollectiveWall", "NEXUSWall", "A0BaBaSandbagWall" });
	STRUCTURE_STATS *hedge = firstAvailable({ "A0BaBaHedgehog", "A0TankTrapHeavy", "A0TankTrap" });
	// (20.11.) in PLAN-Kacheln (DIST_TILE = Original-Kachel) wie der Befestigungsplan und seine Vorschau - in der Viertel-Fassung waren
	// es Feinfelder: Vorschau an falscher Stelle, Stellungen nur ein Feinfeld auseinander (blockierten sich gegenseitig)
	const Vector2i ct(std::max(0, centerWorld.x) / DIST_TILE, std::max(0, centerWorld.y) / DIST_TILE);
	auto one = [&](STRUCTURE_STATS *st, int tx, int ty) { modbuild::Placement p; p.stats = st; p.tile = Vector2i(tx, ty); p.tile2 = p.tile; out.push_back(p); };
	auto line = [&](STRUCTURE_STATS *st, int x0, int y0, int x1, int y1) {
		modbuild::Placement p; p.stats = st; p.tile = Vector2i(x0, y0); p.tile2 = Vector2i(x1, y1); p.line = true; out.push_back(p);
	};
	if (sensor != nullptr) { one(sensor, ct.x, ct.y); }
	if (arty != nullptr)
	{
		// (16.11. 22:50 "immer noch zu gross, nicht so viel Platz verschwenden") dicht um den Sensor
		static const int IN4[4][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
		static const int R2[8][2] = { { -2, -2 }, { 0, -2 }, { 2, -2 }, { 2, 0 }, { 2, 2 }, { 0, 2 }, { -2, 2 }, { -2, 0 } };
		static const int R3[8][2] = { { -3, -3 }, { 0, -3 }, { 3, -3 }, { 3, 0 }, { 3, 3 }, { 0, 3 }, { -3, 3 }, { -3, 0 } };
		if (size != 2) { for (const auto &o : IN4) { one(arty, ct.x + o[0], ct.y + o[1]); } }
		if (size == 2) { for (const auto &o : R2) { one(arty, ct.x + o[0], ct.y + o[1]); } }
		if (size == 3) { for (const auto &o : R3) { one(arty, ct.x + o[0], ct.y + o[1]); } }
	}
	const int r = size + 1;   // Mauerring direkt um den Stellungsblock
	if (wall != nullptr)
	{
		line(wall, ct.x - r, ct.y - r, ct.x + r, ct.y - r);
		line(wall, ct.x + r, ct.y - r + 1, ct.x + r, ct.y + r);
		line(wall, ct.x + r - 1, ct.y + r, ct.x - r, ct.y + r);
		line(wall, ct.x - r, ct.y + r - 1, ct.x - r, ct.y - r + 1);
	}
	if (hedge != nullptr)
	{
		const int h = r + 2;   // ein freies Feld zwischen Mauer und Igeln
		// je Seite zwei Stuecke mit einer Luecke von einem Feld in der Mitte
		line(hedge, ct.x - h, ct.y - h, ct.x - 1, ct.y - h); line(hedge, ct.x + 1, ct.y - h, ct.x + h, ct.y - h);
		line(hedge, ct.x + h, ct.y - h + 1, ct.x + h, ct.y - 1); line(hedge, ct.x + h, ct.y + 1, ct.x + h, ct.y + h);
		line(hedge, ct.x + h - 1, ct.y + h, ct.x + 1, ct.y + h); line(hedge, ct.x - 1, ct.y + h, ct.x - h, ct.y + h);
		line(hedge, ct.x - h, ct.y + h - 1, ct.x - h, ct.y + 1); line(hedge, ct.x - h, ct.y - 1, ct.x - h, ct.y - h + 1);
	}
	return out;
}

// ------------------------------------------------------------------------------------------------- Bergetrupp (Wunsch 14.11. Punkt 25)
// "Bergen": Kreis ziehen -> das Spiel stellt vollautomatisch einen bewaffneten, eskortierten Bergetrupp zusammen (freie Bau-/Reparatur-
// fahrzeuge fuer Teile und Artefakte, Scav-Lkw mit Ladeflaeche fuer Wracks, Kampfeinheiten als Eskorte). Der Trupp sammelt sich zuerst
// vollstaendig am Bereitstellungspunkt (Rand der eigenen Basis in Richtung Ziel), faehrt dann gemeinsam los, birgt alles im Kreis und
// kehrt zurueck. Wracks bringt der Lkw zur Reparaturstation, dort werden sie wieder zu Fahrzeugen (modrecover).
// Die Verwaltung laeuft beim befehlenden Spieler und gibt gewoehnliche Befehle (wie Mausklicks) - also auch im Mehrspieler synchron.
// Nicht im Spielstand (nach dem Laden ist der Trupp aufgeloest, die Einheiten behalten ihren letzten Befehl).
struct SalvageOp
{
	bool active = false;
	int phase = 0;                  // 0 sammeln, 1 anfahren, 2 bergen
	Vector2i c, deploy;
	int r = 0;
	unsigned phaseStart = 0, nextStep = 0, nextEscort = 0;
	std::vector<uint32_t> collectors, towers, escorts;
};
SalvageOp s_op;

// MOD (Wunsch 14.11. Punkt 12, Rest): "Kommen nach einer Zeit keine weiteren Befehle, nehmen sie wieder an den Oberbefehlen teil."
// Jede Einheit merkt sich ihren letzten Schlachtplan-Auftrag (Bewegung, Sammeln, Angriff, Verteidigung, Combot-Gebiet). Hat der Spieler
// sie einzeln woanders hingeschickt, ist sie fertig (kein Befehl mehr), die 45-s-Spielerkontrolle (modcontrol) ist vorbei und sie steht
// weit weg vom Planziel, kehrt sie von selbst zu ihrem Planauftrag zurueck. Ein Planauftrag gilt 15 Minuten.
struct PlanTask
{
	DroidOrder order = DroidOrder(DORDER_NONE);
	Vector2i anchor;
	unsigned born = 0;
	int face = -1;        // (19.11. 16:44) Verteidigungslinie: Blickrichtung am Ziel (Richtung der Querstriche), -1 = keine
	bool faced = false;   // schon eingedreht (bis die Einheit ihren Platz wieder verlaesst)
	unsigned sent = 0;    // letzter eigener Befehl (Linienbefehl oder Rueckkehr) - spaetere Befehle sind Eingriffe des Spielers
};
std::map<uint32_t, PlanTask> s_plan;
unsigned s_nextPlanCheck = 0;
constexpr unsigned PLAN_LIFE = 15 * 60 * 1000;

constexpr unsigned OP_STEP_MS = 1000;
constexpr unsigned OP_GATHER_MAX = 75000;   // laenger wartet der Trupp nicht auf Nachzuegler
constexpr unsigned OP_GO_MAX = 120000;
constexpr unsigned OP_WORK_MAX = 300000;

bool isSupportUnit(const DROID *d)
{
	return d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT || d->droidType == DROID_REPAIR || d->droidType == DROID_CYBORG_REPAIR;
}
bool idleUnit(const DROID *d)
{
	return (d->order.type == DORDER_NONE || d->order.type == DORDER_GUARD || d->order.type == DORDER_HOLD) && !modrecover::carrying(d) && modcrates::crates(d) == 0;
}
bool inOp(uint32_t id)
{
	if (!s_op.active) { return false; }
	for (const auto *v : { &s_op.collectors, &s_op.towers, &s_op.escorts })
	{
		if (std::find(v->begin(), v->end(), id) != v->end()) { return true; }
	}
	return false;
}
DROID *opDroid(uint32_t id)
{
	if (selectedPlayer >= MAX_PLAYERS) { return nullptr; }
	DROID *d = IdToDroid(gameWorld.objects, id, selectedPlayer);
	return (d != nullptr && !d->died) ? d : nullptr;
}

/// bergbare Dinge im Kreis: Wracks (Abschlepp-Lkw) und Teile/Artefakte/Faesser (aufsammeln)
void opItems(std::vector<FEATURE *> &wrecks, std::vector<FEATURE *> &pickups)
{
	wrecks.clear();
	pickups.clear();
	if (selectedPlayer >= MAX_PLAYERS) { return; }
	for (BASE_OBJECT *o : gridStartIterate(s_op.c.x, s_op.c.y, s_op.r))
	{
		if (o->type != OBJ_FEATURE || o->died || !o->visible[selectedPlayer]) { continue; }
		FEATURE *f = static_cast<FEATURE *>(o);
		if (modrecover::isTowable(f)) { wrecks.push_back(f); continue; }
		if (f->psStats->subType == FEAT_OIL_DRUM || f->psStats->subType == FEAT_GEN_ARTE) { pickups.push_back(f); }
	}
	auto byId = [](const FEATURE *a, const FEATURE *b) { return a->id < b->id; };
	std::sort(wrecks.begin(), wrecks.end(), byId);
	std::sort(pickups.begin(), pickups.end(), byId);
}

void opEnd(bool sendHome)
{
	if (sendHome)
	{
		size_t i = 0;
		for (const auto *v : { &s_op.collectors, &s_op.escorts })
		{
			for (uint32_t id : *v)
			{
				DROID *d = opDroid(id);
				if (d != nullptr && !modrecover::carrying(d)) { send(d, DroidOrder(DORDER_MOVE, spread(s_op.deploy, i++)), false); }
			}
		}
	}
	s_op = SalvageOp();
}

void startSalvage(const Vector2i &c, int r)
{
	if (selectedPlayer >= MAX_PLAYERS) { return; }
	if (s_op.active) { opEnd(false); }
	s_op.c = c;
	s_op.r = r;
	std::vector<FEATURE *> wrecks, pickups;
	opItems(wrecks, pickups);
	if (wrecks.empty() && pickups.empty())
	{
		addConsoleMessage(_("Salvage: nothing to recover in this area"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		return;
	}
	// Bereitstellungspunkt: am Rand der eigenen Basis (Gebaeude, das dem Ziel am naechsten liegt), 5 Felder in Richtung Ziel
	const STRUCTURE *home = nullptr;
	int64_t homeD = 0;
	for (const STRUCTURE *st : gameWorld.objects.structures[selectedPlayer])
	{
		if (st->died || st->status != SS_BUILT) { continue; }
		const int64_t d = dist2(st->pos.xy(), c);
		if (home == nullptr || d < homeD) { home = st; homeD = d; }
	}
	const modlayer::Layer view = modlayer::current();
	std::vector<DROID *> pool;
	for (DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d->died || d->isTransporter() || isAir(d) || modlayer::layerOf(d) != view || !idleUnit(d)) { continue; }
		pool.push_back(d);
	}
	Vector2i deploy = c;
	if (home != nullptr)
	{
		const double dx = c.x - home->pos.x, dy = c.y - home->pos.y;
		const double len = std::max(1.0, std::sqrt(dx * dx + dy * dy));
		const double step = std::min(len * 0.5, 5.0 * DIST_TILE);
		deploy = Vector2i(home->pos.x + static_cast<int>(dx / len * step), home->pos.y + static_cast<int>(dy / len * step));
	}
	else if (!pool.empty())
	{
		std::vector<Vector2i> ps;
		for (const DROID *d : pool) { ps.push_back(d->pos.xy()); }
		deploy = centroid(ps);
	}
	deploy.x = std::clamp(deploy.x, TILE_UNITS * 2, world_coord(gameWorld.map.width - 3));
	deploy.y = std::clamp(deploy.y, TILE_UNITS * 2, world_coord(gameWorld.map.height - 3));
	s_op.deploy = deploy;
	std::sort(pool.begin(), pool.end(), [&deploy](const DROID *a, const DROID *b) {
		const int64_t da = dist2(a->pos.xy(), deploy), db = dist2(b->pos.xy(), deploy);
		return da != db ? da < db : a->id < b->id;
	});
	const size_t wantTow = std::min<size_t>(wrecks.size(), 3);
	const size_t wantCol = pickups.empty() ? 0 : std::min<size_t>(2, (pickups.size() + 2) / 3);
	for (DROID *d : pool)
	{
		if (modrecover::isTowTruck(d))
		{
			if (s_op.towers.size() < wantTow) { s_op.towers.push_back(d->id); }
			else if (s_op.collectors.size() < wantCol) { s_op.collectors.push_back(d->id); }
		}
		else if (isSupportUnit(d) && s_op.collectors.size() < wantCol)
		{
			s_op.collectors.push_back(d->id);
		}
	}
	if (s_op.towers.empty() && s_op.collectors.empty())
	{
		addConsoleMessage(wrecks.empty() ? _("Salvage: no free trucks or repair units for a salvage team")
		                                 : _("Salvage: no free trucks - wrecks need a flatbed truck (scavenger truck with cargo bed)"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		s_op = SalvageOp();
		return;
	}
	const size_t wantEsc = std::min<size_t>(6, 2 + s_op.towers.size() + s_op.collectors.size());
	for (DROID *d : pool)
	{
		if (s_op.escorts.size() >= wantEsc) { break; }
		if (isCombat(d) && !isArty(d) && d->droidType != DROID_PERSON && !inOp(d->id)) { s_op.escorts.push_back(d->id); }
	}
	for (DROID *d : pool)   // zur Not auch Infanterie als Eskorte
	{
		if (s_op.escorts.size() >= wantEsc) { break; }
		if (isCombat(d) && !isArty(d) && !inOp(d->id)) { s_op.escorts.push_back(d->id); }
	}
	s_op.active = true;
	s_op.phase = 0;
	s_op.phaseStart = gameTime;
	s_op.nextStep = gameTime + OP_STEP_MS;
	size_t i = 0;
	for (const auto *v : { &s_op.escorts, &s_op.collectors, &s_op.towers })
	{
		for (uint32_t id : *v)
		{
			DROID *d = opDroid(id);
			if (d != nullptr) { send(d, DroidOrder(DORDER_MOVE, spread(deploy, i++)), false); }
		}
	}
	char msg[200];
	snprintf(msg, sizeof(msg), _("Salvage team: %u salvage units, %u escorts - gathering at the deployment point"),
	         static_cast<unsigned>(s_op.towers.size() + s_op.collectors.size()), static_cast<unsigned>(s_op.escorts.size()));
	addConsoleMessage(msg, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
}

void opPrune(std::vector<uint32_t> &v, bool dropCarriers)
{
	v.erase(std::remove_if(v.begin(), v.end(), [dropCarriers](uint32_t id) {
		DROID *d = opDroid(id);
		return d == nullptr || (dropCarriers && modrecover::carrying(d));
	}), v.end());
}

void rememberPlan(const DROID *d, const DroidOrder &o, const Vector2i &anchor, int face = -1)
{
	PlanTask t;
	t.order = o;
	t.anchor = anchor;
	t.born = gameTime;
	t.face = face;
	t.sent = gameTime;
	s_plan[d->id] = t;
}

// MOD (Wunsch 19.11. 16:44): "Die Einheiten in der Defensive line sollen sich zum schluss in Richtung der Stäbe drehen."
// Wer seinen Platz auf der Verteidigungslinie erreicht hat (kein Befehl, steht still, kaempft nicht, hoechstens 2 Felder vom Platz),
// dreht sich sichtbar (gut 100 Grad pro Sekunde) in die Richtung der Querstriche - also zur Feindseite. Einmal pro Ankunft:
// verlaesst die Einheit ihren Platz (Kampf, Spielerbefehl) und kommt zurueck, dreht sie sich wieder ein.
unsigned s_nextFace = 0;
void faceTick()
{
	if (gameTime < s_nextFace || selectedPlayer >= MAX_PLAYERS) { return; }
	s_nextFace = gameTime + 150;
	const int64_t near2 = static_cast<int64_t>(2 * DIST_TILE) * (2 * DIST_TILE);
	for (auto &kv : s_plan)
	{
		PlanTask &t = kv.second;
		if (t.face < 0) { continue; }
		DROID *d = IdToDroid(gameWorld.objects, kv.first, selectedPlayer);
		if (d == nullptr || d->died) { continue; }
		const bool atSpot = dist2(d->pos.xy(), t.anchor) <= near2;
		// (der Linienbefehl selbst zaehlt als Spielerbefehl - nur ein spaeterer Befehl des Spielers hebt die Blickrichtung auf)
		const unsigned sincePlayer = modcontrol::sincePlayerOrder(d);
		if (sincePlayer != 0xffffffffu && sincePlayer + 800u < gameTime - t.sent) { t.face = -1; continue; }
		const bool idle = d->order.type == DORDER_NONE && d->action == DACTION_NONE && d->sMove.Status == MOVEINACTIVE;
		if (!atSpot || !idle) { if (!atSpot) { t.faced = false; } continue; }
		if (t.faced) { continue; }
		const uint16_t want = static_cast<uint16_t>(t.face);
		const int16_t diff = static_cast<int16_t>(want - d->rot.direction);
		if (std::abs(static_cast<int>(diff)) <= DEG(2)) { d->rot.direction = want; t.faced = true; continue; }
		const int step = std::clamp<int>(diff, -DEG(16), DEG(16));
		d->rot.direction = static_cast<uint16_t>(d->rot.direction + step);
	}
}

void planTick()
{
	if (gameTime < s_nextPlanCheck || selectedPlayer >= MAX_PLAYERS) { return; }
	s_nextPlanCheck = gameTime + 2000;
	const int64_t far2 = static_cast<int64_t>(6 * DIST_TILE) * (6 * DIST_TILE);
	for (auto it = s_plan.begin(); it != s_plan.end();)
	{
		DROID *d = IdToDroid(gameWorld.objects, it->first, selectedPlayer);
		if (d == nullptr || d->died || gameTime - it->second.born > PLAN_LIFE) { it = s_plan.erase(it); continue; }
		if (d->order.type == DORDER_NONE && !modcontrol::manual(d) && !inOp(d->id) && !modrecover::carrying(d)
		    && dist2(d->pos.xy(), it->second.anchor) > far2)
		{
			send(d, it->second.order, false);   // zurueck zum Oberbefehl
			it->second.sent = gameTime;
		}
		++it;
	}
}

// MOD (19.11. 17:34): Minen raeumen im Gebiet. Jede Einheit faehrt/laeuft ihre Bahnen (Wegpunkte) ab, einer nach dem anderen. Infanterie
// sieht eine feindliche Mine (2,5 Felder), geht hin und bleibt stehen, bis sie entschaerft ist (modmines: 3 s), dann geht es weiter.
// Raeumpanzer fahren mit drehender Walze. Ein eigener Spielerbefehl (modcontrol) beendet den Auftrag der Einheit.
struct SweepJob
{
	std::vector<Vector2i> wp;
	size_t idx = 0;
	uint32_t mine = 0;          // Infanterie: gerade angesteuerte Mine
	unsigned lastSend = 0;
	bool resend = false;
	bool flail = false;
};
std::map<uint32_t, SweepJob> s_sweep;
unsigned s_nextSweep = 0;

bool insidePoly(const std::vector<Vector2i> &poly, int64_t x, int64_t y);

const STRUCTURE *enemyMineNear(const DROID *d, int r, const std::map<uint32_t, SweepJob> &jobs)
{
	static int s_mineIdx = -2;
	if (s_mineIdx == -2) { s_mineIdx = getStructStatFromName(WzString::fromUtf8("A0Mine")); }
	if (s_mineIdx < 0) { return nullptr; }
	const STRUCTURE_STATS *ms = &asStructureStats[s_mineIdx];
	const STRUCTURE *best = nullptr;
	int64_t bestD = static_cast<int64_t>(r) * r;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		if (p == d->player || aiCheckAlliances(p, d->player)) { continue; }
		for (const STRUCTURE *st : gameWorld.objects.structures[p])
		{
			if (st == nullptr || st->died || st->pStructureType != ms || st->visible[d->player] == 0) { continue; }
			const int64_t dd = dist2(d->pos.xy(), st->pos.xy());
			if (dd >= bestD) { continue; }
			bool taken = false;
			for (const auto &kv : jobs) { if (kv.first != d->id && kv.second.mine == st->id) { taken = true; break; } }
			if (!taken) { best = st; bestD = dd; }
		}
	}
	return best;
}

void sweepTick()
{
	if (s_sweep.empty() || gameTime < s_nextSweep || selectedPlayer >= MAX_PLAYERS) { return; }
	s_nextSweep = gameTime + 400;
	const int64_t reach2 = static_cast<int64_t>(DIST_TILE * 6 / 5) * (DIST_TILE * 6 / 5);
	for (auto it = s_sweep.begin(); it != s_sweep.end();)
	{
		DROID *d = IdToDroid(gameWorld.objects, it->first, selectedPlayer);
		SweepJob &j = it->second;
		if (d == nullptr || d->died) { it = s_sweep.erase(it); continue; }
		// eigene Befehle kommen ueber die Befehlswarteschlange zurueck und zaehlen dort als Spielerbefehl - nur ein Befehl, der
		// deutlich nach unserem letzten kam, ist ein Eingriff des Spielers (dann endet der Raeumauftrag dieser Einheit)
		const unsigned sincePlayer = modcontrol::sincePlayerOrder(d);
		if (j.lastSend != 0 && sincePlayer != 0xffffffffu && sincePlayer + 800u < gameTime - j.lastSend) { it = s_sweep.erase(it); continue; }
		// Infanterie: entdeckte Mine zuerst
		if (!j.flail)
		{
			if (j.mine != 0)
			{
				const STRUCTURE *ms = IdToStruct(j.mine, ANYPLAYER);
				if (ms == nullptr || ms->died) { j.mine = 0; j.resend = true; }
				else { ++it; continue; }   // steht an der Mine und entschaerft
			}
			if (const STRUCTURE *mine = enemyMineNear(d, DIST_TILE * 5 / 2, s_sweep))
			{
				j.mine = mine->id;
				send(d, DroidOrder(DORDER_MOVE, mine->pos.xy()), false);
				j.lastSend = gameTime;
				++it;
				continue;
			}
		}
		while (j.idx < j.wp.size() && dist2(d->pos.xy(), j.wp[j.idx]) <= reach2) { ++j.idx; j.resend = true; }
		if (j.idx >= j.wp.size())
		{
			if (j.flail && modmines::flailing(d)) { secondarySetState(d, gameWorld.objects, DSO_MINECLEAR, DSS_NONE); }
			it = s_sweep.erase(it);
			if (s_sweep.empty()) { addConsoleMessage(_("Mine sweep finished"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
			continue;
		}
		// naechsten Wegpunkt ansteuern (neu senden, wenn die Einheit steht, oder alle 8 s als Sicherheit)
		if (j.resend || j.lastSend == 0 || (d->order.type == DORDER_NONE && gameTime - j.lastSend > 1500) || gameTime - j.lastSend > 8000)
		{
			send(d, DroidOrder(DORDER_MOVE, j.wp[j.idx]), false);
			j.lastSend = gameTime;
			j.resend = false;
		}
		++it;
	}
}

/// Bahnen durch die Flaeche (hin und her, Abstand `lane`), nur Punkte im Gebiet; Bahnen reihum auf `n` Einheiten verteilt
std::vector<std::vector<Vector2i>> sweepLanes(const std::vector<Vector2i> &poly, int lane, size_t n)
{
	std::vector<std::vector<Vector2i>> out(std::max<size_t>(1, n));
	int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
	for (const Vector2i &p : poly) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
	if (x1 <= x0 || y1 <= y0) { return out; }
	const bool alongX = (x1 - x0) >= (y1 - y0);   // Bahnen laengs der laengeren Seite
	const int a0 = alongX ? y0 : x0, a1 = alongX ? y1 : x1;    // quer
	const int b0 = alongX ? x0 : y0, b1 = alongX ? x1 : y1;    // laengs
	std::vector<std::vector<Vector2i>> lanes;
	for (int a = a0 + lane / 2; a <= a1; a += lane)
	{
		std::vector<Vector2i> ln;
		for (int b = b0; b <= b1; b += DIST_TILE)
		{
			const int x = alongX ? b : a, y = alongX ? a : b;
			if (insidePoly(poly, x, y)) { ln.emplace_back(x, y); }
		}
		if (ln.empty()) { continue; }
		// nur Anfang und Ende jeder Bahn (gerade Strecke), lange Bahnen in Stuecken von 6 Feldern (Wegsuche bleibt kurz)
		std::vector<Vector2i> pts;
		for (size_t k = 0; k < ln.size(); k += 6) { pts.push_back(ln[k]); }
		if (pts.back() != ln.back()) { pts.push_back(ln.back()); }
		if (lanes.size() % 2 == 1) { std::reverse(pts.begin(), pts.end()); }
		lanes.push_back(pts);
	}
	if (lanes.empty()) { out[0].push_back(centroid(poly)); return out; }
	// zusammenhaengende Bahnbloecke je Einheit (kurze Wege zwischen den Bahnen)
	const size_t per = (lanes.size() + out.size() - 1) / out.size();
	for (size_t i = 0; i < lanes.size(); ++i)
	{
		auto &dst = out[std::min(out.size() - 1, i / std::max<size_t>(1, per))];
		dst.insert(dst.end(), lanes[i].begin(), lanes[i].end());
	}
	return out;
}

void opTick()
{
	if (!s_op.active || gameTime < s_op.nextStep) { return; }
	s_op.nextStep = gameTime + OP_STEP_MS;
	opPrune(s_op.escorts, false);
	opPrune(s_op.collectors, true);   // traegt ein Teil zur Werkstatt -> faellt aus dem Trupp
	opPrune(s_op.towers, true);       // hat ein Wrack geladen -> faehrt zur Werkstatt
	const unsigned age = gameTime - s_op.phaseStart;
	if (s_op.phase == 0)
	{
		bool all = true;
		const int64_t near2 = static_cast<int64_t>(5 * DIST_TILE) * (5 * DIST_TILE);
		for (const auto *v : { &s_op.escorts, &s_op.collectors, &s_op.towers })
		{
			for (uint32_t id : *v) { DROID *d = opDroid(id); if (d != nullptr && dist2(d->pos.xy(), s_op.deploy) > near2) { all = false; } }
		}
		if (s_op.collectors.empty() && s_op.towers.empty()) { addConsoleMessage(_("Salvage team lost"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); opEnd(true); return; }
		if (all || age >= OP_GATHER_MAX)
		{
			s_op.phase = 1;
			s_op.phaseStart = gameTime;
			size_t i = 0;
			for (uint32_t id : s_op.escorts) { DROID *d = opDroid(id); if (d != nullptr) { send(d, DroidOrder(DORDER_SCOUT, spread(s_op.c, i++)), false); } }
			for (const auto *v : { &s_op.collectors, &s_op.towers })
			{
				for (uint32_t id : *v) { DROID *d = opDroid(id); if (d != nullptr) { send(d, DroidOrder(DORDER_MOVE, spread(s_op.c, i++)), false); } }
			}
			addConsoleMessage(_("Salvage team assembled - moving out"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		}
		return;
	}
	std::vector<FEATURE *> wrecks, pickups;
	opItems(wrecks, pickups);
	if (s_op.phase == 1)
	{
		const int64_t arrive2 = static_cast<int64_t>(s_op.r + 2 * DIST_TILE) * (s_op.r + 2 * DIST_TILE);
		bool arrived = false;
		for (const auto *v : { &s_op.collectors, &s_op.towers })
		{
			for (uint32_t id : *v) { DROID *d = opDroid(id); if (d != nullptr && dist2(d->pos.xy(), s_op.c) <= arrive2) { arrived = true; } }
		}
		if (s_op.collectors.empty() && s_op.towers.empty()) { opEnd(true); return; }
		if (!arrived && age < OP_GO_MAX) { return; }
		s_op.phase = 2;
		s_op.phaseStart = gameTime;
	}
	// Phase 2: bergen - jeder freie Berger holt das naechste noch nicht vergebene Ding
	std::vector<uint32_t> taken;
	auto busyWith = [](const DROID *d) -> uint32_t {
		return (d->order.type == DORDER_RECOVER && d->order.psObj != nullptr && !d->order.psObj->died) ? d->order.psObj->id : 0u;
	};
	for (const auto *v : { &s_op.collectors, &s_op.towers })
	{
		for (uint32_t id : *v) { DROID *d = opDroid(id); if (d != nullptr) { const uint32_t f = busyWith(d); if (f != 0) { taken.push_back(f); } } }
	}
	auto assign = [&](std::vector<uint32_t> &units, std::vector<FEATURE *> &items) {
		for (uint32_t id : units)
		{
			DROID *d = opDroid(id);
			if (d == nullptr || busyWith(d) != 0) { continue; }
			FEATURE *best = nullptr;
			int64_t bestD = 0;
			for (FEATURE *f : items)
			{
				if (std::find(taken.begin(), taken.end(), f->id) != taken.end()) { continue; }
				const int64_t dd = dist2(d->pos.xy(), f->pos.xy());
				if (best == nullptr || dd < bestD) { best = f; bestD = dd; }
			}
			if (best == nullptr) { continue; }
			taken.push_back(best->id);
			send(d, DroidOrder(DORDER_RECOVER, best), false);
		}
	};
	assign(s_op.towers, wrecks);
	assign(s_op.collectors, pickups);
	if (s_op.collectors.empty() && !pickups.empty()) { assign(s_op.escorts, pickups); }   // keine Berger mehr: die Eskorte sammelt Beute/Artefakte
	// Eskorte: sichert das Gebiet (alle 15 s ein neuer Punkt im Kreis)
	if (gameTime >= s_op.nextEscort)
	{
		s_op.nextEscort = gameTime + 15000;
		size_t i = 0;
		for (uint32_t id : s_op.escorts)
		{
			DROID *d = opDroid(id);
			if (d == nullptr || busyWith(d) != 0 || (d->order.type != DORDER_NONE && d->order.type != DORDER_SCOUT)) { ++i; continue; }
			const uint16_t ang = static_cast<uint16_t>((gameTime / 15000u) * 9973u + i * 21011u);
			const int rr = s_op.r * static_cast<int>(1 + (i * 37u + gameTime / 15000u) % 3u) / 4;
			send(d, DroidOrder(DORDER_SCOUT, Vector2i(s_op.c.x + iSinR(ang, rr), s_op.c.y + iCosR(ang, rr))), false);
			++i;
		}
	}
	const bool workLeft = (!wrecks.empty() && !s_op.towers.empty()) || !pickups.empty();
	bool anyBusy = false;
	for (const auto *v : { &s_op.collectors, &s_op.towers, &s_op.escorts })
	{
		for (uint32_t id : *v) { DROID *d = opDroid(id); if (d != nullptr && busyWith(d) != 0) { anyBusy = true; } }
	}
	if ((!workLeft && !anyBusy) || (s_op.towers.empty() && s_op.collectors.empty() && pickups.empty()) || gameTime - s_op.phaseStart >= OP_WORK_MAX)
	{
		addConsoleMessage(_("Salvage complete - team returning"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		opEnd(true);
	}
}

std::vector<Vector2i> circlePts(const Vector2i &c, int r, int n)
{
	std::vector<Vector2i> p;
	for (int k = 0; k < n; ++k)
	{
		const uint16_t a = static_cast<uint16_t>(k * 65536 / n);
		p.emplace_back(c.x + iSinR(a, r), c.y + iCosR(a, r));
	}
	return p;
}

// ------------------------------------------------------------------------------------------------- Auftraege

// MOD (17.11. 18:12/18:13): Stellen fuer Panzersperren (Schachbrett - Fahrzeuge kommen nicht durch, auch nicht schraeg) bzw. Minen
// (verstreut, etwa jedes dritte Feld) in der gezeichneten Flaeche; nur freie, befahrbare Felder
bool insidePoly(const std::vector<Vector2i> &poly, int64_t x, int64_t y)
{
	bool in = false;
	for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
	{
		const int64_t xi = poly[i].x, yi = poly[i].y, xj = poly[j].x, yj = poly[j].y;
		if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) { in = !in; }
	}
	return in;
}

std::vector<Vector2i> fieldSpots(const std::vector<Vector2i> &raw, bool mines)
{
	std::vector<Vector2i> out;
	if (raw.size() < 3) { return out; }
	int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
	for (const Vector2i &p : raw) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
	// (20.11.) Raster in Plan-Kacheln (DIST_TILE = Original-Kachel): ein Igel/eine Mine belegt eine ganze Plan-Kachel - in der Viertel-Fassung
	// lag das Schachbrett auf Feinfeldern und die Bauwerke ueberlappten (viele Bauauftraege schlugen fehl)
	const int pw = world_coord(gameWorld.map.width) / DIST_TILE, ph = world_coord(gameWorld.map.height) / DIST_TILE;
	const int tx0 = std::max(1, std::max(0, x0) / DIST_TILE), ty0 = std::max(1, std::max(0, y0) / DIST_TILE);
	const int tx1 = std::min(pw - 2, std::max(0, x1) / DIST_TILE), ty1 = std::min(ph - 2, std::max(0, y1) / DIST_TILE);
	if (tx1 < tx0 || ty1 < ty0 || (tx1 - tx0) * (ty1 - ty0) > 4096) { return out; }
	for (int ty = ty0; ty <= ty1; ++ty)
	{
		for (int tx = tx0; tx <= tx1; ++tx)
		{
			const int cx = tx * DIST_TILE + DIST_TILE / 2, cy = ty * DIST_TILE + DIST_TILE / 2;
			if (!insidePoly(raw, cx, cy)) { continue; }
			if (mines)
			{
				uint32_t h = static_cast<uint32_t>(tx) * 73856093u ^ static_cast<uint32_t>(ty) * 19349663u;
				h *= 2654435761u;
				if ((h >> 16) % 100u >= 36u) { continue; }
			}
			else if ((tx + ty) % 2 != 0) { continue; }
			bool ok = true;   // alle Kartenfelder der Plan-Kachel frei
			for (int fy = map_coord(ty * DIST_TILE); ok && fy <= map_coord(ty * DIST_TILE + DIST_TILE - 1); ++fy)
			{
				for (int fx = map_coord(tx * DIST_TILE); ok && fx <= map_coord(tx * DIST_TILE + DIST_TILE - 1); ++fx)
				{
					const MAPTILE *t = mapTile(gameWorld.map, fx, fy);
					ok = !(t == nullptr || TileHasStructure(t) || TileHasFeature(t) || terrainType(t) == TER_WATER || terrainType(t) == TER_CLIFFFACE);
				}
			}
			if (!ok) { continue; }
			out.emplace_back(cx, cy);
		}
	}
	const size_t cap = mines ? 40 : 64;
	if (out.size() > cap)
	{
		const Vector2i c = centroid(raw);
		std::stable_sort(out.begin(), out.end(), [&c](const Vector2i &a, const Vector2i &b) { return dist2(a, c) < dist2(b, c); });
		out.resize(cap);
	}
	return out;
}

// Verteidigungslinie: die Querstriche zeigen zur Feindseite = weg von der eigenen Basis (Mitte der eigenen Bauwerke)
int defendSide(const std::vector<Vector2i> &pts)
{
	if (pts.size() < 2 || selectedPlayer >= MAX_PLAYERS) { return 1; }
	int64_t sx = 0, sy = 0, cnt = 0;
	for (const STRUCTURE *st : gameWorld.objects.structures[selectedPlayer]) { if (st != nullptr && !st->died) { sx += st->pos.x; sy += st->pos.y; ++cnt; } }
	if (cnt == 0)
	{
		for (const DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d != nullptr && !d->died) { sx += d->pos.x; sy += d->pos.y; ++cnt; } }
	}
	if (cnt == 0) { return 1; }
	const int64_t hx0 = sx / cnt, hy0 = sy / cnt;
	int64_t acc = 0;
	for (size_t i = 1; i < pts.size(); ++i)
	{
		const int64_t tx = pts[i].x - pts[i - 1].x, ty = pts[i].y - pts[i - 1].y;
		const int64_t hx = hx0 - pts[i].x, hy = hy0 - pts[i].y;
		acc += (tx * hy - ty * hx) / 64;
	}
	return acc > 0 ? -1 : 1;   // Normale (-ty, tx) zeigt zur Basis, wenn acc > 0 -> Striche auf die andere Seite
}

void applyStroke(Mode m, const std::vector<Vector2i> &raw)
{
	if (raw.empty()) { return; }
	if (m == M_RECOVER)
	{
		const Vector2i c = centroid(raw);
		int64_t far2 = 0;
		for (const Vector2i &p : raw) { far2 = std::max(far2, dist2(p, c)); }
		const int r = std::clamp(static_cast<int>(std::sqrt(static_cast<double>(far2))), 3 * DIST_TILE, 16 * DIST_TILE);
		startSalvage(c, r);
		return;
	}
	std::vector<DROID *> units = pickUnits(m, raw);
	if (s_append && !s_chain.empty() && selectedPlayer < MAX_PLAYERS)
	{
		// (18.11.) weitere Linie mit Shift: ohne Auswahl behalten die Einheiten der vorigen Linien ihre Linie
		bool anySelected = false;
		for (const DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d != nullptr && d->selected) { anySelected = true; break; } }
		if (!anySelected)
		{
			units.erase(std::remove_if(units.begin(), units.end(), [](const DROID *d) {
				return std::find(s_chain.begin(), s_chain.end(), d->id) != s_chain.end();
			}), units.end());
		}
	}
	const Vector2i c = centroid(raw);
	if (units.empty())
	{
		return;
	}
	for (const DROID *d : units) { s_chain.push_back(d->id); }
	switch (m)
	{
	case M_MOVE:
	case M_ATTACK:
	{
		const std::vector<Vector2i> wp = resample(raw, std::min<size_t>(5, std::max<size_t>(2, raw.size())));
		const DROID_ORDER ord = (m == M_MOVE) ? DORDER_MOVE : DORDER_SCOUT;
		for (size_t i = 0; i < units.size(); ++i)
		{
			for (size_t k = 1; k < wp.size(); ++k)
			{
				const Vector2i p = (k + 1 == wp.size()) ? spread(wp[k], i) : wp[k];
				send(units[i], DroidOrder(ord, p), k > 1);
				if (k + 1 == wp.size()) { rememberPlan(units[i], DroidOrder(ord, p), p); }
			}
		}
		break;
	}
	case M_RALLY:
		for (size_t i = 0; i < units.size(); ++i) { send(units[i], DroidOrder(DORDER_MOVE, spread(c, i)), false); rememberPlan(units[i], DroidOrder(DORDER_MOVE, spread(c, i)), spread(c, i)); }
		break;
	case M_DEFEND:
	{
		std::vector<DROID *> fighters, builders;
		for (DROID *d : units) { (isBuilder(d) ? builders : fighters).push_back(d); }
		// Kaempfer verteilen sich gleichmaessig auf der Linie
		const std::vector<Vector2i> spots = resample(raw, std::max<size_t>(1, fighters.size()));
		// Blickrichtung am Platz = Richtung der Querstriche (Normale der Linie zur Feindseite, wie tickSegs sie zeichnet)
		const int fside = defendSide(raw);
		const std::vector<Vector2i> fline = resample(raw, std::max<size_t>(24, raw.size()));
		auto faceAt = [&fline, fside](const Vector2i &p) {
			size_t bi = 0;
			int64_t bd = INT64_MAX;
			for (size_t k = 0; k < fline.size(); ++k) { const int64_t dd = dist2(fline[k], p); if (dd < bd) { bd = dd; bi = k; } }
			const Vector2i a = fline[bi > 0 ? bi - 1 : 0], b = fline[std::min(bi + 1, fline.size() - 1)];
			const int64_t tx = b.x - a.x, ty = b.y - a.y;
			if (tx == 0 && ty == 0) { return -1; }
			return static_cast<int>(iAtan2(static_cast<int32_t>(-ty * fside), static_cast<int32_t>(tx * fside)));
		};
		for (size_t i = 0; i < fighters.size(); ++i) { send(fighters[i], DroidOrder(DORDER_MOVE, spots[i]), false); rememberPlan(fighters[i], DroidOrder(DORDER_MOVE, spots[i]), spots[i], faceAt(spots[i])); }
		// MOD (Wunsch 20.11.): Bauer heben ein Grabennetz aus - Hauptgraben lueckenlos entlang der Linie, Gruben, Verbindungsgraeben nach
		// hinten; alle Stellungen mit der Front zu den Querstrichen (modbuild::planDefenseNet). Jeder Bauer bekommt einen zusammenhaengenden
		// Abschnitt (ungefaehr gleich viele Felder), in der Reihenfolge ihrer Lage an der Linie - niemand laeuft hin und her.
		if (!builders.empty() && selectedPlayer < MAX_PLAYERS)
		{
			const std::vector<modbuild::Placement> plan = modbuild::planDefenseNet(selectedPlayer, raw, fside);
			if (!plan.empty())
			{
				auto worldOfPlan = [](const Vector2i &t) { return Vector2i(t.x * DIST_TILE + DIST_TILE / 2, t.y * DIST_TILE + DIST_TILE / 2); };
				std::vector<int64_t> cum(plan.size() + 1, 0);
				for (size_t k = 0; k < plan.size(); ++k)
				{
					cum[k + 1] = cum[k] + 1 + std::abs(plan[k].tile2.x - plan[k].tile.x) + std::abs(plan[k].tile2.y - plan[k].tile.y);
				}
				// Lage eines Bauers an der Linie = Index des naechsten Planeintrags
				std::vector<std::pair<size_t, DROID *>> order;
				for (DROID *b : builders)
				{
					size_t best = 0;
					int64_t bd = INT64_MAX;
					for (size_t k = 0; k < plan.size(); ++k)
					{
						const int64_t dd = dist2(b->pos.xy(), worldOfPlan(plan[k].tile));
						if (dd < bd) { bd = dd; best = k; }
					}
					order.emplace_back(best, b);
				}
				std::stable_sort(order.begin(), order.end(), [](const std::pair<size_t, DROID *> &a, const std::pair<size_t, DROID *> &b) {
					return a.first != b.first ? a.first < b.first : a.second->id < b.second->id;
				});
				const size_t nb = std::min(order.size(), plan.size());
				const int64_t total = cum.back();
				for (size_t bi = 0; bi < order.size(); ++bi)
				{
					// mehr Bauer als Planeintraege: die uebrigen helfen im Abschnitt eines anderen mit
					const size_t chunk = bi % nb;
					const int64_t lo = total * static_cast<int64_t>(chunk) / static_cast<int64_t>(nb);
					const int64_t hi = total * static_cast<int64_t>(chunk + 1) / static_cast<int64_t>(nb);
					bool first = true;
					for (size_t k = 0; k < plan.size(); ++k)
					{
						const int64_t mid = (cum[k] + cum[k + 1]) / 2;   // Eintrag gehoert zum Abschnitt, in dem seine Mitte liegt
						if (mid < lo || mid >= hi) { continue; }
						const modbuild::Placement &pl = plan[k];
						send(order[bi].second, DroidOrder(DORDER_LINEBUILD, pl.stats, worldOfPlan(pl.tile), worldOfPlan(pl.tile2), static_cast<uint16_t>(pl.direction)), !first);
						first = false;
					}
				}
			}
		}
		break;
	}
	case M_OFFENSE:
	{
		// MOD (Wunsch 17.11. 18:37): "Im Prinzip das Gleiche wie die Verteidigungslinie, aber das Netzwerk ist auf Offensive ausgerichtet:
		// mehr Einheiten, weniger starre Verteidigung (aber nicht ganz ohne)."
		//  - Kampfeinheiten (auch Infanterie mit Spaten) sammeln sich in zwei Reihen auf der Linie (Angriffsbewegung: kaempfen unterwegs)
		//    und verfolgen danach Gegner von selbst;
		//  - nur Baufahrzeuge bauen (gibt es keine, graben hoechstens 3 Infanteristen): drei kurze Grabenstuecke und zwei MG-Posten
		//    zur Feindseite.
		std::vector<DROID *> fighters, builders;
		for (DROID *d : units) { if (isTruck(d) && !isAir(d)) { builders.push_back(d); } else if (isCombat(d) || modcombot::isModular(d)) { fighters.push_back(d); } }
		if (builders.empty())
		{
			for (DROID *d : units)
			{
				if (builders.size() < 3 && modbuild::isInfantryBuilder(d)) { builders.push_back(d); }
			}
			fighters.erase(std::remove_if(fighters.begin(), fighters.end(), [&builders](DROID *d) { return std::find(builders.begin(), builders.end(), d) != builders.end(); }), fighters.end());
		}
		const int side = defendSide(raw);
		const std::vector<Vector2i> line = resample(raw, std::max<size_t>(9, raw.size()));
		// Feindrichtung an einer Stelle der Linie (Normale zur Seite der Querstriche)
		auto enemyDir = [&line, side](size_t i) {
			const Vector2i a = line[i > 0 ? i - 1 : 0], b = line[std::min(i + 1, line.size() - 1)];
			const double tx = b.x - a.x, ty = b.y - a.y, l = std::max(1.0, std::sqrt(tx * tx + ty * ty));
			return std::make_pair(-ty / l * side, tx / l * side);
		};
		const bool twoRanks = fighters.size() > 10;
		const size_t front = twoRanks ? (fighters.size() + 1) / 2 : fighters.size();
		const std::vector<Vector2i> spotsA = resample(raw, std::max<size_t>(1, front));
		const std::vector<Vector2i> spotsB = resample(raw, std::max<size_t>(1, fighters.size() - front));
		for (size_t i = 0; i < fighters.size(); ++i)
		{
			Vector2i p = (i < front) ? spotsA[i] : spotsB[i - front];
			if (i >= front)
			{
				// zweite Reihe gut ein Feld hinter der Linie
				const size_t li = std::min(line.size() - 1, (i - front) * line.size() / std::max<size_t>(1, fighters.size() - front));
				const auto e = enemyDir(li);
				p.x -= static_cast<int>(e.first * DIST_TILE * 1.2);
				p.y -= static_cast<int>(e.second * DIST_TILE * 1.2);
			}
			p.x = std::clamp(p.x, TILE_UNITS, world_coord(gameWorld.map.width - 2));
			p.y = std::clamp(p.y, TILE_UNITS, world_coord(gameWorld.map.height - 2));
			send(fighters[i], DroidOrder(DORDER_SCOUT, p), false);
			rememberPlan(fighters[i], DroidOrder(DORDER_SCOUT, p), p);
			if (!modcombot::isModular(fighters[i])) { secondarySetState(fighters[i], gameWorld.objects, DSO_HALTTYPE, DSS_HALT_PURSUE); }
		}
		if (builders.empty()) { break; }
		std::vector<DroidOrder> plan;
		STRUCTURE_STATS *trench = firstAvailable({ "A0BaBaTrench", "A0BaBaTrenchEarth", "A0BaBaSandbagWall" });
		STRUCTURE_STATS *post = firstAvailable({ "A0BaBaMGNestPost" });
		const std::vector<Vector2i> seg = resample(raw, 7);
		if (trench != nullptr)
		{
			for (size_t k = 0; k + 1 < seg.size(); k += 2) { plan.emplace_back(DORDER_LINEBUILD, trench, seg[k], seg[k + 1], 0); }
		}
		if (post != nullptr)
		{
			for (size_t k : { static_cast<size_t>(2), static_cast<size_t>(4) })
			{
				const size_t li = std::min(line.size() - 1, k * (line.size() - 1) / 6);
				const auto e = enemyDir(li);
				Vector2i p = seg[k];
				p.x += static_cast<int>(e.first * DIST_TILE * 0.8);
				p.y += static_cast<int>(e.second * DIST_TILE * 0.8);
				const uint16_t dir = static_cast<uint16_t>(iAtan2(static_cast<int>(e.first * 4096), static_cast<int>(-e.second * 4096)));
				plan.emplace_back(DORDER_BUILD, post, p, dir);
			}
		}
		for (size_t k = 0; k < plan.size(); ++k) { send(builders[k % builders.size()], plan[k], k >= builders.size()); }
		break;
	}
	case M_TRAPS:
	case M_MINES:
	{
		// MOD (17.11. 18:12/18:13): Baufahrzeuge/Infanterie mit Spaten bauen das Feld, Stellen reihum verteilt
		STRUCTURE_STATS *st = (m == M_TRAPS) ? firstAvailable({ "A0BaBaHedgehog", "A0TankTrapHeavy", "A0TankTrap" }) : firstAvailable({ "A0Mine" });   // (20.11.) Sprite-Igel nur noch als letzte Wahl
		if (st == nullptr) { break; }
		std::vector<DROID *> builders;
		for (DROID *d : units) { if (isTruck(d) && !isAir(d)) { builders.push_back(d); } }
		if (builders.empty()) { for (DROID *d : units) { if (isBuilder(d)) { builders.push_back(d); } } }
		if (builders.empty()) { break; }
		const std::vector<Vector2i> spots = fieldSpots(raw, m == M_MINES);
		for (size_t k = 0; k < spots.size(); ++k)
		{
			send(builders[k % builders.size()], DroidOrder(DORDER_BUILD, st, spots[k], 0), k >= builders.size());
		}
		break;
	}
	case M_ARTYBASE:
	{
		auto worldOf = [](const Vector2i &t) { return Vector2i(t.x * DIST_TILE + DIST_TILE / 2, t.y * DIST_TILE + DIST_TILE / 2); };   // Plan-Kacheln
		std::vector<DroidOrder> plan;
		for (const modbuild::Placement &pl : artyBasePlan(c, artyBaseSize(raw)))
		{
			if (pl.line) { plan.emplace_back(DORDER_LINEBUILD, pl.stats, worldOf(pl.tile), worldOf(pl.tile2), 0); }
			else { plan.emplace_back(DORDER_BUILD, pl.stats, worldOf(pl.tile), 0); }
		}
		// alle Baufahrzeuge arbeiten denselben Plan ab (sie helfen sich gegenseitig an derselben Baustelle)
		for (DROID *d : units)
		{
			for (size_t k = 0; k < plan.size(); ++k) { send(d, plan[k], k > 0); }
		}
		break;
	}
	case M_ARTY:
	{
		const std::vector<Vector2i> tgt = resample(raw, std::max<size_t>(1, std::min<size_t>(units.size(), 8)));
		for (size_t i = 0; i < units.size(); ++i) { send(units[i], DroidOrder(DORDER_ATTACKGROUND, tgt[i % tgt.size()]), false); }
		break;
	}
	case M_AIR:
		for (size_t i = 0; i < units.size(); ++i)
		{
			send(units[i], DroidOrder(units[i]->isVtol() ? DORDER_PATROL : DORDER_SCOUT, spread(c, i)), false);
		}
		break;
	case M_CLEARMINES:
	{
		// Raeumpanzer: Walze an, Bahnen im Abstand von 1,5 Feldern (die Walze erfasst gut 1,5 Felder vor dem Bug);
		// Infanterie: Bahnen im Abstand von 4 Feldern (sie sieht Minen 2,5 Felder weit)
		std::vector<DROID *> flails, crew;
		for (DROID *d : units) { (modmines::isFlail(d) ? flails : crew).push_back(d); }
		for (DROID *d : units) { s_sweep.erase(d->id); }
		const auto give = [](std::vector<DROID *> &grp, const std::vector<std::vector<Vector2i>> &lanes, bool flail) {
			// jedem Bahnblock die naechste Einheit
			std::vector<bool> used(grp.size(), false);
			for (const auto &ln : lanes)
			{
				if (ln.empty()) { continue; }
				int best = -1; int64_t bd = INT64_MAX;
				for (size_t i = 0; i < grp.size(); ++i) { if (!used[i]) { const int64_t dd = dist2(grp[i]->pos.xy(), ln.front()); if (dd < bd) { bd = dd; best = static_cast<int>(i); } } }
				if (best < 0) { break; }
				used[static_cast<size_t>(best)] = true;
				DROID *d = grp[static_cast<size_t>(best)];
				SweepJob j; j.wp = ln; j.flail = flail;
				s_sweep[d->id] = j;
				if (flail && !modmines::flailing(d)) { secondarySetState(d, gameWorld.objects, DSO_MINECLEAR, DSS_MINECLEAR_SET); }
			}
		};
		if (!flails.empty()) { give(flails, sweepLanes(raw, DIST_TILE * 3 / 2, flails.size()), true); }
		if (!crew.empty()) { give(crew, sweepLanes(raw, DIST_TILE * 4, std::min<size_t>(crew.size(), 12)), false); }
		s_nextSweep = 0;
		addConsoleMessage(_("Mine sweep: the area is combed lane by lane"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		break;
	}
	case M_DEMOLISH:
	{
		// eigene Bauwerke im Gebiet; jeder Bauer nimmt reihum das naechstgelegene noch freie Bauwerk (Kette), erster Befehl ersetzt
		std::vector<STRUCTURE *> todo;
		const modlayer::Layer view = modlayer::current();
		for (STRUCTURE *st : gameWorld.objects.structures[selectedPlayer])
		{
			if (st == nullptr || st->died || modlayer::layerOf(st) != view) { continue; }
			if (insidePoly(raw, st->pos.x, st->pos.y)) { todo.push_back(st); }
		}
		if (todo.empty()) { addConsoleMessage(_("Demolish: none of your structures in this area"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); break; }
		std::vector<Vector2i> at;
		for (DROID *d : units) { at.push_back(d->pos.xy()); }
		std::vector<bool> done(todo.size(), false);
		std::vector<int> count(units.size(), 0);
		size_t left = todo.size();
		while (left > 0)
		{
			for (size_t u = 0; u < units.size() && left > 0; ++u)
			{
				int best = -1; int64_t bd = INT64_MAX;
				for (size_t i = 0; i < todo.size(); ++i) { if (!done[i]) { const int64_t dd = dist2(at[u], todo[i]->pos.xy()); if (dd < bd) { bd = dd; best = static_cast<int>(i); } } }
				if (best < 0) { left = 0; break; }
				done[static_cast<size_t>(best)] = true; --left;
				STRUCTURE *st = todo[static_cast<size_t>(best)];
				send(units[u], DroidOrder(DORDER_DEMOLISH, st), count[u] > 0 || s_append);   // (17:45) mit Shift: an die Warteschlange anhaengen
				++count[u];
				at[u] = st->pos.xy();
			}
		}
		addConsoleMessage(_("Demolish: builders tear down the structures in the area"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		break;
	}
	case M_COMBOT:
	{
		// zum Gebiet vorruecken, dann zwischen zwei entfernten Punkten des Gebiets patrouillieren
		Vector2i a = raw.front(), b = raw.front();
		int64_t best = -1;
		for (const Vector2i &p : raw) { const int64_t d2 = dist2(p, a); if (d2 > best) { best = d2; b = p; } }
		for (size_t i = 0; i < units.size(); ++i)
		{
			send(units[i], DroidOrder(DORDER_SCOUT, spread(a, i)), false);
			send(units[i], DroidOrder(DORDER_PATROL, spread(b, i)), true);
			rememberPlan(units[i], DroidOrder(DORDER_SCOUT, spread(a, i)), spread(a, i));
		}
		break;
	}
	default:
		break;
	}
}

// ------------------------------------------------------------------------------------------------- Zeichnen im 3D-Bild

std::function<bool(const Vector2i &, Vector2i &)> s_proj;   // (17.11.) grosse Karte: Weltpunkt -> Bildschirmpunkt statt 3D-Projektion

bool project(const Vector2i &w, const glm::mat4 &m, Vector2i &out)
{
	if (s_proj) { return s_proj(w, out); }
	const int wx = std::clamp(w.x, 0, world_coord(gameWorld.map.width) - 1);
	const int wy = std::clamp(w.y, 0, world_coord(gameWorld.map.height) - 1);
	const Vector3i pos(wx, map_Height(gameWorld.map, wx, wy) + 10, -wy);
	return pie_RotateProjectWithPerspective(&pos, m, &out) > 0;
}


// MOD (Wunsch 16.11. 21:34): dicke, sich verjuengende Kriegskarten-Pfeile (wie auf den Beispielbildern) und Linien/Kreise mit leuchtendem Hof
// - Bildschirmpunkte gleichmaessig neu verteilt (glatt), Strich aus weichen, gedrehten Rundboxen; Schwanz blendet ein; Pfeilspitze aus Scheiben.
std::vector<glm::vec2> screenPath(const std::vector<Vector2i> &pts, const glm::mat4 &m, bool closed)
{
	std::vector<glm::vec2> raw;
	for (const Vector2i &p : pts)
	{
		Vector2i sp(0, 0);
		if (project(p, m, sp)) { raw.emplace_back(static_cast<float>(sp.x), static_cast<float>(sp.y)); }
	}
	if (closed && raw.size() >= 3) { raw.push_back(raw.front()); }
	if (raw.size() < 2) { return raw; }
	// Glaetten (Chaikin, 2 Durchgaenge) - Zeichnungen mit der Maus sind zittrig
	for (int it = 0; it < 2; ++it)
	{
		std::vector<glm::vec2> o;
		o.push_back(raw.front());
		for (size_t i = 0; i + 1 < raw.size(); ++i)
		{
			o.push_back(raw[i] * 0.75f + raw[i + 1] * 0.25f);
			o.push_back(raw[i] * 0.25f + raw[i + 1] * 0.75f);
		}
		o.push_back(raw.back());
		raw.swap(o);
	}
	// gleichmaessig neu verteilen (etwa alle 7 Bildpunkte, hoechstens 160 Punkte)
	float total = 0.f;
	for (size_t i = 1; i < raw.size(); ++i) { total += glm::length(raw[i] - raw[i - 1]); }
	if (total < 1.f) { return raw; }
	const int n = std::clamp(static_cast<int>(total / 7.f), 2, 160);
	std::vector<glm::vec2> out;
	out.push_back(raw.front());
	float want = total / static_cast<float>(n), acc = 0.f;
	for (size_t i = 1; i < raw.size(); ++i)
	{
		const glm::vec2 a = raw[i - 1], b = raw[i];
		float seg = glm::length(b - a);
		float t0 = 0.f;
		while (acc + seg * (1.f - t0) >= want && seg > 0.f)
		{
			const float t = t0 + (want - acc) / seg;
			out.push_back(a + (b - a) * t);
			t0 = t; acc = 0.f;
		}
		acc += seg * (1.f - t0);
	}
	if (glm::length(out.back() - raw.back()) > 1.f) { out.push_back(raw.back()); }
	return out;
}

// MOD (Wunsch 17.11. 18:10): "ALLE Pfeil- und Befehlsmarker genauso schoen machen wie unsere Sensoren- und Reichweitenmarker. Gleich
// leuchtend und schoen rendern. Und noch dicker." - Aufbau wie die Reichweitenringe: heller, fast weisser Kern, darunter ein weicher
// farbiger Lichthof in zwei Stufen, in der Mitte eine heisse Linie. Lagen: 0 aeusserer Hof, 1 innerer Hof, 2 Kern, 3 heisse Mitte.
PIELIGHT mixCol(const ModeInfo &mi, float toWhite, float a)
{
	const auto ch = [toWhite](unsigned char c) { return static_cast<UBYTE>(std::min(255, static_cast<int>(c + (255 - c) * toWhite))); };
	return pal_RGBA(ch(mi.r), ch(mi.g), ch(mi.b), static_cast<UBYTE>(std::clamp(static_cast<int>(a), 0, 255)));
}

struct Seg
{
	glm::vec2 a, b;
	float w;        // Breite des Kerns
	float alpha;    // 0..255
	bool box;       // Scheibe (Pfeilspitze/Pyramide): eckig statt Kapsel
};

void drawSegLayer(const Seg &g, int layer, const ModeInfo &mi, float twOverride = -1.f)
{
	const glm::vec2 d = g.b - g.a;
	const float len = glm::length(d);
	if (g.w <= 0.5f || g.alpha <= 0.f) { return; }
	const float sc = modui::uiScale();
	float ww = g.w, soft = 1.6f, tw = 0.35f, am = 1.f, corner = g.box ? 1.f : g.w * 0.5f;
	switch (layer)
	{
	case 0: ww = g.w + 34.f * sc; soft = ww * 0.5f; tw = 0.15f; am = 0.075f; corner = ww * 0.5f; break;
	case 1: ww = g.w + 14.f * sc; soft = 7.f * sc; tw = 0.35f; am = 0.2f; corner = ww * 0.5f; break;
	case 2: break;
	default:
		if (g.box) { return; }
		ww = std::max(2.f, g.w * 0.32f); soft = 1.2f; tw = 0.8f; am = 0.9f; corner = ww * 0.5f; break;
	}
	if (twOverride >= 0.f) { tw = twOverride; }
	const glm::vec2 c = (g.a + g.b) * 0.5f;
	const float along = g.box ? std::max(1.f, len) + (layer <= 1 ? ww - g.w : 0.f) : len + ww;
	pie_DrawSoftRoundedBoxRotated(c.x, c.y, along, ww, len > 0.01f ? std::atan2(d.y, d.x) : 0.f, mixCol(mi, tw, g.alpha * am), corner, soft);
}

void drawSegs(const std::vector<Seg> &segs, const ModeInfo &mi)
{
	for (int layer = 0; layer < 4; ++layer)
	{
		for (const Seg &g : segs) { drawSegLayer(g, layer, mi); }
	}
}

// gefuelltes Dreieck aus Querscheiben (Pfeilspitze, Pyramide): Spitze tip, Grundlinie bei base, halbe Grundbreite halfW
void triangleSegs(std::vector<Seg> &out, const glm::vec2 &base, const glm::vec2 &tip, float halfW, float alpha)
{
	const glm::vec2 d = tip - base;
	const float L = glm::length(d);
	if (L < 1.f) { return; }
	const glm::vec2 dir = d / L, perp(-dir.y, dir.x);
	const int K = std::clamp(static_cast<int>(L / 2.5f), 6, 16);
	for (int k = 0; k < K; ++k)
	{
		const float f = (static_cast<float>(k) + 0.5f) / static_cast<float>(K);
		const glm::vec2 c = base + d * f;
		const float across = std::max(1.f, 2.f * halfW * (1.f - f));
		// Scheibe quer zur Richtung: a/b liegen auf der Querachse, Breite = Dicke entlang der Richtung
		out.push_back(Seg{ c - perp * (across * 0.5f), c + perp * (across * 0.5f), L / static_cast<float>(K) + 1.5f, alpha, true });
	}
}

// Chaikin-Glaettung (wie screenPath), damit Querstriche/Felder genau an der gezeichneten Linie sitzen
std::vector<glm::vec2> smoothWorld(const std::vector<Vector2i> &pts)
{
	std::vector<glm::vec2> raw;
	for (const Vector2i &p : pts) { raw.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y)); }
	if (raw.size() < 3) { return raw; }
	for (int it = 0; it < 2; ++it)
	{
		std::vector<glm::vec2> o;
		o.push_back(raw.front());
		for (size_t i = 0; i + 1 < raw.size(); ++i)
		{
			o.push_back(raw[i] * 0.75f + raw[i + 1] * 0.25f);
			o.push_back(raw[i] * 0.25f + raw[i + 1] * 0.75f);
		}
		o.push_back(raw.back());
		raw.swap(o);
	}
	return raw;
}

bool projectF(const glm::vec2 &w, const glm::mat4 &m, glm::vec2 &out)
{
	Vector2i sp(0, 0);
	if (!project(Vector2i(static_cast<int>(w.x), static_cast<int>(w.y)), m, sp)) { return false; }
	out = glm::vec2(static_cast<float>(sp.x), static_cast<float>(sp.y));
	return true;
}

// MOD (Wunsch 17.11. 18:10): Verteidigungslinie wie auf Kriegskarten (Ultimate General): Linie mit kurzen Querstrichen zur Feindseite
void tickSegs(std::vector<Seg> &out, const std::vector<Vector2i> &pts, int side, float w, float alpha, const glm::mat4 &m)
{
	if (side == 0) { return; }
	const std::vector<glm::vec2> wp = smoothWorld(pts);
	if (wp.size() < 2) { return; }
	std::vector<float> acc(wp.size(), 0.f);
	for (size_t i = 1; i < wp.size(); ++i) { acc[i] = acc[i - 1] + glm::length(wp[i] - wp[i - 1]); }
	const float L = acc.back();
	const float step = DIST_TILE * 1.3f, tickLen = DIST_TILE * 0.7f;
	auto at = [&](float s) {
		s = std::clamp(s, 0.f, L);
		size_t i = 1;
		while (i + 1 < wp.size() && acc[i] < s) { ++i; }
		const float seg = std::max(0.001f, acc[i] - acc[i - 1]);
		return wp[i - 1] + (wp[i] - wp[i - 1]) * ((s - acc[i - 1]) / seg);
	};
	for (float s = step * 0.5f; s <= L - step * 0.25f; s += step)
	{
		glm::vec2 t = at(s + DIST_TILE * 0.3f) - at(s - DIST_TILE * 0.3f);
		if (glm::length(t) < 0.01f) { continue; }
		t = glm::normalize(t);
		const glm::vec2 n = glm::vec2(-t.y, t.x) * static_cast<float>(side);
		const glm::vec2 p = at(s);
		glm::vec2 a, b;
		if (!projectF(p, m, a) || !projectF(p + n * tickLen, m, b)) { continue; }
		out.push_back(Seg{ a, b, w * 0.8f, alpha, false });
	}
}

// MOD (Wunsch 17.11. 18:12): Panzersperrenfeld - gruene Pyramiden an den geplanten Stellen
void pyramidSegs(std::vector<Seg> &out, std::vector<Seg> &facets, const std::vector<Vector2i> &marks, float alpha, const glm::mat4 &m)
{
	for (const Vector2i &p : marks)
	{
		glm::vec2 g, t;
		const glm::vec2 w(static_cast<float>(p.x), static_cast<float>(p.y));
		if (!projectF(w, m, g) || !projectF(w + glm::vec2(DIST_TILE * 0.32f, 0.f), m, t)) { continue; }
		const float r = std::clamp(glm::length(t - g), 4.f, 40.f);   // perspektivische Groesse
		const glm::vec2 base = g + glm::vec2(0.f, r * 0.35f), tip = g - glm::vec2(0.f, r * 1.35f);
		triangleSegs(out, base, tip, r * 0.95f, alpha);
		// helle linke Flanke (Pyramide)
		const glm::vec2 fb = base - glm::vec2(r * 0.95f * 0.5f, 0.f);
		triangleSegs(facets, fb, tip - glm::vec2(r * 0.03f, 0.f), r * 0.95f * 0.5f, alpha * 0.55f);
	}
}

// MOD (Wunsch 17.11. 18:13): Minenfeld - Minen-Zeichen (Scheibe mit Zuendkreuz) an den geplanten Stellen
void mineSegs(std::vector<Seg> &out, std::vector<Seg> &cross, const std::vector<Vector2i> &marks, float alpha, const glm::mat4 &m)
{
	for (const Vector2i &p : marks)
	{
		glm::vec2 g, t;
		const glm::vec2 w(static_cast<float>(p.x), static_cast<float>(p.y));
		if (!projectF(w, m, g) || !projectF(w + glm::vec2(DIST_TILE * 0.28f, 0.f), m, t)) { continue; }
		const float r = std::clamp(glm::length(t - g), 4.f, 34.f);
		out.push_back(Seg{ g, g, r * 1.6f, alpha, false });   // Scheibe (Kapsel der Laenge 0 = Kreis)
		cross.push_back(Seg{ g - glm::vec2(r * 0.6f, r * 0.6f), g + glm::vec2(r * 0.6f, r * 0.6f), std::max(1.5f, r * 0.22f), alpha, false });
		cross.push_back(Seg{ g - glm::vec2(r * 0.6f, -r * 0.6f), g + glm::vec2(r * 0.6f, -r * 0.6f), std::max(1.5f, r * 0.22f), alpha, false });
	}
}

void drawStroke(const std::vector<Vector2i> &pts, int mode, int alpha, const glm::mat4 &m, const std::vector<Vector2i> *marks = nullptr, int side = 0)
{
	const ModeInfo &mi = kModes[mode];
	const bool closed = mi.area;
	const bool field = mode == M_TRAPS || mode == M_MINES;
	const bool lineWithTicks = mode == M_DEFEND || mode == M_OFFENSE;   // Verteidigungs-/Offensivlinie: Linie mit Querstrichen zum Feind
	const bool arrow = !closed && mode != M_RALLY && !lineWithTicks;    // Rally = Kreis/Spirale
	std::vector<glm::vec2> p = screenPath(pts, m, closed);
	if (p.size() < 2 || alpha <= 0) { return; }
	const float sc = modui::uiScale();
	const float body = (arrow ? 18.f : 11.f) * sc;   // Strichstaerke (17.11. 18:10: noch dicker)
	std::vector<Seg> segs;
	// Pfeil: Spitze am Ende, Koerper endet an ihrer Basis
	glm::vec2 tip = p.back(), dir(1.f, 0.f);
	float headL = 0.f, headW = 0.f;
	if (arrow)
	{
		headL = body * 2.6f; headW = body * 2.7f;
		size_t k = p.size() - 1;
		float back = 0.f;
		while (k > 0 && back < headL) { back += glm::length(p[k] - p[k - 1]); --k; }
		dir = tip - p[k];
		if (glm::length(dir) < 0.01f) { dir = glm::vec2(1.f, 0.f); } else { dir = glm::normalize(dir); }
		while (p.size() > 2 && glm::length(p.back() - tip) < headL * 0.8f && glm::dot(tip - p.back(), dir) < headL * 0.8f) { p.pop_back(); }
		p.push_back(tip - dir * (headL * 0.8f));
	}
	const size_t n = p.size();
	// Felder (Panzersperren/Minen): Umriss nur schwach, die Zeichen tragen die Aussage
	const float outlineA = (field && marks != nullptr && !marks->empty()) ? static_cast<float>(alpha) * 0.3f : static_cast<float>(alpha);
	for (size_t i = 1; i < n; ++i)
	{
		const float s = static_cast<float>(i) / static_cast<float>(n - 1);
		// Pfeil: schmaler, durchsichtiger Schwanz -> voller Koerper (wie die Kriegskarten-Pfeile); Linien/Kreise gleichmaessig
		const float taper = arrow ? (0.35f + 0.65f * std::min(1.f, s / 0.45f)) : (field ? 0.55f : 1.f);
		const float fade = arrow ? std::min(1.f, 0.15f + s / 0.4f) : 1.f;
		segs.push_back(Seg{ p[i - 1], p[i], body * taper, outlineA * fade, false });
	}
	if (arrow) { triangleSegs(segs, tip - dir * headL, tip, headW * 0.5f, static_cast<float>(alpha)); }
	if (lineWithTicks) { tickSegs(segs, pts, side, body, static_cast<float>(alpha), m); }
	std::vector<Seg> extra;
	if (field && marks != nullptr)
	{
		if (mode == M_TRAPS) { pyramidSegs(segs, extra, *marks, static_cast<float>(alpha), m); }
		else { mineSegs(segs, extra, *marks, static_cast<float>(alpha), m); }
	}
	drawSegs(segs, mi);
	if (!extra.empty())
	{
		// Pyramiden-Flanke heller, Zuendkreuz der Mine fast weiss
		for (const Seg &g : extra) { drawSegLayer(g, 2, mi, mode == M_TRAPS ? 0.7f : 0.92f); }
	}
}
} // namespace

void tick()
{
	// (17.11. 14:14) Mod-Rosette: der Schlachtplan-Knopf blendet die Leiste ein/aus; "Infantry: build" liegt jetzt in der Rosette
	const bool want = !moddesigner::isOpen() && selectedPlayer < MAX_PLAYERS && modrosette::planOpen() && intMode != INT_DESIGN;
	infButtonTick(false);
	if (!want && !s_screen) { opTick(); planTick(); faceTick(); sweepTick(); return; }
	ensureScreen();
	if (!s_root) { return; }
	s_root->show(want);
	if (want) { layout(); }
	if (!want) { s_mode = -1; s_drawing = false; s_cur.clear(); }
	if (s_mode >= 0 && keyPressed(KEY_ESC)) { s_mode = -1; s_drawing = false; s_cur.clear(); }
	opTick();   // Bergetrupp (laeuft auch, wenn die Knoepfe verborgen sind)
	planTick(); // Oberbefehle wieder aufnehmen
	faceTick(); // Verteidigungslinie: am Platz zur Feindseite drehen
	sweepTick(); // Minen raeumen im Gebiet
	// alte Zeichnungen entfernen
	s_done.erase(std::remove_if(s_done.begin(), s_done.end(), [](const Stroke &st) { return realTime - st.born > SHOW_MS; }), s_done.end());
}

bool processMouse(bool overUi, const Vector2i &worldPos)
{
	// (18.11. 12:14) Artilleriebasis: der echte Bauplan folgt der Maus (beim Ziehen: Mitte der Zeichnung) - statt der Befestigungs-Vorschau
	if (s_mode == M_ARTYBASE && (!overUi || s_drawing))
	{
		static std::vector<modbuild::Placement> s_artyPreview;
		s_artyPreview = artyBasePlan(s_drawing && !s_cur.empty() ? centroid(s_cur) : worldPos, s_drawing ? artyBaseSize(s_cur) : 1);
		modbuild::setExternalPreview(&s_artyPreview);
	}
	else if (s_mode == M_DEFEND && s_drawing && s_cur.size() >= 2 && selectedPlayer < MAX_PLAYERS)
	{
		// MOD (Wunsch 20.11.): beim Ziehen der Verteidigungslinie zeigt die Bauplan-Vorschau das Grabennetz, das die Bauer ausheben werden
		// (nur wenn Bauer dabei sind; neu gerechnet, wenn die Linie waechst oder hoechstens alle 0,15 s)
		static std::vector<modbuild::Placement> s_netPreview;
		static size_t s_netPts = 0;
		static unsigned s_netAt = 0;
		std::vector<Vector2i> pts = s_cur;
		if (pts.back() != worldPos) { pts.push_back(worldPos); }
		if (pts.size() != s_netPts || realTime - s_netAt > 150)
		{
			s_netPts = pts.size();
			s_netAt = realTime;
			bool anyBuilder = false;
			for (const DROID *d : pickUnits(M_DEFEND, pts)) { if (isBuilder(d)) { anyBuilder = true; break; } }
			s_netPreview = anyBuilder ? modbuild::planDefenseNet(selectedPlayer, pts, defendSide(pts)) : std::vector<modbuild::Placement>();
		}
		modbuild::setExternalPreview(&s_netPreview);
	}
	else
	{
		modbuild::setExternalPreview(nullptr);
	}
	if (s_mode < 0) { return false; }
	if (mouseReleased(MOUSE_RMB))
	{
		s_mode = -1;
		s_drawing = false;
		s_cur.clear();
		return true;
	}
	if (!s_drawing)
	{
		if (overUi) { return false; }
		if (mousePressed(MOUSE_LMB))
		{
			s_drawing = true;
			s_cur.clear();
			s_cur.push_back(worldPos);
		}
		return true;   // im Zeichenmodus keine Auswahl / Befehle per Maus
	}
	if (mouseDown(MOUSE_LMB))
	{
		if (dist2(worldPos, s_cur.back()) >= static_cast<int64_t>(DIST_TILE / 2) * (DIST_TILE / 2) && s_cur.size() < 400) { s_cur.push_back(worldPos); }
		return true;
	}
	// losgelassen: Zeichnung fertig
	s_drawing = false;
	if (s_cur.back() != worldPos) { s_cur.push_back(worldPos); }
	const Mode m = static_cast<Mode>(s_mode);
	// (Wunsch 18.11. 13:06 "wenn man sich verklickt, ist immer noch der Befehl ausgewaehlt, den man geben will"): ein blosses Antippen
	// (Strich kuerzer als eine halbe Kachel) gilt als Verklicken - nichts wird befohlen, der gewaehlte Befehl bleibt aktiv.
	// Ausnahme: Artilleriebasis und Sammelpunkt duerfen mit einem Klick gesetzt werden.
	if (m != M_ARTYBASE && m != M_RALLY)
	{
		int64_t len = 0;
		for (size_t i = 1; i < s_cur.size(); ++i) { len += static_cast<int64_t>(std::sqrt(static_cast<double>(dist2(s_cur[i], s_cur[i - 1])))); }
		if (len < DIST_TILE / 2)
		{
			s_cur.clear();
			return true;
		}
	}
	const bool shift = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	if (!shift) { s_chain.clear(); }
	s_append = shift;
	applyStroke(m, s_cur);
	s_append = false;
	Stroke st;
	st.mode = m;
	st.pts = s_cur;
	st.born = realTime;
	if (m == M_TRAPS || m == M_MINES) { st.marks = fieldSpots(s_cur, m == M_MINES); }
	if (m == M_DEFEND || m == M_OFFENSE) { st.side = defendSide(s_cur); }
	// MOD (Wunsch 16.11. 21:25): hoechstens ein Marker je Aktion - eine neue Zeichnung ersetzt die alte derselben Art
	// (18.11.) mit Shift bleiben die vorigen Marker stehen (die Befehle wurden ja angehaengt)
	if (!shift) { s_done.erase(std::remove_if(s_done.begin(), s_done.end(), [m](const Stroke &o) { return o.mode == m; }), s_done.end()); }
	s_done.push_back(std::move(st));
	if (s_done.size() > 24) { s_done.erase(s_done.begin()); }
	s_cur.clear();
	if (!shift) { s_mode = -1; }   // ein Zug je Knopfdruck - mit Shift bleibt der Modus fuer den naechsten Strich an
	return true;
}

void drawOverlay(const glm::mat4 &m)
{
	for (const Stroke &st : s_done)
	{
		const unsigned age = realTime - st.born;
		const int a = age >= SHOW_MS ? 0 : static_cast<int>(230 - 200 * static_cast<int64_t>(age) / static_cast<int64_t>(SHOW_MS));
		if (a > 0) { drawStroke(st.pts, st.mode, a, m, &st.marks, st.side); }
	}
	if (s_op.active)
	{
		// Bergegebiet und Bereitstellungspunkt (pulsierend), solange der Trupp unterwegs ist
		const int a = 150 + static_cast<int>(60.0 * std::sin(static_cast<double>(realTime) / 300.0));
		drawStroke(circlePts(s_op.c, s_op.r, 36), M_RECOVER, a, m);
		if (s_op.phase == 0) { drawStroke(circlePts(s_op.deploy, DIST_TILE * 3 / 2, 16), M_RECOVER, a, m); }
	}
	if (s_drawing && s_mode >= 0)
	{
		// Vorschau: Querstriche / geplante Panzersperren und Minen schon waehrend des Zeichnens
		std::vector<Vector2i> marks;
		if ((s_mode == M_TRAPS || s_mode == M_MINES) && s_cur.size() >= 3) { marks = fieldSpots(s_cur, s_mode == M_MINES); }
		drawStroke(s_cur, s_mode, 255, m, &marks, (s_mode == M_DEFEND || s_mode == M_OFFENSE) ? defendSide(s_cur) : 0);
	}
}

int stackTop()
{
	// (18.11. 12:10 "die Menues ueberlappen sich immer noch extrem") Oberkante des Stapels unten rechts, wenn keine Minikarte sichtbar ist:
	// offener Schlachtplan (unten rechts in der Ecke) - sonst der Bildschirmrand
	int x, y, w, h;
	calcRect(x, y, w, h);
	return modrosette::planOpen() ? y : static_cast<int>(screenHeight) - 4;
}

void selectMode(const char *label)
{
	for (int m = 0; m < M_COUNT; ++m)
	{
		if (label != nullptr && strcmp(kModes[m].label, label) == 0) { s_mode = m; s_drawing = false; s_cur.clear(); return; }
	}
}

void drawOnMap(const std::function<bool(const Vector2i &, Vector2i &)> &toScreen)
{
	s_proj = toScreen;
	drawOverlay(glm::mat4(1.f));
	s_proj = nullptr;
}

const char *activeLabel()
{
	return s_mode >= 0 ? kModes[s_mode].cursor : nullptr;
}

void shutdown()
{
	if (s_screen)
	{
		widgRemoveOverlayScreen(s_screen);
	}
	if (s_infScreen)
	{
		widgRemoveOverlayScreen(s_infScreen);
	}
	s_infScreen = nullptr;
	s_infRoot = nullptr;
	s_infBtn = nullptr;
	s_screen = nullptr;
	s_root = nullptr;
	s_buttons.clear();
	s_mode = -1;
	s_drawing = false;
	s_cur.clear();
	s_done.clear();
	s_chain.clear();
	s_append = false;
	s_op = SalvageOp();
	s_plan.clear();
	s_sweep.clear();
	s_nextPlanCheck = 0;
}
}
