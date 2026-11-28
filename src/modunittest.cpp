// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Testgelaende des Einheiten-Designers (siehe modunittest.h)
#include "modunittest.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <physfs.h>

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/netplay.h"
#include "lib/widget/button.h"
#include "lib/widget/form.h"
#include "lib/widget/widget.h"

#include "challenge.h"
#include "console.h"
#include "design.h"
#include "display3d.h"
#include "droid.h"
#include "frontend.h"
#include "game_world.h"
#include "hci.h"
#include "levels.h"
#include "loadsave.h"
#include "map.h"
#include "modui.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "structure.h"
#include "template.h"
#include "titleui/multiplayer.h"
#include "titleui/titleui.h"
#include "visibility.h"
#include "wzjsonhelpers.h"

namespace modunittest
{
namespace
{
const char *const CFG = "modtest/unittest.json";
const char *const LEVEL = "Sk-Testgelaende-T1";
constexpr uint32_t GROUP_RESPAWN_MS = 25000;    // ausgeloeschte Gegnergruppe kommt wieder
constexpr uint32_t TARGET_RESPAWN_MS = 30000;   // zerstoertes Ziel steht wieder

bool s_armed = false;        // Test vom Designer gestartet
bool s_autostart = false;    // Spieleinstellungen sofort starten
bool s_setup = false;        // Aufstellung erfolgt
bool s_titleReturn = false;  // Hauptmenue soll in den Designer wechseln
int s_fleet = 0;
nlohmann::json s_design;     // der Entwurf (Vorlage)
nlohmann::json s_cfg;        // Aufstellung ("modtest" in modtest/unittest.json)
nlohmann::json s_templates;  // stats/templates.json (Gegner und Attrappen)
unsigned s_enemy = 1;
int s_ux = 12, s_uy = 36;
uint16_t s_udir = 0;

/// ein Ziel (Gebaeude, oder unbewaffnetes Fahrzeug) - wird nach dem Zerstoeren wieder aufgestellt
struct Slot
{
	std::string id;
	int x = 0, y = 0;
	bool droid = false;
	uint32_t obj = 0;
	uint32_t goneAt = 0;
};
struct Group
{
	std::string name;
	int x = 0, y = 0;
	std::vector<std::string> units;
	std::vector<Slot> structs;
	std::vector<uint32_t> ids;
	uint32_t goneAt = 0;
};
std::vector<Slot> s_targets;
std::vector<Group> s_groups;

// ---------------------------------------------------------------- Aufstellen
const Vector2i OFF[] = {{0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}, {2, 0}, {0, 2}, {-2, 0}, {0, -2},
                        {2, 1}, {1, 2}, {-2, 1}, {-1, 2}, {2, -1}, {1, -2}, {-2, -1}, {-1, -2}, {2, 2}, {-2, 2}, {2, -2}, {-2, -2}};
constexpr int NOFF = static_cast<int>(sizeof(OFF) / sizeof(OFF[0]));

bool tileFree(int x, int y)
{
	if (x < 2 || y < 2 || x >= gameWorld.map.width - 2 || y >= gameWorld.map.height - 2) { return false; }
	return !TileIsOccupied(mapTile(gameWorld.map, x, y));
}

/// freies Feld nahe (cx, cy), ab dem k-ten Versatz; k zeigt danach auf den naechsten
Vector2i freeTile(int cx, int cy, int &k)
{
	for (int i = k; i < NOFF; ++i)
	{
		if (tileFree(cx + OFF[i].x, cy + OFF[i].y)) { k = i + 1; return {cx + OFF[i].x, cy + OFF[i].y}; }
	}
	return {cx, cy};
}

uint16_t faceTo(int fx, int fy, int tx, int ty)
{
	return static_cast<uint16_t>(iAtan2(tx - fx, ty - fy));
}

DROID *spawnDroid(const nlohmann::json &t, unsigned player, int tx, int ty, uint16_t dir)
{
	DROID_TEMPLATE tmpl;
	if (!t.is_object() || !loadTemplateCommon(t, tmpl))
	{
		debug(LOG_WARNING, "Test range: template has a component this data set does not have");
		return nullptr;
	}
	if (!intValidTemplate(&tmpl, tmpl.name.toUtf8().c_str(), false, static_cast<int>(player)))
	{
		debug(LOG_WARNING, "Test range: template \"%s\" is not valid", tmpl.name.toUtf8().c_str());
		return nullptr;
	}
	const bool oldMulti = bMultiMessages;
	bMultiMessages = false;
	DROID *d = buildDroid(gameWorld, &tmpl, static_cast<UDWORD>(world_coord(tx) + TILE_UNITS / 2), static_cast<UDWORD>(world_coord(ty) + TILE_UNITS / 2),
	                      player, false, nullptr, Rotation(dir, 0, 0));
	if (d != nullptr) { addDroid(d, gameWorld.objects.droids); }
	bMultiMessages = oldMulti;
	return d;
}

const nlohmann::json *templ(const std::string &id)
{
	auto it = s_templates.find(id);
	return (it != s_templates.end() && it->is_object()) ? &(*it) : nullptr;
}

STRUCTURE *spawnStruct(const std::string &id, unsigned player, int tx, int ty)
{
	const int idx = getStructStatFromName(WzString::fromUtf8(id));
	if (idx < 0) { debug(LOG_WARNING, "Test range: structure %s not found", id.c_str()); return nullptr; }
	STRUCTURE_STATS *st = &asStructureStats[static_cast<size_t>(idx)];
	const uint16_t dir = static_cast<uint16_t>((faceTo(tx, ty, s_ux, s_uy) + DEG(45)) / DEG(90) * DEG(90));   // Gebaeude stehen rechtwinklig
	const Vector2i size = st->size(dir);
	const int x = world_coord(tx) + size.x * TILE_UNITS / 2, y = world_coord(ty) + size.y * TILE_UNITS / 2;
	const bool oldMulti = bMultiMessages;
	bMultiMessages = false;
	STRUCTURE *s = buildStructureDir(gameWorld, st, static_cast<UDWORD>(x), static_cast<UDWORD>(y), dir, player, false, generateSynchronisedObjectId(), false, true, true);
	if (s != nullptr)
	{
		s->status = SS_BUILT;
		buildingComplete(s, gameWorld);
	}
	bMultiMessages = oldMulti;
	return s;
}

bool alive(const Slot &s)
{
	if (s.obj == 0) { return false; }
	if (s.droid)
	{
		const DROID *d = IdToDroid(gameWorld.objects, s.obj, s_enemy);
		return d != nullptr && !d->died;
	}
	const STRUCTURE *st = IdToStruct(s.obj, s_enemy);
	return st != nullptr && !st->died;
}

void spawnSlot(Slot &s)
{
	s.goneAt = 0;
	s.obj = 0;
	if (s.droid)
	{
		const nlohmann::json *t = templ(s.id);
		DROID *d = t != nullptr ? spawnDroid(*t, s_enemy, s.x, s.y, faceTo(s.x, s.y, s_ux, s_uy)) : nullptr;
		if (d != nullptr)
		{
			s.obj = d->id;
			secondarySetStateWithoutMessage(d, gameWorld.objects, DSO_HALTTYPE, DSS_HALT_HOLD);   // Attrappe bleibt stehen
		}
		return;
	}
	if (STRUCTURE *st = spawnStruct(s.id, s_enemy, s.x, s.y)) { s.obj = st->id; }
}

void prune(std::vector<uint32_t> &ids, unsigned player)
{
	std::vector<uint32_t> keep;
	for (uint32_t id : ids)
	{
		const DROID *d = IdToDroid(gameWorld.objects, id, player);
		if (d != nullptr && !d->died) { keep.push_back(id); }
	}
	ids.swap(keep);
}

void spawnGroup(Group &g)
{
	g.ids.clear();
	g.goneAt = 0;
	int k = 0;
	for (const std::string &tid : g.units)
	{
		const nlohmann::json *t = templ(tid);
		if (t == nullptr) { debug(LOG_WARNING, "Test range: template %s not found", tid.c_str()); continue; }
		const Vector2i p = freeTile(g.x, g.y, k);
		if (DROID *d = spawnDroid(*t, s_enemy, p.x, p.y, faceTo(p.x, p.y, s_ux, s_uy))) { g.ids.push_back(d->id); }
	}
}

int livingDroids(unsigned player)
{
	int n = 0;
	for (const DROID *d : gameWorld.objects.droids[player]) { if (d != nullptr && !d->died) { n++; } }
	return n;
}

/// eine Testeinheit am Startplatz (neben schon stehenden)
DROID *spawnUnit()
{
	int k = 0;
	DROID *d = nullptr;
	for (int tries = 0; tries < 4 && d == nullptr; ++tries)
	{
		const Vector2i p = freeTile(s_ux, s_uy, k);
		bool taken = false;   // auch nicht auf eine andere Testeinheit
		for (const DROID *o : gameWorld.objects.droids[selectedPlayer]) { if (o != nullptr && !o->died && map_coord(o->pos.x) == p.x && map_coord(o->pos.y) == p.y) { taken = true; } }
		if (taken && k < NOFF) { continue; }
		d = spawnDroid(s_design, selectedPlayer, p.x, p.y, s_udir);
		break;
	}
	return d;
}

nlohmann::json arr(const nlohmann::json &o, const char *k)
{
	auto it = o.find(k);
	return (it != o.end() && it->is_array()) ? *it : nlohmann::json::array();
}

void setup()
{
	s_setup = true;
	s_enemy = selectedPlayer == 1 ? 0 : 1;
	s_targets.clear();
	s_groups.clear();
	if (optional<nlohmann::json> c = wzLoadJsonObjectFromFile(CFG)) { s_cfg = c->value("modtest", nlohmann::json::object()); }
	if (optional<nlohmann::json> t = wzLoadJsonObjectFromFile("stats/templates.json")) { s_templates = *t; }
	// Startplatz-Lastwagen der Karte wieder weg
	for (unsigned p : {static_cast<unsigned>(selectedPlayer), s_enemy})
	{
		std::vector<DROID *> v;
		for (DROID *d : gameWorld.objects.droids[p]) { if (d != nullptr && !d->died) { v.push_back(d); } }
		for (DROID *d : v) { vanishDroid(d, gameWorld.objects); }
	}
	revealAll(gameWorld.map, static_cast<UBYTE>(selectedPlayer));
	const nlohmann::json u = s_cfg.value("unit", nlohmann::json::object());
	s_ux = u.value("x", 12);
	s_uy = u.value("y", 36);
	s_udir = static_cast<uint16_t>(DEG(u.value("dir", 90)));
	for (const auto &t : arr(s_cfg, "targets"))
	{
		Slot s; s.id = t.value("s", std::string()); s.x = t.value("x", 0); s.y = t.value("y", 0);
		s_targets.push_back(s);
	}
	for (const auto &t : arr(s_cfg, "dummies"))
	{
		Slot s; s.id = t.value("t", std::string()); s.x = t.value("x", 0); s.y = t.value("y", 0); s.droid = true;
		s_targets.push_back(s);
	}
	for (const auto &gj : arr(s_cfg, "groups"))
	{
		Group g;
		g.name = gj.value("name", std::string());
		g.x = gj.value("x", 0); g.y = gj.value("y", 0);
		for (const auto &un : arr(gj, "units")) { if (un.is_string()) { g.units.push_back(un.get<std::string>()); } }
		for (const auto &t : arr(gj, "structs"))
		{
			Slot s; s.id = t.value("s", std::string()); s.x = t.value("x", 0); s.y = t.value("y", 0);
			g.structs.push_back(s);
		}
		s_groups.push_back(g);
	}
	for (Slot &s : s_targets) { spawnSlot(s); }
	for (Group &g : s_groups) { for (Slot &s : g.structs) { spawnSlot(s); } spawnGroup(g); }
	DROID *d = spawnUnit();
	if (d != nullptr)
	{
		SelectDroid(d);
		setViewPos(static_cast<UDWORD>(s_ux + 3), static_cast<UDWORD>(s_uy), false);
		addConsoleMessage(_("Test range: your unit is at the start - targets ahead, enemy camps in the east"), CENTRE_JUSTIFY, SYSTEM_MESSAGE);
	}
	else
	{
		addConsoleMessage(_("Test range: this design could not be built (a part is missing in this game)"), CENTRE_JUSTIFY, SYSTEM_MESSAGE);
	}
}

/// verlorenes ersetzen (die Partie endet nie von selbst)
void upkeep()
{
	if (livingDroids(selectedPlayer) == 0 && spawnUnit() != nullptr)
	{
		addConsoleMessage(_("Test unit destroyed - a new one is waiting at the start"), CENTRE_JUSTIFY, SYSTEM_MESSAGE);
	}
	for (Group &g : s_groups)
	{
		prune(g.ids, s_enemy);
		if (!g.ids.empty() || g.units.empty()) { g.goneAt = 0; continue; }
		if (g.goneAt == 0) { g.goneAt = std::max<uint32_t>(gameTime, 1); }
		else if (gameTime - g.goneAt >= GROUP_RESPAWN_MS) { spawnGroup(g); }
	}
	auto slot = [](Slot &s) {
		if (alive(s)) { s.goneAt = 0; return; }
		if (s.goneAt == 0) { s.goneAt = std::max<uint32_t>(gameTime, 1); return; }
		if (gameTime - s.goneAt < TARGET_RESPAWN_MS) { return; }
		spawnSlot(s);
		if (s.obj == 0) { s.goneAt = std::max<uint32_t>(gameTime, 1); }   // Platz belegt (Wrack, Einheit) - spaeter nochmal
	};
	for (Slot &s : s_targets) { slot(s); }
	for (Group &g : s_groups) { for (Slot &s : g.structs) { slot(s); } }
}

/// alle Gegner losschicken: zur ersten Testeinheit (sonst zum Startplatz)
void sendEnemies()
{
	int tx = s_ux, ty = s_uy;
	for (const DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d != nullptr && !d->died) { tx = map_coord(d->pos.x); ty = map_coord(d->pos.y); break; }
	}
	int n = 0;
	for (Group &g : s_groups)
	{
		for (uint32_t id : g.ids)
		{
			DROID *d = IdToDroid(gameWorld.objects, id, s_enemy);
			if (d == nullptr || d->died) { continue; }
			orderDroidLoc(d, DORDER_SCOUT, static_cast<UDWORD>(world_coord(tx) + TILE_UNITS / 2), static_cast<UDWORD>(world_coord(ty) + TILE_UNITS / 2), ModeImmediate);
			n++;
		}
	}
	addConsoleMessage(astringf(_("Test range: %d enemies are on their way"), n).c_str(), CENTRE_JUSTIFY, SYSTEM_MESSAGE);
}

// ---------------------------------------------------------------- Fenster
std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_panel;
constexpr int PANEL_W = 236, PANEL_H = 70, PAD = 8, BTN_Y = 40, BTN_H = 20;
struct Btn { const char *label; const char *tip; int x, w; };
const Btn BTNS[3] = {
	{"New unit", "Place another test unit at the start", PAD, 66},
	{"Send enemies", "All enemy groups attack your test unit", PAD + 70, 86},
	{"Designer", "End the test and return to the unit designer", PAD + 160, 68},
};

void drawPanel(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	modui::wzPanel(x0, y0, x0 + psWidget->width(), y0 + psWidget->height(), "Test range", 0);
	const std::string name = s_design.value("name", std::string("Test unit"));
	modui::text(name, x0 + PAD, y0 + 22, WZCOL_TEXT_BRIGHT, 0);
}

void drawBtn(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0, down = (b->getState() & WBUT_DOWN) != 0;
	const size_t k = static_cast<size_t>(psWidget->UserData) < 3 ? static_cast<size_t>(psWidget->UserData) : 0;
	modui::wzButton(x0, y0, psWidget->width(), psWidget->height(), BTNS[k].label, false, hl, down, 0, nullptr);
}

void ensureScreen()
{
	if (s_screen) { return; }
	auto screen = W_SCREEN::make();
	screen->psForm->hide();
	auto panel = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(panel);
	panel->displayFunction = drawPanel;
	panel->setGeometry(8, 120, PANEL_W, PANEL_H);
	for (int k = 0; k < 3; ++k)
	{
		auto b = std::make_shared<W_BUTTON>();
		panel->attach(b);
		b->displayFunction = drawBtn;
		b->UserData = static_cast<UDWORD>(k);
		b->setGeometry(BTNS[k].x, BTN_Y, BTNS[k].w, BTN_H);
		b->setTip(BTNS[k].tip);
		b->addOnClickHandler([k](W_BUTTON &) {
			if (k == 0)
			{
				if (DROID *d = spawnUnit()) { SelectDroid(d); }
			}
			else if (k == 1) { sendEnemies(); }
			else { intRequestQuitToMainMenu(); }
		});
	}
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_panel = panel;
}
} // namespace

