// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Treibstoff fuer Fahrzeuge - siehe modfuel.h
#include "modcontrol.h"
#include "modfuel.h"
#include "modoil.h"

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
constexpr int FUEL_RANGE = 480 * DIST_TILE;            // "recht lange": ein voller Tank reicht fuer 480 Kacheln Fahrweg
constexpr int MIN_SPEED_PERCENT = 15;                   // leerer Tank: Mindesttempo
constexpr int STATION_RADIUS = 3 * DIST_TILE;          // in diesem Umkreis um die Tankstelle wird getankt
constexpr int REFUEL_PER_TICK = FUEL_RANGE / 16;        // voll in 8 Sekunden
constexpr unsigned TRIP_TIMEOUT_MS = 300000;            // nach 5 Minuten ohne Erfolg gibt die Einheit die Fahrt auf
// (Wunsch 20.11. 17:46 Punkt 2) Fracht-Lkw und Oeltanker: der Tank geht nie ganz leer - unter der Reserve fahren sie langsamer weiter
// und tanken an einer Tankstelle MIT Vorrat (zu einer leeren fahren sie nicht - sie sind es ja, die das Oel bringen)
constexpr int CARGO_RESERVE = FUEL_RANGE / 10;          // 10 % = 48 Felder
constexpr int CARGO_RESERVE_SPEED = 50;                 // Tempo in der Reserve

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
	// (Wunsch 20.11. 17:46) Tankstellen sind die Scav-Reparaturzentren (heilen nur bis 50 %); die Reparaturstation (100 %) und das
	// Combot-Reparaturgeruest sind keine Tankstellen
	return s->pStructureType->id.startsWith("ScavRepair");
}

/// KI ohne eigene Tankstelle: Fabriken und Hauptquartier dienen als Notfall-Tankstelle (bis die KI selbst Tankstellen baut)
bool aiFallbackStation(const STRUCTURE *s)
{
	if (s == nullptr || s->died || s->status != SS_BUILT || s->pStructureType == nullptr) { return false; }
	switch (s->pStructureType->type)
	{
	case REF_FACTORY:
	case REF_CYBORG_FACTORY:
	case REF_VTOL_FACTORY:
	case REF_HQ:
		return true;
	default:
		return false;
	}
}

bool hasStation(int player)
{
	for (const STRUCTURE *s : gameWorld.objects.structures[player]) { if (stationRaw(s)) { return true; } }
	return false;
}

/// Tankstelle fuer diese Einheit? (Spieler: nur echte Tankstellen; KI ohne Tankstelle: auch Fabrik/HQ)
bool stationFor(const DROID * /*d*/, const STRUCTURE *s, bool aiFallback)
{
	return stationRaw(s) || (aiFallback && aiFallbackStation(s));
}

const STRUCTURE *stationNear(const DROID *d, bool aiFallback)
{
	for (const STRUCTURE *s : gameWorld.objects.structures[d->player])
	{
		if (!stationFor(d, s, aiFallback) || modoil::stationMilli(s) <= 0) { continue; }
		const int dx = s->pos.x - d->pos.x, dy = s->pos.y - d->pos.y;
		if (dx * dx + dy * dy <= STATION_RADIUS * STATION_RADIUS) { return s; }
	}
	return nullptr;
}

const STRUCTURE *nearestStation(const DROID *d, bool aiFallback)
{
	// Tankstellen mit Vorrat zuerst; ist keine gefuellt, die naechste ueberhaupt (dort warten, bis Lkw Faesser bringen)
	const STRUCTURE *best = nullptr, *bestEmpty = nullptr;
	int64_t bestD = INT64_MAX, bestED = INT64_MAX;
	for (const STRUCTURE *s : gameWorld.objects.structures[d->player])
	{
		if (!stationFor(d, s, aiFallback)) { continue; }
		const int64_t dx = s->pos.x - d->pos.x, dy = s->pos.y - d->pos.y;
		const int64_t dist = dx * dx + dy * dy;
		if (modoil::stationMilli(s) > 0) { if (dist < bestD) { bestD = dist; best = s; } }
		else if (dist < bestED) { bestED = dist; bestEmpty = s; }
	}
	return best != nullptr ? best : bestEmpty;
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
	if (d->player >= MAX_PLAYERS) { return false; }   // (16.11. 19:28) gilt fuer ALLE Spieler, auch die KI
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
	if (it == s_state.end()) { return 100; }
	if (modoil::isCargoVehicle(d)) { return (it->second.fuel <= CARGO_RESERVE && usesFuel(d)) ? CARGO_RESERVE_SPEED : 100; }
	if (it->second.fuel > 0) { return 100; }
	return usesFuel(d) ? MIN_SPEED_PERCENT : 100;
}

