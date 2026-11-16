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
#include "order.h"
#include "ordersource.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cmath>
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
	{ "Arty base", "Build an artillery base here: 4 artillery emplacements, a sensor tower and a wall around it", "Artillery base: drag around the place (right click cancels)", 170, 120, 60, true },
	{ "Artillery", "Artillery: bombard this area", "Artillery: drag the target area (right click cancels)", 240, 200, 90, true },
	{ "Aircraft", "Aircraft: patrol over this area", "Aircraft: drag the patrol area (right click cancels)", 255, 235, 50, true },
	{ "Combots", "Combot area: combots patrol and fight in this area on their own", "Combots: drag the combot area (right click cancels)", 190, 90, 255, true },
	{ "Spread out", "Spread out: the selected units spread out evenly (against artillery and bombs)", "", 150, 200, 200, false },
	{ "Recover", "Recover: the selected trucks / repair units collect lost turrets and bring them to the repair facility", "", 120, 220, 255, false },
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
			if (m == M_RECOVER)
			{
				// MOD (16.11. Punkt 10): Bergen - gewaehlte Bau-/Reparatureinheiten holen liegende Turm-Teile
				std::vector<DROID *> sel;
				if (selectedPlayer < MAX_PLAYERS)
				{
					for (DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d->selected) { sel.push_back(d); } }
				}
				modrecover::recoverWithSelection(sel);
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

void layout()
{
	const float s = modui::uiScale();
	const int w = static_cast<int>(TOTAL_W * s), h = static_cast<int>(TOTAL_H * s);
	s_root->setGeometry(static_cast<int>(screenWidth) - w - 4, static_cast<int>(screenHeight) - h - 64, w, h);
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
	const int r = ring * (TILE_UNITS * 3 / 4);
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
		const int64_t r2 = static_cast<int64_t>(world_coord(NEAR_TILES)) * world_coord(NEAR_TILES);
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

// ------------------------------------------------------------------------------------------------- Auftraege

void applyStroke(Mode m, const std::vector<Vector2i> &raw)
{
	if (raw.empty()) { return; }
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
			}
		}
		break;
	}
	case M_RALLY:
		for (size_t i = 0; i < units.size(); ++i) { send(units[i], DroidOrder(DORDER_MOVE, spread(c, i)), false); }
		break;
	case M_DEFEND:
	{
		std::vector<DROID *> fighters, builders;
		for (DROID *d : units) { (isBuilder(d) ? builders : fighters).push_back(d); }
		// Kaempfer verteilen sich gleichmaessig auf der Linie
		const std::vector<Vector2i> spots = resample(raw, std::max<size_t>(1, fighters.size()));
		for (size_t i = 0; i < fighters.size(); ++i) { send(fighters[i], DroidOrder(DORDER_MOVE, spots[i]), false); }
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
			static const int OFF[4][2] = { { -2, -2 }, { 2, -2 }, { -2, 2 }, { 2, 2 } };
			for (const auto &o : OFF) { plan.emplace_back(DORDER_BUILD, arty, worldOf(ct.x + o[0], ct.y + o[1]), 0); }
		}
		if (wall != nullptr)
		{
			const int r = 4;
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

void drawPoly(const std::vector<Vector2i> &pts, bool closed, PIELIGHT col, const glm::mat4 &m)
{
	if (pts.size() < 2) { return; }
	std::vector<glm::ivec4> lines;
	Vector2i prev(0, 0), first(0, 0);
	bool havePrev = false, haveFirst = false;
	for (const Vector2i &p : pts)
	{
		Vector2i s(0, 0);
		const bool ok = project(p, m, s);
		if (ok && havePrev)
		{
			lines.emplace_back(prev.x, prev.y, s.x, s.y);
			lines.emplace_back(prev.x + 1, prev.y, s.x + 1, s.y);
			lines.emplace_back(prev.x, prev.y + 1, s.x, s.y + 1);
		}
		if (ok && !haveFirst) { first = s; haveFirst = true; }
		prev = s;
		havePrev = ok;
	}
	if (closed && havePrev && haveFirst) { lines.emplace_back(prev.x, prev.y, first.x, first.y); }
	// Pfeilspitze am Ende von Linien
	if (!closed && havePrev && pts.size() >= 2)
	{
		Vector2i a(0, 0);
		if (project(pts[pts.size() >= 4 ? pts.size() - 4 : 0], m, a))
		{
			const double dx = prev.x - a.x, dy = prev.y - a.y;
			const double l = std::max(1.0, std::sqrt(dx * dx + dy * dy));
			const double ux = dx / l, uy = dy / l;
			const int hx1 = static_cast<int>(prev.x - ux * 14 - uy * 8), hy1 = static_cast<int>(prev.y - uy * 14 + ux * 8);
			const int hx2 = static_cast<int>(prev.x - ux * 14 + uy * 8), hy2 = static_cast<int>(prev.y - uy * 14 - ux * 8);
			lines.emplace_back(prev.x, prev.y, hx1, hy1);
			lines.emplace_back(prev.x, prev.y, hx2, hy2);
		}
	}
	if (!lines.empty()) { iV_Lines(lines, col); }
}
} // namespace

void tick()
{
	const bool want = !moddesigner::isOpen() && selectedPlayer < MAX_PLAYERS;
	if (!want && !s_screen) { return; }
	ensureScreen();
	if (!s_root) { return; }
	s_root->show(want);
	if (want) { layout(); }
	if (!want) { s_mode = -1; s_drawing = false; s_cur.clear(); }
	if (s_mode >= 0 && keyPressed(KEY_ESC)) { s_mode = -1; s_drawing = false; s_cur.clear(); }
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
		if (dist2(worldPos, s_cur.back()) >= static_cast<int64_t>(TILE_UNITS / 2) * (TILE_UNITS / 2) && s_cur.size() < 400) { s_cur.push_back(worldPos); }
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
		const ModeInfo &mi = kModes[st.mode];
		const unsigned age = realTime - st.born;
		const int a = age >= SHOW_MS ? 0 : static_cast<int>(230 - 200 * static_cast<int64_t>(age) / static_cast<int64_t>(SHOW_MS));
		if (a > 0) { drawPoly(st.pts, mi.area, pal_RGBA(mi.r, mi.g, mi.b, static_cast<unsigned char>(a)), m); }
	}
	if (s_drawing && s_mode >= 0)
	{
		const ModeInfo &mi = kModes[s_mode];
		drawPoly(s_cur, mi.area, pal_RGBA(mi.r, mi.g, mi.b, 255), m);
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
	s_screen = nullptr;
	s_root = nullptr;
	s_buttons.clear();
	s_mode = -1;
	s_drawing = false;
	s_cur.clear();
	s_done.clear();
}
}
