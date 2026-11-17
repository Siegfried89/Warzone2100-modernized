// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Artillerie-Modus (siehe modarty.h)

#include "modarty.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "droid.h"
#include "order.h"
#include "stats.h"

#include <vector>

namespace modarty
{
namespace
{
std::vector<int32_t> s_toArty;   // Waffennummer -> Artillerie-Fassung (-1 = keine)
std::vector<int32_t> s_toBase;   // Artillerie-Fassung -> normale Kanone (-1 = keine Artillerie-Fassung)
const WEAPON_STATS *s_builtFor = nullptr;
size_t s_builtSize = 0;

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

bool groundVehicle(const DROID *d)
{
	if (d == nullptr) { return false; }
	if (d->droidType != DROID_WEAPON) { return false; }   // keine Cyborgs, Infanterie, Transporter, Bau-/Sensorfahrzeuge
	if (d->isVtol() || d->isTransporter()) { return false; }
	return true;
}
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
	return isArtyWeapon(weaponStat) ? static_cast<uint16_t>(DEG(48)) : 0;   // (20:38) steiler, wie Artilleriepanzer
}

void reset()
{
	s_toArty.clear();
	s_toBase.clear();
	s_builtFor = nullptr;
	s_builtSize = 0;
}
}
