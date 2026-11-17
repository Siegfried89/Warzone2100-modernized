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
#include <cmath>
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
int64_t r2(int tiles) { const int64_t r = static_cast<int64_t>(tiles) * DIST_TILE; return r * r; }   // Original-Kacheln (Abstand)

bool waitingUnit(const DROID *d, const Vector2i &home)
{
	if (d->died || d->isTransporter() || d->isVtol() || modlayer::isFlier(d) || (d->psGroup != nullptr && d->psGroup->type == GT_TRANSPORTER)) { return false; }
	if (modlayer::layerOf(d) != modlayer::SURFACE) { return false; }
	if (d->order.type != DORDER_NONE && d->order.type != DORDER_GUARD) { return false; }
	if (d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT) { return false; }   // Baufahrzeuge bleiben daheim
	return d2(d->pos.xy(), home) <= r2(WAIT_TILES);
}

/// Transportschleife eines Spielers: Heimat = aeltestes Leuchtfeuer mit Fluglotse daneben, Front = neuestes andere Leuchtfeuer
bool routeOf(const GameWorld &world, unsigned p, const STRUCTURE *&home, const STRUCTURE *&front)
{
	std::vector<const STRUCTURE *> beacons, atcs;
	for (const STRUCTURE *s : world.objects.structures[p])
	{
		if (isBeacon(s)) { beacons.push_back(s); }
		else if (isATC(s)) { atcs.push_back(s); }
	}
	home = front = nullptr;
	if (beacons.size() < 2 || atcs.empty()) { return false; }
	std::sort(beacons.begin(), beacons.end(), [](const STRUCTURE *a, const STRUCTURE *b) { return a->id < b->id; });
	for (const STRUCTURE *b : beacons)
	{
		for (const STRUCTURE *a : atcs)
		{
			if (d2(a->pos.xy(), b->pos.xy()) <= r2(ATC_TILES)) { home = b; break; }
		}
		if (home) { break; }
	}
	if (home == nullptr) { return false; }
	for (auto it = beacons.rbegin(); it != beacons.rend(); ++it) { if (*it != home) { front = *it; break; } }
	return front != nullptr;
}

