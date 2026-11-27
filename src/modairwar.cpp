// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Luftkampf zwischen allen Fliegern (siehe modairwar.h)
#include "modairwar.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"

#include "action.h"
#include "ai.h"
#include "combat.h"
#include "droid.h"
#include "game_world.h"
#include "geometry.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "order.h"
#include "projectile.h"
#include "stats.h"

#include <algorithm>
#include <unordered_map>

namespace modairwar
{
namespace
{
constexpr int NOSE_ARC = DEG(22);          // feste Bordwaffen: der Gegner muss so nah vor der Nase sein
constexpr unsigned SCAN_MS = 300;          // so oft sieht sich ein Flieger nach Luftzielen um
std::unordered_map<uint32_t, unsigned> s_next;   // id -> naechste Umschau (Spielzeit)

bool attacking(const DROID *d)
{
	switch (d->action)
	{
	case DACTION_ATTACK:
	case DACTION_VTOLATTACK:
	case DACTION_MOVETOATTACK:
	case DACTION_ROTATETOATTACK:
	case DACTION_MOVEFIRE:
		return true;
	default:
		return false;
	}
}

/// auf dem Heimweg (Reparatur, Nachladen, Rueckkehr): dann wird nicht mehr angegriffen, nur noch im Vorbeiflug geschossen
bool homeward(const DROID *d)
{
	switch (d->order.type)
	{
	case DORDER_RTB:
	case DORDER_RTR:
	case DORDER_RTR_SPECIFIED:
	case DORDER_REARM:
		return true;
	default:
		break;
	}
	switch (d->action)
	{
	case DACTION_MOVETOREARM:
	case DACTION_WAITFORREARM:
	case DACTION_MOVETOREARMPOINT:
	case DACTION_WAITDURINGREARM:
		return true;
	default:
		return false;
	}
}

/// Waffentraeger in der Luft, der kaempfen darf
bool fighter(const DROID *d)
{
	if (d->died || d->numWeaps == 0 || d->asWeaps[0].nStat == 0 || !airborne(d) || modlayer::inTransition(d))
	{
		return false;
	}
	if (d->droidType != DROID_WEAPON && d->droidType != DROID_COMMAND && !d->isTransporter())
	{
		return false;
	}
	if (secondaryGetState(d, DSO_ATTACK_LEVEL) != DSS_ALEV_ALWAYS)
	{
		return false;   // "Feuer erwidern" / "Nicht schiessen": keine eigenen Luftangriffe
	}
	return !(d->isVtol() && vtolEmpty(d));
}

/// naechster sichtbarer feindlicher Flieger, auf den eine Bordwaffe zielen darf (innerhalb der groessten Reichweite)
DROID *nearestAirTarget(DROID *d)
{
	int range = 0;
	for (unsigned i = 0; i < d->numWeaps; ++i)
	{
		if (d->asWeaps[i].nStat == 0) { continue; }
		const WEAPON_STATS *ws = d->getWeaponStats(i);
		if (ws != nullptr && (ws->surfaceToAir & SHOOT_IN_AIR)) { range = std::max(range, proj_GetLongRange(*ws, d->player)); }
	}
	if (range <= 0) { return nullptr; }
	DROID *best = nullptr;
	int64_t bestD = static_cast<int64_t>(range) * range;
	for (unsigned q = 0; q < MAX_PLAYERS; ++q)
	{
		if (q == static_cast<unsigned>(d->player) || aiCheckAlliances(d->player, q)) { continue; }
		for (DROID *e : gameWorld.objects.droids[q])
		{
			if (e->died || e->visible[d->player] < UBYTE_MAX || !airborne(e) || !modlayer::canInteract(d, e)) { continue; }
			const int64_t dx = e->pos.x - d->pos.x, dy = e->pos.y - d->pos.y, dd = dx * dx + dy * dy;
			if (dd >= bestD) { continue; }
			bool any = false;
			for (unsigned i = 0; i < d->numWeaps && !any; ++i) { any = d->asWeaps[i].nStat != 0 && validTarget(d, e, i); }
			if (!any) { continue; }
			best = e;
			bestD = dd;
		}
	}
	return best;
}

/// im Vorbeiflug feuern, ohne den Kurs zu aendern
void passingFire(DROID *d, DROID *e)
{
	for (unsigned i = 0; i < d->numWeaps; ++i)
	{
		if (d->asWeaps[i].nStat == 0 || !validTarget(d, e, i) || !actionInRange(d, e, i, false))
		{
			continue;
		}
		const WEAPON_STATS *ws = d->getWeaponStats(i);
		bool aimed = false;
		if (!ws->rotate)
		{
			const uint16_t dir = calcDirection(d->pos.x, d->pos.y, e->pos.x, e->pos.y);
			aimed = std::abs(angleDelta(dir - d->rot.direction)) <= NOSE_ARC;
		}
		else
		{
			aimed = actionTargetTurret(d, e, &d->asWeaps[i]);
		}
		if (aimed)
		{
			combFire(&d->asWeaps[i], d, e, static_cast<int>(i));
		}
	}
}
} // namespace

bool airborne(const DROID *d)
{
	if (d == nullptr || d->died) { return false; }
	const PROPULSION_STATS *p = d->getPropulsionStats();
	if (p == nullptr) { return false; }
	if (p->alwaysFlying) { return true; }
	return p->propulsionType == PROPULSION_TYPE_LIFT && d->sMove.Status != MOVEINACTIVE;
}

void tick()
{
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (!fighter(d) || attacking(d)) { continue; }   // wer schon angreift, kuemmert sich um sein Ziel
			unsigned &next = s_next[d->id];
			if (gameTime < next) { continue; }
			next = gameTime + SCAN_MS;
			DROID *e = nearestAirTarget(d);
			if (e == nullptr) { continue; }
			next = gameTime + GAME_TICKS_PER_UPDATE;   // Ziel in Sicht: jeden Schritt nachrichten
			modlayermap::Scope layerScope(modlayer::layerOf(d));   // Geschosse fliegen in der Karte der Ebene des Fliegers (Oberflaeche: nichts zu tun)
			const bool idle = d->order.type == DORDER_NONE || d->order.type == DORDER_GUARD;
			if (idle && !homeward(d) && !d->isTransporter())
			{
				actionDroid(d, DACTION_ATTACK, e);   // untaetig in der Luft: Luftkampf aufnehmen (VTOL-Angriffsanfluege)
				if (attacking(d)) { continue; }
			}
			passingFire(d, e);
		}
	}
	if (s_next.size() > 4096)
	{
		s_next.clear();
	}
}

void reset()
{
	s_next.clear();
}
}
