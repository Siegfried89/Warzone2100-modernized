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
#include "order.h"
#include "ordersource.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cmath>
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
	M_MOVE = 0, M_RALLY, M_ATTACK, M_DEFEND, M_ARTYBASE, M_ARTY, M_AIR, M_COMBOT, M_SPREAD, M_RECOVER, M_COUNT   // M_SPREAD = Sofort-Befehl (kein Zeichnen)
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
	{ "Arty base", "Build a compact artillery base here: sensor tower, 4 artillery emplacements around it, wall ring", "Artillery base: drag around the place (right click cancels)", 170, 120, 60, true },
	{ "Artillery", "Artillery: bombard this area", "Artillery: drag the target area (right click cancels)", 240, 200, 90, true },
	{ "Aircraft", "Aircraft: patrol over this area", "Aircraft: drag the patrol area (right click cancels)", 255, 235, 50, true },
	{ "Combots", "Combot area: combots patrol and fight in this area on their own", "Combots: drag the combot area (right click cancels)", 190, 90, 255, true },
	{ "Spread out", "Spread out: the selected units spread out evenly (against artillery and bombs)", "", 150, 200, 200, false },
	{ "Salvage", "Salvage: drag a circle around parts, artefacts and wrecks - an armed, escorted salvage team gathers at the base first, then recovers everything in it (wrecks are towed to the repair facility by flatbed trucks)", "Salvage: drag a circle around the salvage (right click cancels)", 120, 220, 255, true },
};

constexpr int BTN_W = 66;
constexpr int BTN_H = 20;
constexpr int GAP = 3;
constexpr int PAD = 4;
constexpr int COLS = 2;
constexpr int ROWS = 5;
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
};

std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_root;
std::vector<std::shared_ptr<W_BUTTON>> s_buttons;
int s_mode = -1;                 // aktiver Zeichenmodus (-1 = keiner)
bool s_drawing = false;
std::vector<Vector2i> s_cur;     // Linie, die gerade gezeichnet wird
std::vector<Stroke> s_done;      // fertige Zeichnungen (verblassen)
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

