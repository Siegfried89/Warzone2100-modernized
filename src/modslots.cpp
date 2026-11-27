// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 02:16): Turm-Plaetze frei belegen - siehe modslots.h
#include <nlohmann/json.hpp> // Must come before WZ includes

#include "modslots.h"
#include "modslots_json.h"

#include "lib/framework/frame.h"
#include "lib/netplay/netplay.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "droid.h"
#include "modcompbody.h"   // (27.11.) Antriebsplaetze
#include "research.h"
#include "stats.h"

#include <algorithm>
#include <vector>

namespace modslots
{

namespace
{

bool isTurretSensor(const DROID_TEMPLATE &t)
{
	return t.asParts[COMP_SENSOR] < asSensorStats.size() && asSensorStats[t.asParts[COMP_SENSOR]].location == LOC_TURRET;
}

bool isTurretEcm(const DROID_TEMPLATE &t)
{
	return t.asParts[COMP_ECM] < asECMStats.size() && asECMStats[t.asParts[COMP_ECM]].location == LOC_TURRET;
}

bool isTurretRepair(const DROID_TEMPLATE &t)
{
	return t.asParts[COMP_REPAIRUNIT] < asRepairStats.size() && asRepairStats[t.asParts[COMP_REPAIRUNIT]].location == LOC_TURRET;
}

unsigned defaultSensor(unsigned player) { return player < MAX_PLAYERS ? aDefaultSensor[player] : 0; }
unsigned defaultEcm(unsigned player) { return player < MAX_PLAYERS ? aDefaultECM[player] : 0; }

void removeWeapon(DROID_TEMPLATE &t, int i)
{
	if (i < 0 || i >= t.numWeaps) { return; }
	for (int k = i; k + 1 < t.numWeaps; ++k)
	{
		t.asWeaps[k] = t.asWeaps[k + 1];
		t.modSlots.weapConn[k] = static_cast<int8_t>(t.modSlots.conn(k + 1));
	}
	t.numWeaps = static_cast<int8_t>(t.numWeaps - 1);
	t.asWeaps[t.numWeaps] = 0;
	t.modSlots.weapConn[t.numWeaps] = -1;
	// die verbleibenden Waffen behalten ihren Anschluss (auch wenn er bisher "Standard" war)
	for (int k = 0; k < t.numWeaps; ++k)
	{
		if (t.modSlots.weapConn[k] < 0) { t.modSlots.weapConn[k] = static_cast<int8_t>(k); }
	}
}

void removeSys(ModSlots &s, unsigned i)
{
	if (i >= s.numSys) { return; }
	for (unsigned k = i; k + 1 < s.numSys; ++k)
	{
		s.sysComp[k] = s.sysComp[k + 1];
		s.sysStat[k] = s.sysStat[k + 1];
		s.sysConn[k] = s.sysConn[k + 1];
	}
	s.numSys = static_cast<uint8_t>(s.numSys - 1);
	s.sysComp[s.numSys] = 0;
	s.sysStat[s.numSys] = 0;
	s.sysConn[s.numSys] = -1;
}

/// Bau- oder Reparaturturm (die stehen hoechstens einmal in der Belegung)
bool isRole(int comp) { return comp == COMP_CONSTRUCT || comp == COMP_REPAIRUNIT; }

bool sysStatOk(int comp, unsigned stat)
{
	switch (comp)
	{
	case COMP_SENSOR: return stat < asSensorStats.size();
	case COMP_ECM: return stat < asECMStats.size();
	case COMP_CONSTRUCT: return stat > 0 && stat < asConstructStats.size();
	case COMP_REPAIRUNIT: return stat > 0 && stat < asRepairStats.size();
	default: return false;
	}
}

/// Systemturm i aus der Belegung einer Vorlage nehmen; ein Bau-/Reparaturturm verschwindet auch aus asParts
void removeSysT(DROID_TEMPLATE &t, unsigned i)
{
	if (i >= t.modSlots.numSys) { return; }
	const int c = t.modSlots.sysComp[i];
	removeSys(t.modSlots, i);
	if (c == COMP_CONSTRUCT) { t.asParts[COMP_CONSTRUCT] = 0; }
	if (c == COMP_REPAIRUNIT && isTurretRepair(t)) { t.asParts[COMP_REPAIRUNIT] = 0; }
}

/// alle Waffen bekommen ihren Anschluss fest eingetragen (vor dem Verschieben/Einfuegen)
void pinWeapons(DROID_TEMPLATE &t)
{
	for (int k = 0; k < t.numWeaps; ++k)
	{
		t.modSlots.weapConn[k] = static_cast<int8_t>(t.modSlots.conn(k));
	}
}

/// Fahrzeug mit einem einzigen Systemturm (bisherige Form) in die Belegungs-Form bringen; der Kommandoturm faellt weg
/// (24.11.: Bau- und Reparaturturm bleiben - als Systemturm auf Platz 1)
void toSlotForm(DROID_TEMPLATE &t)
{
	if (t.asParts[COMP_BRAIN] != 0)
	{
		t.asParts[COMP_BRAIN] = 0;
		t.numWeaps = 0;
		for (unsigned i = 0; i < MAX_WEAPONS; ++i) { t.asWeaps[i] = 0; }
		t.modSlots.resetTurrets();   // (27.11.) Antriebe bleiben
	}
	if (t.modSlots.numSys == 0 && t.numWeaps == 0)
	{
		int comp = -1;
		unsigned stat = 0;
		if (isTurretSensor(t)) { comp = COMP_SENSOR; stat = t.asParts[COMP_SENSOR]; }
		else if (isTurretEcm(t)) { comp = COMP_ECM; stat = t.asParts[COMP_ECM]; }
		else if (t.asParts[COMP_CONSTRUCT] != 0) { comp = COMP_CONSTRUCT; stat = t.asParts[COMP_CONSTRUCT]; }
		else if (isTurretRepair(t)) { comp = COMP_REPAIRUNIT; stat = t.asParts[COMP_REPAIRUNIT]; }
		if (comp >= 0)
		{
			t.modSlots.sysComp[0] = static_cast<uint8_t>(comp); t.modSlots.sysStat[0] = static_cast<uint16_t>(stat); t.modSlots.sysConn[0] = 0;
			t.modSlots.numSys = 1;
		}
	}
	else if (t.modSlots.numSys == 0)
	{
		// Waffen-Fahrzeug: ein alter Bau-/Reparaturturm daneben gab es nicht - sicherheitshalber weg
		t.asParts[COMP_CONSTRUCT] = 0;
		if (isTurretRepair(t)) { t.asParts[COMP_REPAIRUNIT] = 0; }
	}
	pinWeapons(t);
}

/// was an diesem Platz sitzt entfernen (in der Belegungs-Form)
void removeAt(DROID_TEMPLATE &t, int slot)
{
	for (int i = t.numWeaps - 1; i >= 0; --i)
	{
		if (t.modSlots.conn(i) == slot) { removeWeapon(t, i); }
	}
	for (int i = static_cast<int>(t.modSlots.numSys) - 1; i >= 0; --i)
	{
		if (t.modSlots.sysConn[i] == slot) { removeSysT(t, static_cast<unsigned>(i)); }
	}
}

}

unsigned slotCount(const DROID_TEMPLATE &t)
{
	if (t.asParts[COMP_BODY] >= asBodyStats.size()) { return 0; }
	const BODY_STATS &b = asBodyStats[t.asParts[COMP_BODY]];
	unsigned n = std::min<unsigned>(b.weaponSlots, MAX_WEAPONS);
	// nicht mehr Plaetze als das Rumpf-Modell Anschluesse hat (sonst saesse der Turm in der Rumpfmitte)
	const iIMDShape *m = b.pIMD ? b.pIMD->displayModel() : nullptr;
	if (m != nullptr && !m->connectors.empty()) { n = std::min<unsigned>(n, static_cast<unsigned>(m->connectors.size())); }
	return n;
}

bool usable(const DROID_TEMPLATE &t)
{
	if (NetPlay.bComms) { return false; }
	if (slotCount(t) < 2) { return false; }
	if (t.asParts[COMP_PROPULSION] >= asPropulsionStats.size()) { return false; }
	if (asPropulsionStats[t.asParts[COMP_PROPULSION]].propulsionType == PROPULSION_TYPE_LIFT) { return false; }
	switch (t.droidType)
	{
	case DROID_CYBORG: case DROID_CYBORG_SUPER: case DROID_CYBORG_CONSTRUCT: case DROID_CYBORG_REPAIR:
	case DROID_TRANSPORTER: case DROID_SUPERTRANSPORTER: case DROID_PERSON:
		return false;
	default:
		break;
	}
	return true;
}

Occupant occupant(const DROID_TEMPLATE &t, int slot)
{
	Occupant o;
	if (t.asParts[COMP_BRAIN] != 0)
	{
		if (slot == 0) { o.kind = SLOT_SOLE; o.comp = COMP_BRAIN; o.stat = t.asParts[COMP_BRAIN]; }
		return o;
	}
	for (int i = 0; i < t.numWeaps; ++i)
	{
		if (t.modSlots.conn(i) == slot) { o.kind = SLOT_WEAPON; o.index = i; o.comp = COMP_WEAPON; o.stat = t.asWeaps[i]; return o; }
	}
	for (unsigned i = 0; i < t.modSlots.numSys && i < MOD_MAX_SYS; ++i)
	{
		if (t.modSlots.sysConn[i] == slot) { o.kind = SLOT_SYSTEM; o.index = static_cast<int>(i); o.comp = t.modSlots.sysComp[i]; o.stat = t.modSlots.sysStat[i]; return o; }
	}
	if (slot == 0 && t.modSlots.numSys == 0 && t.numWeaps == 0)
	{
		if (isTurretSensor(t)) { o.kind = SLOT_SOLE; o.comp = COMP_SENSOR; o.stat = t.asParts[COMP_SENSOR]; }
		else if (isTurretEcm(t)) { o.kind = SLOT_SOLE; o.comp = COMP_ECM; o.stat = t.asParts[COMP_ECM]; }
		else if (t.asParts[COMP_CONSTRUCT] != 0) { o.kind = SLOT_SOLE; o.comp = COMP_CONSTRUCT; o.stat = t.asParts[COMP_CONSTRUCT]; }
		else if (isTurretRepair(t)) { o.kind = SLOT_SOLE; o.comp = COMP_REPAIRUNIT; o.stat = t.asParts[COMP_REPAIRUNIT]; }
	}
	return o;
}

void assign(DROID_TEMPLATE &t, int slot, int comp, unsigned stat, unsigned player)
{
	if (slot < 0 || slot >= static_cast<int>(MAX_WEAPONS)) { return; }
	if (comp != COMP_WEAPON && comp != COMP_SENSOR && comp != COMP_ECM && !isRole(comp)) { return; }
	toSlotForm(t);
	removeAt(t, slot);
	if (isRole(comp))
	{
		// hoechstens ein Bau- ODER Reparaturturm je Fahrzeug: ein anderer weicht
		for (int i = static_cast<int>(t.modSlots.numSys) - 1; i >= 0; --i)
		{
			if (isRole(t.modSlots.sysComp[i])) { removeSysT(t, static_cast<unsigned>(i)); }
		}
	}
	if (comp == COMP_WEAPON)
	{
		if (t.numWeaps < static_cast<int>(MAX_WEAPONS) && stat < asWeaponStats.size())
		{
			t.asWeaps[t.numWeaps] = stat;
			t.modSlots.weapConn[t.numWeaps] = static_cast<int8_t>(slot);
			t.numWeaps = static_cast<int8_t>(t.numWeaps + 1);
		}
	}
	else if (t.modSlots.numSys < MOD_MAX_SYS)
	{
		const bool ok = sysStatOk(comp, stat);
		if (ok)
		{
			const unsigned n = t.modSlots.numSys;
			t.modSlots.sysComp[n] = static_cast<uint8_t>(comp);
			t.modSlots.sysStat[n] = static_cast<uint16_t>(stat);
			t.modSlots.sysConn[n] = static_cast<int8_t>(slot);
			t.modSlots.numSys = static_cast<uint8_t>(n + 1);
		}
	}
	normalize(t, player);
}

void clearSlot(DROID_TEMPLATE &t, int slot, unsigned player)
{
	const Occupant o = occupant(t, slot);
	switch (o.kind)
	{
	case SLOT_EMPTY:
		return;
	case SLOT_SOLE:
		if (o.comp == COMP_BRAIN)
		{
			t.asParts[COMP_BRAIN] = 0;
			t.numWeaps = 0;
			for (unsigned i = 0; i < MAX_WEAPONS; ++i) { t.asWeaps[i] = 0; }
		}
		else if (o.comp == COMP_SENSOR) { t.asParts[COMP_SENSOR] = static_cast<uint16_t>(defaultSensor(player)); }
		else if (o.comp == COMP_ECM) { t.asParts[COMP_ECM] = static_cast<uint16_t>(defaultEcm(player)); }
		else if (o.comp == COMP_CONSTRUCT) { t.asParts[COMP_CONSTRUCT] = 0; }
		else if (o.comp == COMP_REPAIRUNIT) { t.asParts[COMP_REPAIRUNIT] = 0; }
		break;
	case SLOT_WEAPON:
		pinWeapons(t);
		removeWeapon(t, o.index);
		break;
	case SLOT_SYSTEM:
		pinWeapons(t);
		removeSysT(t, static_cast<unsigned>(o.index));
		if (t.modSlots.numSys == 0)
		{
			// eingebaute Teile (LOC_DEFAULT, z.B. Fatboy-Schild) bleiben
			if (isTurretSensor(t)) { t.asParts[COMP_SENSOR] = static_cast<uint16_t>(defaultSensor(player)); }
			if (isTurretEcm(t)) { t.asParts[COMP_ECM] = static_cast<uint16_t>(defaultEcm(player)); }
		}
		break;
	}
	normalize(t, player);
}

void fitToBody(DROID_TEMPLATE &t, unsigned player)
{
	const unsigned n = slotCount(t);
	const bool hadSys = t.modSlots.numSys > 0;
	// (27.11.) Antriebsplaetze: nur so viele, wie der Rumpf hat; unbekannte Antriebe zurueck auf den Hauptantrieb
	{
		const size_t nd = std::min<size_t>(modcompbody::driveCount(t.asParts[COMP_BODY]), MOD_MAX_DRIVES);
		if (t.modSlots.numDrive > nd) { t.modSlots.numDrive = static_cast<uint8_t>(nd); }
		for (unsigned i = 0; i < MOD_MAX_DRIVES; ++i)
		{
			if (i >= t.modSlots.numDrive || t.modSlots.driveProp[i] >= asPropulsionStats.size()) { t.modSlots.driveProp[i] = 0; }
		}
	}
	if (!usable(t))
	{
		// ohne freie Belegung: ein Bau-/Reparaturturm aus der Belegung bleibt nur, wenn keine Waffen da sind (bisherige Form)
		for (unsigned i = 0; i < t.modSlots.numSys; ++i)
		{
			const int c = t.modSlots.sysComp[i];
			if (isRole(c) && t.numWeaps > 0) { if (c == COMP_CONSTRUCT) { t.asParts[COMP_CONSTRUCT] = 0; } else if (isTurretRepair(t)) { t.asParts[COMP_REPAIRUNIT] = 0; } }
		}
		t.modSlots.resetTurrets();   // (27.11.) Antriebe bleiben
		if (t.numWeaps > static_cast<int>(n))
		{
			for (unsigned i = n; i < MAX_WEAPONS; ++i) { t.asWeaps[i] = 0; }
			t.numWeaps = static_cast<int8_t>(n);
		}
	}
	else
	{
		pinWeapons(t);
		for (int i = t.numWeaps - 1; i >= 0; --i)
		{
			if (t.modSlots.conn(i) >= static_cast<int>(n)) { removeWeapon(t, i); }
		}
		for (int i = static_cast<int>(t.modSlots.numSys) - 1; i >= 0; --i)
		{
			if (t.modSlots.sysConn[i] >= static_cast<int>(n)) { removeSysT(t, static_cast<unsigned>(i)); }
		}
	}
	if (hadSys && t.modSlots.numSys == 0)
	{
		if (isTurretSensor(t)) { t.asParts[COMP_SENSOR] = static_cast<uint16_t>(defaultSensor(player)); }
		if (isTurretEcm(t)) { t.asParts[COMP_ECM] = static_cast<uint16_t>(defaultEcm(player)); }
	}
	normalize(t, player);
}

void normalize(DROID_TEMPLATE &t, unsigned player)
{
	ModSlots &s = t.modSlots;
	// ungueltige Systemtuerme verwerfen
	for (int i = static_cast<int>(s.numSys) - 1; i >= 0; --i)
	{
		const bool ok = sysStatOk(s.sysComp[i], s.sysStat[i]);
		if (!ok || s.sysConn[i] < 0) { removeSysT(t, static_cast<unsigned>(i)); }
	}
	// hoechstens ein Bau-/Reparaturturm (der erste bleibt)
	{
		bool seen = false;
		for (unsigned i = 0; i < s.numSys; )
		{
			if (isRole(s.sysComp[i])) { if (seen) { removeSys(s, i); continue; } seen = true; }
			++i;
		}
	}
	// Waffen nach Platz ordnen: die Waffe auf dem niedrigsten Platz ist die erste (Hauptwaffe)
	if (t.numWeaps > 1)
	{
		std::vector<std::pair<int, uint32_t>> w;
		for (int i = 0; i < t.numWeaps; ++i) { w.emplace_back(s.conn(i), t.asWeaps[i]); }
		std::stable_sort(w.begin(), w.end(), [](const std::pair<int, uint32_t> &a, const std::pair<int, uint32_t> &b) { return a.first < b.first; });
		for (int i = 0; i < t.numWeaps; ++i) { t.asWeaps[i] = w[i].second; s.weapConn[i] = static_cast<int8_t>(w[i].first); }
	}
	// ein einzelner Systemturm auf Platz 1 ohne Waffen = Fahrzeug wie bisher
	if (s.numSys == 1 && t.numWeaps == 0 && s.sysConn[0] == 0)
	{
		const int c = s.sysComp[0];
		const uint16_t st = s.sysStat[0];
		if (isTurretSensor(t) && c != COMP_SENSOR) { t.asParts[COMP_SENSOR] = static_cast<uint16_t>(defaultSensor(player)); }
		if (isTurretEcm(t) && c != COMP_ECM) { t.asParts[COMP_ECM] = static_cast<uint16_t>(defaultEcm(player)); }
		if (c != COMP_CONSTRUCT) { t.asParts[COMP_CONSTRUCT] = 0; }
		if (c != COMP_REPAIRUNIT && isTurretRepair(t)) { t.asParts[COMP_REPAIRUNIT] = 0; }
		t.asParts[c] = st;
		removeSys(s, 0);
	}
	if (s.numSys == 0)
	{
		if (t.numWeaps > 0 && t.asParts[COMP_BRAIN] == 0)
		{
			if (isTurretSensor(t)) { t.asParts[COMP_SENSOR] = static_cast<uint16_t>(defaultSensor(player)); }
			if (isTurretEcm(t)) { t.asParts[COMP_ECM] = static_cast<uint16_t>(defaultEcm(player)); }
		}
		bool identity = true;
		for (int i = 0; i < t.numWeaps; ++i) { if (s.conn(i) != i) { identity = false; } }
		if (identity) { for (unsigned i = 0; i < MAX_WEAPONS; ++i) { s.weapConn[i] = -1; } }
		else { for (unsigned i = static_cast<unsigned>(std::max<int>(0, t.numWeaps)); i < MAX_WEAPONS; ++i) { s.weapConn[i] = -1; } }
		return;
	}
	for (unsigned i = static_cast<unsigned>(std::max<int>(0, t.numWeaps)); i < MAX_WEAPONS; ++i) { s.weapConn[i] = -1; }
	for (int i = 0; i < t.numWeaps; ++i) { s.weapConn[i] = static_cast<int8_t>(s.conn(i)); }
	// bester Sensor (Reichweite) und bester Stoersender stehen in asParts: Sicht, Ortung, Aufwertungen wie gewohnt
	int bestS = -1, bestE = -1, rangeS = -1, rangeE = -1;
	for (unsigned i = 0; i < s.numSys; ++i)
	{
		if (s.sysComp[i] == COMP_SENSOR)
		{
			const int r = sensorRange(asSensorStats[s.sysStat[i]], static_cast<int>(std::min<unsigned>(player, MAX_PLAYERS - 1)));
			if (r > rangeS) { rangeS = r; bestS = s.sysStat[i]; }
		}
		else if (s.sysComp[i] == COMP_ECM)
		{
			const int r = ecmRange(asECMStats[s.sysStat[i]], static_cast<int>(std::min<unsigned>(player, MAX_PLAYERS - 1)));
			if (r > rangeE) { rangeE = r; bestE = s.sysStat[i]; }
		}
	}
	if (bestS >= 0) { t.asParts[COMP_SENSOR] = static_cast<uint16_t>(bestS); }
	else if (isTurretSensor(t)) { t.asParts[COMP_SENSOR] = static_cast<uint16_t>(defaultSensor(player)); }
	if (bestE >= 0) { t.asParts[COMP_ECM] = static_cast<uint16_t>(bestE); }
	else if (isTurretEcm(t)) { t.asParts[COMP_ECM] = static_cast<uint16_t>(defaultEcm(player)); }
	t.asParts[COMP_BRAIN] = 0;
	// (24.11.) Bau-/Reparaturturm der Belegung steht in asParts: Bauen/Reparieren, Fahrzeugart, Aufwertungen wie gewohnt
	int cons = -1, rep = -1;
	for (unsigned i = 0; i < s.numSys; ++i)
	{
		if (s.sysComp[i] == COMP_CONSTRUCT) { cons = s.sysStat[i]; }
		if (s.sysComp[i] == COMP_REPAIRUNIT) { rep = s.sysStat[i]; }
	}
	t.asParts[COMP_CONSTRUCT] = static_cast<uint16_t>(cons >= 0 ? cons : 0);
	if (rep >= 0) { t.asParts[COMP_REPAIRUNIT] = static_cast<uint16_t>(rep); }
	else if (isTurretRepair(t)) { t.asParts[COMP_REPAIRUNIT] = 0; }
}

bool valid(const DROID_TEMPLATE &t, std::string *why)
{
	const ModSlots &s = t.modSlots;
	if (s.isDefault()) { return true; }
	auto fail = [&](const char *m) { if (why) { *why = m; } return false; };
	if (NetPlay.bComms) { return fail("free turret slots are not available in network games"); }
	const unsigned n = slotCount(t);
	if (static_cast<unsigned>(std::max<int>(0, t.numWeaps)) + s.numSys > n) { return fail("more turrets than slots"); }
	if (s.numSys > 0)
	{
		if (t.asParts[COMP_BRAIN] != 0) { return fail("command turrets cannot be combined"); }
		unsigned roles = 0;
		for (unsigned i = 0; i < s.numSys; ++i) { if (isRole(s.sysComp[i])) { ++roles; } }
		if (roles > 1) { return fail("only one construction or repair turret per vehicle"); }
		if (t.asParts[COMP_PROPULSION] < asPropulsionStats.size() && asPropulsionStats[t.asParts[COMP_PROPULSION]].propulsionType == PROPULSION_TYPE_LIFT)
		{
			return fail("VTOLs cannot carry system turrets beside weapons");
		}
	}
	bool used[MAX_WEAPONS] = {};
	auto take = [&](int c) {
		if (c < 0 || c >= static_cast<int>(n) || used[c]) { return false; }
		used[c] = true;
		return true;
	};
	for (int i = 0; i < t.numWeaps; ++i) { if (!take(s.conn(i))) { return fail("weapon on an invalid or doubly used slot"); } }
	for (unsigned i = 0; i < s.numSys; ++i)
	{
		if (!take(s.sysConn[i])) { return fail("system turret on an invalid or doubly used slot"); }
		if (!sysStatOk(s.sysComp[i], s.sysStat[i])) { return fail("invalid system turret"); }
	}
	return true;
}

bool researched(const DROID_TEMPLATE &t, unsigned player, bool allowRedundant)
{
	if (player >= MAX_PLAYERS) { return false; }
	const ModSlots &s = t.modSlots;
	for (unsigned i = 0; i < s.numSys; ++i)
	{
		const UBYTE *list = apCompLists[player][s.sysComp[i]];
		if (list == nullptr) { return false; }
		const UBYTE st = list[s.sysStat[i]];
		if (!(st == AVAILABLE || (allowRedundant && st == REDUNDANT))) { return false; }
	}
	return true;
}

const SENSOR_STATS *extraShieldSensor(const DROID *d)
{
	if (d == nullptr) { return nullptr; }
	const SENSOR_STATS *best = nullptr;
	const ModSlots &s = d->modSlots;
	for (unsigned i = 0; i < s.numSys && i < MOD_MAX_SYS; ++i)
	{
		if (s.sysComp[i] != COMP_SENSOR || s.sysStat[i] >= asSensorStats.size()) { continue; }
		const SENSOR_STATS *ss = &asSensorStats[s.sysStat[i]];
		if (ss->shieldRadius > 0 && ss->shieldHP > 0 && (best == nullptr || ss->shieldHP > best->shieldHP)) { best = ss; }
	}
	return best;
}

nlohmann::json toJson(const ModSlots &s)
{
	nlohmann::json j = nlohmann::json::object();
	nlohmann::json wc = nlohmann::json::array();
	int last = -1;
	for (unsigned i = 0; i < MAX_WEAPONS; ++i) { if (s.weapConn[i] >= 0) { last = static_cast<int>(i); } }
	for (int i = 0; i <= last; ++i) { wc.push_back(static_cast<int>(s.weapConn[i])); }
	j["weapConn"] = wc;
	nlohmann::json sys = nlohmann::json::array();
	for (unsigned i = 0; i < s.numSys && i < MOD_MAX_SYS; ++i)
	{
		nlohmann::json e = nlohmann::json::object();
		const COMPONENT_STATS *cs = nullptr;
		const char *type = nullptr;
		const unsigned st = s.sysStat[i];
		switch (s.sysComp[i])
		{
		case COMP_SENSOR: if (st < asSensorStats.size()) { cs = &asSensorStats[st]; type = "sensor"; } break;
		case COMP_ECM: if (st < asECMStats.size()) { cs = &asECMStats[st]; type = "ecm"; } break;
		case COMP_CONSTRUCT: if (st < asConstructStats.size()) { cs = &asConstructStats[st]; type = "construct"; } break;
		case COMP_REPAIRUNIT: if (st < asRepairStats.size()) { cs = &asRepairStats[st]; type = "repair"; } break;
		default: break;
		}
		if (cs == nullptr) { continue; }
		e["type"] = type;
		e["id"] = cs->id.toUtf8();
		e["slot"] = static_cast<int>(s.sysConn[i]);
		sys.push_back(e);
	}
	if (!sys.empty()) { j["systems"] = sys; }
	// (27.11.) Antrieb je Antriebsplatz (als ID - Indizes aendern sich mit dem Paket)
	nlohmann::json drv = nlohmann::json::array();
	for (unsigned i = 0; i < s.numDrive && i < MOD_MAX_DRIVES; ++i)
	{
		const unsigned st = s.driveProp[i];
		drv.push_back(st < asPropulsionStats.size() ? asPropulsionStats[st].id.toUtf8() : std::string());
	}
	if (!drv.empty()) { j["drives"] = drv; }
	return j;
}

bool fromJson(const nlohmann::json &j, ModSlots &s)
{
	s.reset();
	if (!j.is_object()) { return false; }
	const auto wc = j.find("weapConn");
	if (wc != j.end() && wc->is_array())
	{
		for (size_t i = 0; i < wc->size() && i < MAX_WEAPONS; ++i)
		{
			const int c = (*wc)[i].is_number_integer() ? (*wc)[i].get<int>() : -1;
			s.weapConn[i] = static_cast<int8_t>((c >= 0 && c < static_cast<int>(MAX_WEAPONS)) ? c : -1);
		}
	}
	const auto sys = j.find("systems");
	if (sys != j.end() && sys->is_array())
	{
		for (const auto &e : *sys)
		{
			if (s.numSys >= MOD_MAX_SYS || !e.is_object()) { break; }
			const std::string type = e.value("type", std::string("sensor"));
			const int comp = type == "ecm" ? COMP_ECM : type == "construct" ? COMP_CONSTRUCT : type == "repair" ? COMP_REPAIRUNIT : COMP_SENSOR;
			const int idx = getCompFromID(static_cast<COMPONENT_TYPE>(comp), WzString::fromUtf8(e.value("id", std::string())));
			const int slot = e.value("slot", -1);
			if (idx < 0 || slot < 0 || slot >= static_cast<int>(MAX_WEAPONS)) { continue; }
			s.sysComp[s.numSys] = static_cast<uint8_t>(comp);
			s.sysStat[s.numSys] = static_cast<uint16_t>(idx);
			s.sysConn[s.numSys] = static_cast<int8_t>(slot);
			s.numSys = static_cast<uint8_t>(s.numSys + 1);
		}
	}
	const auto drv = j.find("drives");
	if (drv != j.end() && drv->is_array())
	{
		for (const auto &e : *drv)
		{
			if (s.numDrive >= MOD_MAX_DRIVES) { break; }
			int idx = e.is_string() ? getCompFromID(COMP_PROPULSION, WzString::fromUtf8(e.get<std::string>())) : -1;
			if (idx < 0) { idx = 0; }   // unbekannter Antrieb: Platz nimmt den Hauptantrieb (beim Zeichnen/Rechnen)
			s.driveProp[s.numDrive] = static_cast<uint16_t>(idx);
			s.numDrive = static_cast<uint8_t>(s.numDrive + 1);
		}
	}
	return true;
}

}
