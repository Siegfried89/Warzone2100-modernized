// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Gelaende verformen (Krater, Graeben, Zuschaufeln) - siehe modterrain.h
#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/framework/fixedpoint.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "modterrain.h"
#include "modgroundfire.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "map.h"
#include "droid.h"
#include "droiddef.h"
#include "effects.h"
#include "display3d.h"
#include "projectile.h"
#include "statsdef.h"
#include "stats.h"
#include "structure.h"
#include "mapgrid.h"
#include "lib/gamelib/gtime.h"
#include "game_world.h"

namespace modterrain
{
namespace
{
constexpr int MAX_DEPTH = 110;        // tiefer als so wird der Boden an einem Eckpunkt nie abgesenkt (gegenueber der Ausgangshoehe)
constexpr int MAX_RAISE = 16;         // so hoch wird der Randwall hoechstens
constexpr int COVER_DEPTH = 10;       // ab dieser mittleren Tiefe gibt die Mulde Deckung

struct Vertex
{
	int32_t orig = 0;   // Hoehe, bevor das Modul den Punkt veraendert hat
	int32_t ours = 0;   // Hoehe, die das Modul zuletzt gesetzt hat
};

std::unordered_map<uint64_t, Vertex> s_table;

int layerTag()
{
	return modlayermap::worldsExist() ? static_cast<int>(modlayermap::resident()) : static_cast<int>(modlayer::SURFACE);
}

uint64_t keyOf(int x, int y)
{
	return (static_cast<uint64_t>(layerTag()) << 48) | (static_cast<uint64_t>(static_cast<uint32_t>(x) & 0xffffff) << 24) | static_cast<uint64_t>(static_cast<uint32_t>(y) & 0xffffff);
}

bool vertexOk(const WorldMapState &m, int x, int y)
{
	if (x < 2 || y < 2 || x >= m.width - 2 || y >= m.height - 2)
	{
		return false;
	}
	for (int dy = -1; dy <= 0; dy++)
	{
		for (int dx = -1; dx <= 0; dx++)
		{
			const MAPTILE *t = &m.tiles[(x + dx) + (y + dy) * m.width];
			if (TileHasStructure(t))
			{
				return false;
			}
			const uint8_t tt = terrainType(t);
			if (tt == TER_WATER || tt == TER_CLIFFFACE)
			{
				return false;
			}
		}
	}
	return true;
}

/// Eckpunkt um delta verschieben (Tiefe/Wall begrenzt, Fremdaenderungen werden erkannt). true, wenn sich etwas geaendert hat.
bool applyDelta(WorldMapState &m, int x, int y, int delta, int maxRaise = MAX_RAISE)
{
	if (delta == 0 || !vertexOk(m, x, y))
	{
		return false;
	}
	const int cur = m.tiles[x + y * m.width].height;
	const uint64_t k = keyOf(x, y);
	int orig = cur;
	const auto it = s_table.find(k);
	if (it != s_table.end())
	{
		if (it->second.ours == cur)
		{
			orig = it->second.orig;
		}
		else
		{
			s_table.erase(it);   // jemand anders hat die Hoehe veraendert (Fundament, Grube): als neue Ausgangshoehe nehmen
		}
	}
	const int target = std::clamp(cur + delta, orig - MAX_DEPTH, orig + maxRaise);
	if (target == cur)
	{
		return false;
	}
	setTileHeight(m, x, y, target);
	if (target == orig)
	{
		s_table.erase(k);
	}
	else
	{
		s_table[k] = Vertex{orig, target};
	}
	return true;
}

/// Ausgangshoehe minus aktuelle Hoehe (positiv = Mulde); 0, wenn der Punkt nicht vom Modul veraendert wurde
int vertexDepth(const WorldMapState &m, int x, int y)
{
	if (x < 0 || y < 0 || x >= m.width || y >= m.height)
	{
		return 0;
	}
	const auto it = s_table.find(keyOf(x, y));
	if (it == s_table.end())
	{
		return 0;
	}
	const int cur = m.tiles[x + y * m.width].height;
	if (cur != it->second.ours)
	{
		return 0;
	}
	return it->second.orig - cur;
}

void dust(int wx, int wy, int size)
{
	if (!clipXY(wx, wy))
	{
		return;
	}
	Vector3i pos(wx, map_Height(gameWorld.map, wx, wy) + 12, wy);
	const int n = 2 + size * 2;
	for (int i = 0; i < n; i++)
	{
		Vector3i p = pos;
		p.x += ((i * 53) % 61 - 30) * size;
		p.z += ((i * 37) % 59 - 29) * size;
		addEffect(&p, EFFECT_SMOKE, (i % 2) ? SMOKE_TYPE_BILLOW : SMOKE_TYPE_DRIFTING, false, nullptr, 0, gameTime);
	}
}

struct CraterShape
{
	float radius;      // Kacheln
	int depth;         // Weltmasseinheiten
};

CraterShape shapeOf(int size)
{
	switch (size)
	{
	case 1: return {1.5f, 26};
	case 2: return {2.3f, 44};
	default: return {3.3f, 66};
	}
}

bool isLegged(const DROID *d)
{
	if (d == nullptr)
	{
		return false;
	}
	const PROPULSION_STATS *p = d->getPropulsionStats();
	return p != nullptr && p->propulsionType == PROPULSION_TYPE_LEGGED;
}
} // namespace

bool addCrater(int worldX, int worldY, int size)
{
	WorldMapState &m = gameWorld.map;
	size = std::clamp(size, 1, 3);
	if (!worldOnMap(m, worldX, worldY))
	{
		return false;
	}
	const CraterShape sh = shapeOf(size);
	const float cx = static_cast<float>(worldX) / TILE_UNITS;
	const float cy = static_cast<float>(worldY) / TILE_UNITS;
	const int r = static_cast<int>(std::ceil(sh.radius)) + 1;
	const int ix = static_cast<int>(std::floor(cx));
	const int iy = static_cast<int>(std::floor(cy));
	// Mitte muss gueltig sein, sonst kein Krater (verhindert halbe Krater an Mauern und Wasser)
	if (!vertexOk(m, ix, iy) && !vertexOk(m, ix + 1, iy + 1))
	{
		return false;
	}
	bool any = false;
	for (int vy = iy - r; vy <= iy + r + 1; vy++)
	{
		for (int vx = ix - r; vx <= ix + r + 1; vx++)
		{
			const float dx = vx - cx, dy = vy - cy;
			const float d = std::sqrt(dx * dx + dy * dy);
			int delta = 0;
			if (d <= sh.radius)
			{
				const float t = d / sh.radius;
				const float s = 1.0f - t * t;
				delta = -static_cast<int>(std::lround(sh.depth * s * s));
			}
			else if (d <= sh.radius + 1.0f)
			{
				const float u = (d - sh.radius);
				delta = static_cast<int>(std::lround(sh.depth * 0.2f * (1.0f - u)));
			}
			if (delta != 0)
			{
				any = applyDelta(m, vx, vy, delta) || any;
			}
		}
	}
	if (any)
	{
		dust(worldX, worldY, size);
	}
	return any;
}

void onProjectileImpact(const PROJECTILE *psProj)
{
	if (psProj == nullptr || psProj->psWStats == nullptr)
	{
		return;
	}
	// nur Treffer auf freien Boden (Fehlschuss, Bodenziel); Treffer auf Einheiten und Bauwerke graben nicht
	if (psProj->psDest != nullptr && !modgroundfire::isMarker(psProj->psDest))
	{
		return;
	}
	const WEAPON_STATS *ws = psProj->psWStats;
	const unsigned dmg = ws->upgrade[psProj->player].damage;
	const unsigned rad = ws->upgrade[psProj->player].radius;
	int size = 0;
	switch (ws->weaponSubClass)
	{
	case WSC_CANNON:
		if (dmg < 20) { return; }
		size = (dmg >= 70 || rad >= 192) ? 3 : (dmg >= 35 || rad >= 96) ? 2 : 1;
		break;
	case WSC_MORTARS:
		size = (dmg >= 70 || rad >= 192) ? 3 : 2;
		if (dmg < 12) { return; }
		break;
	case WSC_HOWITZERS:
	case WSC_BOMB:
		size = 3;
		break;
	default:
		return;
	}
	if (size == 1)
	{
		// leichte Kanonen graben nur jeden dritten Schuss (deterministisch aus Ort und Zeit)
		const unsigned h = static_cast<unsigned>((psProj->pos.x >> 7) * 31 + (psProj->pos.y >> 7) * 17) + gameTime / 97;
		if (h % 3 != 0)
		{
			return;
		}
	}
	addCrater(psProj->pos.x, psProj->pos.y, size);
}

int depthAtTile(int tileX, int tileY)
{
	const WorldMapState &m = gameWorld.map;
	if (s_table.empty() || tileX < 0 || tileY < 0 || tileX >= m.width - 1 || tileY >= m.height - 1)
	{
		return 0;
	}
	const int sum = vertexDepth(m, tileX, tileY) + vertexDepth(m, tileX + 1, tileY) + vertexDepth(m, tileX, tileY + 1) + vertexDepth(m, tileX + 1, tileY + 1);
	return std::max(0, sum / 4);
}

bool tileGivesCover(int tileX, int tileY)
{
	return depthAtTile(tileX, tileY) >= COVER_DEPTH;
}

bool droidInCover(const DROID *psDroid)
{
	if (s_table.empty() || !isLegged(psDroid))
	{
		return false;
	}
	return tileGivesCover(map_coord(psDroid->pos.x), map_coord(psDroid->pos.y));
}

int fillTile(int tileX, int tileY, int amount)
{
	WorldMapState &m = gameWorld.map;
	if (s_table.empty() || amount <= 0)
	{
		return 0;
	}
	for (int vy = tileY - 1; vy <= tileY + 2; vy++)
	{
		for (int vx = tileX - 1; vx <= tileX + 2; vx++)
		{
			if (vx < 0 || vy < 0 || vx >= m.width || vy >= m.height)
			{
				continue;
			}
			const auto it = s_table.find(keyOf(vx, vy));
			if (it == s_table.end())
			{
				continue;
			}
			const int cur = m.tiles[vx + vy * m.width].height;
			if (cur != it->second.ours)
			{
				s_table.erase(it);
				continue;
			}
			const int diff = it->second.orig - cur;   // > 0: Mulde, < 0: Wall
			const int step = std::clamp(diff, -amount, amount);
			if (step == 0)
			{
				s_table.erase(it);
				continue;
			}
			const int target = cur + step;
			setTileHeight(m, vx, vy, target);
			if (target == it->second.orig)
			{
				s_table.erase(it);
			}
			else
			{
				it->second.ours = target;
			}
		}
	}
	return depthAtTile(tileX, tileY);
}

bool findDent(const Vector2i &worldCenter, int radiusTiles, int minDepth, Vector2i &outTile)
{
	if (s_table.empty())
	{
		return false;
	}
	const int cx = map_coord(worldCenter.x), cy = map_coord(worldCenter.y);
	int best = minDepth - 1;
	for (int ty = cy - radiusTiles; ty <= cy + radiusTiles; ty++)
	{
		for (int tx = cx - radiusTiles; tx <= cx + radiusTiles; tx++)
		{
			const int d = depthAtTile(tx, ty);
			if (d > best)
			{
				best = d;
				outTile = Vector2i(tx, ty);
			}
		}
	}
	return best >= minDepth;
}

bool dig(int tileX, int tileY, int depth, int bermHeight, unsigned dirAngle)
{
	WorldMapState &m = gameWorld.map;
	bool any = false;
	for (int dy = 0; dy <= 1; dy++)
	{
		for (int dx = 0; dx <= 1; dx++)
		{
			any = applyDelta(m, tileX + dx, tileY + dy, -depth) || any;
		}
	}
	if (bermHeight > 0)
	{
		const int b = std::min(bermHeight, MAX_RAISE);
		// Laengsrichtung auf die naechste Achse runden: Richtung 0 = Norden (-y); 16384 = Osten (+x)
		const unsigned q = ((dirAngle + 8192u) >> 14) & 3u;   // 0 Nord, 1 Ost, 2 Sued, 3 West
		const bool alongY = (q == 0 || q == 2);
		if (alongY)
		{
			// Graben laeuft in y-Richtung: Wall links und rechts (x)
			for (int dy = 0; dy <= 1; dy++)
			{
				any = applyDelta(m, tileX - 1, tileY + dy, b, b) || any;
				any = applyDelta(m, tileX + 2, tileY + dy, b, b) || any;
			}
		}
		else
		{
			for (int dx = 0; dx <= 1; dx++)
			{
				any = applyDelta(m, tileX + dx, tileY - 1, b, b) || any;
				any = applyDelta(m, tileX + dx, tileY + 2, b, b) || any;
			}
		}
	}
	return any;
}

void reset()
{
	s_table.clear();
}
} // namespace modterrain
