// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Aussenposten-Teleport (Metal Fatigue) - siehe modoutpost.h
#include "modoutpost.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "droid.h"
#include "effects.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "objmem.h"
#include "order.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace modoutpost
{
namespace
{
constexpr unsigned CHECK_MS = 250;
constexpr unsigned POST_COOLDOWN_MS = 4000;
constexpr unsigned UNIT_COOLDOWN_MS = 10000;

uint32_t s_next = 0;
std::unordered_map<uint32_t, uint32_t> s_postReady;   // Posten-ID -> naechster Sprung frei
std::unordered_map<uint32_t, uint32_t> s_unitReady;   // Einheit-ID -> naechster Sprung frei

}

bool isOutpost(const STRUCTURE *s)
{
	if (s == nullptr || s->died || s->status != SS_BUILT || s->pStructureType == nullptr) { return false; }
	const WzString &id = s->pStructureType->id;
	return id.startsWith("MFS_") && s->pStructureType->name.startsWith("Outpost");
}

namespace
{

void flash(const Vector3i &p)
{
	const Vector3i ep(p.x, p.z + 40, p.y);
	addEffect(&ep, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER, false, nullptr, 0, gameTime);
	addEffect(&ep, EFFECT_EXPLOSION, EXPLOSION_TYPE_SHOCKWAVE, false, nullptr, 0, gameTime);
	audio_PlayStaticTrack(p.x, p.y, ID_SOUND_LASER_HEAVY);
}
}

void tick(GameWorld &world)
{
	if (&world != &gameWorld || gameTime < s_next) { return; }
	s_next = gameTime + CHECK_MS;
	for (unsigned p = 0; p < MAX_PLAYERS; p++)
	{
		std::vector<STRUCTURE *> posts;
		for (STRUCTURE *s : world.objects.structures[p]) { if (isOutpost(s)) { posts.push_back(s); } }
		if (posts.size() < 2) { continue; }
		std::sort(posts.begin(), posts.end(), [](const STRUCTURE *a, const STRUCTURE *b) { return a->id < b->id; });
		for (DROID *d : world.objects.droids[p])
		{
			if (d->died || d->isVtol() || d->isTransporter() || modlayer::inTransition(d)) { continue; }
			if (d->order.type != DORDER_MOVE && d->order.type != DORDER_SCOUT) { continue; }
			const auto ur = s_unitReady.find(d->id);
			if (ur != s_unitReady.end() && gameTime < ur->second) { continue; }
			for (size_t i = 0; i < posts.size(); i++)
			{
				STRUCTURE *src = posts[i];
				if (modlayer::layerOf(src) != modlayer::layerOf(d)) { continue; }
				// Ziel des Befehls liegt auf dem Posten (Fussabdruck + 1 Feld) und die Einheit ist angekommen
				const Vector2i sz = src->size();
				const int reach = world_coord(std::max(sz.x, sz.y)) / 2 + TILE_UNITS;
				if (iHypot(d->order.pos - src->pos.xy()) > reach || iHypot(d->pos.xy() - src->pos.xy()) > reach + TILE_UNITS / 2) { continue; }
				uint32_t &pr = s_postReady[src->id];
				if (gameTime < pr) { break; }
				STRUCTURE *dst = nullptr;
				for (size_t k = 1; k < posts.size() && dst == nullptr; k++)
				{
					STRUCTURE *c = posts[(i + k) % posts.size()];
					if (modlayer::layerOf(c) == modlayer::layerOf(d)) { dst = c; }
				}
				if (dst == nullptr) { break; }
				UDWORD tx = map_coord(dst->pos.x), ty = map_coord(dst->pos.y);
				if (!pickATileGen(world, &tx, &ty, LOOK_FOR_EMPTY_TILE, zonedPAT)) { break; }
				const Vector3i from = d->pos;
				orderDroid(d, DORDER_STOP, ModeImmediate);
				droidSetPosition(d, world.map, world_coord(tx) + TILE_UNITS / 2, world_coord(ty) + TILE_UNITS / 2);
				pr = gameTime + POST_COOLDOWN_MS;
				s_unitReady[d->id] = gameTime + UNIT_COOLDOWN_MS;
				flash(from);
				flash(d->pos);
				break;
			}
		}
	}
}

void reset()
{
	s_next = 0;
	s_postReady.clear();
	s_unitReady.clear();
}
}
