// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Invade (siehe modinvade.h)

#include "modinvade.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"

#include "ai.h"
#include "baseobject.h"
#include "console.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modlayerdig.h"
#include "multiplay.h"
#include "orderdef.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace modinvade
{
namespace
{
constexpr unsigned CHECK_MS = 250;
constexpr int MAX_ENHANCER = 5;

struct Bore
{
	uint32_t structId = 0;
	unsigned structPlayer = 0;
	unsigned from = 0;
	unsigned until = 0;
};

std::map<uint32_t, Bore> s_bores;
unsigned s_lastCheck = 0;
int s_enhancer[MAX_PLAYERS] = {};

bool isInvadable(const STRUCTURE *s)
{
	if (s == nullptr || s->died || s->status != SS_BUILT) { return false; }
	switch (s->pStructureType->type)
	{
	case REF_WALL:
	case REF_WALLCORNER:
	case REF_GATE:
		return false;
	default:
		break;
	}
	return !s->pStructureType->combinesWithWall;
}

bool isEnhancerStructure(const STRUCTURE *s)
{
	switch (s->pStructureType->type)
	{
	case REF_HQ:
	case REF_RESEARCH:
	case REF_FACTORY:
	case REF_CYBORG_FACTORY:
	case REF_VTOL_FACTORY:
		return true;
	default:
		return false;
	}
}

int halfFootprint(const STRUCTURE *s)
{
	const Vector2i sz = s->size();
	return std::max(sz.x, sz.y) * TILE_UNITS / 2;
}

/// naechstes einnehmbares Gebaeude in Wandreichweite des Fahrzeugs
STRUCTURE *nearestTarget(const DROID *d)
{
	STRUCTURE *best = nullptr;
	long bestD = 0;
	for (int q = 0; q < MAX_PLAYERS; q++)
	{
		if (q == static_cast<int>(d->player) || aiCheckAlliances(q, d->player)) { continue; }
		for (STRUCTURE *s : gameWorld.objects.structures[q])
		{
			if (!isInvadable(s) || modlayer::layerOf(s) != modlayer::layerOf(d)) { continue; }
			const long dx = s->pos.x - d->pos.x, dy = s->pos.y - d->pos.y;
			const long reach = halfFootprint(s) + TILE_UNITS;          // bis eine Kachel vor der Wand
			const long dd = dx * dx + dy * dy;
			if (dd > reach * reach) { continue; }
			if (best == nullptr || dd < bestD) { best = s; bestD = dd; }
		}
	}
	return best;
}

unsigned boreMs(const STRUCTURE *s)
{
	const unsigned ms = 6000u + static_cast<unsigned>(std::max(0, static_cast<int>(s->body))) * 12u;
	return std::min(60000u, std::max(8000u, ms));
}
}

bool canInvade(const DROID *psDroid, const BASE_OBJECT *psObj)
{
	if (psDroid == nullptr || psObj == nullptr || psObj->type != OBJ_STRUCTURE || !modlayerdig::isDrill(psDroid)) { return false; }
	const STRUCTURE *s = static_cast<const STRUCTURE *>(psObj);
	if (!isInvadable(s) || s->player == psDroid->player || aiCheckAlliances(s->player, psDroid->player)) { return false; }
	return modlayer::layerOf(s) == modlayer::layerOf(psDroid);
}

bool approachPoint(const DROID *psDroid, const BASE_OBJECT *psObj, Vector2i &worldPos)
{
	if (!canInvade(psDroid, psObj)) { return false; }
	const STRUCTURE *s = static_cast<const STRUCTURE *>(psObj);
	double dx = psDroid->pos.x - s->pos.x, dy = psDroid->pos.y - s->pos.y;
	double len = std::sqrt(dx * dx + dy * dy);
	if (len < 1.0) { dx = 1.0; dy = 0.0; len = 1.0; }
	const double dist = halfFootprint(s) + 56.0;                     // dicht an der Wand, aber noch ausserhalb des gesperrten Feldes
	worldPos = Vector2i(static_cast<int>(s->pos.x + dx / len * dist), static_cast<int>(s->pos.y + dy / len * dist));
	return true;
}

void tick(GameWorld &world)
{
	if (gameTime - s_lastCheck < CHECK_MS) { return; }
	s_lastCheck = gameTime;

	struct Capture { uint32_t structId; unsigned structPlayer; unsigned newOwner; };
	std::vector<Capture> done;
	std::vector<uint32_t> keep;
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (DROID *d : world.objects.droids[p])
		{
			if (d->died || d->droidType != DROID_CONSTRUCT || !modlayerdig::isDrill(d)) { continue; }
			auto it = s_bores.find(d->id);
			const bool idle = d->order.type == DORDER_NONE && d->action == DACTION_NONE;
			STRUCTURE *tgt = idle ? nearestTarget(d) : nullptr;
			if (tgt == nullptr)
			{
				if (it != s_bores.end()) { s_bores.erase(it); }
				continue;
			}
			if (it == s_bores.end() || it->second.structId != tgt->id)
			{
				Bore b;
				b.structId = tgt->id; b.structPlayer = tgt->player; b.from = gameTime; b.until = gameTime + boreMs(tgt);
				s_bores[d->id] = b;
				if (d->player == selectedPlayer)
				{
					addConsoleMessage("Drill truck is boring through the wall...", DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
				}
				continue;
			}
			if (gameTime >= it->second.until)
			{
				done.push_back({tgt->id, tgt->player, static_cast<unsigned>(d->player)});
				s_bores.erase(it);
			}
		}
	}
	for (const Capture &c : done)
	{
		STRUCTURE *s = IdToStruct(c.structId, c.structPlayer);
		if (s == nullptr || s->died || s->player != c.structPlayer) { continue; }
		const bool enh = isEnhancerStructure(s);
		(void)giftSingleStructure(s, static_cast<UBYTE>(c.newOwner), false, false);
		if (enh && s_enhancer[c.newOwner] < MAX_ENHANCER) { s_enhancer[c.newOwner]++; }
		if (c.newOwner == selectedPlayer)
		{
			addConsoleMessage(enh ? "Building captured! Enhancer gained." : "Building captured!", DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		}
	}
}

int enhancer(unsigned player) { return player < MAX_PLAYERS ? s_enhancer[player] : 0; }
int enhancerPercent(unsigned player) { return enhancer(player) * 6; }

int boreProgress(const DROID *psDroid)
{
	if (psDroid == nullptr) { return -1; }
	const auto it = s_bores.find(psDroid->id);
	if (it == s_bores.end() || it->second.until <= it->second.from) { return -1; }
	const unsigned span = it->second.until - it->second.from;
	return static_cast<int>(std::min(100u, (gameTime - it->second.from) * 100u / span));
}

void reset()
{
	s_bores.clear();
	s_lastCheck = 0;
	for (int &e : s_enhancer) { e = 0; }
}
}
