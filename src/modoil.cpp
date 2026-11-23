// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Oelkreislauf, siehe modoil.h

#include "modoil.h"
#include "modshockwave.h"
#include "modcontrol.h"   // (21.11.) Spieler-Befehl unterbricht den Autopiloten kurz
#include "action.h"
#include "modterrain.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/imd.h"
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
#include "qtscript.h"   // triggerEventDroidBuilt

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/transform.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <unordered_map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "modgrid.h"
#include "modcrates.h"
#include "modfuel.h"
#include "modnuke.h"

namespace modoil
{

namespace
{

// (Wunsch 20.11. 17:46) Neuer Kreislauf: Oelturm -> (Pickup oder Pipeline) -> Raffinerie -> Oeltanker (wahlweise ueber das Oellager,
// kleiner Bonus) -> Kraftwerk und Tankstelle. Rohoel geht nur noch in die Raffinerie; Kraftwerk, Lager und Tankstelle bekommen nur
// veredelte Faesser vom Tanker. Alle Speicher starten leer.
constexpr int DERRICK_CAP = 20;             // Fass-Plaetze am Oelturm ("deutlich mehr als 5")
constexpr int DERRICK_SECS = 180;           // jeder Oelturm stellt 1 Fass alle 3 Minuten ab (Fracht fuer Pickups, Tanker und Tankstellen)
// (Wunsch 23.11. 19:29 "Die Economy laeuft viel zu langsam ... sie sollte kontinuierlich im WZ-Stil konstant laufen, anstatt schubweise.
// Sobald alle wichtigen Eco-Gebaeude verbunden sind = Boom, staendiges Plus, ohne erst lange warten zu muessen, dass Faesser verarbeitet
// werden"): die Einnahmen laufen wie in Warzone stetig ueber die Verbindung Oelturm -> Kraftwerk (4 Oeltuerme je Kraftwerk, Kraftwerk-Modul
// und Forschung wie WZ, power.cpp). Die Faesser fahren weiter sichtbar im Kreislauf, das Einkommen haengt aber nicht mehr an ihrer Ankunft.
// Abgestuft, damit es sanft anlaeuft: schon Oelturm + Kraftwerk bringen etwas, die Raffinerie den vollen Ertrag, das Lager etwas obendrauf.
// (WZ: 1 Energie je Sekunde und Oelturm x Kraftwerk-Punkte / 100 - ohne Modul 55 = 33 je Minute; Modul und Forschung erhoehen die Punkte)
constexpr int OIL_PCT_CRUDE = 75;           // Oelturm am Kraftwerk, aber keine Raffinerie angeschlossen: Rohoel brennt schlechter
constexpr int OIL_PCT_REFINED = 120;        // Raffinerie angeschlossen (Rohr zum Oelturm oder Pickups) und Raffinat am Kraftwerk (Tanker oder Naehe)
constexpr int OIL_PCT_STORE = 10;           // Oellager mit Vorrat in Reichweite des Kraftwerks: +10 % (Punkte)
constexpr int STORAGE_CAP = 8;              // Fass-Plaetze im Lager (Oellager / Wasserturm)
// (Wunsch 22.11. 18:27 "grosses Oellager"): Grosses Oellager "A0BaBaOilStoreBig" (schwarze Lagerhallen, 3x2) fasst 48 Faesser
constexpr int STORAGE_CAP_BIG = 48;
std::unordered_map<uint32_t, int> s_storeCapById;   // Fass-Plaetze je Lager (fehlt = STORAGE_CAP)
int storeCapOf(const STRUCTURE_STATS *st)
{
	return st != nullptr && st->id.startsWith("A0BaBaOilStoreBig") ? STORAGE_CAP_BIG : STORAGE_CAP;
}
int storeCapById(uint32_t id)
{
	const auto it = s_storeCapById.find(id);
	return it == s_storeCapById.end() ? STORAGE_CAP : it->second;
}
constexpr int REF_CAP = 10;                 // Fass-Plaetze in der Raffinerie (je Eingang und Ausgang)
constexpr int REF_SECS = 20;                // Sekunden je veredeltes Fass
constexpr int REF_SECS_NOPOWER = 40;        // (20.11.) Notbetrieb ohne Strom: halbes Tempo (sonst steht der Kreislauf, wenn das Kraftwerk leer ist)
constexpr int STATION_CAP = 10;             // (16.11.) Fass-Plaetze an der Tankstelle
constexpr int STATION_START = 0;            // (20.11.) neue Tankstelle startet leer
constexpr int STATION_LOW = 6;              // Tanker beliefern die Tankstelle bevorzugt, solange weniger Faesser da sind
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
	bool manualWait = false;    // (18.11.) nach einem Spieler-Befehl: erst kurz frei stehen, dann wieder Autopilot
	int idleSec = 0;
	bool forced = false;        // (21.11.) Auftrag kam per Klick auf ein Oel-Gebaeude (startOrder) - kein "fremder" Spieler-Befehl
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
std::map<uint32_t, int> s_oilPct;           // (23.11. 19:29) Oelturm -> Ertrag in Prozent (Raffinerie-/Lager-Anschluss), power.cpp liest ihn
std::map<uint32_t, int> s_store;           // Wasserturm (Feature) -> rohe Faesser
std::map<uint32_t, int> s_storeR;           // Wasserturm (Feature) -> veredelte Faesser
std::map<uint32_t, int> s_refIn, s_refOut, s_refT;   // Raffinerie: Eingang, Ausgang, Sekunden im laufenden Fass
std::map<uint32_t, CargoState> s_cargo;     // Lkw -> Zustand
std::map<uint32_t, int> s_station;           // (16.11.) Tankstelle -> Vorrat in Promille-Faessern (1000 = 1 Fass)
std::map<std::pair<int, int>, int> s_lowered;   // tiefe Graeben: abgesenkte Kachel -> Tiefe (modterrain)
std::vector<Boom> s_booms;
std::vector<Replace> s_repl;
int s_gift[MAX_PLAYERS] = {};               // (20.11. Punkt 4) Bit 1 = Kraftwerk-Geschenk, Bit 2 = Raffinerie-Geschenk schon vergeben
bool s_duty[MAX_PLAYERS] = {};              // (20.11.) Strompflicht: erst ab der ersten echten Stromquelle (modgrid::noteDuty)
int s_bank[MAX_PLAYERS] = {};               // Restguthaben aus Spielstaenden der Fass-Wirtschaft (vor 23.11.): wird einmal ausgezahlt
int64_t s_oilFP[MAX_PLAYERS] = {};          // (23.11.) Oel-Einnahmen aus power.cpp (Festkomma 32.32) fuer das Einnahmen-Buch
std::vector<std::pair<int, bool>> s_tankRespawn;   // (20.11.) (Spieler, Tanker?) - letzter Oeltanker bzw. letzter Pickup zerstoert
int s_beepTrack = -2;                       // (20.11. 19:06) Rueckfahr-Piepen beim Abliefern (-2 = noch nicht gesucht)
bool s_have = false;
UDWORD s_lastSec = 0;
// (Wunsch 17.11. 20:35 "Economy pruefen") Einnahmen je Quelle seit dem letzten Bericht (Log jede Spielminute)
int64_t s_ledger[MAX_PLAYERS][INC_COUNT] = {};
int64_t s_ledgerPower[MAX_PLAYERS] = {};
bool s_ledgerStarted[MAX_PLAYERS] = {};
int64_t s_lastIncome[MAX_PLAYERS] = {};     // (20.11.) Oel-Fenster: Einnahmen und verfallene Energie der letzten Spielminute
int64_t s_lastWaste[MAX_PLAYERS] = {};
int64_t s_wasteMark[MAX_PLAYERS] = {};
UDWORD s_ledgerMinute = 0;

bool idIs(const WzString &id, const char *s) { return id.toUtf8() == s; }
bool idStarts(const WzString &id, const char *s) { return id.toUtf8().rfind(s, 0) == 0; }

bool isPipe(const STRUCTURE_STATS *st)
{
	return st != nullptr && st->type == REF_WALL && idStarts(st->id, "A0BaBaPipeline");
}

/// MOD (Wunsch 16.11. 21:25): baubarer Oellagerturm (Modell = WZ-Wasserturm) - arbeitet wie die Wasserturm-Features als Lager
bool isOilStore(const STRUCTURE_STATS *st)
{
	return st != nullptr && st->type == REF_GENERIC && idStarts(st->id, "A0BaBaOilStore");
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
	if (idIs(cs->id, "ScavBed1")) { return 3; }   // (18.11.) groessere Pickup-Ladeflaeche: 3 statt 2
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
	if (n >= 5) { modshockwave::spawn(x, y, z, m); }   // MOD (22.11. 21:19): Druckwelle bei grossen Oel-Explosionen
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

int generatorDerricks(const STRUCTURE *g)
{
	if (g == nullptr || g->pStructureType == nullptr || g->pStructureType->type != REF_POWER_GEN || g->pFunctionality == nullptr) { return 0; }
	int n = 0;
	for (int i = 0; i < NUM_POWER_MODULES; ++i)
	{
		const STRUCTURE *e = g->pFunctionality->powerGenerator.apResExtractors[i];
		if (e != nullptr && !e->died) { n++; }
	}
	return n;
}

int oilPercent(const STRUCTURE *derrick)
{
	if (derrick == nullptr) { return OIL_PCT_CRUDE; }
	const auto it = s_oilPct.find(derrick->id);
	return it == s_oilPct.end() ? OIL_PCT_CRUDE : it->second;
}

void noteOilIncomeFP(int player, int64_t amountFP)
{
	if (player >= 0 && player < MAX_PLAYERS && amountFP > 0) { s_oilFP[player] += amountFP; }
}

bool generatorHungry(const STRUCTURE *g)
{
	// (23.11.) "hungrig" = Kraftwerk ohne angeschlossenen Oelturm (liefert nichts)
	return g != nullptr && manages(static_cast<int>(g->player)) && g->status == SS_BUILT && !modgrid::generatorStandby(g) && generatorDerricks(g) == 0;
}

EconomyView economyView(int p)
{
	EconomyView v;
	if (p < 0 || p >= MAX_PLAYERS || !manages(p)) { return v; }
	v.active = true;
	std::vector<const STRUCTURE *> pipes;
	int64_t rateNum = 0;   // (23.11.) stetiger Oel-Ertrag je Minute x 10^6
	for (const STRUCTURE *s : gameWorld.objects.structures[p])
	{
		if (s->died || s->status != SS_BUILT || s->pStructureType == nullptr) { continue; }
		const STRUCTURE_STATS *st = s->pStructureType;
		if (st->type == REF_RESOURCE_EXTRACTOR)
		{
			v.derricks++;
			const auto it = s_barrels.find(s->id);
			const int b = it == s_barrels.end() ? 0 : it->second;
			v.derrickBarrels += b; v.derrickCap += DERRICK_CAP;
			if (b >= DERRICK_CAP) { v.derricksFull++; }
		}
		else if (isRefinery(st))
		{
			v.refineries++;
			const auto ri = s_refIn.find(s->id), ro = s_refOut.find(s->id);
			v.refIn += ri == s_refIn.end() ? 0 : ri->second;
			v.refOut += ro == s_refOut.end() ? 0 : ro->second;
			v.refCap += REF_CAP;
			if (modgrid::isPoweredDown(s)) { v.refNoPower++; }
		}
		else if (isOilStore(st))
		{
			int cap = 0;
			v.stores++; v.storeBarrels += std::max(0, storeBarrels(s, cap)); v.storeCap += cap;
		}
		else if (st->type == REF_POWER_GEN)
		{
			v.gens++;
			if (generatorRunning(s)) { v.gensRunning++; }
			if (modgrid::generatorStandby(s)) { v.gensStandby++; }
			v.derrickSlots += NUM_POWER_MODULES;
			// (23.11.) stetiger Ertrag wie in WZ: je angeschlossenem Oelturm 1 je Sekunde x Energie-Einstellung x Kraftwerk (Punkte / 100) x Anschluss
			const int pts = std::max(1, getBuildingPowerPoints(s));
			const int mod = getPowerModifier(p);
			for (int i = 0; i < NUM_POWER_MODULES; ++i)
			{
				const STRUCTURE *e = s->pFunctionality != nullptr ? s->pFunctionality->powerGenerator.apResExtractors[i] : nullptr;
				if (e == nullptr || e->died) { continue; }
				v.linkedDerricks++;
				const int pct = oilPercent(e);
				if (pct < OIL_PCT_REFINED) { v.crudeDerricks++; }
				rateNum += static_cast<int64_t>(60) * mod * pts * pct;
			}
			if (generatorDerricks(s) == 0 && !modgrid::generatorStandby(s)) { v.gensIdle++; }
		}
		else if (modfuel::isStation(s))
		{
			v.stations++; v.stationBarrels += stationBarrels(s); v.stationCap += STATION_CAP;
		}
		else if (isPipe(st) && !isBroken(st)) { pipes.push_back(s); }
		if (modgrid::isPoweredDown(s)) { v.buildingsDown++; }
	}
	// Oeltuerme mit Rohr direkt daneben (grobe Anzeige)
	for (const STRUCTURE *s : gameWorld.objects.structures[p])
	{
		if (s->died || s->status != SS_BUILT || s->pStructureType->type != REF_RESOURCE_EXTRACTOR) { continue; }
		for (const STRUCTURE *q : pipes)
		{
			if (std::abs(q->pos.x - s->pos.x) <= DIST_TILE + DIST_TILE / 2 && std::abs(q->pos.y - s->pos.y) <= DIST_TILE + DIST_TILE / 2) { v.pipeDerricks++; break; }
		}
	}
	for (const DROID *d : gameWorld.objects.droids[p])
	{
		if (d->died) { continue; }
		const int cap = cargoCapacity(d);
		if (cap <= 0) { continue; }
		const auto it = s_cargo.find(d->id);
		const int n = it == s_cargo.end() ? 0 : it->second.n;
		if (isTank(d)) { v.tankers++; v.tankerBarrels += n; v.tankerCap += cap; }
		else { v.pickups++; v.pickupBarrels += n; v.pickupCap += cap; }
	}
	v.ratePerMin = static_cast<int>(rateNum / 1000000);
	v.power = getPower(static_cast<unsigned>(p));
	v.powerMax = getPowerMaxStorage(static_cast<unsigned>(p));
	v.incomeLastMinute = static_cast<int>(s_lastIncome[p]);
	v.wastedLastMinute = static_cast<int>(s_lastWaste[p]);
	return v;
}

int stationCapMilli()
{
	return STATION_CAP * 1000;
}

int stationMilli(const STRUCTURE *st)
{
	if (st == nullptr) { return 0; }
	if (!manages(static_cast<int>(st->player))) { return STATION_CAP * 1000; }   // KI ohne Oel-Wirtschaft: Tankstelle immer voll
	const auto it = s_station.find(st->id);
	return it == s_station.end() ? STATION_START * 1000 : it->second;
}

int stationTake(const STRUCTURE *st, int milli)
{
	if (st == nullptr || milli <= 0) { return 0; }
	if (!manages(static_cast<int>(st->player))) { return milli; }
	auto it = s_station.find(st->id);
	if (it == s_station.end()) { it = s_station.emplace(st->id, STATION_START * 1000).first; }
	const int take = std::min(milli, it->second);
	it->second -= take;
	return take;
}

int storeBarrels(const STRUCTURE *st, int &cap)
{
	cap = STORAGE_CAP;
	if (st == nullptr || st->pStructureType == nullptr || !isOilStore(st->pStructureType)) { return -1; }
	cap = storeCapOf(st->pStructureType);
	const auto it = s_store.find(st->id);
	const auto ir = s_storeR.find(st->id);
	return (it == s_store.end() ? 0 : it->second) + (ir == s_storeR.end() ? 0 : ir->second);
}

int stationBarrels(const STRUCTURE *st)
{
	return (stationMilli(st) + 999) / 1000;
}

void noteIncome(int player, int kind, int amount)
{
	if (player < 0 || player >= MAX_PLAYERS || kind < 0 || kind >= INC_COUNT) { return; }
	s_ledger[player][kind] += amount;
}

bool manages(int player)
{
	if (player < 0 || player >= MAX_PLAYERS || !modPresent()) { return false; }
	// Eine Regel fuer jede Spielart: alle menschlichen Spieler (im Einzelspieler traegt SPinit Spieler 0 als Mensch ein)
	return isHumanPlayer(player);
}

void reset()
{
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (int k = 0; k < INC_COUNT; k++) { s_ledger[p][k] = 0; }
		s_ledgerPower[p] = 0;
		s_ledgerStarted[p] = false;
	}
	s_ledgerMinute = 0;
	s_barrels.clear();
	s_progress.clear();
	s_oilPct.clear();
	s_store.clear();
	s_storeCapById.clear();
	s_storeR.clear();
	s_refIn.clear();
	s_refOut.clear();
	s_refT.clear();
	s_cargo.clear();
	s_lowered.clear();
	s_station.clear();
	s_booms.clear();
	s_repl.clear();
	for (int p = 0; p < MAX_PLAYERS; p++) { s_gift[p] = 0; s_duty[p] = false; s_bank[p] = 0; s_oilFP[p] = 0; s_lastIncome[p] = 0; s_lastWaste[p] = 0; s_wasteMark[p] = 0; }
	s_tankRespawn.clear();
	s_have = false;
}

namespace
{
template<typename M> void saveMap(std::vector<int64_t> &out, int tag, const M &m)
{
	for (const auto &kv : m) { out.push_back(tag); out.push_back(static_cast<int64_t>(kv.first)); out.push_back(static_cast<int64_t>(kv.second)); }
}
}

void saveState(std::vector<int64_t> &out)
{
	out.clear();
	saveMap(out, 1, s_barrels);
	saveMap(out, 2, s_progress);
	saveMap(out, 4, s_store);
	saveMap(out, 5, s_storeR);
	saveMap(out, 6, s_refIn);
	saveMap(out, 7, s_refOut);
	saveMap(out, 8, s_refT);
	saveMap(out, 9, s_station);
	saveMap(out, 13, s_oilPct);   // (23.11.) Ertrag je Oelturm - gleich nach dem Laden wie vorher
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		if (s_gift[p] != 0) { out.push_back(10); out.push_back(p); out.push_back(s_gift[p]); }
		if (s_duty[p]) { out.push_back(11); out.push_back(p); out.push_back(1); }
		if (s_bank[p] > 0) { out.push_back(12); out.push_back(p); out.push_back(s_bank[p]); }   // Restguthaben (noch nicht ausgezahlt)
	}
}

void loadState(const std::vector<int64_t> &in)
{
	for (size_t i = 0; i + 3 <= in.size(); i += 3)
	{
		const uint32_t id = static_cast<uint32_t>(in[i + 1]);
		const int64_t v = in[i + 2];
		const int iv = static_cast<int>(std::clamp<int64_t>(v, 0, 1000000000));
		switch (static_cast<int>(in[i]))
		{
		case 1: s_barrels[id] = iv; break;
		case 2: s_progress[id] = std::max<int64_t>(0, v); break;
		case 3: break;   // (23.11.) Brennstoff der Kraftwerke gibt es nicht mehr (stetige Einnahmen)
		case 4: s_storeR[id] += iv; break;   // (20.11.) Lager hat nur noch veredelte Faesser
		case 10: if (id < MAX_PLAYERS) { s_gift[id] = iv; } break;
		case 11: if (id < MAX_PLAYERS) { s_duty[id] = iv != 0; } break;
		case 12: if (id < MAX_PLAYERS) { s_bank[id] = iv; } break;
		case 5: s_storeR[id] += iv; break;
		case 6: s_refIn[id] = iv; break;
		case 7: s_refOut[id] = iv; break;
		case 8: s_refT[id] = iv; break;
		case 9: s_station[id] = iv; break;
		case 13: s_oilPct[id] = iv; break;
		default: break;
		}
	}
}

void extract(const STRUCTURE *, int64_t, int64_t)
{
	// (20.11.) nicht mehr benutzt: jeder Oelturm foerdert gleich schnell, unabhaengig vom Kraftwerk (siehe logistics)
}

bool dutyStarted(int player)
{
	return player >= 0 && player < MAX_PLAYERS && s_duty[player];
}

void noteDuty(int player)
{
	if (player >= 0 && player < MAX_PLAYERS) { s_duty[player] = true; }
}

bool isCargoVehicle(const DROID *d)
{
	return d != nullptr && cargoCapacity(d) > 0;
}

int refineryProgress(const STRUCTURE *r)
{
	if (r == nullptr || !isRefinery(r->pStructureType) || !manages(static_cast<int>(r->player))) { return -1; }
	const auto ii = s_refIn.find(r->id), io = s_refOut.find(r->id), it = s_refT.find(r->id);
	const int in = ii == s_refIn.end() ? 0 : ii->second, out = io == s_refOut.end() ? 0 : io->second, t = it == s_refT.end() ? 0 : it->second;
	if (in <= 0 || out >= REF_CAP) { return 0; }
	const int need = modgrid::isPoweredDown(r) ? REF_SECS_NOPOWER : REF_SECS;
	return std::clamp(1 + t * 1000 / need, 1, 1000);
}

int cargoBarrels(const DROID *d, int &cap)
{
	cap = (d != nullptr && manages(static_cast<int>(d->player))) ? cargoCapacity(d) : 0;
	if (cap <= 0) { return 0; }
	const auto it = s_cargo.find(d->id);
	return it == s_cargo.end() ? 0 : it->second.n;
}


bool isInfantry(const DROID *d)
{
	if (d == nullptr) { return false; }
	switch (d->droidType)
	{
	case DROID_PERSON:
	case DROID_CYBORG:
	case DROID_CYBORG_SUPER:
	case DROID_CYBORG_CONSTRUCT:
	case DROID_CYBORG_REPAIR:
		break;
	default:
		return false;
	}
	const PROPULSION_STATS *prop = d->getPropulsionStats();
	return prop != nullptr && prop->propulsionType == PROPULSION_TYPE_LEGGED;
}

bool tilePassableForInfantry(const WorldMapState &mapState, int tileX, int tileY)
{
	const MAPTILE *t = mapTile(mapState, tileX, tileY);
	if (t == nullptr || t->psObject == nullptr) { return false; }
	if (t->psObject->type == OBJ_FEATURE)   // MOD: besetzbare Haeuser/Hochhaeuser (nicht auf Klippen/im Wasser)
	{
		const FEATURE *f = static_cast<const FEATURE *>(t->psObject);
		return f->psStats != nullptr && f->psStats->infantryPassable && terrainType(t) != TER_CLIFFFACE && terrainType(t) != TER_WATER;
	}
	if (t->psObject->type != OBJ_STRUCTURE) { return false; }
	const STRUCTURE *s = static_cast<const STRUCTURE *>(t->psObject);
	return s->pStructureType != nullptr && s->pStructureType->infantryPassable && s->status == SS_BUILT;
}

bool isHouseOccupant(const DROID *d)
{
	return d != nullptr && d->droidType == DROID_PERSON && isInfantry(d);
}

bool tileHasHouse(const WorldMapState &mapState, int tileX, int tileY)
{
	const MAPTILE *t = mapTile(mapState, tileX, tileY);
	if (t == nullptr || t->psObject == nullptr || t->psObject->type != OBJ_FEATURE) { return false; }
	const FEATURE *f = static_cast<const FEATURE *>(t->psObject);
	return f->psStats != nullptr && f->psStats->infantryPassable;
}

bool trenchForInfantry(const BASE_OBJECT *obj, const DROID *dominant)
{
	if (obj != nullptr && dominant != nullptr && obj->type == OBJ_FEATURE)   // MOD: Haus/Hochhaus besetzen
	{
		const FEATURE *f = static_cast<const FEATURE *>(obj);
		return f->psStats != nullptr && f->psStats->infantryPassable && isHouseOccupant(dominant);   // nur echte Infanterie, keine Cyborgs
	}
	if (obj == nullptr || dominant == nullptr || obj->type != OBJ_STRUCTURE || obj->player != selectedPlayer) { return false; }
	const STRUCTURE *s = static_cast<const STRUCTURE *>(obj);
	if (s->pStructureType == nullptr || !s->pStructureType->infantryPassable || s->status != SS_BUILT) { return false; }
	return isInfantry(dominant);
}

bool droidInTrench(const DROID *d)
{
	if (!isInfantry(d)) { return false; }
	return tilePassableForInfantry(gameWorld.map, map_coord(d->pos.x), map_coord(d->pos.y));
}

int roofHeight(const DROID *d)
{
	if (d == nullptr || d->droidType != DROID_PERSON || !isInfantry(d)) { return 0; }
	const MAPTILE *t = mapTile(gameWorld.map, map_coord(d->pos.x), map_coord(d->pos.y));
	if (t == nullptr || t->psObject == nullptr || t->psObject->type != OBJ_STRUCTURE) { return 0; }
	const STRUCTURE *s = static_cast<const STRUCTURE *>(t->psObject);
	if (s->pStructureType == nullptr || s->status != SS_BUILT || !s->pStructureType->infantryPassable) { return 0; }
	return s->pStructureType->id.startsWith("A0BaBaContainer") ? 54 : 0;   // Containerdach (55 hoch)
}

bool isOilTarget(const DROID *d, const STRUCTURE *s)
{
	if (d == nullptr || s == nullptr || s->died || s->status != SS_BUILT || d->player != s->player || !manages(static_cast<int>(d->player))) { return false; }
	if (cargoCapacity(d) <= 0) { return false; }
	const STRUCTURE_STATS *st = s->pStructureType;
	if (isTank(d)) { return isRefinery(st) || st->type == REF_POWER_GEN || modfuel::isStation(s) || isOilStore(st); }
	return st->type == REF_RESOURCE_EXTRACTOR || isRefinery(st);
}

bool startOrder(DROID *d, int x, int y, uint16_t code)
{
	if (code != CODE_OIL) { return false; }
	if (d == nullptr || d->player >= MAX_PLAYERS) { return true; }
	STRUCTURE *tgt = nullptr;
	for (STRUCTURE *s : gameWorld.objects.structures[d->player])
	{
		if (s != nullptr && s->pos.x == x && s->pos.y == y && isOilTarget(d, s)) { tgt = s; break; }
	}
	d->order = DroidOrder(DORDER_NONE);
	actionDroid(d, DACTION_NONE);
	CargoState &c = s_cargo[d->id];
	c.player = static_cast<int>(d->player);
	c.cap = cargoCapacity(d);
	c.tank = isTank(d);
	c.manualWait = false; c.idleSec = 0; c.pause = 0; c.stuck = 0; c.forced = true;
	c.mode = 0;   // nichts Sinnvolles an diesem Gebaeude (z. B. leer zum Kraftwerk): einfach sofort weiter im Arbeitsmodus
	if (tgt == nullptr) { return true; }
	const STRUCTURE_STATS *st = tgt->pStructureType;
	int mode = 0, kind = 0;
	if (st->type == REF_RESOURCE_EXTRACTOR) { mode = 1; kind = 3; }                         // Pickup: Rohoel am Oelturm laden
	else if (isRefinery(st)) { if (c.tank) { mode = c.n < c.cap ? 1 : 0; kind = 2; } else if (c.n > 0) { mode = 2; kind = 2; } }
	else if (st->type == REF_POWER_GEN) { if (c.n > 0) { mode = 2; kind = 0; } }
	else if (modfuel::isStation(tgt)) { if (c.n > 0) { mode = 2; kind = 4; } }
	else if (isOilStore(st)) { if (c.n > 0) { mode = 2; kind = 1; } }
	if (mode == 0) { return true; }
	c.mode = mode; c.kind = kind; c.target = tgt->id; c.tx = tgt->pos.x; c.ty = tgt->pos.y;
	return true;
}

bool isCargo(uint32_t droidId)
{
	return s_cargo.find(droidId) != s_cargo.end();
}

bool isBusy(uint32_t droidId)
{
	const auto it = s_cargo.find(droidId);
	return it != s_cargo.end() && (it->second.mode != 0 || it->second.n > 0);
}

void drawCargo(const DROID *d, const iIMDShape *bed, const glm::mat4 &m, const glm::mat4 &viewMatrix, int team, PIELIGHT brightness, int pieFlag,
               int pieFlagData, float stretchDepth)
{
	if (d == nullptr || bed == nullptr) { return; }
	const CONSTRUCT_STATS *cs = d->getConstructStats();
	if (cs == nullptr || !cs->id.startsWith("ScavBed")) { return; }   // offene Ladeflaechen; Tanker/Schaufel zeigen nichts
	const auto it = s_cargo.find(d->id);
	const int barrels = (it != s_cargo.end() && !it->second.tank) ? std::max(0, it->second.n) : 0;
	const int crates = std::max(0, modcrates::crates(d));
	const int n = std::min(8, barrels + crates);
	if (n <= 0) { return; }
	static iIMDBaseShape *s_barrel = nullptr;
	static iIMDBaseShape *s_crate = nullptr;
	static unsigned s_tried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
	if (modelCacheStale(s_tried))
	{
		s_barrel = modelGet(WzString::fromUtf8("mioil.pie"));
		s_crate = modelGet(WzString::fromUtf8("ammobox_c.pie"));
	}
	const float x0 = static_cast<float>(bed->min.x), x1 = static_cast<float>(bed->max.x);
	const float z0 = static_cast<float>(bed->min.z), z1 = static_cast<float>(bed->max.z);
	const float y0 = static_cast<float>(bed->min.y);
	const float w = x1 - x0, l = z1 - z0;
	const int cols = (w > 30.f && n > 1) ? 2 : 1;
	const int rows = (n + cols - 1) / cols;
	for (int k = 0; k < n; ++k)
	{
		const bool isBarrel = k < barrels;
		iIMDBaseShape *shape = isBarrel ? s_barrel : s_crate;
		if (shape == nullptr) { continue; }
		const int r = k / cols, c = k % cols;
		const float px = (cols == 1) ? (x0 + x1) * 0.5f : x0 + w * (c == 0 ? 0.3f : 0.7f);
		const float pz = z0 + l * (static_cast<float>(r) + 0.5f) / static_cast<float>(rows);
		// Fass (mioil.pie, 27 breit, 37 hoch) auf etwa 11 Hoehe; Kiste (ammobox_c.pie) fast in Originalgroesse
		const float sc = isBarrel ? std::min(0.36f, (l / static_cast<float>(rows)) / 30.f) : std::min(0.95f, (l / static_cast<float>(rows)) / 11.f);
		// (Wunsch 18.11. 12:45 "Man sieht nicht, ob der Lkw etwas geladen hat") - die Ladung stand am Boden des Ladeflaechen-Modells
		// und verschwand hinter der Bordwand. Jetzt schaut sie ein Stueck ueber die Bordwand heraus (Oberkante = Bordwand + 5).
		const float ch = isBarrel ? 37.f * sc : 7.f * sc;
		const float yb = std::max(y0 + 1.5f, static_cast<float>(bed->max.y) + 5.f - ch);
		const glm::mat4 mm = m * glm::translate(glm::vec3(px, yb, pz)) * glm::rotate(static_cast<float>(k) * 0.7f, glm::vec3(0.f, 1.f, 0.f))
		                     * glm::scale(glm::vec3(sc));
		pie_Draw3DShape(shape->displayModel(), 0, team, brightness, pieFlag, pieFlagData, mm, viewMatrix, stretchDepth);
	}
}

bool generatorRunning(const STRUCTURE *psGen)
{
	if (!manages(psGen->player)) { return true; }
	// (23.11.) ein Kraftwerk laeuft, solange mindestens ein Oelturm angeschlossen ist (wie in WZ, kein Brennstoff-Vorrat mehr)
	return generatorDerricks(psGen) > 0;
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
	else if (isOilStore(st))
	{
		int n = 0;
		const auto it = s_store.find(psDel->id);
		if (it != s_store.end()) { n = it->second; s_store.erase(it); }
		const auto ir = s_storeR.find(psDel->id);
		if (ir != s_storeR.end()) { n += ir->second; s_storeR.erase(ir); }
		if (n > 0 && psDel->status == SS_BUILT) { queueBoom(psDel->pos.x, psDel->pos.y, psDel->pos.z, n, true, 100); }
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
	// (Wunsch 20.11. 17:46 / 19:06 "beide spawnen kostenlos") letzter Oeltanker bzw. letzter Pickup zerstoert -> sofort ein neuer (leer, gratis)
	if (cargoCapacity(psDel) > 0 && manages(static_cast<int>(psDel->player)))
	{
		const bool tank = isTank(psDel);
		bool other = false;
		for (const DROID *d : gameWorld.objects.droids[psDel->player])
		{
			if (d != psDel && !d->died && cargoCapacity(d) > 0 && isTank(d) == tank) { other = true; break; }
		}
		if (!other) { s_tankRespawn.emplace_back(static_cast<int>(psDel->player), tank); }
	}
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
	std::vector<STRUCTURE *> derricks, gens, pipes, refs, stations;
};

int storeRoom(uint32_t id)
{
	return std::max(0, storeCapById(id) - s_store[id] - s_storeR[id]);
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
		if (f == nullptr || f->died || f->status != SS_BUILT || f->pStructureType == nullptr || f->pStructureType->type != REF_FACTORY || f->pFunctionality == nullptr || f->isMobileCore()) { continue; }
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
	std::vector<BASE_OBJECT *> featureTowers;
	for (FEATURE *f : gameWorld.objects.features[0])
	{
		if (f != nullptr && !f->died && f->psStats != nullptr && idIs(f->psStats->id, "WaterTower")) { featureTowers.push_back(f); }
	}

	std::set<uint32_t> liveStruct, liveDroid;
	std::vector<STRUCTURE *> brokenPieces;
	const int64_t SR2 = STORAGE_RANGE * STORAGE_RANGE;
	const int64_t LR2 = LOAD_RANGE * LOAD_RANGE;

	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		// Lager dieses Spielers: Wassertuerme auf der Karte (fuer alle) + eigene gebaute Oellagertuerme
		std::vector<BASE_OBJECT *> towers = featureTowers;
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s != nullptr && !s->died && s->status == SS_BUILT && isOilStore(s->pStructureType))
			{
				towers.push_back(s); liveStruct.insert(s->id);
				const int cap = storeCapOf(s->pStructureType);
				if (cap != STORAGE_CAP) { s_storeCapById[s->id] = cap; }
			}
		}
		std::map<uint32_t, BASE_OBJECT *> towerById;
		for (BASE_OBJECT *t : towers) { towerById[t->id] = t; }
		Snap sn;
		std::map<uint32_t, STRUCTURE *> sById;
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s == nullptr || s->died || s->status != SS_BUILT) { continue; }
			const STRUCTURE_STATS *st = s->pStructureType;
			if (st->type == REF_RESOURCE_EXTRACTOR) { sn.derricks.push_back(s); liveStruct.insert(s->id); sById[s->id] = s; }
			else if (st->type == REF_POWER_GEN) { sn.gens.push_back(s); liveStruct.insert(s->id); sById[s->id] = s; }
			else if (isRefinery(st)) { sn.refs.push_back(s); liveStruct.insert(s->id); sById[s->id] = s; }
			else if (modfuel::isStation(s)) { sn.stations.push_back(s); liveStruct.insert(s->id); sById[s->id] = s; if (s_station.find(s->id) == s_station.end()) { s_station[s->id] = STATION_START * 1000; } }
			else if (isPipe(st))
			{
				if (isBroken(st)) { brokenPieces.push_back(s); }
				else { sn.pipes.push_back(s); }
			}
		}
		if (!manages(p)) { continue; }

		// (Wunsch 20.11. 17:46 "Der Oelturm soll sofort anfangen zu foerdern ... ein Bohrturm soll immer gleich foerdern"): jeder fertige
		// Oelturm foerdert 1 Fass alle DERRICK_SECS Sekunden - ohne Kraftwerk, ohne Unterschiede - bis DERRICK_CAP Faesser daliegen
		for (STRUCTURE *der : sn.derricks)
		{
			int &b = s_barrels[der->id];
			int64_t &prog = s_progress[der->id];
			if (prog < 0 || prog > DERRICK_SECS) { prog = DERRICK_SECS; }   // aeltere Spielstaende (Festkomma-Fortschritt)
			if (b >= DERRICK_CAP) { continue; }   // voll: es wird nichts mehr abgestellt, bis jemand abholt
			if (++prog >= DERRICK_SECS) { prog = 0; b++; }
		}

		// (20:35) Kraftwerke in Bereitschaft (der Solarstrom reicht) bekommen keine Faesser; die gehen an Lager und Tankstellen
		std::vector<STRUCTURE *> activeGens;
		for (STRUCTURE *g : sn.gens) { if (!modgrid::generatorStandby(g)) { activeGens.push_back(g); } }

		// Raffinerien: ein Fass je REF_SECS Sekunden veredeln; (20.11.) ohne Strom Notbetrieb mit halbem Tempo
		for (STRUCTURE *r : sn.refs)
		{
			int &in = s_refIn[r->id];
			int &out = s_refOut[r->id];
			int &t = s_refT[r->id];
			const int need = modgrid::isPoweredDown(r) ? REF_SECS_NOPOWER : REF_SECS;
			if (in > 0 && out < REF_CAP)
			{
				if (++t >= need) { t = 0; in--; out++; }
			}
			else { t = 0; }
		}

		std::set<uint32_t> pipeLinked;      // (23.11.) Oeltuerme mit Rohr-Verbindung zu einer Raffinerie
		// Pipeline-Netz: Oeltuerme -> Raffinerie (20.11.: nur noch dorthin), ein Fass je Sekunde und Turm. Angeschlossen ist ein Gebaeude, wenn
		// ein Rohrstueck auf einem der 8 Felder rundherum liegt (auch die Raffinerie - vorher 5x5, 20.11. Punkt 9)
		if (!sn.pipes.empty() && !sn.refs.empty())
		{
			constexpr int PS = (DIST_TILE / TILE_UNITS) > 0 ? (DIST_TILE / TILE_UNITS) : 1;   // Feinfelder je Feld (Viertel-Version: 2)
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
			// MOD (20.11. Oel-Pruefung): Rohrstuecke, die sich mit einer Kante beruehren, gehoeren zusammen (nicht ueber Eck). Bei der
			// Kachel-Viertelung belegt ein Rohr 2x2 Feinfelder - der Nachbar liegt dann 2 Feinfelder weiter.
			for (const auto &kv : pidx)
			{
				for (int ddx = -PS; ddx <= PS; ddx++)
				{
					for (int ddy = -PS; ddy <= PS; ddy++)
					{
						if (std::max(std::abs(ddx), std::abs(ddy)) != PS || std::min(std::abs(ddx), std::abs(ddy)) >= PS) { continue; }
						const auto r = pidx.find(std::make_pair(kv.first.first + ddx, kv.first.second + ddy));
						if (r != pidx.end()) { uunite(par, kv.second, r->second); }
					}
				}
			}
			auto touching = [&](int wx, int wy)
			{
				std::set<int> roots;
				const int tx = map_coord(wx), ty = map_coord(wy);
				for (int dx = -PS; dx <= PS; dx++)
				{
					for (int dy = -PS; dy <= PS; dy++)
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
			std::vector<std::set<int>> refRoots;
			for (STRUCTURE *r : sn.refs) { refRoots.push_back(touching(r->pos.x, r->pos.y)); }
			for (STRUCTURE *der : sn.derricks)
			{
				const std::set<int> rd = touching(der->pos.x, der->pos.y);
				if (rd.empty()) { continue; }
				for (size_t k = 0; k < sn.refs.size(); k++)
				{
					if (shares(rd, refRoots[k])) { pipeLinked.insert(der->id); break; }   // (23.11.) Rohr zur Raffinerie = angeschlossen
				}
				int &barrels = s_barrels[der->id];
				if (barrels <= 0) { continue; }
				STRUCTURE *br = nullptr;
				int64_t bd = INT64_MAX;
				for (size_t k = 0; k < sn.refs.size(); k++)
				{
					STRUCTURE *r = sn.refs[k];
					if (s_refIn[r->id] >= REF_CAP) { continue; }
					if (!shares(rd, refRoots[k])) { continue; }
					const int64_t dd = d2(r->pos.x, r->pos.y, der->pos.x, der->pos.y);
					if (dd < bd) { bd = dd; br = r; }
				}
				if (br != nullptr)
				{
					barrels--;
					s_refIn[br->id]++;
				}
			}
		}

		// (Wunsch 23.11. 19:29) Ertrag je Oelturm fuer die stetigen Einnahmen (power.cpp): angeschlossen an ein Kraftwerk (WZ-Verbindung,
		// 4 je Kraftwerk) bringt er Rohoel-Ertrag; ist eine Raffinerie angeschlossen (Rohr zum Oelturm oder ein Pickup faehrt) und kommt das
		// Raffinat zum Kraftwerk (ein Oeltanker faehrt oder die Raffinerie steht in Reichweite des Kraftwerks), den vollen; ein Oellager
		// mit Vorrat in Reichweite des Kraftwerks gibt etwas obendrauf. Haengt nur am Aufbau, nicht an der Ankunft einzelner Faesser.
		{
			bool havePickup = false, haveTanker = false;
			for (const DROID *d : gameWorld.objects.droids[p])
			{
				if (d == nullptr || d->died || cargoCapacity(d) <= 0) { continue; }
				if (isTank(d)) { haveTanker = true; } else { havePickup = true; }
			}
			for (STRUCTURE *der : sn.derricks)
			{
				const STRUCTURE *gen = der->pFunctionality != nullptr ? der->pFunctionality->resourceExtractor.psPowerGen : nullptr;
				int pct = OIL_PCT_CRUDE;
				const bool toRefinery = !sn.refs.empty() && (havePickup || pipeLinked.count(der->id) != 0);
				if (toRefinery)
				{
					bool toGen = haveTanker;
					if (!toGen && gen != nullptr)
					{
						for (STRUCTURE *r : sn.refs)
						{
							if (d2(r->pos.x, r->pos.y, gen->pos.x, gen->pos.y) <= SR2) { toGen = true; break; }
						}
					}
					if (toGen) { pct = OIL_PCT_REFINED; }
				}
				if (gen != nullptr)
				{
					for (BASE_OBJECT *t : towers)
					{
						if (s_store[t->id] + s_storeR[t->id] > 0 && d2(t->pos.x, t->pos.y, gen->pos.x, gen->pos.y) <= SR2) { pct += OIL_PCT_STORE; break; }
					}
				}
				s_oilPct[der->id] = pct;
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
			if (modcrates::busy(d->id)) { continue; }   // MOD: faehrt gerade Munitionskisten - kein Oel-Autopilot
			if (c.pause > 0) { c.pause--; continue; }

			const bool ownMove = d->order.type == DORDER_MOVE && d->order.pos == Vector2i(c.tx, c.ty);
			// MOD (Wunsch 21.11. 15:40 "warum tut das Kraftwerk nicht?"): Warzone gibt jedem untaetigen Fahrzeug von selbst den Befehl
			// "Stellung halten" (DORDER_GUARD ohne Ziel, Halte-Art "Bewachen"). Der Autopilot hielt das fuer einen Spieler-Befehl - alle
			// Pickups und der Oeltanker warteten fuer immer (Log: "wartend 3"), die Raffinerie blieb voll, das Kraftwerk leer. Jetzt gilt
			// das wie "kein Befehl".
			// (15:46 "die Abholmechanik soll in Dauerschleife sein ... nach kurzer Wartezeit wieder in ihren Arbeitsmodus"): auch
			// "Halten"/"Stopp" eines Spielers ist nach der Wartezeit (5 s Stillstand) vorbei - nur laufende Fahrbefehle warten bis zum Ziel
			const bool idleOrder = d->order.type == DORDER_NONE || d->order.type == DORDER_HOLD || d->order.type == DORDER_STOP
			                       || (d->order.type == DORDER_GUARD && d->order.psObj == nullptr);
			// ein Spieler-Befehl der letzten 5 s (auch "Halten"/"Stopp" waehrend der Fahrt) unterbricht den Autopiloten
			if (c.forced && modcontrol::sincePlayerOrder(d) >= 5000u) { c.forced = false; }
			const bool freshOrder = !c.forced && modcontrol::sincePlayerOrder(d) < 5000u;
			if (c.mode != 0 && ((!ownMove && !idleOrder) || freshOrder))
			{
				c.mode = 0;       // der Spieler hat selbst einen Befehl gegeben: Autopilot wartet, bis der Lkw wieder frei ist
				c.manualWait = true;
				c.idleSec = 0;
				continue;
			}
			if (c.mode == 0 && (!idleOrder || freshOrder)) { c.manualWait = true; c.idleSec = 0; continue; }
			// (Wunsch 18.11. 02:44) nach einem Spieler-Befehl nimmt der Lkw/Tanker seine Arbeit nach kurzer Wartezeit (5 s frei) wieder auf
			if (c.mode == 0 && c.manualWait)
			{
				if (++c.idleSec < 5) { continue; }
				c.manualWait = false;
				c.idleSec = 0;
			}

			// kind: 0 Kraftwerk, 1 Lager, 2 Raffinerie, 3 Oelturm, 4 Tankstelle
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
						s_storeR[c.target] += m;
						c.n -= m;
					}
					else if (c.kind == 2)
					{
						const int m = std::min(c.n, std::max(0, REF_CAP - s_refIn[c.target]));
						s_refIn[c.target] += m;
						c.n -= m;
					}
					else if (c.kind == 4)
					{
						// Tankstelle: 1 veredeltes Fass = 1 Fass Vorrat (= 2 volle Tanks)
						int &st = s_station[c.target];
						const int m = std::min(c.n, std::max(0, (STATION_CAP * 1000 - st) / 1000));
						st += m * 1000;
						c.n -= m;
					}
					else
					{
						// (23.11.) das Kraftwerk verbrennt das Raffinat (sichtbarer Kreislauf); die Energie kommt stetig ueber die Oeltuerme
						(void)ts;
						c.n = 0;
					}
					// (Wunsch 20.11. 19:06) beim Abliefern haelt der Oeltanker kurz, dazu leises Rueckfahr-Piepen wie in der Stadt
					if (c.mode == 2 && c.tank)
					{
						c.pause = 3;
						if (tileSeen(d->pos.x, d->pos.y))
						{
							if (s_beepTrack == -2) { s_beepTrack = audio_GetTrackID("truckbeep.ogg"); }
							if (s_beepTrack >= 0) { audio_PlayStaticTrack(d->pos.x, d->pos.y, s_beepTrack); }
						}
					}
					c.mode = 0;
				}
				else if (idleOrder)
				{
					if (++c.stuck >= 3) { c.mode = 0; c.pause = 10; continue; }
					go(c.mode, c.kind, c.target, wx, wy);
					continue;
				}
				else { continue; }
			}

			if (c.mode != 0) { continue; }

			if (!c.tank)
			{
				// Fracht-Lkw (Rohoel): erst volladen, dann zur naechsten Raffinerie mit Platz. Rohoel geht nirgends sonst hin (20.11.).
				if (cap - c.n > 0)
				{
					STRUCTURE *best = nullptr;
					int64_t bd = INT64_MAX;
					for (STRUCTURE *s : sn.derricks)
					{
						if (avail[s->id] <= 0) { continue; }
						const int64_t dd = d2(d->pos.x, d->pos.y, s->pos.x, s->pos.y);
						if (dd < bd) { bd = dd; best = s; }
					}
					if (best != nullptr)
					{
						avail[best->id] -= (cap - c.n);
						go(1, 3, best->id, best->pos.x, best->pos.y);
						continue;
					}
				}
				if (c.n > 0)
				{
					STRUCTURE *br = nullptr;
					int64_t bd = INT64_MAX;
					for (STRUCTURE *r : sn.refs)
					{
						if (s_refIn[r->id] >= REF_CAP) { continue; }
						const int64_t dd = d2(d->pos.x, d->pos.y, r->pos.x, r->pos.y);
						if (dd < bd) { bd = dd; br = r; }
					}
					if (br != nullptr) { go(2, 2, br->id, br->pos.x, br->pos.y); }
				}
				continue;
			}

			// Oeltanker (veredelt): Raffinerie -> Tankstelle / Lager / Kraftwerk
			if (cap - c.n > 0)
			{
				STRUCTURE *best = nullptr;
				int64_t bd = INT64_MAX;
				for (STRUCTURE *s : sn.refs)
				{
					if (avail[s->id] <= 0) { continue; }
					const int64_t dd = d2(d->pos.x, d->pos.y, s->pos.x, s->pos.y);
					if (dd < bd) { bd = dd; best = s; }
				}
				if (best != nullptr)
				{
					avail[best->id] -= (cap - c.n);
					go(1, 2, best->id, best->pos.x, best->pos.y);
					continue;
				}
			}
			if (c.n <= 0) { continue; }
			// Tankstelle mit wenig Vorrat (unter STATION_LOW)
			{
				STRUCTURE *bs = nullptr;
				int64_t bsd = INT64_MAX;
				for (STRUCTURE *s : sn.stations)
				{
					if (s_station[s->id] >= STATION_LOW * 1000) { continue; }
					const int64_t dd = d2(d->pos.x, d->pos.y, s->pos.x, s->pos.y);
					if (dd < bsd) { bsd = dd; bs = s; }
				}
				if (bs != nullptr) { go(2, 4, bs->id, bs->pos.x, bs->pos.y); continue; }
			}
			// Lager mit Platz in Reichweite eines laufenden Kraftwerks (kleiner Bonus, kein Muss)
			{
				BASE_OBJECT *bt = nullptr;
				int64_t bd = INT64_MAX;
				for (BASE_OBJECT *t : towers)
				{
					if (storeRoom(t->id) <= 0) { continue; }
					bool closeBy = false;
					for (STRUCTURE *g : activeGens)
					{
						if (d2(t->pos.x, t->pos.y, g->pos.x, g->pos.y) <= SR2) { closeBy = true; break; }
					}
					if (!closeBy) { continue; }
					const int64_t dd = d2(d->pos.x, d->pos.y, t->pos.x, t->pos.y);
					if (dd < bd) { bd = dd; bt = t; }
				}
				if (bt != nullptr) { go(2, 1, bt->id, bt->pos.x, bt->pos.y); continue; }
			}
			// naechstes Kraftwerk, das Oel braucht
			{
				STRUCTURE *bg = nullptr;
				int64_t bd = INT64_MAX;
				for (STRUCTURE *g : activeGens)
				{
					const int64_t dd = d2(d->pos.x, d->pos.y, g->pos.x, g->pos.y);
					if (dd < bd) { bd = dd; bg = g; }
				}
				if (bg != nullptr) { go(2, 0, bg->id, bg->pos.x, bg->pos.y); continue; }
			}
			// sonst die Tankstellen ganz auffuellen
			{
				STRUCTURE *bs = nullptr;
				int64_t bsd = INT64_MAX;
				for (STRUCTURE *s : sn.stations)
				{
					if (s_station[s->id] >= STATION_CAP * 1000) { continue; }
					const int64_t dd = d2(d->pos.x, d->pos.y, s->pos.x, s->pos.y);
					if (dd < bsd) { bsd = dd; bs = s; }
				}
				if (bs != nullptr) { go(2, 4, bs->id, bs->pos.x, bs->pos.y); continue; }
			}
		}

		(void)autoTrain;   // (20.11. 19:06) aus: Ersatz fuer zerstoerte Pickups/Tanker kommt sofort und gratis (processTankRespawns)
	}

	// Aufraeumen: Eintraege nicht mehr vorhandener Objekte
	for (auto it = s_oilPct.begin(); it != s_oilPct.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_oilPct.erase(it); }
	for (auto it = s_barrels.begin(); it != s_barrels.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_barrels.erase(it); }
	for (auto it = s_progress.begin(); it != s_progress.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_progress.erase(it); }
	for (auto it = s_refIn.begin(); it != s_refIn.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_refIn.erase(it); }
	for (auto it = s_station.begin(); it != s_station.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_station.erase(it); }
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

	syncDebug("modoil b%zu o%zu c%zu", s_barrels.size(), s_oilPct.size(), s_cargo.size());
}

