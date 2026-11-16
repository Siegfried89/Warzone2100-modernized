// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Betonboden unter Gebaeuden (siehe modconcrete.h)
#include "modconcrete.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"

#include "game_world.h"
#include "map.h"
#include "structure.h"
#include "structuredef.h"
#include "terrain.h"
#include "texture.h"

#include <physfs.h>

#include <cstdio>
#include <string>

namespace modconcrete
{
namespace
{
int s_first = -2;            // -2 = noch nicht gelesen, -1 = keine Betonkacheln im Mod
int s_maskIndex[256];

// Kanten: N=1, O=2, S=4, W=8; Ecken (nur wenn beide angrenzenden Kanten frei sind): NO=16, SO=32, SW=64, NW=128
int canonical(int m)
{
	const int e = m & 15;
	int c = 0;
	if ((m & 16) && !(e & (1 | 2))) { c |= 16; }
	if ((m & 32) && !(e & (4 | 2))) { c |= 32; }
	if ((m & 64) && !(e & (4 | 8))) { c |= 64; }
	if ((m & 128) && !(e & (1 | 8))) { c |= 128; }
	return e | c;
}

void load()
{
	if (s_first != -2) { return; }
	s_first = -1;
	for (int &v : s_maskIndex) { v = -1; }
	if (!PHYSFS_exists("mf/concrete.txt")) { return; }
	char *buf = nullptr;
	UDWORD size = 0;
	if (!loadFile("mf/concrete.txt", &buf, &size) || buf == nullptr) { return; }
	const std::string txt(buf, size);
	free(buf);
	int first = 0, count = 0;
	if (sscanf(txt.c_str(), "%d %d", &first, &count) != 2) { return; }
	int idx = 1;   // 0 = voller Beton
	for (int m = 1; m < 256; ++m)
	{
		if (canonical(m) == m) { s_maskIndex[m] = idx++; }
	}
	if (count != idx || first < 256 || first + count > 256 + modExtTileCount || first + count - 1 > TILE_NUMMASK)
	{
		debug(LOG_ERROR, "MOD Beton: Kachelliste passt nicht (erste %d, Anzahl %d, erwartet %d, Zusatzkacheln %d)", first, count, idx, modExtTileCount);
		for (int &v : s_maskIndex) { v = -1; }
		return;
	}
	s_first = first;
	debug(LOG_INFO, "MOD Beton: %d Kacheln ab Nr. %d", count, first);
}

bool concreteAt(WorldMapState &m, int x, int y)
{
	if (x < 0 || y < 0 || x >= m.width || y >= m.height) { return false; }
	const MAPTILE *t = mapTile(m, x, y);
	if (!TileHasStructure(t)) { return false; }
	return wants(castStructure(t->psObject));
}
}

bool wants(const STRUCTURE *psStruct)
{
	if (psStruct == nullptr || psStruct->pStructureType == nullptr) { return false; }
	switch (psStruct->pStructureType->type)
	{
	case REF_FACTORY:
	case REF_CYBORG_FACTORY:
	case REF_VTOL_FACTORY:
	case REF_POWER_GEN:
		return true;
	default:
		break;
	}
	const WzString &id = psStruct->pStructureType->id;
	return id.compare("A0BaBaATC") == 0 || id.compare("A0BaBaCrane") == 0 || id.compare("A0BaBaRefinery") == 0;
}

int overrideTexture(WorldMapState &m, int x, int y, bool classic)
{
	load();
	if (s_first < 0) { return -1; }
	if (concreteAt(m, x, y))
	{
		const unsigned rot = static_cast<unsigned>(x * 7 + y * 13) & 3u;
		return static_cast<int>((static_cast<unsigned>(s_first) & TILE_NUMMASK) | (rot << TILE_ROTSHIFT));
	}
	if (classic) { return -1; }
	const MAPTILE *t = mapTile(m, x, y);
	if (TILE_HAS_DECAL(t)) { return -1; }
	const unsigned char tt = terrainType(t);
	if (tt == TER_WATER || tt == TER_CLIFFFACE) { return -1; }
	int mask = 0;
	if (concreteAt(m, x, y - 1)) { mask |= 1; }
	if (concreteAt(m, x + 1, y)) { mask |= 2; }
	if (concreteAt(m, x, y + 1)) { mask |= 4; }
	if (concreteAt(m, x - 1, y)) { mask |= 8; }
	if (concreteAt(m, x + 1, y - 1)) { mask |= 16; }
	if (concreteAt(m, x + 1, y + 1)) { mask |= 32; }
	if (concreteAt(m, x - 1, y + 1)) { mask |= 64; }
	if (concreteAt(m, x - 1, y - 1)) { mask |= 128; }
	mask = canonical(mask);
	if (mask == 0 || s_maskIndex[mask] < 0) { return -1; }
	return (s_first + s_maskIndex[mask]) & TILE_NUMMASK;
}

void footprintChanged(const STRUCTURE *psStruct, int tileX, int tileY, int sizeX, int sizeY)
{
	if (!wants(psStruct)) { return; }
	for (int y = tileY - 1; y <= tileY + sizeY; ++y)
	{
		for (int x = tileX - 1; x <= tileX + sizeX; ++x)
		{
			if (x >= 0 && y >= 0 && x < gameWorld.map.width && y < gameWorld.map.height) { markTileDirty(x, y); }
		}
	}
}

void reset()
{
	s_first = -2;
}
}
