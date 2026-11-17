// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Minen (siehe modmines.h)
#include "modmines.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/math_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "ai.h"
#include "display.h"
#include "droid.h"
#include "effects.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "mapgrid.h"
#include "modlayer.h"
#include "objmem.h"
#include "stats.h"
#include "structure.h"

#include <vector>

namespace modmines
{
namespace
{
const STRUCTURE_STATS *s_mine = nullptr;
bool s_looked = false;

constexpr int TRIGGER_R = DIST_TILE * 45 / 100;   // Ausloesen: Bodeneinheit fast auf der Mine
constexpr int BLAST_R = DIST_TILE * 9 / 10;       // Wirkung
constexpr int SPOT_R = DIST_TILE * 3 / 2;         // direkt davor sieht man sie
constexpr int SWEEP_R = DIST_TILE * 4;            // Sensor-Fahrzeug: Minensuche
constexpr unsigned BLAST_DMG = 340;

const STRUCTURE_STATS *mineStats()
{
	if (!s_looked)
	{
		s_looked = true;
		const int idx = getStructStatFromName(WzString::fromUtf8("A0Mine"));
		s_mine = idx >= 0 ? &asStructureStats[idx] : nullptr;
	}
	return s_mine;
}

bool groundUnit(const DROID *d)
{
	return d != nullptr && !d->died && !d->isFlying() && !d->isVtol() && !d->isTransporter() && !modlayer::isFlier(d);
}

void blast(STRUCTURE *mine)
{
	const int x = mine->pos.x, y = mine->pos.y;
	const int z = map_Height(gameWorld.map, x, y);
	const unsigned it = gameTime - deltaGameTime / 2 + 1;
	// Anzeige: Erdfontaene, Feuerball, Rauch
	Vector3i c(x, z + 8, y);
	addEffect(&c, EFFECT_EXPLOSION, EXPLOSION_TYPE_LARGE, false, nullptr, 0, it);
	for (int i = 0; i < 4; ++i)
	{
		Vector3i p(x + ((i & 1) ? 22 : -22), z + 14 + 10 * i, y + ((i & 2) ? 18 : -18));
		addEffect(&p, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM, false, nullptr, 0, it);
	}
	Vector3i sp(x, z + 30, y);
	addEffect(&sp, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH, false, nullptr, 0, it);
	audio_PlayStaticTrack(x, y, ID_SOUND_EXPLOSION);
	// Wirkung: Bodeneinheiten schwer, Bauwerke wenig (andere Minen koennen mitgehen)
	std::vector<BASE_OBJECT *> hit;
	for (BASE_OBJECT *o : gridStartIterate(x, y, static_cast<uint32_t>(BLAST_R)))
	{
		if (o != nullptr && !o->died && o != mine && modlayer::canInteract(mine, o)) { hit.push_back(o); }
	}
	for (BASE_OBJECT *o : hit)
	{
		if (o->died) { continue; }
		const int dist = iHypot(Vector2i(o->pos.x - x, o->pos.y - y));
		if (dist > BLAST_R) { continue; }
		const unsigned dmg = BLAST_DMG * static_cast<unsigned>(100 - 60 * dist / BLAST_R) / 100u;
		if (o->type == OBJ_DROID)
		{
			DROID *d = static_cast<DROID *>(o);
			if (!groundUnit(d)) { continue; }
			droidDamage(gameWorld, d, nullptr, dmg, WC_KINETIC, WSC_BOMB, it, false, 0, false);
		}
		else if (o->type == OBJ_STRUCTURE)
		{
			structureDamage(gameWorld, static_cast<STRUCTURE *>(o), nullptr, dmg / 4u, WC_KINETIC, WSC_BOMB, it, false, 0, false);
		}
	}
	if (!mine->died) { destroyStruct(mine, it, gameWorld); }
}
}

void tick()
{
	const STRUCTURE_STATS *ms = mineStats();
	if (ms == nullptr) { return; }
	std::vector<STRUCTURE *> boom;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s == nullptr || s->died || s->pStructureType != ms) { continue; }
			bool spotted[MAX_PLAYERS] = {};
			bool trigger = false;
			for (BASE_OBJECT *o : gridStartIterate(s->pos.x, s->pos.y, static_cast<uint32_t>(SWEEP_R)))
			{
				if (o == nullptr || o->died || o->type != OBJ_DROID || o->player >= MAX_PLAYERS || aiCheckAlliances(o->player, p)) { continue; }
				const DROID *d = static_cast<const DROID *>(o);
				if (!modlayer::canInteract(s, d)) { continue; }
				const int dist = iHypot(Vector2i(d->pos.x - s->pos.x, d->pos.y - s->pos.y));
				if (dist <= SPOT_R || (d->droidType == DROID_SENSOR && dist <= SWEEP_R)) { spotted[d->player] = true; }
				if (s->status == SS_BUILT && dist <= TRIGGER_R && groundUnit(d)) { trigger = true; }
			}
			// fuer fremde Spieler verstecken (Verbuendete sehen die Minen ihrer Partner)
			for (unsigned q = 0; q < MAX_PLAYERS; ++q)
			{
				if (q == p || aiCheckAlliances(q, p) || spotted[q]) { continue; }
				s->visible[q] = 0;
			}
			if (trigger) { boom.push_back(s); }
		}
	}
	for (STRUCTURE *s : boom)
	{
		if (!s->died) { blast(s); }
	}
}

void reset()
{
	s_mine = nullptr;
	s_looked = false;
}
}
