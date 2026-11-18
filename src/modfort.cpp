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
bool heavyCrosser(const DROID *psDroid);
bool isFort(const STRUCTURE *psStructure);
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
	int faceSide = 2;   // (18.11.) Seite, in die das Bauwerk beim Platzieren gedreht wurde (0 N, 1 O, 2 S, 3 W)
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
			{
				// Richtung 0 = Sueden (+y), 90 Grad = Osten ... (wie das Ziehen beim Platzieren)
				const uint16_t d = static_cast<uint16_t>(s->rot.direction + 0x2000);
				static const int SIDE_OF_QUADRANT[4] = {2, 1, 0, 3};
				b.faceSide = SIDE_OF_QUADRANT[(d >> 14) & 3];
			}
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
		// (Wunsch 18.11. 12:42 "Richtungs-Ziehen funktioniert noch nicht mit allen Stellungen"): eine einzelne Stellung (ohne Nachbarn)
		// bekommt ihre Oeffnung auf der Seite, in die sie beim Platzieren gezogen wurde
		if (comp.size() == 1)
		{
			Block &b1 = blocks[comp[0]];
			if (usable(b1, b1.faceSide))
			{
				b1.entry |= static_cast<uint8_t>(BIT[b1.faceSide]);
				continue;
			}
		}
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

// MOD (Wunsch 17.11. 13:58): "... und es auch zerstoeren mit allen Maennern darin (quasi Ueberfahren)". Faehrt ein schwerer
// Kettenpanzer auf eine gegnerische Stellung, wird sie samt der gegnerischen Infanterie darin zerstoert. Eigene und verbuendete
// Stellungen ueberfaehrt er nur (sie bleiben stehen). Spiellogik im Spielschritt in fester Reihenfolge (auf allen Rechnern gleich).
void crushTick()
{
	if (s_family.empty() || !surfaceResident()) { return; }
	std::vector<STRUCTURE *> victims;
	std::vector<int> crushers;
	for (int p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d->died || !heavyCrosser(d) || modlayer::layerOf(d) != modlayer::SURFACE) { continue; }
			const int tx = map_coord(d->pos.x), ty = map_coord(d->pos.y);
			if (!tileOnMap(gameWorld.map, tx, ty)) { continue; }
			MAPTILE *t = mapTile(gameWorld.map, tx, ty);
			if (t == nullptr || t->psObject == nullptr || t->psObject->type != OBJ_STRUCTURE) { continue; }
			STRUCTURE *st = static_cast<STRUCTURE *>(t->psObject);
			if (st->died || !isFort(st) || st->player == d->player || aiCheckAlliances(st->player, d->player)) { continue; }
			if (std::find(victims.begin(), victims.end(), st) != victims.end()) { continue; }
			victims.push_back(st);
			crushers.push_back(p);
		}
	}
	for (size_t v = 0; v < victims.size(); ++v)
	{
		STRUCTURE *st = victims[v];
		if (st->died) { continue; }
		const int crusher = crushers[v];
		// Infanterie in der Stellung (alle Felder der Grundflaeche), die nicht zum Panzer gehoert
		const int tx0 = map_coord(st->pos.x - (S * TILE_UNITS) / 2), ty0 = map_coord(st->pos.y - (S * TILE_UNITS) / 2);
		std::vector<DROID *> men;
		for (int p = 0; p < MAX_PLAYERS; ++p)
		{
			if (p == crusher || aiCheckAlliances(p, crusher)) { continue; }
			for (DROID *d : gameWorld.objects.droids[p])
			{
				if (d->died || d->droidType == DROID_SUPERTRANSPORTER || d->droidType == DROID_TRANSPORTER) { continue; }
				const PROPULSION_STATS *pr = d->getPropulsionStats();
				if (pr == nullptr || pr->propulsionType != PROPULSION_TYPE_LEGGED) { continue; }
				const int x = map_coord(d->pos.x), y = map_coord(d->pos.y);
				if (x >= tx0 && x < tx0 + S && y >= ty0 && y < ty0 + S) { men.push_back(d); }
			}
		}
		for (DROID *d : men)
		{
			if (!d->died) { destroyDroid(d, gameTime, gameWorld); }
		}
		destroyStruct(st, gameTime, gameWorld);
	}
}
} // namespace