// ---------------------------------------------------------------- tiefe Graeben: Gelaende absenken

constexpr int TRENCH_DEPTH = 24;   // (17.11. 14:14) = Sohle der tiefen Modelle (trench.py); das Netz darunter senkt modterrain tiefer

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
			// (17.11.) alle Felder unter der Stellung (Feinraster: Block aus 2x2 Feldern)
			constexpr int S = DIST_TILE / TILE_UNITS;
			const int tx0 = map_coord(s->pos.x - (S * TILE_UNITS) / 2), ty0 = map_coord(s->pos.y - (S * TILE_UNITS) / 2);
			for (int j = 0; j < S; ++j)
			{
				for (int i = 0; i < S; ++i) { want.insert(std::make_pair(tx0 + i, ty0 + j)); }
			}
		}
	}
	if (want.empty() && s_lowered.empty()) { return; }
	for (const auto &t : want)
	{
		if (s_lowered.find(t) == s_lowered.end())
		{
			s_lowered[t] = TRENCH_DEPTH;
			modterrain::setStructureSink(t.first, t.second, TRENCH_DEPTH);
		}
	}
	for (auto it = s_lowered.begin(); it != s_lowered.end();)
	{
		if (want.count(it->first) == 0)
		{
			modterrain::setStructureSink(it->first.first, it->first.second, 0);
			it = s_lowered.erase(it);
		}
		else { ++it; }
	}
}

} // namespace

