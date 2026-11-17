// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: leere MG-Stellungen neu besetzen (siehe modnestcrew.h)

#include "modnestcrew.h"
#include "modcontrol.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/gamelib/gtime.h"

#include "console.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "mapgrid.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "random.h"
#include "structure.h"

#include <map>

namespace modnestcrew
{
namespace
{
constexpr unsigned TICK_MS = 1000;
constexpr unsigned BASE_RELIEF_MS = 180000;          // Ersatz aus der Basis nach 3 Minuten
constexpr int SEARCH_RANGE = 12 * DIST_TILE;
constexpr int ENTER_RANGE = DIST_TILE;               // so nah muss der Infanterist an die Stellung
constexpr unsigned RUNNER_TIMEOUT_MS = 40000;        // so lange darf der Laeufer brauchen, sonst wird ein anderer gesucht

struct Empty
{
	uint8_t player = 0;
	unsigned since = 0;
	uint32_t runnerId = 0;
	unsigned runnerSince = 0;
};
std::map<uint32_t, Empty> s_empty;    // Stellungs-ID -> ohne Schuetze
unsigned s_nextTick = 0;

bool isNest(const STRUCTURE *s)
{
	return s != nullptr && s->pStructureType != nullptr && s->status == SS_BUILT && s->pStructureType->id.toUtf8().compare(0, 12, "A0BaBaMGNest") == 0;
}

int64_t dist2(const Vector2i &a, const Vector2i &b)
{
	const int64_t dx = a.x - b.x, dy = a.y - b.y;
	return dx * dx + dy * dy;
}

bool freeInfantry(const DROID *d)
{
	return !d->died && d->droidType == DROID_PERSON && d->numWeaps > 0 && !d->isVtol()
	       && (d->order.type == DORDER_NONE || d->order.type == DORDER_GUARD) && !modcontrol::manual(d);
}
} // namespace

void onDamaged(STRUCTURE *s)
{
	if (!isNest(s) || s->died || s_empty.count(s->id) != 0) { return; }
	const unsigned maxBody = std::max<unsigned>(1, s->structureBody());
	const unsigned pct = s->body * 100 / maxBody;
	if (pct >= 60) { return; }
	if (pct >= 30 && gameRand(100) >= 40) { return; }
	Empty e;
	e.player = static_cast<uint8_t>(s->player);
	e.since = gameTime;
	s_empty[s->id] = e;
	if (s->player == selectedPlayer)
	{
		addConsoleMessage(_("MG nest: gunner down - nearby infantry will man it"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
}

bool unmanned(const STRUCTURE *s)
{
	return s != nullptr && !s_empty.empty() && s_empty.count(s->id) != 0;
}

void tick()
{
	if (s_empty.empty() || gameTime < s_nextTick) { return; }
	s_nextTick = gameTime + TICK_MS;
	for (auto it = s_empty.begin(); it != s_empty.end();)
	{
		Empty &e = it->second;
		STRUCTURE *s = IdToStruct(it->first, e.player);
		if (s == nullptr || s->died || s->player != e.player) { it = s_empty.erase(it); continue; }
		// Ersatz aus der Basis
		if (gameTime - e.since >= BASE_RELIEF_MS) { it = s_empty.erase(it); continue; }
		DROID *runner = e.runnerId != 0 ? IdToDroid(gameWorld.objects, e.runnerId, e.player) : nullptr;
		if (runner != nullptr && !runner->died)
		{
			if (dist2(runner->pos.xy(), s->pos.xy()) <= static_cast<int64_t>(ENTER_RANGE) * ENTER_RANGE)
			{
				// der Infanterist uebernimmt das MG (geht in der Stellung auf)
				vanishDroid(runner, gameWorld.objects);
				it = s_empty.erase(it);
				continue;
			}
			const bool stillGoing = runner->order.type == DORDER_MOVE && gameTime - e.runnerSince < RUNNER_TIMEOUT_MS && !modcontrol::manual(runner);
			if (stillGoing) { ++it; continue; }
			if (modcontrol::manual(runner)) { e.runnerId = 0; }   // der Spieler hat ihn anders eingesetzt
		}
		e.runnerId = 0;
		// naechster freier Infanterist des Besitzers
		DROID *best = nullptr;
		int64_t bestD = 0;
		for (BASE_OBJECT *o : gridStartIterate(s->pos.x, s->pos.y, SEARCH_RANGE))
		{
			if (o->type != OBJ_DROID || o->player != e.player) { continue; }
			DROID *d = static_cast<DROID *>(o);
			if (!freeInfantry(d)) { continue; }
			bool taken = false;
			for (const auto &kv : s_empty) { if (kv.second.runnerId == d->id) { taken = true; break; } }
			if (taken) { continue; }
			const int64_t dd = dist2(d->pos.xy(), s->pos.xy());
			if (best == nullptr || dd < bestD || (dd == bestD && d->id < best->id)) { best = d; bestD = dd; }
		}
		if (best != nullptr)
		{
			orderDroidLoc(best, DORDER_MOVE, static_cast<UDWORD>(s->pos.x), static_cast<UDWORD>(s->pos.y), ModeImmediate);
			e.runnerId = best->id;
			e.runnerSince = gameTime;
		}
		++it;
	}
}

void reset()
{
	s_empty.clear();
	s_nextTick = 0;
}
} // namespace modnestcrew
