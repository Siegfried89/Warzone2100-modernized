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
#include "order.h"
#include "statsdef.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cmath>
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
constexpr int DEPOT_RADIUS = 6 * TILE_UNITS;
constexpr unsigned RESUPPLY_TIMEOUT_MS = 300000;   // nach 5 Minuten ohne Erfolg gibt die Einheit die Fahrt auf

struct State
{
	int left = -1;    // verbleibende Schuesse; -1 = noch nicht angelegt (voll)
	// Selbstversorgung: leere Einheiten fahren von selbst zum naechsten Munitionsdepot und danach zurueck an die Stelle, wo ihnen die Munition ausging
	bool resupply = false;
	int originX = 0, originY = 0, goalX = 0, goalY = 0;
	unsigned since = 0, nextAllowed = 0;
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

/// Nachschub-Rate einer Quelle: Anteil des Vorrats je Sekunde (0 = keine Munitionsquelle)
float rateOf(const STRUCTURE *s)
{
	if (s == nullptr || s->died || s->status != SS_BUILT || s->pStructureType == nullptr) { return 0.f; }
	const std::string id = s->pStructureType->id.toUtf8();
	if (id.find("AmmoDepot") != std::string::npos) { return 1.f / 5.f; }
	if (id.find("AmmoBox") != std::string::npos) { return 1.f / 8.f; }
	if (id.find("Outpost") != std::string::npos) { return 1.f / 25.f; }
	switch (s->pStructureType->type)
	{
	case REF_FACTORY:
	case REF_CYBORG_FACTORY:
	case REF_VTOL_FACTORY:
	case REF_HQ:
	case REF_REPAIR_FACILITY:
		return 1.f / 25.f;
	default:
		return 0.f;
	}
}

int radiusOf(const STRUCTURE *s)
{
	return s->pStructureType->id.toUtf8().find("AmmoDepot") != std::string::npos ? DEPOT_RADIUS : SUPPLY_RADIUS;
}

/// Nachschub-Rate der naechsten Quelle (0 = keine Quelle in der Naehe)
float supplyRate(const DROID *d)
{
	float best = 0.f;
	for (const STRUCTURE *s : gameWorld.objects.structures[d->player])
	{
		const float rate = rateOf(s);
		if (rate <= 0.f) { continue; }
		const int dx = s->pos.x - d->pos.x, dy = s->pos.y - d->pos.y, r = radiusOf(s);
		if (dx * dx + dy * dy > r * r) { continue; }
		best = std::max(best, rate);
	}
	return best;
}

/// naechste Munitionsquelle (Depots und Kisten gelten als naeher als gleich weit entfernte Fabriken: Entfernung / Rate-Gewicht)
const STRUCTURE *nearestSource(const DROID *d)
{
	const STRUCTURE *best = nullptr;
	float bestScore = 1e30f;
	for (const STRUCTURE *s : gameWorld.objects.structures[d->player])
	{
		const float rate = rateOf(s);
		if (rate <= 0.f) { continue; }
		const float dx = static_cast<float>(s->pos.x - d->pos.x), dy = static_cast<float>(s->pos.y - d->pos.y);
		const float score = std::sqrt(dx * dx + dy * dy) * (rate >= 0.1f ? 0.7f : 1.f);
		if (score < bestScore) { bestScore = score; best = s; }
	}
	return best;
}

bool mayAutoResupply(const DROID *d)
{
	if (d->isVtol() || d->isTransporter()) { return false; }
	switch (d->order.type)
	{
	case DORDER_RTR:
	case DORDER_RTB:
	case DORDER_RECYCLE:
	case DORDER_EMBARK:
	case DORDER_DISEMBARK:
	case DORDER_BUILD:
	case DORDER_LINEBUILD:
	case DORDER_FORTIFY:
		return false;
	default:
		return true;
	}
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
			State &st = it->second;
			int &left = st.left;
			// Selbstversorgung: Rueckfahrt an die Stelle, wo die Munition ausging, sobald wieder alles voll ist
			if (st.resupply)
			{
				const bool full = left < 0 || left >= cap;
				const bool timedOut = gameTime - st.since > RESUPPLY_TIMEOUT_MS;
				bool taken = d->order.type != DORDER_MOVE && d->order.type != DORDER_NONE;   // Spieler hat der Einheit etwas anderes befohlen
				if (d->order.type == DORDER_MOVE)
				{
					const int ox = d->order.pos.x - st.goalX, oy = d->order.pos.y - st.goalY;
					if (ox * ox + oy * oy > 2 * TILE_UNITS * 2 * TILE_UNITS) { taken = true; }    // anderes Marschziel
				}
				if (full || timedOut || taken)
				{
					st.resupply = false;
					if (taken || timedOut) { st.nextAllowed = gameTime + 30000; }
					else
					{
						const int dx = st.originX - d->pos.x, dy = st.originY - d->pos.y;
						if (dx * dx + dy * dy > 6 * TILE_UNITS * 6 * TILE_UNITS)
						{
							orderDroidLoc(const_cast<DROID *>(d), DORDER_SCOUT, st.originX, st.originY, ModeImmediate);
						}
					}
				}
			}
			else if (left == 0 && gameTime >= st.nextAllowed && mayAutoResupply(d))
			{
				const STRUCTURE *src = nearestSource(d);
				if (src != nullptr)
				{
					const int dx = src->pos.x - d->pos.x, dy = src->pos.y - d->pos.y, r = radiusOf(src);
					if (dx * dx + dy * dy > r * r)
					{
						// Zielpunkt: 3 Felder vor dem Depot, auf der Seite der Einheit
						const float len = std::max(1.f, std::sqrt(static_cast<float>(dx) * dx + static_cast<float>(dy) * dy));
						st.goalX = src->pos.x - static_cast<int>(static_cast<float>(dx) / len * 3.f * TILE_UNITS);
						st.goalY = src->pos.y - static_cast<int>(static_cast<float>(dy) / len * 3.f * TILE_UNITS);
						st.originX = d->pos.x; st.originY = d->pos.y;
						st.since = gameTime;
						st.resupply = true;
						orderDroidLoc(const_cast<DROID *>(d), DORDER_MOVE, st.goalX, st.goalY, ModeImmediate);
					}
				}
			}
			if (left < 0) { continue; }
			if (left >= cap) { if (!st.resupply) { left = -1; } continue; }
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
