// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Befestigungsbau - siehe modbuild.h
#include "modbuild.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "baseobject.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "modterrain.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "ordersource.h"
#include "range_rings.h"
#include "stats.h"
#include "statsdef.h"
#include "structure.h"
#include "action.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>

namespace modbuild
{
namespace
{
constexpr int MAX_PLACEMENTS = 170;

struct Job
{
	unsigned player = 0;
	Vector2i center;                 // Weltkoordinaten
	int tier = 2;
	bool moving = false;             // wir haben dem Fahrzeug einen Marsch zur Mulde gegeben
	Vector2i moveTarget;
	unsigned startTime = 0;
};

std::unordered_map<uint32_t, Job> s_jobs;
int s_tier = 2;
bool s_ringOn = false;

enum class Role
{
	Dragon, Hedgehog, Bunker, Flame, Wall, Mortar, GunTower, RocketPit, Trench
};

const char *const *namesOf(Role r, int &count)
{
	static const char *const dragon[] = {"A0TankTrapHeavy", "A0TankTrap"};
	static const char *const hedge[] = {"A0TankTrap", "A0TankTrapHeavy"};
	static const char *const bunker[] = {"A0BaBaBunker", "PillBox1", "PillBox4"};
	static const char *const flame[] = {"A0BaBaFlameTower", "PillBox5", "Emplacement-Rocket06-IDF"};
	static const char *const wall[] = {"A0BaBaSandbagWall", "A0HardcreteMk1Wall", "A0BaBaHorizontalWall"};
	static const char *const mortar[] = {"A0BaBaMortarPit", "Emplacement-MortarPit01"};
	static const char *const gun[] = {"A0BaBaGunTower", "GuardTower1", "A0BaBaBunker"};
	static const char *const rocket[] = {"A0BaBaRocketPitAT", "A0BaBaRocketPit", "Emplacement-MRL-pit"};
	static const char *const trench[] = {"A0BaBaTrench", "A0BaBaTrenchCover"};
	switch (r)
	{
	case Role::Dragon: count = 2; return dragon;
	case Role::Hedgehog: count = 2; return hedge;
	case Role::Bunker: count = 3; return bunker;
	case Role::Flame: count = 3; return flame;
	case Role::Wall: count = 3; return wall;
	case Role::Mortar: count = 2; return mortar;
	case Role::GunTower: count = 3; return gun;
	case Role::RocketPit: count = 3; return rocket;
	default: count = 2; return trench;
	}
}

STRUCTURE_STATS *pick(unsigned player, Role r)
{
	int n = 0;
	const char *const *names = namesOf(r, n);
	for (int i = 0; i < n; i++)
	{
		STRUCTURE_STATS *st = getStructStatsFromName(WzString::fromUtf8(names[i]));
		if (st == nullptr) { continue; }
		const int idx = static_cast<int>(st - asStructureStats);
		if (idx < 0 || idx >= static_cast<int>(numStructureStats)) { continue; }
		const int status = apStructTypeLists[player][idx];
		if (status == AVAILABLE || status == REDUNDANT)
		{
			return st;
		}
	}
	return nullptr;
}

struct Rect
{
	int x0, y0, x1, y1;   // Kacheln, einschliesslich
};

bool isBaseStructure(const STRUCTURE *s)
{
	if (s == nullptr || s->died || s->pStructureType == nullptr) { return false; }
	switch (s->pStructureType->type)
	{
	case REF_DEFENSE:
	case REF_WALL:
	case REF_WALLCORNER:
	case REF_GATE:
		return false;
	default:
		return true;
	}
}

/// Rechteck der eigenen Gebaeude im Kreis; ohne Gebaeude ein 3x3-Feld um die Mitte
Rect baseRect(unsigned player, const Vector2i &center, int radiusTiles)
{
	const int cx = map_coord(center.x), cy = map_coord(center.y);
	Rect r{cx - 1, cy - 1, cx + 1, cy + 1};
	bool any = false;
	for (const STRUCTURE *s : gameWorld.objects.structures[player])
	{
		if (!isBaseStructure(s)) { continue; }
		const int sx = map_coord(s->pos.x), sy = map_coord(s->pos.y);
		if (std::abs(sx - cx) > radiusTiles || std::abs(sy - cy) > radiusTiles) { continue; }
		const int hw = std::max(1, static_cast<int>(s->pStructureType->baseWidth)) / 2;
		const int hb = std::max(1, static_cast<int>(s->pStructureType->baseBreadth)) / 2;
		if (!any)
		{
			r = Rect{sx - hw, sy - hb, sx + hw, sy + hb};
			any = true;
		}
		else
		{
			r.x0 = std::min(r.x0, sx - hw);
			r.y0 = std::min(r.y0, sy - hb);
			r.x1 = std::max(r.x1, sx + hw);
			r.y1 = std::max(r.y1, sy + hb);
		}
	}
	return r;
}

struct RingSpec
{
	int k;          // Abstand vom Gebaeude-Rechteck (Kacheln)
	Role role;
	int step;       // jede step-te Kachel des Umfangs
	int offset;
	bool gates;     // Tore (3 Kacheln) in der Seitenmitte freilassen
	int minTier;
};

// innen -> aussen. k = Abstand vom Gebaeuderechteck.
const RingSpec kRings[] = {
	{1, Role::Mortar,    4, 1, false, 2},
	{2, Role::Mortar,    7, 3, false, 1},
	{3, Role::Wall,      1, 0, true,  1},
	{4, Role::Bunker,    5, 0, true,  1},
	{4, Role::Flame,     5, 2, true,  2},
	{4, Role::GunTower,  5, 3, true,  1},
	{6, Role::Hedgehog,  3, 0, true,  3},
	{8, Role::Hedgehog,  2, 1, true,  2},
	{11, Role::Dragon,   2, 0, true,  3},
	{9, Role::Dragon,    2, 0, true,  2},
};

struct Cell
{
	int x, y, side;     // side: 0 N, 1 E, 2 S, 3 W
};

/// Umfang des Rechtecks um rect, Abstand k, im Uhrzeigersinn
std::vector<Cell> perimeter(const Rect &b, int k)
{
	std::vector<Cell> v;
	const int x0 = b.x0 - k, y0 = b.y0 - k, x1 = b.x1 + k, y1 = b.y1 + k;
	for (int x = x0; x <= x1; x++) { v.push_back({x, y0, 0}); }
	for (int y = y0 + 1; y <= y1; y++) { v.push_back({x1, y, 1}); }
	for (int x = x1 - 1; x >= x0; x--) { v.push_back({x, y1, 2}); }
	for (int y = y1 - 1; y > y0; y--) { v.push_back({x0, y, 3}); }
	return v;
}

bool inGate(const Rect &b, int k, const Cell &c)
{
	const int midX = (b.x0 + b.x1) / 2, midY = (b.y0 + b.y1) / 2;
	(void)k;
	if (c.side == 0 || c.side == 2) { return std::abs(c.x - midX) <= 1; }
	return std::abs(c.y - midY) <= 1;
}

unsigned outwardDir(int side)
{
	// 0 = Norden; Viertelkreise im Uhrzeigersinn
	return static_cast<unsigned>(side) * 0x4000u;
}

bool tileOk(unsigned player, STRUCTURE_STATS *st, int tx, int ty, unsigned dir)
{
	if (tx < 3 || ty < 3 || tx >= gameWorld.map.width - 3 || ty >= gameWorld.map.height - 3) { return false; }
	const Vector2i w(world_coord(tx) + TILE_UNITS / 2, world_coord(ty) + TILE_UNITS / 2);
	return validLocation(gameWorld, st, w, static_cast<uint16_t>(dir), player, false);
}
} // namespace

int radiusOfTier(int tier)
{
	switch (std::clamp(tier, 1, 3))
	{
	case 1: return 5;
	case 2: return 8;
	default: return 12;
	}
}

int currentTier()
{
	return s_tier;
}

std::vector<Placement> planFor(unsigned player, const Vector2i &worldCenter, int tier)
{
	std::vector<Placement> out;
	if (player >= MAX_PLAYERS) { return out; }
	tier = std::clamp(tier, 1, 3);
	const Rect b = baseRect(player, worldCenter, radiusOfTier(tier));
	for (const RingSpec &rs : kRings)
	{
		if (tier < rs.minTier) { continue; }
		// kleine Anlage: die aeusseren Ringe rutschen nach innen (Ring 4 -> 3 usw. ist hier nicht noetig, Groesse 1 baut nur bis Ring 4)
		if (tier == 1 && rs.k > 4) { continue; }
		if (tier == 2 && rs.k > 9) { continue; }
		STRUCTURE_STATS *st = pick(player, rs.role);
		if (st == nullptr) { continue; }
		const std::vector<Cell> ring = perimeter(b, rs.k);
		if (rs.role == Role::Wall)
		{
			// zusammenhaengende Laeufe je Seite (Tore ausgespart), als Linienbau
			int runStart = -1;
			for (size_t i = 0; i <= ring.size(); i++)
			{
				const bool open = i < ring.size() && !inGate(b, rs.k, ring[i]) && (runStart < 0 || ring[i].side == ring[runStart].side);
				if (open && runStart < 0) { runStart = static_cast<int>(i); }
				if (!open && runStart >= 0)
				{
					const Cell &a = ring[runStart], &z = ring[i - 1];
					if (a.x != z.x || a.y != z.y)
					{
						Placement p;
						p.stats = st; p.tile = Vector2i(a.x, a.y); p.tile2 = Vector2i(z.x, z.y); p.line = true; p.direction = 0;
						out.push_back(p);
					}
					runStart = -1;
					// die Zelle, die den Lauf beendet hat, beginnt ggf. einen neuen (Seitenwechsel)
					if (i < ring.size() && !inGate(b, rs.k, ring[i])) { runStart = static_cast<int>(i); }
				}
				if (static_cast<int>(out.size()) >= MAX_PLACEMENTS) { break; }
			}
			continue;
		}
		for (size_t i = 0; i < ring.size(); i++)
		{
			if (static_cast<int>((i + rs.offset) % rs.step) != 0) { continue; }
			const Cell &c = ring[i];
			if (rs.gates && inGate(b, rs.k, c)) { continue; }
			const unsigned dir = outwardDir(c.side);
			if (!tileOk(player, st, c.x, c.y, dir)) { continue; }
			Placement p;
			p.stats = st; p.tile = Vector2i(c.x, c.y); p.tile2 = p.tile; p.direction = dir;
			out.push_back(p);
			if (static_cast<int>(out.size()) >= MAX_PLACEMENTS) { break; }
		}
		if (static_cast<int>(out.size()) >= MAX_PLACEMENTS) { break; }
	}
	return out;
}

bool canFortify(const DROID *psDroid)
{
	if (psDroid == nullptr || psDroid->died || !psDroid->isConstructionDroid()) { return false; }
	if (psDroid->isVtol() || psDroid->isTransporter()) { return false; }
	return psDroid->numWeaps == 0 || psDroid->asWeaps[0].nStat == 0;
}

bool keysForFortify()
{
	const bool ctrl = keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL);
	const bool shift = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	return ctrl && !shift;
}