namespace
{
/// Einnahmen-Bericht ins Log (jede Spielminute, fuer menschliche Spieler): woher kommt die Energie?
void ledgerReport()
{
	const UDWORD minute = gameTime / 60000;
	if (minute == s_ledgerMinute) { return; }
	s_ledgerMinute = minute;
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		if (!isHumanPlayer(p)) { continue; }
		const int64_t now = getPower(static_cast<unsigned>(p));
		if (!s_ledgerStarted[p])
		{
			s_ledgerStarted[p] = true;
			s_ledgerPower[p] = now;
			for (int k = 0; k < INC_COUNT; k++) { s_ledger[p][k] = 0; }
			continue;
		}
		const int64_t delta = now - s_ledgerPower[p];
		const int64_t known = s_ledger[p][INC_BARRELS] + s_ledger[p][INC_SOLAR] + s_ledger[p][INC_LAVA] + s_ledger[p][INC_SALVAGE] + s_ledger[p][INC_NUCLEAR];
		int derricks = 0, linked = 0, barrels = 0, gens = 0, fuelled = 0, standby = 0, refs = 0, trucks = 0, tankers = 0, nukes = 0, nukesOn = 0;
		int refIn = 0, refOut = 0, refsDown = 0;   // (19.11.) genauere Angaben zur Fehlersuche
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT) { continue; }
			const STRUCTURE_STATS *st = s->pStructureType;
			if (st->type == REF_RESOURCE_EXTRACTOR)
			{
				derricks++;
				const auto it = s_barrels.find(s->id);
				if (it != s_barrels.end()) { barrels += it->second; }
				if (s->pFunctionality != nullptr && s->pFunctionality->resourceExtractor.psPowerGen != nullptr) { linked++; }
			}
			else if (st->type == REF_POWER_GEN)
			{
				gens++;
				if (generatorRunning(s)) { fuelled++; }
				if (modgrid::generatorStandby(s)) { standby++; }
			}
			else if (isRefinery(st))
			{
				refs++;
				refIn += s_refIn[s->id]; refOut += s_refOut[s->id];
				if (modgrid::isPoweredDown(s)) { refsDown++; }
			}
			else if (modnuke::isPlant(st)) { nukes++; if (modnuke::running(s)) { nukesOn++; } }
		}
		int loaded = 0, driving = 0, waiting = 0;
		for (const auto &kv : s_cargo)
		{
			if (kv.second.player != p) { continue; }
			if (kv.second.tank) { tankers++; } else { trucks++; }
			loaded += kv.second.n;
			if (kv.second.mode != 0) { driving++; }
			else if (kv.second.manualWait) { waiting++; }
		}
		debug(LOG_INFO, "MOD-Wirtschaft Spieler %d: Energie %lld (%+lld/min: Oel %lld, Solar %lld, Lava %lld, Bergung %lld, Atom %lld, sonstige %lld) | Oel-Wirtschaft=%s | Oeltuerme %d (am Kraftwerk %d, Faesser %d) | Kraftwerke %d (mit Oelturm %d, Bereitschaft %d) | Raffinerien %d (ohne Strom %d, Eingang %d, Ausgang %d) | Lkw %d, Tanker %d (unterwegs %d, wartend %d, geladen %d) | Atomkraftwerke %d (laufend %d), Uran %d",
		      p, static_cast<long long>(now), static_cast<long long>(delta), static_cast<long long>(s_ledger[p][INC_BARRELS]), static_cast<long long>(s_ledger[p][INC_SOLAR]),
		      static_cast<long long>(s_ledger[p][INC_LAVA]), static_cast<long long>(s_ledger[p][INC_SALVAGE]), static_cast<long long>(s_ledger[p][INC_NUCLEAR]), static_cast<long long>(delta - known),
		      manages(p) ? "ja" : "NEIN", derricks, linked, barrels, gens, fuelled, standby, refs, refsDown, refIn, refOut, trucks, tankers, driving, waiting, loaded,
		      nukes, nukesOn, modnuke::uranium(p));
		s_lastIncome[p] = known;
		const int64_t waste = getWastedPower(static_cast<unsigned>(p));
		s_lastWaste[p] = std::max<int64_t>(0, waste - s_wasteMark[p]);
		s_wasteMark[p] = waste;
		s_ledgerPower[p] = now;
		for (int k = 0; k < INC_COUNT; k++) { s_ledger[p][k] = 0; }
	}
}

