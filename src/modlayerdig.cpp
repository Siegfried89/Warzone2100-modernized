// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Bohrfahrzeug (siehe modlayerdig.h)

#include "modlayerdig.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"

#include "action.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "multiplay.h"
#include "order.h"
#include "orderdef.h"
#include "stats.h"

#include <cstdlib>
#include <deque>
#include <map>
#include <string>

namespace modlayerdig
{
namespace
{
constexpr unsigned DIG_MS = 2000;      // Bohrzeit je Feld
constexpr int REVEAL_RAD = 4;

struct Goal
{
	unsigned player = 0;
	Vector2i tile;
	bool digging = false;
	Vector2i digTile;
	unsigned digUntil = 0;
	std::deque<Vector2i> route;   // weitere Ziele (Shift+Rechtsklick), nacheinander angebohrt
};

std::map<uint32_t, Goal> s_goals;
}

bool isDrill(const DROID *d)
{
	if (d == nullptr || d->died) { return false; }
	const CONSTRUCT_STATS *cs = d->getConstructStats();
	return cs != nullptr && cs->id.toStdString() == "DrillRig";
}

bool onOrder(DROID *psDroid, const DroidOrder *psOrder)
{
	if (psDroid == nullptr) { return false; }
	const bool had = s_goals.erase(psDroid->id) > 0;
	(void)had;
	if (psOrder == nullptr || psOrder->type != DORDER_MOVE) { return false; }
	if (!isDrill(psDroid) || modlayer::layerOf(psDroid) != modlayer::UNDERGROUND || !modlayermap::worldsExist()) { return false; }
	if (!worldOnMap(gameWorld.map, psOrder->pos.x, psOrder->pos.y)) { return false; }
	Goal g;
	g.player = psDroid->player;
	g.tile = Vector2i(map_coord(psOrder->pos.x), map_coord(psOrder->pos.y));
	s_goals[psDroid->id] = g;
	actionDroid(psDroid, DACTION_NONE);
	psDroid->order = DroidOrder(DORDER_NONE);
	return true;
}

bool onQueue(DROID *psDroid, const DroidOrder *psOrder)
{
	if (psDroid == nullptr || psOrder == nullptr || psOrder->type != DORDER_MOVE) { return false; }
	if (!isDrill(psDroid) || modlayer::layerOf(psDroid) != modlayer::UNDERGROUND || !modlayermap::worldsExist()) { return false; }
	if (!worldOnMap(gameWorld.map, psOrder->pos.x, psOrder->pos.y)) { return false; }
	const Vector2i target(map_coord(psOrder->pos.x), map_coord(psOrder->pos.y));
	auto it = s_goals.find(psDroid->id);
	if (it == s_goals.end())
	{
		// noch kein Ziel: das erste Shift-Ziel wird wie ein normaler Befehl gestartet
		return onOrder(psDroid, psOrder);
	}
	it->second.route.push_back(target);
	return true;
}

void tick(GameWorld &world)
{
	if (s_goals.empty()) { return; }
	for (auto it = s_goals.begin(); it != s_goals.end();)
	{
		Goal &g = it->second;
		DROID *d = IdToDroid(world.objects, it->first, g.player);
		if (d == nullptr || d->died || modlayer::layerOf(d) != modlayer::UNDERGROUND || d->order.type != DORDER_NONE)
		{
			it = s_goals.erase(it);
			continue;
		}
		if (g.digging)
		{
			if (gameTime < g.digUntil) { ++it; continue; }
			modlayermap::drillTile(g.digTile.x, g.digTile.y, static_cast<int>(g.player), REVEAL_RAD);
			g.digging = false;
		}
		if (d->action != DACTION_NONE) { ++it; continue; }   // faehrt noch
		const Vector2i cur(map_coord(d->pos.x), map_coord(d->pos.y));
		if (cur == g.tile)
		{
			if (g.route.empty()) { it = s_goals.erase(it); continue; }
			g.tile = g.route.front();   // naechstes Ziel der Route
			g.route.pop_front();
			if (cur == g.tile) { continue; }
		}
		const int dx = g.tile.x - cur.x, dy = g.tile.y - cur.y;
		Vector2i next = cur;
		if (std::abs(dx) >= std::abs(dy)) { next.x += (dx > 0 ? 1 : -1); } else { next.y += (dy > 0 ? 1 : -1); }
		if (modlayermap::isSolid(modlayer::UNDERGROUND, next.x, next.y))
		{
			g.digging = true;
			g.digTile = next;
			g.digUntil = gameTime + DIG_MS;
		}
		else
		{
			actionDroid(d, DACTION_MOVE, static_cast<UDWORD>(world_coord(next.x) + TILE_UNITS / 2), static_cast<UDWORD>(world_coord(next.y) + TILE_UNITS / 2));
		}
		++it;
	}
}

void reset()
{
	s_goals.clear();
}
}