void tick()
{
	crushTick();
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

/// (Wunsch 18.11. 12:49 "Vorschau und Ergebnis passen nicht - rund wird zu eckig ..."): Bauplan beim Platzieren zeigt dasselbe Modell wie
/// das fertige Bauwerk - Gaenge zu angrenzenden eigenen Stellungen, sonst die Oeffnung zur gezogenen Seite
uint8_t predictOpen(const STRUCTURE *bp)
{
	const int tx0 = map_coord(bp->pos.x - (S * TILE_UNITS) / 2), ty0 = map_coord(bp->pos.y - (S * TILE_UNITS) / 2);
	uint8_t conn = 0;
	for (int k = 0; k < 4; ++k)
	{
		const int nx = tx0 + S * DX[k], ny = ty0 + S * DY[k];
		if (!tileOnMap(gameWorld.map, nx, ny)) { continue; }
		const MAPTILE *t = mapTile(gameWorld.map, nx, ny);
		if (t == nullptr || t->psObject == nullptr || t->psObject->type != OBJ_STRUCTURE) { continue; }
		const STRUCTURE *n = static_cast<const STRUCTURE *>(t->psObject);
		if (n->died || familyOf(n) == nullptr || n->player != bp->player) { continue; }
		conn |= static_cast<uint8_t>(BIT[k]);
	}
	if (conn != 0) { return conn; }
	const uint16_t d = static_cast<uint16_t>(bp->rot.direction + 0x2000);
	static const int SIDE_OF_QUADRANT[4] = {2, 1, 0, 3};
	return static_cast<uint8_t>(BIT[SIDE_OF_QUADRANT[(d >> 14) & 3]]);
}

const iIMDShape *displayModel(const STRUCTURE *psStructure)
{
	const std::string *key = familyOf(psStructure);
	if (key == nullptr) { return nullptr; }
	uint8_t open = 0;
	if (psStructure->isBlueprint())
	{
		// Graeben werden als Linie gezogen: deren Bauplan richtet sich wie Mauern nach den Nachbar-Bauplaenen aus (Standardmodelle)
		const STRUCTURE_TYPE t = psStructure->pStructureType->type;
		if (t == REF_WALL || t == REF_WALLCORNER || t == REF_GATE) { return nullptr; }
		if (!surfaceResident() || modlayer::layerOf(psStructure) != modlayer::SURFACE) { return nullptr; }
		open = predictOpen(psStructure);
	}
	else
	{
		const auto it = s_open.find(psStructure->id);
		if (it == s_open.end()) { return nullptr; }
		open = it->second;
	}
	char name[64];
	static unsigned cacheGen = 0;
	if (modelCacheStale(cacheGen)) { s_models.clear(); }   // MOD (18.11.): Modelle wurden neu geladen
	snprintf(name, sizeof(name), "%s_m%d.pie", key->c_str(), static_cast<int>(open));
	const auto c = s_models.find(name);
	if (c != s_models.end()) { return c->second; }
	const iIMDBaseShape *b = modelGet(WzString::fromUtf8(name));
	const iIMDShape *m = b != nullptr ? b->displayModel() : nullptr;
	s_models[name] = m;
	return m;
}

bool isDeep(const STRUCTURE *psStructure)
{
	const std::string *key = familyOf(psStructure);
	if (key == nullptr) { return false; }
	return key->rfind("trd", 0) == 0 || (key->size() > 2 && key->compare(key->size() - 2, 2, "_d") == 0);
}

int displayHeight(const STRUCTURE *psStructure, int fallback)
{
	if (psStructure == nullptr || psStructure->status != SS_BUILT || !isDeep(psStructure)) { return fallback; }
	const int half = DIST_TILE / 2;
	const int x0 = map_coord(psStructure->pos.x - half), y0 = map_coord(psStructure->pos.y - half);
	const int x1 = map_coord(psStructure->pos.x + half), y1 = map_coord(psStructure->pos.y + half);
	if (x0 < 0 || y0 < 0 || x1 > gameWorld.map.width || y1 > gameWorld.map.height) { return fallback; }
	// Eckpunkthoehen ohne Krater-/Grabenabsenkung (map_TileHeight liest die Kartenhoehe)
	const int h = map_TileHeight(gameWorld.map, x0, y0) + map_TileHeight(gameWorld.map, x1, y0) + map_TileHeight(gameWorld.map, x0, y1)
	              + map_TileHeight(gameWorld.map, x1, y1);
	return (h + 2) / 4;
}

namespace
{
struct Region
{
	int core = 0, arm = 0, half = 0;
	uint8_t open = 0;
};

bool inRegion(const Region &r, int u, int v)
{
	if (std::abs(u) <= r.core && std::abs(v) <= r.core) { return true; }
	if ((r.open & 1) && std::abs(u) <= r.arm && v <= 0 && v >= -r.half) { return true; }
	if ((r.open & 2) && std::abs(v) <= r.arm && u >= 0 && u <= r.half) { return true; }
	if ((r.open & 4) && std::abs(u) <= r.arm && v >= 0 && v <= r.half) { return true; }
	if ((r.open & 8) && std::abs(v) <= r.arm && u <= 0 && u >= -r.half) { return true; }
	return false;
}

void nearestIn(const Region &r, int u, int v, int &ou, int &ov)
{
	int64_t best = INT64_MAX;
	auto tryRect = [&](int u0, int u1, int v0, int v1) {
		const int cu = std::clamp(u, u0, u1), cv = std::clamp(v, v0, v1);
		const int64_t d = int64_t(cu - u) * (cu - u) + int64_t(cv - v) * (cv - v);
		if (d < best) { best = d; ou = cu; ov = cv; }
	};
	tryRect(-r.core, r.core, -r.core, r.core);
	if (r.open & 1) { tryRect(-r.arm, r.arm, -r.half, 0); }
	if (r.open & 2) { tryRect(0, r.half, -r.arm, r.arm); }
	if (r.open & 4) { tryRect(-r.arm, r.arm, 0, r.half); }
	if (r.open & 8) { tryRect(-r.half, 0, -r.arm, r.arm); }
}

const STRUCTURE *fortAt(int worldX, int worldY)
{
	const int tx = map_coord(worldX), ty = map_coord(worldY);
	if (!tileOnMap(gameWorld.map, tx, ty)) { return nullptr; }
	const MAPTILE *t = mapTile(gameWorld.map, tx, ty);
	if (t == nullptr || t->psObject == nullptr || t->psObject->type != OBJ_STRUCTURE) { return nullptr; }
	const STRUCTURE *s = static_cast<const STRUCTURE *>(t->psObject);
	if (s->died || s->status != SS_BUILT || !isFort(s)) { return nullptr; }
	return s;
}

bool regionOf(const STRUCTURE *s, Region &r)
{
	const auto it = s_open.find(s->id);
	if (it == s_open.end()) { return false; }   // altes Einzelmodell (nicht im Netz): keine Wandmasse bekannt
	const std::string *key = familyOf(s);
	if (key == nullptr) { return false; }
	const bool pit = key->rfind("pit", 0) == 0;
	// lichte Masse aus trench.py: Grabengang 40 breit (HI 20), Grube 76 (PIT_HI 38); abzueglich ~4-5 fuer den Koerper des Soldaten.
	// Die Deckungsplaetze (modcover::slotPos) liegen hoechstens 16 seitlich bzw. 32 laengs - sie bleiben damit erreichbar.
	r.core = pit ? 33 : 16;
	r.arm = 16;
	r.half = (S * TILE_UNITS) / 2 + 2;
	r.open = it->second & 0x0F;
	return true;
}
} // namespace

bool clampInfantry(const DROID *psDroid, int oldX, int oldY, int &newX, int &newY)
{
	if (psDroid == nullptr || s_grid.empty() || !surfaceResident() || modlayer::layerOf(psDroid) != modlayer::SURFACE) { return false; }
	const STRUCTURE *s = fortAt(newX, newY);
	if (s == nullptr) { return false; }
	Region r;
	if (!regionOf(s, r)) { return false; }
	const int u = newX - s->pos.x, v = newY - s->pos.y;
	if (inRegion(r, u, v)) { return false; }
	const int ou0 = oldX - s->pos.x, ov0 = oldY - s->pos.y;
	const bool oldInBlock = std::abs(ou0) <= r.half && std::abs(ov0) <= r.half;
	if (oldInBlock)
	{
		// innen (oder in einer Wand gestrandet): am naechsten freien Punkt bleiben - gleitet an der Wand entlang
		int ou = 0, ov = 0;
		nearestIn(r, u, v, ou, ov);
		newX = s->pos.x + ou;
		newY = s->pos.y + ov;
		return true;
	}
	// von aussen gegen eine geschlossene Wand: an ihr entlang gleiten (nur eine Achse), sonst stehen bleiben
	const int cand[2][2] = {{newX, oldY}, {oldX, newY}};
	for (const auto &c : cand)
	{
		const STRUCTURE *cs = fortAt(c[0], c[1]);
		if (cs == nullptr) { newX = c[0]; newY = c[1]; return true; }
		Region cr;
		if (!regionOf(cs, cr) || inRegion(cr, c[0] - cs->pos.x, c[1] - cs->pos.y)) { newX = c[0]; newY = c[1]; return true; }
	}
	newX = oldX;
	newY = oldY;
	return true;
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