/// Fahrzeug nach Vorlage neben einem Gebaeude aufstellen (wie aus einer Fabrik, aber sofort)
bool spawnNear(const STRUCTURE *s, const char *templateId)
{
	const DROID_TEMPLATE *t = getTemplateFromTranslatedNameNoPlayer(templateId);
	if (t == nullptr)
	{
		debug(LOG_INFO, "MOD-Wirtschaft: Vorlage %s fehlt - kein Fahrzeug fuer %s", templateId, getStatsName(s->pStructureType));
		return false;
	}
	const int b = static_cast<int>(s->pStructureType->baseBreadth);
	UDWORD tx = static_cast<UDWORD>(std::max(0, map_coord(s->pos.x)));
	UDWORD ty = static_cast<UDWORD>(std::max(0, map_coord(s->pos.y) + (b + 1) / 2 + 1));
	if (!pickATileGen(gameWorld, &tx, &ty, LOOK_FOR_EMPTY_TILE, zonedPAT)) { return false; }
	DROID *d = buildDroid(gameWorld, const_cast<DROID_TEMPLATE *>(t), world_coord(static_cast<int>(tx)) + TILE_UNITS / 2,
	                      world_coord(static_cast<int>(ty)) + TILE_UNITS / 2, s->player, false, nullptr);
	if (d == nullptr) { return false; }
	addDroid(d, gameWorld.objects.droids);
	triggerEventDroidBuilt(d, nullptr);
	return true;
}
} // namespace

