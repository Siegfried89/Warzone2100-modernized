// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Munitions-Nachschub Fabrik -> Lkw -> Depot -> Front (siehe modcrates.h)

#include "modcrates.h"
#include "modapc.h"
#include "modshockwave.h"
#include "modammo.h"
#include "modcontrol.h"
#include "modoil.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "action.h"
#include "display.h"
#include "droid.h"
#include "effects.h"
#include "miscimd.h"
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
#include <set>
#include <string>
#include <vector>

namespace modcrates
{
// MOD (21.11.): untaetige Fahrzeuge bekommen von Warzone selbst "Stellung halten" (DORDER_GUARD ohne Ziel) - das ist kein Spieler-Befehl
static bool idleOrder(const DROID *d)
{
	return d->order.type == DORDER_NONE || d->order.type == DORDER_HOLD || d->order.type == DORDER_STOP
	       || (d->order.type == DORDER_GUARD && d->order.psObj == nullptr);
}
namespace
{
constexpr unsigned TICK_MS = 250;
constexpr int LOAD_RANGE = 5 * DIST_TILE / 2;
constexpr int DROP_RANGE = 3 * DIST_TILE / 2;
constexpr unsigned JOB_TIMEOUT_MS = 120000;
constexpr int CRATE_STOCK = 2000;          // eine Kiste = 2 volle Ladungen (Promille)
constexpr int DEPOT_CAP = 30;              // Kisten im Depot
constexpr int BOX_CAP = 4;                 // Kisten in einer gebauten Munitionskiste
constexpr int PILE_CAP = 4;                // Kisten im Haufen an der Fabrik
constexpr int DEPOT_START = 4, BOX_START = 1;
constexpr unsigned PRODUCE_MS = 45000;     // Fabrik: eine Kiste alle 45 s

enum Job { J_NONE = 0, J_LOAD = 1, J_DROP = 2, J_STORE = 3 };

struct Truck
{
	uint8_t player = 0;
	int crates = 0;
	int job = J_NONE;
	bool autopilot = false;   // vom Autopiloten (Fabrik -> Depot) vergeben
	int tx = 0, ty = 0;       // Ziel
	uint32_t source = 0;      // Ziel-Bauwerk (Quelle / Depot)
	int mx = -1, my = -1;     // eigener Fahrbefehl (um Spieler-Befehle zu erkennen)
	unsigned since = 0;
	int tries = 0;
};
struct Boom
{
	unsigned due;
	int x, y, z, n;
};

std::map<uint32_t, Truck> s_trucks;
std::map<uint32_t, int> s_stock;        // Depot/Kiste/Haufen -> Vorrat (Promille-Ladungen)
std::map<uint32_t, uint32_t> s_facPile; // Fabrik -> ihr Ausgabe-Haufen
std::map<uint32_t, uint32_t> s_idleSince;   // (18.11. 11:54) Lkw -> Spielzeit, seit der er steht und keinen Befehl hat
std::map<uint32_t, unsigned> s_facNext; // Fabrik -> naechste fertige Kiste
std::vector<Boom> s_booms;
unsigned s_nextTick = 0;
unsigned s_nextAuto = 0;

bool idHas(const STRUCTURE *s, const char *what)
{
	return s != nullptr && s->pStructureType != nullptr && s->pStructureType->id.toUtf8().find(what) != std::string::npos;
}
bool alive(const STRUCTURE *s) { return s != nullptr && !s->died && s->status == SS_BUILT; }
bool isPile(const STRUCTURE *s) { return idHas(s, "AmmoBoxDrop"); }
bool isDepot(const STRUCTURE *s) { return idHas(s, "AmmoDepot"); }
bool isBox(const STRUCTURE *s) { return idHas(s, "AmmoBox") && !isPile(s); }
bool isFactory(const STRUCTURE *s)
{
	return s != nullptr && s->pStructureType != nullptr && !s->pStructureType->mobileCore && (s->pStructureType->type == REF_FACTORY || s->pStructureType->type == REF_CYBORG_FACTORY);
}
int capOf(const STRUCTURE *s) { return isDepot(s) ? DEPOT_CAP : (isBox(s) ? BOX_CAP : PILE_CAP); }

int cratesIn(int stock) { return std::max(0, (stock + CRATE_STOCK - 1) / CRATE_STOCK); }

int64_t dist2(int ax, int ay, int bx, int by)
{
	const int64_t dx = ax - bx, dy = ay - by;
	return dx * dx + dy * dy;
}

/// Vorrat einer begrenzten Quelle (wird beim ersten Mal angelegt)
int &stockRef(const STRUCTURE *s)
{
	auto it = s_stock.find(s->id);
	if (it == s_stock.end())
	{
		const int start = isDepot(s) ? DEPOT_START : (isBox(s) ? BOX_START : 1);
		it = s_stock.emplace(s->id, start * CRATE_STOCK).first;
	}
	return it->second;
}

STRUCTURE *findStruct(uint32_t id, unsigned player)
{
	if (id == 0 || player >= MAX_PLAYERS) { return nullptr; }
	STRUCTURE *s = IdToStruct(id, player);
	return alive(s) ? s : nullptr;
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

/// Anfahrpunkt neben einem Bauwerk (auf der Seite des Lkw)
void approach(DROID *d, Truck &t, const STRUCTURE *s)
{
	const int dx = d->pos.x - s->pos.x, dy = d->pos.y - s->pos.y;
	const int len = std::max(1, iHypot(Vector2i(dx, dy)));
	const int r = 3 * DIST_TILE / 2 + TILE_UNITS * std::max(s->pStructureType->baseWidth, s->pStructureType->baseBreadth) / 2;
	moveTo(d, t, s->pos.x + dx * r / len, s->pos.y + dy * r / len);
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

STRUCTURE *buildPile(unsigned player, int tx, int ty, int stock, uint16_t dir)
{
	STRUCTURE_STATS *st = pileStats(cratesIn(stock));
	if (st == nullptr) { return nullptr; }
	STRUCTURE *s = buildStructureDir(gameWorld, st, static_cast<UDWORD>(world_coord(tx) + TILE_UNITS / 2), static_cast<UDWORD>(world_coord(ty) + TILE_UNITS / 2),
	                                 dir, player, false, generateSynchronisedObjectId(), false, true, true);
	if (s == nullptr) { return nullptr; }
	s->status = SS_BUILT;
	buildingComplete(s, gameWorld);
	s_stock[s->id] = stock;
	return s;
}

/// Haufen mit neuer Kistenzahl neu aufstellen (Modell 1-4 Kisten); gibt den (neuen) Haufen zurueck, nullptr wenn leer/entfernt
STRUCTURE *refreshPile(STRUCTURE *s)
{
	if (s == nullptr) { return nullptr; }
	const int stock = stockRef(s);
	const int n = cratesIn(stock);
	char want[40];
	snprintf(want, sizeof(want), "A0BaBaAmmoBoxDrop%d", std::clamp(n, 1, 4));
	if (n > 0 && s->pStructureType->id.toUtf8() == want) { return s; }
	const unsigned player = s->player;
	const int tx = map_coord(s->pos.x), ty = map_coord(s->pos.y);
	const uint16_t dir = s->rot.direction;
	const uint32_t oldId = s->id;
	s_stock.erase(oldId);
	removeStruct(s, true, gameWorld);
	if (n <= 0) { return nullptr; }
	STRUCTURE *ns = buildPile(player, tx, ty, stock, dir);
	if (ns != nullptr)
	{
		for (auto &kv : s_facPile) { if (kv.second == oldId) { kv.second = ns->id; } }
		for (auto &kv : s_trucks) { if (kv.second.source == oldId) { kv.second.source = ns->id; } }
	}
	return ns;
}

/// freies Feld neben einem Bauwerk (feste Reihenfolge)
bool freeTileNear(const STRUCTURE *s, int extra, int &ox, int &oy)
{
	const int w = std::max(1, static_cast<int>(s->pStructureType->baseWidth)), b = std::max(1, static_cast<int>(s->pStructureType->baseBreadth));
	const int x0 = map_coord(s->pos.x) - w / 2, y0 = map_coord(s->pos.y) - b / 2;
	for (int r = 1; r <= 1 + extra; ++r)
	{
		for (int y = y0 - r; y < y0 + b + r; ++y)
		{
			for (int x = x0 - r; x < x0 + w + r; ++x)
			{
				const bool ring = (x == x0 - r || x == x0 + w + r - 1 || y == y0 - r || y == y0 + b + r - 1);
				if (!ring || !tileFree(x, y)) { continue; }
				ox = x; oy = y;
				return true;
			}
		}
	}
	return false;
}

bool dropPile(DROID *d, Truck &t)
{
	const int cx = map_coord(t.tx), cy = map_coord(t.ty);
	for (int r = 0; r <= 2; ++r)
	{
		for (int dy = -r; dy <= r; ++dy)
		{
			for (int dx = -r; dx <= r; ++dx)
			{
				if (std::max(std::abs(dx), std::abs(dy)) != r) { continue; }
				const int tx = cx + dx, ty = cy + dy;
				if (!tileFree(tx, ty)) { continue; }
				if (tx == map_coord(d->pos.x) && ty == map_coord(d->pos.y)) { continue; }
				if (buildPile(d->player, tx, ty, t.crates * CRATE_STOCK, static_cast<uint16_t>((d->id * 9973u) & 0xc000u)) == nullptr) { continue; }
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
	static unsigned tried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
	if (modelCacheStale(tried)) { crate = modelGet("ammocrate.pie"); }
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
			// MOD (Abstuerze 27.11. 03:19 und 16:41 "als gerade ein Ammo-Depot in die Luft ging"): hier flog ein Truemmerteil OHNE Modell
			// los (Effekt ohne Modell -> Absturz beim Zeichnen). Jetzt wie im Originalspiel ein zufaelliges Truemmerteil mit Rauchspur.
			const iIMDBaseShape *debris = getRandomDebrisImd();
			if (debris != nullptr)
			{
				addEffect(&p, EFFECT_GRAVITON, GRAVITON_TYPE_EMITTING_DR, true, debris->displayModel(), 0, graphicsTime, nullptr, &vel);
			}
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
	modshockwave::bigBoomSound(x, y, m, true);   // MOD (27.11. 16:43): gewaltiger, geschichteter Klang statt eines einzelnen Knalls
	if (n >= 3) { modshockwave::spawn(x, y, z, std::min(10, 2 + n / 3)); }   // MOD (22.11. 21:19): Druckwelle bei grossen Munitionsexplosionen
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

/// Laden an der Quelle s; gibt true zurueck, wenn etwas geladen wurde
bool loadFrom(DROID *d, Truck &t, STRUCTURE *s)
{
	const int want = capacity(d) - t.crates;
	if (want <= 0) { return false; }
	if (!modcrates::finite(s))
	{
		return false;   // Fabrik, HQ, Werkstatt: dort wird nichts gelagert (Fabrik-Kisten liegen im Haufen daneben)
	}
	int &st = stockRef(s);
	const int take = std::min(want, st / CRATE_STOCK);   // nur ganze Kisten
	if (take <= 0) { return false; }
	t.crates += take;
	st -= take * CRATE_STOCK;
	if (isPile(s)) { refreshPile(s); }
	return true;
}

/// Einlagern in Depot/Kiste/Haufen s
bool storeInto(Truck &t, STRUCTURE *s)
{
	if (!modcrates::finite(s)) { return false; }
	int &st = stockRef(s);
	const int space = capOf(s) - cratesIn(st);
	const int put = std::min(space, t.crates);
	if (put <= 0) { return false; }
	st += put * CRATE_STOCK;
	t.crates -= put;
	if (isPile(s)) { refreshPile(s); }
	return true;
}

/// Ausgabe-Haufen einer Fabrik (Fabrik-Kisten); nullptr wenn keiner
STRUCTURE *facPile(const STRUCTURE *fac)
{
	auto it = s_facPile.find(fac->id);
	if (it == s_facPile.end()) { return nullptr; }
	STRUCTURE *p = findStruct(it->second, fac->player);
	if (p == nullptr) { s_facPile.erase(it); }
	return p;
}

void produce()
{
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		if (!isHumanPlayer(p)) { continue; }   // Munition gilt nur fuer menschliche Spieler (modammo)
		std::vector<STRUCTURE *> facs;
		for (STRUCTURE *s : gameWorld.objects.structures[p]) { if (alive(s) && isFactory(s)) { facs.push_back(s); } }
		std::sort(facs.begin(), facs.end(), [](const STRUCTURE *a, const STRUCTURE *b) { return a->id < b->id; });
		for (STRUCTURE *f : facs)
		{
			auto nx = s_facNext.find(f->id);
			if (nx == s_facNext.end()) { s_facNext[f->id] = gameTime + PRODUCE_MS; continue; }
			if (gameTime < nx->second) { continue; }
			STRUCTURE *pile = facPile(f);
			if (pile != nullptr && cratesIn(stockRef(pile)) >= PILE_CAP) { continue; }   // voll: Fabrik wartet
			nx->second = gameTime + PRODUCE_MS;
			if (pile != nullptr)
			{
				stockRef(pile) += CRATE_STOCK;
				pile = refreshPile(pile);
				if (pile != nullptr) { s_facPile[f->id] = pile->id; }
			}
			else
			{
				int tx = 0, ty = 0;
				if (!freeTileNear(f, 1, tx, ty)) { continue; }
				STRUCTURE *np = buildPile(p, tx, ty, CRATE_STOCK, 0);
				if (np != nullptr) { s_facPile[f->id] = np->id; }
			}
		}
	}
}

/// Autopilot: freie Lkw bringen Fabrik-Kisten zum naechsten Depot mit Platz
void autopilot()
{
	// (Wunsch 18.11. 11:54) "nur wenn sie stehen und keinen Befehl mehr haben": Wartezeit zaehlt erst ab dem Stillstand ohne Befehl
	// (ein Spieler-Befehl - hinfahren, abladen - wird immer erst ganz ausgefuehrt)
	{
		std::map<uint32_t, uint32_t> idle;
		for (unsigned p = 0; p < MAX_PLAYERS; ++p)
		{
			for (DROID *d : gameWorld.objects.droids[p])
			{
				if (d == nullptr || d->died || capacity(d) <= 0 || !idleOrder(d) || d->sMove.Status != MOVEINACTIVE) { continue; }
				const auto it = s_idleSince.find(d->id);
				idle[d->id] = (it != s_idleSince.end()) ? it->second : gameTime;
			}
		}
		s_idleSince.swap(idle);
	}
	auto idleMs = [](const DROID *d) -> uint32_t {
		const auto it = s_idleSince.find(d->id);
		return it == s_idleSince.end() ? 0u : gameTime - it->second;
	};
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		if (!isHumanPlayer(p)) { continue; }
		std::vector<STRUCTURE *> piles, depots;
		std::set<uint32_t> reserved;
		for (const auto &kv : s_trucks) { if (kv.second.player == p && kv.second.job == J_LOAD) { reserved.insert(kv.second.source); } }
		for (const auto &kv : s_facPile)
		{
			STRUCTURE *ps = findStruct(kv.second, p);
			if (ps != nullptr && cratesIn(stockRef(ps)) > 0 && reserved.count(ps->id) == 0) { piles.push_back(ps); }
		}
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (alive(s) && isDepot(s) && cratesIn(stockRef(s)) < DEPOT_CAP) { depots.push_back(s); }
		}
		if (depots.empty()) { continue; }
		std::vector<DROID *> trucks;
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d == nullptr || d->died || capacity(d) <= 0 || !idleOrder(d)) { continue; }
			// (Wunsch 18.11. 02:44) "alle Fahrzeuge wie Lkw und Oeltanker nehmen selbstaendig ihre Arbeit auf, immer wieder, falls sie
			// abbeordert werden, aber mit kurzer Wartezeit": nach einem Spieler-Befehl nur 6 s Pause, sobald der Lkw frei ist
			if (modoil::isBusy(d->id) || modcontrol::sincePlayerOrder(d) < 6000u || idleMs(d) < 6000u) { continue; }
			if (modapc::isApc(d) && modapc::load(d) > 0) { continue; }   // (24.11.) mit Mannschaft an Bord: kein Kisten-Autopilot
			auto it = s_trucks.find(d->id);
			if (it != s_trucks.end() && it->second.job != J_NONE) { continue; }
			trucks.push_back(d);
		}
		std::sort(trucks.begin(), trucks.end(), [](const DROID *a, const DROID *b) { return a->id < b->id; });
		for (DROID *d : trucks)
		{
			Truck &t = s_trucks[d->id];
			t.player = static_cast<uint8_t>(p);
			if (t.crates > 0)
			{
				// geladen (vom Autopiloten): zum naechsten Depot mit Platz
				if (!t.autopilot)
				{
					// vom Spieler beladen: der Spieler entscheidet - steht der Lkw aber 20 s untaetig, bringt er die Kisten selbst ins Depot
					if (modcontrol::sincePlayerOrder(d) < 20000u || idleMs(d) < 20000u) { continue; }
					t.autopilot = true;
				}
				STRUCTURE *best = nullptr; int64_t bd = 0;
				for (STRUCTURE *s : depots) { const int64_t dd = dist2(s->pos.x, s->pos.y, d->pos.x, d->pos.y); if (!best || dd < bd) { best = s; bd = dd; } }
				if (best == nullptr) { continue; }
				t.job = J_STORE; t.source = best->id; t.tx = best->pos.x; t.ty = best->pos.y; t.since = gameTime; t.tries = 0; t.mx = -1; t.my = -1;
				continue;
			}
			if (piles.empty()) { continue; }
			size_t bi = 0; int64_t bd = -1;
			for (size_t i = 0; i < piles.size(); ++i)
			{
				const int64_t dd = dist2(piles[i]->pos.x, piles[i]->pos.y, d->pos.x, d->pos.y);
				if (bd < 0 || dd < bd) { bd = dd; bi = i; }
			}
			STRUCTURE *ps = piles[bi];
			piles.erase(piles.begin() + static_cast<long>(bi));
			t.job = J_LOAD; t.autopilot = true; t.source = ps->id; t.tx = ps->pos.x; t.ty = ps->pos.y; t.since = gameTime; t.tries = 0; t.mx = -1; t.my = -1;
		}
	}
}
} // namespace

int capacity(const DROID *d)
{
	if (d == nullptr || d->isVtol() || d->isTransporter()) { return 0; }
	// (Wunsch 24.11. 17:46 "Munitionskisten ueber den APC (Mannschaftstransporter, der auch Einheiten laden kann)"): nur noch die
	// Schuetzenpanzer fahren Kisten (6 Plaetze); Ladeflaechen-Lkw (Pickup = Wrack-Bergung) und Radlader nicht mehr
	if (modapc::isApc(d)) { return 6; }
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
	return it != s_trucks.end() && (it->second.crates > 0 || it->second.job != J_NONE);
}

bool startOrder(DROID *d, int x, int y, uint16_t code)
{
	if (code != CODE_LOAD && code != CODE_UNLOAD) { return false; }
	if (d == nullptr || capacity(d) <= 0) { return true; }   // falscher Typ: Befehl verwerfen
	Truck &t = s_trucks[d->id];
	t.player = static_cast<uint8_t>(d->player);
	t.autopilot = false;
	t.tx = x; t.ty = y;
	t.mx = -1; t.my = -1;
	t.since = gameTime;
	t.tries = 0;
	t.source = 0;
	if (code == CODE_UNLOAD)
	{
		if (t.crates <= 0) { t.job = J_NONE; return true; }
		t.job = J_DROP;
	}
	else
	{
		// angeklicktes Bauwerk an dieser Stelle; mit Kisten an Bord und Platz dort: einlagern, sonst laden
		STRUCTURE *src = nullptr;
		if (d->player < MAX_PLAYERS)
		{
			for (STRUCTURE *s : gameWorld.objects.structures[d->player])
			{
				if (!alive(s) || !(modammo::isSource(s) || isFactory(s))) { continue; }
				if (s->pos.x == x && s->pos.y == y) { src = s; break; }
			}
		}
		if (src == nullptr) { t.job = J_NONE; return true; }
		t.source = src->id;
		const bool canStore = finite(src) && cratesIn(stockRef(src)) < capOf(src);
		if (isFactory(src))
		{
			STRUCTURE *fp = facPile(src);   // Fabrik angeklickt: deren Kisten-Haufen
			if (fp == nullptr) { t.job = J_NONE; return true; }
			t.source = fp->id; t.tx = fp->pos.x; t.ty = fp->pos.y;
			t.job = J_LOAD;
		}
		else
		{
			t.job = (t.crates > 0 && canStore) ? J_STORE : J_LOAD;
		}
	}
	d->order = DroidOrder(DORDER_NONE);
	actionDroid(d, DACTION_NONE);
	return true;
}

bool loadOrderSelected(unsigned player, const STRUCTURE *s)
{
	if (player >= MAX_PLAYERS || s == nullptr || s->player != player || !alive(s) || !modammo::isSource(s)) { return false; }
	bool any = false;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (d == nullptr || !d->selected || d->died || capacity(d) <= 0) { continue; }
		DroidOrder o(DORDER_FORTIFY, s->pos.xy());
		o.direction = CODE_LOAD;
		sendDroidInfo(d, o, false, currentOrderSource());
		any = true;
	}
	return any;
}

bool wouldLoadSelected(unsigned player, const STRUCTURE *s)
{
	if (player >= MAX_PLAYERS || s == nullptr || s->player != player || !alive(s) || !modammo::isSource(s)) { return false; }
	for (const DROID *d : gameWorld.objects.droids[player])
	{
		if (d != nullptr && d->selected && !d->died && capacity(d) > 0) { return true; }
	}
	return false;
}

bool isCrateTarget(const DROID *d, const STRUCTURE *s)
{
	if (d == nullptr || s == nullptr || capacity(d) <= 0 || s->player != d->player || !alive(s)) { return false; }
	// beschaedigte Gebaeude: der normale Klick des Lkw repariert wie bisher (Laden dann mit Strg + Klick)
	if ((d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT) && s->body < s->structureBody()) { return false; }
	if (isFactory(s)) { return facPile(s) != nullptr && crates(d) < capacity(d); }
	if (isPile(s)) { return crates(d) < capacity(d) && stockRef(s) > 0; }
	if (isDepot(s) || isBox(s))
	{
		const bool canStore = crates(d) > 0 && cratesIn(stockRef(s)) < capOf(s);
		const bool canLoad = crates(d) < capacity(d) && stockRef(s) > 0;
		return canStore || canLoad;
	}
	return false;
}

bool wantsUnload(const DROID *d)
{
	return crates(d) > 0;
}

bool finite(const STRUCTURE *s)
{
	return isDepot(s) || isBox(s) || isPile(s);
}

bool hasStock(const STRUCTURE *s)
{
	if (!finite(s)) { return true; }
	return stockRef(s) > 0;
}

void consume(const STRUCTURE *s, int permille)
{
	if (s == nullptr || permille <= 0 || !finite(s)) { return; }
	stockRef(s) -= permille;
}

int crateCapAt(const STRUCTURE *s)
{
	if (s == nullptr || !finite(s)) { return -1; }
	return capOf(s);
}

int cratesAt(const STRUCTURE *s)
{
	if (!finite(s)) { return -1; }
	return cratesIn(stockRef(s));
}

void onStructureDestroyed(const STRUCTURE *s)
{
	if (s == nullptr) { return; }
	s_facNext.erase(s->id);
	auto it = s_stock.find(s->id);
	int n = 0;
	if (it != s_stock.end()) { n = cratesIn(it->second); s_stock.erase(it); }
	else if (isPile(s)) { n = 2; }
	else if (isDepot(s)) { n = DEPOT_START; }   // nie benutztes Depot: Anfangsvorrat
	// (Wunsch 22.11. 21:19 "grosse Ammodepot-Explosionen"): ein fertiges Munitionsdepot geht mit seinem Vorrat hoch wie ein Kistenhaufen
	if ((isPile(s) || (isDepot(s) && s->status == SS_BUILT)) && n > 0)
	{
		s_booms.push_back({ gameTime + 60 + static_cast<unsigned>(s->id % 4u) * 40u, s->pos.x, s->pos.y, s->pos.z, n });
	}
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
	if (!s_booms.empty())
	{
		std::vector<Boom> due, keep;
		for (const Boom &b : s_booms) { (b.due <= gameTime ? due : keep).push_back(b); }
		s_booms.swap(keep);
		for (const Boom &b : due)
		{
			effects(b.x, b.y, b.z, b.n);
			const int m = std::clamp(b.n, 1, 6);
			areaDamage(b.x, b.y, 5 * DIST_TILE / 2 + m * DIST_TILE / 2, 160 * m);
		}
	}

	if (gameTime < s_nextTick) { return; }
	s_nextTick = gameTime + TICK_MS;

	if (gameTime >= s_nextAuto)
	{
		s_nextAuto = gameTime + 1000;
		produce();
		autopilot();
	}

	for (auto it = s_trucks.begin(); it != s_trucks.end();)
	{
		Truck &t = it->second;
		DROID *d = IdToDroid(gameWorld.objects, it->first, t.player);
		if (d == nullptr || d->died) { it = s_trucks.erase(it); continue; }
		if (t.job != J_NONE)
		{
			const bool mine = ownMove(d, t);
			if (!mine && !idleOrder(d) && t.mx >= 0)
			{
				t.job = J_NONE; t.autopilot = false;   // der Spieler hat einen anderen Befehl gegeben
			}
			else if (gameTime - t.since > JOB_TIMEOUT_MS)
			{
				t.job = J_NONE;
			}
			else if (t.job == J_LOAD || t.job == J_STORE)
			{
				STRUCTURE *s = findStruct(t.source, t.player);
				if (s == nullptr) { t.job = J_NONE; }
				else if (dist2(d->pos.x, d->pos.y, s->pos.x, s->pos.y) <= static_cast<int64_t>(LOAD_RANGE + DIST_TILE) * (LOAD_RANGE + DIST_TILE))
				{
					if (t.job == J_LOAD) { loadFrom(d, t, s); } else { storeInto(t, s); }
					t.job = J_NONE;
					if (d->order.type == DORDER_MOVE) { orderDroid(d, DORDER_STOP, ModeImmediate); }
					if (t.crates <= 0) { t.autopilot = false; }
				}
				else if (idleOrder(d) && t.tries < 4)
				{
					approach(d, t, s);
					t.tries++;
				}
				else if (idleOrder(d))
				{
					t.job = J_NONE;
				}
			}
			else if (t.job == J_DROP)
			{
				if (t.crates <= 0) { t.job = J_NONE; }
				else if (dist2(d->pos.x, d->pos.y, t.tx, t.ty) <= static_cast<int64_t>(DROP_RANGE) * DROP_RANGE
				         || (idleOrder(d) && t.tries >= 4 && dist2(d->pos.x, d->pos.y, t.tx, t.ty) <= static_cast<int64_t>(3 * DIST_TILE) * (3 * DIST_TILE)))
				{
					if (d->order.type == DORDER_MOVE) { orderDroid(d, DORDER_STOP, ModeImmediate); }
					dropPile(d, t);
					t.job = J_NONE;
					t.autopilot = false;
				}
				else if (idleOrder(d) && t.tries < 4)
				{
					moveTo(d, t, t.tx, t.ty);
					t.tries++;
				}
				else if (idleOrder(d))
				{
					t.job = J_NONE;
				}
			}
		}
		if (t.job == J_NONE && t.crates <= 0) { it = s_trucks.erase(it); continue; }
		++it;
	}

	// leere Haufen verschwinden (ohne Explosion); Haufen-Modell an die Kistenzahl anpassen
	std::vector<std::pair<uint32_t, unsigned>> check;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (alive(s) && isPile(s) && s_stock.count(s->id)) { check.emplace_back(s->id, p); }
		}
	}
	std::sort(check.begin(), check.end());
	for (const auto &c : check)
	{
		STRUCTURE *s = findStruct(c.first, c.second);
		if (s != nullptr) { refreshPile(s); }
	}
}

void reset()
{
	s_trucks.clear();
	s_stock.clear();
	s_facPile.clear();
	s_idleSince.clear();
	s_facNext.clear();
	s_booms.clear();
	s_nextTick = 0;
	s_nextAuto = 0;
}
} // namespace modcrates
