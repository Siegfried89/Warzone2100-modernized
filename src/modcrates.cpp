// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Munitionskisten per Lkw (siehe modcrates.h)

#include "modcrates.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "action.h"
#include "display.h"
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
#include "structure.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace modcrates
{
namespace
{
constexpr unsigned TICK_MS = 250;
constexpr int LOAD_RANGE = 5 * TILE_UNITS / 2;
constexpr int DROP_RANGE = 3 * TILE_UNITS / 2;
constexpr unsigned JOB_TIMEOUT_MS = 90000;

struct Truck
{
	uint8_t player = 0;
	int crates = 0;
	int job = 0;          // 0 keiner, 1 laden, 2 abladen
	int tx = 0, ty = 0;   // Ziel (Depot bzw. Abladestelle)
	int mx = 0, my = 0;   // eigener Fahrbefehl (um Spieler-Befehle zu erkennen)
	unsigned since = 0;
	int tries = 0;
};
struct Boom
{
	unsigned due;
	int x, y, z, n;
};

std::map<uint32_t, Truck> s_trucks;
std::map<uint32_t, int> s_piles;     // Kistenhaufen (Bauwerk) -> Kisten
std::vector<Boom> s_booms;
unsigned s_nextTick = 0;

bool idHas(const STRUCTURE *s, const char *what)
{
	return s != nullptr && s->pStructureType != nullptr && s->pStructureType->id.toUtf8().find(what) != std::string::npos;
}

bool isDepot(const STRUCTURE *s)
{
	return idHas(s, "AmmoDepot") && !s->died && s->status == SS_BUILT;
}

bool isPileStats(const STRUCTURE *s)
{
	return idHas(s, "AmmoBoxDrop");
}

int64_t dist2(int ax, int ay, int bx, int by)
{
	const int64_t dx = ax - bx, dy = ay - by;
	return dx * dx + dy * dy;
}

const STRUCTURE *nearestDepot(unsigned player, int x, int y)
{
	if (player >= MAX_PLAYERS) { return nullptr; }
	const STRUCTURE *best = nullptr;
	int64_t bd = 0;
	for (const STRUCTURE *s : gameWorld.objects.structures[player])
	{
		if (!isDepot(s)) { continue; }
		const int64_t d = dist2(s->pos.x, s->pos.y, x, y);
		if (best == nullptr || d < bd) { best = s; bd = d; }
	}
	return best;
}

void moveTo(DROID *d, Truck &t, int x, int y)
{
	x = std::clamp(x, TILE_UNITS, world_coord(gameWorld.map.width - 2));
	y = std::clamp(y, TILE_UNITS, world_coord(gameWorld.map.height - 2));
	orderDroidLoc(d, DORDER_MOVE, static_cast<UDWORD>(x), static_cast<UDWORD>(y), ModeImmediate);
	t.mx = x; t.my = y;
}

bool ownMove(const DROID *d, const Truck &t)
{
	return d->order.type == DORDER_MOVE && d->order.pos.x == t.mx && d->order.pos.y == t.my;
}

STRUCTURE_STATS *pileStats(int n)
{
	char name[40];
	snprintf(name, sizeof(name), "A0BaBaAmmoBoxDrop%d", std::clamp(n, 1, 4));
	const int idx = getStructStatFromName(WzString::fromUtf8(name));
	return idx >= 0 ? &asStructureStats[idx] : nullptr;
}

bool tileFree(int tx, int ty)
{
	if (tx < 1 || ty < 1 || tx >= gameWorld.map.width - 1 || ty >= gameWorld.map.height - 1) { return false; }
	MAPTILE *t = mapTile(gameWorld.map, tx, ty);
	return t != nullptr && !TileIsOccupied(t) && terrainType(t) != TER_WATER && terrainType(t) != TER_CLIFFFACE;
}

bool dropPile(DROID *d, Truck &t)
{
	STRUCTURE_STATS *st = pileStats(t.crates);
	if (st == nullptr) { return false; }
	const int cx = map_coord(t.tx), cy = map_coord(t.ty);
	// Zielfeld, sonst Ring 1 und 2 (feste Reihenfolge)
	for (int r = 0; r <= 2; ++r)
	{
		for (int dy = -r; dy <= r; ++dy)
		{
			for (int dx = -r; dx <= r; ++dx)
			{
				if (std::max(std::abs(dx), std::abs(dy)) != r) { continue; }
				const int tx = cx + dx, ty = cy + dy;
				if (!tileFree(tx, ty)) { continue; }
				// nicht auf das Feld des Lkw selbst
				if (tx == map_coord(d->pos.x) && ty == map_coord(d->pos.y)) { continue; }
				STRUCTURE *s = buildStructureDir(gameWorld, st, static_cast<UDWORD>(world_coord(tx) + TILE_UNITS / 2), static_cast<UDWORD>(world_coord(ty) + TILE_UNITS / 2),
				                                 static_cast<uint16_t>((d->id * 9973u) & 0xc000u), d->player, false, generateSynchronisedObjectId(), false, true, true);
				if (s == nullptr) { continue; }
				s->status = SS_BUILT;
				buildingComplete(s, gameWorld);
				s_piles[s->id] = t.crates;
				t.crates = 0;
				return true;
			}
		}
	}
	return false;
}

bool seen(int x, int y)
{
	if (!worldOnMap(gameWorld.map, x, y)) { return false; }
	MAPTILE *t = mapTile(gameWorld.map, map_coord(x), map_coord(y));
	return t != nullptr && TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(t);
}

void effects(int x, int y, int z, int n)
{
	if (!seen(x, y)) { return; }
	const int m = std::clamp(n, 1, 6);
	static iIMDBaseShape *crate = nullptr;
	static bool tried = false;
	if (!tried) { tried = true; crate = modelGet("ammocrate.pie"); }
	// Feuerball und Druckwelle
	for (int i = 0; i < 4 + 2 * m; ++i)
	{
		Vector3i p(x + rand() % (60 + 30 * m) - (30 + 15 * m), z + 20 + rand() % (40 + 20 * m), y + rand() % (60 + 30 * m) - (30 + 15 * m));
		addEffect(&p, EFFECT_EXPLOSION, (i % 2 == 0) ? EXPLOSION_TYPE_LARGE : EXPLOSION_TYPE_MEDIUM, false, nullptr, 0);
	}
	Vector3i c(x, z + 10, y);
	addEffect(&c, EFFECT_EXPLOSION, EXPLOSION_TYPE_SHOCKWAVE, false, nullptr, 0);
	// spektakulaer viele Teile: Kisten, Deckel, Patronen fliegen brennend mit Rauchspur durch die Luft
	for (int i = 0; i < 10 + 6 * m; ++i)
	{
		Vector3i p(x + rand() % 40 - 20, z + 30, y + rand() % 40 - 20);
		Vector3f vel(static_cast<float>(rand() % (700 + 120 * m) - (350 + 60 * m)), 0.f, static_cast<float>(rand() % (700 + 120 * m) - (350 + 60 * m)));
		if (crate != nullptr && i % 2 == 0)
		{
			addEffect(&p, EFFECT_GRAVITON, GRAVITON_TYPE_TURRET, true, crate->displayModel(), 0, graphicsTime, nullptr, &vel);
		}
		else
		{
			addEffect(&p, EFFECT_GRAVITON, GRAVITON_TYPE_EMITTING_DR, false, nullptr, 0);
		}
	}
	for (int i = 0; i < 2 + m / 2; ++i)
	{
		Vector3i p(x + rand() % 80 - 40, z + 10, y + rand() % 80 - 40);
		effectGiveAuxVar(50 + 10 * m);
		effectGiveAuxVarSec(8000 + 1000 * m);
		addEffect(&p, EFFECT_FIRE, FIRE_TYPE_SMOKY, false, nullptr, 0);
	}
	shakeStart(300 + 60 * static_cast<unsigned>(m));
	audio_PlayStaticTrack(x, y, ID_SOUND_EXPLOSION);
}

void areaDamage(int x, int y, int radius, int base)
{
	const unsigned it = gameTime - deltaGameTime / 2 + 1;
	std::vector<BASE_OBJECT *> hit;
	for (BASE_OBJECT *o : gridStartIterate(x, y, static_cast<uint32_t>(radius)))
	{
		if (o != nullptr && !o->died) { hit.push_back(o); }
	}
	std::sort(hit.begin(), hit.end(), [](const BASE_OBJECT *a, const BASE_OBJECT *b) { return a->id < b->id; });
	for (BASE_OBJECT *o : hit)
	{
		if (o->died) { continue; }
		const int dist = iHypot(Vector2i(o->pos.x - x, o->pos.y - y));
		if (dist > radius) { continue; }
		const int pct = 100 - 70 * dist / std::max(1, radius);
		const unsigned dmg = static_cast<unsigned>(std::max(1, base * pct / 100));
		const unsigned heat = dmg * 4 / 10;
		const unsigned kin = dmg - heat;
		switch (o->type)
		{
		case OBJ_DROID:
		{
			DROID *d = static_cast<DROID *>(o);
			if (d->isFlying()) { break; }
			droidDamage(gameWorld, d, nullptr, kin, WC_KINETIC, WSC_BOMB, it, false, 0, false);
			if (!d->died) { droidDamage(gameWorld, d, nullptr, heat, WC_HEAT, WSC_FLAME, it, false, 0, false); }
			break;
		}
		case OBJ_STRUCTURE:
		{
			STRUCTURE *s = static_cast<STRUCTURE *>(o);
			structureDamage(gameWorld, s, nullptr, kin, WC_KINETIC, WSC_BOMB, it, false, 0, false);
			if (!s->died) { structureDamage(gameWorld, s, nullptr, heat, WC_HEAT, WSC_FLAME, it, false, 0, false); }
			break;
		}
		case OBJ_FEATURE:
		{
			FEATURE *f = static_cast<FEATURE *>(o);
			if (f->psStats != nullptr && f->psStats->damageable)
			{
				featureDamage(gameWorld, f, dmg, WC_KINETIC, WSC_BOMB, it, false, 0, false);
			}
			break;
		}
		default:
			break;
		}
	}
}
} // namespace

int capacity(const DROID *d)
{
	if (d == nullptr || d->isVtol() || d->isTransporter()) { return 0; }
	const CONSTRUCT_STATS *cs = d->getConstructStats();
	if (cs == nullptr) { return 0; }
	const std::string id = cs->id.toUtf8();
	if (id == "ScavBed1") { return 2; }
	if (id == "ScavBed3") { return 3; }
	if (id == "ScavBed2") { return 4; }
	if (id == "ScavBucket") { return 4; }
	return 0;
}

int crates(const DROID *d)
{
	if (d == nullptr) { return 0; }
	auto it = s_trucks.find(d->id);
	return it == s_trucks.end() ? 0 : it->second.crates;
}

bool busy(uint32_t droidId)
{
	auto it = s_trucks.find(droidId);
	return it != s_trucks.end() && (it->second.crates > 0 || it->second.job != 0);
}

bool startOrder(DROID *d, int x, int y, uint16_t code)
{
	if (code != CODE_LOAD && code != CODE_UNLOAD) { return false; }
	if (d == nullptr || capacity(d) <= 0) { return true; }   // falscher Typ: Befehl verwerfen
	Truck &t = s_trucks[d->id];
	t.player = static_cast<uint8_t>(d->player);
	if (code == CODE_UNLOAD && t.crates <= 0) { return true; }
	t.job = (code == CODE_LOAD) ? 1 : 2;
	t.tx = x; t.ty = y;
	t.mx = -1; t.my = -1;
	t.since = gameTime;
	t.tries = 0;
	d->order = DroidOrder(DORDER_NONE);
	actionDroid(d, DACTION_NONE);
	return true;
}

bool loadOrderSelected(unsigned player, const STRUCTURE *s)
{
	if (player >= MAX_PLAYERS || s == nullptr || s->player != player || !isDepot(s)) { return false; }
	bool any = false;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (d == nullptr || !d->selected || d->died) { continue; }
		if (capacity(d) <= 0 || crates(d) >= capacity(d)) { continue; }
		DroidOrder o(DORDER_FORTIFY, s->pos.xy());
		o.direction = CODE_LOAD;
		sendDroidInfo(d, o, false, currentOrderSource());
		any = true;
	}
	return any;
}

