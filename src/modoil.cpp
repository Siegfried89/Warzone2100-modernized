// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Oelkreislauf, siehe modoil.h

#include "modoil.h"
#include "modshockwave.h"
#include "modcontrol.h"   // (21.11.) Spieler-Befehl unterbricht den Autopiloten kurz
#include "action.h"
#include "modterrain.h"
#include "modfort.h"

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
constexpr int DERRICK_SECS = 180;           // jeder Oelturm foerdert gleich: 1 Fass alle 3 Minuten, ohne Kraftwerk-Verknuepfung
constexpr int GEN_ENERGY = 100;             // Energie je Fass im Kraftwerk ohne Modul (Modul +50 %, Forschung wie WZ) = WZ-Foerderung
constexpr int GEN_REF_POINTS = 55;          // Bezug: Kraftwerk ohne Modul hat 55 Leistungspunkte (WZ-Wert)
constexpr int STORE_BONUS = 10;             // Fass ueber das Lager: +10 % Energie
constexpr int GEN_SECS = 180;               // ein Fass haelt das Kraftwerk / Stromnetz 3 Minuten am Laufen
constexpr int START_FUEL = 0;               // (20.11.) neues Kraftwerk startet leer
constexpr int PULL_BELOW = 60;              // das Kraftwerk holt ein Fass aus dem Lager, wenn der Vorrat darunter faellt
constexpr int HUNGRY_BELOW = 600;           // (Wunsch 24.11. 17:46 "das Kraftwerk darf keine einzige Sekunde stillstehen und muss VOR Ablauf beliefert
                                            // werden") Tanker beliefern ein Kraftwerk mit hoechstens 10 Minuten Vorrat zuerst (vorher 3)
constexpr int GEN_BARREL_CAP = 500;         // (17:46 "muss viele viele Faesser Lagerplatz haben ... einige hundert Faesser anstauen") Faesser im Kraftwerk
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
	int content = 0;            // (Wunsch 24.11. 17:01, nur noch Tanker) Ladung: 0 leer, 1 Rohoel (Oelturm -> Raffinerie), 2 veredelt
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
std::map<uint32_t, int> s_fuel;             // Kraftwerk -> Sekunden
std::map<uint32_t, int> s_store;            // Wasserturm (Feature) -> rohe Faesser
std::map<uint32_t, int> s_storeR;           // Wasserturm (Feature) -> veredelte Faesser
std::map<uint32_t, int> s_refIn, s_refOut, s_refT;   // Raffinerie: Eingang, Ausgang, Sekunden im laufenden Fass
std::map<uint32_t, CargoState> s_cargo;     // Lkw -> Zustand
std::map<uint32_t, int> s_station;           // (16.11.) Tankstelle -> Vorrat in Promille-Faessern (1000 = 1 Fass)
std::map<std::pair<int, int>, int> s_lowered;   // tiefe Graeben: abgesenkte Kachel -> Tiefe (modterrain)
std::vector<Boom> s_booms;
std::vector<Replace> s_repl;
bool s_startChecked[MAX_PLAYERS] = {};
int s_freeTankers[MAX_PLAYERS] = {};        // (24.11. 17:46) schon gratis vergebene Tanker (Fabrik-Tanker zaehlen nicht)       // (24.11. 17:01) Tanker fuer schon stehende Oeltuerme/Raffinerien vergeben
int s_gift[MAX_PLAYERS] = {};               // (20.11. Punkt 4) Bit 1 = Kraftwerk-Geschenk, Bit 2 = Raffinerie-Geschenk schon vergeben
int s_bank[MAX_PLAYERS] = {};               // (Wunsch 21.11. 20:22) Energie verbrannter Faesser, die noch stetig ins Konto laeuft
int s_payRate[MAX_PLAYERS] = {};            // (Wunsch 23.11. 19:29 "gleichmaessig statt schubweise") feste Auszahlung je Sekunde bis zur naechsten Lieferung
// (23.11. 20:22) abgebrochene Rohrstuecke, die spaeter wieder beauftragt werden
struct PipeRetry
{
	int player = 0;
	uint32_t droid = 0;
	Vector2i pos{0, 0};
	uint16_t dir = 0;
	UDWORD until = 0;
	const STRUCTURE_STATS *stats = nullptr;
};
std::vector<PipeRetry> s_pipeRetry;
constexpr UDWORD PIPE_RETRY_MS = 10 * 60 * 1000;
std::map<uint32_t, uint32_t> s_pipeAssign;  // (23.11. 19:57) Oelturm -> Kraftwerk (per Pipeline, stetige Einnahmen)
std::map<uint32_t, int> s_genPiped;         // Kraftwerk -> Zahl der per Pipeline zugeteilten Oeltuerme
int64_t s_oilFP[MAX_PLAYERS] = {};          // Pipeline-Einnahmen aus power.cpp (Festkomma 32.32) fuer das Einnahmen-Buch
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
	// (Wunsch 24.11. 17:01 "Fasstransporter-Trucks GANZ entfernen ... Den Oeltransport von jeder Stelle zu jeder Stelle uebernehmen ab jetzt
	// NUR noch die Oeltanker"): Ladeflaechen (Pickup, Pritsche, Schaufel) fahren kein Oel mehr (Munitionskisten weiterhin, modcrates)
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
	modshockwave::bigBoomSound(x, y, m, false);   // MOD (27.11. 16:43): gewaltiger, geschichteter Klang statt eines einzelnen Knalls
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


// (23.11. 19:57) Rohr-Raster: ein Rohrstueck belegt PS x PS Felder (Viertel-Version: 2 x 2 Feinfelder), sein Schluesselfeld ist das Feld
// unter seiner Mitte. Zwei Rohre gehoeren zusammen, wenn sie sich mit einer Kante beruehren (Abstand genau PS in einer Achse, weniger in
// der anderen). Ein Gebaeude haengt an einem Netz, wenn ein Rohr um hoechstens PS Felder neben seiner Grundflaeche liegt.
constexpr int PS = (DIST_TILE / TILE_UNITS) > 0 ? (DIST_TILE / TILE_UNITS) : 1;

struct PipeGrid
{
	std::map<std::pair<int, int>, int> idx;
	std::vector<int> par;

