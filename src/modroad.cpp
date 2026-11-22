// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: baubare Strassen (siehe modroad.h)
#include "modroad.h"

#include <map>
#include <vector>

#include "lib/framework/frame.h"
#include "game_world.h"
#include "map.h"
#include "objmem.h"
#include "structure.h"
#include "terrain.h"

bool modTileTextureIsDecal(int tileNo);   // map.cpp

namespace modroad
{
namespace
{
struct Orig
{
	uint16_t texture;
	bool decal;
};
std::map<int, Orig> s_roads;   // Feldindex -> urspruengliche Bodenkachel (sortiert: feste Reihenfolge)

struct Tiles
{
	int straight, straightAlt, end, tee, plaza;
};
Tiles tilesForSet()
{
	switch (currentMapTileset)
	{
	case MAP_TILESET::URBAN: return {40, 40, 46, 49, 49};
	case MAP_TILESET::ROCKIES: return {59, 37, 60, 13, 22};
	case MAP_TILESET::ARIZONA:
	default: return {59, 59, 47, 57, 22};
	}
}

bool roadAt(int x, int y)
{
	const WorldMapState &m = gameWorld.map;
	if (x < 0 || y < 0 || x >= m.width || y >= m.height) { return false; }
	return s_roads.count(y * m.width + x) != 0;
}

void setTile(int x, int y, int tileNo, int rot)
{
	MAPTILE *t = mapTile(gameWorld.map, x, y);
	t->texture = static_cast<uint16_t>((static_cast<unsigned>(tileNo) & TILE_NUMMASK) | ((static_cast<unsigned>(rot) & 3u) << TILE_ROTSHIFT));
	if (modTileTextureIsDecal(tileNo)) { SET_TILE_DECAL(t); } else { CLEAR_TILE_DECAL(t); }
	markTileDirty(x, y);
}

/// Strassenkachel eines Feldes nach den Nachbarn (Bits N 1, O 2, S 4, W 8); Drehung = Viertel im Uhrzeigersinn
void paint(int x, int y)
{
	if (!roadAt(x, y)) { return; }
	const Tiles T = tilesForSet();
	const int mask = (roadAt(x, y - 1) ? 1 : 0) | (roadAt(x + 1, y) ? 2 : 0) | (roadAt(x, y + 1) ? 4 : 0) | (roadAt(x - 1, y) ? 8 : 0);
	const int alt = ((x * 7 + y * 13) % 5 == 0) ? T.straightAlt : T.straight;
	switch (mask)
	{
	case 0: case 2: case 8: case 10:            // einzeln oder Ost-West
		if (mask == 2) { setTile(x, y, T.end, 2); }      // Ende: Grundlage fuehrt nach Westen; nach Osten = 2 Viertel
		else if (mask == 8) { setTile(x, y, T.end, 0); }
		else { setTile(x, y, alt, 0); }
		break;
	case 1: setTile(x, y, T.end, 1); break;     // nur Norden
	case 4: setTile(x, y, T.end, 3); break;     // nur Sueden
	case 5: setTile(x, y, alt, 1); break;       // Nord-Sued
	case 14: setTile(x, y, T.tee, 0); break;    // T ohne Norden (Grundlage)
	case 13: setTile(x, y, T.tee, 1); break;    // ohne Osten
	case 11: setTile(x, y, T.tee, 2); break;    // ohne Sueden
	case 7: setTile(x, y, T.tee, 3); break;     // ohne Westen
	default: setTile(x, y, T.plaza, 0); break;  // Kurven und Kreuzung
	}
}

void paintAround(int x, int y)
{
	paint(x, y);
	paint(x, y - 1); paint(x + 1, y); paint(x, y + 1); paint(x - 1, y);
}
}

void tick()
{
	WorldMapState &m = gameWorld.map;
	if (m.width <= 0) { return; }
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !s->pStructureType->id.startsWith("A0Road")) { continue; }
			const int x = map_coord(s->pos.x), y = map_coord(s->pos.y);
			removeStruct(s, true, gameWorld);
			s->died = gameTime;
			if (!tileOnMap(m, x, y)) { continue; }
			const int idx = y * m.width + x;
			if (s_roads.count(idx) == 0)
			{
				const MAPTILE *t = mapTile(m, x, y);
				s_roads[idx] = Orig{t->texture, TILE_HAS_DECAL(t) != 0};
			}
			paintAround(x, y);
		}
	}
}

void onCrater(int tx, int ty)
{
	const WorldMapState &m = gameWorld.map;
	if (tx < 0 || ty < 0 || tx >= m.width || ty >= m.height) { return; }
	const auto it = s_roads.find(ty * m.width + tx);
	if (it == s_roads.end()) { return; }
	MAPTILE *t = mapTile(gameWorld.map, tx, ty);
	t->texture = it->second.texture;
	if (it->second.decal) { SET_TILE_DECAL(t); } else { CLEAR_TILE_DECAL(t); }
	markTileDirty(tx, ty);
	s_roads.erase(it);
	paintAround(tx, ty);
}

void reset()
{
	s_roads.clear();
}
}
