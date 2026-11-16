// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Gelaende verformen (Krater, Schuetzenloecher, tiefe Graeben, Zuschaufeln) - siehe modterrain.h
//
// 16.11. 18:24 (Wunsch): Krater nur 1 Kachel gross, mit Kratertextur, zurueck in den Originalzustand, sobald zugeschuettet; Graeben tiefer;
// harte Kante pro Kachel statt weicher Mulde. Umsetzung: statt Eckpunkte abzusenken, bekommt jede betroffene KACHEL eine Absenkung
// ("Sink"). Die Hoehe einer Stelle = Gelaende minus Absenkung, die an den Kachelraendern nur auf einem schmalen Streifen (SINK_EDGE
// Einheiten je Seite) uebergeht - fast senkrechte Waende. Die Originalhoehen werden nie veraendert; nur die Kacheltextur wird
// fuer Krater getauscht und beim Zuschuetten wiederhergestellt.
#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/framework/fixedpoint.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "modterrain.h"

#include "modgroundfire.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "map.h"
#include "terrain.h"
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
constexpr int SINK_EDGE = 14;          // Uebergangsbreite je Kachelseite (Weltmasseinheiten; Kachel = 128) -> fast senkrechte Kante
constexpr int MAX_CRATER_SINK = 64;    // tiefer werden Krater auch bei vielen Treffern nicht
constexpr int MAX_SINK = 120;          // absolute Grenze (Graeben)
constexpr int COVER_DEPTH = 10;        // ab dieser Tiefe gibt die Mulde Deckung

struct Cell
{
	int16_t dyn = 0;            // Krater / Schuetzenloch (zuschuettbar)
	int16_t fixed = 0;          // tiefer Graben (Bauwerk) - verschwindet mit dem Bauwerk
	uint16_t origTexture = 0;   // Kacheltextur vor dem Krater
	bool texSwapped = false;
	bool origDecal = false;
	int depth() const { return std::max<int>(dyn, fixed); }
};

std::unordered_map<uint64_t, Cell> s_cells;

int layerTag()
{
	return modlayermap::worldsExist() ? static_cast<int>(modlayermap::resident()) : static_cast<int>(modlayer::SURFACE);
}

uint64_t keyOf(int layer, int x, int y)
{
	return (static_cast<uint64_t>(layer) << 48) | (static_cast<uint64_t>(static_cast<uint32_t>(x) & 0xffffff) << 24) | static_cast<uint64_t>(static_cast<uint32_t>(y) & 0xffffff);
}

int cellDepth(int layer, int x, int y)
{
	const auto it = s_cells.find(keyOf(layer, x, y));
	return it == s_cells.end() ? 0 : it->second.depth();
}

bool tileOk(const WorldMapState &m, int x, int y)
{
	if (x < 2 || y < 2 || x >= m.width - 2 || y >= m.height - 2)
	{
		return false;
	}
	const MAPTILE *t = &m.tiles[x + y * m.width];
	if (TileHasStructure(t) || TileHasFeature(t))
	{
		return false;
	}
	const uint8_t tt = terrainType(t);
	return tt != TER_WATER && tt != TER_CLIFFFACE;
}

void dirtyAround(int x, int y)
{
	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			if (tileOnMap(gameWorld.map, x + dx, y + dy)) { markTileDirty(x + dx, y + dy); }
		}
	}
}

/// Kratertextur setzen (Original merken) bzw. wiederherstellen
void swapTexture(Cell &c, int x, int y, bool crater)
{
	MAPTILE *t = mapTile(gameWorld.map, x, y);
	if (crater && !c.texSwapped)
	{
		c.origTexture = t->texture;
		c.origDecal = TILE_HAS_DECAL(t) != 0;
		c.texSwapped = true;
		// Drehung/Spiegelung aus der Lage ableiten (deterministisch) - nicht jeder Krater sieht gleich aus
		const unsigned rot = static_cast<unsigned>(x * 7 + y * 13) & 3u;
		t->texture = static_cast<uint16_t>((RUBBLE_TILE & TILE_NUMMASK) | (rot << TILE_ROTSHIFT));
		SET_TILE_DECAL(t);
	}
	else if (!crater && c.texSwapped)
	{
		t->texture = c.origTexture;
		if (c.origDecal) { SET_TILE_DECAL(t); } else { CLEAR_TILE_DECAL(t); }
		c.texSwapped = false;
	}
}

/// Kachel loeschen, wenn nichts mehr abgesenkt ist (Textur zurueck)
void tidy(uint64_t k, int x, int y)
{
	const auto it = s_cells.find(k);
	if (it == s_cells.end()) { return; }
	if (it->second.dyn <= 0) { swapTexture(it->second, x, y, false); }
	if (it->second.dyn <= 0 && it->second.fixed <= 0) { s_cells.erase(it); }
}