bool orderSelected(unsigned player, int worldX, int worldY)
{
	if (player >= MAX_PLAYERS) { return false; }
	if (modlayermap::viewLayer() != modlayer::SURFACE) { return false; }
	if (!worldOnMap(gameWorld.map, worldX, worldY)) { return false; }
	bool any = false;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (d == nullptr || !d->selected || !canFortify(d)) { continue; }
		DroidOrder sOrder(DORDER_FORTIFY, Vector2i(worldX, worldY));
		sOrder.direction = static_cast<uint16_t>(s_tier);
		sendDroidInfo(d, sOrder, false, currentOrderSource());
		any = true;
	}
	return any;
}

void startOrder(DROID *psDroid, int worldX, int worldY, int tier)
{
	if (psDroid == nullptr || !canFortify(psDroid)) { return; }
	psDroid->order = DroidOrder(DORDER_NONE);
	actionDroid(psDroid, DACTION_NONE);
	Job j;
	j.player = psDroid->player;
	j.center = Vector2i(worldX, worldY);
	j.tier = std::clamp(tier, 1, 3);
	j.startTime = gameTime;
	s_jobs[psDroid->id] = j;
}

void updatePreview(bool wanted, const Vector2i &worldPos)
{
	bool show = false;
	if (wanted && selectedPlayer < MAX_PLAYERS && keysForFortify())
	{
		for (const DROID *d : gameWorld.objects.droids[selectedPlayer])
		{
			if (d != nullptr && d->selected && canFortify(d)) { show = true; break; }
		}
	}
	if (show)
	{
		if (mousePressed(MOUSE_WUP)) { s_tier = std::min(3, s_tier + 1); }
		if (mousePressed(MOUSE_WDN)) { s_tier = std::max(1, s_tier - 1); }
		range_rings::setDebugRange(static_cast<float>(worldPos.x), static_cast<float>(worldPos.y), static_cast<float>(radiusOfTier(s_tier) * TILE_UNITS));
		s_ringOn = true;
	}
	else if (s_ringOn)
	{
		range_rings::setDebugRange(0.f, 0.f, 0.f);
		s_ringOn = false;
	}
}

