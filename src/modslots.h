// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 02:16): Turm-Plaetze ("Waffenplaetze") frei belegen.
//  - bis zu 30 Plaetze je Rumpf (MAX_WEAPONS), jeder Platz ist ein Anschluss des Rumpf-Modells (beim Verbundrumpf ein gesetzter Waffenplatz)
//  - Waffen UND Systemtuerme zugleich: Sensoren (auch Schild-Tuerme, das sind Sensoren mit Schild) und Stoersender neben Waffen
//    (z.B. Fatboy-Nachbau mit Schildturm und Sensor). (Wunsch 24.11. 01:26 "Systemtuerme und Waffentuerme gleichzeitig platzierbar
//    machen ... ueberall"): auch EIN Bau- ODER Reparaturturm neben Waffen und Sensoren - das Fahrzeug baut bzw. repariert dann und
//    schiesst trotzdem. Nur der Kommandoturm bleibt allein.
//  - Die Waffen bleiben dicht in asWeaps[0..numWeaps-1] (alle Kampf-Regeln unveraendert); hier steht nur, an welchem Anschluss sie sitzen,
//    und die zusaetzlichen Systemtuerme. Der beste Sensor (Reichweite) und der beste Stoersender stehen wie gewohnt in asParts (Sicht, Ortung).
//  - Nur in Einzelspieler-/Skirmish-Partien (Netzwerk: Vorlagen werden ohne diese Belegung verschickt -> wie bisher).
#pragma once

#include "basedef.h"   // MAX_WEAPONS
#include <cstdint>
#include <string>

struct DROID_TEMPLATE;
struct DROID;
struct SENSOR_STATS;

constexpr unsigned MOD_MAX_SYS = 16;  ///< Systemtuerme neben Waffen je Fahrzeug (24.11.: 16, Rumpfe haben bis 30 Plaetze)

struct ModSlots
{
	int8_t weapConn[MAX_WEAPONS];     ///< Anschluss je Waffe; -1 = wie bisher (Waffe i an Anschluss i)
	uint8_t numSys = 0;               ///< Systemtuerme in der Belegung (0 = Fahrzeug wie bisher)
	uint8_t sysComp[MOD_MAX_SYS];     ///< COMP_SENSOR, COMP_ECM, COMP_CONSTRUCT oder COMP_REPAIRUNIT (Bau/Reparatur hoechstens einer)
	uint16_t sysStat[MOD_MAX_SYS];    ///< Index in asSensorStats / asECMStats / asConstructStats / asRepairStats
	int8_t sysConn[MOD_MAX_SYS];      ///< Anschluss

	ModSlots() { reset(); }
	void reset()
	{
		for (unsigned i = 0; i < MAX_WEAPONS; ++i) { weapConn[i] = -1; }
		numSys = 0;
		for (unsigned i = 0; i < MOD_MAX_SYS; ++i) { sysComp[i] = 0; sysStat[i] = 0; sysConn[i] = -1; }
	}
	bool isDefault() const
	{
		if (numSys != 0) { return false; }
		for (unsigned i = 0; i < MAX_WEAPONS; ++i) { if (weapConn[i] >= 0 && weapConn[i] != static_cast<int>(i)) { return false; } }
		return true;
	}
	/// Anschluss der Waffe i
	int conn(int weapon) const { return (weapon >= 0 && weapon < static_cast<int>(MAX_WEAPONS) && weapConn[weapon] >= 0) ? weapConn[weapon] : weapon; }
	bool operator==(const ModSlots &o) const
	{
		if (numSys != o.numSys) { return false; }
		for (unsigned i = 0; i < MAX_WEAPONS; ++i) { if (conn(static_cast<int>(i)) != o.conn(static_cast<int>(i))) { return false; } }
		for (unsigned i = 0; i < numSys; ++i)
		{
			if (sysComp[i] != o.sysComp[i] || sysStat[i] != o.sysStat[i] || sysConn[i] != o.sysConn[i]) { return false; }
		}
		return true;
	}
	bool operator!=(const ModSlots &o) const { return !(*this == o); }
};

namespace modslots
{

enum Kind
{
	SLOT_EMPTY = 0,
	SLOT_WEAPON,      ///< index = Waffe
	SLOT_SYSTEM,      ///< index = Eintrag in sys (Sensor/Stoersender/Bau/Reparatur neben anderen Tuermen)
	SLOT_SOLE,        ///< Fahrzeug wie bisher mit einem einzigen Systemturm (Sensor, Stoersender, Bau, Reparatur, Kommando) auf Platz 1
};

struct Occupant
{
	Kind kind = SLOT_EMPTY;
	int index = -1;
	int comp = -1;          ///< COMPONENT_TYPE
	unsigned stat = 0;
};

/// Zahl der Turm-Plaetze des Rumpfes (Anschluesse), hoechstens MAX_WEAPONS
unsigned slotCount(const DROID_TEMPLATE &t);
/// freie Belegung moeglich? (Rumpf mit mindestens zwei Plaetzen, kein VTOL/Transporter/Cyborg, keine Netzwerk-Partie)
bool usable(const DROID_TEMPLATE &t);
/// was sitzt auf Platz slot
Occupant occupant(const DROID_TEMPLATE &t, int slot);
/// Platz belegen: comp = COMP_WEAPON, COMP_SENSOR, COMP_ECM, COMP_CONSTRUCT oder COMP_REPAIRUNIT (Kommandoturm geht den bisherigen Weg)
void assign(DROID_TEMPLATE &t, int slot, int comp, unsigned stat, unsigned player);
/// Platz leeren
void clearSlot(DROID_TEMPLATE &t, int slot, unsigned player);
/// nach einem Rumpf-Wechsel: was auf Plaetzen jenseits des neuen Rumpfes sitzt, faellt weg (ohne freie Belegung: Standardform)
void fitToBody(DROID_TEMPLATE &t, unsigned player);
/// Belegung aufraeumen: bester Sensor/Stoersender in asParts, Standardform wenn nichts verschoben ist
void normalize(DROID_TEMPLATE &t, unsigned player);
/// Belegung gueltig? (Zahl der Tuerme, Plaetze, keine Doppelbelegung) - why = Grund fuer das Log
bool valid(const DROID_TEMPLATE &t, std::string *why = nullptr);
/// alle Systemtuerme erforscht? (allowRedundant wie researchedTemplate)
bool researched(const DROID_TEMPLATE &t, unsigned player, bool allowRedundant);

/// Systemtuerme zusaetzlich zu denen in asParts (fuer Kosten, Gewicht, Lebenspunkte): f(comp, stat)
template <typename F>
void forEachExtra(const ModSlots &s, const uint16_t *asParts, F &&f)
{
	bool skip[COMP_NUMCOMPONENTS] = {};
	skip[COMP_SENSOR] = skip[COMP_ECM] = skip[COMP_CONSTRUCT] = skip[COMP_REPAIRUNIT] = true;
	for (unsigned i = 0; i < s.numSys && i < MOD_MAX_SYS; ++i)
	{
		const int c = s.sysComp[i];
		if (c >= 0 && c < COMP_NUMCOMPONENTS && skip[c] && s.sysStat[i] == asParts[c]) { skip[c] = false; continue; }   // steht schon in asParts
		f(c, s.sysStat[i]);
	}
}

/// Schild-Sensor eines Fahrzeugs unter den zusaetzlichen Systemtuermen (staerkster Schild), nullptr = keiner
const SENSOR_STATS *extraShieldSensor(const DROID *d);

}
