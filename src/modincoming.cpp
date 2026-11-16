// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Artilleriegranate im Anflug - Pfeifen, Warnrufe, Verteilen, Eingraben (siehe modincoming.h)

#include "modincoming.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"

#include "ai.h"
#include "droid.h"
#include "effects.h"
#include "fpath.h"
#include "game_world.h"
#include "map.h"
#include "mapgrid.h"
#include "modlayer.h"
#include "modoil.h"
#include "modterrain.h"
#include "modvoice.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "ordersource.h"
#include "projectile.h"
#include "stats.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace modincoming
{
namespace
{
constexpr int LEAD_MS = 1500;               // so lange vor dem Einschlag beginnt das Pfeifen (= Laenge von incoming_*.ogg)
constexpr int MIN_DANGER = 5 * TILE_UNITS / 2;  // Gefahrenzone mindestens 2,5 Felder um den Einschlag
constexpr int ESCAPE_EXTRA = 3 * TILE_UNITS / 2; // so weit hinter die Gefahrenzone laufen
constexpr unsigned ALERT_MERGE_MS = 2500;   // Granaten einer Salve auf dieselbe Stelle loesen nur eine Reaktion aus
constexpr int ALERT_MERGE_DIST = 4 * TILE_UNITS;
constexpr unsigned COOLDOWN_MS = 12000;     // eine Einheit reagiert fruehestens dann wieder
constexpr unsigned SPREAD_TIMEOUT_MS = 9000;
constexpr unsigned DIG_MS = 4500;           // so lange graebt ein Soldat seine Mulde
constexpr int DIG_DEPTH = 13;               // tiefer als die Deckungsgrenze von modterrain (10)
constexpr int DIG_BERM = 5;                 // kleiner Erdwall, keine Sandsaecke

enum Phase { PH_SPREAD = 0, PH_DIG = 1, PH_DONE = 2 };

struct Entry
{
	uint8_t player = 0;
	Phase phase = PH_SPREAD;
	unsigned until = 0;        // SPREAD: Zeitlimit, DIG: Ende des Grabens, DONE: Ende der Sperre
	int tileX = -1, tileY = -1;
};
std::map<uint32_t, Entry> s_units;   // geordnet nach ID: gleiche Reihenfolge auf allen Rechnern

struct Alert
{
	Vector2i pos;
	unsigned time;
	int player;
};
std::vector<Alert> s_alerts;

int s_track[3] = { -2, -2, -2 };
unsigned s_nextWhistle = 0;          // Echtzeit (nur Ton)

bool infantry(const DROID *d)
{
	return d->droidType == DROID_PERSON || d->droidType == DROID_CYBORG || d->droidType == DROID_CYBORG_SUPER;
}

bool ground(const DROID *d)
{
	return !d->isVtol() && !d->isTransporter() && !modlayer::isFlier(d);
}

bool interruptible(const DROID *d)
{
	if (d->order.type != DORDER_NONE && d->order.type != DORDER_GUARD) { return false; }
	return secondaryGetState(d, DSO_HALTTYPE) != DSS_HALT_HOLD;
}

bool inCover(const DROID *d)
{
	return modoil::droidInTrench(d) || modterrain::droidInCover(d);
}

uint32_t mix(uint32_t a)
{
	a ^= a >> 16; a *= 0x7feb352dU; a ^= a >> 15; a *= 0x846ca68bU; a ^= a >> 16;
	return a;
}

PROPULSION_TYPE propOf(const DROID *d)
{
	const PROPULSION_STATS *ps = d->getPropulsionStats();
	return ps ? ps->propulsionType : PROPULSION_TYPE_LEGGED;
}

bool freeTile(const DROID *d, const Vector2i &w)
{
	const int tx = map_coord(w.x), ty = map_coord(w.y);
	if (tx < 2 || ty < 2 || tx >= gameWorld.map.width - 2 || ty >= gameWorld.map.height - 2) { return false; }
	return !fpathBlockingTile(gameWorld.map, tx, ty, propOf(d));
}

DROID *findDroid(uint32_t id, unsigned player)
{
	return (player < MAX_PLAYERS) ? IdToDroid(gameWorld.objects, id, player) : nullptr;
}

void whistle(const PROJECTILE *p)
{
	if (realTime < s_nextWhistle) { return; }
	MAPTILE *t = mapTile(gameWorld.map, map_coord(p->dst.x), map_coord(p->dst.y));
	if (!p->bVisible && (t == nullptr || !TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(t))) { return; }
	const int k = static_cast<int>(p->id % 3u);
	if (s_track[k] == -2)
	{
		char fn[32];
		snprintf(fn, sizeof(fn), "incoming_%d.ogg", k);
		s_track[k] = audio_GetTrackID(fn);
	}
	if (s_track[k] < 0) { return; }
	if (audio_PlayStaticTrack(p->dst.x, p->dst.y, s_track[k]))
	{
		s_nextWhistle = realTime + 140;   // dichte Salven: nicht jede einzelne Granate (sonst Brei)
	}
}

/// Fluchtpunkt weg vom Einschlag; false, wenn nirgends frei
bool escapePoint(const DROID *d, const Vector2i &impact, int danger, Vector2i &out)
{
	Vector2i v = d->pos.xy() - impact;
	const uint32_t h = mix(d->id * 2654435761u + static_cast<uint32_t>(impact.x * 31 + impact.y));
	uint16_t baseDir;
	if (std::abs(v.x) + std::abs(v.y) < 24)
	{
		baseDir = static_cast<uint16_t>(h & 0xffffu);   // direkt am Einschlag: Richtung aus der ID
	}
	else
	{
		baseDir = iAtan2(v.x, v.y);
	}
	const int distNow = iHypot(v);
	const int run = std::max(TILE_UNITS, danger + ESCAPE_EXTRA - distNow);
	const int side = static_cast<int>((h >> 16) % static_cast<uint32_t>(2 * TILE_UNITS)) - TILE_UNITS;   // seitlich versetzt: nicht alle auf einen Haufen
	static const int TURN[5] = { 0, 1, -1, 2, -2 };
	for (int k : TURN)
	{
		const uint16_t dir = static_cast<uint16_t>(baseDir + k * DEG(35));
		const uint16_t perp = static_cast<uint16_t>(dir + DEG(90));
		Vector2i to(d->pos.x + iSinR(dir, run) + iSinR(perp, side), d->pos.y + iCosR(dir, run) + iCosR(perp, side));
		if (freeTile(d, to))
		{
			out = to;
			return true;
		}
	}
	return false;
}

void react(const PROJECTILE *p)
{
	const Vector2i impact = p->dst.xy();
	const int shooter = static_cast<int>(p->player);
	for (const Alert &a : s_alerts)
	{
		if (a.player == shooter && gameTime - a.time < ALERT_MERGE_MS
		    && std::abs(a.pos.x - impact.x) < ALERT_MERGE_DIST && std::abs(a.pos.y - impact.y) < ALERT_MERGE_DIST)
		{
			return;
		}
	}
	s_alerts.push_back({ impact, gameTime, shooter });

	int danger = MIN_DANGER;
	if (p->psWStats != nullptr && shooter >= 0 && shooter < MAX_PLAYERS)
	{
		danger = std::max(danger, static_cast<int>(p->psWStats->upgrade[shooter].radius) + TILE_UNITS);
	}
	std::vector<DROID *> victims;
	for (BASE_OBJECT *o : gridStartIterate(impact.x, impact.y, static_cast<uint32_t>(danger)))
	{
		if (o->type != OBJ_DROID || o->died) { continue; }
		DROID *d = static_cast<DROID *>(o);
		if (d->player == p->player || (d->player < MAX_PLAYERS && shooter < MAX_PLAYERS && aiCheckAlliances(d->player, p->player))) { continue; }
		if (!ground(d) || modlayer::layerOf(d) != modlayer::layerOf(p)) { continue; }
		victims.push_back(d);
	}
	if (victims.empty()) { return; }
	std::sort(victims.begin(), victims.end(), [](const DROID *a, const DROID *b) { return a->id < b->id; });

	const DROID *caller = nullptr;
	bool anyMoved = false;
	for (DROID *d : victims)
	{
		auto it = s_units.find(d->id);
		if (it != s_units.end()) { continue; }   // reagiert schon / Sperre
		if (caller == nullptr) { caller = d; }
		if (inCover(d) || !interruptible(d)) { continue; }
		Vector2i to;
		if (!escapePoint(d, impact, danger, to)) { continue; }
		orderDroidLoc(d, DORDER_MOVE, static_cast<UDWORD>(to.x), static_cast<UDWORD>(to.y), ModeImmediate);
		Entry e;
		e.player = static_cast<uint8_t>(d->player);
		e.phase = PH_SPREAD;
		e.until = gameTime + SPREAD_TIMEOUT_MS;
		s_units[d->id] = e;
		anyMoved = true;
	}

	// Warnruf (nur Ton): Infanterie "Incoming!/Get down!" bzw. "Spread out!", Fahrzeuge "Spread out!"
	if (caller != nullptr)
	{
		const bool inf = infantry(caller);
		const char *cat = inf ? ((anyMoved && (gameTime / 1000u) % 2u == 0u) ? "inf_spread" : "inf_incoming")
		                      : (anyMoved ? "tnk_spread" : "arty_tnk");
		modvoice::shout(caller, cat);
	}
}

void dirtPuff(const DROID *d)
{
	Vector3i pos(d->pos.x + static_cast<int>((d->id * 13u + gameTime / 97u) % 41u) - 20, d->pos.z + 6,
	             d->pos.y + static_cast<int>((d->id * 7u + gameTime / 89u) % 41u) - 20);
	addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING, false, nullptr, 0, gameTime);
}
} // namespace

