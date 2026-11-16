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
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "power.h"
#include "random.h"
#include "stats.h"
#include "structure.h"

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
		if (d->died || !modlayer::isFlier(d) || modlayer::inTransition(d)) { continue; }
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
	}
}

void reset()
{
	for (unsigned p = 0; p < MAX_PLAYERS; p++) { s_next[p] = s_nextFortify[p] = s_nextOrbit[p] = 0; }
}
}
