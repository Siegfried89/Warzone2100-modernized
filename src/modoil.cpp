// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Oelkreislauf, siehe modoil.h

#include "modoil.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/netplay/sync_debug.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

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
#include "power.h"
#include "range_rings.h"
#include "structure.h"
#include "template.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace modoil
{

namespace
{

constexpr int BARREL_ENERGY = 100;          // Energie je Fass, direkt ins Kraftwerk
constexpr int STORED_ENERGY = 110;          // Energie je Fass nach dem Lager (+10 %)
constexpr int DERRICK_CAP = 5;              // Fass-Plaetze am Oelturm
constexpr int GRID_SECS = 180;              // ein Fass haelt das Kraftwerk / Stromnetz 3 Minuten am Laufen
constexpr int START_FUEL = 120;             // neues Kraftwerk startet mit 2 Minuten
constexpr int PULL_BELOW = 60;              // das Kraftwerk holt ein Fass aus dem Lager, wenn der Vorrat darunter faellt
constexpr int STORAGE_CAP = 8;              // Fass-Plaetze im Wasserturm
constexpr int REF_CAP = 10;                 // Fass-Plaetze in der Raffinerie (je Eingang und Ausgang)
constexpr int REF_SECS = 20;                // Sekunden je veredeltes Fass
constexpr int REFINED_ENERGY = 150;         // Treibstoff-Fass direkt ins Kraftwerk
constexpr int REFINED_STORED = 165;         // Treibstoff-Fass nach dem Lager (+10 %)
constexpr int REFINED_GRID_SECS = 270;      // Treibstoff-Fass haelt das Netz 4,5 Minuten
constexpr int64_t STORAGE_RANGE = 12 * 128; // Lager und Kraftwerk gehoeren zusammen bis zu dieser Entfernung
constexpr int64_t LOAD_RANGE = 320;         // Lkw laedt / entlaedt in dieser Naehe

struct CargoState
{
	int player = 0;
	int cap = 0;
	int n = 0;
	int mode = 0;               // 0 frei, 1 unterwegs zum Oelturm, 2 unterwegs zum Abladen
	uint32_t target = 0;
	int kind = 0;               // Ziel: 0 Kraftwerk, 1 Wasserturm, 2 Raffinerie, 3 Oelturm
	bool tank = false;          // Oeltransporter (holt veredelte Faesser an der Raffinerie)
	int tx = 0, ty = 0;
	int stuck = 0;
	int pause = 0;
	int x = 0, y = 0, z = 0;    // letzte bekannte Position
};

struct Boom
{
	UDWORD due;
	int x, y, z;
	int n;
	bool damage;
};

struct Replace
{
	int x, y;
	uint16_t dir;
	int player;
	bool broken;                // true = Bruchstueck bauen, false = heile Pipeline bauen
};

std::map<uint32_t, int> s_barrels;          // Oelturm -> Faesser
std::map<uint32_t, int64_t> s_progress;     // Oelturm -> Fortschritt zum naechsten Fass (Festkomma)
std::map<uint32_t, int> s_fuel;             // Kraftwerk -> Sekunden
std::map<uint32_t, int> s_store;            // Wasserturm (Feature) -> rohe Faesser
std::map<uint32_t, int> s_storeR;           // Wasserturm (Feature) -> veredelte Faesser
std::map<uint32_t, int> s_refIn, s_refOut, s_refT;   // Raffinerie: Eingang, Ausgang, Sekunden im laufenden Fass
std::map<uint32_t, CargoState> s_cargo;     // Lkw -> Zustand
std::map<std::pair<int, int>, int> s_lowered;   // tiefe Graeben: abgesenkter Eckpunkt -> urspruengliche Hoehe
std::vector<Boom> s_booms;
std::vector<Replace> s_repl;
bool s_have = false;
UDWORD s_lastSec = 0;

bool idIs(const WzString &id, const char *s) { return id.toUtf8() == s; }
bool idStarts(const WzString &id, const char *s) { return id.toUtf8().rfind(s, 0) == 0; }

bool isPipe(const STRUCTURE_STATS *st)
{
	return st != nullptr && st->type == REF_WALL && idStarts(st->id, "A0BaBaPipeline");
}

bool isRefinery(const STRUCTURE_STATS *st)
{
	return st != nullptr && st->type == REF_GENERIC && idIs(st->id, "A0BaBaRefinery");
}

bool isBroken(const STRUCTURE_STATS *st)
{
	return isPipe(st) && idIs(st->id, "A0BaBaPipelineBroken");
}

bool modPresent()
{
	static bool have = false;
	static UDWORD cachedNum = 0;
	static bool cached = false;
	if (!have || cachedNum != numStructureStats)
	{
		have = true;
		cachedNum = numStructureStats;
		cached = getStructStatFromName(WzString::fromUtf8("A0BaBaPipeline")) >= 0;
	}
	return cached;
}

int cargoCapacity(const DROID *d)
{
	const CONSTRUCT_STATS *cs = d->getConstructStats();
	if (cs == nullptr) { return 0; }
	if (idIs(cs->id, "ScavBed1")) { return 2; }
	if (idIs(cs->id, "ScavBed3")) { return 3; }
	if (idIs(cs->id, "ScavBed2")) { return 4; }
	if (idIs(cs->id, "ScavBucket")) { return 6; }
	if (idIs(cs->id, "ScavTank") || idIs(cs->id, "ScavTankB")) { return 10; }
	return 0;
}

bool isTank(const DROID *d)
{
	const CONSTRUCT_STATS *cs = d->getConstructStats();
	return cs != nullptr && (idIs(cs->id, "ScavTank") || idIs(cs->id, "ScavTankB"));
}

int64_t d2(int ax, int ay, int bx, int by)
{
	const int64_t dx = ax - bx, dy = ay - by;
	return dx * dx + dy * dy;
}

int ufind(std::vector<int> &par, int x)
{
	while (par[x] != x)
	{
		par[x] = par[par[x]];
		x = par[x];
	}
	return x;
}

void uunite(std::vector<int> &par, int a, int b)
{
	a = ufind(par, a);
	b = ufind(par, b);
	if (a != b) { par[b] = a; }
}

bool tileSeen(int x, int y)
{
	MAPTILE *t = mapTile(gameWorld.map, map_coord(x), map_coord(y));
	return t != nullptr && TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(t);
}

void queueBoom(int x, int y, int z, int n, bool damage, unsigned delay)
{
	s_booms.push_back({gameTime + delay, x, y, z, n, damage});
}

// ---------------------------------------------------------------- Explosion

void boomEffects(int x, int y, int z, int n)
{
	if (!tileSeen(x, y)) { return; }
	const unsigned it = gameTime - deltaGameTime / 2 + 1;
	const int m = std::min(n, 10);
	const int count = 3 + m;
	const int scatter = 64 + 12 * m;
	for (int i = 0; i < count; i++)
	{
		const int ox = ((i * 53) % (2 * scatter)) - scatter;
		const int oy = ((i * 97) % (2 * scatter)) - scatter;
		Vector3i pos(x + ox, z + 24 + (i * 37) % 80, y + oy);
		addEffect(&pos, EFFECT_EXPLOSION, (i % 3 == 0) ? EXPLOSION_TYPE_LARGE : EXPLOSION_TYPE_MEDIUM, false, nullptr, 0, it);
	}
	for (int i = 0; i < 2; i++)
	{
		Vector3i pos(x + (i ? 40 : -40), z + 20, y + (i ? -30 : 30));
		effectGiveAuxVar(60 + 8 * m);
		effectGiveAuxVarSec(12000 + 1500 * m);
		addEffect(&pos, EFFECT_FIRE, FIRE_TYPE_SMOKY, false, nullptr, 0, it);
		Vector3i sp(x, z + 60, y);
		addEffect(&sp, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH, false, nullptr, 0, it);
	}
	shakeStart(200 + 40 * static_cast<unsigned>(m));
	audio_PlayStaticTrack(x, y, ID_SOUND_EXPLOSION);
}

void areaDamage(int x, int y, int radius, int base)
{
	const unsigned it = gameTime - deltaGameTime / 2 + 1;
	for (BASE_OBJECT *o : gridStartIterate(x, y, static_cast<uint32_t>(radius)))
	{
		if (o == nullptr || o->died) { continue; }
		const int dist = iHypot(Vector2i(o->pos.x - x, o->pos.y - y));
		if (dist > radius) { continue; }
		const int pct = 100 - 65 * dist / std::max(1, radius);
		const unsigned dmg = static_cast<unsigned>(std::max(1, base * pct / 100));
		const unsigned heat = dmg * 7 / 10;
		const unsigned kin = dmg - heat;
		switch (o->type)
		{
		case OBJ_DROID:
		{
			DROID *d = static_cast<DROID *>(o);
			if (d->isFlying()) { break; }
			droidDamage(gameWorld, d, nullptr, heat, WC_HEAT, WSC_FLAME, it, false, 0, false);
			if (!d->died) { droidDamage(gameWorld, d, nullptr, kin, WC_KINETIC, WSC_BOMB, it, false, 0, false); }
			break;
		}
		case OBJ_STRUCTURE:
		{
			STRUCTURE *s = static_cast<STRUCTURE *>(o);
			structureDamage(gameWorld, s, nullptr, heat, WC_HEAT, WSC_FLAME, it, false, 0, false);
			if (!s->died) { structureDamage(gameWorld, s, nullptr, kin, WC_KINETIC, WSC_BOMB, it, false, 0, false); }
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

void explode(const Boom &b)
{
	boomEffects(b.x, b.y, b.z, b.n);
	if (b.damage)
	{
		const int m = std::min(b.n, 10);
		areaDamage(b.x, b.y, 160 + 40 * m, 120 * std::max(1, m));
	}
}

void processBooms()
{
	if (s_booms.empty()) { return; }
	std::vector<Boom> due, keep;
	for (const Boom &b : s_booms)
	{
		(b.due <= gameTime ? due : keep).push_back(b);
	}
	s_booms.swap(keep);
	for (const Boom &b : due)
	{
		explode(b);   // darf neue Explosionen (Kettenreaktion) einreihen
	}
}

// ---------------------------------------------------------------- Pipeline: Bruchstueck bauen / reparieren

void processReplacements()
{
	if (s_repl.empty()) { return; }
	std::vector<Replace> todo;
	todo.swap(s_repl);
	for (const Replace &r : todo)
	{
		const int idx = getStructStatFromName(WzString::fromUtf8(r.broken ? "A0BaBaPipelineBroken" : "A0BaBaPipeline"));
		if (idx < 0) { continue; }
		STRUCTURE_STATS *st = &asStructureStats[idx];
		STRUCTURE *s = buildStructureDir(gameWorld, st, r.x, r.y, r.dir, r.player, false, generateSynchronisedObjectId(), false, true, true);
		if (s == nullptr) { continue; }
		s->status = SS_BUILT;
		buildingComplete(s, gameWorld);
		if (r.broken)
		{
			s->body = std::max(1u, structureBodyBuilt(s) / 4u);   // Bruchstueck: ein Viertel, Bautrupp repariert es
		}
	}
}

} // namespace

// ---------------------------------------------------------------- oeffentliche Funktionen

bool manages(int player)
{
	if (player < 0 || player >= MAX_PLAYERS || !modPresent()) { return false; }
	// Gefecht/Mehrspieler: alle menschlichen Spieler. Kampagne (Wunsch 13.11.): der eigene Spieler
	return bMultiPlayer ? isHumanPlayer(player) : player == static_cast<int>(selectedPlayer);
}

void reset()
{
	s_barrels.clear();
	s_progress.clear();
	s_fuel.clear();
	s_store.clear();
	s_storeR.clear();
	s_refIn.clear();
	s_refOut.clear();
	s_refT.clear();
	s_cargo.clear();
	s_lowered.clear();
	s_booms.clear();
	s_repl.clear();
	s_have = false;
}

void extract(const STRUCTURE *psDerrick, int64_t amountFP, int64_t fpOne)
{
	const int64_t full = static_cast<int64_t>(BARREL_ENERGY) * fpOne;
	int64_t &p = s_progress[psDerrick->id];
	int &b = s_barrels[psDerrick->id];
	p += amountFP;
	while (p >= full)
	{
		if (b >= DERRICK_CAP)
		{
			p = full;      // Turm ist voll: es wird nichts mehr abgestellt
			break;
		}
		b++;
		p -= full;
	}
}

bool tilePassableForInfantry(const WorldMapState &mapState, int tileX, int tileY)
{
	const MAPTILE *t = mapTile(mapState, tileX, tileY);
	if (t == nullptr || t->psObject == nullptr || t->psObject->type != OBJ_STRUCTURE) { return false; }
	const STRUCTURE *s = static_cast<const STRUCTURE *>(t->psObject);
	return s->pStructureType != nullptr && s->pStructureType->infantryPassable && s->status == SS_BUILT;
}

bool trenchForInfantry(const BASE_OBJECT *obj, const DROID *dominant)
{
	if (obj == nullptr || dominant == nullptr || obj->type != OBJ_STRUCTURE || obj->player != selectedPlayer) { return false; }
	const STRUCTURE *s = static_cast<const STRUCTURE *>(obj);
	if (s->pStructureType == nullptr || !s->pStructureType->infantryPassable || s->status != SS_BUILT) { return false; }
	const PROPULSION_STATS *prop = dominant->getPropulsionStats();
	return prop != nullptr && prop->propulsionType == PROPULSION_TYPE_LEGGED;
}

bool droidInTrench(const DROID *d)
{
	if (d == nullptr || d->getPropulsionStats() == nullptr || d->getPropulsionStats()->propulsionType != PROPULSION_TYPE_LEGGED) { return false; }
	return tilePassableForInfantry(gameWorld.map, map_coord(d->pos.x), map_coord(d->pos.y));
}

bool isCargo(uint32_t droidId)
{
	return s_cargo.find(droidId) != s_cargo.end();
}

bool generatorRunning(const STRUCTURE *psGen)
{
	if (!manages(psGen->player)) { return true; }
	const auto it = s_fuel.find(psGen->id);
	return it == s_fuel.end() || it->second > 0;
}

void onStructureDestroyed(const STRUCTURE *psDel)
{
	if (psDel == nullptr) { return; }
	const STRUCTURE_STATS *st = psDel->pStructureType;
	if (st->type == REF_RESOURCE_EXTRACTOR)
	{
		const auto it = s_barrels.find(psDel->id);
		if (it != s_barrels.end())
		{
			if (it->second > 0 && psDel->status == SS_BUILT)
			{
				queueBoom(psDel->pos.x, psDel->pos.y, psDel->pos.z, it->second, true, 100);   // volle Fass-Plaetze gehen hoch
			}
			s_barrels.erase(it);
		}
		s_progress.erase(psDel->id);
	}
	else if (isRefinery(st))
	{
		const int n = s_refIn[psDel->id] + s_refOut[psDel->id];
		if (n > 0 && psDel->status == SS_BUILT) { queueBoom(psDel->pos.x, psDel->pos.y, psDel->pos.z, n, true, 100); }
		s_refIn.erase(psDel->id);
		s_refOut.erase(psDel->id);
		s_refT.erase(psDel->id);
	}
	else if (isPipe(st) && !isBroken(st) && psDel->status == SS_BUILT)
	{
		s_repl.push_back({psDel->pos.x, psDel->pos.y, psDel->rot.direction, static_cast<int>(psDel->player), true});
		queueBoom(psDel->pos.x, psDel->pos.y, psDel->pos.z, 4, false, 0);   // grosse feurige Explosion
	}
}

void onFeatureDestroyed(const FEATURE *psDel)
{
	if (psDel == nullptr || psDel->psStats == nullptr) { return; }
	int n = 0;
	if (idIs(psDel->psStats->id, "OilDrum5")) { n = 5; }
	else if (idIs(psDel->psStats->id, "OilDrum10")) { n = 10; }
	else if (idIs(psDel->psStats->id, "WaterTower"))
	{
		const auto it = s_store.find(psDel->id);
		if (it != s_store.end()) { n = it->second; s_store.erase(it); }
		const auto ir = s_storeR.find(psDel->id);
		if (ir != s_storeR.end()) { n += ir->second; s_storeR.erase(ir); }
	}
	if (n > 0)
	{
		queueBoom(psDel->pos.x, psDel->pos.y, psDel->pos.z, n, true, 100 + static_cast<unsigned>(psDel->id % 4) * 70);   // Kettenreaktion: gestaffelt
	}
}

void onDroidDestroyed(const DROID *psDel)
{
	if (psDel == nullptr) { return; }
	const auto it = s_cargo.find(psDel->id);
	if (it == s_cargo.end()) { return; }
	if (it->second.n > 0)
	{
		queueBoom(psDel->pos.x, psDel->pos.y, psDel->pos.z, it->second.n, true, 60);
	}
	s_cargo.erase(it);
}

// ---------------------------------------------------------------- Logistik (einmal je Spielsekunde)

namespace
{

void updateTerrain();

struct Snap
{
	std::vector<STRUCTURE *> derricks, gens, pipes, refs;
};

void deliverToGenerator(int player, STRUCTURE *gen, int barrels, bool refined)
{
	for (int i = 0; i < barrels; i++)
	{
		addPower(player, refined ? REFINED_ENERGY : BARREL_ENERGY);
		s_fuel[gen->id] += refined ? REFINED_GRID_SECS : GRID_SECS;
	}
}

int storeRoom(uint32_t id)
{
	return std::max(0, STORAGE_CAP - s_store[id] - s_storeR[id]);
}

// Automatische Ausbildung: fehlt der erste Fracht-Lkw (bzw. Tanker), startet eine freie Fabrik selbst einen (normale Kosten, normaler Bau)
void autoTrain(int p, const Snap &sn)
{
	// Wunsch 13.11.: nur EIN Fracht-Lkw und EIN Tanker kommen automatisch (sobald Oelturm bzw. Raffinerie stehen), alle weiteren bildet der Spieler selbst aus
	const int wantTruck = std::min(1, static_cast<int>(sn.derricks.size()));
	const int wantTank = std::min(1, static_cast<int>(sn.refs.size()));
	if (wantTruck <= 0 && wantTank <= 0) { return; }
	int haveTruck = 0, haveTank = 0;
	for (const auto &kv : s_cargo)
	{
		if (kv.second.player != p) { continue; }
		if (kv.second.tank) { haveTank++; } else { haveTruck++; }
	}
	std::vector<STRUCTURE *> idle;
	for (STRUCTURE *f : gameWorld.objects.structures[p])
	{
		if (f == nullptr || f->died || f->status != SS_BUILT || f->pStructureType == nullptr || f->pStructureType->type != REF_FACTORY || f->pFunctionality == nullptr) { continue; }
		const FACTORY &fac = f->pFunctionality->factory;
		const DROID_TEMPLATE *sub = fac.psSubject != nullptr ? fac.psSubject : fac.psSubjectPending;
		if (sub == nullptr) { idle.push_back(f); continue; }
		if (idIs(sub->id, "ScavTanker") || idIs(sub->id, "ScavTankerB")) { haveTank++; }
		else if (idIs(sub->id, "ScavPickup") || idIs(sub->id, "ScavLkw") || idIs(sub->id, "ScavTraktor") || idIs(sub->id, "ScavRadlader")) { haveTruck++; }
	}
	auto train = [&](const char *name)
	{
		if (idle.empty()) { return; }
		STRUCTURE *f = idle.back();
		for (DROID_TEMPLATE *t : fillTemplateList(f))
		{
			if (t != nullptr && idIs(t->id, name))
			{
				if (structSetManufacture(f, t, ModeImmediate)) { idle.pop_back(); }
				return;
			}
		}
	};
	if (haveTruck < wantTruck) { train("ScavLkw"); }
	if (haveTank < wantTank) { train("ScavTanker"); }
}

void logistics()
{
	std::vector<FEATURE *> towers;
	for (FEATURE *f : gameWorld.objects.features[0])
	{
		if (f != nullptr && !f->died && f->psStats != nullptr && idIs(f->psStats->id, "WaterTower")) { towers.push_back(f); }
	}
	std::map<uint32_t, FEATURE *> towerById;
	for (FEATURE *t : towers) { towerById[t->id] = t; }

	std::set<uint32_t> liveStruct, liveDroid;
	std::vector<STRUCTURE *> brokenPieces;
	const int64_t SR2 = STORAGE_RANGE * STORAGE_RANGE;
	const int64_t LR2 = LOAD_RANGE * LOAD_RANGE;

	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		Snap sn;
		std::map<uint32_t, STRUCTURE *> sById;
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s == nullptr || s->died || s->status != SS_BUILT) { continue; }
			const STRUCTURE_STATS *st = s->pStructureType;
			if (st->type == REF_RESOURCE_EXTRACTOR) { sn.derricks.push_back(s); liveStruct.insert(s->id); sById[s->id] = s; }
			else if (st->type == REF_POWER_GEN) { sn.gens.push_back(s); liveStruct.insert(s->id); sById[s->id] = s; }
			else if (isRefinery(st)) { sn.refs.push_back(s); liveStruct.insert(s->id); sById[s->id] = s; }
			else if (isPipe(st))
			{
				if (isBroken(st)) { brokenPieces.push_back(s); }
				else { sn.pipes.push_back(s); }
			}
		}
		if (!manages(p)) { continue; }

		// Kraftwerke: Brennstoff
		for (STRUCTURE *g : sn.gens)
		{
			auto it = s_fuel.find(g->id);
			if (it == s_fuel.end()) { s_fuel[g->id] = START_FUEL; }
			else if (it->second > 0) { it->second--; }
		}

		// Raffinerien: ein Fass je REF_SECS Sekunden veredeln
		for (STRUCTURE *r : sn.refs)
		{
			int &in = s_refIn[r->id];
			int &out = s_refOut[r->id];
			int &t = s_refT[r->id];
			if (in > 0 && out < REF_CAP)
			{
				if (++t >= REF_SECS) { t = 0; in--; out++; }
			}
			else { t = 0; }
		}

		// Kraftwerk holt Fass aus dem Lager (veredelte zuerst)
		for (STRUCTURE *g : sn.gens)
		{
			if (s_fuel[g->id] > PULL_BELOW) { continue; }
			FEATURE *best = nullptr;
			int64_t bd = INT64_MAX;
			for (FEATURE *t : towers)
			{
				if (s_store[t->id] + s_storeR[t->id] <= 0) { continue; }
				const int64_t dd = d2(t->pos.x, t->pos.y, g->pos.x, g->pos.y);
				if (dd > SR2) { continue; }
				if (dd < bd) { bd = dd; best = t; }
			}
			if (best != nullptr)
			{
				if (s_storeR[best->id] > 0)
				{
					s_storeR[best->id]--;
					addPower(p, REFINED_STORED);
					s_fuel[g->id] += REFINED_GRID_SECS;
				}
				else
				{
					s_store[best->id]--;
					addPower(p, STORED_ENERGY);
					s_fuel[g->id] += GRID_SECS;
				}
			}
		}

		// Pipeline-Netz: Oeltuerme -> Raffinerie (sonst Lager, sonst Kraftwerk), ein Fass je Sekunde und Turm
		if (!sn.pipes.empty())
		{
			std::map<std::pair<int, int>, int> pidx;
			std::vector<int> par;
			for (STRUCTURE *s : sn.pipes)
			{
				const std::pair<int, int> tile(map_coord(s->pos.x), map_coord(s->pos.y));
				if (pidx.find(tile) == pidx.end())
				{
					pidx[tile] = static_cast<int>(par.size());
					par.push_back(static_cast<int>(par.size()));
				}
			}
			for (const auto &kv : pidx)
			{
				const auto r = pidx.find(std::make_pair(kv.first.first + 1, kv.first.second));
				if (r != pidx.end()) { uunite(par, kv.second, r->second); }
				const auto u = pidx.find(std::make_pair(kv.first.first, kv.first.second + 1));
				if (u != pidx.end()) { uunite(par, kv.second, u->second); }
			}
			auto touch = [&](int wx, int wy)
			{
				std::set<int> roots;
				const int tx = map_coord(wx), ty = map_coord(wy);
				for (int dx = -2; dx <= 2; dx++)       // Raffinerie ist 3x3: Rohr darf bis 2 Felder neben der Mitte liegen
				{
					for (int dy = -2; dy <= 2; dy++)
					{
						const auto it = pidx.find(std::make_pair(tx + dx, ty + dy));
						if (it != pidx.end()) { roots.insert(ufind(par, it->second)); }
					}
				}
				return roots;
			};
			auto touchSmall = [&](int wx, int wy)
			{
				std::set<int> roots;
				const int tx = map_coord(wx), ty = map_coord(wy);
				for (int dx = -1; dx <= 1; dx++)
				{
					for (int dy = -1; dy <= 1; dy++)
					{
						const auto it = pidx.find(std::make_pair(tx + dx, ty + dy));
						if (it != pidx.end()) { roots.insert(ufind(par, it->second)); }
					}
				}
				return roots;
			};
			auto shares = [](const std::set<int> &a, const std::set<int> &b)
			{
				for (int v : a) { if (b.count(v) != 0) { return true; } }
				return false;
			};
			for (STRUCTURE *der : sn.derricks)
			{
				int &barrels = s_barrels[der->id];
				if (barrels <= 0) { continue; }
				const std::set<int> rd = touchSmall(der->pos.x, der->pos.y);
				if (rd.empty()) { continue; }
				STRUCTURE *br = nullptr;
				int64_t bd = INT64_MAX;
				for (STRUCTURE *r : sn.refs)
				{
					if (s_refIn[r->id] >= REF_CAP) { continue; }
					if (!shares(rd, touch(r->pos.x, r->pos.y))) { continue; }
					const int64_t dd = d2(r->pos.x, r->pos.y, der->pos.x, der->pos.y);
					if (dd < bd) { bd = dd; br = r; }
				}
				if (br != nullptr)
				{
					barrels--;
					s_refIn[br->id]++;
					continue;
				}
				FEATURE *bt = nullptr;
				bd = INT64_MAX;
				for (FEATURE *t : towers)
				{
					if (storeRoom(t->id) <= 0) { continue; }
					if (!shares(rd, touchSmall(t->pos.x, t->pos.y))) { continue; }
					const int64_t dd = d2(t->pos.x, t->pos.y, der->pos.x, der->pos.y);
					if (dd < bd) { bd = dd; bt = t; }
				}
				if (bt != nullptr)
				{
					barrels--;
					s_store[bt->id]++;
					continue;
				}
				STRUCTURE *bg = nullptr;
				bd = INT64_MAX;
				for (STRUCTURE *g : sn.gens)
				{
					if (!shares(rd, touchSmall(g->pos.x, g->pos.y))) { continue; }
					const int64_t dd = d2(g->pos.x, g->pos.y, der->pos.x, der->pos.y);
					if (dd < bd) { bd = dd; bg = g; }
				}
				if (bg != nullptr)
				{
					barrels--;
					deliverToGenerator(p, bg, 1, false);
				}
			}
		}

		// Lkw- und Tanker-Autopilot
		std::map<uint32_t, int> avail;      // abholbare Faesser: Oelturm (roh) bzw. Raffinerie (veredelt)
		for (STRUCTURE *d : sn.derricks) { avail[d->id] = s_barrels[d->id]; }
		for (STRUCTURE *r : sn.refs) { avail[r->id] = s_refOut[r->id]; }
		for (const auto &kv : s_cargo)
		{
			if (kv.second.player == p && kv.second.mode == 1 && (kv.second.kind == 3 || kv.second.kind == 2))
			{
				const auto it = avail.find(kv.second.target);
				if (it != avail.end()) { it->second -= std::max(0, kv.second.cap - kv.second.n); }
			}
		}

		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d == nullptr || d->died) { continue; }
			const int cap = cargoCapacity(d);
			if (cap <= 0) { continue; }
			CargoState &c = s_cargo[d->id];
			c.player = p;
			c.cap = cap;
			c.tank = isTank(d);
			c.x = d->pos.x; c.y = d->pos.y; c.z = d->pos.z;
			liveDroid.insert(d->id);
			if (c.pause > 0) { c.pause--; continue; }

			const bool ownMove = d->order.type == DORDER_MOVE && d->order.pos == Vector2i(c.tx, c.ty);
			if (c.mode != 0 && !ownMove && d->order.type != DORDER_NONE)
			{
				c.mode = 0;       // der Spieler hat selbst einen Befehl gegeben: Autopilot wartet, bis der Lkw wieder frei ist
				c.pause = 20;
				continue;
			}
			if (c.mode == 0 && d->order.type != DORDER_NONE) { continue; }

			// kind: 0 Kraftwerk, 1 Wasserturm (Lager), 2 Raffinerie, 3 Oelturm
			auto go = [&](int mode, int kind, uint32_t target, int wx, int wy)
			{
				orderDroidLoc(d, DORDER_MOVE, static_cast<UDWORD>(wx), static_cast<UDWORD>(wy), ModeImmediate);
				c.mode = mode; c.kind = kind; c.target = target; c.tx = wx; c.ty = wy; c.stuck = 0;
			};

			if (c.mode != 0)
			{
				bool exists = false;
				int wx = 0, wy = 0;
				STRUCTURE *ts = nullptr;
				if (c.kind == 1)
				{
					const auto it = towerById.find(c.target);
					if (it != towerById.end()) { exists = true; wx = it->second->pos.x; wy = it->second->pos.y; }
				}
				else
				{
					const auto it = sById.find(c.target);
					if (it != sById.end()) { exists = true; ts = it->second; wx = ts->pos.x; wy = ts->pos.y; }
				}
				if (!exists) { c.mode = 0; }
				else if (d2(d->pos.x, d->pos.y, wx, wy) <= LR2)
				{
					if (c.mode == 1)
					{
						int &stock = (c.kind == 3) ? s_barrels[c.target] : s_refOut[c.target];
						const int take = std::min(cap - c.n, stock);
						c.n += take;
						stock -= take;
					}
					else if (c.kind == 1)
					{
						const int m = std::min(c.n, storeRoom(c.target));
						if (c.tank) { s_storeR[c.target] += m; } else { s_store[c.target] += m; }
						c.n -= m;
					}
					else if (c.kind == 2)
					{
						const int m = std::min(c.n, std::max(0, REF_CAP - s_refIn[c.target]));
						s_refIn[c.target] += m;
						c.n -= m;
					}
					else
					{
						deliverToGenerator(p, ts, c.n, c.tank);
						c.n = 0;
					}
					c.mode = 0;
				}
				else if (d->order.type == DORDER_NONE)
				{
					if (++c.stuck >= 3) { c.mode = 0; c.pause = 10; continue; }
					go(c.mode, c.kind, c.target, wx, wy);
					continue;
				}
				else { continue; }
			}

			if (c.mode != 0) { continue; }

			// neu entscheiden: erst Fass holen (falls Platz und Vorrat), sonst abliefern
			if (cap - c.n > 0)
			{
				STRUCTURE *best = nullptr;
				int64_t bd = INT64_MAX;
				for (STRUCTURE *s : (c.tank ? sn.refs : sn.derricks))
				{
					if (avail[s->id] <= 0) { continue; }
					const int64_t dd = d2(d->pos.x, d->pos.y, s->pos.x, s->pos.y);
					if (dd < bd) { bd = dd; best = s; }
				}
				if (best != nullptr)
				{
					avail[best->id] -= (cap - c.n);
					go(1, c.tank ? 2 : 3, best->id, best->pos.x, best->pos.y);
					continue;
				}
			}
			if (c.n > 0)
			{
				if (!c.tank)
				{
					STRUCTURE *br = nullptr;
					int64_t bd = INT64_MAX;
					for (STRUCTURE *r : sn.refs)
					{
						if (s_refIn[r->id] >= REF_CAP) { continue; }
						const int64_t dd = d2(d->pos.x, d->pos.y, r->pos.x, r->pos.y);
						if (dd < bd) { bd = dd; br = r; }
					}
					if (br != nullptr) { go(2, 2, br->id, br->pos.x, br->pos.y); continue; }
				}
				FEATURE *bt = nullptr;
				int64_t bd = INT64_MAX;
				for (FEATURE *t : towers)
				{
					if (storeRoom(t->id) <= 0) { continue; }
					bool closeBy = false;
					for (STRUCTURE *g : sn.gens)
					{
						if (d2(t->pos.x, t->pos.y, g->pos.x, g->pos.y) <= SR2) { closeBy = true; break; }
					}
					if (!closeBy) { continue; }
					const int64_t dd = d2(d->pos.x, d->pos.y, t->pos.x, t->pos.y);
					if (dd < bd) { bd = dd; bt = t; }
				}
				if (bt != nullptr)
				{
					go(2, 1, bt->id, bt->pos.x, bt->pos.y);
					continue;
				}
				STRUCTURE *bg = nullptr;
				bd = INT64_MAX;
				for (STRUCTURE *g : sn.gens)
				{
					const int64_t dd = d2(d->pos.x, d->pos.y, g->pos.x, g->pos.y);
					if (dd < bd) { bd = dd; bg = g; }
				}
				if (bg != nullptr) { go(2, 0, bg->id, bg->pos.x, bg->pos.y); }
			}
		}

		autoTrain(p, sn);   // MOD: fehlende Fracht-Lkw / Tanker automatisch ausbilden
	}

	// Aufraeumen: Eintraege nicht mehr vorhandener Objekte
	for (auto it = s_fuel.begin(); it != s_fuel.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_fuel.erase(it); }
	for (auto it = s_barrels.begin(); it != s_barrels.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_barrels.erase(it); }
	for (auto it = s_progress.begin(); it != s_progress.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_progress.erase(it); }
	for (auto it = s_refIn.begin(); it != s_refIn.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_refIn.erase(it); }
	for (auto it = s_refOut.begin(); it != s_refOut.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_refOut.erase(it); }
	for (auto it = s_refT.begin(); it != s_refT.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_refT.erase(it); }
	for (auto it = s_cargo.begin(); it != s_cargo.end();) { it = liveDroid.count(it->first) ? std::next(it) : s_cargo.erase(it); }

	// Bruchstuecke: Rauch, und bei vollstaendiger Reparatur wieder durch die heile Pipeline ersetzen
	std::vector<STRUCTURE *> repaired;
	for (STRUCTURE *s : brokenPieces)
	{
		if (tileSeen(s->pos.x, s->pos.y))
		{
			const unsigned it = gameTime - deltaGameTime / 2 + 1;
			Vector3i sp(s->pos.x, s->pos.z + 50, s->pos.y);
			addEffect(&sp, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH, false, nullptr, 0, it);
		}
		if (s->body >= structureBodyBuilt(s)) { repaired.push_back(s); }
	}
	for (STRUCTURE *s : repaired)
	{
		const Replace r{s->pos.x, s->pos.y, s->rot.direction, static_cast<int>(s->player), false};
		removeStruct(s, true, gameWorld);
		s_repl.push_back(r);
	}

	updateTerrain();

	syncDebug("modoil b%zu f%zu c%zu", s_barrels.size(), s_fuel.size(), s_cargo.size());
}

// ---------------------------------------------------------------- tiefe Graeben: Gelaende absenken

constexpr int TRENCH_DEPTH = 20;

bool isDeepTrench(const STRUCTURE_STATS *st)
{
	if (st == nullptr || !st->infantryPassable) { return false; }
	return st->id.toUtf8().find("Deep") != std::string::npos;
}

void updateTerrain()
{
	std::set<std::pair<int, int>> want;
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s == nullptr || s->died || s->status != SS_BUILT || !isDeepTrench(s->pStructureType)) { continue; }
			const int tx = map_coord(s->pos.x), ty = map_coord(s->pos.y);
			for (int dx = 0; dx <= 1; dx++)
			{
				for (int dy = 0; dy <= 1; dy++)
				{
					const int vx = tx + dx, vy = ty + dy;
					if (vx > 0 && vy > 0 && vx < static_cast<int>(gameWorld.map.width) - 1 && vy < static_cast<int>(gameWorld.map.height) - 1)
					{
						want.insert(std::make_pair(vx, vy));
					}
				}
			}
		}
	}
	if (want.empty() && s_lowered.empty()) { return; }
	for (const auto &v : want)
	{
		if (s_lowered.find(v) == s_lowered.end())
		{
			const int h = map_TileHeight(gameWorld.map, v.first, v.second);
			s_lowered[v] = h;
			setTileHeight(gameWorld.map, v.first, v.second, h - TRENCH_DEPTH);
		}
	}
	for (auto it = s_lowered.begin(); it != s_lowered.end();)
	{
		if (want.count(it->first) == 0)
		{
			setTileHeight(gameWorld.map, it->first.first, it->first.second, it->second);
			it = s_lowered.erase(it);
		}
		else { ++it; }
	}
}

} // namespace

