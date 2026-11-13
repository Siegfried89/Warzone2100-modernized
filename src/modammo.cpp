// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Munition - siehe modammo.h
#include "modammo.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modcombot.h"
#include "multiplay.h"
#include "statsdef.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace modammo
{
namespace
{
constexpr int SEGMENTS = 5;
constexpr unsigned TICK_MS = 500;
constexpr int SUPPLY_RADIUS = 5 * TILE_UNITS;

struct State
{
	int left = -1;    // verbleibende Schuesse; -1 = noch nicht angelegt (voll)
};

std::unordered_map<uint32_t, State> s_state;
unsigned s_nextTick = 0;

int capacityOf(const WEAPON_STATS *ws)
{
	if (ws == nullptr) { return 0; }
	switch (ws->weaponSubClass)
	{
	case WSC_MGUN: return 220;
	case WSC_CANNON: return 40;
	case WSC_MORTARS: return 28;
	case WSC_ROCKET: return 24;
	case WSC_MISSILE: return 12;
	case WSC_HOWITZERS: return 18;
	case WSC_SLOWROCKET:
	case WSC_SLOWMISSILE: return 10;
	case WSC_FLAME: return 120;
	case WSC_AAGUN: return 120;
	case WSC_BOMB: return 4;
	default: return 0;
	}
}

int capacity(const DROID *d)
{
	if (d == nullptr || d->numWeaps == 0 || d->asWeaps[0].nStat == 0) { return 0; }
	return capacityOf(d->getWeaponStats(0));
}

/// Nachschub-Rate der naechsten Quelle: Anteil des Vorrats je Sekunde (0 = keine Quelle in der Naehe)
float supplyRate(const DROID *d)
{
	float best = 0.f;
	for (const STRUCTURE *s : gameWorld.objects.structures[d->player])
	{
		if (s == nullptr || s->died || s->status != SS_BUILT || s->pStructureType == nullptr) { continue; }
		const int dx = s->pos.x - d->pos.x, dy = s->pos.y - d->pos.y;
		if (dx * dx + dy * dy > SUPPLY_RADIUS * SUPPLY_RADIUS) { continue; }
		const std::string id = s->pStructureType->id.toUtf8();
		float rate = 0.f;
		if (id.find("AmmoBox") != std::string::npos) { rate = 1.f / 8.f; }
		else if (id.find("Outpost") != std::string::npos) { rate = 1.f / 25.f; }
		else
		{
			switch (s->pStructureType->type)
			{
			case REF_FACTORY:
			case REF_CYBORG_FACTORY:
			case REF_VTOL_FACTORY:
			case REF_HQ:
			case REF_REPAIR_FACILITY:
				rate = 1.f / 25.f;
				break;
			default:
				break;
			}
		}
		best = std::max(best, rate);
	}
	return best;
}
} // namespace

bool usesAmmo(const DROID *d)
{
	if (d == nullptr || d->died) { return false; }
	if (!isHumanPlayer(d->player)) { return false; }
	if (modcombot::isModular(d)) { return false; }
	return capacity(d) > 0;
}

int segments(const DROID *d)
{
	if (!usesAmmo(d)) { return -1; }
	const int cap = capacity(d);
	const auto it = s_state.find(d->id);
	const int left = (it == s_state.end() || it->second.left < 0) ? cap : std::min(it->second.left, cap);
	if (left <= 0) { return 0; }
	return std::min(SEGMENTS, (left * SEGMENTS + cap - 1) / cap);   // angefangene Abschnitte zaehlen voll
}

unsigned firePauseFactor(const DROID *d)
{
	return segments(d) == 0 ? 10u : 1u;
}

void onShot(const DROID *d)
{
	if (!usesAmmo(d)) { return; }
	State &st = s_state[d->id];
	const int cap = capacity(d);
	if (st.left < 0) { st.left = cap; }
	if (st.left > 0) { st.left--; }
}

void tick(GameWorld &world)
{
	if (gameTime < s_nextTick) { return; }
	s_nextTick = gameTime + TICK_MS;
	if (s_state.empty()) { return; }
	std::unordered_map<uint32_t, bool> seen;
	for (unsigned p = 0; p < MAX_PLAYERS; p++)
	{
		if (!isHumanPlayer(p)) { continue; }
		for (const DROID *d : world.objects.droids[p])
		{
			if (d == nullptr || d->died) { continue; }
			const auto it = s_state.find(d->id);
			if (it == s_state.end()) { continue; }
			seen[d->id] = true;
			const int cap = capacity(d);
			if (cap <= 0) { continue; }
			int &left = it->second.left;
			if (left < 0) { continue; }
			if (left >= cap) { left = -1; continue; }
			const float rate = supplyRate(d);
			if (rate > 0.f)
			{
				const float add = rate * static_cast<float>(cap) * (static_cast<float>(TICK_MS) / 1000.f);
				left = std::min(cap, left + std::max(1, static_cast<int>(add + 0.5f)));
			}
		}
	}
	for (auto it = s_state.begin(); it != s_state.end();)
	{
		it = seen.count(it->first) ? std::next(it) : s_state.erase(it);
	}
}

void reset()
{
	s_state.clear();
	s_nextTick = 0;
}
}
