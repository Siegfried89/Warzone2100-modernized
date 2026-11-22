// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Ruinen zerstoerter Gebaeude (siehe modruin.h)
#include "modruin.h"

#include <unordered_map>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "effects.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "multiplay.h"
#include "objmem.h"
#include "structure.h"

namespace modruin
{
namespace
{
struct Pending
{
	Vector2i pos;
	uint16_t dir;
	int owner;
};
std::vector<Pending> s_pending;
std::unordered_map<uint32_t, unsigned> s_nextSmoke;   // Anzeige: naechster Rauch je Ruine (Echtzeit)
constexpr unsigned SMOKE_MS = 240000;                  // 4 Minuten schwarzer Rauch

bool leavesRuin(const STRUCTURE *s)
{
	if (s->status != SS_BUILT || modlayer::layerOf(s) != modlayer::SURFACE) { return false; }
	const STRUCTURE_STATS *st = s->pStructureType;
	if (st->id.startsWith("A0BuildingRuin") || st->id.startsWith("A0Settle") || st->id.startsWith("A0Rail") || st->id.startsWith("A0Bridge")
	    || st->id.startsWith("A0Road")) { return false; }
	const Vector2i size = s->size();
	switch (st->type)
	{
	case REF_HQ: case REF_FACTORY: case REF_CYBORG_FACTORY: case REF_VTOL_FACTORY: case REF_RESEARCH: case REF_POWER_GEN:
	case REF_REPAIR_FACILITY: case REF_COMMAND_CONTROL: case REF_SAT_UPLINK: case REF_LASSAT:
		return size.x * size.y >= 2;
	case REF_GENERIC:
		return size.x * size.y >= 2;
	default:
		return false;
	}
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

bool isRuin(const STRUCTURE *s)
{
	return s != nullptr && s->pStructureType != nullptr && s->pStructureType->id.startsWith("A0BuildingRuin");
}

void onDestroyed(const STRUCTURE *psDel)
{
	if (psDel == nullptr || !leavesRuin(psDel)) { return; }
	const Vector2i size = psDel->size();
	Pending p;
	p.pos = psDel->pos.xy();
	p.dir = size.x >= 2 ? 0 : DEG(90);
	p.owner = bMultiPlayer ? static_cast<int>(psDel->player) : 0;
	s_pending.push_back(p);
}

void tick()
{
	if (!s_pending.empty())
	{
		const int idx = getStructStatFromName(WzString::fromUtf8("A0BuildingRuin"));
		std::vector<Pending> todo;
		todo.swap(s_pending);
		for (const Pending &p : todo)
		{
			if (idx < 0 || p.owner < 0 || p.owner >= MAX_PLAYERS) { continue; }
			STRUCTURE_STATS *st = &asStructureStats[idx];
			if (!validLocation(gameWorld, st, p.pos, p.dir, static_cast<unsigned>(p.owner), false)) { continue; }
			STRUCTURE *r = buildStructureDir(gameWorld, st, p.pos.x, p.pos.y, p.dir, static_cast<UDWORD>(p.owner), true);
			if (r == nullptr) { continue; }
			r->status = SS_BUILT;
			buildingComplete(r, gameWorld);
		}
	}
	// schwarzer Rauch der frischen Ruinen (nur Anzeige)
	const iIMDShape *smoke = blackSmoke();
	if (smoke == nullptr) { return; }
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || !isRuin(s) || gameTime - s->born > SMOKE_MS || !s->visibleForLocalDisplay()) { continue; }
			unsigned &next = s_nextSmoke[s->id];
			if (realTime < next) { continue; }
			// am Anfang dichter, dann seltener
			const unsigned age = gameTime - s->born;
			next = realTime + 120 + age / 900;
			Vector3i pos(s->pos.x + (rand() % 120) - 60, s->pos.z + 40 + rand() % 30, s->pos.y + (rand() % 80) - 40);
			addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_BILLOW, true, smoke, 0);
		}
	}
	if (s_nextSmoke.size() > 512) { s_nextSmoke.clear(); }
}

void reset()
{
	s_pending.clear();
	s_nextSmoke.clear();
}
}
