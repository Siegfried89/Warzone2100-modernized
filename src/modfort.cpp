// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Infanterie-Stellungen als zusammenhaengendes Netz (siehe modfort.h)

#include "modfort.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "objmem.h"
#include "stats.h"
#include "structure.h"

#include <physfs.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>

namespace modfort
{
namespace
{
constexpr unsigned REBUILD_MS = 2000;
constexpr int ENTRY_SPACING = 5;      // bei langen Linien hoechstens alle 5 Felder eine Oeffnung

bool s_tableLoaded = false;
std::map<std::string, std::string> s_family;            // Bauwerks-ID -> Modellkuerzel
unsigned s_bucket = UINT32_MAX;
int s_w = 0, s_h = 0;
std::vector<uint8_t> s_grid;                            // 0x10 | offene Seiten
std::unordered_map<uint32_t, uint8_t> s_open;           // Bauwerks-ID -> offene Seiten (Anzeige)
std::unordered_map<std::string, const iIMDShape *> s_models;

void loadTable()
{
	if (s_tableLoaded) { return; }
	s_tableLoaded = true;
	s_family.clear();
	char *buf = nullptr;
	UDWORD size = 0;
	if (!PHYSFS_exists("mf/fort.txt") || !loadFile("mf/fort.txt", &buf, &size) || buf == nullptr) { return; }
	std::istringstream in(std::string(buf, size));
	free(buf);
	std::string id, key;
	while (in >> id >> key) { s_family[id] = key; }
	debug(LOG_INFO, "MOD Stellungen: %zu Arten", s_family.size());
}

const std::string *familyOf(const STRUCTURE *s)
{
	if (s == nullptr || s->pStructureType == nullptr) { return nullptr; }
	loadTable();
	if (s_family.empty()) { return nullptr; }
	const auto it = s_family.find(s->pStructureType->id.toUtf8());
	return it == s_family.end() ? nullptr : &it->second;
}

bool surfaceResident()
{
	return !modlayermap::worldsExist() || modlayermap::resident() == modlayer::SURFACE;
}

// Seite k (0 Nord, 1 Ost, 2 Sued, 3 West): Bit und Feldversatz
const int BIT[4] = {1, 2, 4, 8};
const int DX[4] = {0, 1, 0, -1};
const int DY[4] = {-1, 0, 1, 0};

bool homeOf(int player, int64_t &hx, int64_t &hy)
{
	int64_t sx = 0, sy = 0, n = 0;
	for (const STRUCTURE *s : gameWorld.objects.structures[player])
	{
		if (s->died || s->pStructureType == nullptr || s->status != SS_BUILT || modlayer::layerOf(s) != modlayer::SURFACE) { continue; }
		switch (s->pStructureType->type)
		{
		case REF_HQ: case REF_FACTORY: case REF_CYBORG_FACTORY: case REF_VTOL_FACTORY: case REF_POWER_GEN: case REF_RESEARCH:
			sx += s->pos.x; sy += s->pos.y; ++n;
			break;
		default:
			break;
		}
	}
	if (n == 0)
	{
		for (const DROID *d : gameWorld.objects.droids[player])
		{
			if (d->died) { continue; }
			sx += d->pos.x; sy += d->pos.y; ++n;
		}
	}
	if (n == 0) { return false; }
	hx = sx / n;
	hy = sy / n;
	return true;
}

bool sideUsable(int x, int y)
{
	if (x < 1 || y < 1 || x >= s_w - 1 || y >= s_h - 1) { return false; }
	const MAPTILE *t = mapTile(gameWorld.map, x, y);
	const unsigned char tt = terrainType(t);
	if (tt == TER_CLIFFFACE || tt == TER_WATER) { return false; }
	return !TileHasStructure(t);
}

void rebuild()
{
	s_w = gameWorld.map.width;
	s_h = gameWorld.map.height;
	s_grid.assign(static_cast<size_t>(s_w) * static_cast<size_t>(s_h), 0);
	s_open.clear();
	loadTable();
	if (s_family.empty() || s_w <= 0 || s_h <= 0 || !surfaceResident()) { return; }
	std::vector<int16_t> owner(s_grid.size(), -1);
	std::vector<uint32_t> sid(s_grid.size(), 0);
	bool any = false;
	for (int p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || familyOf(s) == nullptr || modlayer::layerOf(s) != modlayer::SURFACE) { continue; }
			const int x = map_coord(s->pos.x), y = map_coord(s->pos.y);
			if (x < 0 || y < 0 || x >= s_w || y >= s_h) { continue; }
			const size_t i = static_cast<size_t>(x) + static_cast<size_t>(y) * s_w;
			owner[i] = static_cast<int16_t>(p);
			sid[i] = s->id;
			any = true;
		}
	}
	if (!any) { s_grid.clear(); return; }
	std::vector<uint8_t> conn(s_grid.size(), 0), entry(s_grid.size(), 0);
	for (int y = 0; y < s_h; ++y)
	{
		for (int x = 0; x < s_w; ++x)
		{
			const size_t i = static_cast<size_t>(x) + static_cast<size_t>(y) * s_w;
			if (owner[i] < 0) { continue; }
			for (int k = 0; k < 4; ++k)
			{
				const int nx = x + DX[k], ny = y + DY[k];
				if (nx < 0 || ny < 0 || nx >= s_w || ny >= s_h) { continue; }
				if (owner[static_cast<size_t>(nx) + static_cast<size_t>(ny) * s_w] == owner[i]) { conn[i] |= static_cast<uint8_t>(BIT[k]); }
			}
		}
	}
	// zusammenhaengende Netze je Spieler; Oeffnungen zur eigenen Basis hin
	int64_t homeX[MAX_PLAYERS], homeY[MAX_PLAYERS];
	bool haveHome[MAX_PLAYERS];
	for (int p = 0; p < MAX_PLAYERS; ++p) { haveHome[p] = homeOf(p, homeX[p], homeY[p]); }
	std::vector<uint8_t> seen(s_grid.size(), 0);
	std::vector<size_t> comp, stack;
	for (size_t start = 0; start < s_grid.size(); ++start)
	{
		if (owner[start] < 0 || seen[start]) { continue; }
		const int p = owner[start];
		comp.clear();
		stack.assign(1, start);
		seen[start] = 1;
		while (!stack.empty())
		{
			const size_t i = stack.back();
			stack.pop_back();
			comp.push_back(i);
			const int x = static_cast<int>(i % s_w), y = static_cast<int>(i / s_w);
			for (int k = 0; k < 4; ++k)
			{
				if (!(conn[i] & BIT[k])) { continue; }
				const size_t j = static_cast<size_t>(x + DX[k]) + static_cast<size_t>(y + DY[k]) * s_w;
				if (!seen[j]) { seen[j] = 1; stack.push_back(j); }
			}
		}
		const int64_t hx = haveHome[p] ? homeX[p] : world_coord(s_w / 2), hy = haveHome[p] ? homeY[p] : world_coord(s_h / 2);
		auto d2 = [&](size_t i) {
			const int64_t cx = world_coord(static_cast<int>(i % s_w)) + TILE_UNITS / 2 - hx;
			const int64_t cy = world_coord(static_cast<int>(i / s_w)) + TILE_UNITS / 2 - hy;
			return cx * cx + cy * cy;
		};
		std::sort(comp.begin(), comp.end(), [&](size_t a, size_t b) { const int64_t da = d2(a), db = d2(b); return da != db ? da < db : a < b; });
		std::vector<size_t> entries;
		auto bestSide = [&](size_t i, bool needFacing) {
			const int x = static_cast<int>(i % s_w), y = static_cast<int>(i / s_w);
			const int64_t vx = hx - (world_coord(x) + TILE_UNITS / 2), vy = hy - (world_coord(y) + TILE_UNITS / 2);
			int best = -1;
			int64_t bestDot = 0;
			for (int k = 0; k < 4; ++k)
			{
				if (conn[i] & BIT[k]) { continue; }
				if (!sideUsable(x + DX[k], y + DY[k])) { continue; }
				const int64_t dot = vx * DX[k] + vy * DY[k];
				if (needFacing && dot <= 0) { continue; }
				if (best < 0 || dot > bestDot) { best = k; bestDot = dot; }
			}
			return best;
		};
		for (size_t i : comp)
		{
			bool farEnough = true;
			for (size_t e : entries)
			{
				const int ex = static_cast<int>(e % s_w), ey = static_cast<int>(e / s_w);
				const int x = static_cast<int>(i % s_w), y = static_cast<int>(i / s_w);
				if (std::max(std::abs(ex - x), std::abs(ey - y)) < ENTRY_SPACING) { farEnough = false; break; }
			}
			if (!farEnough) { continue; }
			const int k = bestSide(i, true);
			if (k < 0) { continue; }
			entry[i] |= static_cast<uint8_t>(BIT[k]);
			entries.push_back(i);
		}
		if (entries.empty())
		{
			for (size_t i : comp)
			{
				const int k = bestSide(i, false);
				if (k >= 0) { entry[i] |= static_cast<uint8_t>(BIT[k]); break; }
			}
		}
	}
	for (size_t i = 0; i < s_grid.size(); ++i)
	{
		if (owner[i] < 0) { continue; }
		const uint8_t open = static_cast<uint8_t>((conn[i] | entry[i]) & 15);
		s_grid[i] = static_cast<uint8_t>(0x10 | open);
		s_open[sid[i]] = open;
	}
}
} // namespace