bool isHalfRepair(const STRUCTURE *s)
{
	return s != nullptr && s->pStructureType != nullptr && s->pStructureType->type == REF_REPAIR_FACILITY && s->pStructureType->id.startsWith("ScavRepair");
}

void tick(GameWorld &world)
{
	if (gameTime < s_nextTick) { return; }
	s_nextTick = gameTime + TICK_MS;
	std::unordered_map<uint32_t, bool> seen;
	for (unsigned p = 0; p < MAX_PLAYERS; p++)
	{
		const bool aiFallback = !isHumanPlayer(p) && !hasStation(static_cast<int>(p));
		for (const DROID *d : world.objects.droids[p])
		{
			if (!usesFuel(d)) { continue; }
			seen[d->id] = true;
			State &st = s_state[d->id];
			const bool cargo = modoil::isCargoVehicle(d);
			// Verbrauch = gefahrener Weg seit dem letzten Schritt
			if (st.havePos)
			{
				const int moved = iHypot(d->pos.x - st.lastX, d->pos.y - st.lastY);
				if (moved > 0 && moved < 8 * DIST_TILE)   // Spruenge (Transport, Ebenenwechsel) zaehlen nicht
				{
					const int floorFuel = cargo ? std::min(st.fuel, CARGO_RESERVE) : 0;   // Frachter: nie unter die Reserve
					st.fuel = std::max(floorFuel, st.fuel - moved);
				}
			}
			st.lastX = d->pos.x; st.lastY = d->pos.y; st.havePos = true;

			// Tanken an der Tankstelle: verbraucht Oel aus deren Vorrat (1 Fass = 2 volle Tanks)
			if (st.fuel < FUEL_RANGE)
			{
				const STRUCTURE *stn = stationNear(d, aiFallback);
				if (stn != nullptr)
				{
					const int want = std::min(REFUEL_PER_TICK, FUEL_RANGE - st.fuel);
					const int milli = std::max(1, static_cast<int>(static_cast<int64_t>(want) * 1000 / (2 * static_cast<int64_t>(FUEL_RANGE))));
					const int got = modoil::stationTake(stn, milli);
					st.fuel = std::min(FUEL_RANGE, st.fuel + static_cast<int>(static_cast<int64_t>(got) * 2 * FUEL_RANGE / 1000));
				}
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
					if (ox * ox + oy * oy > 2 * DIST_TILE * 2 * DIST_TILE) { taken = true; }
				}
				if (full || timedOut || taken)
				{
					st.trip = false;
					if (taken || timedOut) { st.nextAllowed = gameTime + 30000; }
					else if (!cargo)   // Frachter kehren nicht zurueck - ihr Autopilot uebernimmt
					{
						const int dx = st.originX - d->pos.x, dy = st.originY - d->pos.y;
						if (dx * dx + dy * dy > 6 * DIST_TILE * 6 * DIST_TILE)
						{
							orderDroidLoc(const_cast<DROID *>(d), DORDER_MOVE, st.originX, st.originY, ModeImmediate);
						}
					}
				}
			}
			else if ((cargo ? st.fuel <= CARGO_RESERVE : st.fuel == 0) && gameTime >= st.nextAllowed && mayAutoTrip(d) && !modcontrol::manual(d))
			{
				const STRUCTURE *src = nearestStation(d, aiFallback);
				if (cargo && src != nullptr && modoil::stationMilli(src) <= 0) { src = nullptr; }   // Frachter: nur Tankstellen mit Vorrat
				if (src != nullptr)
				{
					const int dx = src->pos.x - d->pos.x, dy = src->pos.y - d->pos.y;
					if (dx * dx + dy * dy > STATION_RADIUS * STATION_RADIUS)
					{
						// Zielpunkt: 2 Felder vor der Tankstelle, auf der Seite der Einheit
						const int len = std::max(1, iHypot(dx, dy));
						st.goalX = src->pos.x - dx * 2 * DIST_TILE / len;
						st.goalY = src->pos.y - dy * 2 * DIST_TILE / len;
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

void saveState(std::vector<int> &out)
{
	out.clear();
	std::vector<std::pair<uint32_t, int>> list;
	for (const auto &kv : s_state) { if (kv.second.fuel < FUEL_RANGE) { list.emplace_back(kv.first, kv.second.fuel); } }
	std::sort(list.begin(), list.end());
	for (const auto &e : list) { out.push_back(static_cast<int>(e.first)); out.push_back(e.second); }
}

void loadState(const std::vector<int> &in)
{
	for (size_t i = 0; i + 2 <= in.size(); i += 2)
	{
		State &st = s_state[static_cast<uint32_t>(in[i])];
		st.fuel = std::clamp(in[i + 1], 0, FUEL_RANGE);
		st.havePos = false;
	}
}
}
