// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Panzer wie in Sudden Strike - Feuerhalt der Kanone und Rumpf-MG (siehe modtankguns.h)

#include "modtankguns.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "ai.h"
#include "droid.h"
#include "effects.h"
#include "game_world.h"
#include "modlayer.h"
#include "modcrew.h"
#include "projectile.h"
#include "random.h"
#include "stats.h"
#include "statsdef.h"

#include <unordered_map>

namespace modtankguns
{
namespace
{
constexpr unsigned HALT_BEFORE_MS = 1200;   // so lange darf das Abbremsen vor dem Schuss hoechstens dauern
constexpr unsigned HALT_AFTER_MS = 650;     // so lange bleibt der Panzer nach dem Schuss stehen
constexpr int STOP_SPEED = 12;              // darunter gilt der Panzer als stehend
constexpr int ARC = DEG(25);                // Rumpf-MG: Schussbogen vor dem Bug
constexpr unsigned BURST_SHOTS = 5, SHOT_MS = 110, BURST_PAUSE_MS = 1400;

struct Gun { unsigned next = 0; unsigned shots = 0; };
std::unordered_map<uint32_t, unsigned> s_haltUntil;
std::unordered_map<uint32_t, Gun> s_guns;
int s_mgStat = -2;

bool mainIsCannon(const WEAPON_STATS *st)
{
	return st != nullptr && (st->weaponSubClass == WSC_CANNON || st->weaponSubClass == WSC_GAUSS);
}

int mgStat()
{
	if (s_mgStat == -2)
	{
		s_mgStat = -1;
		for (const char *id : {"TankHullMG", "MG2Mk1", "MG1Mk1", "BabaMG"})   // TankHullMG: tiefer Ton (hullmg.py)
		{
			const int i = getCompFromID(COMP_WEAPON, WzString::fromUtf8(id));
			if (i >= 0) { s_mgStat = i; break; }
		}
	}
	return s_mgStat;
}
}

bool isTank(const DROID *d)
{
	if (d == nullptr || d->died || d->droidType != DROID_WEAPON || d->isVtol() || d->isTransporter() || d->numWeaps == 0) { return false; }
	const BODY_STATS *b = d->getBodyStats();
	if (b == nullptr || b->size < SIZE_MEDIUM) { return false; }
	const PROPULSION_STATS *p = d->getPropulsionStats();
	if (p == nullptr || p->alwaysFlying) { return false; }
	if (p->propulsionType != PROPULSION_TYPE_TRACKED && p->propulsionType != PROPULSION_TYPE_HALF_TRACKED && p->propulsionType != PROPULSION_TYPE_WHEELED) { return false; }
	const WEAPON_STATS *w = d->getWeaponStats(0);
	return w != nullptr && w->weaponSubClass != WSC_MGUN;
}

bool readyToFire(DROID *d, const WEAPON_STATS *st)
{
	if (!mainIsCannon(st) || !isTank(d)) { return true; }
	const bool moving = d->sMove.Status != MOVEINACTIVE && d->sMove.Status != MOVEHOVER && std::abs(d->sMove.speed) > STOP_SPEED;
	if (!moving) { return true; }
	auto it = s_haltUntil.find(d->id);
	if (it == s_haltUntil.end() || gameTime >= it->second)
	{
		s_haltUntil[d->id] = gameTime + HALT_BEFORE_MS;   // anhalten, dann feuern
	}
	return false;
}

void afterFire(DROID *d, const WEAPON_STATS *st)
{
	if (!mainIsCannon(st) || !isTank(d)) { return; }
	s_haltUntil[d->id] = gameTime + HALT_AFTER_MS;
}

bool halted(const DROID *d)
{
	if (d == nullptr || s_haltUntil.empty()) { return false; }
	const auto it = s_haltUntil.find(d->id);
	return it != s_haltUntil.end() && gameTime < it->second;
}

void tick()
{
	if (!s_haltUntil.empty() && (gameTime / GAME_TICKS_PER_SEC) % 10 == 0)
	{
		for (auto it = s_haltUntil.begin(); it != s_haltUntil.end();) { it = (gameTime > it->second + 5000) ? s_haltUntil.erase(it) : std::next(it); }
	}
	const int mg = mgStat();
	if (mg < 0) { return; }
	const WEAPON_STATS &ms = asWeaponStats[mg];
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (!isTank(d)) { continue; }
			Gun &g = s_guns[d->id];
			if (gameTime < g.next) { continue; }
			const int range = proj_GetLongRange(ms, static_cast<int>(p));
			// naechster sichtbarer Gegner vor dem Bug
			BASE_OBJECT *best = nullptr;
			int64_t bestD = static_cast<int64_t>(range) * range;
			for (unsigned q = 0; q < MAX_PLAYERS; ++q)
			{
				if (q == p || aiCheckAlliances(p, q)) { continue; }
				for (DROID *e : gameWorld.objects.droids[q])
				{
					if (e->died || e->visible[p] < UBYTE_MAX || e->isFlying() || !modlayer::canInteract(d, e) || modcrew::crewless(e)) { continue; }
					const int64_t dx = e->pos.x - d->pos.x, dy = e->pos.y - d->pos.y, dd = dx * dx + dy * dy;
					if (dd >= bestD) { continue; }
					const int diff = std::abs(angleDelta(iAtan2(static_cast<int>(dx), static_cast<int>(dy)) - d->rot.direction));
					if (diff > ARC) { continue; }
					best = e; bestD = dd;
				}
			}
			if (best == nullptr)
			{
				g.shots = 0;
				g.next = gameTime + 400;
				continue;
			}
			// Schuss: trifft mit 60 %, sonst ins Gelaende neben das Ziel
			WEAPON w;
			w.nStat = static_cast<uint32_t>(mg);
			w.ammo = 1;
			w.lastFired = gameTime;
			Vector3i aim = best->pos;
			BASE_OBJECT *tgt = best;
			if (gameRand(100) >= 60)
			{
				aim.x += static_cast<int>(gameRand(97)) - 48;
				aim.y += static_cast<int>(gameRand(97)) - 48;
				tgt = nullptr;
			}
			proj_SendProjectile(&w, d, static_cast<int>(p), aim, tgt, true, -1);
			// gelbes Muendungsfeuer vorn am Rumpf (nur Anzeige)
			const BODY_STATS *b = d->getBodyStats();
			const int front = (b && b->pIMD) ? std::max(20, b->pIMD->max.z) : 40;
			const int hgt = (b && b->pIMD) ? std::max(10, b->pIMD->max.y * 2 / 5) : 16;
			Vector3i fx(d->pos.x + iSinR(d->rot.direction, front), d->pos.z + hgt, d->pos.y + iCosR(d->rot.direction, front));
			if (ms.pMuzzleGraphic != nullptr)
			{
				addEffect(&fx, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED, true, ms.pMuzzleGraphic->displayModel(), 0, gameTime - deltaGameTime + 1);
			}
			if (++g.shots >= BURST_SHOTS) { g.shots = 0; g.next = gameTime + BURST_PAUSE_MS; }
			else { g.next = gameTime + SHOT_MS; }
		}
	}
	if (s_guns.size() > 2048)
	{
		s_guns.clear();
	}
}

void reset()
{
	s_haltUntil.clear();
	s_guns.clear();
	s_mgStat = -2;
}
}