bool launch(const std::string &designJson, int fleet, std::string &err)
{
	nlohmann::json d = nlohmann::json::parse(designJson, nullptr, false);
	if (d.is_discarded() || !d.is_object()) { err = _("This design cannot be tested"); return false; }
	if (levFindDataSet(LEVEL) == nullptr) { err = _("Test map missing (2c-Testgelaende.wz in the maps folder)"); return false; }
	if (!PHYSFS_exists(CFG)) { err = _("Test range missing in the mod (modtest/unittest.json)"); return false; }
	if (!d.contains("name") || !d["name"].is_string() || d["name"].get<std::string>().empty()) { d["name"] = "Test unit"; }
	s_design = d;
	s_fleet = fleet;
	s_armed = true;
	s_autostart = true;
	s_setup = false;
	s_titleReturn = false;
	SPinit(LEVEL_TYPE::SKIRMISH);
	sstrcpy(game.map, LEVEL);
	game.hash = levGetMapNameHash(game.map);
	game.maxPlayers = 2;
	sstrcpy(sRequestResult, CFG);
	challengeFileName = sRequestResult;
	challengeActive = true;
	ingame.side = InGameSide::HOST_OR_SINGLEPLAYER;
	changeTitleUI(std::make_shared<WzMultiplayerOptionsTitleUI>(wzTitleUICurrent));
	return true;
}

