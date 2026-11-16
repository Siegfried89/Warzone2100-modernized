// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Schlachtfeld-Autonomie - kurzer Rueckzug unter schwerem Beschuss (siehe modmorale.h)

#include "modcontrol.h"
#include "modmorale.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "droid.h"
#include "fpath.h"
#include "game_world.h"
#include "map.h"
#include "multiplay.h"
#include "modlayer.h"
#include "modoil.h"
#include "modterrain.h"
#include "objmem.h"
#include "order.h"
#include "projectile.h"
#include "stats.h"

#include "lib/framework/trig.h"
#include "lib/framework/fixedpoint.h"
#include <map>

namespace modmorale
{
namespace
{
constexpr unsigned RETREAT_TILES = 4;        // so weit vom Schuetzen weg
constexpr unsigned WAIT_MS = 9000;           // so lange abwarten (ab dem Treffer)
constexpr unsigned COOLDOWN_MS = 25000;      // fruehestens dann wieder
constexpr unsigned GIVEUP_MS = 30000;        // kommt die Einheit nicht zur Ruhe, wird die Rueckkehr verworfen

struct Entry
{
	Vector2i back;          // Rueckkehrziel (Weltkoordinaten)
	unsigned resumeAt = 0;  // Spielzeit, ab der zurueckgekehrt wird
	unsigned cooldownUntil = 0;
	bool pending = false;
};
std::map<uint32_t, Entry> s_state;   // geordnet: gleiche Reihenfolge auf allen Rechnern

bool heavyWeapon(const PROJECTILE *p)
{
	if (p == nullptr || p->psWStats == nullptr) { return false; }
	const WEAPON_SUBCLASS sc = p->psWStats->weaponSubClass;
	return sc == WSC_CANNON || sc == WSC_HOWITZERS || sc == WSC_MORTARS;
}

bool infantry(const DROID *d)
{
	return d != nullptr && (d->droidType == DROID_PERSON || d->droidType == DROID_CYBORG);
}

bool interruptible(const DROID *d)
{
	switch (d->order.type)
	{
	case DORDER_NONE:
	case DORDER_GUARD:
	case DORDER_SCOUT:
	case DORDER_ATTACK:
		return secondaryGetState(d, DSO_HALTTYPE) != DSS_HALT_HOLD;
	default:
		return false;   // ausdruecklicher Fahrbefehl, Bauen, Besetzen, Halten ...
	}
}
}

void onHit(DROID *psDroid, int hpLost, const PROJECTILE *psProj)
{
	if (!infantry(psDroid) || psDroid->died || hpLost <= 0 || !heavyWeapon(psProj)) { return; }
	if (modlayer::layerOf(psDroid) != modlayer::SURFACE) { return; }
	if (modoil::droidInTrench(psDroid) || modterrain::droidInCover(psDroid)) { return; }   // im Graben / in der eigenen Mulde bleibt man in Deckung
	Entry &e = s_state[psDroid->id];
	if (e.pending || gameTime < e.cooldownUntil || !interruptible(psDroid) || modcontrol::manual(psDroid)) { return; }

	// Richtung: weg vom Schuetzen (sonst weg vom Einschlag)
	Vector2i from = psProj->psSource ? psProj->psSource->pos.xy() : psProj->pos.xy();
	Vector2i d = psDroid->pos.xy() - from;
	if (d.x == 0 && d.y == 0) { d.x = 1; }
	const PROPULSION_STATS *prop = psDroid->getPropulsionStats();
	const PROPULSION_TYPE pt = prop ? prop->propulsionType : PROPULSION_TYPE_LEGGED;
	// geradeaus weg, sonst leicht seitlich (feste Reihenfolge, kein Zufall)
	static const int SIDE[5][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 2, 0 }, { -2, 0 } };
	// Ganzzahl-Trigonometrie (iSinR/iCosR/iAtan2): auf allen Rechnern bitgleich
	const uint16_t baseDir = iAtan2(d.x, d.y);   // wie im Spiel: atan2(x, y)
	for (const auto &s : SIDE)
	{
		const uint16_t dir = static_cast<uint16_t>(baseDir + s[0] * DEG(20));
		const int dist = static_cast<int>(RETREAT_TILES) * DIST_TILE;
		Vector2i to(psDroid->pos.x + iSinR(dir, dist), psDroid->pos.y + iCosR(dir, dist));
		const int tx = map_coord(to.x), ty = map_coord(to.y);
		if (tx < 1 || ty < 1 || tx >= gameWorld.map.width - 1 || ty >= gameWorld.map.height - 1) { continue; }
		if (fpathBlockingTile(gameWorld.map, tx, ty, pt)) { continue; }
		// Rueckkehrziel: altes Vorrueck-Ziel, sonst die jetzige Stelle
		e.back = (psDroid->order.type == DORDER_SCOUT) ? psDroid->order.pos : psDroid->pos.xy();
		e.resumeAt = gameTime + WAIT_MS;
		e.cooldownUntil = gameTime + COOLDOWN_MS;
		e.pending = true;
		orderDroidLoc(psDroid, DORDER_MOVE, static_cast<UDWORD>(to.x), static_cast<UDWORD>(to.y), ModeImmediate);
		return;
	}
}

void tick()
{
	if (s_state.empty()) { return; }
	for (auto it = s_state.begin(); it != s_state.end();)
	{
		Entry &e = it->second;
		DROID *d = nullptr;
		if (e.pending)
		{
			// Einheit suchen (nur Spieler, die Einheiten haben)
			for (unsigned p = 0; p < MAX_PLAYERS && d == nullptr; ++p)
			{
				d = IdToDroid(gameWorld.objects, it->first, p);
			}
			if (d == nullptr || d->died)
			{
				it = s_state.erase(it);
				continue;
			}
			if (gameTime >= e.resumeAt)
			{
				if (d->order.type == DORDER_NONE || d->order.type == DORDER_GUARD)
				{
					orderDroidLoc(d, DORDER_SCOUT, static_cast<UDWORD>(e.back.x), static_cast<UDWORD>(e.back.y), ModeImmediate);
					e.pending = false;
				}
				else if (d->order.type != DORDER_MOVE || gameTime >= e.resumeAt + GIVEUP_MS)
				{
					e.pending = false;   // inzwischen neuer Befehl: nichts tun
				}
			}
		}
		if (!e.pending && gameTime >= e.cooldownUntil)
		{
			it = s_state.erase(it);
			continue;
		}
		++it;
	}
}

void reset()
{
	s_state.clear();
}
}
