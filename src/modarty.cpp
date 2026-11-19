// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Artillerie-Modus (siehe modarty.h)

#include "modarty.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/math_ext.h"
#include "droid.h"
#include "order.h"
#include "stats.h"
#include "orderdef.h"
#include "lib/gamelib/gtime.h"

#include <unordered_map>
#include <vector>

namespace modarty
{
namespace
{
std::vector<int32_t> s_toArty;   // Waffennummer -> Artillerie-Fassung (-1 = keine)
std::vector<int32_t> s_toBase;   // Artillerie-Fassung -> normale Kanone (-1 = keine Artillerie-Fassung)
const WEAPON_STATS *s_builtFor = nullptr;
size_t s_builtSize = 0;
std::unordered_map<uint32_t, uint32_t> s_leftAt;   // Einheit -> Spielzeit, zu der sie den Steilfeuermodus verlassen hat
constexpr uint32_t PACK_MS = 1000;                  // so lange steht sie danach noch (Rohr faehrt herunter)

void ensureTables()
{
	if (s_builtFor == asWeaponStats.data() && s_builtSize == asWeaponStats.size() && !s_toArty.empty()) { return; }
	s_builtFor = asWeaponStats.data();
	s_builtSize = asWeaponStats.size();
	s_toArty.assign(asWeaponStats.size(), -1);
	s_toBase.assign(asWeaponStats.size(), -1);
	for (size_t i = 0; i < asWeaponStats.size(); ++i)
	{
		const WzString &id = asWeaponStats[i].id;
		if (id.isEmpty() || id.endsWith("_ARTY")) { continue; }
		// leise Suche (getCompFromID wuerde fuer jede Waffe ohne Artillerie-Fassung einen Fehler melden)
		const COMPONENT_STATS *c = getCompStatsFromName(id + "_ARTY");
		if (c == nullptr || c->compType != COMP_WEAPON) { continue; }
		const int a = static_cast<int>(c->index);
		if (a > 0 && static_cast<size_t>(a) < asWeaponStats.size())
		{
			s_toArty[i] = a;
			s_toBase[static_cast<size_t>(a)] = static_cast<int32_t>(i);
		}
	}
}

bool mechBody(const DROID *d)
{
	if (d == nullptr || d->droidType != DROID_CYBORG_SUPER) { return false; }
	const BODY_STATS *b = d->getBodyStats();
	return b != nullptr && b->gaitStride > 0;   // Battle Mechs (Lauf nach Strecke); der GDI Juggernaut (eigene Artillerie) hat keinen
}

bool groundVehicle(const DROID *d)
{
	if (d == nullptr) { return false; }
	if (mechBody(d)) { return true; }                       // (19.11. 22:04) Battle Mech: Steilfeuer mit den Raketenbatterien
	if (d->droidType != DROID_WEAPON) { return false; }   // keine Cyborgs, Infanterie, Transporter, Bau-/Sensorfahrzeuge
	if (d->isVtol() || d->isTransporter()) { return false; }
	return true;
}
}

bool isMech(const DROID *psDroid)
{
	return mechBody(psDroid);
}

uint16_t restPitch(const DROID *psDroid, uint32_t weaponStat)
{
	if (!isArtyWeapon(weaponStat)) { return 0; }
	return static_cast<uint16_t>(mechBody(psDroid) ? DEG(30) : DEG(68));
}

uint16_t aimPitch(const DROID *psDroid, int dist, int longRange)
{
	const int a = shotAngle(dist, longRange);
	return static_cast<uint16_t>(mechBody(psDroid) ? a * 45 / 100 : a);
}

bool supported(const DROID *psDroid)
{
	if (!groundVehicle(psDroid)) { return false; }
	ensureTables();
	for (unsigned i = 0; i < psDroid->numWeaps && i < MAX_WEAPONS; ++i)
	{
		const uint32_t s = psDroid->asWeaps[i].nStat;
		if (s < s_toArty.size() && (s_toArty[s] >= 0 || s_toBase[s] >= 0)) { return true; }
	}
	return false;
}

void apply(DROID *psDroid, bool on)
{
	if (!groundVehicle(psDroid)) { return; }
	ensureTables();
	for (unsigned i = 0; i < psDroid->numWeaps && i < MAX_WEAPONS; ++i)
	{
		WEAPON &w = psDroid->asWeaps[i];
		if (w.nStat >= s_toArty.size()) { continue; }
		if (on && s_toArty[w.nStat] >= 0)
		{
			w.nStat = static_cast<uint32_t>(s_toArty[w.nStat]);
		}
		else if (!on && s_toBase[w.nStat] >= 0)
		{
			w.nStat = static_cast<uint32_t>(s_toBase[w.nStat]);
		}
	}
}

void refresh(DROID *psDroid)
{
	if (!groundVehicle(psDroid)) { return; }
	apply(psDroid, (psDroid->secondaryOrder & DSS_ARTY_MASK) != 0);
}

bool isArtyWeapon(uint32_t weaponStat)
{
	ensureTables();
	return weaponStat < s_toBase.size() && s_toBase[weaponStat] >= 0;
}

uint16_t restPitch(uint32_t weaponStat)
{
	return isArtyWeapon(weaponStat) ? static_cast<uint16_t>(DEG(68)) : 0;   // (19.11. 19:43) Steilfeuer: Rohr steil nach oben
}

bool active(const DROID *psDroid)
{
	if (!groundVehicle(psDroid) || (psDroid->secondaryOrder & DSS_ARTY_MASK) == 0) { return false; }
	for (unsigned i = 0; i < psDroid->numWeaps && i < MAX_WEAPONS; ++i)
	{
		if (isArtyWeapon(psDroid->asWeaps[i].nStat)) { return true; }
	}
	return false;
}

bool halted(const DROID *psDroid)
{
	if (psDroid == nullptr) { return false; }
	if (active(psDroid)) { return true; }
	if (s_leftAt.empty()) { return false; }
	const auto it = s_leftAt.find(psDroid->id);
	if (it == s_leftAt.end()) { return false; }
	if (gameTime - it->second < PACK_MS) { return true; }
	s_leftAt.erase(it);
	return false;
}

int shotAngle(int dist, int longRange)
{
	const int range = std::max(1, longRange);
	const int frac = clip(dist * 100 / range, 0, 100);
	return DEG(75) - DEG(10) * frac / 100;
}

bool isMoveOrder(int orderType)
{
	switch (orderType)
	{
	case DORDER_MOVE: case DORDER_SCOUT: case DORDER_PATROL: case DORDER_CIRCLE:
	case DORDER_RTB: case DORDER_RTR: case DORDER_RTR_SPECIFIED: case DORDER_EMBARK:
	case DORDER_RECYCLE: case DORDER_RECOVER: case DORDER_COMMANDERSUPPORT:
		return true;
	default:
		return false;
	}
}

void noteChanged(const DROID *psDroid, bool on)
{
	if (psDroid == nullptr) { return; }
	if (on) { s_leftAt.erase(psDroid->id); }
	else { s_leftAt[psDroid->id] = gameTime; }
}

void reset()
{
	s_leftAt.clear();
	s_toArty.clear();
	s_toBase.clear();
	s_builtFor = nullptr;
	s_builtSize = 0;
}
}