void stepPlayer(GameWorld &world, unsigned p)
{
	const STRUCTURE *home = nullptr, *front = nullptr;
	if (!routeOf(world, p, home, front)) { return; }
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

// ---------------------------------------------------------------- Transportschleife wie in Supreme Commander (Wunsch 17.11. 15:34)
// "Schau dir an, wie Supreme Commander die schoenen breiten blauen, fast lilanen Pfeile fuer die Transportschleifen rendert. Mach das genauso."
// SupCom (lua/ui/game/commandgraphparams.lua, UNITCOMMAND_Ferry): Band mit der Textur orderline_arrow-big (grosse Pfeile im Band),
// laufend (orderline_anim_rate 0.20, uv_aspect 0.5 = ein Pfeil je zwei Bandbreiten), Farbe 0xaa654bc2 (lila-blau, 2/3 deckend),
// angewaehlt 0xdd654bc2, hervorgehoben 0xff7558e2, am Ende eine Pfeilspitze (orderline_arrow04). Hier im Bildschirm nachgebaut:
// weiches Band auf dem Boden (Breite perspektivisch), darin laufende Winkel-Pfeile, grosse Spitze am Front-Leuchtfeuer.
void softSeg(const glm::vec2 &a, const glm::vec2 &b, float w, PIELIGHT col)
{
	const glm::vec2 d = b - a;
	const float len = glm::length(d);
	if (len < 0.01f || w <= 0.5f) { return; }
	const glm::vec2 c = (a + b) * 0.5f;
	pie_DrawSoftRoundedBoxRotated(c.x, c.y, len + w * 0.5f, w, std::atan2(d.y, d.x), col, w * 0.5f, std::max(1.2f, w * 0.25f));
}

struct RoutePt { glm::vec2 c; float halfW; bool ok; };

RoutePt projectOnGround(const glm::vec2 &w, const glm::vec2 &perp, float halfW, const glm::mat4 &m)
{
	RoutePt r{ glm::vec2(0.f), 0.f, false };
	Vector2i a(0, 0), b(0, 0);
	const int wx = static_cast<int>(w.x), wy = static_cast<int>(w.y);
	if (wx < 0 || wy < 0 || wx >= world_coord(gameWorld.map.width) || wy >= world_coord(gameWorld.map.height)) { return r; }
	const int z = map_Height(gameWorld.map, wx, wy) + 6;
	if (!project(Vector3i(wx, wy, z), m, a)) { return r; }
	const glm::vec2 e = w + perp * halfW;
	if (!project(Vector3i(static_cast<int>(e.x), static_cast<int>(e.y), z), m, b)) { return r; }
	r.c = glm::vec2(a.x, a.y);
	r.halfW = glm::length(glm::vec2(b.x - a.x, b.y - a.y));
	r.ok = true;
	return r;
}

void drawFerryRoute(const Vector2i &from, const Vector2i &to, const glm::mat4 &m, bool hot)
{
	const glm::vec2 A(from.x, from.y), B(to.x, to.y);
	const float L = glm::length(B - A);
	if (L < DIST_TILE) { return; }
	const glm::vec2 dir = (B - A) / L, perp(-dir.y, dir.x);
	const float halfW = DIST_TILE * 0.32f;                 // Band etwa 2/3 Feld breit
	const float headL = DIST_TILE * 0.9f;                   // grosse Spitze am Ende
	const float bodyEnd = L - headL * 0.85f;
	const int alpha = hot ? 0xdd : 0xaa;
	const PIELIGHT band = pal_RGBA(0x65, 0x4b, 0xc2, static_cast<UBYTE>(alpha * 0.42f));
	const PIELIGHT glow = pal_RGBA(0x75, 0x58, 0xe2, static_cast<UBYTE>(alpha * 0.16f));
	// 1. Band (Hof + Kern) in Abschnitten von 1/2 Feld, perspektivisch breit
	RoutePt prev = projectOnGround(A, perp, halfW, m);
	for (float s = DIST_TILE * 0.5f; ; s += DIST_TILE * 0.5f)
	{
		const float t = std::min(s, bodyEnd);
		const RoutePt cur = projectOnGround(A + dir * t, perp, halfW, m);
		if (prev.ok && cur.ok)
		{
			const float w = prev.halfW + cur.halfW;            // = volle Breite (Mittel der beiden Enden x 2 / 2)
			softSeg(prev.c, cur.c, w * 1.55f, glow);
			softSeg(prev.c, cur.c, w, band);
		}
		prev = cur;
		if (t >= bodyEnd) { break; }
	}
	// 2. laufende Winkel-Pfeile im Band (ein Pfeil je zwei Bandbreiten, 0,2 Wiederholungen pro Sekunde wie in SupCom ... etwas flotter)
	const float spacing = halfW * 4.f;
	const float shift = std::fmod(static_cast<float>(realTime) / 1000.f * 0.35f * spacing, spacing);
	const PIELIGHT arrowCol = pal_RGBA(0x9a, 0x86, 0xff, static_cast<UBYTE>(hot ? 235 : 190));
	const PIELIGHT arrowGlow = pal_RGBA(0x75, 0x58, 0xe2, static_cast<UBYTE>(hot ? 90 : 60));
	for (float s = shift + spacing * 0.5f; s < bodyEnd - spacing * 0.3f; s += spacing)
	{
		const float fade = std::min(1.f, std::min(s / spacing, (bodyEnd - s) / spacing));
		const glm::vec2 tipW = A + dir * (s + halfW * 0.55f);
		const glm::vec2 armW = A + dir * (s - halfW * 0.55f);
		const RoutePt tip = projectOnGround(tipW, perp, halfW, m);
		const RoutePt l = projectOnGround(armW + perp * (halfW * 0.78f), perp, halfW, m);
		const RoutePt r = projectOnGround(armW - perp * (halfW * 0.78f), perp, halfW, m);
		if (!tip.ok || !l.ok || !r.ok) { continue; }
		const float th = std::max(1.5f, tip.halfW * 0.42f);
		PIELIGHT c = arrowCol; c.byte.a = static_cast<UBYTE>(c.byte.a * fade);
		PIELIGHT g = arrowGlow; g.byte.a = static_cast<UBYTE>(g.byte.a * fade);
		softSeg(l.c, tip.c, th * 2.4f, g); softSeg(r.c, tip.c, th * 2.4f, g);
		softSeg(l.c, tip.c, th, c); softSeg(r.c, tip.c, th, c);
	}
	// 3. Spitze am Front-Leuchtfeuer (gefuelltes Dreieck aus Querscheiben, wie orderline_arrow04)
	const RoutePt base = projectOnGround(A + dir * (L - headL), perp, halfW * 2.1f, m);
	const RoutePt tipP = projectOnGround(B, perp, halfW, m);
	if (base.ok && tipP.ok)
	{
		const glm::vec2 d = tipP.c - base.c;
		const float len = glm::length(d);
		if (len > 2.f)
		{
			const float ang = std::atan2(d.y, d.x);
			const int K = 12;
			for (int pass = 0; pass < 2; ++pass)
			{
				for (int k = 0; k < K; ++k)
				{
					const float f = (static_cast<float>(k) + 0.5f) / static_cast<float>(K);
					const glm::vec2 c = base.c + d * f;
					const float across = base.halfW * 2.f * (1.f - f) + (pass == 0 ? base.halfW * 0.5f : 0.f);
					pie_DrawSoftRoundedBoxRotated(c.x, c.y, len / static_cast<float>(K) + 2.f, std::max(1.f, across), ang,
					                              pass == 0 ? glow : pal_RGBA(0x75, 0x58, 0xe2, static_cast<UBYTE>(alpha * 0.8f)), 1.f, 1.2f);
				}
			}
		}
	}
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
	// eigene Transportschleife (SupCom-Pfeil); heller, wenn ein Transporter, Leuchtfeuer oder Fluglotse angewaehlt ist
	{
		const STRUCTURE *home = nullptr, *front = nullptr;
		if (routeOf(gameWorld, selectedPlayer, home, front))
		{
			bool hot = home->selected || front->selected;
			for (const DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d && d->selected && d->isTransporter()) { hot = true; break; } }
			for (const STRUCTURE *s : gameWorld.objects.structures[selectedPlayer]) { if (s && s->selected && isATC(s)) { hot = true; break; } }
			drawFerryRoute(home->pos.xy(), front->pos.xy(), m, hot);
		}
	}
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