void dust(int wx, int wy, int size)
{
	if (!clipXY(wx, wy))
	{
		return;
	}
	Vector3i pos(wx, map_Height(gameWorld.map, wx, wy) + 12, wy);
	// grosse Staubwolke - verdeckt das Einsinken des Kraters (der Krater entsteht erst kurz danach, siehe tick())
	const int n = 3 + size * 3;
	for (int i = 0; i < n; i++)
	{
		Vector3i p = pos;
		p.x += ((i * 53) % 61 - 30) * size;
		p.z += ((i * 37) % 59 - 29) * size;
		p.y += (i % 3) * 10 * size;
		addEffect(&p, EFFECT_SMOKE, (i % 3 != 2) ? SMOKE_TYPE_BILLOW : SMOKE_TYPE_DRIFTING, false, nullptr, 0, gameTime);
	}
}

struct PendingCrater
{
	int x, y, size;
	unsigned at;
};
std::vector<PendingCrater> s_pending;   // Reihenfolge = Einschlagsreihenfolge (auf allen Rechnern gleich)

int craterDepth(int size)
{
	switch (size)
	{
	case 1: return 16;
	case 2: return 26;
	default: return 36;
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

/// Anteil (in 1/(2E)) der Strecke [c-E, c+E] in der Kachel lo..lo+TILE_UNITS (ganzzahlig)
inline int overlapI(int c, int lo)
{
	const int a = std::max(c - SINK_EDGE, lo), b = std::min(c + SINK_EDGE, lo + TILE_UNITS);
	return std::max(0, b - a);
}
inline float overlapF(float c, float lo)
{
	const float e = static_cast<float>(SINK_EDGE);
	const float a = std::max(c - e, lo), b = std::min(c + e, lo + static_cast<float>(TILE_UNITS));
	return std::max(0.f, b - a);
}
} // namespace

int sinkAt(const WorldMapState &mapState, int worldX, int worldY)
{
	if (s_cells.empty() || &mapState != &gameWorld.map)
	{
		return 0;
	}
	const int layer = layerTag();
	const int tx = worldX >> TILE_SHIFT, ty = worldY >> TILE_SHIFT;
	int sum = 0;
	for (int dy = -1; dy <= 1; ++dy)
	{
		const int wy = overlapI(worldY, (ty + dy) << TILE_SHIFT);
		if (wy == 0) { continue; }
		for (int dx = -1; dx <= 1; ++dx)
		{
			const int wx = overlapI(worldX, (tx + dx) << TILE_SHIFT);
			if (wx == 0) { continue; }
			const int d = cellDepth(layer, tx + dx, ty + dy);
			if (d != 0) { sum += d * wx * wy; }
		}
	}
	constexpr int norm = 4 * SINK_EDGE * SINK_EDGE;
	return (sum + norm / 2) / norm;
}

float sinkAtF(const WorldMapState &mapState, float worldX, float worldY)
{
	if (s_cells.empty() || &mapState != &gameWorld.map)
	{
		return 0.f;
	}
	const int layer = layerTag();
	const int tx = static_cast<int>(std::floor(worldX / TILE_UNITS)), ty = static_cast<int>(std::floor(worldY / TILE_UNITS));
	float sum = 0.f;
	for (int dy = -1; dy <= 1; ++dy)
	{
		const float wy = overlapF(worldY, static_cast<float>((ty + dy) * TILE_UNITS));
		if (wy <= 0.f) { continue; }
		for (int dx = -1; dx <= 1; ++dx)
		{
			const float wx = overlapF(worldX, static_cast<float>((tx + dx) * TILE_UNITS));
			if (wx <= 0.f) { continue; }
			const int d = cellDepth(layer, tx + dx, ty + dy);
			if (d != 0) { sum += static_cast<float>(d) * wx * wy; }
		}
	}
	return sum / static_cast<float>(4 * SINK_EDGE * SINK_EDGE);
}

int sinkAtCorner(const WorldMapState &mapState, int x, int y)
{
	if (s_cells.empty() || &mapState != &gameWorld.map)
	{
		return 0;
	}
	const int layer = layerTag();
	return std::max({cellDepth(layer, x - 1, y - 1), cellDepth(layer, x, y - 1), cellDepth(layer, x - 1, y), cellDepth(layer, x, y)});
}

bool addCrater(int worldX, int worldY, int size)
{
	WorldMapState &m = gameWorld.map;
	size = std::clamp(size, 1, 3);
	if (!worldOnMap(m, worldX, worldY))
	{
		return false;
	}
	const int tx = map_coord(worldX), ty = map_coord(worldY);
	if (!tileOk(m, tx, ty))
	{
		return false;
	}
	const uint64_t k = keyOf(layerTag(), tx, ty);
	Cell &c = s_cells[k];
	const int before = c.dyn;
	const int add = c.dyn > 0 ? craterDepth(size) / 2 : craterDepth(size);   // weitere Treffer vertiefen nur noch halb so viel
	c.dyn = static_cast<int16_t>(std::min(MAX_CRATER_SINK, std::max<int>(c.dyn, 0) + add));
	swapTexture(c, tx, ty, true);
	if (c.dyn == before)
	{
		return false;
	}
	dirtyAround(tx, ty);
	return true;
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
		if (dmg < 12) { return; }
		size = (dmg >= 70 || rad >= 192) ? 3 : 2;
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
	// erst die Staubwolke, der Krater sinkt 0,35 s spaeter darunter ein (kein sichtbares "Aufploppen")
	dust(psProj->pos.x, psProj->pos.y, size);
	s_pending.push_back({ psProj->pos.x, psProj->pos.y, size, gameTime + 350 });
}

void tick()
{
	if (s_pending.empty()) { return; }
	size_t keep = 0;
	for (size_t i = 0; i < s_pending.size(); ++i)
	{
		const PendingCrater pc = s_pending[i];
		if (gameTime >= pc.at)
		{
			addCrater(pc.x, pc.y, pc.size);
		}
		else
		{
			s_pending[keep++] = pc;
		}
	}
	s_pending.resize(keep);
}

int depthAtTile(int tileX, int tileY)
{
	if (s_cells.empty())
	{
		return 0;
	}
	return cellDepth(layerTag(), tileX, tileY);
}

bool tileGivesCover(int tileX, int tileY)
{
	return depthAtTile(tileX, tileY) >= COVER_DEPTH;
}

bool droidInCover(const DROID *psDroid)
{
	if (s_cells.empty() || !isLegged(psDroid))
	{
		return false;
	}
	return tileGivesCover(map_coord(psDroid->pos.x), map_coord(psDroid->pos.y));
}

int fillTile(int tileX, int tileY, int amount)
{
	if (s_cells.empty() || amount <= 0)
	{
		return 0;
	}
	const uint64_t k = keyOf(layerTag(), tileX, tileY);
	const auto it = s_cells.find(k);
	if (it == s_cells.end() || it->second.dyn <= 0)
	{
		return depthAtTile(tileX, tileY);
	}
	// Zuschuetten geht schneller als das Graben dauerte: je Aufruf mindestens 2 Einheiten
	it->second.dyn = static_cast<int16_t>(std::max(0, it->second.dyn - std::max(2, amount * 2)));
	dirtyAround(tileX, tileY);
	tidy(k, tileX, tileY);   // ganz zu -> Originaltextur zurueck
	return depthAtTile(tileX, tileY);
}

bool findDent(const Vector2i &worldCenter, int radiusTiles, int minDepth, Vector2i &outTile)
{
	if (s_cells.empty())
	{
		return false;
	}
	const int layer = layerTag();
	const int cx = map_coord(worldCenter.x), cy = map_coord(worldCenter.y);
	int best = minDepth - 1;
	for (int ty = cy - radiusTiles; ty <= cy + radiusTiles; ty++)
	{
		for (int tx = cx - radiusTiles; tx <= cx + radiusTiles; tx++)
		{
			const auto it = s_cells.find(keyOf(layer, tx, ty));
			if (it == s_cells.end()) { continue; }
			const int d = it->second.dyn;   // nur Krater/Loecher, keine Graben-Bauwerke
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
	(void)bermHeight; (void)dirAngle;   // 16.11.: kein Erdwall mehr - harte Kante, das Loch ist einfach tiefer
	WorldMapState &m = gameWorld.map;
	if (!tileOk(m, tileX, tileY))
	{
		return false;
	}
	Cell &c = s_cells[keyOf(layerTag(), tileX, tileY)];
	const int want = std::clamp(depth * 2, 0, MAX_CRATER_SINK);   // Schuetzenloch: doppelt so tief wie frueher
	if (c.dyn >= want)
	{
		return false;
	}
	c.dyn = static_cast<int16_t>(want);
	dirtyAround(tileX, tileY);
	return true;
}

void setStructureSink(int tileX, int tileY, int depth)
{
	const uint64_t k = keyOf(layerTag(), tileX, tileY);
	depth = std::clamp(depth, 0, MAX_SINK);
	auto it = s_cells.find(k);
	if (it == s_cells.end())
	{
		if (depth <= 0) { return; }
		it = s_cells.emplace(k, Cell()).first;
	}
	if (it->second.fixed == depth) { return; }
	it->second.fixed = static_cast<int16_t>(depth);
	dirtyAround(tileX, tileY);
	tidy(k, tileX, tileY);
}

void reset()
{
	s_cells.clear();
	s_pending.clear();
}
} // namespace modterrain