	void build(const std::vector<STRUCTURE *> &pipes)
	{
		idx.clear(); par.clear();
		for (const STRUCTURE *s : pipes)
		{
			const std::pair<int, int> t(map_coord(s->pos.x), map_coord(s->pos.y));
			if (idx.find(t) == idx.end()) { idx[t] = static_cast<int>(par.size()); par.push_back(static_cast<int>(par.size())); }
		}
		for (const auto &kv : idx)
		{
			for (int ddx = -PS; ddx <= PS; ddx++)
			{
				for (int ddy = -PS; ddy <= PS; ddy++)
				{
					if (std::max(std::abs(ddx), std::abs(ddy)) != PS || std::min(std::abs(ddx), std::abs(ddy)) >= PS) { continue; }
					const auto r = idx.find(std::make_pair(kv.first.first + ddx, kv.first.second + ddy));
					if (r != idx.end()) { uunite(par, kv.second, r->second); }
				}
			}
		}
	}
	std::set<int> touching(const STRUCTURE *s)
	{
		std::set<int> roots;
		if (idx.empty() || s == nullptr) { return roots; }
		const StructureBounds b = getStructureBounds(s);
		for (int x = b.map.x - PS; x <= b.map.x + b.size.x - 1 + PS; x++)
		{
			for (int y = b.map.y - PS; y <= b.map.y + b.size.y - 1 + PS; y++)
			{
				const auto it = idx.find(std::make_pair(x, y));
				if (it != idx.end()) { roots.insert(ufind(par, it->second)); }
			}
		}
		return roots;
	}
};

bool sharesRoot(const std::set<int> &a, const std::set<int> &b)
{
	for (int v : a) { if (b.count(v) != 0) { return true; } }
	return false;
}

/// (23.11. 20:22) Bautrupp fuer Rohre: echter Bau-Lkw / Bau-Cyborg, kein Fracht-Lkw, kein Bohr-/Abrissfahrzeug
bool pipeBuilder(const DROID *d)
{
	if (d == nullptr || d->died || (d->droidType != DROID_CONSTRUCT && d->droidType != DROID_CYBORG_CONSTRUCT) || cargoCapacity(d) > 0) { return false; }
	const CONSTRUCT_STATS *cs = d->getConstructStats();
	if (cs == nullptr) { return false; }
	const std::string id = cs->id.toUtf8();
	return id != "DrillRig" && id != "WreckBall" && id.find("Bucket") == std::string::npos && id.find("ScavBed") == std::string::npos && id != "ScavTank";
}

bool droidIdle(const DROID *d)
{
	return d->order.type == DORDER_NONE || d->order.type == DORDER_HOLD || d->order.type == DORDER_STOP
	       || (d->order.type == DORDER_GUARD && d->order.psObj == nullptr);
}

/// (Wunsch 23.11. 20:22) blockierte Rohrstuecke erneut beauftragen: steht dort inzwischen ein Rohr (fertig oder im Bau) -> erledigt;
/// ist der Platz wieder frei, bekommt der urspruengliche Bautrupp (oder der naechste freie) den Auftrag, sobald er frei ist
void processPipeRetries()
{
	for (size_t i = 0; i < s_pipeRetry.size();)
	{
		PipeRetry &r = s_pipeRetry[i];
		bool drop = gameTime > r.until || r.stats == nullptr || r.player < 0 || r.player >= MAX_PLAYERS || !manages(r.player);
		if (!drop && apStructTypeLists[r.player][r.stats->index] != AVAILABLE) { drop = true; }   // nicht (mehr) baubar
		if (!drop)
		{
			const MAPTILE *t = mapTile(gameWorld.map, map_coord(r.pos.x), map_coord(r.pos.y));
			if (t != nullptr && TileHasStructure(t) && t->psObject != nullptr && isPipe(static_cast<const STRUCTURE *>(t->psObject)->pStructureType)) { drop = true; }
		}
		if (!drop && validLocation(gameWorld, const_cast<STRUCTURE_STATS *>(r.stats), r.pos, r.dir, static_cast<unsigned>(r.player), false))
		{
			DROID *d = nullptr;
			for (DROID *c : gameWorld.objects.droids[r.player]) { if (c != nullptr && c->id == r.droid && pipeBuilder(c)) { d = c; break; } }
			if (d == nullptr)
			{
				int64_t bd = INT64_MAX;
				for (DROID *c : gameWorld.objects.droids[r.player])
				{
					if (!pipeBuilder(c) || !droidIdle(c)) { continue; }
					const int64_t dd = d2(c->pos.x, c->pos.y, r.pos.x, r.pos.y);
					if (dd < bd) { bd = dd; d = c; }
				}
			}
			if (d != nullptr && droidIdle(d))
			{
				orderDroidStatsLocDir(d, DORDER_BUILD, const_cast<STRUCTURE_STATS *>(r.stats), static_cast<UDWORD>(r.pos.x), static_cast<UDWORD>(r.pos.y), r.dir, ModeImmediate);
				r.droid = d->id;
			}
		}
		if (drop) { s_pipeRetry.erase(s_pipeRetry.begin() + static_cast<long>(i)); }
		else { ++i; }
	}
}
} // namespace

// ---------------------------------------------------------------- oeffentliche Funktionen

int pipedDerricks(const STRUCTURE *g)
{
	if (g == nullptr) { return 0; }
	const auto it = s_genPiped.find(g->id);
	return it == s_genPiped.end() ? 0 : it->second;
}

bool derrickPiped(const STRUCTURE *d)
{
	return d != nullptr && s_pipeAssign.count(d->id) != 0;
}

void noteOilIncomeFP(int player, int64_t amountFP)
{
	if (player >= 0 && player < MAX_PLAYERS && amountFP > 0) { s_oilFP[player] += amountFP; }
}

bool isPipeStructure(const STRUCTURE *s)
{
	return s != nullptr && !s->died && s->status == SS_BUILT && isPipe(s->pStructureType) && !isBroken(s->pStructureType);
}

Vector2i pipeKey(const STRUCTURE *s)
{
	return Vector2i(map_coord(s->pos.x), map_coord(s->pos.y));
}

int pipeStep()
{
	return PS;
}

int pipeKeyToWorld(int key)
{
	// Mitte eines PS x PS grossen Rohrstuecks, dessen Mitte ueber dem Schluesselfeld liegt (PS = 1: Feldmitte, PS = 2: Feldecke)
	return world_coord(key) + ((PS % 2) != 0 ? TILE_UNITS / 2 : 0);
}