void layout()
{
	const float s = modui::uiScale();
	const int w = static_cast<int>(TOTAL_W * s), h = static_cast<int>(TOTAL_H * s);
	// (17.11.) nicht ueber der Karte unten rechts (Radar): links daneben, unten buendig
	int x = static_cast<int>(screenWidth) - w - 4;
	int y = static_cast<int>(screenHeight) - h - 64;
	const std::shared_ptr<WIDGET> radar = getRadarWidget();
	if (radar && radar->visible() && radar->width() > 0)
	{
		x = std::max(4, radar->x() - w - 6);
		y = std::max(4, static_cast<int>(screenHeight) - h - 8);
	}
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
bool isTruck(const DROID *d) { return d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT; }
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
		const int64_t r2 = static_cast<int64_t>((NEAR_TILES * DIST_TILE)) * (NEAR_TILES * DIST_TILE);
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
	if (nearby.size() > MAX_UNITS) { nearby.resize(MAX_UNITS); }
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

void rememberPlan(const DROID *d, const DroidOrder &o, const Vector2i &anchor)
{
	PlanTask t;
	t.order = o;
	t.anchor = anchor;
	t.born = gameTime;
	s_plan[d->id] = t;
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
		}
		++it;
	}
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
	const Vector2i c = centroid(raw);
	if (units.empty())
	{
		return;
	}
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
		for (size_t i = 0; i < fighters.size(); ++i) { send(fighters[i], DroidOrder(DORDER_MOVE, spots[i]), false); rememberPlan(fighters[i], DroidOrder(DORDER_MOVE, spots[i]), spots[i]); }
		// Bauer heben Graeben entlang der Linie aus (Abschnitte reihum verteilt)
		STRUCTURE_STATS *trench = firstAvailable({ "A0BaBaTrench", "A0BaBaTrenchEarth", "A0BaBaSandbagWall" });
		if (trench != nullptr && !builders.empty())
		{
			const std::vector<Vector2i> seg = resample(raw, std::min<size_t>(9, std::max<size_t>(2, raw.size())));
			for (size_t k = 0; k + 1 < seg.size(); ++k)
			{
				DROID *b = builders[k % builders.size()];
				const bool add = k >= builders.size();
				send(b, DroidOrder(DORDER_LINEBUILD, trench, seg[k], seg[k + 1], 0), add);
			}
		}
		break;
	}
	case M_ARTYBASE:
	{
		STRUCTURE_STATS *arty = bestArtyEmplacement();
		STRUCTURE_STATS *sensor = firstAvailable({ "Sys-SensoTowerWS", "Sys-SensoTower02", "Sys-SensoTower01", "A0BaBaOutpost" });
		STRUCTURE_STATS *wall = firstAvailable({ "A0BaBaHorizontalWall", "A0HardcreteMk1Wall", "CollectiveWall", "NEXUSWall", "A0BaBaSandbagWall" });
		const Vector2i ct(map_coord(c.x), map_coord(c.y));
		auto worldOf = [](int tx, int ty) { return Vector2i(world_coord(tx) + TILE_UNITS / 2, world_coord(ty) + TILE_UNITS / 2); };
		std::vector<DroidOrder> plan;
		if (sensor != nullptr) { plan.emplace_back(DORDER_BUILD, sensor, worldOf(ct.x, ct.y), 0); }
		if (arty != nullptr)
		{
			// (16.11. 22:50 "immer noch zu gross, nicht so viel Platz verschwenden") dicht um den Sensor: 3 x 3 Felder
			static const int OFF[4][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
			for (const auto &o : OFF) { plan.emplace_back(DORDER_BUILD, arty, worldOf(ct.x + o[0], ct.y + o[1]), 0); }
		}
		if (wall != nullptr)
		{
			const int r = 2;   // Mauerring direkt um den 3x3-Block (vorher 4 = 9x9 Felder)
			plan.emplace_back(DORDER_LINEBUILD, wall, worldOf(ct.x - r, ct.y - r), worldOf(ct.x + r, ct.y - r), 0);
			plan.emplace_back(DORDER_LINEBUILD, wall, worldOf(ct.x + r, ct.y - r + 1), worldOf(ct.x + r, ct.y + r), 0);
			plan.emplace_back(DORDER_LINEBUILD, wall, worldOf(ct.x + r - 1, ct.y + r), worldOf(ct.x - r, ct.y + r), 0);
			plan.emplace_back(DORDER_LINEBUILD, wall, worldOf(ct.x - r, ct.y + r - 1), worldOf(ct.x - r, ct.y - r + 1), 0);
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

bool project(const Vector2i &w, const glm::mat4 &m, Vector2i &out)
{
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

void softSeg(const glm::vec2 &a, const glm::vec2 &b, float w, PIELIGHT col)
{
	const glm::vec2 d = b - a;
	const float len = glm::length(d);
	if (len < 0.01f || w <= 0.5f) { return; }
	const glm::vec2 c = (a + b) * 0.5f;
	pie_DrawSoftRoundedBoxRotated(c.x, c.y, len + w, w, std::atan2(d.y, d.x), col, w * 0.5f, std::max(1.2f, w * 0.18f));
}

void drawStroke(const std::vector<Vector2i> &pts, int mode, int alpha, const glm::mat4 &m)
{
	const ModeInfo &mi = kModes[mode];
	const bool closed = mi.area;
	const bool arrow = !closed && mode != M_RALLY && mode != M_DEFEND;   // Rally = Kreis/Spirale, Defend = Linie: ohne Spitze
	std::vector<glm::vec2> p = screenPath(pts, m, closed);
	if (p.size() < 2 || alpha <= 0) { return; }
	const float sc = modui::uiScale();
	const float body = (arrow ? 13.f : 7.f) * sc;   // Strichstaerke
	const float halo = body + 14.f * sc;            // leuchtender Hof
	// Pfeil: Spitze am Ende, Koerper endet an ihrer Basis
	glm::vec2 tip = p.back(), dir(1.f, 0.f);
	float headL = 0.f, headW = 0.f;
	if (arrow)
	{
		headL = body * 2.6f; headW = body * 2.7f;
		// Richtung aus dem letzten Stueck (etwa eine Spitzenlaenge zurueck)
		size_t k = p.size() - 1;
		float back = 0.f;
		while (k > 0 && back < headL) { back += glm::length(p[k] - p[k - 1]); --k; }
		dir = tip - p[k];
		if (glm::length(dir) < 0.01f) { dir = glm::vec2(1.f, 0.f); } else { dir = glm::normalize(dir); }
		// Punkte innerhalb der Spitze weglassen
		while (p.size() > 2 && glm::length(p.back() - tip) < headL * 0.8f && glm::dot(tip - p.back(), dir) < headL * 0.8f) { p.pop_back(); }
		p.push_back(tip - dir * (headL * 0.8f));
	}
	const size_t n = p.size();
	auto colA = [&](float a, float bright) {
		const int r = std::min(255, static_cast<int>(mi.r + (255 - mi.r) * bright));
		const int g = std::min(255, static_cast<int>(mi.g + (255 - mi.g) * bright));
		const int b = std::min(255, static_cast<int>(mi.b + (255 - mi.b) * bright));
		return pal_RGBA(static_cast<UBYTE>(r), static_cast<UBYTE>(g), static_cast<UBYTE>(b), static_cast<UBYTE>(std::clamp(static_cast<int>(a), 0, 255)));
	};
	// 1. Hof (breit, schwach, heller), 2. Kern
	for (int pass = 0; pass < 2; ++pass)
	{
		for (size_t i = 1; i < n; ++i)
		{
			const float s = static_cast<float>(i) / static_cast<float>(n - 1);
			// Pfeil: schmaler, durchsichtiger Schwanz -> voller Koerper (wie die Kriegskarten-Pfeile); Linien/Kreise gleichmaessig
			const float taper = arrow ? (0.35f + 0.65f * std::min(1.f, s / 0.45f)) : 1.f;
			const float fade = arrow ? std::min(1.f, 0.15f + s / 0.4f) : 1.f;
			if (pass == 0) { softSeg(p[i - 1], p[i], halo * (arrow ? (0.6f + 0.4f * taper) : 1.f), colA(alpha * 0.28f * fade, 0.45f)); }
			else { softSeg(p[i - 1], p[i], body * taper, colA(alpha * fade, 0.f)); }
		}
		if (arrow)
		{
			// Spitze aus Scheiben quer zur Richtung (Dreieck mit weichen Kanten)
			const glm::vec2 base = tip - dir * headL;
			const float ang = std::atan2(dir.y, dir.x);
			const int K = 12;
			for (int k = 0; k < K; ++k)
			{
				const float f = (static_cast<float>(k) + 0.5f) / static_cast<float>(K);
				const glm::vec2 c = base + dir * (headL * f);
				const float wAcross = headW * (1.f - f) + (pass == 0 ? 12.f * sc : 0.f);
				const float wAlong = headL / static_cast<float>(K) + 2.f;
				pie_DrawSoftRoundedBoxRotated(c.x, c.y, wAlong, std::max(1.f, wAcross), ang, pass == 0 ? colA(alpha * 0.28f, 0.45f) : colA(alpha, 0.f), 1.f, 1.2f);
			}
		}
	}
}
} // namespace

void tick()
{
	const bool want = !moddesigner::isOpen() && selectedPlayer < MAX_PLAYERS;
	infButtonTick(want);
	if (!want && !s_screen) { opTick(); planTick(); return; }
	ensureScreen();
	if (!s_root) { return; }
	s_root->show(want);
	if (want) { layout(); }
	if (!want) { s_mode = -1; s_drawing = false; s_cur.clear(); }
	if (s_mode >= 0 && keyPressed(KEY_ESC)) { s_mode = -1; s_drawing = false; s_cur.clear(); }
	opTick();   // Bergetrupp (laeuft auch, wenn die Knoepfe verborgen sind)
	planTick(); // Oberbefehle wieder aufnehmen
	// alte Zeichnungen entfernen
	s_done.erase(std::remove_if(s_done.begin(), s_done.end(), [](const Stroke &st) { return realTime - st.born > SHOW_MS; }), s_done.end());
}

bool processMouse(bool overUi, const Vector2i &worldPos)
{
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
	applyStroke(m, s_cur);
	Stroke st;
	st.mode = m;
	st.pts = s_cur;
	st.born = realTime;
	// MOD (Wunsch 16.11. 21:25): hoechstens ein Marker je Aktion - eine neue Zeichnung ersetzt die alte derselben Art
	s_done.erase(std::remove_if(s_done.begin(), s_done.end(), [m](const Stroke &o) { return o.mode == m; }), s_done.end());
	s_done.push_back(std::move(st));
	if (s_done.size() > 24) { s_done.erase(s_done.begin()); }
	s_cur.clear();
	s_mode = -1;   // ein Zug je Knopfdruck
	return true;
}

void drawOverlay(const glm::mat4 &m)
{
	for (const Stroke &st : s_done)
	{
		const unsigned age = realTime - st.born;
		const int a = age >= SHOW_MS ? 0 : static_cast<int>(230 - 200 * static_cast<int64_t>(age) / static_cast<int64_t>(SHOW_MS));
		if (a > 0) { drawStroke(st.pts, st.mode, a, m); }
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
		drawStroke(s_cur, s_mode, 255, m);
	}
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
	s_op = SalvageOp();
	s_plan.clear();
	s_nextPlanCheck = 0;
}
}