void onIndirectFlight(const PROJECTILE *psProj, int timeSoFar, int flightTime)
{
	if (psProj == nullptr || psProj->psWStats == nullptr || psProj->penetratingProjectile) { return; }
	const int remaining = flightTime - timeSoFar;
	const int remainingBefore = flightTime - (timeSoFar - static_cast<int>(deltaGameTime));
	const bool crossing = remaining <= LEAD_MS && (remainingBefore > LEAD_MS || timeSoFar <= static_cast<int>(deltaGameTime));
	if (!crossing || remaining < 0) { return; }
	whistle(psProj);
	react(psProj);
}

void tick()
{
	// alte Warnungen vergessen
	if (!s_alerts.empty())
	{
		s_alerts.erase(std::remove_if(s_alerts.begin(), s_alerts.end(), [](const Alert &a) { return gameTime - a.time >= ALERT_MERGE_MS; }), s_alerts.end());
	}
	for (auto it = s_units.begin(); it != s_units.end();)
	{
		Entry &e = it->second;
		if (e.phase == PH_DONE)
		{
			if (gameTime >= e.until) { it = s_units.erase(it); } else { ++it; }
			continue;
		}
		DROID *d = findDroid(it->first, e.player);
		if (d == nullptr || d->died)
		{
			it = s_units.erase(it);
			continue;
		}
		if (e.phase == PH_SPREAD)
		{
			const bool arrived = d->order.type == DORDER_NONE;
			if (!arrived && d->order.type != DORDER_MOVE)
			{
				e.phase = PH_DONE; e.until = gameTime + COOLDOWN_MS;   // neuer Befehl vom Spieler: nichts weiter tun
			}
			else if (arrived || gameTime >= e.until)
			{
				const int tx = map_coord(d->pos.x), ty = map_coord(d->pos.y);
				if (infantry(d) && arrived && !inCover(d) && !modterrain::tileGivesCover(tx, ty)
				    && modlayer::layerOf(d) == modlayer::SURFACE)
				{
					e.phase = PH_DIG;
					e.until = gameTime + DIG_MS;
					e.tileX = tx; e.tileY = ty;
					modvoice::shout(d, "inf_dig");
				}
				else
				{
					e.phase = PH_DONE; e.until = gameTime + COOLDOWN_MS;
				}
			}
			++it;
			continue;
		}
		// PH_DIG
		if (d->order.type != DORDER_NONE || map_coord(d->pos.x) != e.tileX || map_coord(d->pos.y) != e.tileY)
		{
			e.phase = PH_DONE; e.until = gameTime + COOLDOWN_MS;   // weggegangen / neuer Befehl: Graben abgebrochen
			++it;
			continue;
		}
		if ((gameTime / 450u) != ((gameTime - deltaGameTime) / 450u))
		{
			dirtPuff(d);
		}
		if (gameTime >= e.until)
		{
			if (!modterrain::tileGivesCover(e.tileX, e.tileY))
			{
				const unsigned dir = mix(it->first) & 0xffffu;
				modterrain::dig(e.tileX, e.tileY, DIG_DEPTH, DIG_BERM, dir);
			}
			e.phase = PH_DONE; e.until = gameTime + COOLDOWN_MS;
		}
		++it;
	}
}

