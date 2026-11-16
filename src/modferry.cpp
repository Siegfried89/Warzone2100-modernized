// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Transport-Dauerschleife (siehe modferry.h)

#include "modferry.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"

#include "ai.h"
#include "droid.h"
#include "group.h"
#include "game_world.h"
#include "levels.h"
#include "map.h"
#include "modlayer.h"
#include "multiplay.h"
#include "order.h"
#include "structure.h"
#include "transporter.h"

#include <algorithm>
#include <map>
#include <vector>

namespace modferry
{
namespace
{
constexpr unsigned STEP_MS = 1000;
constexpr int ATC_TILES = 6;        // Flugkontrollturm hoechstens so weit vom Heimat-Leuchtfeuer
constexpr int WAIT_TILES = 6;       // Einheiten in diesem Umkreis um das Heimat-Leuchtfeuer warten auf den Transport
constexpr int HOME_TILES = 5;       // Transporter gilt als "daheim"
constexpr unsigned LOAD_MAX_MS = 20000;   // spaetestens dann startet ein teilweise beladener Transporter
constexpr unsigned LOAD_MIN_MS = 6000;    // so lange wird mindestens auf weitere Einheiten gewartet

enum Phase { IDLE, LOADING, OUTBOUND, RETURNING };
struct TState
{
	Phase phase = IDLE;
	unsigned since = 0;
};
std::map<uint32_t, TState> s_trans;   // geordnet: gleiche Reihenfolge auf allen Rechnern
unsigned s_next = 0;

bool isBeacon(const STRUCTURE *s) { return s && s->status == SS_BUILT && s->pStructureType && s->pStructureType->id == "A0BaBaTransportBeacon"; }
bool isATC(const STRUCTURE *s) { return s && s->status == SS_BUILT && s->pStructureType && s->pStructureType->id == "A0BaBaATC"; }

int64_t d2(const Vector2i &a, const Vector2i &b)
{
	const int64_t dx = a.x - b.x, dy = a.y - b.y;
	return dx * dx + dy * dy;
}
int64_t r2(int tiles) { const int64_t r = world_coord(tiles); return r * r; }

bool waitingUnit(const DROID *d, const Vector2i &home)
{
	if (d->died || d->isTransporter() || d->isVtol() || modlayer::isFlier(d) || (d->psGroup != nullptr && d->psGroup->type == GT_TRANSPORTER)) { return false; }
	if (modlayer::layerOf(d) != modlayer::SURFACE) { return false; }
	if (d->order.type != DORDER_NONE && d->order.type != DORDER_GUARD) { return false; }
	if (d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT) { return false; }   // Baufahrzeuge bleiben daheim
	return d2(d->pos.xy(), home) <= r2(WAIT_TILES);
}

void stepPlayer(GameWorld &world, unsigned p)
{
	// Leuchtfeuer und Fluglotsen
	std::vector<const STRUCTURE *> beacons, atcs;
	for (const STRUCTURE *s : world.objects.structures[p])
	{
		if (isBeacon(s)) { beacons.push_back(s); }
		else if (isATC(s)) { atcs.push_back(s); }
	}
	if (beacons.size() < 2 || atcs.empty()) { return; }
	std::sort(beacons.begin(), beacons.end(), [](const STRUCTURE *a, const STRUCTURE *b) { return a->id < b->id; });
	// Heimat: das aelteste Leuchtfeuer mit Fluglotse in der Naehe; Front: das neueste andere Leuchtfeuer
	const STRUCTURE *home = nullptr;
	for (const STRUCTURE *b : beacons)
	{
		for (const STRUCTURE *a : atcs)
		{
			if (d2(a->pos.xy(), b->pos.xy()) <= r2(ATC_TILES)) { home = b; break; }
		}
		if (home) { break; }
	}
	if (home == nullptr) { return; }
	const STRUCTURE *front = nullptr;
	for (auto it = beacons.rbegin(); it != beacons.rend(); ++it) { if (*it != home) { front = *it; break; } }
	if (front == nullptr) { return; }
	const Vector2i homePos = home->pos.xy(), frontPos = front->pos.xy();

	std::vector<DROID *> waiting;
	for (DROID *d : world.objects.droids[p])
	{
		if (d && waitingUnit(d, homePos)) { waiting.push_back(d); }
	}

	for (DROID *t : world.objects.droids[p])
	{
		if (t == nullptr || t->died || !t->isTransporter()) { continue; }
		TState &st = s_trans[t->id];
		const bool atHome = d2(t->pos.xy(), homePos) <= r2(HOME_TILES);
		const bool idle = t->order.type == DORDER_NONE || t->order.type == DORDER_HOLD;
		const bool empty = transporterIsEmpty(t);
		switch (st.phase)
		{
		case IDLE:
			if (!idle) { break; }                                   // der Spieler hat etwas anderes befohlen
			if (!atHome)
			{
				if (empty) { orderDroidLoc(t, DORDER_MOVE, homePos.x, homePos.y, ModeImmediate); st.phase = RETURNING; st.since = gameTime; }
				break;
			}
			if (!waiting.empty())
			{
				int space = calcRemainingCapacity(t);
				for (DROID *u : waiting)
				{
					if (u->order.type == DORDER_EMBARK) { continue; }
					const int need = transporterSpaceRequired(u);
					if (need > space) { continue; }
					orderDroidObj(u, DORDER_EMBARK, t, ModeImmediate);
					space -= need;
					if (space <= 0) { break; }
				}
				st.phase = LOADING;
				st.since = gameTime;
			}
			break;
		case LOADING:
		{
			const bool full = calcRemainingCapacity(t) <= 0;
			bool stillComing = false;
			for (const DROID *u : world.objects.droids[p])
			{
				if (u && !u->died && u->order.type == DORDER_EMBARK && u->order.psObj == t) { stillComing = true; break; }
			}
			const unsigned waited = gameTime - st.since;
			if (!empty && (full || waited >= LOAD_MAX_MS || (!stillComing && waited >= LOAD_MIN_MS)))
			{
				orderDroidLoc(t, DORDER_DISEMBARK, frontPos.x, frontPos.y, ModeImmediate);
				st.phase = OUTBOUND;
				st.since = gameTime;
			}
			else if (empty && waited >= LOAD_MAX_MS)
			{
				st.phase = IDLE;   // niemand ist eingestiegen
			}
			break;
		}
		case OUTBOUND:
			if (empty && idle)
			{
				orderDroidLoc(t, DORDER_MOVE, homePos.x, homePos.y, ModeImmediate);
				st.phase = RETURNING;
				st.since = gameTime;
			}
			break;
		case RETURNING:
			if (atHome && idle) { st.phase = IDLE; st.since = gameTime; }
			else if (idle && !atHome && gameTime - st.since > 5000) { orderDroidLoc(t, DORDER_MOVE, homePos.x, homePos.y, ModeImmediate); st.since = gameTime; }
			break;
		}
	}
}

bool project(const Vector3i &world, const glm::mat4 &m, Vector2i &out)
{
	const Vector3i pos(world.x, world.z, -world.y);
	return pie_RotateProjectWithPerspective(&pos, m, &out) > 0;
}
}

void tick(GameWorld &world)
{
	if (game.type == LEVEL_TYPE::CAMPAIGN) { return; }   // Kampagnen-Transporter gehoeren den Missionen
	if (gameTime < s_next) { return; }
	s_next = gameTime + STEP_MS;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p) { stepPlayer(world, p); }
	// vergessene Transporter aufraeumen
	for (auto it = s_trans.begin(); it != s_trans.end();)
	{
		bool alive = false;
		for (unsigned p = 0; p < MAX_PLAYERS && !alive; ++p)
		{
			alive = IdToDroid(world.objects, it->first, p) != nullptr;
		}
		it = alive ? std::next(it) : s_trans.erase(it);
	}
}