bool consumeAutostart()
{
	const bool r = s_autostart && s_armed && challengeActive;
	s_autostart = false;
	return r;
}

bool active()
{
	return s_armed && s_setup && challengeActive && std::strstr(game.map, "Testgelaende") != nullptr;
}

void tick()
{
	if (!s_armed || !challengeActive || selectedPlayer >= MAX_PLAYERS || std::strstr(game.map, "Testgelaende") == nullptr) { return; }
	if (!s_setup)
	{
		if (gameTime < 600) { return; }   // Karte und Spielregeln erst ganz aufbauen lassen
		setup();
	}
	upkeep();
	ensureScreen();
}

void shutdown()
{
	if (s_screen) { widgRemoveOverlayScreen(s_screen); }
	s_screen = nullptr;
	s_panel = nullptr;
	if (s_armed && s_setup)
	{
		s_titleReturn = true;   // nach dem Spiel zurueck in den Designer
		s_armed = false;
	}
	s_setup = false;
	s_targets.clear();
	s_groups.clear();
	s_templates = nlohmann::json();
}

bool titleReturn()
{
	if (!s_titleReturn) { return false; }
	s_titleReturn = false;
	challengeActive = false;
	challengeFileName = "";
	return true;
}

int returnFleet()
{
	return s_fleet;
}
}