void tick()
{
	processReplacements();
	processBooms();
	if (s_have && gameTime >= s_lastSec * 1000 && gameTime / 1000 == s_lastSec) { return; }
	s_have = true;
	s_lastSec = gameTime / 1000;
	if (!modPresent()) { return; }
	logistics();
}

// ---------------------------------------------------------------- Anzeige (Anzeigemodus 4)

namespace
{

bool project(const Vector3i &world, const glm::mat4 &m, Vector2i &out)
{
	const Vector3i pos(world.x, world.z, -world.y);
	return pie_RotateProjectWithPerspective(&pos, m, &out) > 0;
}

void squares(const glm::mat4 &m, int wx, int wy, int wz, int have, int cap, PIELIGHT on, PIELIGHT off)
{
	Vector2i s(0, 0);
	if (!project(Vector3i(wx, wy, wz), m, s)) { return; }
	constexpr int w = 5, gap = 2;
	const int total = cap * (w + gap) - gap;
	const int x0 = s.x - total / 2;
	for (int i = 0; i < cap; i++)
	{
		pie_BoxFill(x0 + i * (w + gap) - 1, s.y - 1, x0 + i * (w + gap) + w + 1, s.y + w + 1, pal_RGBA(0, 0, 0, 255));
		pie_BoxFill(x0 + i * (w + gap), s.y, x0 + i * (w + gap) + w, s.y + w, i < have ? on : off);
	}
}

void bar(const glm::mat4 &m, int wx, int wy, int wz, int value, int maxValue, PIELIGHT on, PIELIGHT off)
{
	Vector2i s(0, 0);
	if (!project(Vector3i(wx, wy, wz), m, s)) { return; }
	constexpr int w = 30, h = 5;
	const int fill = std::min(w, std::max(0, value) * w / std::max(1, maxValue));
	pie_BoxFill(s.x - w / 2 - 1, s.y - 1, s.x + w / 2 + 1, s.y + h + 1, pal_RGBA(0, 0, 0, 255));
	pie_BoxFill(s.x - w / 2, s.y, s.x + w / 2, s.y + h, off);
	if (fill > 0) { pie_BoxFill(s.x - w / 2, s.y, s.x - w / 2 + fill, s.y + h, on); }
}

} // namespace

