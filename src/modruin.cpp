// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Ruinen zerstoerter Bauwerke (siehe modruin.h)
#include "modruin.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "effects.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "structure.h"

namespace modruin
{
namespace
{
struct Pending
{
	Vector2i pos;
	Vector2i size;      // Grundflaeche in Feldern (gedreht)
	int statsIdx;
	uint16_t dir;
	int player;
};
struct Info
{
	int statsIdx = -1;   // altes Bauwerk (-1 = unbekannt, z. B. nach dem Laden)
	Vector2i pos{0, 0};
	uint16_t dir = 0;
	int player = -1;
	unsigned born = 0;
};
std::vector<Pending> s_pending;
std::map<uint32_t, Info> s_info;                       // Merkmal-Kennung -> altes Bauwerk (sortiert: feste Reihenfolge)
std::unordered_map<uint32_t, unsigned> s_nextSmoke;   // Anzeige: naechster Rauch je Ruine (Echtzeit)
constexpr unsigned SMOKE_MS = 240000;                  // 4 Minuten schwarzer Rauch
constexpr unsigned LIFE_MS = 900000;                   // 15 Minuten, dann weg

bool leavesRuin(const STRUCTURE *s)
{
	if (s->status != SS_BUILT || modlayer::layerOf(s) != modlayer::SURFACE) { return false; }
	const STRUCTURE_STATS *st = s->pStructureType;
	if (st->id.startsWith("A0Settle") || st->id.startsWith("A0Rail") || st->id.startsWith("A0Bridge") || st->id.startsWith("A0Road")
	    || st->id.contains(WzString::fromUtf8("Mine")) || st->passable) { return false; }
	switch (st->type)
	{
	case REF_FACTORY_MODULE: case REF_RESEARCH_MODULE: case REF_POWER_MODULE: case REF_RESOURCE_EXTRACTOR: case REF_DEMOLISH:
		return false;
	default:
		return true;
	}
}

FEATURE_STATS *ruinStats(Vector2i size)
{
	size.x = std::clamp(size.x, 1, 3);
	size.y = std::clamp(size.y, 1, 3);
	const WzString name = WzString::fromUtf8("ModRuin_" + std::to_string(size.x) + "x" + std::to_string(size.y));
	const int idx = getFeatureStatFromName(name);
	return idx >= 0 ? &asFeatureStats[idx] : nullptr;
}

const iIMDShape *blackSmoke()
{
	static const iIMDShape *shape = nullptr;
	static unsigned gen = 0;
	if (modelCacheStale(gen))
	{
		const iIMDBaseShape *b = modelGet(WzString::fromUtf8("fxsmokeblk.pie"));
		shape = b ? b->displayModel() : nullptr;
	}
	return shape;
}
}

bool isRuinStats(const FEATURE_STATS *s)
{
	return s != nullptr && s->id.startsWith("ModRuin_");
}

bool isRuinTile(const MAPTILE *t)
{
	if (t == nullptr || t->psObject == nullptr || t->psObject->type != OBJ_FEATURE) { return false; }
	return isRuinStats(static_cast<const FEATURE *>(t->psObject)->psStats);
}

void onDestroyed(const STRUCTURE *psDel)
{
	if (psDel == nullptr || !leavesRuin(psDel)) { return; }
	Pending p;
	p.pos = psDel->pos.xy();
	p.size = psDel->size();
	p.statsIdx = static_cast<int>(psDel->pStructureType - asStructureStats);
	p.dir = psDel->rot.direction;
	p.player = static_cast<int>(psDel->player);
	s_pending.push_back(p);
}

bool rebuildOrder(const DROID *psDroid, const BASE_OBJECT *psObj, DroidOrder &out)
{
	if (psDroid == nullptr || psObj == nullptr || psObj->type != OBJ_FEATURE || !psDroid->isConstructionDroid()) { return false; }
	if (!isRuinStats(static_cast<const FEATURE *>(psObj)->psStats)) { return false; }
	const auto it = s_info.find(psObj->id);
	if (it == s_info.end() || it->second.statsIdx < 0 || it->second.statsIdx >= static_cast<int>(numStructureStats)) { return false; }
	STRUCTURE_STATS *st = &asStructureStats[it->second.statsIdx];
	const ItemAvailability ia = static_cast<ItemAvailability>(apStructTypeLists[psDroid->player][it->second.statsIdx]);
	if (ia != AVAILABLE && ia != REDUNDANT) { return false; }
	out = DroidOrder(DORDER_BUILD, st, it->second.pos, it->second.dir);
	return true;
}

void clearSite(STRUCTURE_STATS *psStats, Vector2i pos, uint16_t direction)
{
	if (psStats == nullptr) { return; }
	const StructureBounds b = getStructureBounds(psStats, pos, direction);
	std::vector<FEATURE *> gone;
	for (int j = 0; j < b.size.y; ++j)
	{
		for (int i = 0; i < b.size.x; ++i)
		{
			if (!tileOnMap(gameWorld.map, b.map.x + i, b.map.y + j)) { continue; }
			MAPTILE *t = mapTile(gameWorld.map, b.map.x + i, b.map.y + j);
			if (isRuinTile(t))
			{
				FEATURE *f = static_cast<FEATURE *>(t->psObject);
				if (std::find(gone.begin(), gone.end(), f) == gone.end()) { gone.push_back(f); }
			}
		}
	}
	for (FEATURE *f : gone)
	{
		s_info.erase(f->id);
		removeFeature(f, gameWorld);
	}
}

void tick()
{
	if (!s_pending.empty())
	{
		std::vector<Pending> todo;
		todo.swap(s_pending);
		for (const Pending &p : todo)
		{
			FEATURE_STATS *fs = ruinStats(p.size);
			if (fs == nullptr) { continue; }
			// Feld frei? (Ruine nur dort, wo das Bauwerk stand und nichts Neues steht)
			const StructureBounds b = getStructureBounds(fs, p.pos);
			bool free = true;
			for (int j = 0; j < b.size.y && free; ++j)
			{
				for (int i = 0; i < b.size.x && free; ++i)
				{
					free = tileOnMap(gameWorld.map, b.map.x + i, b.map.y + j) && mapTile(gameWorld.map, b.map.x + i, b.map.y + j)->psObject == nullptr;
				}
			}
			if (!free) { continue; }
			FEATURE *f = buildFeature(gameWorld, fs, p.pos.x, p.pos.y, false);
			if (f == nullptr) { continue; }
			Info in;
			in.statsIdx = p.statsIdx; in.pos = p.pos; in.dir = p.dir; in.player = p.player; in.born = gameTime;
			s_info[f->id] = in;
		}
	}
	// Ablauf (15 Minuten) und Ruinen ohne Eintrag (aus Spielstaenden): Spielzustand, feste Reihenfolge
	std::vector<FEATURE *> expired;
	for (FEATURE *f : gameWorld.objects.features[0])
	{
		if (f->died || !isRuinStats(f->psStats)) { continue; }
		auto it = s_info.find(f->id);
		if (it == s_info.end()) { Info in; in.born = gameTime; it = s_info.emplace(f->id, in).first; }
		if (gameTime - it->second.born > LIFE_MS) { expired.push_back(f); }
	}
	for (FEATURE *f : expired)
	{
		s_info.erase(f->id);
		removeFeature(f, gameWorld);
	}
	// schwarzer Rauch der frischen Ruinen (nur Anzeige)
	const iIMDShape *smoke = blackSmoke();
	if (smoke == nullptr) { return; }
	for (const FEATURE *f : gameWorld.objects.features[0])
	{
		if (f->died || !isRuinStats(f->psStats) || !f->visibleForLocalDisplay()) { continue; }
		const auto it = s_info.find(f->id);
		if (it == s_info.end()) { continue; }
		const unsigned age = gameTime - it->second.born;
		if (age > SMOKE_MS) { continue; }
		unsigned &next = s_nextSmoke[f->id];
		if (realTime < next) { continue; }
		next = realTime + 120 + age / 900;   // am Anfang dicht, dann seltener
		const int spread = 40 + 30 * std::max(f->psStats->baseWidth, f->psStats->baseBreadth);
		Vector3i pos(f->pos.x + (rand() % (2 * spread)) - spread, f->pos.z + 40 + rand() % 30, f->pos.y + (rand() % (2 * spread)) - spread);
		addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_BILLOW, true, smoke, 0);
	}
	if (s_nextSmoke.size() > 1024) { s_nextSmoke.clear(); }
}

void reset()
{
	s_pending.clear();
	s_info.clear();
	s_nextSmoke.clear();
}
}