void tick()
{
	const unsigned b = gameTime / REBUILD_MS;
	if (b == s_bucket) { return; }
	s_bucket = b;
	rebuild();
}

void reset()
{
	s_tableLoaded = false;
	s_family.clear();
	s_bucket = UINT32_MAX;
	s_grid.clear();
	s_open.clear();
	s_models.clear();
	s_w = s_h = 0;
}

uint8_t sides(int tileX, int tileY)
{
	if (s_grid.empty() || tileX < 0 || tileY < 0 || tileX >= s_w || tileY >= s_h) { return 0; }
	return s_grid[static_cast<size_t>(tileX) + static_cast<size_t>(tileY) * s_w];
}

void copySides(std::vector<uint8_t> &out)
{
	out.clear();
	if (s_grid.empty() || s_w != gameWorld.map.width || s_h != gameWorld.map.height || !surfaceResident()) { return; }
	out = s_grid;
}

bool edgeOpen(const std::vector<uint8_t> &sd, int width, int x0, int y0, int x1, int y1)
{
	const uint8_t a = sd[static_cast<size_t>(x0) + static_cast<size_t>(y0) * width];
	const uint8_t b = sd[static_cast<size_t>(x1) + static_cast<size_t>(y1) * width];
	if (a == 0 && b == 0) { return true; }
	const int dx = x1 - x0, dy = y1 - y0;
	if (dx != 0 && dy != 0) { return false; }   // nicht schraeg in eine Stellung hinein oder heraus
	const int bitA = dx > 0 ? 2 : dx < 0 ? 8 : dy < 0 ? 1 : 4;
	const int bitB = dx > 0 ? 8 : dx < 0 ? 2 : dy < 0 ? 4 : 1;
	if (a != 0 && !(a & bitA)) { return false; }
	if (b != 0 && !(b & bitB)) { return false; }
	return true;
}

