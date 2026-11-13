// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Hilferuf - siehe modhelp.h
#include "modhelp.h"
#include "modvoice.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "ai.h"
#include "baseobject.h"
#include "droid.h"
#include "map.h"
#include "game_world.h"
#include "order.h"
#include "projectile.h"
#include "statsdef.h"
#include "stats.h"

#include <unordered_map>

namespace modhelp
{
namespace
{
constexpr int HELP_RADIUS = 10 * TILE_UNITS;     // so weit hoeren Verbuendete den Ruf
constexpr unsigned HELP_COOLDOWN = 2500;         // pro getroffener Einheit hoechstens alle 2,5 s
constexpr int MAX_HELPERS = 6;                   // so viele Einheiten reagieren je Ruf

std::unordered_map<uint32_t, unsigned> s_nextCall;

bool idleForHelp(const DROID *d)
{
	if (d == nullptr || d->died || d->numWeaps == 0 || d->asWeaps[0].nStat == 0) { return false; }
	if (d->isVtol() || d->isTransporter()) { return false; }
	if (d->action != DACTION_NONE) { return false; }
	if (d->order.type != DORDER_NONE && d->order.type != DORDER_GUARD) { return false; }
	if (secondaryGetState(d, DSO_ATTACK_LEVEL) == DSS_ALEV_NEVER) { return false; }
	if (secondaryGetState(d, DSO_HALTTYPE) == DSS_HALT_HOLD) { return false; }
	return true;
}
} // namespace

void onDroidHit(DROID *psVictim, const PROJECTILE *psProj)
{
	if (psVictim == nullptr || psProj == nullptr || psProj->psSource == nullptr || psProj->psSource->died) { return; }
	BASE_OBJECT *attacker = psProj->psSource;
	if (attacker->player == psVictim->player || aiCheckAlliances(attacker->player, psVictim->player)) { return; }
	unsigned &next = s_nextCall[psVictim->id];
	if (gameTime < next) { return; }
	next = gameTime + HELP_COOLDOWN;

	int helpers = 0;
	for (unsigned p = 0; p < MAX_PLAYERS && helpers < MAX_HELPERS; p++)
	{
		if (p != psVictim->player && !aiCheckAlliances(p, psVictim->player)) { continue; }
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (helpers >= MAX_HELPERS) { break; }
			if (d == psVictim || !idleForHelp(d)) { continue; }
			const int dx = d->pos.x - psVictim->pos.x, dy = d->pos.y - psVictim->pos.y;
			if (dx * dx + dy * dy > HELP_RADIUS * HELP_RADIUS) { continue; }
			if (!validTarget(d, attacker, 0)) { continue; }
			if (attacker->visible[d->player] == 0 && !aiCheckAlliances(d->player, psVictim->player) && d->player != psVictim->player) { continue; }
			orderDroidObj(d, DORDER_ATTACK, attacker, ModeImmediate);
			helpers++;
		}
	}
	if (helpers > 0)
	{
		modvoice::onHelpCall(psVictim);   // Funkspruch "Requesting support" (nur Ton)
	}
}

void reset()
{
	s_nextCall.clear();
}
}