void reset()
{
	s_units.clear();
	s_alerts.clear();
	s_nextWhistle = 0;
	for (int &t : s_track) { t = -2; }
}

void spreadUnits(const std::vector<DROID *> &unitsIn)
{
	std::vector<DROID *> units;
	for (DROID *d : unitsIn)
	{
		if (d != nullptr && !d->died && ground(d)) { units.push_back(d); }
	}
	if (units.size() < 2) { return; }
	std::sort(units.begin(), units.end(), [](const DROID *a, const DROID *b) { return a->id < b->id; });
	int64_t sx = 0, sy = 0;
	bool anyVehicle = false;
	for (const DROID *d : units)
	{
		sx += d->pos.x; sy += d->pos.y;
		anyVehicle = anyVehicle || !infantry(d);
	}
	const Vector2i c(static_cast<int>(sx / static_cast<int64_t>(units.size())), static_cast<int>(sy / static_cast<int64_t>(units.size())));
	// Sonnenblumen-Muster: gleichmaessige Abstaende (Infanterie 2 Felder, mit Fahrzeugen 2,5 Felder)
	const double spacing = (anyVehicle ? 2.5 : 2.0) * TILE_UNITS;
	const double cstep = spacing * 0.58;
	const double golden = 2.399963229728653;
	std::vector<Vector2i> spots;
	for (size_t k = 0; k < units.size(); ++k)
	{
		const double r = cstep * std::sqrt(static_cast<double>(k) + 0.6);
		const double a = static_cast<double>(k) * golden;
		spots.emplace_back(c.x + static_cast<int>(r * std::cos(a)), c.y + static_cast<int>(r * std::sin(a)));
	}
	// Zuordnung: die Einheiten, die am weitesten aussen stehen, waehlen zuerst den naechsten freien Platz
	std::vector<size_t> order(units.size());
	for (size_t i = 0; i < order.size(); ++i) { order[i] = i; }
	auto d2 = [](const Vector2i &a, const Vector2i &b) { const int64_t dx = a.x - b.x, dy = a.y - b.y; return dx * dx + dy * dy; };
	std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return d2(units[a]->pos.xy(), c) > d2(units[b]->pos.xy(), c); });
	std::vector<bool> taken(spots.size(), false);
	for (size_t oi : order)
	{
		DROID *d = units[oi];
		size_t best = spots.size();
		int64_t bestD = 0;
		for (size_t s = 0; s < spots.size(); ++s)
		{
			if (taken[s]) { continue; }
			const int64_t dd = d2(d->pos.xy(), spots[s]);
			if (best == spots.size() || dd < bestD) { best = s; bestD = dd; }
		}
		if (best == spots.size()) { break; }
		taken[best] = true;
		Vector2i to = spots[best];
		if (!freeTile(d, to))
		{
			// Platz liegt auf Hindernis/Wasser: ein Stueck weiter nach aussen oder innen suchen
			bool ok = false;
			for (int step = 1; step <= 3 && !ok; ++step)
			{
				for (int sgn = 1; sgn >= -1 && !ok; sgn -= 2)
				{
					const Vector2i cand = to + (to - c) * (sgn * step) / 4;
					if (freeTile(d, cand)) { to = cand; ok = true; }
				}
			}
			if (!ok) { continue; }
		}
		sendDroidInfo(d, DroidOrder(DORDER_MOVE, to), false, currentOrderSource());
	}
	const DROID *caller = units.front();
	modvoice::shout(caller, infantry(caller) ? "inf_spread" : "tnk_spread");
}
} // namespace modincoming