void drawOverlay(const glm::mat4 &m)
{
	if (selectedPlayer >= MAX_PLAYERS || modlayer::current() != modlayer::SURFACE) { return; }
	const bool on = (realTime / 450) % 2 == 0;     // Blinktakt
	const bool on2 = (realTime / 450) % 2 == 1;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (!isBeacon(s) || !s->visibleForLocalDisplay()) { continue; }
			// vier kleine Lichter an den Ecken, abwechselnd blinkend wie an einer WZ-Landezone
			static const int C[4][2] = { { -1, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 } };
			for (int k = 0; k < 4; ++k)
			{
				if ((k < 2) ? !on : !on2) { continue; }
				const int wx = s->pos.x + C[k][0] * (DIST_TILE * 2 / 5), wy = s->pos.y + C[k][1] * (DIST_TILE * 2 / 5);
				Vector2i sp(0, 0);
				if (!project(Vector3i(wx, wy, map_Height(gameWorld.map, wx, wy) + 14), m, sp)) { continue; }
				const PIELIGHT c = (k < 2) ? pal_RGBA(255, 70, 50, 255) : pal_RGBA(90, 255, 110, 255);
				pie_BoxFill_alpha(sp.x - 3, sp.y - 3, sp.x + 3, sp.y + 3, pal_RGBA(c.byte.r, c.byte.g, c.byte.b, 90));
				pie_BoxFill_alpha(sp.x - 1, sp.y - 1, sp.x + 2, sp.y + 2, c);
			}
		}
	}
}

void reset()
{
	s_trans.clear();
	s_next = 0;
}
}
