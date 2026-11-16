// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Bohrfahrzeug (siehe modlayerdig.h)

#include "modlayerdig.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"

#include "action.h"
#include "droid.h"
#include "fpath.h"
#include "lighting.h"
#include "terrain.h"
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
#include <vector>
#include <algorithm>

namespace modlayerdig
{
namespace
{
constexpr unsigned DIG_MS = 2000 / TILE_SPLIT;   // Bohrzeit je Feld (Kachel-Viertelung: je Feinfeld anteilig)
constexpr int REVEAL_RAD = 4 * TILE_SPLIT;   // Original-Kacheln -> Raster
constexpr int PLUNGE_DEPTH = 3 * TILE_SPLIT;   // ab so vielen Klippenfeldern am Stueck bohrt sich das Fahrzeug in den Untergrund

struct Goal
{
	unsigned player = 0;
	Vector2i tile{0, 0};
	bool digging = false;
	Vector2i digTile{0, 0};
	unsigned digUntil = 0;
	std::deque<Vector2i> route;   // weitere Ziele (Shift+Rechtsklick), nacheinander angebohrt
	bool surface = false;         // Oberflaeche: das Fahrzeug bohrt sich durch Klippen zu einem sonst unerreichbaren Ziel
	int depth = 0;                // Klippenfelder am Stueck, die schon durchbohrt wurden
	std::vector<Vector2i> bored;  // schon durchbohrte Felder dieser Fahrt (zaehlen weiter als "am Stueck")
	Vector2i lastTile{-1, -1};    // Standfeld beim letzten Fahrversuch
	int stuck = 0;                // Fahrversuche ohne Fortschritt (unerreichbares Zwischenfeld): nach einigen gibt das Fahrzeug auf
};

std::map<uint32_t, Goal> s_goals;

/// Oberflaeche: ein Klippenfeld wird zum Durchgang (Boden wie die Nachbarn, Ecken auf die Hoehe des Einstiegs, Blockierung weg)
void boreSurfaceTile(int x, int y, int groundHeight, int fallbackX, int fallbackY)
{
	WorldMapState &m = gameWorld.map;
	if (!tileOnMap(m, x, y)) { return; }
	MAPTILE *t = mapTile(m, x, y);
	const MAPTILE *ref = nullptr;
	for (int dy = -1; dy <= 1 && ref == nullptr; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			if ((dx == 0 && dy == 0) || !tileOnMap(m, x + dx, y + dy)) { continue; }
			const MAPTILE *n = mapTile(m, x + dx, y + dy);
			if (terrainType(n) != TER_CLIFFFACE && terrainType(n) != TER_WATER) { ref = n; break; }
		}
	}
	if (ref == nullptr && tileOnMap(m, fallbackX, fallbackY)) { ref = mapTile(m, fallbackX, fallbackY); }   // kein offenes Nachbarfeld: Boden des Fahrzeugs
	if (ref != nullptr)
	{
		t->texture = ref->texture;
		t->ground = ref->ground;
		if (TILE_HAS_DECAL(ref)) { SET_TILE_DECAL(t); } else { CLEAR_TILE_DECAL(t); }
	}
	for (int cy = y; cy <= y + 1; ++cy)
	{
		for (int cx = x; cx <= x + 1; ++cx)
		{
			if (tileOnMap(m, cx, cy)) { setTileHeight(m, cx, cy, groundHeight); }
		}
	}
	auxClearBlocking(m, x, y, FEATURE_BLOCKED);
	markTileDirty(x, y);
	if (terrainType(t) == TER_CLIFFFACE) { return; }   // auch das Standfeld war Fels: bleibt unveraendert (die Fahrt wird abgebrochen)
	mapFloodFillContinents(m);
	initLighting(m, static_cast<UDWORD>(std::max(0, x - 3)), static_cast<UDWORD>(std::max(0, y - 3)),
	             static_cast<UDWORD>(std::min<int>(m.width, x + 5)), static_cast<UDWORD>(std::min<int>(m.height, y + 5)));
}
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
	if (!isDrill(psDroid) || !modlayermap::worldsExist()) { return false; }
	if (!worldOnMap(gameWorld.map, psOrder->pos.x, psOrder->pos.y)) { return false; }
	bool surface = false;
	if (modlayer::layerOf(psDroid) == modlayer::SURFACE)
	{
		// Oberflaeche: erreicht das Fahrzeug das Ziel auf normalem Weg, gilt der gewoehnliche Marschbefehl; liegt das Ziel hinter
		// Klippen (oder in der Klippe), bohrt es sich geradeaus hindurch
		Position dest(psOrder->pos.x, psOrder->pos.y, 0);
		if (fpathCheck(gameWorld.map, psDroid->pos, dest, psDroid->getPropulsionStats()->propulsionType)) { return false; }
		surface = true;
	}
	else if (modlayer::layerOf(psDroid) != modlayer::UNDERGROUND)
	{
		return false;
	}
	Goal g;
	g.surface = surface;
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
		if (d == nullptr || d->died || modlayer::layerOf(d) != (g.surface ? modlayer::SURFACE : modlayer::UNDERGROUND) || d->order.type != DORDER_NONE)
		{
			it = s_goals.erase(it);
			continue;
		}
		if (g.digging)
		{
			if (gameTime < g.digUntil) { ++it; continue; }
			g.digging = false;
			if (g.surface)
			{
				// Klippenfeld durchbohrt; Ecken auf die Hoehe des Einstiegs (Gelaende unter dem Fahrzeug)
				boreSurfaceTile(g.digTile.x, g.digTile.y, static_cast<int>(map_Height(gameWorld.map, d->pos.x, d->pos.y)), map_coord(d->pos.x), map_coord(d->pos.y));
				if (terrainType(mapTile(gameWorld.map, g.digTile.x, g.digTile.y)) == TER_CLIFFFACE) { it = s_goals.erase(it); continue; }
				g.bored.push_back(g.digTile);
				++g.depth;
				if (g.depth >= PLUNGE_DEPTH)
				{
					// tief genug: das Fahrzeug bohrt sich nach unten und landet im Untergrund, dort werden vier Felder frei
					modlayer::moveToLayer(d, modlayer::UNDERGROUND);
					const int px = std::clamp(static_cast<int>(map_coord(d->pos.x)), 1, static_cast<int>(gameWorld.map.width) - 3);
					const int py = std::clamp(static_cast<int>(map_coord(d->pos.y)), 1, static_cast<int>(gameWorld.map.height) - 3);
					for (int oy = 0; oy < 2; ++oy)
					{
						for (int ox = 0; ox < 2; ++ox)
						{
							modlayermap::drillTile(px + ox, py + oy, static_cast<int>(g.player), REVEAL_RAD);
						}
					}
					actionDroid(d, DACTION_NONE);
					it = s_goals.erase(it);
					continue;
				}
			}
			else
			{
				modlayermap::drillTile(g.digTile.x, g.digTile.y, static_cast<int>(g.player), REVEAL_RAD);
			}
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
		const bool solid = g.surface ? (tileOnMap(gameWorld.map, next.x, next.y) && terrainType(mapTile(gameWorld.map, next.x, next.y)) == TER_CLIFFFACE)
		                             : modlayermap::isSolid(modlayer::UNDERGROUND, next.x, next.y);
		if (!solid && std::find(g.bored.begin(), g.bored.end(), next) == g.bored.end()) { g.depth = 0; }
		if (g.surface)
		{
			if (cur == g.lastTile) { if (++g.stuck > 12) { it = s_goals.erase(it); continue; } } else { g.stuck = 0; g.lastTile = cur; }
		}
		if (solid)
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