bool pipeTouches(const STRUCTURE *s, Vector2i k)
{
	if (s == nullptr) { return false; }
	const StructureBounds b = getStructureBounds(s);
	return k.x >= b.map.x - PS && k.x <= b.map.x + b.size.x - 1 + PS && k.y >= b.map.y - PS && k.y <= b.map.y + b.size.y - 1 + PS;
}

void notePipeBuildCancelled(const DROID *d, const STRUCTURE_STATS *st, Vector2i pos, uint16_t dir)
{
	if (d == nullptr || st == nullptr || !isPipe(st) || isBroken(st) || !manages(static_cast<int>(d->player))) { return; }
	for (PipeRetry &r : s_pipeRetry)
	{
		if (r.player == static_cast<int>(d->player) && r.pos == pos) { r.droid = d->id; return; }   // schon gemerkt: Frist bleibt
	}
	PipeRetry r;
	r.player = static_cast<int>(d->player); r.droid = d->id; r.pos = pos; r.dir = dir; r.until = gameTime + PIPE_RETRY_MS; r.stats = st;
	s_pipeRetry.push_back(r);
	objTrace(d->id, "MOD: Rohrstueck blockiert - wird spaeter wieder beauftragt");
}

bool generatorHungry(const STRUCTURE *g)
{
	return g != nullptr && manages(static_cast<int>(g->player)) && g->status == SS_BUILT && !modgrid::generatorStandby(g) && pipedDerricks(g) == 0
	       && generatorFuel(g) <= HUNGRY_BELOW;
}

EconomyView economyView(int p)
{
	EconomyView v;
	if (p < 0 || p >= MAX_PLAYERS || !manages(p)) { return v; }
	v.active = true;
	std::vector<const STRUCTURE *> pipes;
	int64_t pipeRateNum = 0;
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
			// (23.11. 19:57) Oeltuerme per Pipeline und ihr stetiger Ertrag: je Oelturm 1/s x Energie-Einstellung x Kraftwerk-Punkte / 100
			const int pip = pipedDerricks(s);
			v.pipedDerricks += pip;
			v.pipeSlots += PIPE_PER_GEN;
			pipeRateNum += static_cast<int64_t>(60) * getPowerModifier(p) * std::max(1, getBuildingPowerPoints(s)) * pip;
			const int f = generatorFuel(s);
			if (generatorRunning(s)) { v.gensRunning++; }
			if (modgrid::generatorStandby(s)) { v.gensStandby++; }
			else if (pip == 0)   // (23.11.) per Pipeline versorgt: braucht keine Faesser
			{
				if (f <= HUNGRY_BELOW) { v.gensHungry++; }
				v.minFuel = v.minFuel < 0 ? f : std::min(v.minFuel, f);
			}
		}
		else if (modfuel::isStation(s))
		{
			v.stations++; v.stationBarrels += stationBarrels(s); v.stationCap += STATION_CAP;
		}
		else if (isPipe(st) && !isBroken(st)) { pipes.push_back(s); }
		if (modgrid::isPoweredDown(s))
		{
			if (modgrid::isCrewShort(s)) { v.crewDown++; }   // (27.11.) hat Strom, aber keine Besatzung
			else { v.buildingsDown++; }
		}
	}
	{
		int off = 0;
		v.crewActive = modgrid::crewAccount(p, v.crewUsed, v.crewSupply, off);
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
		// (24.11. 17:01) nur noch Tanker: im Fenster zaehlen sie beim Transport (Rohoel) und beim Oeltanker (veredelt)
		const bool raw = it != s_cargo.end() && it->second.content == 1;
		v.tankers++; v.tankerCap += cap; v.pickups++; v.pickupCap += cap;
		if (raw) { v.pickupBarrels += n; } else { v.tankerBarrels += n; }
	}
	v.pipeRatePerMin = static_cast<int>(pipeRateNum * INCOME_MULT / 10000);   // (24.11.) x3
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

int generatorStock(const STRUCTURE *g, int &cap)
{
	cap = 0;
	if (g == nullptr || g->pStructureType == nullptr || g->pStructureType->type != REF_POWER_GEN || g->status != SS_BUILT || !manages(static_cast<int>(g->player))) { return -1; }
	cap = GEN_BARREL_CAP;
	const auto it = s_fuel.find(g->id);
	const int fuel = it == s_fuel.end() ? 0 : it->second;
	return (fuel + GEN_SECS - 1) / GEN_SECS;
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
	for (bool &b : s_startChecked) { b = false; }
	for (int &n : s_freeTankers) { n = 0; }
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (int k = 0; k < INC_COUNT; k++) { s_ledger[p][k] = 0; }
		s_ledgerPower[p] = 0;
		s_ledgerStarted[p] = false;
	}
	s_ledgerMinute = 0;
	s_barrels.clear();
	s_progress.clear();
	s_fuel.clear();
	s_pipeAssign.clear();
	s_genPiped.clear();
	s_pipeRetry.clear();
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
	for (int p = 0; p < MAX_PLAYERS; p++) { s_gift[p] = 0; s_bank[p] = 0; s_payRate[p] = 0; s_oilFP[p] = 0; s_lastIncome[p] = 0; s_lastWaste[p] = 0; s_wasteMark[p] = 0; }
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
	saveMap(out, 3, s_fuel);
	saveMap(out, 4, s_store);
	saveMap(out, 5, s_storeR);
	saveMap(out, 6, s_refIn);
	saveMap(out, 7, s_refOut);
	saveMap(out, 8, s_refT);
	saveMap(out, 9, s_station);
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		if (s_gift[p] != 0) { out.push_back(10); out.push_back(p); out.push_back(s_gift[p]); }
		if (s_bank[p] > 0) { out.push_back(12); out.push_back(p); out.push_back(s_bank[p]); }
	}
	for (int p = 0; p < MAX_PLAYERS; p++) { if (s_freeTankers[p] > 0) { out.push_back(14); out.push_back(p); out.push_back(s_freeTankers[p]); } }
	// (24.11. 17:01) geladene Faesser der Tanker (vorher gingen sie beim Speichern verloren): Wert = Anzahl + Inhalt * 65536
	for (const auto &kv : s_cargo)
	{
		if (kv.second.n > 0) { out.push_back(13); out.push_back(static_cast<int64_t>(kv.first)); out.push_back(kv.second.n + kv.second.content * 65536); }
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
		case 3: s_fuel[id] = iv; break;
		case 4: s_storeR[id] += iv; break;   // (20.11.) Lager hat nur noch veredelte Faesser
		case 10: if (id < MAX_PLAYERS) { s_gift[id] = iv; } break;
		case 11: break;   // (26.11.) frueher Strompflicht-Merker (nie ausgewertet, entfernt) - in alten Spielstaenden ueberspringen
		case 12: if (id < MAX_PLAYERS) { s_bank[id] = iv; } break;
		case 14: if (id < MAX_PLAYERS) { s_freeTankers[id] = iv; } break;
		case 13: { CargoState &c = s_cargo[id]; c.n = iv & 0xffff; c.content = (iv >> 16) & 3; if (c.content == 0) { c.content = 2; } } break;
		case 5: s_storeR[id] += iv; break;
		case 6: s_refIn[id] = iv; break;
		case 7: s_refOut[id] = iv; break;
		case 8: s_refT[id] = iv; break;
		case 9: s_station[id] = iv; break;
		default: break;
		}
	}
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