void onBuilt(STRUCTURE *s)
{
	// (Wunsch 17.11. 20:35) "Oeltanker spawnen beim Bau eines Kraftwerks, und Lkw, die die Oelfaesser zur Raffinerie bringen"
	if (s == nullptr || s->died || s->capacity != 0 || !modPresent() || !manages(static_cast<int>(s->player))) { return; }   // capacity != 0: Modul fertig
	const STRUCTURE_STATS *st = s->pStructureType;
	const int p = static_cast<int>(s->player);
	// (Wunsch 20.11. 17:46 Punkt 4) nur beim ERSTEN Kraftwerk bzw. der ERSTEN Raffinerie der Partie - danach nicht mehr (sonst Abreissen +
	// Neubauen = beliebig viele Gratis-Fahrzeuge)
	if (st->type == REF_POWER_GEN && !(s_gift[p] & 1))
	{
		s_gift[p] |= 1;
		spawnNear(s, "ScavTanker");
		spawnNear(s, "ScavLkw");
	}
	else if (isRefinery(st) && !(s_gift[p] & 2))
	{
		s_gift[p] |= 2;
		spawnNear(s, "ScavLkw");
	}
}

namespace
{
/// (20.11.) Ersatz fuer den letzten zerstoerten Oeltanker / Pickup: an einer Fabrik (sonst HQ, Raffinerie, Kraftwerk), gratis und leer
void processTankRespawns()
{
	if (s_tankRespawn.empty()) { return; }
	std::vector<std::pair<int, bool>> todo;
	todo.swap(s_tankRespawn);
	for (const auto &req : todo)
	{
		const int p = req.first;
		const bool tank = req.second;
		if (p < 0 || p >= MAX_PLAYERS) { continue; }
		bool have = false;
		for (const DROID *d : gameWorld.objects.droids[p]) { if (!d->died && cargoCapacity(d) > 0 && isTank(d) == tank) { have = true; break; } }
		if (have) { continue; }
		const STRUCTURE *at = nullptr;
		int rank = 99;
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || s->pStructureType == nullptr) { continue; }
			const STRUCTURE_TYPE t = s->pStructureType->type;
			const int r = (t == REF_FACTORY && !s->isMobileCore()) ? 0 : t == REF_HQ ? 1 : isRefinery(s->pStructureType) ? 2 : t == REF_POWER_GEN ? 3 : 99;
			if (r < rank) { rank = r; at = s; }
		}
		if (at != nullptr && spawnNear(at, tank ? "ScavTanker" : "ScavLkw"))
		{
			debug(LOG_INFO, "MOD-Wirtschaft: letzter %s von Spieler %d zerstoert - Ersatz an %s", tank ? "Oeltanker" : "Pickup", p, getStatsName(at->pStructureType));
		}
	}
}
}

