// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: "Fully autonomous" (siehe modauto.h)

#include "modauto.h"
#include "modcrew.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"

#include "ai.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modcontrol.h"
#include "modlayer.h"
#include "order.h"
#include "orderdef.h"
#include "structure.h"

#include <cstdint>

namespace modauto
{
namespace
{
constexpr unsigned STEP_MS = 2000;
constexpr int HUNT_TILES = 26;
unsigned s_next = 0;

bool candidate(const DROID *d, const BASE_OBJECT *o, unsigned player, int64_t &bestD2)
{
	if (o == nullptr || o->died || o->visible[player] != UBYTE_MAX || !modlayer::canInteract(d, o))
	{
		return false;
	}
	const int64_t dx = o->pos.x - d->pos.x, dy = o->pos.y - d->pos.y;
	const int64_t dd = dx * dx + dy * dy;
	if (dd >= bestD2)
	{
		return false;
	}
	bestD2 = dd;
	return true;
}
}

bool supported(const DROID *psDroid)
{
	return psDroid != nullptr && !psDroid->isTransporter() && !psDroid->isVtol() && psDroid->numWeaps > 0 && !modcrew::crewless(psDroid);   // ohne Besatzung: nichts
}

bool isOn(const DROID *psDroid)
{
	return psDroid != nullptr && (psDroid->secondaryOrder & DSS_FULLAUTO_MASK) != 0;
}

void tick(GameWorld &world)
{
	if (gameTime < s_next)
	{
		return;
	}
	s_next = gameTime + STEP_MS;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : world.objects.droids[p])
		{
			if (d == nullptr || d->died || !isOn(d) || !supported(d) || modcontrol::manual(d))
			{
				continue;
			}
			if (d->order.type != DORDER_NONE && d->order.type != DORDER_GUARD)
			{
				continue;   // hat schon etwas zu tun (auch Rueckzug/Reparatur)
			}
			if (d->action == DACTION_ATTACK || d->action == DACTION_MOVETOATTACK)
			{
				continue;   // kaempft gerade
			}
			int64_t best = int64_t(world_coord(HUNT_TILES)) * world_coord(HUNT_TILES);
			const BASE_OBJECT *target = nullptr;
			for (unsigned q = 0; q < MAX_PLAYERS; ++q)
			{
				if (q == p || aiCheckAlliances(p, q))
				{
					continue;
				}
				for (const DROID *e : world.objects.droids[q])
				{
					if (candidate(d, e, p, best)) { target = e; }
				}
				for (const STRUCTURE *s : world.objects.structures[q])
				{
					if (candidate(d, s, p, best)) { target = s; }
				}
			}
			if (target != nullptr)
			{
				orderDroidLoc(d, DORDER_SCOUT, target->pos.x, target->pos.y, ModeImmediate);
			}
		}
	}
}

void reset()
{
	s_next = 0;
}
}