int generatorFuel(const STRUCTURE *g)
{
	const auto it = s_fuel.find(g->id);
	return it == s_fuel.end() ? START_FUEL : it->second;
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
	// (24.11. 17:01) der Tanker faehrt alles: Rohoel am Oelturm, Raffinerie (Rohoel abgeben / veredeltes laden), Kraftwerk, Tankstelle, Lager
	return st->type == REF_RESOURCE_EXTRACTOR || isRefinery(st) || st->type == REF_POWER_GEN || modfuel::isStation(s) || isOilStore(st);
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
	if (c.n <= 0) { c.content = 0; }
	const bool raw = c.n > 0 && c.content == 1, refined = c.n > 0 && c.content != 1;
	if (st->type == REF_RESOURCE_EXTRACTOR) { if (!refined && c.n < c.cap) { mode = 1; kind = 3; } }   // Rohoel am Oelturm laden
	else if (isRefinery(st)) { if (raw) { mode = 2; kind = 2; } else if (c.n < c.cap) { mode = 1; kind = 2; } }   // Rohoel abgeben / Veredeltes laden
	else if (st->type == REF_POWER_GEN) { if (refined) { mode = 2; kind = 0; } }
	else if (modfuel::isStation(tgt)) { if (refined) { mode = 2; kind = 4; } }
	else if (isOilStore(st)) { if (refined) { mode = 2; kind = 1; } }
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
	const int barrels = (it != s_cargo.end() && !it->second.tank) ? std::max(0, it->second.n) : 0;   // (24.11.) immer 0: nur Tanker fahren Oel
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
	if (pipedDerricks(psGen) > 0) { return true; }   // (23.11.) Oelturm per Pipeline: laeuft stetig
	const auto it = s_fuel.find(psGen->id);
	return it == s_fuel.end() ? START_FUEL > 0 : it->second > 0;   // (20.11.) neues Kraftwerk ist leer
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
		bool other = false;
		for (const DROID *d : gameWorld.objects.droids[psDel->player])
		{
			if (d != psDel && !d->died && cargoCapacity(d) > 0) { other = true; break; }
		}
		if (!other) { s_tankRespawn.emplace_back(static_cast<int>(psDel->player), true); }   // (24.11.) nur noch Tanker
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

/// (20.11.) Energie je Fass: 100 beim Kraftwerk ohne Modul; Modul und Kraftwerk-Forschung wie in WZ (Leistungspunkte / 55),
/// Energie-Einstellung der Partie (niedrig/normal/hoch), +10 % wenn das Fass aus dem Lager kommt
int barrelEnergy(const STRUCTURE *gen, bool fromStore)
{
	int64_t e = static_cast<int64_t>(GEN_ENERGY) * std::max(1, getBuildingPowerPoints(gen)) / GEN_REF_POINTS;
	e = e * getPowerModifier(static_cast<int>(gen->player)) / 100;
	if (fromStore) { e = e * (100 + STORE_BONUS) / 100; }
	e *= INCOME_MULT;   // (24.11. 15:19) alle Energieeinnahmen x3
	return static_cast<int>(std::max<int64_t>(1, e));
}

int genRoom(const STRUCTURE *gen)
{
	const auto it = s_fuel.find(gen->id);
	const int fuel = it == s_fuel.end() ? 0 : it->second;
	return std::max(0, GEN_BARREL_CAP - (fuel + GEN_SECS - 1) / GEN_SECS);
}

void deliverToGenerator(int player, STRUCTURE *gen, int barrels, bool fromStore)
{
	const int e = barrelEnergy(gen, fromStore);
	for (int i = 0; i < barrels; i++)
	{
		// (Wunsch 21.11. 20:22 "mein Power supply erhoeht sich nicht (wie im WZ vanilla)"): nicht auf einen Schlag, sondern stetig (payout)
		if (player >= 0 && player < MAX_PLAYERS) { s_bank[player] += e; }
		s_fuel[gen->id] += GEN_SECS;
	}
	if (barrels > 0 && player >= 0 && player < MAX_PLAYERS)
	{
		s_payRate[player] = std::max(1, (s_bank[player] + GEN_SECS - 1) / GEN_SECS);
	}
}

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
	s_pipeAssign.clear();   // (23.11. 19:57) Pipeline-Zuteilung wird jede Sekunde neu bestimmt
	s_genPiped.clear();
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

		// (Wunsch 23.11. 19:57 "Ein Oilrig muss per Pipeline angeschlossen sein, damit er staendig fliesst ... Falls das naechste Kraftwerk
		// voll waere, wuerde ein bestehender Oilrig zu einem passenderen wechseln, damit die Pipelines kurz bleiben"): Rohrnetz einmal je
		// Sekunde. Jeder Oelturm, der an einem Netz mit Kraftwerk haengt, wird dem naechsten Kraftwerk dieses Netzes mit freiem Platz
		// zugeteilt (je Kraftwerk PIPE_PER_GEN; kuerzeste Wege zuerst - kommt ein naeherer Oelturm dazu, rueckt ein weiter entfernter zum
		// naechstbesten Kraftwerk im Netz). Zugeteilte Oeltuerme liefern stetig (power.cpp) und stellen keine Faesser mehr ab.
		PipeGrid grid;
		grid.build(sn.pipes);
		std::map<uint32_t, std::set<int>> derRoots;
		{
			struct Pair { int64_t dd; uint32_t der, gen; };
			std::vector<Pair> pairs;
			std::vector<std::set<int>> genRoots;
			for (STRUCTURE *g : sn.gens) { genRoots.push_back(grid.touching(g)); }
			for (STRUCTURE *der : sn.derricks)
			{
				std::set<int> rd = grid.touching(der);
				if (rd.empty()) { continue; }
				for (size_t k = 0; k < sn.gens.size(); k++)
				{
					if (sharesRoot(rd, genRoots[k])) { pairs.push_back({d2(der->pos.x, der->pos.y, sn.gens[k]->pos.x, sn.gens[k]->pos.y), der->id, sn.gens[k]->id}); }
				}
				derRoots[der->id] = std::move(rd);
			}
			std::sort(pairs.begin(), pairs.end(), [](const Pair &a, const Pair &b) { return a.dd != b.dd ? a.dd < b.dd : (a.der != b.der ? a.der < b.der : a.gen < b.gen); });
			for (const Pair &pr : pairs)
			{
				if (s_pipeAssign.count(pr.der) != 0) { continue; }
				int &n = s_genPiped[pr.gen];
				if (n >= PIPE_PER_GEN) { continue; }
				n++;
				s_pipeAssign[pr.der] = pr.gen;
			}
		}

		// (Wunsch 20.11. 17:46 "Der Oelturm soll sofort anfangen zu foerdern ... ein Bohrturm soll immer gleich foerdern"): jeder fertige
		// Oelturm foerdert 1 Fass alle DERRICK_SECS Sekunden - ohne Kraftwerk, ohne Unterschiede - bis DERRICK_CAP Faesser daliegen
		for (STRUCTURE *der : sn.derricks)
		{
			int &b = s_barrels[der->id];
			// (Wunsch 23.11. 20:20 "es soll sofort das erste Fass da stehen und dann sofort mit dem Countdown fuer das naechste beginnen"):
			// ein neuer Oelturm stellt beim ersten Durchlauf sofort ein Fass ab, der Fortschritt zum naechsten beginnt bei 0
			const auto fresh = s_progress.try_emplace(der->id, static_cast<int64_t>(0));
			if (fresh.second && b < DERRICK_CAP) { b++; }
			if (s_pipeAssign.count(der->id) != 0) { continue; }   // (23.11. 19:57) per Pipeline am Kraftwerk: Oel fliesst direkt, keine Faesser
			int64_t &prog = fresh.first->second;
			if (prog < 0 || prog > DERRICK_SECS) { prog = DERRICK_SECS; }   // aeltere Spielstaende (Festkomma-Fortschritt)
			if (b >= DERRICK_CAP) { continue; }   // voll: es wird nichts mehr abgestellt, bis jemand abholt
			if (++prog >= DERRICK_SECS) { prog = 0; b++; }
		}

		// Kraftwerke: Brennstoff (in Bereitschaft - der Solarstrom reicht - verbraucht ein Kraftwerk nichts)
		for (STRUCTURE *g : sn.gens)
		{
			auto it = s_fuel.find(g->id);
			if (it == s_fuel.end()) { s_fuel[g->id] = START_FUEL; }
			else if (it->second > 0 && !modgrid::generatorStandby(g)) { it->second--; }
		}
		// (20:35) nur Kraftwerke, die Oel brauchen, bekommen welches; die anderen Faesser gehen an die Tankstellen
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

		// Kraftwerk holt ein Fass aus dem naechsten Lager (bis 12 Felder), wenn sein Vorrat unter 1 Minute faellt (+10 % Energie)
		for (STRUCTURE *g : activeGens)
		{
			if (s_fuel[g->id] > PULL_BELOW) { continue; }
			BASE_OBJECT *best = nullptr;
			int64_t bd = INT64_MAX;
			for (BASE_OBJECT *t : towers)
			{
				if (s_store[t->id] + s_storeR[t->id] <= 0) { continue; }
				const int64_t dd = d2(t->pos.x, t->pos.y, g->pos.x, g->pos.y);
				if (dd > SR2) { continue; }
				if (dd < bd) { bd = dd; best = t; }
			}
			if (best != nullptr)
			{
				if (s_storeR[best->id] > 0) { s_storeR[best->id]--; } else { s_store[best->id]--; }
				deliverToGenerator(p, g, 1, true);
			}
		}

		// Pipeline-Netz: Oeltuerme -> Raffinerie (20.11.), ein Fass je Sekunde und Turm - fuer Oeltuerme, die nicht per Pipeline an einem
		// Kraftwerk haengen (deren Oel fliesst direkt ins Kraftwerk)
		if (!sn.pipes.empty() && !sn.refs.empty())
		{
			std::vector<std::set<int>> refRoots;
			for (STRUCTURE *r : sn.refs) { refRoots.push_back(grid.touching(r)); }
			for (STRUCTURE *der : sn.derricks)
			{
				int &barrels = s_barrels[der->id];
				if (barrels <= 0) { continue; }
				const auto ri = derRoots.find(der->id);
				if (ri == derRoots.end()) { continue; }
				STRUCTURE *br = nullptr;
				int64_t bd = INT64_MAX;
				for (size_t k = 0; k < sn.refs.size(); k++)
				{
					STRUCTURE *r = sn.refs[k];
					if (s_refIn[r->id] >= REF_CAP) { continue; }
					if (!sharesRoot(ri->second, refRoots[k])) { continue; }
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
		// (24.11. 17:01) Rohoel, das schon zur Raffinerie unterwegs ist (Platz im Eingang nicht doppelt vergeben)
		std::map<uint32_t, int> refIncoming;
		for (const auto &kv : s_cargo)
		{
			if (kv.second.player == p && kv.second.mode == 2 && kv.second.kind == 2) { refIncoming[kv.second.target] += kv.second.n; }
		}
		auto refRoom = [&](const STRUCTURE *r) { return REF_CAP - s_refIn[r->id] - refIncoming[r->id]; };
		int refRoomTotal = 0;
		for (STRUCTURE *r : sn.refs) { refRoomTotal += std::max(0, refRoom(r)); }
		// "hungriges" Kraftwerk: hoechstens HUNGRY_BELOW Sekunden Vorrat und kein Lager mit Faessern in seiner Reichweite
		auto hungryFor = [&](const DROID *d) -> STRUCTURE *
		{
			STRUCTURE *hungry = nullptr;
			int64_t hd = INT64_MAX;
			for (STRUCTURE *g : activeGens)
			{
				if (s_fuel[g->id] > HUNGRY_BELOW || s_genPiped[g->id] > 0) { continue; }   // (23.11.) per Pipeline versorgt: nicht hungrig
				bool stocked = false;
				for (BASE_OBJECT *t : towers)
				{
					if (s_store[t->id] + s_storeR[t->id] > 0 && d2(t->pos.x, t->pos.y, g->pos.x, g->pos.y) <= SR2) { stocked = true; break; }
				}
				if (stocked) { continue; }
				const int64_t dd = d2(d->pos.x, d->pos.y, g->pos.x, g->pos.y);
				if (dd < hd) { hd = dd; hungry = g; }
			}
			return hungry;
		};

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
			// (Wunsch 24.11. 17:46) 4 s statt 5 s
			if (c.forced && modcontrol::sincePlayerOrder(d) >= 4000u) { c.forced = false; }
			const bool freshOrder = !c.forced && modcontrol::sincePlayerOrder(d) < 4000u;
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
				if (++c.idleSec < 4) { continue; }
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
						const int kindContent = (c.kind == 3) ? 1 : 2;   // Oelturm: Rohoel, Raffinerie: veredelt
						if (c.n <= 0) { c.content = kindContent; }
						if (c.content == kindContent)   // (24.11.) nie Roh- und veredeltes Oel mischen
						{
							int &stock = (c.kind == 3) ? s_barrels[c.target] : s_refOut[c.target];
							const int take = std::min(cap - c.n, stock);
							c.n += take;
							stock -= take;
						}
					}
					else if (c.kind == 1)
					{
						const int m = std::min(c.n, storeRoom(c.target));
						s_storeR[c.target] += m;
						c.n -= m;
					}
					else if (c.kind == 2 && c.content == 1)
					{
						const int m = std::min(c.n, std::max(0, REF_CAP - s_refIn[c.target]));
						s_refIn[c.target] += m;
						c.n -= m;
					}
					else if (c.kind == 2) { /* veredeltes Oel gehoert nicht in den Raffinerie-Eingang */ }
					else if (c.content == 1) { /* Rohoel nur in die Raffinerie */ }
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
						const int m = std::min(c.n, genRoom(ts));   // (17:46) Kraftwerk fasst GEN_BARREL_CAP Faesser
						deliverToGenerator(p, ts, m, false);
						c.n -= m;
					}
					if (c.n <= 0) { c.n = 0; c.content = 0; }
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

			if (c.n <= 0) { c.n = 0; c.content = 0; }
			// (Wunsch 24.11. 17:01) der Oeltanker faehrt ALLES Oel: Rohoel vom Oelturm zur Raffinerie und veredeltes Oel von der Raffinerie zu
			// Kraftwerk / Tankstelle / Lager. Er mischt nie: geladenes Rohoel geht nur in die Raffinerie.
			auto nearestRawPickup = [&]() -> STRUCTURE *
			{
				STRUCTURE *best = nullptr;
				int64_t bd = INT64_MAX;
				for (STRUCTURE *s : sn.derricks)
				{
					if (avail[s->id] <= 0) { continue; }
					const int64_t dd = d2(d->pos.x, d->pos.y, s->pos.x, s->pos.y);
					if (dd < bd) { bd = dd; best = s; }
				}
				return best;
			};
			auto nearestRefinedPickup = [&]() -> STRUCTURE *
			{
				STRUCTURE *best = nullptr;
				int64_t bd = INT64_MAX;
				for (STRUCTURE *s : sn.refs)
				{
					if (avail[s->id] <= 0) { continue; }
					const int64_t dd = d2(d->pos.x, d->pos.y, s->pos.x, s->pos.y);
					if (dd < bd) { bd = dd; best = s; }
				}
				return best;
			};
			if (c.n > 0 && c.content == 1)
			{
				// Rohoel geladen: auffuellen, solange die Raffinerien noch Platz haben, dann zur naechsten Raffinerie mit Platz
				if (cap - c.n > 0 && refRoomTotal >= cap)
				{
					if (STRUCTURE *best = nearestRawPickup())
					{
						avail[best->id] -= (cap - c.n);
						go(1, 3, best->id, best->pos.x, best->pos.y);
						continue;
					}
				}
				STRUCTURE *br = nullptr;
				int64_t bd = INT64_MAX;
				for (STRUCTURE *r : sn.refs)
				{
					if (refRoom(r) <= 0) { continue; }
					const int64_t dd = d2(d->pos.x, d->pos.y, r->pos.x, r->pos.y);
					if (dd < bd) { bd = dd; br = r; }
				}
				if (br != nullptr) { refIncoming[br->id] += c.n; refRoomTotal -= c.n; go(2, 2, br->id, br->pos.x, br->pos.y); }
				continue;   // keine Raffinerie mit Platz: mit der Ladung warten
			}
			if (c.n <= 0)
			{
				// leer: veredeltes Oel holen, wenn ein Kraftwerk hungert oder an einer Raffinerie mindestens eine halbe Ladung wartet; sonst
				// Rohoel zur Raffinerie bringen (nur so viel, wie dort Platz ist); sonst den Rest an veredeltem Oel
				// veredeltes Oel nur holen, wenn es jemand braucht (laufendes Kraftwerk oder Tankstelle mit Platz) - sonst stuende der Tanker
				// voll herum und faehre kein Rohoel mehr
				bool refinedDemand = false;
				for (STRUCTURE *g : activeGens) { if (genRoom(g) > 0) { refinedDemand = true; break; } }
				for (STRUCTURE *st : sn.stations) { if (s_station[st->id] < STATION_CAP * 1000) { refinedDemand = true; break; } }
				STRUCTURE *ref = refinedDemand ? nearestRefinedPickup() : nullptr;
				STRUCTURE *raw = refRoomTotal > 0 ? nearestRawPickup() : nullptr;
				const bool hungry = hungryFor(d) != nullptr;
				const bool refinedWaiting = ref != nullptr && (hungry || avail[ref->id] >= cap / 2 || s_refOut[ref->id] >= REF_CAP);
				if (ref != nullptr && (refinedWaiting || raw == nullptr))
				{
					avail[ref->id] -= cap;
					go(1, 2, ref->id, ref->pos.x, ref->pos.y);
					continue;
				}
				if (raw != nullptr)
				{
					avail[raw->id] -= cap;
					refRoomTotal -= std::min(cap, avail[raw->id] + cap);
					go(1, 3, raw->id, raw->pos.x, raw->pos.y);
					continue;
				}
				continue;
			}
			// veredeltes Oel geladen: Raffinerie -> Kraftwerk / Tankstelle, wahlweise ueber das Lager.
			// (20.11. Punkt 7) Ein hungriges Kraftwerk wird beliefert, bevor der Tanker nachlaedt.
			if (STRUCTURE *hungry = hungryFor(d)) { go(2, 0, hungry->id, hungry->pos.x, hungry->pos.y); continue; }
			if (cap - c.n > 0)
			{
				if (STRUCTURE *best = nearestRefinedPickup())
				{
					avail[best->id] -= (cap - c.n);
					go(1, 2, best->id, best->pos.x, best->pos.y);
					continue;
				}
			}
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
			// naechstes Kraftwerk mit Platz (Reserve - es darf sich viel anstauen)
			{
				STRUCTURE *bg = nullptr;
				int64_t bd = INT64_MAX;
				for (STRUCTURE *g : activeGens)
				{
					if (genRoom(g) <= 0) { continue; }
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
	for (auto it = s_fuel.begin(); it != s_fuel.end();) { it = liveStruct.count(it->first) ? std::next(it) : s_fuel.erase(it); }
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

	processPipeRetries();   // (23.11. 20:22) blockierte Rohrstuecke erneut beauftragen

	updateTerrain();

	syncDebug("modoil b%zu f%zu c%zu", s_barrels.size(), s_fuel.size(), s_cargo.size());
}

// ---------------------------------------------------------------- tiefe Graeben: Gelaende absenken

constexpr int TRENCH_DEPTH = 24;   // (17.11. 14:14) = Sohle der tiefen Modelle (trench.py)

/// MOD (Wunsch 24.11. 15:33 "Die Graeben sind keine, und die tiefen Graeben versinken nicht richtig"): JEDER Graben ist eingegraben -
/// Sohle des Modells unter dem Boden (floor) und Tiefe des Gelaendenetzes in der Rinne (vis, tiefer, damit das grobe Netz nirgends
/// durch die Sohle stoesst). 0 = senkt nicht (flache Gruben, Mauern ...).
struct SinkKind { int floor = 0, vis = 0; bool pit = false; };
SinkKind sinkKindOf(const STRUCTURE_STATS *st)
{
	SinkKind k;
	if (st == nullptr || st->type == REF_DEMOLISH) { return k; }
	const std::string id = st->id.toUtf8();
	if (id == "A0BaBaTrench" || id == "A0BaBaTrenchCover") { k.floor = 12; k.vis = 26; }
	else if (id == "A0BaBaTrenchDeep" || id == "A0BaBaTrenchDeepCover") { k.floor = TRENCH_DEPTH; k.vis = 44; }
	else if (id == "A0BaBaTrenchEarth") { k.floor = 10; k.vis = 22; }
	else if (id == "A0BaBaTrenchEarthDeep") { k.floor = 20; k.vis = 38; }
	else if (id == "A0BaBaPitSquareDeep" || id == "A0BaBaPitHexDeep") { k.floor = TRENCH_DEPTH; k.vis = 48; k.pit = true; }
	return k;
}

void updateTerrain()
{
	// alle fertigen Graben-Bauwerke der aktiven Ebene (Mitte -> Art); Reihenfolge der Liste egal (modterrain sortiert)
	std::map<std::pair<int, int>, std::pair<const STRUCTURE *, SinkKind>> at;
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s == nullptr || s->died || s->status != SS_BUILT) { continue; }
			const SinkKind k = sinkKindOf(s->pStructureType);
			if (k.floor <= 0) { continue; }
			at[std::make_pair(s->pos.x, s->pos.y)] = std::make_pair(s, k);
		}
	}
	if (at.empty()) { modterrain::setTrenchSinks({}); return; }
	static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};   // Seite k: 1 Nord, 2 Ost, 4 Sued, 8 West
	std::vector<modterrain::TrenchSink> list;
	list.reserve(at.size());
	for (const auto &kv : at)
	{
		const STRUCTURE *s = kv.second.first;
		const SinkKind &k = kv.second.second;
		modterrain::TrenchSink t;
		t.x = s->pos.x; t.y = s->pos.y; t.floor = k.floor; t.vis = k.vis; t.pit = k.pit;
		uint8_t sinkNb = 0;
		for (int d = 0; d < 4; ++d)
		{
			if (at.count(std::make_pair(s->pos.x + DX[d] * DIST_TILE, s->pos.y + DY[d] * DIST_TILE)) != 0) { sinkNb |= static_cast<uint8_t>(1 << d); }
		}
		uint8_t arms = sinkNb;
		if (modfort::isFort(s))
		{
			// Stellungsnetz: Gaenge des Modells (auch zu Gruben und Eingaengen ohne absenkenden Nachbarn)
			arms |= static_cast<uint8_t>(modfort::sides(map_coord(s->pos.x), map_coord(s->pos.y)) & 0x0f);
		}
		else if (!k.pit)
		{
			// Erdgraben (gewoehnliches Mauer-Modell): ein Nachbar = gerades Stueck, das auf der Gegenseite offen endet;
			// keiner = gerades Stueck in Baurichtung
			if (arms == 1 || arms == 2 || arms == 4 || arms == 8) { arms |= static_cast<uint8_t>(((arms << 2) | (arms >> 2)) & 0x0f); }
			else if (arms == 0) { arms = (((s->rot.direction + 0x2000u) >> 14) & 1u) ? 0x05 : 0x0a; }   // Richtung 0: Modell laengs x
		}
		t.arms = arms;
		t.ramps = static_cast<uint8_t>(arms & ~sinkNb);
		list.push_back(t);
	}
	modterrain::setTrenchSinks(std::move(list));
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
		const int64_t known = s_ledger[p][INC_BARRELS] + s_ledger[p][INC_PIPE] + s_ledger[p][INC_SOLAR] + s_ledger[p][INC_LAVA] + s_ledger[p][INC_SALVAGE] + s_ledger[p][INC_NUCLEAR];
		int piped = 0;   // (23.11.) Oeltuerme per Pipeline am Kraftwerk
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
				if (derrickPiped(s)) { piped++; }
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
		debug(LOG_INFO, "MOD-Wirtschaft Spieler %d: Energie %lld (%+lld/min: Faesser %lld, Pipeline %lld, Solar %lld, Lava %lld, Bergung %lld, Atom %lld, sonstige %lld) | Fass-Wirtschaft=%s | Oeltuerme %d (am Kraftwerk %d, Faesser %d) | Kraftwerke %d (laufend %d, Bereitschaft %d, Pipeline-Oeltuerme %d) | Raffinerien %d (ohne Strom %d, Eingang %d, Ausgang %d) | Lkw %d, Tanker %d (unterwegs %d, wartend %d, geladen %d) | Atomkraftwerke %d (laufend %d), Uran %d",
		      p, static_cast<long long>(now), static_cast<long long>(delta), static_cast<long long>(s_ledger[p][INC_BARRELS]), static_cast<long long>(s_ledger[p][INC_PIPE]), static_cast<long long>(s_ledger[p][INC_SOLAR]),
		      static_cast<long long>(s_ledger[p][INC_LAVA]), static_cast<long long>(s_ledger[p][INC_SALVAGE]), static_cast<long long>(s_ledger[p][INC_NUCLEAR]), static_cast<long long>(delta - known),
		      manages(p) ? "ja" : "NEIN", derricks, linked, barrels, gens, fuelled, standby, piped, refs, refsDown, refIn, refOut, trucks, tankers, driving, waiting, loaded,
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

namespace
{
/// (Wunsch 24.11. 17:01 "Sobald ein Oilrig gebaut ist, spawnt einer. Sobald eine Raffinerie gebaut ist, spawnt einer."): jeder Oelturm und
/// jede Raffinerie bringt einen Oeltanker mit. Damit Abreissen und Neubauen keine Gratis-Tanker erzeugt, gilt eine Obergrenze: so viele
/// Tanker wie Oeltuerme + Raffinerien des Spielers (gezaehlt mit dem neuen Gebaeude) - steht schon genug Tanker da, kommt keiner.
int tankerQuota(int p)
{
	int q = 0;
	for (const STRUCTURE *s : gameWorld.objects.structures[p])
	{
		if (s->died || s->status != SS_BUILT || s->pStructureType == nullptr) { continue; }
		if (s->pStructureType->type == REF_RESOURCE_EXTRACTOR || isRefinery(s->pStructureType)) { q++; }
	}
	return q;
}
int tankerCount(int p)
{
	int n = 0;
	for (const DROID *d : gameWorld.objects.droids[p]) { if (!d->died && cargoCapacity(d) > 0) { n++; } }
	return n;
}
void tankerFor(STRUCTURE *s)
{
	// (Wunsch 24.11. 17:46 "Es sollen aber zusaetzliche Oeltanker in der Fabrik ausgebildet werden duerfen"): gezaehlt werden nur die
	// gratis vergebenen Tanker - selbst gebaute zaehlen nicht und nehmen keinem Oelturm seinen Tanker weg
	const int p = static_cast<int>(s->player);
	if (p < 0 || p >= MAX_PLAYERS) { return; }
	if (s_freeTankers[p] < tankerQuota(p) && spawnNear(s, "ScavTanker")) { s_freeTankers[p]++; }
}


/// Oeltuerme und Raffinerien, die schon auf der Karte stehen, wurden nie "gebaut": zu Beginn einer neuen Partie (erste Sekunden) bekommt
/// jeder Spieler die Tanker dafuer (bis zur Obergrenze). Nicht beim Laden eines Spielstands (dort sind die Tanker schon da).
void startGifts()
{
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		if (s_startChecked[p]) { continue; }
		s_startChecked[p] = true;
		if (!manages(p)) { continue; }
		if (gameTime > 10000)
		{
			// geladener Spielstand ohne Zaehler: vorhandene Tanker gelten als vergeben (keine Gratis-Tanker beim naechsten Oelturm doppelt)
			if (s_freeTankers[p] == 0) { s_freeTankers[p] = std::min(tankerCount(p), tankerQuota(p)); }
			continue;
		}
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || s->pStructureType == nullptr) { continue; }
			if (s->pStructureType->type == REF_RESOURCE_EXTRACTOR || isRefinery(s->pStructureType)) { tankerFor(s); }
		}
	}
}
}

