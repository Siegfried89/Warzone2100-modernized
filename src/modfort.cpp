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

// Eine Stellung belegt S x S Felder (S = 1; Zweig viertel: 2 Feinfelder je Original-Kachel)
constexpr int S = DIST_TILE / TILE_UNITS;

struct Block
{
	int tx0, ty0, player;
	uint32_t id;
	uint8_t conn = 0, entry = 0;
};

void rebuild()
{
	s_w = gameWorld.map.width;
	s_h = gameWorld.map.height;
	s_grid.clear();
	s_open.clear();
	loadTable();
	if (s_family.empty() || s_w <= 0 || s_h <= 0 || !surfaceResident()) { return; }
	std::vector<Block> blocks;
	std::map<std::pair<int, int>, size_t> at;
	for (int p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || familyOf(s) == nullptr || modlayer::layerOf(s) != modlayer::SURFACE) { continue; }
			const int tx0 = map_coord(s->pos.x - (S * TILE_UNITS) / 2), ty0 = map_coord(s->pos.y - (S * TILE_UNITS) / 2);
			if (tx0 < 0 || ty0 < 0 || tx0 + S > s_w || ty0 + S > s_h) { continue; }
			if (at.count({tx0, ty0}) != 0) { continue; }
			at[{tx0, ty0}] = blocks.size();
			Block b;
			b.tx0 = tx0; b.ty0 = ty0; b.player = p; b.id = s->id;
			blocks.push_back(b);
		}
	}
	if (blocks.empty()) { return; }
	auto blockAt = [&](int tx0, int ty0) -> int {
		const auto it = at.find({tx0, ty0});
		return it == at.end() ? -1 : static_cast<int>(it->second);
	};
	for (Block &b : blocks)
	{
		for (int k = 0; k < 4; ++k)
		{
			const int n = blockAt(b.tx0 + S * DX[k], b.ty0 + S * DY[k]);
			if (n >= 0 && blocks[static_cast<size_t>(n)].player == b.player) { b.conn |= static_cast<uint8_t>(BIT[k]); }
		}
	}
	// zusammenhaengende Netze je Spieler; Oeffnungen zur eigenen Basis hin
	int64_t homeX[MAX_PLAYERS], homeY[MAX_PLAYERS];
	bool haveHome[MAX_PLAYERS];
	for (int p = 0; p < MAX_PLAYERS; ++p) { haveHome[p] = homeOf(p, homeX[p], homeY[p]); }
	std::vector<uint8_t> seen(blocks.size(), 0);
	std::vector<size_t> comp, stack;
	for (size_t start = 0; start < blocks.size(); ++start)
	{
		if (seen[start]) { continue; }
		const int p = blocks[start].player;
		comp.clear();
		stack.assign(1, start);
		seen[start] = 1;
		while (!stack.empty())
		{
			const size_t i = stack.back();
			stack.pop_back();
			comp.push_back(i);
			for (int k = 0; k < 4; ++k)
			{
				if (!(blocks[i].conn & BIT[k])) { continue; }
				const int j = blockAt(blocks[i].tx0 + S * DX[k], blocks[i].ty0 + S * DY[k]);
				if (j >= 0 && !seen[static_cast<size_t>(j)]) { seen[static_cast<size_t>(j)] = 1; stack.push_back(static_cast<size_t>(j)); }
			}
		}
		const int64_t hx = haveHome[p] ? homeX[p] : world_coord(s_w / 2), hy = haveHome[p] ? homeY[p] : world_coord(s_h / 2);
		auto centre = [&](size_t i, int64_t &cx, int64_t &cy) {
			cx = world_coord(blocks[i].tx0) + (S * TILE_UNITS) / 2;
			cy = world_coord(blocks[i].ty0) + (S * TILE_UNITS) / 2;
		};
		auto d2 = [&](size_t i) {
			int64_t cx, cy;
			centre(i, cx, cy);
			return (cx - hx) * (cx - hx) + (cy - hy) * (cy - hy);
		};
		std::sort(comp.begin(), comp.end(), [&](size_t a, size_t b) { const int64_t da = d2(a), db = d2(b); return da != db ? da < db : a < b; });
		auto usable = [&](const Block &b, int k) {
			for (int t = 0; t < S; ++t)
			{
				int x, y;
				switch (k)
				{
				case 0: x = b.tx0 + t; y = b.ty0 - 1; break;
				case 1: x = b.tx0 + S; y = b.ty0 + t; break;
				case 2: x = b.tx0 + t; y = b.ty0 + S; break;
				default: x = b.tx0 - 1; y = b.ty0 + t; break;
				}
				if (!sideUsable(x, y)) { return false; }
			}
			return true;
		};
		auto bestSide = [&](size_t i, bool needFacing) {
			int64_t cx, cy;
			centre(i, cx, cy);
			const int64_t vx = hx - cx, vy = hy - cy;
			int best = -1;
			int64_t bestDot = 0;
			for (int k = 0; k < 4; ++k)
			{
				if (blocks[i].conn & BIT[k]) { continue; }
				if (!usable(blocks[i], k)) { continue; }
				const int64_t dot = vx * DX[k] + vy * DY[k];
				if (needFacing && dot <= 0) { continue; }
				if (best < 0 || dot > bestDot) { best = k; bestDot = dot; }
			}
			return best;
		};
		std::vector<size_t> entries;
		for (size_t i : comp)
		{
			bool farEnough = true;
			for (size_t e : entries)
			{
				if (std::max(std::abs(blocks[e].tx0 - blocks[i].tx0), std::abs(blocks[e].ty0 - blocks[i].ty0)) < ENTRY_SPACING * S) { farEnough = false; break; }
			}
			if (!farEnough) { continue; }
			const int k = bestSide(i, true);
			if (k < 0) { continue; }
			blocks[i].entry |= static_cast<uint8_t>(BIT[k]);
			entries.push_back(i);
		}
		if (entries.empty())
		{
			for (size_t i : comp)
			{
				const int k = bestSide(i, false);
				if (k >= 0) { blocks[i].entry |= static_cast<uint8_t>(BIT[k]); break; }
			}
		}
	}
	// Raster fuer die Wegsuche: jedes Feld eines Blocks - innen offen, an den Blockkanten nur die offenen Seiten
	s_grid.assign(static_cast<size_t>(s_w) * static_cast<size_t>(s_h), 0);
	for (const Block &b : blocks)
	{
		const uint8_t open = static_cast<uint8_t>((b.conn | b.entry) & 15);
		s_open[b.id] = open;
		for (int j = 0; j < S; ++j)
		{
			for (int i = 0; i < S; ++i)
			{
				uint8_t m = 0x10;
				if (j > 0 || (open & 1)) { m |= 1; }
				if (i < S - 1 || (open & 2)) { m |= 2; }
				if (j < S - 1 || (open & 4)) { m |= 4; }
				if (i > 0 || (open & 8)) { m |= 8; }
				s_grid[static_cast<size_t>(b.tx0 + i) + static_cast<size_t>(b.ty0 + j) * s_w] = m;
			}
		}
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