void drawOverlay(const glm::mat4 &m)
{
	if (range_rings::getMode() != 4 || !manages(static_cast<int>(selectedPlayer))) { return; }
	const int p = static_cast<int>(selectedPlayer);
	const PIELIGHT barrelOn = pal_RGBA(255, 214, 40, 255), barrelOff = pal_RGBA(60, 50, 20, 255);
	const PIELIGHT storeOn = pal_RGBA(90, 190, 255, 255), storeOff = pal_RGBA(20, 40, 60, 255), refinedOn = pal_RGBA(255, 130, 30, 255);
	const PIELIGHT okOn = pal_RGBA(70, 255, 90, 255), lowOn = pal_RGBA(255, 60, 50, 255), barOff = pal_RGBA(30, 30, 30, 255);

	std::vector<const STRUCTURE *> gens;
	for (const STRUCTURE *s : gameWorld.objects.structures[p])
	{
		if (s->died || s->status != SS_BUILT) { continue; }
		if (s->pStructureType->type == REF_RESOURCE_EXTRACTOR)
		{
			const auto it = s_barrels.find(s->id);
			squares(m, s->pos.x, s->pos.y, s->pos.z + 200, it == s_barrels.end() ? 0 : it->second, DERRICK_CAP, barrelOn, barrelOff);
		}
		else if (isRefinery(s->pStructureType))
		{
			const auto ri = s_refIn.find(s->id);
			const auto ro = s_refOut.find(s->id);
			squares(m, s->pos.x, s->pos.y, s->pos.z + 300, ri == s_refIn.end() ? 0 : ri->second, REF_CAP, barrelOn, barrelOff);
			squares(m, s->pos.x, s->pos.y, s->pos.z + 330, ro == s_refOut.end() ? 0 : ro->second, REF_CAP, refinedOn, storeOff);
		}
		else if (s->pStructureType->type == REF_POWER_GEN)
		{
			gens.push_back(s);
			const auto it = s_fuel.find(s->id);
			const int fuel = it == s_fuel.end() ? START_FUEL : it->second;
			bar(m, s->pos.x, s->pos.y, s->pos.z + 230, fuel, 2 * GRID_SECS, fuel > PULL_BELOW ? okOn : lowOn, barOff);
		}
	}
	for (const FEATURE *f : gameWorld.objects.features[0])
	{
		if (f->died || f->psStats == nullptr || !idIs(f->psStats->id, "WaterTower")) { continue; }
		bool closeBy = false;
		for (const STRUCTURE *g : gens)
		{
			if (d2(f->pos.x, f->pos.y, g->pos.x, g->pos.y) <= STORAGE_RANGE * STORAGE_RANGE) { closeBy = true; break; }
		}
		const auto it = s_store.find(f->id);
		const auto ir = s_storeR.find(f->id);
		const int have = (it == s_store.end() ? 0 : it->second) + (ir == s_storeR.end() ? 0 : ir->second);
		if (!closeBy && have <= 0) { continue; }
		squares(m, f->pos.x, f->pos.y, f->pos.z + 330, have, STORAGE_CAP, storeOn, storeOff);
	}
	for (const DROID *d : gameWorld.objects.droids[p])
	{
		if (d->died) { continue; }
		const auto it = s_cargo.find(d->id);
		if (it == s_cargo.end()) { continue; }
		squares(m, d->pos.x, d->pos.y, d->pos.z + 140, it->second.n, it->second.cap, barrelOn, barrelOff);
	}
}

}
