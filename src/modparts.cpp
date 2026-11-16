// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Teile-Schaden (siehe modparts.h)

#include "modparts.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "action.h"
#include "droid.h"
#include "effects.h"
#include "game_world.h"
#include "map.h"
#include "modarty.h"
#include "modcombot.h"
#include "modvoice.h"
#include "move.h"
#include "objmem.h"
#include "order.h"
#include "projectile.h"
#include "random.h"
#include "stats.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/transform.hpp>   // glm::rotate/translate ohne Matrix-Argument

namespace modparts
{
namespace
{
constexpr unsigned TICK_MS = 250;
constexpr int TURRET_PERCENT = 12;      // Lebenspunkte des Turms in Prozent der Rumpf-Lebenspunkte
constexpr int TRACK_PERCENT = 14;       // Lebenspunkte des Laufwerks
constexpr int MIN_PART_HP = 30;

struct State
{
	unsigned player = 0;
	int turretHp = 0, turretMax = 0;
	int trackHp = 0, trackMax = 0;
	unsigned lastBody = 0;
	bool turretGone = false, trackGone = false;
	bool turretPending = false, trackPending = false;
	uint32_t shooterId = 0;
	unsigned shooterPlayer = 0;
	unsigned savedStat[MAX_WEAPONS] = {};
	unsigned savedAmmo[MAX_WEAPONS] = {};
};

std::unordered_map<uint32_t, State> s_state;
unsigned s_nextTick = 0;
unsigned s_smokeRound = 0;

bool hasTurret(const DROID *d)
{
	return d->numWeaps > 0 && d->asWeaps[0].nStat > 0;
}

State &stateOf(const DROID *d)
{
	auto it = s_state.find(d->id);
	if (it != s_state.end()) { return it->second; }
	State s;
	s.player = d->player;
	s.turretMax = std::max(MIN_PART_HP, static_cast<int>(d->originalBody) * TURRET_PERCENT / 100);
	s.trackMax = std::max(MIN_PART_HP, static_cast<int>(d->originalBody) * TRACK_PERCENT / 100);
	s.turretHp = s.turretMax;
	s.trackHp = s.trackMax;
	s.lastBody = d->body;
	return s_state.emplace(d->id, s).first->second;
}

bool visibleAt(int x, int y)
{
	if (!worldOnMap(gameWorld.map, x, y)) { return false; }
	MAPTILE *t = mapTile(gameWorld.map, map_coord(x), map_coord(y));
	return t != nullptr && TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(t);
}

/// ein Truemmerstueck (Halterung oder Waffe) fliegt hoch, dreht sich, zieht eine Rauchspur und explodiert beim Aufprall
void launchPiece(const iIMDBaseShape *base, const Vector3i &pos)
{
	if (base == nullptr) { return; }
	Vector3f vel(static_cast<float>(rand() % 520 - 260), 0.f, static_cast<float>(rand() % 520 - 260));
	addEffect(&pos, EFFECT_GRAVITON, GRAVITON_TYPE_TURRET, true, base->displayModel(), 0, graphicsTime, nullptr, &vel);
}

void loseTurret(DROID *d, State &s, GameWorld &world)
{
	for (unsigned i = 0; i < MAX_WEAPONS; ++i)
	{
		s.savedStat[i] = d->asWeaps[i].nStat;
		s.savedAmmo[i] = d->asWeaps[i].ammo;
	}
	const bool show = visibleAt(d->pos.x, d->pos.y);
	const Vector3i top(d->pos.x, d->pos.z + 45, d->pos.y);
	for (unsigned i = 0; i < d->numWeaps && i < MAX_WEAPONS; ++i)
	{
		const unsigned stat = d->asWeaps[i].nStat;
		if (stat == 0) { continue; }
		if (show && stat < asWeaponStats.size())
		{
			const WEAPON_STATS &ws = asWeaponStats[stat];
			launchPiece(ws.pMountGraphic, top);
			launchPiece(ws.pIMD, top);
		}
		d->asWeaps[i].nStat = 0;
		d->asWeaps[i].ammo = 0;
	}
	if (show)
	{
		addEffect(&top, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM, false, nullptr, 0);
		for (int k = 0; k < 3; ++k)
		{
			const Vector3i p(top.x + rand() % 40 - 20, top.y + rand() % 30, top.z + rand() % 40 - 20);
			addEffect(&p, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING, false, nullptr, 0);
		}
	}
	if (d->action == DACTION_ATTACK || d->action == DACTION_MOVETOATTACK || d->action == DACTION_ROTATETOATTACK || d->action == DACTION_MOVEFIRE)
	{
		actionDroid(d, DACTION_NONE);
	}
	if (d->order.type == DORDER_ATTACK)
	{
		orderDroid(d, DORDER_STOP, ModeImmediate);
	}
	// der Schuetze jubelt
	if (s.shooterId != 0)
	{
		const DROID *shooter = IdToDroid(world.objects, s.shooterId, s.shooterPlayer);
		if (shooter != nullptr && !shooter->died) { modvoice::onPartShot(shooter); }
	}
}

void loseTracks(DROID *d)
{
	orderDroid(d, DORDER_STOP, ModeImmediate);
	moveReallyStopDroid(d);
	if (visibleAt(d->pos.x, d->pos.y))
	{
		const Vector3i p(d->pos.x, d->pos.z + 20, d->pos.y);
		addEffect(&p, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL, false, nullptr, 0);
		for (int k = 0; k < 2; ++k)
		{
			const Vector3i q(p.x + rand() % 60 - 30, p.y + rand() % 20, p.z + rand() % 60 - 30);
			addEffect(&q, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING, false, nullptr, 0);
		}
	}
}

void restore(DROID *d, State &s)
{
	if (s.turretGone)
	{
		for (unsigned i = 0; i < MAX_WEAPONS; ++i)
		{
			if (s.savedStat[i] != 0)
			{
				d->asWeaps[i].nStat = s.savedStat[i];
				d->asWeaps[i].ammo = s.savedAmmo[i];
			}
		}
		modarty::refresh(d);   // Artillerie-Modus: neu aufgesetzter Turm folgt dem eingestellten Modus
	}
}
} // namespace

bool applies(const DROID *d)
{
	if (d == nullptr || d->died) { return false; }
	if (d->droidType == DROID_PERSON || d->isCyborg() || d->isTransporter() || d->isVtol()) { return false; }
	if (modcombot::isModular(d)) { return false; }
	const PROPULSION_STATS *ps = d->getPropulsionStats();
	return ps != nullptr && !ps->alwaysFlying;
}

unsigned scaleDamage(const DROID *d, unsigned damage)
{
	if (s_state.empty() || d == nullptr) { return damage; }
	const auto it = s_state.find(d->id);
	if (it != s_state.end() && it->second.turretGone && it->second.trackGone) { return damage + damage / 2; }
	return damage;
}

void onHit(DROID *d, int hpLost, const PROJECTILE *proj)
{
	if (proj == nullptr || hpLost <= 0 || !applies(d)) { return; }
	State &s = stateOf(d);
	const bool arty = proj->psWStats != nullptr && (proj->psWStats->movementModel == MM_INDIRECT || proj->psWStats->weaponSubClass == WSC_MORTARS
	                  || proj->psWStats->weaponSubClass == WSC_HOWITZERS);
	int wTurret = arty ? 40 : 26;
	int wTrack = arty ? 12 : 26;
	if (s.turretGone || s.turretPending || (!hasTurret(d))) { wTurret = 0; }
	if (s.trackGone || s.trackPending) { wTrack = 0; }
	const int roll = static_cast<int>(gameRand(100));
	if (roll < wTurret)
	{
		s.turretHp -= hpLost;
		if (s.turretHp <= 0) { s.turretHp = 0; s.turretPending = true; }
	}
	else if (roll < wTurret + wTrack)
	{
		s.trackHp -= hpLost;
		if (s.trackHp <= 0) { s.trackHp = 0; s.trackPending = true; }
	}
	s.lastBody = d->body;
	if (proj->psSource != nullptr && proj->psSource->type == OBJ_DROID)
	{
		s.shooterId = proj->psSource->id;
		s.shooterPlayer = proj->psSource->player;
	}
}

bool immobile(const DROID *d)
{
	if (s_state.empty() || d == nullptr) { return false; }
	const auto it = s_state.find(d->id);
	return it != s_state.end() && it->second.trackGone;
}

bool turretGone(const DROID *d)
{
	if (s_state.empty() || d == nullptr) { return false; }
	const auto it = s_state.find(d->id);
	return it != s_state.end() && it->second.turretGone;
}

bool slump(const DROID *d, glm::mat4 &out)
{
	if (s_state.empty() || d == nullptr) { return false; }
	const auto it = s_state.find(d->id);
	if (it == s_state.end() || !it->second.trackGone) { return false; }
	const float side = (d->id & 1) ? 1.f : -1.f;
	out = glm::translate(glm::vec3(0.f, -4.f, 0.f))
	      * glm::rotate(glm::radians(side * 3.2f), glm::vec3(0.f, 0.f, 1.f))
	      * glm::rotate(glm::radians(1.6f), glm::vec3(1.f, 0.f, 0.f));
	return true;
}

int partPercent(const DROID *d, bool turret)
{
	if (d == nullptr || !applies(d)) { return -1; }
	if (turret && !hasTurret(d) && !turretGone(d)) { return -1; }
	const auto it = s_state.find(d->id);
	if (it == s_state.end()) { return 100; }
	const State &s = it->second;
	const int hp = turret ? s.turretHp : s.trackHp;
	const int mx = turret ? s.turretMax : s.trackMax;
	return std::max(0, std::min(100, hp * 100 / std::max(1, mx)));
}

void tick(GameWorld &world)
{
	if (gameTime < s_nextTick) { return; }
	s_nextTick = gameTime + TICK_MS;
	if (s_state.empty()) { return; }
	++s_smokeRound;
	for (auto it = s_state.begin(); it != s_state.end();)
	{
		State &s = it->second;
		DROID *d = IdToDroid(world.objects, it->first, s.player);
		if (d == nullptr || d->died) { it = s_state.erase(it); continue; }
		if (s.turretPending)
		{
			s.turretPending = false;
			s.turretGone = true;
			loseTurret(d, s, world);
		}
		if (s.trackPending)
		{
			s.trackPending = false;
			s.trackGone = true;
			loseTracks(d);
		}
		// Heilung: die Teile heilen mit dem Rumpf mit, zerstoerte Teile kommen erst bei voller Gesundheit zurueck
		if (d->body > s.lastBody && d->originalBody > 0)
		{
			const int delta = static_cast<int>(d->body - s.lastBody);
			if (!s.turretGone) { s.turretHp = std::min(s.turretMax, s.turretHp + delta * s.turretMax / static_cast<int>(d->originalBody)); }
			if (!s.trackGone) { s.trackHp = std::min(s.trackMax, s.trackHp + delta * s.trackMax / static_cast<int>(d->originalBody)); }
		}
		s.lastBody = d->body;
		if (d->body >= d->originalBody)
		{
			restore(d, s);
			it = s_state.erase(it);
			continue;
		}
		if (s.trackGone)
		{
			// bleibt stehen, auch wenn sich noch etwas bewegt (Rutschen, alte Wegpunkte)
			if (d->sMove.Status != MOVEINACTIVE && d->sMove.Status != MOVEHOVER)
			{
				moveReallyStopDroid(d);
			}
			// qualmt (nur Anzeige); mit zerstoertem Turm zusaetzlich dichter
			if (visibleAt(d->pos.x, d->pos.y) && ((s_smokeRound + d->id) % (s.turretGone ? 2u : 3u)) == 0)
			{
				const Vector3i p(d->pos.x + rand() % 50 - 25, d->pos.z + 25, d->pos.y + rand() % 50 - 25);
				addEffect(&p, EFFECT_SMOKE, s.turretGone ? SMOKE_TYPE_BILLOW : SMOKE_TYPE_DRIFTING, false, nullptr, 0);
			}
		}
		++it;
	}
}

void reset()
{
	s_state.clear();
	s_nextTick = 0;
}
}
