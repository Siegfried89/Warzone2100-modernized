// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Computer-Spieler benutzen die Mod-Systeme - siehe modai.h
#include "modai.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/netplay.h"
#include "ai.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modbuild.h"
#include "modfuel.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "modrocket.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "power.h"
#include "random.h"
#include "stats.h"
#include "structure.h"
#include "template.h"

#include <algorithm>
#include <string>
#include <vector>

namespace modai
{
namespace
{
constexpr unsigned CYCLE_MS = 20000;            // jede KI denkt alle 20 s nach (gestaffelt)
constexpr unsigned FORTIFY_EVERY_MS = 240000;   // Befestigungsbau hoechstens alle 4 min
constexpr unsigned ORBIT_EVERY_MS = 45000;

unsigned s_next[MAX_PLAYERS] = {};
unsigned s_nextFortify[MAX_PLAYERS] = {};
unsigned s_nextOrbit[MAX_PLAYERS] = {};

bool isAI(unsigned p)
{
	if (p >= MAX_PLAYERS || p >= static_cast<unsigned>(game.maxPlayers)) { return false; }
	if (static_cast<int>(p) == scavengerPlayer()) { return false; }
	return !isHumanPlayer(static_cast<int>(p)) && NetPlay.players[p].ai >= 0 && !NetPlay.players[p].isSpectator;
}

STRUCTURE_STATS *available(unsigned p, const char *id)
{
	const int idx = getStructStatFromName(WzString::fromUtf8(id));
	if (idx < 0 || apStructTypeLists[p] == nullptr || apStructTypeLists[p][idx] != AVAILABLE) { return nullptr; }
	return &asStructureStats[idx];
}

STRUCTURE_STATS *availableOfType(unsigned p, STRUCTURE_TYPE t, const char *exclude = nullptr)
{
	for (unsigned i = 0; i < numStructureStats; i++)
	{
		if (asStructureStats[i].type != t || apStructTypeLists[p] == nullptr || apStructTypeLists[p][i] != AVAILABLE) { continue; }
		if (exclude != nullptr && asStructureStats[i].id.toUtf8().find(exclude) != std::string::npos) { continue; }
		return &asStructureStats[i];
	}
	return nullptr;
}

bool baseCenter(GameWorld &w, unsigned p, Vector2i &out)
{
	const STRUCTURE *best = nullptr;
	for (const STRUCTURE *s : w.objects.structures[p])
	{
		if (s->died || s->status != SS_BUILT || modlayer::layerOf(s) != modlayer::SURFACE) { continue; }
		if (s->pStructureType->type == REF_HQ) { best = s; break; }
		if (best == nullptr && (s->pStructureType->type == REF_FACTORY || s->pStructureType->type == REF_POWER_GEN)) { best = s; }
	}
	if (best == nullptr) { return false; }
	out = best->pos.xy();
	return true;
}

DROID *idleTruck(GameWorld &w, unsigned p)
{
	for (DROID *d : w.objects.droids[p])
	{
		if (d->died || !d->isConstructionDroid() || modlayer::layerOf(d) != modlayer::SURFACE) { continue; }
		if (d->order.type == DORDER_NONE && d->action == DACTION_NONE && !modbuild::isInfantryBuilder(d)) { return d; }
	}
	return nullptr;
}

/// freier Bauplatz (mit einem Feld Luft rundherum) in Spiralen um die Mitte
bool findSpot(GameWorld &w, unsigned p, STRUCTURE_STATS *st, const Vector2i &center, int minR, int maxR, Vector2i &out)
{
	const int cx = map_coord(center.x), cy = map_coord(center.y);
	const Vector2i size = st->size(0);
	for (int r = minR; r <= maxR; r++)
	{
		for (int k = 0; k < 8 * r; k++)
		{
			// Startpunkt je Ring gewuerfelt, damit nicht alle KIs gleich bauen
			const int q = (k + static_cast<int>(gameRand(static_cast<uint32_t>(8 * r)))) % (8 * r);
			int dx, dy;
			if (q < 2 * r) { dx = -r + q; dy = -r; }
			else if (q < 4 * r) { dx = r; dy = -r + (q - 2 * r); }
			else if (q < 6 * r) { dx = r - (q - 4 * r); dy = r; }
			else { dx = -r; dy = r - (q - 6 * r); }
			const int tx = cx + dx, ty = cy + dy;
			if (tx < 3 || ty < 3 || tx + size.x + 3 >= w.map.width || ty + size.y + 3 >= w.map.height) { continue; }
			bool free = true;
			for (int y = ty - 1; y <= ty + size.y && free; y++)
			{
				for (int x = tx - 1; x <= tx + size.x && free; x++) { free = !TileIsOccupied(mapTile(w.map, x, y)); }
			}
			if (!free) { continue; }
			const Vector2i wp(world_coord(tx) + world_coord(size.x) / 2, world_coord(ty) + world_coord(size.y) / 2);
			if (validLocation(w, st, wp, 0, p, true)) { out = wp; return true; }
		}
	}
	return false;
}

bool hasStructure(GameWorld &w, unsigned p, STRUCTURE_TYPE t, const char *exclude = nullptr)
{
	for (const STRUCTURE *s : w.objects.structures[p])
	{
		if (s->died || s->pStructureType->type != t) { continue; }
		if (exclude != nullptr && s->pStructureType->id.toUtf8().find(exclude) != std::string::npos) { continue; }
		return true;
	}
	return false;
}

bool tryFuelStation(GameWorld &w, unsigned p, const Vector2i &base)
{
	if (hasStructure(w, p, REF_REPAIR_FACILITY, "Gantry")) { return false; }
	int vehicles = 0;
	for (const DROID *d : w.objects.droids[p]) { if (modfuel::usesFuel(d)) { vehicles++; } }
	if (vehicles < 4) { return false; }
	STRUCTURE_STATS *st = availableOfType(p, REF_REPAIR_FACILITY, "Gantry");
	if (st == nullptr || getPower(p) < static_cast<int>(st->powerToBuild / 2u)) { return false; }
	DROID *t = idleTruck(w, p);
	Vector2i at;
	if (t == nullptr || !findSpot(w, p, st, base, 4, 12, at)) { return false; }
	orderDroidStatsLocDir(t, DORDER_BUILD, st, at.x, at.y, 0, ModeImmediate);
	return true;
}

bool tryFortify(GameWorld &w, unsigned p, const Vector2i &base)
{
	if (gameTime < s_nextFortify[p] || gameTime < 4 * 60000) { return false; }
	if (getPower(p) < 400) { return false; }
	int trucks = 0;
	for (const DROID *d : w.objects.droids[p]) { if (d->isConstructionDroid() && !d->died) { trucks++; } }
	if (trucks < 3) { return false; }
	DROID *t = idleTruck(w, p);
	if (t == nullptr || !modbuild::canFortify(t)) { return false; }
	s_nextFortify[p] = gameTime + FORTIFY_EVERY_MS;
	const int tier = gameTime < 15 * 60000 ? 1 : (gameTime < 35 * 60000 ? 2 : 3);
	DroidOrder o(DORDER_FORTIFY, base);
	o.direction = static_cast<uint16_t>(tier);
	orderDroidBase(t, &o);
	return true;
}

bool tryOutpost(GameWorld &w, unsigned p, const Vector2i &base)
{
	STRUCTURE_STATS *st = available(p, "A0BaBaOutpost");
	if (st == nullptr || getPower(p) < 250) { return false; }
	for (const STRUCTURE *der : w.objects.structures[p])
	{
		if (der->died || der->status != SS_BUILT || der->pStructureType->type != REF_RESOURCE_EXTRACTOR) { continue; }
		if (iHypot(der->pos.xy() - base) < 16 * DIST_TILE) { continue; }
		bool covered = false;
		for (const STRUCTURE *s : w.objects.structures[p])
		{
			if (!s->died && s->pStructureType->id.toUtf8().rfind("A0BaBaOutpost", 0) == 0 && iHypot(s->pos.xy() - der->pos.xy()) < 8 * DIST_TILE) { covered = true; break; }
		}
		if (covered) { continue; }
		DROID *t = idleTruck(w, p);
		Vector2i at;
		if (t == nullptr || !findSpot(w, p, st, der->pos.xy(), 2, 6, at)) { return false; }
		orderDroidStatsLocDir(t, DORDER_BUILD, st, at.x, at.y, 0, ModeImmediate);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------- Stufe 2 (16.11.): Konzern-Einheiten
// Jede KI gehoert einem MF-Konzern (Spieler-Nr. reihum Rimtech, Mil-Agro, Neuropa) und baut zwischendurch dessen Combot,
// Kampfpanzer und Artillerie (aus templates.json: MFP_<Konzern>, MFTank_<Konzern>, MFArty_<Konzern>). Nur in einer gerade
// untaetigen Fabrik (die Skript-KI fuellt sie sonst selbst), hoechstens ein Auftrag je Runde und KI, hoechstens 14 solche Einheiten.
constexpr unsigned MF_UNITS_MAX = 14;
unsigned s_nextMF[MAX_PLAYERS] = {};

DROID_TEMPLATE *findTemplate(unsigned p, const char *id)
{
	const WzString wid = WzString::fromUtf8(id);
	DROID_TEMPLATE *found = nullptr;
	enumerateTemplates(static_cast<int>(p), [&](DROID_TEMPLATE *t) {
		if (t->id == wid) { found = t; return false; }
		return true;
	});
	return found;
}

bool tryCorpUnits(GameWorld &w, unsigned p)
{
	if (gameTime < s_nextMF[p] || gameTime < 3 * 60000) { return false; }
	static const char *corp[3] = {"Rimtech", "MilAgro", "Neuropa"};
	const std::string c = corp[p % 3];
	const std::string ids[3] = {"MFP_" + c, "MFTank_" + c, "MFArty_" + c};
	unsigned have = 0;
	for (const DROID *d : w.objects.droids[p])
	{
		if (d->died || d->getBodyStats() == nullptr) { continue; }
		const std::string b = d->getBodyStats()->id.toUtf8();
		if (b.rfind("B3MFTank", 0) == 0 || b.rfind("B3MFArty", 0) == 0 || b.rfind("MFT_", 0) == 0) { have++; }
	}
	if (have >= MF_UNITS_MAX) { return false; }
	// Mischung: 3 Combots : 2 Panzer : 1 Artillerie
	static const int pick[6] = {0, 1, 0, 2, 1, 0};
	const int want = pick[(gameTime / CYCLE_MS + p) % 6];
	for (int k = 0; k < 3; k++)
	{
		DROID_TEMPLATE *t = findTemplate(p, ids[(want + k) % 3].c_str());
		if (t == nullptr || !researchedTemplate(t, static_cast<int>(p), true)) { continue; }
		if (getPower(p) < static_cast<int>(calcTemplatePower(t))) { return false; }
		for (STRUCTURE *s : w.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !s->isFactory() || !s->isIdle() || modlayer::layerOf(s) != modlayer::SURFACE) { continue; }
			if (!validTemplateForFactory(t, s, false)) { continue; }
			if (structSetManufacture(s, t, ModeImmediate))
			{
				s_nextMF[p] = gameTime + 50000;
				return true;
			}
		}
	}
	return false;
}

void orbitStrike(GameWorld &w, unsigned p)
{
	if (gameTime < s_nextOrbit[p]) { return; }
	s_nextOrbit[p] = gameTime + ORBIT_EVERY_MS;
	// hat ein Gegner etwas im Orbit?
	BASE_OBJECT *target = nullptr;
	for (unsigned e = 0; e < MAX_PLAYERS && target == nullptr; e++)
	{
		if (e == p || aiCheckAlliances(p, e)) { continue; }
		for (STRUCTURE *s : w.objects.structures[e]) { if (!s->died && modlayer::layerOf(s) == modlayer::ORBIT) { target = s; break; } }
		if (target == nullptr) { for (DROID *d : w.objects.droids[e]) { if (!d->died && modlayer::layerOf(d) == modlayer::ORBIT) { target = d; break; } } }
	}
	std::vector<DROID *> up, there;
	for (DROID *d : w.objects.droids[p])
	{
		if (d->died || !modlayer::isFlier(d) || modlayer::inTransition(d) || d->isTransporter()) { continue; }   // Transporter fliegen nur auf Befehl in den Orbit
		const modlayer::Layer l = modlayer::layerOf(d);
		if (l == modlayer::ORBIT) { there.push_back(d); }
		else if (l == modlayer::SURFACE && d->order.type == DORDER_NONE && (!d->isVtol() || !vtolEmpty(d))) { up.push_back(d); }
	}
	if (target != nullptr && up.size() >= 3)
	{
		// die Haelfte der untaetigen Flieger steigt auf
		for (size_t i = 0; i < up.size() / 2; i++) { modlayer::requestLayer(up[i], modlayer::ORBIT); }
	}
	// Flieger im Orbit: untaetige greifen das naechste Ziel an, ohne Ziel kehren sie zurueck
	for (DROID *d : there)
	{
		if (d->order.type != DORDER_NONE) { continue; }
		if (target != nullptr) { orderDroidObj(d, DORDER_ATTACK, target, ModeImmediate); }
		else { modlayer::requestLayer(d, modlayer::SURFACE); }
	}
}

// ---------------------------------------------------------------- Stufe 3 (17.11. 22:57 "alles abarbeiten"; offen seit 16.11.: KI im
// Untergrund / Solar im Orbit): die KI benutzt die Ebenen und die neuen Kraftwerke.
//  - Wasserkraftwerk am Wasser (hoechstens 2), sobald erforscht
//  - Schacht bei der Basis, ein Lkw faehrt hinunter und baut im Untergrund Uranbergwerke (hoechstens 2); mit Uran ein Atomkraftwerk an der
//    Basis, spaeter den 2. Kuehlturm
//  - Raketensilo, Cyborg-Pioniere (bei Bedarf in der Cyborg-Fabrik gebaut) steigen ein; im Orbit bauen sie Solarpaneele (hoechstens 8)
//    und einen NASDA-Lasersatelliten
// Alles nur mit untaetigen Einheiten, gameRand und Spielzustand (mehrspielersicher), hoechstens ein Auftrag je Bereich und Runde.
unsigned s_nextHydro[MAX_PLAYERS] = {};
unsigned s_nextDown[MAX_PLAYERS] = {};
unsigned s_nextUp[MAX_PLAYERS] = {};

bool idIs(const STRUCTURE *s, const char *prefix)
{
	return s->pStructureType->id.toUtf8().rfind(prefix, 0) == 0;
}

int countOwn(GameWorld &w, unsigned p, const char *prefix, bool builtOnly = false, int layer = -1)
{
	int n = 0;
	for (const STRUCTURE *s : w.objects.structures[p])
	{
		if (s->died || !idIs(s, prefix) || (builtOnly && s->status != SS_BUILT)) { continue; }
		if (layer >= 0 && static_cast<int>(modlayer::layerOf(s)) != layer) { continue; }
		n++;
	}
	return n;
}

STRUCTURE *firstOwn(GameWorld &w, unsigned p, const char *prefix, bool builtOnly)
{
	for (STRUCTURE *s : w.objects.structures[p])
	{
		if (!s->died && idIs(s, prefix) && (!builtOnly || s->status == SS_BUILT)) { return s; }
	}
	return nullptr;
}

bool idleIn(const DROID *d, modlayer::Layer l)
{
	return !d->died && d->isConstructionDroid() && modlayer::layerOf(d) == l && !modlayer::inTransition(d) && !modrocket::aboard(d)
	       && d->order.type == DORDER_NONE && d->action == DACTION_NONE && !modbuild::isInfantryBuilder(d);
}

bool affordable(unsigned p, const STRUCTURE_STATS *st, int reserve = 100)
{
	return getPower(p) >= static_cast<int>(st->powerToBuild) + reserve;
}

/// Bauplatz in der Karte einer anderen Ebene suchen
bool findSpotIn(GameWorld &w, modlayer::Layer l, unsigned p, STRUCTURE_STATS *st, const Vector2i &center, int minR, int maxR, Vector2i &out)
{
	modlayermap::Scope sc(l);
	return findSpot(w, p, st, center, minR, maxR, out);
}

bool tryHydro(GameWorld &w, unsigned p, const Vector2i &base)
{
	if (gameTime < s_nextHydro[p]) { return false; }
	STRUCTURE_STATS *st = available(p, "A0HydroPlant");
	if (st == nullptr || countOwn(w, p, "A0HydroPlant") >= 2 || !affordable(p, st)) { return false; }
	DROID *t = idleTruck(w, p);
	if (t == nullptr) { return false; }
	Vector2i at;
	if (!findSpot(w, p, st, base, 3, 24, at))
	{
		s_nextHydro[p] = gameTime + 180000;   // kein Wasser in der Naehe: erst in 3 min wieder suchen
		return false;
	}
	orderDroidStatsLocDir(t, DORDER_BUILD, st, at.x, at.y, 0, ModeImmediate);
	s_nextHydro[p] = gameTime + 60000;
	return true;
}

bool tryUnderground(GameWorld &w, unsigned p, const Vector2i &base)
{
	if (gameTime < s_nextDown[p] || gameTime < 10 * 60000) { return false; }
	STRUCTURE *shaft = firstOwn(w, p, "A0LayerShaft", false);
	if (shaft == nullptr)
	{
		STRUCTURE_STATS *st = available(p, "A0LayerShaft");
		if (st == nullptr || !affordable(p, st, 300)) { return false; }
		DROID *t = idleTruck(w, p);
		Vector2i at;
		if (t == nullptr || !findSpot(w, p, st, base, 5, 12, at)) { return false; }
		orderDroidStatsLocDir(t, DORDER_BUILD, st, at.x, at.y, 0, ModeImmediate);
		s_nextDown[p] = gameTime + 60000;
		return true;
	}
	if (shaft->status != SS_BUILT || modlayer::layerOf(shaft) != modlayer::SURFACE) { return false; }
	// (19.11.) Uran kommt von Harvestern an den Lavafeldern im Untergrund (das Uranbergwerk gibt es nicht mehr): einen Harvester bauen
	// und durch den Schacht hinunterschicken; unten faehrt er von selbst zum Lavafeld (modlava).
	int harvesters = 0;
	DROID *idleUp = nullptr;
	for (DROID *d : w.objects.droids[p])
	{
		if (d->died || d->getBodyStats() == nullptr || !d->getBodyStats()->id.startsWith("HarvesterBody")) { continue; }
		harvesters++;
		if (idleUp == nullptr && modlayer::layerOf(d) == modlayer::SURFACE && d->order.type == DORDER_NONE) { idleUp = d; }
	}
	if (idleUp != nullptr && modlayer::isPortalTile(static_cast<int>(p), map_coord(shaft->pos.x), map_coord(shaft->pos.y)))
	{
		orderDroidLoc(idleUp, DORDER_MOVE, shaft->pos.x, shaft->pos.y, ModeImmediate);
		s_nextDown[p] = gameTime + 40000;
		return true;
	}
	if (harvesters >= 2) { return false; }
	DROID_TEMPLATE *t = findTemplate(p, "Harvester");
	if (t == nullptr || !researchedTemplate(t, static_cast<int>(p), true) || getPower(p) < static_cast<int>(calcTemplatePower(t)) + 150) { return false; }
	for (STRUCTURE *s : w.objects.structures[p])
	{
		if (s->died || s->status != SS_BUILT || !s->isFactory() || !s->isIdle() || modlayer::layerOf(s) != modlayer::SURFACE) { continue; }
		if (!validTemplateForFactory(t, s, false)) { continue; }
		if (structSetManufacture(s, t, ModeImmediate)) { s_nextDown[p] = gameTime + 60000; return true; }
	}
	return false;
}

bool tryNuclear(GameWorld &w, unsigned p, const Vector2i &base)
{
	// (19.11.) erst wenn ein Harvester im Untergrund Uran schuerft
	bool harvester = false;
	for (const DROID *d : w.objects.droids[p])
	{
		if (!d->died && d->getBodyStats() != nullptr && d->getBodyStats()->id.startsWith("HarvesterBody") && modlayer::layerOf(d) == modlayer::UNDERGROUND) { harvester = true; break; }
	}
	if (!harvester) { return false; }
	STRUCTURE *plant = firstOwn(w, p, "A0NuclearPlant", false);
	if (plant == nullptr)
	{
		STRUCTURE_STATS *st = available(p, "A0NuclearPlant");
		if (st == nullptr || !affordable(p, st, 200)) { return false; }
		DROID *t = idleTruck(w, p);
		Vector2i at;
		if (t == nullptr || !findSpot(w, p, st, base, 4, 14, at)) { return false; }
		orderDroidStatsLocDir(t, DORDER_BUILD, st, at.x, at.y, 0, ModeImmediate);
		return true;
	}
	// 2. Kuehlturm: Ausbau auf das fertige Kraftwerk setzen
	if (plant->status != SS_BUILT || idIs(plant, "A0NuclearPlant2")) { return false; }
	STRUCTURE_STATS *up = available(p, "A0NuclearPlant2");
	if (up == nullptr || !affordable(p, up, 300)) { return false; }
	DROID *t = idleTruck(w, p);
	if (t == nullptr || !validLocation(w, up, plant->pos.xy(), 0, p, true)) { return false; }
	orderDroidStatsLocDir(t, DORDER_BUILD, up, plant->pos.x, plant->pos.y, 0, ModeImmediate);
	return true;
}

bool tryOrbitBase(GameWorld &w, unsigned p, const Vector2i &base)
{
	if (gameTime < s_nextUp[p] || gameTime < 14 * 60000) { return false; }
	// Pioniere im Orbit: Solarpaneele, dann ein Lasersatellit
	int crewUp = 0;
	DROID *idleUp = nullptr;
	for (DROID *d : w.objects.droids[p])
	{
		if (d->died || d->droidType != DROID_CYBORG_CONSTRUCT || modlayer::layerOf(d) != modlayer::ORBIT) { continue; }
		crewUp++;
		if (idleUp == nullptr && idleIn(d, modlayer::ORBIT)) { idleUp = d; }
	}
	if (idleUp != nullptr)
	{
		STRUCTURE_STATS *solar = available(p, "A0SolarPanel");
		STRUCTURE_STATS *sat = available(p, "A0LaserSatellite");
		STRUCTURE_STATS *st = nullptr;
		if (solar != nullptr && countOwn(w, p, "A0SolarPanel", false, modlayer::ORBIT) < 8 && affordable(p, solar)) { st = solar; }
		else if (sat != nullptr && countOwn(w, p, "A0LaserSatellite") == 0 && affordable(p, sat, 400)) { st = sat; }
		Vector2i at;
		if (st != nullptr && findSpotIn(w, modlayer::ORBIT, p, st, idleUp->pos.xy(), 1, 7, at))
		{
			orderDroidStatsLocDir(idleUp, DORDER_BUILD, st, at.x, at.y, 0, ModeImmediate);
			s_nextUp[p] = gameTime + 30000;
			return true;
		}
	}
	// Raketensilo an der Basis
	STRUCTURE *silo = firstOwn(w, p, "A0RocketSilo", false);
	if (silo == nullptr)
	{
		STRUCTURE_STATS *st = available(p, "A0RocketSilo");
		if (st == nullptr || !affordable(p, st, 400)) { return false; }
		DROID *t = idleTruck(w, p);
		Vector2i at;
		if (t == nullptr || !findSpot(w, p, st, base, 5, 14, at)) { return false; }
		orderDroidStatsLocDir(t, DORDER_BUILD, st, at.x, at.y, 0, ModeImmediate);
		s_nextUp[p] = gameTime + 60000;
		return true;
	}
	if (silo->status != SS_BUILT || crewUp >= 3) { return false; }
	// untaetige Cyborg-Pioniere zum Silo schicken (sie steigen ein, sobald die Rakete bereitsteht)
	int sent = 0;
	for (DROID *d : w.objects.droids[p])
	{
		if (sent >= 2) { break; }
		if (d->died || d->droidType != DROID_CYBORG_CONSTRUCT || !idleIn(d, modlayer::SURFACE)) { continue; }
		orderDroidLoc(d, DORDER_MOVE, silo->pos.x + TILE_UNITS, silo->pos.y + TILE_UNITS, ModeImmediate);
		sent++;
	}
	if (sent > 0) { s_nextUp[p] = gameTime + 40000; return true; }
	// keine Pioniere: einen in einer untaetigen Cyborg-Fabrik bauen
	int engineers = 0;
	for (const DROID *d : w.objects.droids[p]) { if (!d->died && d->droidType == DROID_CYBORG_CONSTRUCT) { engineers++; } }
	if (engineers >= 2) { return false; }
	DROID_TEMPLATE *t = findTemplate(p, "Cyb-ComEng");
	if (t == nullptr || !researchedTemplate(t, static_cast<int>(p), true) || getPower(p) < static_cast<int>(calcTemplatePower(t)) + 200) { return false; }
	for (STRUCTURE *s : w.objects.structures[p])
	{
		if (s->died || s->status != SS_BUILT || !s->isFactory() || !s->isIdle() || modlayer::layerOf(s) != modlayer::SURFACE) { continue; }
		if (!validTemplateForFactory(t, s, false)) { continue; }
		if (structSetManufacture(s, t, ModeImmediate)) { s_nextUp[p] = gameTime + 45000; return true; }
	}
	return false;
}
}

void tick(GameWorld &world)
{
	if (&world != &gameWorld || !bMultiPlayer) { return; }   // nur Gefecht/Mehrspieler (die Kampagne hat ihre eigene Skript-KI)
	for (unsigned p = 0; p < MAX_PLAYERS; p++)
	{
		if (!isAI(p)) { continue; }
		if (s_next[p] == 0) { s_next[p] = 30000 + p * 2500; }
		if (gameTime < s_next[p]) { continue; }
		s_next[p] = gameTime + CYCLE_MS;
		Vector2i base;
		if (!baseCenter(world, p, base)) { continue; }
		if (!tryFuelStation(world, p, base))
		{
			if (!tryFortify(world, p, base)) { tryOutpost(world, p, base); }
		}
		orbitStrike(world, p);
		tryCorpUnits(world, p);
		// Stufe 3: je Runde hoechstens ein Bauauftrag fuer Kraftwerke/Ebenen
		if (!tryHydro(world, p, base) && !tryNuclear(world, p, base) && !tryUnderground(world, p, base)) { tryOrbitBase(world, p, base); }
	}
}

void reset()
{
	for (unsigned p = 0; p < MAX_PLAYERS; p++) { s_next[p] = s_nextFortify[p] = s_nextOrbit[p] = s_nextMF[p] = s_nextHydro[p] = s_nextDown[p] = s_nextUp[p] = 0; }
}
}