bool isFort(const STRUCTURE *psStructure)
{
	return familyOf(psStructure) != nullptr;
}

const iIMDShape *displayModel(const STRUCTURE *psStructure)
{
	const std::string *key = familyOf(psStructure);
	if (key == nullptr) { return nullptr; }
	const auto it = s_open.find(psStructure->id);
	if (it == s_open.end()) { return nullptr; }
	char name[64];
	snprintf(name, sizeof(name), "%s_m%d.pie", key->c_str(), static_cast<int>(it->second));
	const auto c = s_models.find(name);
	if (c != s_models.end()) { return c->second; }
	const iIMDBaseShape *b = modelGet(WzString::fromUtf8(name));
	const iIMDShape *m = b != nullptr ? b->displayModel() : nullptr;
	s_models[name] = m;
	return m;
}

bool heavyCrosser(const DROID *psDroid)
{
	if (psDroid == nullptr) { return false; }
	const PROPULSION_STATS *p = psDroid->getPropulsionStats();
	const BODY_STATS *b = psDroid->getBodyStats();
	return p != nullptr && b != nullptr && p->propulsionType == PROPULSION_TYPE_TRACKED && b->size >= SIZE_MEDIUM;
}

bool fortTile(int tileX, int tileY)
{
	if (!tileOnMap(gameWorld.map, tileX, tileY)) { return false; }
	const MAPTILE *t = mapTile(gameWorld.map, tileX, tileY);
	if (t == nullptr || t->psObject == nullptr || t->psObject->type != OBJ_STRUCTURE) { return false; }
	return isFort(static_cast<const STRUCTURE *>(t->psObject));
}
} // namespace modfort
