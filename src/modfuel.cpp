// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Treibstoff fuer Fahrzeuge - siehe modfuel.h
#include "modcontrol.h"
#include "modfuel.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
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
#include <string>
#include <unordered_map>

namespace modfuel
{
namespace
{
constexpr unsigned TICK_MS = 500;
constexpr int FUEL_RANGE = 480 * TILE_UNITS;            // "recht lange": ein voller Tank reicht fuer 480 Kacheln Fahrweg
constexpr int MIN_SPEED_PERCENT = 15;                   // leerer Tank: Mindesttempo
constexpr int STATION_RADIUS = 3 * TILE_UNITS;          // in diesem Umkreis um die Tankstelle wird getankt
constexpr int REFUEL_PER_TICK = FUEL_RANGE / 16;        // voll in 8 Sekunden
constexpr unsigned TRIP_TIMEOUT_MS = 300000;            // nach 5 Minuten ohne Erfolg gibt die Einheit die Fahrt auf

struct State
{
	int fuel = FUEL_RANGE;        // verbleibender Fahrweg (Weltmasseinheiten)
	int lastX = 0, lastY = 0;
	bool havePos = false;
	bool trip = false;            // faehrt gerade selbst zur Tankstelle
	int originX = 0, originY = 0, goalX = 0, goalY = 0;
	unsigned since = 0, nextAllowed = 0;
};

std::unordered_map<uint32_t, State> s_state;
unsigned s_nextTick = 0;

bool stationRaw(const STRUCTURE *s)
{
	if (s == nullptr || s->died || s->status != SS_BUILT || s->pStructureType == nullptr) { return false; }
	if (s->pStructureType->type != REF_REPAIR_FACILITY) { return false; }
	return s->pStructureType->id.toUtf8().find("Gantry") == std::string::npos;   // Combot-Reparaturgeruest ist keine Tankstelle
}

const STRUCTURE *stationNear(const DROID *d)
{
	for (const STRUCTURE *s : gameWorld.objects.structures[d->player])
	{
		if (!stationRaw(s)) { continue; }
		const int dx = s->pos.x - d->pos.x, dy = s->pos.y - d->pos.y;
		if (dx * dx + dy * dy <= STATION_RADIUS * STATION_RADIUS) { return s; }
	}
	return nullptr;
}

const STRUCTURE *nearestStation(const DROID *d)
{
	const STRUCTURE *best = nullptr;
	int64_t bestD = INT64_MAX;
	for (const STRUCTURE *s : gameWorld.objects.structures[d->player])
	{
		if (!stationRaw(s)) { continue; }
		const int64_t dx = s->pos.x - d->pos.x, dy = s->pos.y - d->pos.y;
		const int64_t dist = dx * dx + dy * dy;
		if (dist < bestD) { bestD = dist; best = s; }
	}
	return best;
}

bool mayAutoTrip(const DROID *d)
{
	switch (d->order.type)
	{
	case DORDER_RTR:
	case DORDER_RTB:
	case DORDER_RECYCLE:
	case DORDER_EMBARK:
	case DORDER_DISEMBARK:
		return false;
	default:
		return true;
	}
}
} // namespace

bool isStation(const STRUCTURE *s)
{
	return stationRaw(s);
}

bool usesFuel(const DROID *d)
{
	if (d == nullptr || d->died) { return false; }
	if (!isHumanPlayer(d->player)) { return false; }
	if (d->isVtol() || d->isTransporter()) { return false; }
	switch (d->droidType)
	{
	case DROID_PERSON:
	case DROID_CYBORG:
	case DROID_CYBORG_SUPER:
	case DROID_CYBORG_CONSTRUCT:
	case DROID_CYBORG_REPAIR:
		return false;
	default:
		break;
	}
	if (modcombot::isModular(d)) { return false; }
	const PROPULSION_STATS *p = d->getPropulsionStats();
	if (p == nullptr || p->propulsionType == PROPULSION_TYPE_LEGGED || p->propulsionType == PROPULSION_TYPE_LIFT || p->alwaysFlying) { return false; }
	return true;
}

int permille(const DROID *d)
{
	if (!usesFuel(d)) { return -1; }
	const auto it = s_state.find(d->id);
	const int fuel = it == s_state.end() ? FUEL_RANGE : it->second.fuel;
	return std::clamp(static_cast<int>(static_cast<int64_t>(fuel) * 1000 / FUEL_RANGE), 0, 1000);
}

int speedPercent(const DROID *d)
{
	if (s_state.empty()) { return 100; }
	const auto it = s_state.find(d->id);
	if (it == s_state.end() || it->second.fuel > 0) { return 100; }
	return usesFuel(d) ? MIN_SPEED_PERCENT : 100;
}

void tick(GameWorld &world)
{
	if (gameTime < s_nextTick) { return; }
	s_nextTick = gameTime + TICK_MS;
	std::unordered_map<uint32_t, bool> seen;
	for (unsigned p = 0; p < MAX_PLAYERS; p++)
	{
		if (!isHumanPlayer(p)) { continue; }
		for (const DROID *d : world.objects.droids[p])
		{
			if (!usesFuel(d)) { continue; }
			seen[d->id] = true;
			State &st = s_state[d->id];
			// Verbrauch = gefahrener Weg seit dem letzten Schritt
			if (st.havePos)
			{
				const int moved = iHypot(d->pos.x - st.lastX, d->pos.y - st.lastY);
				if (moved > 0 && moved < 8 * TILE_UNITS) { st.fuel = std::max(0, st.fuel - moved); }   // Spruenge (Transport, Ebenenwechsel) zaehlen nicht
			}
			st.lastX = d->pos.x; st.lastY = d->pos.y; st.havePos = true;

			// Tanken an der Tankstelle (kostenlos)
			if (st.fuel < FUEL_RANGE && stationNear(d) != nullptr)
			{
				st.fuel = std::min(FUEL_RANGE, st.fuel + REFUEL_PER_TICK);
			}

			// Selbstversorgung: zurueck an die Stelle, wo der Tank leer wurde, sobald wieder voll
			if (st.trip)
			{
				const bool full = st.fuel >= FUEL_RANGE;
				const bool timedOut = gameTime - st.since > TRIP_TIMEOUT_MS;
				bool taken = d->order.type != DORDER_MOVE && d->order.type != DORDER_NONE;   // Spieler hat etwas anderes befohlen
				if (d->order.type == DORDER_MOVE)
				{
					const int ox = d->order.pos.x - st.goalX, oy = d->order.pos.y - st.goalY;
					if (ox * ox + oy * oy > 2 * TILE_UNITS * 2 * TILE_UNITS) { taken = true; }
				}
				if (full || timedOut || taken)
				{
					st.trip = false;
					if (taken || timedOut) { st.nextAllowed = gameTime + 30000; }
					else
					{
						const int dx = st.originX - d->pos.x, dy = st.originY - d->pos.y;
						if (dx * dx + dy * dy > 6 * TILE_UNITS * 6 * TILE_UNITS)
						{
							orderDroidLoc(const_cast<DROID *>(d), DORDER_MOVE, st.originX, st.originY, ModeImmediate);
						}
					}
				}
			}
			else if (st.fuel == 0 && gameTime >= st.nextAllowed && mayAutoTrip(d) && !modcontrol::manual(d))
			{
				const STRUCTURE *src = nearestStation(d);
				if (src != nullptr)
				{
					const int dx = src->pos.x - d->pos.x, dy = src->pos.y - d->pos.y;
					if (dx * dx + dy * dy > STATION_RADIUS * STATION_RADIUS)
					{
						// Zielpunkt: 2 Felder vor der Tankstelle, auf der Seite der Einheit
						const int len = std::max(1, iHypot(dx, dy));
						st.goalX = src->pos.x - dx * 2 * TILE_UNITS / len;
						st.goalY = src->pos.y - dy * 2 * TILE_UNITS / len;
						st.originX = d->pos.x; st.originY = d->pos.y;
						st.since = gameTime;
						st.trip = true;
						orderDroidLoc(const_cast<DROID *>(d), DORDER_MOVE, st.goalX, st.goalY, ModeImmediate);
					}
				}
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
