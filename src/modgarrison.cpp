// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Haeuser/Hochhaeuser besetzen (siehe modgarrison.h)

#include "modgarrison.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"

#include "baseobject.h"
#include "droid.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "modoil.h"
#include "random.h"
#include "stats.h"

#include <vector>

namespace modgarrison
{
void onFeatureRemoved(FEATURE *psFeature, GameWorld &world)
{
	if (psFeature == nullptr || psFeature->psStats == nullptr || !psFeature->psStats->infantryPassable) { return; }

	const Vector2i size = psFeature->psStats->size();
	const Vector2i first = map_coord(Vector2i(psFeature->pos.x, psFeature->pos.y)) - size / 2;
	const Vector2i last = first + size;   // exklusiv

	std::vector<DROID *> victims;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : world.objects.droids[p])
		{
			if (d == nullptr || d->died) { continue; }
			if (!modoil::isInfantry(d)) { continue; }
			const int tx = map_coord(d->pos.x);
			const int ty = map_coord(d->pos.y);
			if (tx >= first.x && tx < last.x && ty >= first.y && ty < last.y)
			{
				victims.push_back(d);
			}
		}
	}

	for (DROID *d : victims)
	{
		// an den Rand des Gebaeudes setzen (zufaellige Seite), damit die Sterbeanimation "am Fenster" gespielt wird
		const int side = static_cast<int>(gameRand(4));
		const int along = static_cast<int>(gameRand(TILE_UNITS));
		int x = d->pos.x;
		int y = d->pos.y;
		const int minX = first.x * TILE_UNITS;
		const int maxX = last.x * TILE_UNITS;
		const int minY = first.y * TILE_UNITS;
		const int maxY = last.y * TILE_UNITS;
		switch (side)
		{
		case 0: x = minX + along;                     y = minY; break;
		case 1: x = minX + along;                     y = maxY; break;
		case 2: x = minX;                             y = minY + along; break;
		default: x = maxX;                            y = minY + along; break;
		}
		d->pos.x = x;
		d->pos.y = y;
		d->pos.z = map_Height(world.map, x, y);
		destroyDroid(d, gameTime, world);
	}
}
}