void tick()
{
	processReplacements();
	processBooms();
	processTankRespawns();
	if (s_have && gameTime >= s_lastSec * 1000 && gameTime / 1000 == s_lastSec) { return; }
	s_have = true;
	s_lastSec = gameTime / 1000;
	if (!modPresent()) { return; }
	logistics();
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		// (23.11.) Restguthaben aus einem Spielstand der alten Fass-Wirtschaft auf einmal gutschreiben
		if (s_bank[p] > 0)
		{
			addPower(p, s_bank[p]);
			noteIncome(p, INC_BARRELS, s_bank[p]);
			s_bank[p] = 0;
		}
		// stetige Oel-Einnahmen aus power.cpp (Festkomma) ins Einnahmen-Buch
		const int64_t whole = s_oilFP[p] >> 32;
		if (whole > 0)
		{
			s_oilFP[p] -= whole << 32;
			noteIncome(p, INC_BARRELS, static_cast<int>(std::min<int64_t>(whole, INT_MAX)));
		}
	}
	ledgerReport();
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

// (16.11.) Fenster ueber der gewaehlten Tankstelle: so viele Oelfaesser, wie gerade darin sind
static void drawStationPanels(const glm::mat4 &m)
{
	const int p = static_cast<int>(selectedPlayer);
	if (p < 0 || p >= MAX_PLAYERS) { return; }
	for (const STRUCTURE *st : gameWorld.objects.structures[p])
	{
		if (st == nullptr || st->died || !st->selected || !modfuel::isStation(st)) { continue; }
		Vector2i sc(0, 0);
		if (!project(Vector3i(st->pos.x, st->pos.y, st->pos.z + 150), m, sc)) { continue; }
		const int n = std::clamp(stationBarrels(st), 0, STATION_CAP);
		const int milli = stationMilli(st);
		constexpr int bw = 12, bh = 16, gap = 4, pad = 6;
		const int cols = 5, rows = 2;
		const int pw = pad * 2 + cols * bw + (cols - 1) * gap, ph = pad * 2 + rows * bh + (rows - 1) * gap + 8;
		const int x0 = sc.x - pw / 2, y0 = sc.y - ph;
		pie_BoxFill(x0 - 2, y0 - 2, x0 + pw + 2, y0 + ph + 2, pal_RGBA(140, 150, 170, 255));
		pie_BoxFill(x0, y0, x0 + pw, y0 + ph, pal_RGBA(16, 22, 38, 235));
		for (int i = 0; i < STATION_CAP; ++i)
		{
			const int cx = x0 + pad + (i % cols) * (bw + gap), cy = y0 + pad + (i / cols) * (bh + gap);
			if (i < n)
			{
				// Oelfass: Koerper, zwei Ringe, heller Deckel
				pie_BoxFill(cx, cy + 2, cx + bw, cy + bh, pal_RGBA(40, 90, 200, 255));
				pie_BoxFill(cx + 2, cy + 2, cx + 4, cy + bh, pal_RGBA(90, 150, 255, 255));
				pie_BoxFill(cx, cy + 5, cx + bw, cy + 6, pal_RGBA(15, 30, 70, 255));
				pie_BoxFill(cx, cy + bh - 5, cx + bw, cy + bh - 4, pal_RGBA(15, 30, 70, 255));
				pie_BoxFill(cx + 1, cy, cx + bw - 1, cy + 3, pal_RGBA(150, 190, 255, 255));
			}
			else
			{
				pie_BoxFill(cx, cy + 2, cx + bw, cy + bh, pal_RGBA(40, 44, 56, 255));
			}
		}
		// Fuellstand als duenner Balken darunter (mit Abschnitten je Fass)
		const int by = y0 + ph - 7, bx0 = x0 + pad, bx1 = x0 + pw - pad;
		pie_BoxFill(bx0 - 1, by - 1, bx1 + 1, by + 4, pal_RGBA(0, 0, 0, 255));
		const int fill = (bx1 - bx0) * std::clamp(milli, 0, STATION_CAP * 1000) / (STATION_CAP * 1000);
		if (fill > 0) { pie_BoxFill(bx0, by, bx0 + fill, by + 3, pal_RGBA(70, 150, 255, 255)); }
		for (int k = 1; k < STATION_CAP; ++k)
		{
			const int sx = bx0 + (bx1 - bx0) * k / STATION_CAP;
			pie_BoxFill(sx, by, sx + 1, by + 3, pal_RGBA(0, 0, 0, 255));
		}
	}
}

