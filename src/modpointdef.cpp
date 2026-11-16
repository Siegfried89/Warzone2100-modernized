// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Punktverteidigung (Metal Fatigue) - siehe modpointdef.h
#include "modpointdef.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "ai.h"
#include "effects.h"
#include "game_world.h"
#include "modlayer.h"
#include "objmem.h"
#include "projectile.h"
#include "random.h"
#include "stats.h"
#include "structure.h"

#include <unordered_map>
#include <vector>

namespace modpointdef
{
namespace
{
constexpr unsigned COOLDOWN_MS = 1200;
constexpr unsigned CHANCE_PCT = 60;
constexpr int MIN_AGE_MS = 150;                 // nicht schon im Lauf der Abschussrampe abfangen

struct Station
{
	const STRUCTURE *s;
	int range;
	modlayer::Layer layer;
};

int s_wpn = -2;                                 // Index der Waffe "MFPointDefense" (-2 = noch nicht gesucht, -1 = gibt es nicht)
uint32_t s_tick = UINT32_MAX;
std::vector<Station> s_stations;
std::unordered_map<uint32_t, uint32_t> s_ready;   // Bau-ID -> fruehester naechster Abschuss

int pdWeapon()
{
	if (s_wpn == -2)
	{
		s_wpn = -1;
		for (unsigned i = 0; i < asWeaponStats.size(); i++)
		{
			if (asWeaponStats[i].id == "MFPointDefense") { s_wpn = static_cast<int>(i); break; }
		}
	}
	return s_wpn;
}

bool interceptable(const WEAPON_STATS *w)
{
	if (w == nullptr) { return false; }
	if (w->movementModel == MM_INDIRECT || w->movementModel == MM_HOMINGINDIRECT) { return true; }
	switch (w->weaponSubClass)
	{
	case WSC_MISSILE: case WSC_ROCKET: case WSC_SLOWMISSILE: case WSC_SLOWROCKET:
	case WSC_MORTARS: case WSC_HOWITZERS: case WSC_BOMB:
		return true;
	default:
		return false;
	}
}
}

void beginTick()
{
	if (s_tick == gameTime) { return; }
	s_tick = gameTime;
	s_stations.clear();
	const int wpn = pdWeapon();
	if (wpn < 0) { return; }
	for (unsigned p = 0; p < MAX_PLAYERS; p++)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || s->numWeaps == 0 || static_cast<int>(s->asWeaps[0].nStat) != wpn) { continue; }
			s_stations.push_back({s, proj_GetLongRange(asWeaponStats[wpn], static_cast<int>(p)), modlayer::layerOf(s)});
		}
	}
}

bool tryIntercept(PROJECTILE *proj)
{
	if (s_stations.empty() || proj == nullptr || proj->state != PROJ_INFLIGHT || !interceptable(proj->psWStats)) { return false; }
	if (static_cast<int>(gameTime - proj->born) < MIN_AGE_MS) { return false; }
	const int owner = static_cast<int>(proj->player);
	const modlayer::Layer layer = modlayer::projectileLayer(proj);
	for (const Station &st : s_stations)
	{
		const int sp = static_cast<int>(st.s->player);
		if (sp == owner || aiCheckAlliancesInRange(sp, owner) || st.layer != layer) { continue; }
		const Vector2i d = proj->pos.xy() - st.s->pos.xy();
		if (std::abs(d.x) > st.range || std::abs(d.y) > st.range || iHypot(d) > st.range) { continue; }
		uint32_t &ready = s_ready[st.s->id];
		if (gameTime < ready) { continue; }
		ready = gameTime + COOLDOWN_MS;
		if (gameRand(100) >= CHANCE_PCT) { continue; }   // daneben - diese Stellung muss nachladen
		// abgeschossen: kleine Explosion in der Luft (nur Anzeige), Knall
		const Vector3i ep(proj->pos.x, proj->pos.z, proj->pos.y);
		addEffect(&ep, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL, false, nullptr, 0, gameTime);
		audio_PlayStaticTrack(proj->pos.x, proj->pos.y, ID_SOUND_EXPLOSION_SMALL);
		return true;
	}
	return false;
}

void reset()
{
	s_wpn = -2;
	s_tick = UINT32_MAX;
	s_stations.clear();
	s_ready.clear();
}
}
