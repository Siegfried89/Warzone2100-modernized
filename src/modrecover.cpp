// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Turm-Teile bergen und Reparatur mit Teil (siehe modrecover.h)

#include "modrecover.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"

#include "ai.h"
#include "droid.h"
#include "effects.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "mapgrid.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "ordersource.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace modrecover
{
namespace
{
constexpr unsigned SPARE_MS = 150000;        // Ersatzteil aus der Werkstatt nach 2,5 Minuten Warten
constexpr int NEAR_FACILITY = 3 * TILE_UNITS;
constexpr int HELPER_RANGE = 16 * TILE_UNITS;
constexpr unsigned TICK_MS = 250;

struct Part
{
	FEATURE *feature = nullptr;
	uint32_t ownerId = 0;
	uint8_t ownerPlayer = 0;
	unsigned weapon = 0;
	uint32_t helperId = 0;
	unsigned helperRetry = 0;
};
struct Wait
{
	uint8_t player = 0;
	unsigned weapon = 0;
	unsigned since = 0;
	bool ready = false;
	unsigned nextRtr = 0;
};
struct Carry
{
	uint8_t player = 0;          // Spieler des Traegers
	uint32_t ownerId = 0;
	uint8_t ownerPlayer = 0;
	unsigned weapon = 0;
};

std::map<uint32_t, Part> s_parts;     // Feature-ID -> Teil
std::map<uint32_t, Wait> s_wait;      // Fahrzeug-ID -> wartet auf seinen Turm
std::map<uint32_t, Carry> s_carry;    // Traeger-ID -> traegt ein Teil
unsigned s_nextTick = 0;
FEATURE_STATS *s_partStats = nullptr;
const FEATURE_STATS *s_statsBase = nullptr;

FEATURE_STATS *partStats()
{
	if (s_statsBase != asFeatureStats.data())
	{
		s_statsBase = asFeatureStats.data();
		s_partStats = nullptr;
		for (auto &s : asFeatureStats)
		{
			if (s.id == "ModTurretPart") { s_partStats = &s; break; }
		}
	}
	return s_partStats;
}

bool support(const DROID *d)
{
	return d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT || d->droidType == DROID_REPAIR || d->droidType == DROID_CYBORG_REPAIR;
}

int64_t dist2(const Vector2i &a, const Vector2i &b)
{
	const int64_t dx = a.x - b.x, dy = a.y - b.y;
	return dx * dx + dy * dy;
}

const STRUCTURE *nearestFacility(unsigned player, const Vector2i &pos)
{
	if (player >= MAX_PLAYERS) { return nullptr; }
	const STRUCTURE *best = nullptr;
	int64_t bestD = 0;
	for (const STRUCTURE *s : gameWorld.objects.structures[player])
	{
		if (s->died || s->status != SS_BUILT || s->pStructureType->type != REF_REPAIR_FACILITY) { continue; }
		const int64_t d = dist2(s->pos.xy(), pos);
		if (best == nullptr || d < bestD) { best = s; bestD = d; }
	}
	return best;
}

bool nearFacility(unsigned player, const Vector2i &pos)
{
	const STRUCTURE *s = nearestFacility(player, pos);
	return s != nullptr && dist2(s->pos.xy(), pos) <= static_cast<int64_t>(NEAR_FACILITY) * NEAR_FACILITY;
}

void sendToFacility(DROID *d, unsigned player, const Vector2i &from)
{
	const STRUCTURE *s = nearestFacility(player, from);
	if (s == nullptr) { return; }
	// neben die Werkstatt (nicht hinein), Seite aus der ID
	const int side = (d->id & 1u) ? TILE_UNITS * 3 / 2 : -TILE_UNITS * 3 / 2;
	orderDroidLoc(d, DORDER_MOVE, static_cast<UDWORD>(std::max(TILE_UNITS, s->pos.x + side)), static_cast<UDWORD>(std::max(TILE_UNITS, s->pos.y + TILE_UNITS)), ModeImmediate);
}

bool freeSpot(int x, int y)
{
	const int tx = map_coord(x), ty = map_coord(y);
	if (tx < 1 || ty < 1 || tx >= gameWorld.map.width - 1 || ty >= gameWorld.map.height - 1) { return false; }
	MAPTILE *t = mapTile(gameWorld.map, tx, ty);
	return t != nullptr && !TileIsOccupied(t) && terrainType(t) != TER_WATER && terrainType(t) != TER_CLIFFFACE;
}
} // namespace

void onTurretLost(DROID *d, unsigned weaponStat)
{
	if (d == nullptr || d->died) { return; }
	Wait w;
	w.player = static_cast<uint8_t>(d->player);
	w.weapon = weaponStat;
	w.since = gameTime;
	w.nextRtr = gameTime;
	s_wait[d->id] = w;

	// Teil faellt neben das Fahrzeug
	FEATURE_STATS *fs = partStats();
	if (fs != nullptr)
	{
		const uint16_t baseDir = static_cast<uint16_t>((d->id * 40503u + gameTime * 7u) & 0xffffu);
		for (int k = 0; k < 6; ++k)
		{
			const uint16_t dir = static_cast<uint16_t>(baseDir + k * DEG(60));
			const int dist = TILE_UNITS + static_cast<int>(d->id % 48u);
			const int x = d->pos.x + iSinR(dir, dist), y = d->pos.y + iCosR(dir, dist);
			if (!freeSpot(x, y)) { continue; }
			FEATURE *f = buildFeature(gameWorld, fs, static_cast<UDWORD>(x), static_cast<UDWORD>(y), false);
			if (f == nullptr) { continue; }
			f->rot.direction = dir;
			Part p;
			p.feature = f;
			p.ownerId = d->id;
			p.ownerPlayer = static_cast<uint8_t>(d->player);
			p.weapon = weaponStat;
			s_parts[f->id] = p;
			break;
		}
	}
	// ohne Waffe sofort in die Werkstatt
	if (nearestFacility(d->player, d->pos.xy()) != nullptr)
	{
		orderDroid(d, DORDER_RTR, ModeImmediate);
	}
}

bool partReady(const DROID *d)
{
	auto it = s_wait.find(d->id);
	return it == s_wait.end() || it->second.ready;
}

void onRestored(const DROID *d)
{
	s_wait.erase(d->id);
}

void onFeatureRemoved(const FEATURE *f)
{
	s_parts.erase(f->id);
}

bool blocksPickup(const FEATURE *f, const DROID *d)
{
	if (f == nullptr || d == nullptr || f->psStats != partStats()) { return false; }
	auto it = s_parts.find(f->id);
	if (it == s_parts.end()) { return false; }
	const bool friendly = d->player == it->second.ownerPlayer || (d->player < MAX_PLAYERS && it->second.ownerPlayer < MAX_PLAYERS && aiCheckAlliances(d->player, it->second.ownerPlayer));
	return friendly && !support(d);
}

bool onPickup(FEATURE *f, DROID *d)
{
	if (f == nullptr || d == nullptr || f->psStats != partStats()) { return false; }
	auto it = s_parts.find(f->id);
	if (it == s_parts.end()) { return false; }   // unbekanntes Teil (z. B. nach dem Laden): Schrott wie ein Oelfass
	const Part p = it->second;
	s_parts.erase(it);
	const bool friendly = d->player == p.ownerPlayer || (d->player < MAX_PLAYERS && p.ownerPlayer < MAX_PLAYERS && aiCheckAlliances(d->player, p.ownerPlayer));
	if (!friendly)
	{
		return false;   // Gegner: verschrottet (normales Oelfass-Verhalten = Energie)
	}
	Carry c;
	c.player = static_cast<uint8_t>(d->player);
	c.ownerId = p.ownerId;
	c.ownerPlayer = p.ownerPlayer;
	c.weapon = p.weapon;
	s_carry[d->id] = c;
	if (support(d) || !isHumanPlayer(d->player))
	{
		const DROID *owner = IdToDroid(gameWorld.objects, p.ownerId, p.ownerPlayer);
		sendToFacility(d, p.ownerPlayer, owner ? owner->pos.xy() : d->pos.xy());
	}
	return true;
}

void tick()
{
	if (gameTime < s_nextTick) { return; }
	s_nextTick = gameTime + TICK_MS;

	// Traeger: an der Werkstatt angekommen -> Teil abgeliefert
	for (auto it = s_carry.begin(); it != s_carry.end();)
	{
		DROID *c = IdToDroid(gameWorld.objects, it->first, it->second.player);
		if (c == nullptr || c->died) { it = s_carry.erase(it); continue; }   // Teil verloren -> Ersatzteil nach Wartezeit
		if (nearFacility(it->second.ownerPlayer, c->pos.xy()))
		{
			auto w = s_wait.find(it->second.ownerId);
			if (w != s_wait.end()) { w->second.ready = true; }
			it = s_carry.erase(it);
			continue;
		}
		++it;
	}

	// wartende Fahrzeuge
	for (auto it = s_wait.begin(); it != s_wait.end();)
	{
		Wait &w = it->second;
		DROID *d = IdToDroid(gameWorld.objects, it->first, w.player);
		if (d == nullptr || d->died) { it = s_wait.erase(it); continue; }
		if (!w.ready)
		{
			const bool atFac = nearFacility(w.player, d->pos.xy());
			if (atFac && gameTime - w.since >= SPARE_MS)
			{
				w.ready = true;   // Werkstatt hat ein Ersatzteil gebaut
			}
			else if (!atFac && d->order.type == DORDER_NONE && gameTime >= w.nextRtr && nearestFacility(w.player, d->pos.xy()) != nullptr)
			{
				orderDroid(d, DORDER_RTR, ModeImmediate);   // steht untaetig herum: zurueck in die Werkstatt
				w.nextRtr = gameTime + 8000;
			}
		}
		++it;
	}

	// liegende Teile: ein freies Bau-/Reparaturfahrzeug des Besitzers in der Naehe holt sie
	for (auto &kv : s_parts)
	{
		Part &p = kv.second;
		if (p.feature == nullptr || p.feature->died) { continue; }
		if (p.helperId != 0)
		{
			DROID *h = IdToDroid(gameWorld.objects, p.helperId, p.ownerPlayer);
			if (h != nullptr && !h->died && h->order.type == DORDER_RECOVER) { continue; }
			p.helperId = 0;
			p.helperRetry = gameTime + 6000;
		}
		if (gameTime < p.helperRetry) { continue; }
		DROID *best = nullptr;
		int64_t bestD = 0;
		for (BASE_OBJECT *o : gridStartIterate(p.feature->pos.x, p.feature->pos.y, HELPER_RANGE))
		{
			if (o->type != OBJ_DROID || o->died || o->player != p.ownerPlayer) { continue; }
			DROID *h = static_cast<DROID *>(o);
			if (!support(h) || h->order.type != DORDER_NONE || h->isVtol()) { continue; }
			if (s_carry.count(h->id) != 0) { continue; }
			const int64_t dd = dist2(h->pos.xy(), p.feature->pos.xy());
			if (best == nullptr || dd < bestD || (dd == bestD && h->id < best->id)) { best = h; bestD = dd; }
		}
		if (best != nullptr)
		{
			orderDroidObj(best, DORDER_RECOVER, p.feature, ModeImmediate);
			p.helperId = best->id;
		}
		else
		{
			p.helperRetry = gameTime + 3000;
		}
	}
}

void reset()
{
	s_parts.clear();
	s_wait.clear();
	s_carry.clear();
	s_nextTick = 0;
	s_statsBase = nullptr;
	s_partStats = nullptr;
}

void recoverWithSelection(const std::vector<DROID *> &units)
{
	std::vector<FEATURE *> free;
	for (auto &kv : s_parts)
	{
		if (kv.second.feature != nullptr && !kv.second.feature->died) { free.push_back(kv.second.feature); }
	}
	for (DROID *d : units)
	{
		if (d == nullptr || d->died || d->isVtol() || !support(d)) { continue; }
		FEATURE *best = nullptr;
		int64_t bestD = 0;
		for (FEATURE *f : free)
		{
			auto it = s_parts.find(f->id);
			if (it == s_parts.end()) { continue; }
			const bool friendly = d->player == it->second.ownerPlayer || (d->player < MAX_PLAYERS && it->second.ownerPlayer < MAX_PLAYERS && aiCheckAlliances(d->player, it->second.ownerPlayer));
			if (!friendly) { continue; }
			const int64_t dd = dist2(d->pos.xy(), f->pos.xy());
			if (best == nullptr || dd < bestD) { best = f; bestD = dd; }
		}
		if (best == nullptr) { continue; }
		free.erase(std::remove(free.begin(), free.end(), best), free.end());
		sendDroidInfo(d, DroidOrder(DORDER_RECOVER, best), false, currentOrderSource());
	}
}

unsigned partWeapon(const FEATURE *f)
{
	if (f == nullptr) { return 0; }
	auto it = s_parts.find(f->id);
	return it == s_parts.end() ? 0 : it->second.weapon;
}

bool isPartStats(const void *psFeatureStats)
{
	return psFeatureStats != nullptr && psFeatureStats == partStats();
}

void repairEffects(const STRUCTURE *s, const DROID *target, int &pitchWobble, int &dirWobble)
{
	pitchWobble = 0;
	dirWobble = 0;
	if (s == nullptr || target == nullptr) { return; }
	if (dist2(s->pos.xy(), target->pos.xy()) > static_cast<int64_t>(TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2)) { return; }
	// Arm bewegt sich leicht (Anzeige)
	pitchWobble = iSinSR(static_cast<int32_t>(graphicsTime + s->id * 97u), 1100, DEG(7));
	dirWobble = iSinSR(static_cast<int32_t>(graphicsTime + s->id * 131u), 1700, DEG(9));
	if (!s->visibleForLocalDisplay()) { return; }
	static std::unordered_map<uint32_t, unsigned> s_nextSpark, s_nextZap;
	static int s_zap[3] = { -2, -2, -2 };
	unsigned &ns = s_nextSpark[s->id];
	if (graphicsTime >= ns)
	{
		ns = graphicsTime + 140 + static_cast<unsigned>(rand() % 160);
		// blaue Funken am Fahrzeug
		Vector3i p(target->pos.x + rand() % 50 - 25, target->pos.z + 18 + rand() % 22, target->pos.y + rand() % 50 - 25);
		addEffect(&p, EFFECT_EXPLOSION, EXPLOSION_TYPE_TESLA, false, nullptr, 0);
	}
	unsigned &nz = s_nextZap[s->id];
	if (graphicsTime >= nz)
	{
		nz = graphicsTime + 1100 + static_cast<unsigned>(rand() % 900);
		const int k = rand() % 3;
		if (s_zap[k] == -2)
		{
			char fn[32];
			snprintf(fn, sizeof(fn), "repair_zap_%d.ogg", k);
			s_zap[k] = audio_GetTrackID(fn);
		}
		if (s_zap[k] >= 0) { audio_PlayStaticTrack(target->pos.x, target->pos.y, s_zap[k]); }
	}
}
} // namespace modrecover