void tick(GameWorld &world)
{
	if (s_jobs.empty()) { return; }
	std::vector<uint32_t> done;
	for (auto &kv : s_jobs)
	{
		Job &j = kv.second;
		DROID *d = IdToDroid(world.objects, kv.first, j.player);
		if (d == nullptr || d->died) { done.push_back(kv.first); continue; }
		const int radius = radiusOfTier(j.tier);

		// ein anderer Befehl des Spielers beendet die Arbeit
		if (d->order.type != DORDER_NONE)
		{
			if (!(j.moving && d->order.type == DORDER_MOVE && d->order.pos == j.moveTarget))
			{
				done.push_back(kv.first);
				continue;
			}
			continue;   // laeuft zur Mulde
		}
		j.moving = false;

		// Phase 1: Mulden im Kreis zuschaufeln
		Vector2i dent;
		if (modterrain::findDent(j.center, radius, 6, dent))
		{
			const Vector2i dw(world_coord(dent.x) + TILE_UNITS / 2, world_coord(dent.y) + TILE_UNITS / 2);
			const int dx = d->pos.x - dw.x, dy = d->pos.y - dw.y;
			if (dx * dx + dy * dy > (TILE_UNITS * 2) * (TILE_UNITS * 2))
			{
				j.moving = true;
				j.moveTarget = dw;
				orderDroidLoc(d, DORDER_MOVE, dw.x, dw.y, ModeImmediate);
			}
			else if (((gameTime / GAME_TICKS_PER_UPDATE) + kv.first) % 2 == 0)
			{
				modterrain::fillTile(dent.x, dent.y, 1);
			}
			if (gameTime - j.startTime > 5 * 60 * GAME_TICKS_PER_SEC) { done.push_back(kv.first); }   // Sicherung gegen Endlosschleife
			continue;
		}

		// Phase 2: Bauplan in die Auftragsliste
		const std::vector<Placement> plan = planFor(j.player, j.center, j.tier);
		for (const Placement &p : plan)
		{
			const Vector2i w1(world_coord(p.tile.x) + TILE_UNITS / 2, world_coord(p.tile.y) + TILE_UNITS / 2);
			const Vector2i w2(world_coord(p.tile2.x) + TILE_UNITS / 2, world_coord(p.tile2.y) + TILE_UNITS / 2);
			DroidOrder o = p.line ? DroidOrder(DORDER_LINEBUILD, p.stats, w1, w2, static_cast<uint16_t>(p.direction))
			                      : DroidOrder(DORDER_BUILD, p.stats, w1, static_cast<uint16_t>(p.direction));
			orderDroidAdd(d, &o);
		}
		done.push_back(kv.first);
	}
	for (uint32_t id : done)
	{
		s_jobs.erase(id);
	}
	(void)world;
}

void reset()
{
	s_jobs.clear();
	s_tier = 2;
	if (s_ringOn)
	{
		range_rings::setDebugRange(0.f, 0.f, 0.f);
		s_ringOn = false;
	}
}
}