bool wantsUnload(const DROID *d)
{
	return crates(d) > 0;
}

void onStructureDestroyed(const STRUCTURE *s)
{
	if (s == nullptr) { return; }
	auto it = s_piles.find(s->id);
	if (it == s_piles.end())
	{
		if (!isPileStats(s)) { return; }
		s_booms.push_back({ gameTime + 60, s->pos.x, s->pos.y, s->pos.z, 2 });   // Haufen ohne Zaehlung (z. B. nach dem Laden)
		return;
	}
	const int n = it->second;
	s_piles.erase(it);
	if (n > 0) { s_booms.push_back({ gameTime + 60 + static_cast<unsigned>(s->id % 4u) * 40u, s->pos.x, s->pos.y, s->pos.z, n }); }
}

void onDroidDestroyed(const DROID *d)
{
	if (d == nullptr) { return; }
	auto it = s_trucks.find(d->id);
	if (it == s_trucks.end()) { return; }
	if (it->second.crates > 0) { s_booms.push_back({ gameTime + 40, d->pos.x, d->pos.y, d->pos.z, it->second.crates }); }
	s_trucks.erase(it);
}

void tick()
{
	// Explosionen (sofort faellig, Kettenreaktion durch neue Eintraege)
	if (!s_booms.empty())
	{
		std::vector<Boom> due, keep;
		for (const Boom &b : s_booms) { (b.due <= gameTime ? due : keep).push_back(b); }
		s_booms.swap(keep);
		for (const Boom &b : due)
		{
			effects(b.x, b.y, b.z, b.n);
			const int m = std::clamp(b.n, 1, 6);
			areaDamage(b.x, b.y, 5 * TILE_UNITS / 2 + m * TILE_UNITS / 2, 160 * m);
		}
	}

	if (gameTime < s_nextTick) { return; }
	s_nextTick = gameTime + TICK_MS;
	for (auto it = s_trucks.begin(); it != s_trucks.end();)
	{
		Truck &t = it->second;
		DROID *d = IdToDroid(gameWorld.objects, it->first, t.player);
		if (d == nullptr || d->died) { it = s_trucks.erase(it); continue; }
		if (t.job != 0)
		{
			const bool mine = ownMove(d, t);
			if (!mine && d->order.type != DORDER_NONE && t.mx >= 0)
			{
				t.job = 0;   // Spieler hat einen anderen Befehl gegeben
			}
			else if (gameTime - t.since > JOB_TIMEOUT_MS)
			{
				t.job = 0;
			}
			else if (t.job == 1)
			{
				const STRUCTURE *dep = nearestDepot(t.player, t.tx, t.ty);
				if (dep == nullptr) { t.job = 0; }
				else if (dist2(d->pos.x, d->pos.y, dep->pos.x, dep->pos.y) <= static_cast<int64_t>(LOAD_RANGE) * LOAD_RANGE)
				{
					t.crates = capacity(d);   // voll geladen
					t.job = 0;
					if (d->order.type == DORDER_MOVE) { orderDroid(d, DORDER_STOP, ModeImmediate); }
				}
				else if (d->order.type == DORDER_NONE && t.tries < 4)
				{
					// neben das Depot, auf der Seite des Lkw
					const int dx = d->pos.x - dep->pos.x, dy = d->pos.y - dep->pos.y;
					const int len = std::max(1, iHypot(Vector2i(dx, dy)));
					moveTo(d, t, dep->pos.x + dx * (3 * TILE_UNITS / 2) / len, dep->pos.y + dy * (3 * TILE_UNITS / 2) / len);
					t.tries++;
				}
			}
			else if (t.job == 2)
			{
				if (t.crates <= 0) { t.job = 0; }
				else if (dist2(d->pos.x, d->pos.y, t.tx, t.ty) <= static_cast<int64_t>(DROP_RANGE) * DROP_RANGE
				         || (d->order.type == DORDER_NONE && t.tries >= 4 && dist2(d->pos.x, d->pos.y, t.tx, t.ty) <= static_cast<int64_t>(3 * TILE_UNITS) * (3 * TILE_UNITS)))
				{
					if (d->order.type == DORDER_MOVE) { orderDroid(d, DORDER_STOP, ModeImmediate); }
					dropPile(d, t);
					t.job = 0;
				}
				else if (d->order.type == DORDER_NONE && t.tries < 4)
				{
					moveTo(d, t, t.tx, t.ty);
					t.tries++;
				}
				else if (d->order.type == DORDER_NONE)
				{
					t.job = 0;
				}
			}
		}
		if (t.job == 0 && t.crates <= 0) { it = s_trucks.erase(it); continue; }
		++it;
	}
	// zerstoerte/abgerissene Haufen vergessen (zur Sicherheit; normal ueber onStructureDestroyed)
}

void reset()
{
	s_trucks.clear();
	s_piles.clear();
	s_booms.clear();
	s_nextTick = 0;
}
} // namespace modcrates
