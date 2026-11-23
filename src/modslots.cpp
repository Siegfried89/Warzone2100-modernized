// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 02:16): Turm-Plaetze frei belegen - siehe modslots.h
#include <nlohmann/json.hpp> // Must come before WZ includes

#include "modslots.h"
#include "modslots_json.h"

#include "lib/framework/frame.h"
#include "lib/netplay/netplay.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "droid.h"
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

/// alle Waffen bekommen ihren Anschluss fest eingetragen (vor dem Verschieben/Einfuegen)
void pinWeapons(DROID_TEMPLATE &t)
{
	for (int k = 0; k < t.numWeaps; ++k)
	{
		t.modSlots.weapConn[k] = static_cast<int8_t>(t.modSlots.conn(k));
	}
}

/// Fahrzeug mit einem einzigen Systemturm (bisherige Form) in die Belegungs-Form bringen; Bau/Reparatur/Kommando fallen weg
void toSlotForm(DROID_TEMPLATE &t)
{
	if (t.asParts[COMP_BRAIN] != 0)
	{
		t.asParts[COMP_BRAIN] = 0;
		t.numWeaps = 0;
		for (unsigned i = 0; i < MAX_WEAPONS; ++i) { t.asWeaps[i] = 0; }
		t.modSlots.reset();
	}
	t.asParts[COMP_CONSTRUCT] = 0;
	if (isTurretRepair(t)) { t.asParts[COMP_REPAIRUNIT] = 0; }
	if (t.modSlots.numSys == 0 && t.numWeaps == 0)
	{
		if (isTurretSensor(t))
		{
			t.modSlots.sysComp[0] = COMP_SENSOR; t.modSlots.sysStat[0] = t.asParts[COMP_SENSOR]; t.modSlots.sysConn[0] = 0;
			t.modSlots.numSys = 1;
		}
		else if (isTurretEcm(t))
		{
			t.modSlots.sysComp[0] = COMP_ECM; t.modSlots.sysStat[0] = t.asParts[COMP_ECM]; t.modSlots.sysConn[0] = 0;
			t.modSlots.numSys = 1;
		}
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
		if (t.modSlots.sysConn[i] == slot) { removeSys(t.modSlots, static_cast<unsigned>(i)); }
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
	if (comp != COMP_WEAPON && comp != COMP_SENSOR && comp != COMP_ECM) { return; }
	toSlotForm(t);
	removeAt(t, slot);
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
		const bool ok = (comp == COMP_SENSOR) ? stat < asSensorStats.size() : stat < asECMStats.size();
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
		removeSys(t.modSlots, static_cast<unsigned>(o.index));
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
	if (!usable(t))
	{
		t.modSlots.reset();
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
			if (t.modSlots.sysConn[i] >= static_cast<int>(n)) { removeSys(t.modSlots, static_cast<unsigned>(i)); }
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
		const bool ok = (s.sysComp[i] == COMP_SENSOR && s.sysStat[i] < asSensorStats.size())
		             || (s.sysComp[i] == COMP_ECM && s.sysStat[i] < asECMStats.size());
		if (!ok || s.sysConn[i] < 0) { removeSys(s, static_cast<unsigned>(i)); }
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
		if (s.sysComp[0] == COMP_SENSOR)
		{
			t.asParts[COMP_SENSOR] = s.sysStat[0];
			if (isTurretEcm(t)) { t.asParts[COMP_ECM] = static_cast<uint16_t>(defaultEcm(player)); }
		}
		else
		{
			t.asParts[COMP_ECM] = s.sysStat[0];
			if (isTurretSensor(t)) { t.asParts[COMP_SENSOR] = static_cast<uint16_t>(defaultSensor(player)); }
		}
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
		else
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
	t.asParts[COMP_CONSTRUCT] = 0;
	if (isTurretRepair(t)) { t.asParts[COMP_REPAIRUNIT] = 0; }
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
		if (t.asParts[COMP_BRAIN] != 0 || t.asParts[COMP_CONSTRUCT] != 0 || isTurretRepair(t)) { return fail("construction/repair/command turrets cannot be combined"); }
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
		const bool ok = (s.sysComp[i] == COMP_SENSOR && s.sysStat[i] < asSensorStats.size())
		             || (s.sysComp[i] == COMP_ECM && s.sysStat[i] < asECMStats.size());
		if (!ok) { return fail("invalid system turret"); }
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
		const bool sensor = s.sysComp[i] == COMP_SENSOR;
		const COMPONENT_STATS *cs = nullptr;
		if (sensor && s.sysStat[i] < asSensorStats.size()) { cs = &asSensorStats[s.sysStat[i]]; }
		if (!sensor && s.sysStat[i] < asECMStats.size()) { cs = &asECMStats[s.sysStat[i]]; }
		if (cs == nullptr) { continue; }
		e["type"] = sensor ? "sensor" : "ecm";
		e["id"] = cs->id.toUtf8();
		e["slot"] = static_cast<int>(s.sysConn[i]);
		sys.push_back(e);
	}
	if (!sys.empty()) { j["systems"] = sys; }
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
			const bool sensor = e.value("type", std::string("sensor")) != "ecm";
			const int idx = getCompFromID(sensor ? COMP_SENSOR : COMP_ECM, WzString::fromUtf8(e.value("id", std::string())));
			const int slot = e.value("slot", -1);
			if (idx < 0 || slot < 0 || slot >= static_cast<int>(MAX_WEAPONS)) { continue; }
			s.sysComp[s.numSys] = static_cast<uint8_t>(sensor ? COMP_SENSOR : COMP_ECM);
			s.sysStat[s.numSys] = static_cast<uint16_t>(idx);
			s.sysConn[s.numSys] = static_cast<int8_t>(slot);
			s.numSys = static_cast<uint8_t>(s.numSys + 1);
		}
	}
	return true;
}

}