void drawOverlay(const glm::mat4 &m)
{
	drawStationPanels(m);
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
			bar(m, s->pos.x, s->pos.y, s->pos.z + 200, it == s_barrels.end() ? 0 : it->second, DERRICK_CAP, barrelOn, barrelOff);   // 20 Plaetze: Balken
		}
		else if (isRefinery(s->pStructureType))
		{
			const auto ri = s_refIn.find(s->id);
			const auto ro = s_refOut.find(s->id);
			squares(m, s->pos.x, s->pos.y, s->pos.z + 300, ri == s_refIn.end() ? 0 : ri->second, REF_CAP, barrelOn, barrelOff);
			squares(m, s->pos.x, s->pos.y, s->pos.z + 330, ro == s_refOut.end() ? 0 : ro->second, REF_CAP, refinedOn, storeOff);
		}
		else if (isOilStore(s->pStructureType))
		{
			const auto it = s_store.find(s->id);
			const auto ir = s_storeR.find(s->id);
			const int have = (it == s_store.end() ? 0 : it->second) + (ir == s_storeR.end() ? 0 : ir->second);
			const int cap = storeCapOf(s->pStructureType);
			if (cap > STORAGE_CAP) { squares(m, s->pos.x, s->pos.y, s->pos.z + 200, (have + 3) / 4, cap / 4, storeOn, storeOff); }   // je Feld 4 Faesser
			else { squares(m, s->pos.x, s->pos.y, s->pos.z + 330, have, STORAGE_CAP, storeOn, storeOff); }
		}
		else if (s->pStructureType->type == REF_POWER_GEN)
		{
			gens.push_back(s);
			// (23.11.) angeschlossene Oeltuerme (je Kraftwerk 4, wie in WZ) statt Brennstoff-Vorrat
			const int n = generatorDerricks(s);
			squares(m, s->pos.x, s->pos.y, s->pos.z + 230, n, NUM_POWER_MODULES, n > 0 ? okOn : lowOn, barOff);
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
