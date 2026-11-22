// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: baubare Strassen (siehe modroad.h)
#include "modroad.h"

#include <algorithm>
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

bool isRoadTileNo(int tileNo)
{
	const Tiles T = tilesForSet();
	return tileNo == T.straight || tileNo == T.straightAlt || tileNo == T.end || tileNo == T.tee || tileNo == T.plaza;
}

/// urspruengliche Bodenkachel unbekannt (Spielstand): haeufigste Nicht-Strassen-Kachel der Nachbarn
Orig guessGround(int x, int y)
{
	const WorldMapState &m = gameWorld.map;
	std::map<uint16_t, int> cnt;
	Orig best{mapTile(m, x, y)->texture, TILE_HAS_DECAL(mapTile(m, x, y)) != 0};
	int bestN = 0;
	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			if ((dx == 0 && dy == 0) || !tileOnMap(m, x + dx, y + dy)) { continue; }
			const MAPTILE *t = mapTile(m, x + dx, y + dy);
			if (isRoadTileNo(TileNumber_tile(t->texture))) { continue; }
			const int n = ++cnt[t->texture];
			if (n > bestN) { bestN = n; best = Orig{t->texture, TILE_HAS_DECAL(t) != 0}; }
		}
	}
	return best;
}

void tick()
{
	// (Wunsch 22.11. 21:19 "sie sollen auch abgerissen werden koennen"): das Strassen-Bauwerk bleibt stehen (unsichtbar, anklickbar,
	// abreissbar); solange es steht, zeigt die Kachel die Strasse - verschwindet es (Abriss, Zerstoerung, Krater), kehrt der Boden zurueck
	WorldMapState &m = gameWorld.map;
	if (m.width <= 0) { return; }
	std::vector<int> now;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !s->pStructureType->id.startsWith("A0Road")) { continue; }
			const int x = map_coord(s->pos.x), y = map_coord(s->pos.y);
			if (tileOnMap(m, x, y)) { now.push_back(y * m.width + x); }
		}
	}
	std::sort(now.begin(), now.end());
	now.erase(std::unique(now.begin(), now.end()), now.end());
	std::vector<int> changed;
	for (auto it = s_roads.begin(); it != s_roads.end();)
	{
		if (std::binary_search(now.begin(), now.end(), it->first)) { ++it; continue; }
		const int x = it->first % m.width, y = it->first / m.width;
		MAPTILE *t = mapTile(m, x, y);
		t->texture = it->second.texture;
		if (it->second.decal) { SET_TILE_DECAL(t); } else { CLEAR_TILE_DECAL(t); }
		markTileDirty(x, y);
		changed.push_back(it->first);
		it = s_roads.erase(it);
	}
	for (int idx : now)
	{
		if (s_roads.count(idx) != 0) { continue; }
		const int x = idx % m.width, y = idx / m.width;
		const MAPTILE *t = mapTile(m, x, y);
		s_roads[idx] = isRoadTileNo(TileNumber_tile(t->texture)) ? guessGround(x, y) : Orig{t->texture, TILE_HAS_DECAL(t) != 0};
		changed.push_back(idx);
	}
	for (int idx : changed) { paintAround(idx % m.width, idx / m.width); }
}

bool isRoad(const STRUCTURE *s)
{
	return s != nullptr && s->pStructureType != nullptr && s->pStructureType->id.startsWith("A0Road");
}

void onCrater(int tx, int ty)
{
	const WorldMapState &m = gameWorld.map;
	if (tx < 0 || ty < 0 || tx >= m.width || ty >= m.height) { return; }
	MAPTILE *t = mapTile(gameWorld.map, tx, ty);
	if (t->psObject != nullptr && t->psObject->type == OBJ_STRUCTURE && isRoad(static_cast<STRUCTURE *>(t->psObject)))
	{
		destroyStruct(static_cast<STRUCTURE *>(t->psObject), gameTime, gameWorld);
	}
}

void reset()
{
	s_roads.clear();
}
}