void onBuilt(STRUCTURE *s)
{
	if (s == nullptr || s->died || s->capacity != 0 || !modPresent() || !manages(static_cast<int>(s->player))) { return; }   // capacity != 0: Modul fertig
	// (Wunsch 24.11. 17:01) Oelturm oder Raffinerie fertig -> ein Oeltanker steht daneben (bis zur Obergrenze, siehe tankerFor)
	if (s->pStructureType->type == REF_RESOURCE_EXTRACTOR || isRefinery(s->pStructureType)) { tankerFor(s); }
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
		for (const DROID *d : gameWorld.objects.droids[p]) { if (!d->died && cargoCapacity(d) > 0) { have = true; break; } }
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
		if (!tank) { continue; }   // (24.11.) keine Pickups mehr
		if (at != nullptr && spawnNear(at, "ScavTanker"))
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
	startGifts();
	logistics();
	// Energie der verbrannten Faesser stetig auszahlen (mit mehreren Oeltuermen steigt das Konto wie in Vanilla laufend)
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		int &b = s_bank[p];
		if (b <= 0) { continue; }
		// (Wunsch 23.11. 19:29 "gleichmaessig statt schubweise"): gleichbleibender Betrag je Sekunde - das Guthaben laeuft in GEN_SECS
		// (so lange brennt ein Fass) gleichmaessig ein; jede Lieferung legt den Satz neu fest (deliverToGenerator)
		if (s_payRate[p] <= 0) { s_payRate[p] = std::max(1, (b + GEN_SECS - 1) / GEN_SECS); }
		const int pay = std::min(b, s_payRate[p]);
		b -= pay;
		if (b <= 0) { s_payRate[p] = 0; }
		addPower(p, pay);
		noteIncome(p, INC_BARRELS, pay);
	}
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		// (23.11. 19:57) stetige Pipeline-Einnahmen (power.cpp, Festkomma) ins Einnahmen-Buch
		const int64_t whole = s_oilFP[p] >> 32;
		if (whole > 0)
		{
			s_oilFP[p] -= whole << 32;
			noteIncome(p, INC_PIPE, static_cast<int>(std::min<int64_t>(whole, INT_MAX)));
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
			const auto it = s_fuel.find(s->id);
			const int fuel = it == s_fuel.end() ? START_FUEL : it->second;
			bar(m, s->pos.x, s->pos.y, s->pos.z + 230, (fuel + GEN_SECS - 1) / GEN_SECS, GEN_BARREL_CAP, fuel > GEN_SECS ? okOn : lowOn, barOff);   // (17:46) Faesser; rot unter 2
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
