// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Befestigungsbau - siehe modbuild.h
#include "modcrates.h"
#include "modbuild.h"
#include "modfort.h"
#include "feature.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "baseobject.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "modoil.h"
#include "modterrain.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "ordersource.h"
#include "range_rings.h"
#include "stats.h"
#include "statsdef.h"
#include "structure.h"
#include "action.h"
#include "random.h"
#include "ai.h"

#include <algorithm>
#include <string>
#include <cmath>
#include <cstdlib>
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>

namespace modbuild
{
bool isInfantryBuilder(const DROID *d)
{
	if (d == nullptr) { return false; }
	if (d->droidType != DROID_PERSON && d->droidType != DROID_CYBORG && d->droidType != DROID_CYBORG_SUPER) { return false; }
	return d->asBits[COMP_CONSTRUCT] != 0 && modoil::isInfantry(d);
}

bool mayBuild(const DROID *d, const STRUCTURE_STATS *st)
{
	if (st == nullptr || !isInfantryBuilder(d)) { return true; }
	return st->infantryBuildable;
}

namespace
{
constexpr int MAX_PLACEMENTS = 170;

struct Job
{
	unsigned player = 0;
	Vector2i center{0, 0};                 // Weltkoordinaten
	int tier = 2;
	bool moving = false;             // wir haben dem Fahrzeug einen Marsch zur Mulde gegeben
	Vector2i moveTarget{0, 0};
	unsigned startTime = 0;
};

std::unordered_map<uint32_t, Job> s_jobs;
int s_tier = 2;
bool s_ringOn = false;
// (16.11. 19:54) gruene Bauplan-Vorschau schon beim Drueberfahren (vor dem Klick)
std::vector<Placement> s_preview;
Vector2i s_previewTile{-1, -1};
int s_previewTier = 0;
unsigned s_previewAt = 0;
bool s_previewOn = false;

enum class Role
{
	Dragon, Hedgehog, Bunker, Flame, Wall, Mortar, GunTower, RocketPit, Trench
};

const char *const *namesOf(Role r, int &count)
{
	static const char *const dragon[] = {"A0TankTrapHeavy", "A0BaBaHedgehog", "A0TankTrap"};
	// MOD (Wunsch 20.11. 02:13 "Die Sprite-Version der Hedgehogs wieder rausnehmen"): 3D-Igel zuerst, dann Beton-Panzersperren
	static const char *const hedge[] = {"A0BaBaHedgehog", "A0TankTrapHeavy", "A0TankTrap"};
	static const char *const bunker[] = {"A0BaBaBunker", "PillBox1", "PillBox4"};
	static const char *const flame[] = {"A0BaBaFlameTower", "PillBox5", "Emplacement-Rocket06-IDF"};
	static const char *const wall[] = {"A0BaBaSandbagWall", "A0HardcreteMk1Wall", "A0BaBaHorizontalWall"};
	static const char *const mortar[] = {"A0BaBaMortarPit", "Emplacement-MortarPit01"};
	static const char *const gun[] = {"A0BaBaGunTower", "GuardTower1", "A0BaBaBunker"};
	static const char *const rocket[] = {"A0BaBaRocketPitAT", "A0BaBaRocketPit", "Emplacement-MRL-pit"};
	static const char *const trench[] = {"A0BaBaTrench", "A0BaBaTrenchCover"};
	switch (r)
	{
	case Role::Dragon: count = 3; return dragon;
	case Role::Hedgehog: count = 3; return hedge;
	case Role::Bunker: count = 3; return bunker;
	case Role::Flame: count = 3; return flame;
	case Role::Wall: count = 3; return wall;
	case Role::Mortar: count = 2; return mortar;
	case Role::GunTower: count = 3; return gun;
	case Role::RocketPit: count = 3; return rocket;
	default: count = 2; return trench;
	}
}

STRUCTURE_STATS *pick(unsigned player, Role r)
{
	int n = 0;
	const char *const *names = namesOf(r, n);
	for (int i = 0; i < n; i++)
	{
		STRUCTURE_STATS *st = getStructStatsFromName(WzString::fromUtf8(names[i]));
		if (st == nullptr) { continue; }
		const int idx = static_cast<int>(st - asStructureStats);
		if (idx < 0 || idx >= static_cast<int>(numStructureStats)) { continue; }
		const int status = apStructTypeLists[player][idx];
		if (status == AVAILABLE || status == REDUNDANT)
		{
			return st;
		}
	}
	return nullptr;
}

struct Rect
{
	int x0, y0, x1, y1;   // Kacheln, einschliesslich
};

bool isBaseStructure(const STRUCTURE *s)
{
	if (s == nullptr || s->died || s->pStructureType == nullptr) { return false; }
	switch (s->pStructureType->type)
	{
	case REF_DEFENSE:
	case REF_WALL:
	case REF_WALLCORNER:
	case REF_GATE:
		return false;
	default:
		return true;
	}
}

/// Rechteck der eigenen Gebaeude im Kreis; ohne Gebaeude ein 3x3-Feld um die Mitte
Rect baseRect(unsigned player, const Vector2i &center, int radiusTiles)
{
	const int cx = map_coord(center.x), cy = map_coord(center.y);
	Rect r{cx - 1, cy - 1, cx + 1, cy + 1};
	bool any = false;
	for (const STRUCTURE *s : gameWorld.objects.structures[player])
	{
		if (!isBaseStructure(s)) { continue; }
		const int sx = map_coord(s->pos.x), sy = map_coord(s->pos.y);
		if (std::abs(sx - cx) > radiusTiles || std::abs(sy - cy) > radiusTiles) { continue; }
		const int hw = std::max(1, static_cast<int>(s->pStructureType->baseWidth)) / 2;
		const int hb = std::max(1, static_cast<int>(s->pStructureType->baseBreadth)) / 2;
		if (!any)
		{
			r = Rect{sx - hw, sy - hb, sx + hw, sy + hb};
			any = true;
		}
		else
		{
			r.x0 = std::min(r.x0, sx - hw);
			r.y0 = std::min(r.y0, sy - hb);
			r.x1 = std::max(r.x1, sx + hw);
			r.y1 = std::max(r.y1, sy + hb);
		}
	}
	return r;
}

struct RingSpec
{
	int k;          // Abstand vom Gebaeude-Rechteck (Kacheln)
	Role role;
	int step;       // jede step-te Kachel des Umfangs
	int offset;
	bool gates;     // Tore (3 Kacheln) in der Seitenmitte freilassen
	int minTier;
};

// innen -> aussen. k = Abstand vom Gebaeuderechteck.
// MOD (Wunsch 16.11. 20:54): deutlich kleiner; Mauer dicht an der Basis, Kanonentuerme als Ecktuerme (siehe Mauer), davor Igel und
// Hoecker in versetzten Reihen in die Tiefe gestaffelt (Schachbrett, step 2 = jedes zweite Feld nach x+y, Nachbarreihe versetzt).
// (18.11. 02:55 "Diese Basenbildung funktioniert anscheinend noch nicht so richtig") aufgeraeumt: statt bis zu fuenf Igel-/Hoecker-Reihen
// (Hunderte verstreute Sperren) eine klare Anlage - Mauer mit Toren und Ecktuermen, Bunker/Flammentuerme davor, hoechstens zwei
// versetzte Panzersperren-Reihen; Moerser nur bei der grossen Anlage.
const RingSpec kRings[] = {
	{1, Role::Mortar,    5, 1, false, 3},
	{2, Role::Wall,      1, 0, true,  1},
	{3, Role::Bunker,    6, 0, true,  2},
	{3, Role::Flame,     6, 3, true,  3},
	{4, Role::Hedgehog,  2, 0, true,  2},
	{5, Role::Hedgehog,  2, 1, true,  3},
};

struct Cell
{
	int x, y, side;     // side: 0 N, 1 E, 2 S, 3 W
};

/// Umfang des Rechtecks um rect, Abstand k, im Uhrzeigersinn
std::vector<Cell> perimeter(const Rect &b, int k)
{
	std::vector<Cell> v;
	const int x0 = b.x0 - k, y0 = b.y0 - k, x1 = b.x1 + k, y1 = b.y1 + k;
	for (int x = x0; x <= x1; x++) { v.push_back({x, y0, 0}); }
	for (int y = y0 + 1; y <= y1; y++) { v.push_back({x1, y, 1}); }
	for (int x = x1 - 1; x >= x0; x--) { v.push_back({x, y1, 2}); }
	for (int y = y1 - 1; y > y0; y--) { v.push_back({x0, y, 3}); }
	return v;
}

bool inGate(const Rect &b, int k, const Cell &c)
{
	const int midX = (b.x0 + b.x1) / 2, midY = (b.y0 + b.y1) / 2;
	(void)k;
	if (c.side == 0 || c.side == 2) { return std::abs(c.x - midX) <= 1; }
	return std::abs(c.y - midY) <= 1;
}

unsigned outwardDir(int side)
{
	// 0 = Norden; Viertelkreise im Uhrzeigersinn
	return static_cast<unsigned>(side) * 0x4000u;
}

bool tileOk(unsigned player, STRUCTURE_STATS *st, int tx, int ty, unsigned dir)
{
	if (tx < 3 || ty < 3 || tx >= gameWorld.map.width - 3 || ty >= gameWorld.map.height - 3) { return false; }
	const Vector2i w(world_coord(tx) + TILE_UNITS / 2, world_coord(ty) + TILE_UNITS / 2);
	return validLocation(gameWorld, st, w, static_cast<uint16_t>(dir), player, false);
}

// ---------------------------------------------------------------------------------------------------------------------
// MOD (Wunsch 16.11. 17:54): dynamisch um Hindernisse herum bauen. Blockiert Gelaende oder etwas im Weg (Steine, Wracks,
// Gebaeude) einen Graben, eine Mauer oder eine Stellung, wird drumherum gebaut: Stellungen rutschen auf das naechste freie
// Feld, Graeben/Mauern laufen um die Ecke (Weg in 4er-Nachbarschaft, damit die Stuecke verbunden bleiben), werden dabei
// verlaengert - oder verkuerzt, wenn es gar keinen Weg gibt. Alles deterministisch (feste Reihenfolge, keine Zufallszahlen).
// ---------------------------------------------------------------------------------------------------------------------
using TileKey = std::pair<int, int>;
using TileSet = std::set<TileKey>;

inline TileKey keyOf(const Vector2i &v) { return TileKey(v.x, v.y); }
inline int manhattan(const Vector2i &a, const Vector2i &b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

/// eigenes Gebaeude auf dem Feld (schon gebaut - bei erneutem Befehl nicht noch einmal daneben setzen)
STRUCTURE *ownStructAt(unsigned player, const Vector2i &t)
{
	if (t.x < 0 || t.y < 0 || t.x >= gameWorld.map.width || t.y >= gameWorld.map.height) { return nullptr; }
	const MAPTILE *mt = mapTile(gameWorld.map, t.x, t.y);
	if (mt == nullptr || !TileHasStructure(mt)) { return nullptr; }
	STRUCTURE *os = static_cast<STRUCTURE *>(mt->psObject);
	return (os != nullptr && !os->died && os->player == player) ? os : nullptr;
}

bool freeFor(unsigned player, STRUCTURE_STATS *st, const Vector2i &t, unsigned dir, const TileSet &used)
{
	return st != nullptr && used.count(keyOf(t)) == 0 && tileOk(player, st, t.x, t.y, dir);
}

/// Felder um want nach Abstand (Manhattan-Ringe 0..maxR), feste Reihenfolge
std::vector<Vector2i> candidatesAround(const Vector2i &want, int maxR)
{
	std::vector<Vector2i> v;
	for (int r = 0; r <= maxR; ++r)
	{
		for (int dy = -r; dy <= r; ++dy)
		{
			for (int dx = -r; dx <= r; ++dx)
			{
				if (std::abs(dx) + std::abs(dy) == r) { v.emplace_back(want.x + dx, want.y + dy); }
			}
		}
	}
	return v;
}

/// Weg aus Feldern (4er-Nachbarschaft) von einem Nachbarn von 'from' bis zu einem Nachbarn von 'to' (beide selbst nicht im Weg).
/// true + leerer Weg, wenn from und to schon benachbart sind. Kuerzester Weg, bei Gleichstand der geradere (Richtung zum Ziel zuerst).
bool routeTiles(const Vector2i &from, const Vector2i &to, const std::function<bool(const Vector2i &)> &passable, int maxLen, std::vector<Vector2i> &out)
{
	out.clear();
	if (manhattan(from, to) == 1) { return true; }
	static const int D[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
	auto sortedNeighbours = [&](const Vector2i &t) {
		std::vector<Vector2i> n;
		for (const auto &d : D) { n.emplace_back(t.x + d[0], t.y + d[1]); }
		std::stable_sort(n.begin(), n.end(), [&](const Vector2i &a, const Vector2i &b) { return manhattan(a, to) < manhattan(b, to); });
		return n;
	};
	std::map<TileKey, TileKey> prev;
	std::deque<std::pair<Vector2i, int>> q;
	for (const Vector2i &n : sortedNeighbours(from))
	{
		if (n == to || !passable(n)) { continue; }
		prev[keyOf(n)] = keyOf(from);
		q.emplace_back(n, 1);
	}
	while (!q.empty())
	{
		const Vector2i t = q.front().first;
		const int len = q.front().second;
		q.pop_front();
		if (manhattan(t, to) == 1)
		{
			TileKey k = keyOf(t);
			while (k != keyOf(from))
			{
				out.emplace_back(k.first, k.second);
				k = prev[k];
			}
			std::reverse(out.begin(), out.end());
			return true;
		}
		if (len >= maxLen) { continue; }
		for (const Vector2i &n : sortedNeighbours(t))
		{
			if (n == from || n == to || prev.count(keyOf(n)) != 0 || !passable(n)) { continue; }
			prev[keyOf(n)] = keyOf(t);
			q.emplace_back(n, len + 1);
		}
	}
	return false;
}

/// Feldfolge in gerade Stuecke zerlegen (fuer Linienbau): Paare (Anfang, Ende) in Bau-Reihenfolge
std::vector<std::pair<Vector2i, Vector2i>> straightRuns(const std::vector<Vector2i> &cells)
{
	std::vector<std::pair<Vector2i, Vector2i>> runs;
	size_t i = 0;
	while (i < cells.size())
	{
		size_t j = i;
		if (j + 1 < cells.size() && manhattan(cells[j], cells[j + 1]) == 1)
		{
			const int dx = cells[j + 1].x - cells[j].x, dy = cells[j + 1].y - cells[j].y;
			while (j + 1 < cells.size() && cells[j + 1].x - cells[j].x == dx && cells[j + 1].y - cells[j].y == dy) { ++j; }
		}
		runs.emplace_back(cells[i], cells[j]);
		i = j + 1;
	}
	return runs;
}
} // namespace

int radiusOfTier(int tier)
{
	switch (std::clamp(tier, 1, 3))
	{
	case 1: return 4;
	case 2: return 6;
	default: return 9;
	}
}

int currentTier()
{
	return s_tier;
}

/// MOD (16.11.): Deko im Niemandsland zwischen den Ringen (Steine, Wracks, Absperrungen) - wie im Wunsch-Layout "dazwischen immer Deko".
/// Wird beim Start des Befestigungsbaus einmal gesetzt (Spielschritt, feste Reihenfolge, Zufall nur aus den Kachelkoordinaten).
static void placeDeco(unsigned player, const Vector2i &worldCenter, int tier)
{
	static const char *const decoNames[] = {"Boulder1", "Boulder2", "Boulder3", "Wreck0", "Wreck1", "Wreck2", "Wreck3", "Wreck4", "Wreck5",
	                                        "WreckedSuzukiJeep", "WreckedVertCampVan", "barrier", "Boulder2", "Wreck1"};
	constexpr int NDECO = static_cast<int>(sizeof(decoNames) / sizeof(decoNames[0]));
	const Rect b = baseRect(player, worldCenter, radiusOfTier(tier));
	// (16.11. 20:54) Deko nur noch vor dem aeussersten Hindernisguertel (der Plan ist kleiner geworden)
	const int rings[3] = {6, 8, 10};
	for (int ri = 0; ri < 3; ++ri)
	{
		const int k = rings[ri];
		if (tier == 1 && k != 6) { continue; }
		if (tier == 2 && k != 8) { continue; }
		if (tier == 3 && k != 10) { continue; }
		for (const Cell &c : perimeter(b, k))
		{
			const uint32_t h = static_cast<uint32_t>(c.x) * 73856093u ^ static_cast<uint32_t>(c.y) * 19349663u ^ static_cast<uint32_t>(k) * 83492791u;
			if (h % 5u != 0u) { continue; }   // etwa jedes fuenfte Feld
			if (c.x < 2 || c.y < 2 || c.x >= gameWorld.map.width - 2 || c.y >= gameWorld.map.height - 2) { continue; }
			MAPTILE *t = mapTile(gameWorld.map, c.x, c.y);
			if (t == nullptr || TileIsOccupied(t) || terrainType(t) == TER_WATER || terrainType(t) == TER_CLIFFFACE) { continue; }
			FEATURE_STATS *fs = nullptr;
			const char *want = decoNames[(h >> 8) % static_cast<uint32_t>(NDECO)];
			for (auto &s : asFeatureStats) { if (s.id == want) { fs = &s; break; } }
			if (fs == nullptr) { continue; }
			FEATURE *f = buildFeature(gameWorld, fs, static_cast<UDWORD>(world_coord(c.x) + TILE_UNITS / 2), static_cast<UDWORD>(world_coord(c.y) + TILE_UNITS / 2), false);
			if (f != nullptr) { f->rot.direction = static_cast<uint16_t>((h >> 4) & 0xffffu); }
		}
	}
}

std::vector<Placement> planFor(unsigned player, const Vector2i &worldCenter, int tier)
{
	std::vector<Placement> out;
	if (player >= MAX_PLAYERS) { return out; }
	tier = std::clamp(tier, 1, 3);
	const Rect b = baseRect(player, worldCenter, radiusOfTier(tier));
	TileSet used;   // MOD 16.11.: schon verplante Felder (Ausweichen darf nichts doppelt belegen)
	// Abstand eines Feldes vom Gebaeuderechteck (Schachbrett-Abstand)
	auto ringDist = [&](const Vector2i &t) {
		const int dx = std::max({b.x0 - t.x, 0, t.x - b.x1});
		const int dy = std::max({b.y0 - t.y, 0, t.y - b.y1});
		return std::max(dx, dy);
	};
	for (const RingSpec &rs : kRings)
	{
		if (tier < rs.minTier) { continue; }
		// kleine Anlage: die aeusseren Ringe rutschen nach innen (Ring 4 -> 3 usw. ist hier nicht noetig, Groesse 1 baut nur bis Ring 4)
		if (tier == 1 && rs.k > 4) { continue; }
		if (tier == 2 && rs.k > 6) { continue; }
		STRUCTURE_STATS *st = pick(player, rs.role);
		if (st == nullptr) { continue; }
		const std::vector<Cell> ring = perimeter(b, rs.k);
		if (rs.role == Role::Wall)
		{
			// MOD 16.11.: Mauerring ohne Tore als Feldfolge; blockierte Stuecke werden umgangen (Umweg nach aussen/innen, um die Ecke),
			// geht es gar nicht, bleibt dort eine Luecke. Danach in gerade Linienbau-Stuecke zerlegt.
			const int k = rs.k;
			auto wallPassable = [&](const Vector2i &t) {
				const int d = ringDist(t);
				return d >= std::max(1, k - 2) && d <= k + 2 && (freeFor(player, st, t, 0, used) || (used.count(keyOf(t)) == 0 && ownStructAt(player, t) != nullptr));
			};
			// MOD (Wunsch 16.11. 20:54): Kanonentuerme als Ecktuerme - an den vier Ecken des Mauerrings, die Mauer endet daneben
			TileSet towerCells;
			if (STRUCTURE_STATS *tw = pick(player, Role::GunTower))
			{
				const Vector2i corners[4] = { Vector2i(b.x0 - k, b.y0 - k), Vector2i(b.x1 + k, b.y0 - k), Vector2i(b.x1 + k, b.y1 + k), Vector2i(b.x0 - k, b.y1 + k) };
				const unsigned cornerDir[4] = { 0xE000u, 0x2000u, 0x6000u, 0xA000u };   // schraeg nach aussen
				for (int ci = 0; ci < 4; ++ci)
				{
					const Vector2i &t = corners[ci];
					if (ownStructAt(player, t) != nullptr) { towerCells.insert(keyOf(t)); continue; }   // steht schon (erneuter Befehl)
					if (!freeFor(player, tw, t, cornerDir[ci], used)) { continue; }   // blockiert: die Mauer laeuft dort durch
					used.insert(keyOf(t));
					towerCells.insert(keyOf(t));
					Placement p;
					p.stats = tw; p.tile = t; p.tile2 = t; p.direction = cornerDir[ci];
					out.push_back(p);
				}
			}
			std::vector<std::vector<Vector2i>> pieces;   // zusammenhaengende Mauerstuecke (zwischen den Toren)
			std::vector<Vector2i> cur;
			auto flush = [&]() { if (!cur.empty()) { pieces.push_back(cur); cur.clear(); } };
			size_t i = 0;
			while (i < ring.size())
			{
				const Cell &c = ring[i];
				const Vector2i t(c.x, c.y);
				if (inGate(b, k, c)) { flush(); ++i; continue; }
				if (towerCells.count(keyOf(t)) != 0) { flush(); ++i; continue; }   // Eckturm
				// frei - oder schon ein eigenes Bauwerk dort (Linienbau ueberspringt es, die Mauer bleibt durchgehend)
				if (freeFor(player, st, t, 0, used) || (used.count(keyOf(t)) == 0 && ownStructAt(player, t) != nullptr)) { cur.push_back(t); used.insert(keyOf(t)); ++i; continue; }
				// blockiert: naechstes freies Feld dieses Laufs suchen (nicht ueber ein Tor hinweg)
				size_t j = i + 1;
				auto stopAt = [&](size_t jj) { return inGate(b, k, ring[jj]) || towerCells.count(TileKey(ring[jj].x, ring[jj].y)) != 0; };
				while (j < ring.size() && !stopAt(j) && !wallPassable(Vector2i(ring[j].x, ring[j].y))) { ++j; }
				const bool haveNext = j < ring.size() && !stopAt(j);
				std::vector<Vector2i> detour;
				if (!cur.empty() && haveNext)
				{
					const Vector2i nextT(ring[j].x, ring[j].y);
					if (routeTiles(cur.back(), nextT, wallPassable, static_cast<int>(j - i) * 2 + 8, detour))
					{
						for (const Vector2i &d : detour) { cur.push_back(d); used.insert(keyOf(d)); }
						i = j;   // weiter mit dem freien Feld hinter dem Hindernis (gehoert zum selben Stueck)
						continue;
					}
				}
				// kein Umweg: Luecke (Stueck endet hier, verkuerzt)
				flush();
				i = j;
			}
			flush();
			for (const auto &piece : pieces)
			{
				for (const auto &run : straightRuns(piece))
				{
					Placement p;
					p.stats = st; p.tile = run.first; p.tile2 = run.second; p.line = true; p.direction = 0;
					out.push_back(p);
					if (static_cast<int>(out.size()) >= MAX_PLACEMENTS) { break; }
				}
				if (static_cast<int>(out.size()) >= MAX_PLACEMENTS) { break; }
			}
			continue;
		}
		for (size_t i = 0; i < ring.size(); i++)
		{
			const Cell &c = ring[i];
			if (rs.step == 2)
			{
				// Schachbrett nach Feldkoordinaten: die Nachbarreihe (anderer offset) liegt genau versetzt dazwischen
				if ((((c.x + c.y + rs.offset) % 2) + 2) % 2 != 0) { continue; }
			}
			else if (static_cast<int>((i + rs.offset) % rs.step) != 0) { continue; }
			if (rs.gates && inGate(b, rs.k, c)) { continue; }
			const unsigned dir = outwardDir(c.side);
			// MOD 16.11.: blockiert? auf das naechste freie Feld ausweichen - erst entlang des Rings, dann nach aussen/innen
			static const int TAN[4][2] = { {1, 0}, {0, 1}, {-1, 0}, {0, -1} };
			static const int NRM[4][2] = { {0, -1}, {1, 0}, {0, 1}, {-1, 0} };
			const int tx = TAN[c.side & 3][0], ty = TAN[c.side & 3][1], nx = NRM[c.side & 3][0], ny = NRM[c.side & 3][1];
			const int shiftsAll[][2] = { {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {2, 0}, {-2, 0}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1} };
			// (18.11.) Panzersperren bleiben in ihrer Reihe (blockiert = auslassen) - Ausweichen machte das Feld unordentlich
			const bool rowItem = rs.role == Role::Hedgehog || rs.role == Role::Dragon;
			const size_t nShifts = rowItem ? 1u : sizeof(shiftsAll) / sizeof(shiftsAll[0]);
			const std::vector<std::array<int, 2>> shifts = [&]() {
				std::vector<std::array<int, 2>> v;
				for (size_t q = 0; q < nShifts; ++q) { v.push_back({shiftsAll[q][0], shiftsAll[q][1]}); }
				return v;
			}();
			if (ownStructAt(player, Vector2i(c.x, c.y)) != nullptr) { continue; }   // steht schon (erneuter Befehl)
			bool already = false;   // frueher schon ausgewichen? dann nicht noch einmal
			for (const auto &sh : shifts)
			{
				const STRUCTURE *os = ownStructAt(player, Vector2i(c.x + tx * sh[0] + nx * sh[1], c.y + ty * sh[0] + ny * sh[1]));
				if (os != nullptr && os->pStructureType == st) { already = true; break; }
			}
			if (already) { continue; }
			bool placed = false;
			Vector2i at(c.x, c.y);
			for (const auto &sh : shifts)
			{
				const Vector2i t(c.x + tx * sh[0] + nx * sh[1], c.y + ty * sh[0] + ny * sh[1]);
				if (ringDist(t) < 1) { continue; }
				if (freeFor(player, st, t, dir, used)) { at = t; placed = true; break; }
			}
			if (!placed) { continue; }
			used.insert(keyOf(at));
			Placement p;
			p.stats = st; p.tile = at; p.tile2 = p.tile; p.direction = dir;
			out.push_back(p);
			if (static_cast<int>(out.size()) >= MAX_PLACEMENTS) { break; }
		}
		if (static_cast<int>(out.size()) >= MAX_PLACEMENTS) { break; }
	}
	return out;
}

void onPlayerBuildOrder(DROID *psDroid, const DroidOrder &order, bool queued)
{
	if (psDroid == nullptr || psDroid->died || !isInfantryBuilder(psDroid)) { return; }
	if (order.type != DORDER_BUILD || order.psStats == nullptr) { return; }
	const auto *core = static_cast<const STRUCTURE_STATS *>(order.psStats);
	const std::string id = core->id.toUtf8();
	const bool nest = id.rfind("A0BaBaMGNest", 0) == 0;
	const bool pit = id.rfind("A0BaBaPit", 0) == 0;
	if (!nest && !pit) { return; }   // nur Kern-Stellungen loesen einen Komplex aus (Graeben/Kisten nicht)
	if (id.find("Post") != std::string::npos) { return; }   // (17.11. 16:10) einzelnes MG am Graben: kein eigener Komplex
	const unsigned player = psDroid->player;
	if (player >= MAX_PLAYERS) { return; }
	(void)queued;
	// Richtung der Stellung = Front; vorne/seitlich in Kachelschritten
	const unsigned q = ((static_cast<unsigned>(order.direction) + 0x2000u) >> 14) & 3u;   // 0 N, 1 O, 2 S, 3 W
	static const int FWD[4][2] = { {0, -1}, {1, 0}, {0, 1}, {-1, 0} };
	const int fx = FWD[q][0], fy = FWD[q][1];
	const int sx = -fy, sy = fx;   // seitlich (rechts)
	const Vector2i c(map_coord(order.pos.x), map_coord(order.pos.y));
	auto tileAt = [&](int side, int fwd) { return Vector2i(c.x + sx * side + fx * fwd, c.y + sy * side + fy * fwd); };
	auto stat = [&](const char *a, const char *b) -> STRUCTURE_STATS * {
		for (const char *nm : {a, b})
		{
			if (nm == nullptr) { continue; }
			STRUCTURE_STATS *st = getStructStatsFromName(WzString::fromUtf8(nm));
			if (st == nullptr) { continue; }
			const int idx = static_cast<int>(st - asStructureStats);
			if (idx < 0 || idx >= static_cast<int>(numStructureStats)) { continue; }
			const int status = apStructTypeLists[player][idx];
			if (status == AVAILABLE || status == REDUNDANT) { return st; }
		}
		return nullptr;
	};
	STRUCTURE_STATS *trench = stat("A0BaBaTrench", "A0BaBaTrenchEarth");
	STRUCTURE_STATS *box = stat("A0BaBaAmmoBox", nullptr);
	// MOD (Wunsch 17.11. 16:10): "Die Sandsackstellungen ... haben ab und zu - je nach Bewaffnung der Infanterie - die Scav-Raketenstellung,
	// oder die Scav-Panzerabwehrstellung, oder das Scav-MG (mit Schuetze) ... Pro Netzwerk mindestens 2, manchmal 3 Verteidigungswaffen."
	// Die Gruben des Komplexes sind quadratisch, sechseckig oder rund (Zufall, gameRand in fester Reihenfolge -> auf allen Rechnern gleich).
	// Bewaffnung nach der Waffe des Bauenden: Raketen -> Raketenstellung, Panzerabwehr (Rakete/Kanone) -> AT-Raketenstellung, sonst MG.
	// Das MG darf auch ohne Grube direkt neben dem Verbindungsgraben stehen (A0BaBaMGNestPost).
	enum ModArm { ARM_MG, ARM_ROCKET, ARM_AT };
	ModArm arm = ARM_MG;
	if (psDroid->numWeaps > 0 && psDroid->asWeaps[0].nStat > 0)
	{
		const WEAPON_STATS &ws = asWeaponStats[psDroid->asWeaps[0].nStat];
		const bool rocket = ws.weaponSubClass == WSC_ROCKET || ws.weaponSubClass == WSC_MISSILE || ws.weaponSubClass == WSC_SLOWROCKET
		                    || ws.weaponSubClass == WSC_SLOWMISSILE;
		if (rocket) { arm = ws.weaponEffect == WE_ANTI_TANK ? ARM_AT : ARM_ROCKET; }
		else if (ws.weaponSubClass == WSC_CANNON) { arm = ARM_AT; }
	}
	static const char *const SHAPE[3] = {"Square", "Hex", "Round"};
	auto pitStat = [&](int shape, bool armed) -> STRUCTURE_STATS * {
		std::string n = armed ? (arm == ARM_MG ? std::string("A0BaBaMGNest") + SHAPE[shape]
		                                       : std::string("A0BaBaPit") + SHAPE[shape] + (arm == ARM_AT ? "AT" : "Rkt"))
		                      : std::string("A0BaBaPit") + SHAPE[shape];
		STRUCTURE_STATS *st = stat(n.c_str(), nullptr);
		if (st == nullptr && armed) { st = stat(arm == ARM_MG ? "A0BaBaMGNestSquare" : (arm == ARM_AT ? "A0BaBaPitSquareAT" : "A0BaBaPitSquareRkt"), "A0BaBaMGNestHex"); }
		if (st == nullptr) { st = stat("A0BaBaPitSquare", "A0BaBaPitHex"); }
		return st;
	};
	const bool coreArmed = nest || (id.size() > 3 && (id.compare(id.size() - 2, 2, "AT") == 0 || id.compare(id.size() - 3, 3, "Rkt") == 0));
	const int total = 2 + (gameRand(3) == 0 ? 1 : 0);
	int need = std::max(0, total - (coreArmed ? 1 : 0));
	STRUCTURE_STATS *post = nullptr;
	if (arm == ARM_MG && need > 0 && gameRand(2) == 0)
	{
		post = stat("A0BaBaMGNestPost", nullptr);
		if (post != nullptr) { --need; }
	}
	bool armedSlot[3] = {false, false, false};   // links, rechts, hinten
	if (need >= 2) { armedSlot[0] = armedSlot[1] = true; }
	else if (need == 1) { armedSlot[gameRand(2)] = true; }
	if (need >= 3) { armedSlot[2] = true; }
	STRUCTURE_STATS *slotStat[3];
	for (int k = 0; k < 3; ++k) { slotStat[k] = pitStat(static_cast<int>(gameRand(3)), armedSlot[k]); }
	// MOD (Wunsch 16.11. 17:14 + 17:54): Graben-Komplex, dynamisch um Hindernisse herum.
	//      [A]==[=][=]( K )[=][=]==[B]      vorne (Front)
	//                  [=]
	//            [M]   [=]
	//                  [=][=][=][A]         hinten, versetzt (90-Grad-Abzweigung)
	// Ist ein Wunschfeld blockiert (Gelaende, Steine, Wracks, Gebaeude), rutscht die Stellung auf ein freies Feld in der Naehe
	// und der Graben laeuft um die Ecke dorthin (laenger); gibt es keinen Weg, wird der Graben verkuerzt.
	const unsigned dir = order.direction;
	const Vector2i coreT = c;
	TileSet used;
	used.insert(keyOf(coreT));
	auto inWindow = [&](const Vector2i &t) { return std::abs(t.x - coreT.x) <= 7 && std::abs(t.y - coreT.y) <= 7; };
	auto worldOf = [](const Vector2i &t) { return Vector2i(world_coord(t.x) + TILE_UNITS / 2, world_coord(t.y) + TILE_UNITS / 2); };
	auto issueTrench = [&](const std::vector<Vector2i> &cells) {
		if (trench == nullptr) { return; }
		for (const auto &run : straightRuns(cells))
		{
			DroidOrder o(DORDER_LINEBUILD, trench, worldOf(run.first), worldOf(run.second), static_cast<uint16_t>(dir));
			orderDroidAdd(psDroid, &o);
		}
	};
	auto issueOne = [&](STRUCTURE_STATS *st, const Vector2i &t) {
		DroidOrder o(DORDER_BUILD, st, worldOf(t), static_cast<uint16_t>(dir));
		orderDroidAdd(psDroid, &o);
	};
	auto trenchFree = [&](const Vector2i &t) { return inWindow(t) && freeFor(player, trench, t, dir, used); };
	auto chain = [&](const Vector2i &anchor, const std::vector<Vector2i> &plan, STRUCTURE_STATS *posSt, const Vector2i &posWant) -> std::vector<Vector2i> {
		std::vector<Vector2i> cells;
		Vector2i pos = posWant;
		bool havePos = false;
		const bool planFree = trench != nullptr && std::all_of(plan.begin(), plan.end(), trenchFree);
		if (planFree && (posSt == nullptr || (inWindow(posWant) && freeFor(player, posSt, posWant, dir, used))))
		{
			cells = plan;
			havePos = posSt != nullptr;
		}
		else if (trench != nullptr)
		{
			bool done = false;
			if (posSt != nullptr)
			{
				// Stellung auf ein freies Feld nahe dem Wunschfeld, Graben um die Hindernisse herum dorthin
				for (const Vector2i &cand : candidatesAround(posWant, 2))
				{
					if (!inWindow(cand) || !freeFor(player, posSt, cand, dir, used)) { continue; }
					std::vector<Vector2i> r;
					if (routeTiles(anchor, cand, trenchFree, static_cast<int>(plan.size()) + 6, r))
					{
						cells = r; pos = cand; havePos = true; done = true;
						break;
					}
				}
			}
			if (!done)
			{
				// kein Weg: verkuerzen - der zusammenhaengende freie Anfang der Wunschform, Stellung ans Ende, wenn dort Platz ist
				for (const Vector2i &t : plan)
				{
					if (!trenchFree(t)) { break; }
					cells.push_back(t);
				}
				if (posSt != nullptr)
				{
					const Vector2i end = cells.empty() ? anchor : cells.back();
					for (const Vector2i &cand : candidatesAround(end, 1))
					{
						if (manhattan(cand, end) != 1 || !inWindow(cand)) { continue; }
						if (std::find(cells.begin(), cells.end(), cand) != cells.end()) { continue; }
						if (freeFor(player, posSt, cand, dir, used)) { pos = cand; havePos = true; break; }
					}
				}
			}
		}
		else if (posSt != nullptr && inWindow(posWant) && freeFor(player, posSt, posWant, dir, used))
		{
			havePos = true;
		}
		for (const Vector2i &t : cells) { used.insert(keyOf(t)); }
		if (havePos) { used.insert(keyOf(pos)); }
		issueTrench(cells);
		if (havePos) { issueOne(posSt, pos); }
		return cells;
	};

	chain(coreT, { tileAt(-1, 0), tileAt(-2, 0) }, slotStat[0], tileAt(-3, 0));
	chain(coreT, { tileAt(1, 0), tileAt(2, 0) }, slotStat[1], tileAt(3, 0));
	const std::vector<Vector2i> back = chain(coreT, { tileAt(0, -1), tileAt(0, -2), tileAt(0, -3), tileAt(1, -3), tileAt(2, -3) }, slotStat[2], tileAt(3, -3));
	// (17.11. 16:10) einzelnes MG mit Schuetze direkt am Verbindungsgraben (seitlich, Wunschfeld neben dem 3. Grabenfeld)
	if (post != nullptr && !back.empty())
	{
		static const int D4[4][2] = { {-1, 0}, {1, 0}, {0, 1}, {0, -1} };
		bool placed = false;
		const size_t n = back.size(), start = std::min<size_t>(2, n - 1);
		for (size_t j = 0; j < n && !placed; ++j)
		{
			const Vector2i &t = back[(start + j) % n];
			for (const auto &d : D4)
			{
				const Vector2i cand(t.x + d[0], t.y + d[1]);
				if (!inWindow(cand) || !freeFor(player, post, cand, dir, used)) { continue; }
				used.insert(keyOf(cand));
				issueOne(post, cand);
				placed = true;
				break;
			}
		}
	}
	// Munitionskiste direkt am Verbindungsgraben (Wunschfeld neben dem 2. Grabenfeld, sonst ein freies Nachbarfeld des Grabens)
	if (box != nullptr)
	{
		std::vector<Vector2i> cands = { tileAt(-1, -2) };
		static const int D4[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
		const std::vector<Vector2i> &touch = back.empty() ? std::vector<Vector2i>{ coreT } : back;
		for (const Vector2i &t : touch)
		{
			for (const auto &d : D4) { cands.emplace_back(t.x + d[0], t.y + d[1]); }
		}
		for (const Vector2i &cand : cands)
		{
			bool adjacent = false;
			for (const Vector2i &t : touch) { if (manhattan(t, cand) == 1) { adjacent = true; break; } }
			if (!adjacent || !inWindow(cand) || !freeFor(player, box, cand, dir, used)) { continue; }
			used.insert(keyOf(cand));
			issueOne(box, cand);
			break;
		}
	}
}

bool canFortify(const DROID *psDroid)
{
	if (psDroid == nullptr || psDroid->died || !psDroid->isConstructionDroid()) { return false; }
	if (psDroid->isVtol() || psDroid->isTransporter()) { return false; }
	return psDroid->numWeaps == 0 || psDroid->asWeaps[0].nStat == 0;
}

bool keysForFortify()
{
	const bool ctrl = keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL);
	const bool shift = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	return ctrl && !shift;
}

bool orderSelected(unsigned player, int worldX, int worldY)
{
	if (player >= MAX_PLAYERS) { return false; }
	if (modlayermap::viewLayer() != modlayer::SURFACE) { return false; }
	if (!worldOnMap(gameWorld.map, worldX, worldY)) { return false; }
	bool any = false;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		// (18.11.) Pickup/Tanker bauen nicht mehr (kein Baufahrzeug) - beladene Lkw laden mit Strg + Klick trotzdem hier ab
		if (d == nullptr || !d->selected || !(canFortify(d) || modcrates::wantsUnload(d))) { continue; }
		DroidOrder sOrder(DORDER_FORTIFY, Vector2i(worldX, worldY));
		// MOD (16.11.): Lkw mit Munitionskisten laden sie hier ab statt eine Befestigung zu bauen
		sOrder.direction = modcrates::wantsUnload(d) ? modcrates::CODE_UNLOAD : static_cast<uint16_t>(s_tier);
		sendDroidInfo(d, sOrder, false, currentOrderSource());
		any = true;
	}
	return any;
}

void startOrder(DROID *psDroid, int worldX, int worldY, int tier)
{
	if (psDroid == nullptr || !canFortify(psDroid)) { return; }
	psDroid->order = DroidOrder(DORDER_NONE);
	actionDroid(psDroid, DACTION_NONE);
	Job j;
	j.player = psDroid->player;
	j.center = Vector2i(worldX, worldY);
	j.tier = std::clamp(tier, 1, 3);
	j.startTime = gameTime;
	s_jobs[psDroid->id] = j;
}

namespace { const std::vector<Placement> *s_external = nullptr; }   // (18.11.) fremder Bauplan als Vorschau

void updatePreview(bool wanted, const Vector2i &worldPos)
{
	bool show = false;
	// (18.11. 12:14) waehrend der Schlachtplan einen Bauplan zeigt (Arty base), keine Befestigungs-Vorschau daneben
	if (wanted && selectedPlayer < MAX_PLAYERS && keysForFortify() && s_external == nullptr)
	{
		for (const DROID *d : gameWorld.objects.droids[selectedPlayer])
		{
			if (d != nullptr && d->selected && canFortify(d)) { show = true; break; }
		}
	}
	if (show)
	{
		if (mousePressed(MOUSE_WUP)) { s_tier = std::min(3, s_tier + 1); }
		if (mousePressed(MOUSE_WDN)) { s_tier = std::max(1, s_tier - 1); }
		range_rings::setDebugRange(static_cast<float>(worldPos.x), static_cast<float>(worldPos.y), static_cast<float>(radiusOfTier(s_tier) * TILE_UNITS));
		s_ringOn = true;
		// Vorschau des Bauplans (nur Anzeige; planFor veraendert nichts und wuerfelt nicht)
		const Vector2i tile = map_coord(worldPos);
		if (tile != s_previewTile || s_tier != s_previewTier || realTime - s_previewAt > 1500)
		{
			s_previewTile = tile;
			s_previewTier = s_tier;
			s_previewAt = realTime;
			s_preview = (modlayermap::viewLayer() == modlayer::SURFACE && worldOnMap(gameWorld.map, worldPos.x, worldPos.y))
			            ? planFor(selectedPlayer, worldPos, s_tier) : std::vector<Placement>();
		}
		s_previewOn = true;
	}
	else if (s_ringOn)
	{
		range_rings::setDebugRange(0.f, 0.f, 0.f);
		s_ringOn = false;
		s_previewOn = false;
		s_preview.clear();
		s_previewTile = Vector2i(-1, -1);
	}
}

bool gateFromWall(unsigned player, const STRUCTURE *w, bool add)
{
	if (w == nullptr || player >= MAX_PLAYERS || w->player != static_cast<int>(player) || w->status != SS_BUILT) { return false; }
	if (w->pStructureType == nullptr || w->pStructureType->type != REF_WALL || w->pFunctionality == nullptr) { return false; }
	const std::string id = w->pStructureType->id.toUtf8();
	if (id.find("Pipeline") != std::string::npos) { return false; }
	if (w->pFunctionality->wall.type != 0) { return false; }   // nur gerade Stuecke (keine Ecken, T-Stuecke, Kreuzungen)
	const bool scav = id.find("BaBa") != std::string::npos || id.find("Baba") != std::string::npos;
	STRUCTURE_STATS *gate = nullptr;
	for (unsigned i = 0; i < numStructureStats; i++)
	{
		STRUCTURE_STATS *st = &asStructureStats[i];
		if (st->type != REF_GATE) { continue; }
		const std::string gid = st->id.toUtf8();
		const bool scavGate = gid.find("BaBa") != std::string::npos || gid.find("Baba") != std::string::npos;
		if (scavGate == scav) { gate = st; break; }
		if (gate == nullptr) { gate = st; }
	}
	if (gate == nullptr) { return false; }
	orderSelectedStatsLocDir(player, DORDER_BUILD, gate, w->pos.x, w->pos.y, w->rot.direction, add);
	return true;
}

void setExternalPreview(const std::vector<Placement> *plan)
{
	s_external = plan;
}

const std::vector<Placement> *previewPlan()
{
	if (s_external != nullptr && !s_external->empty()) { return s_external; }   // (18.11.) Bauplan aus dem Schlachtplan (Arty base)
	return (s_previewOn && !s_preview.empty()) ? &s_preview : nullptr;
}

const char *activeLabel()
{
	if (!s_ringOn) { return nullptr; }
	switch (s_tier)
	{
	case 1: return "Fortify: small  (Ctrl + mouse wheel: size)";
	case 3: return "Fortify: large  (Ctrl + mouse wheel: size)";
	default: return "Fortify: medium  (Ctrl + mouse wheel: size)";
	}
}

// MOD (Wunsch 20.11. 16:42 "Die 3 Baubagger sollen mehr eigenstaendig arbeiten ..."): ein Baufahrzeug, das gerade seinen Bau beendet hat
// (und nichts mehr in der Warteschlange), hilft nach einer Sekunde von selbst beim naechsten eigenen Bau in der Naehe (12 Felder) mit -
// nicht, wenn der Spieler es zuletzt woandershin geschickt hat (dann bleibt es dort). Abrissbaustellen zaehlen nicht. Spiellogik im
// Spielschritt, in fester Reihenfolge (auf allen Rechnern gleich).
namespace
{
struct Helper
{
	bool wasBuilding = false;   // letzter Auftrag war ein Bau (dann darf es danach helfen)
	uint32_t idleSince = 0;
};
std::map<uint32_t, Helper> s_helpers;
uint32_t s_nextHelp = 0, s_nextHelperCleanup = 0;
constexpr unsigned HELP_TICK_MS = 500, HELP_IDLE_MS = 1000;
constexpr int HELP_RADIUS = 12 * DIST_TILE;

bool buildOrder(DROID_ORDER o)
{
	return o == DORDER_BUILD || o == DORDER_LINEBUILD || o == DORDER_HELPBUILD || o == DORDER_BUILDMODULE;
}

void autoHelp(GameWorld &world)
{
	if (gameTime < s_nextHelp) { return; }
	s_nextHelp = gameTime + HELP_TICK_MS;
	const bool cleanup = gameTime >= s_nextHelperCleanup;
	std::map<uint32_t, Helper> alive;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		if (!isHumanPlayer(p)) { continue; }
		for (DROID *d : world.objects.droids[p])
		{
			if (d->died || (d->droidType != DROID_CONSTRUCT && d->droidType != DROID_CYBORG_CONSTRUCT)) { continue; }
			Helper &h = s_helpers[d->id];
			const DROID_ORDER o = d->order.type;
			if (buildOrder(o)) { h.wasBuilding = true; h.idleSince = 0; }
			else if (o != DORDER_NONE) { h.wasBuilding = false; h.idleSince = 0; }   // anderer Auftrag (Fahrt ...): dort bleiben
			else if (h.wasBuilding && d->listPendingBegin >= d->asOrderList.size() && d->action == DACTION_NONE)
			{
				if (h.idleSince == 0) { h.idleSince = gameTime; }
				else if (gameTime - h.idleSince >= HELP_IDLE_MS)
				{
					h.wasBuilding = false;   // einmal suchen; findet es nichts, bleibt es stehen
					h.idleSince = 0;
					STRUCTURE *best = nullptr;
					int64_t bestD = static_cast<int64_t>(HELP_RADIUS) * HELP_RADIUS;
					for (STRUCTURE *s : world.objects.structures[p])
					{
						if (s->died || s->status != SS_BEING_BUILT || modlayer::layerOf(s) != modlayer::layerOf(d)) { continue; }
						if (checkDroidsDemolishing(s)) { continue; }
						const int64_t dx = s->pos.x - d->pos.x, dy = s->pos.y - d->pos.y, dd = dx * dx + dy * dy;
						if (dd < bestD) { bestD = dd; best = s; }
					}
					if (best != nullptr)
					{
						orderDroidObj(d, DORDER_HELPBUILD, best, ModeImmediate);
						h.wasBuilding = true;   // nach dem Helfen wieder suchen (naechste Baustelle)
					}
				}
			}
			if (cleanup) { alive[d->id] = h; }
		}
	}
	if (cleanup)
	{
		s_helpers.swap(alive);
		s_nextHelperCleanup = gameTime + 30000;
	}
}
} // namespace

void tick(GameWorld &world)
{
	autoHelp(world);   // (20.11.) Baufahrzeuge helfen nach ihrem Bau in der Naehe mit
	if (s_jobs.empty()) { return; }
	std::vector<uint32_t> done;
	for (auto &kv : s_jobs)
	{
		Job &j = kv.second;
		DROID *d = IdToDroid(world.objects, kv.first, j.player);
		if (d == nullptr || d->died) { done.push_back(kv.first); continue; }
		const int radius = radiusOfTier(j.tier);

		// ein anderer Befehl des Spielers beendet die Arbeit
		if (d->order.type != DORDER_NONE)
		{
			if (!(j.moving && d->order.type == DORDER_MOVE && d->order.pos == j.moveTarget))
			{
				done.push_back(kv.first);
				continue;
			}
			continue;   // laeuft zur Mulde
		}
		j.moving = false;

		// Phase 1: Mulden im Kreis zuschaufeln
		Vector2i dent{0, 0};
		if (modterrain::findDent(j.center, radius, 6, dent))
		{
			const Vector2i dw(world_coord(dent.x) + TILE_UNITS / 2, world_coord(dent.y) + TILE_UNITS / 2);
			const int dx = d->pos.x - dw.x, dy = d->pos.y - dw.y;
			if (dx * dx + dy * dy > (DIST_TILE * 2) * (DIST_TILE * 2))
			{
				j.moving = true;
				j.moveTarget = dw;
				orderDroidLoc(d, DORDER_MOVE, dw.x, dw.y, ModeImmediate);
			}
			else if (((gameTime / GAME_TICKS_PER_UPDATE) + kv.first) % 2 == 0)
			{
				modterrain::fillTile(dent.x, dent.y, 1);
			}
			if (gameTime - j.startTime > 5 * 60 * GAME_TICKS_PER_SEC) { done.push_back(kv.first); }   // Sicherung gegen Endlosschleife
			continue;
		}

		// Phase 2: Bauplan in die Auftragsliste (plus Deko im Niemandsland)
		const std::vector<Placement> plan = planFor(j.player, j.center, j.tier);
		placeDeco(j.player, j.center, j.tier);
		for (const Placement &p : plan)
		{
			const Vector2i w1(world_coord(p.tile.x) + TILE_UNITS / 2, world_coord(p.tile.y) + TILE_UNITS / 2);
			const Vector2i w2(world_coord(p.tile2.x) + TILE_UNITS / 2, world_coord(p.tile2.y) + TILE_UNITS / 2);
			DroidOrder o = p.line ? DroidOrder(DORDER_LINEBUILD, p.stats, w1, w2, static_cast<uint16_t>(p.direction))
			                      : DroidOrder(DORDER_BUILD, p.stats, w1, static_cast<uint16_t>(p.direction));
			orderDroidAdd(d, &o);
		}
		done.push_back(kv.first);
	}
	for (uint32_t id : done)
	{
		s_jobs.erase(id);
	}
	(void)world;
}

bool armWork(const DROID *psDroid, unsigned anim, float &yawDeg, float &pitchDeg)
{
	if (psDroid == nullptr || psDroid->droidType != DROID_CONSTRUCT) { return false; }
	const CONSTRUCT_STATS *cs = psDroid->getConstructStats();
	if (cs == nullptr) { return false; }
	if (psDroid->action != DACTION_BUILD && psDroid->action != DACTION_DEMOLISH && psDroid->action != DACTION_REPAIR) { return false; }
	const unsigned since = gameTime > psDroid->actionStarted ? gameTime - psDroid->actionStarted : 0;
	const float ramp = std::min(1.f, static_cast<float>(since) / 500.f);
	const float t = static_cast<float>((anim + psDroid->id * 397u) % 100000u);
	if (cs->id.startsWith("WreckBall"))
	{
		// (18.11. 12:22) der Kran dreht sich nur leicht mit - die Birne selbst pendelt (ballSwing)
		yawDeg = ramp * 8.f * std::sin(t * (6.2831853f / 4800.f));
		pitchDeg = 0.f;
	}
	else
	{
		yawDeg = ramp * 22.f * std::sin(t * (6.2831853f / 2600.f));
		pitchDeg = ramp * 6.f * std::sin(t * (6.2831853f / 1300.f) + 0.8f);
	}
	return true;
}

int demolishFactor(const DROID *psDroid, const STRUCTURE *psTarget)
{
	const CONSTRUCT_STATS *cs = psDroid != nullptr ? psDroid->getConstructStats() : nullptr;
	if (cs == nullptr || !cs->id.startsWith("WreckBall")) { return 1; }
	// (Wunsch 18.11. 12:22) "Zweck: Gebaeude abreissen, die sehr gross sind und aus Beton - nicht die kleineren, fragiler wirkenden
	// Scav-Gebaeude, eher die Project-Gebaeude und so weiter": grosse/Beton-Bauwerke 8-fach, kleine Scav-Bauwerke nur normal
	if (psTarget != nullptr && psTarget->pStructureType != nullptr)
	{
		const STRUCTURE_STATS *st = psTarget->pStructureType;
		const std::string id = st->id.toUtf8();
		const bool scav = id.find("BaBa") != std::string::npos || id.find("Baba") != std::string::npos || id.find("Scav") != std::string::npos;
		const bool big = st->baseWidth * st->baseBreadth >= 4;
		if (scav && !big) { return 1; }
	}
	return 8;
}

float ballSwing(const DROID *psDroid, unsigned anim)
{
	// Pendel der Abrissbirne (Grad um die Querachse; positiv = nach vorn). Beim Abreissen grosse Schwuenge, sonst leichtes Nachpendeln.
	if (psDroid == nullptr) { return 0.f; }
	const float t = static_cast<float>((anim + psDroid->id * 613u) % 1000000u);
	if (psDroid->action == DACTION_DEMOLISH)
	{
		const unsigned since = gameTime > psDroid->actionStarted ? gameTime - psDroid->actionStarted : 0;
		const float ramp = std::min(1.f, static_cast<float>(since) / 900.f);
		// Ausholen nach hinten, Schlag nach vorn (Kosinus mit verstaerkter Vorwaertshaelfte)
		const float c = std::cos(t * (6.2831853f / 2200.f));
		return ramp * (c > 0.f ? 46.f * c : 30.f * c);
	}
	const bool moving = psDroid->sMove.Status != MOVEINACTIVE;
	return (moving ? 9.f : 3.f) * std::sin(t * (6.2831853f / (moving ? 1700.f : 3100.f)));
}

unsigned craneBoost(const DROID *psDroid, unsigned points)
{
	// Kranliste je Spielsekunde neu aufbauen (nur Spielzeit, deshalb auf allen Rechnern gleich)
	static unsigned s_stamp = ~0u;
	static std::vector<Vector3i> s_cranes[MAX_PLAYERS];
	if (psDroid == nullptr) { return points; }
	const unsigned stamp = gameTime / GAME_TICKS_PER_SEC;
	if (stamp != s_stamp)
	{
		s_stamp = stamp;
		for (int p = 0; p < MAX_PLAYERS; p++)
		{
			s_cranes[p].clear();
			for (const STRUCTURE *s : gameWorld.objects.structures[p])
			{
				if (s->status == SS_BUILT && s->pStructureType->type == REF_DEFENSE && s->pStructureType->id.startsWith("A0BaBaCrane"))
				{
					s_cranes[p].push_back(s->pos);
				}
			}
		}
	}
	constexpr int R = 9 * DIST_TILE;
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		if (s_cranes[p].empty() || (p != static_cast<int>(psDroid->player) && !aiCheckAlliances(p, psDroid->player))) { continue; }
		for (const Vector3i &c : s_cranes[p])
		{
			const int dx = c.x - psDroid->pos.x, dy = c.y - psDroid->pos.y;
			if (dx * dx + dy * dy <= R * R) { return points + points / 4; }
		}
	}
	return points;
}

void reset()
{
	s_preview.clear();
	s_previewOn = false;
	s_previewTile = Vector2i(-1, -1);
	s_jobs.clear();
	s_helpers.clear();   // (20.11.)
	s_nextHelp = 0;
	s_nextHelperCleanup = 0;
	s_tier = 2;
	if (s_ringOn)
	{
		range_rings::setDebugRange(0.f, 0.f, 0.f);
		s_ringOn = false;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// MOD (Wunsch 20.11. "mit defensive line. Sie soll eigentlich so ein Netz bilden wie die von der Infanterie gebauten Netze, nur mit der
// Eigenschaft, dass die laengste Linie entlang der gezogenen blauen Verteidigungslinie laeuft und alle Stellungen mit der Front zu den
// blauen Staeben gerichtet sind."). Bisher gruben die Bauer gerade Grabenstuecke zwischen wenigen Stuetzpunkten der Linie: schraege
// Stuecke wurden zu Treppen (Felder nur ueber Eck = nicht verbunden), die Stuecke trafen sich nicht, alles blickte nach Sueden.
// Jetzt (alles in Plan-Kacheln = Original-Kacheln, siehe tileOk):
//  - die Linie wird zu einer lueckenlosen Feldfolge (nur waagerecht/senkrecht benachbart; Ecken bekommen ein Zwischenfeld), Hindernisse
//    werden umgangen, Schleifen abgeschnitten - das ist der Hauptgraben (der laengste Strang des Netzes),
//  - im Hauptgraben etwa alle 5 Felder eine Grube, an beiden Enden eine; abwechselnd bewaffnet (MG, Raketen, Panzerabwehr - je nach
//    Forschung) und unbewaffnet,
//  - von den unbewaffneten Gruben laeuft ein Verbindungsgraben nach hinten (3 Felder zur eigenen Seite, 2 seitlich, am Ende eine
//    Reservegrube) wie beim Infanterie-Netz, daneben Munitionskiste und manchmal ein einzelnes MG,
//  - jedes Bauwerk blickt zur Feindseite (Seite der Querstriche) - die Eingaenge liegen damit hinten (modfort).
// Alle Teile sind Linienbau-Auftraege (auch Einzelstuecke: Anfang = Ende) - so loest der Bau einer Grube durch Infanterie keinen
// zusaetzlichen Infanterie-Komplex aus.
namespace
{
STRUCTURE_STATS *availStat(unsigned player, const std::string &name)
{
	STRUCTURE_STATS *st = getStructStatsFromName(WzString::fromUtf8(name));
	if (st == nullptr) { return nullptr; }
	const int idx = static_cast<int>(st - asStructureStats);
	if (idx < 0 || idx >= static_cast<int>(numStructureStats)) { return nullptr; }
	const int status = apStructTypeLists[player][idx];
	return (status == AVAILABLE || status == REDUNDANT) ? st : nullptr;
}

/// Bauwerks-Richtung, mit der ein Bauwerk zur Seite k blickt (0 N, 1 O, 2 S, 3 W): Richtung 0 = Sueden (+y), 0x4000 = Osten (modfort)
uint16_t dirOfSide(int k)
{
	static const uint16_t D[4] = {0x8000u, 0x4000u, 0x0000u, 0xC000u};
	return D[k & 3];
}

const int SDX[4] = {0, 1, 0, -1};
const int SDY[4] = {-1, 0, 1, 0};

int64_t dist2i(const Vector2i &a, const Vector2i &b)
{
	const int64_t dx = a.x - b.x, dy = a.y - b.y;
	return dx * dx + dy * dy;
}

/// fester "Zufall" aus den Feldkoordinaten (Vorschau und Bau ergeben denselben Plan)
uint32_t cellHash(const Vector2i &t, uint32_t salt)
{
	return (static_cast<uint32_t>(t.x) * 73856093u) ^ (static_cast<uint32_t>(t.y) * 19349663u) ^ (salt * 83492791u);
}
}

std::vector<Placement> planDefenseNet(unsigned player, const std::vector<Vector2i> &stroke, int side)
{
	std::vector<Placement> out;
	if (player >= MAX_PLAYERS || stroke.empty()) { return out; }
	STRUCTURE_STATS *trench = availStat(player, "A0BaBaTrench");
	if (trench == nullptr) { trench = availStat(player, "A0BaBaTrenchEarth"); }
	if (trench == nullptr) { trench = availStat(player, "A0BaBaSandbagWall"); }
	if (trench == nullptr) { return out; }
	side = side < 0 ? -1 : 1;
	constexpr size_t MAX_LINE = 80;      // Felder Hauptgraben
	constexpr int MAX_BRANCHES = 6;

	// 1) Linie -> lueckenlose Feldfolge in 4er-Nachbarschaft (Stueckchen von einer Viertel-Kachel abtasten)
	auto tileOf = [](const Vector2i &w) { return Vector2i(std::max(0, w.x) / DIST_TILE, std::max(0, w.y) / DIST_TILE); };
	auto worldOf = [](const Vector2i &t) { return Vector2i(t.x * DIST_TILE + DIST_TILE / 2, t.y * DIST_TILE + DIST_TILE / 2); };
	std::vector<Vector2i> path;
	std::vector<Vector2i> dense;   // abgetastete Linie (fuer die Blickrichtung)
	auto push = [&](const Vector2i &t) {
		const auto it = std::find(path.begin(), path.end(), t);
		if (it != path.end()) { path.erase(it + 1, path.end()); return; }   // Schleife: abschneiden
		path.push_back(t);
	};
	for (size_t i = 0; i < stroke.size() && path.size() < MAX_LINE; ++i)
	{
		const Vector2i a = i == 0 ? stroke[0] : stroke[i - 1], b = stroke[i];
		const int64_t len = static_cast<int64_t>(std::sqrt(static_cast<double>(dist2i(a, b))));
		const int steps = std::max<int>(1, static_cast<int>(len / (DIST_TILE / 4)));
		for (int k = (i == 0 ? 0 : 1); k <= steps && path.size() < MAX_LINE; ++k)
		{
			const Vector2i w(a.x + static_cast<int>((static_cast<int64_t>(b.x - a.x) * k) / steps), a.y + static_cast<int>((static_cast<int64_t>(b.y - a.y) * k) / steps));
			dense.push_back(w);
			const Vector2i t = tileOf(w);
			if (!path.empty() && path.back() == t) { continue; }
			if (!path.empty() && manhattan(path.back(), t) == 2 && path.back().x != t.x && path.back().y != t.y)
			{
				// schraeger Schritt: Zwischenfeld (das der Linie naehere der beiden Ecken), damit die Felder verbunden bleiben
				const Vector2i ca(t.x, path.back().y), cb(path.back().x, t.y);
				push(dist2i(worldOf(ca), w) <= dist2i(worldOf(cb), w) ? ca : cb);
			}
			else if (!path.empty() && manhattan(path.back(), t) > 1)
			{
				// (sehr schnelle Mausbewegung) Luecke: gerade Felder dazwischen
				Vector2i c = path.back();
				while (manhattan(c, t) > 1 && path.size() < MAX_LINE)
				{
					if (c.x != t.x) { c.x += (t.x > c.x) ? 1 : -1; } else { c.y += (t.y > c.y) ? 1 : -1; }
					push(c);
				}
			}
			push(t);
		}
	}
	if (path.empty() || dense.empty()) { return out; }

	// 2) Hindernisse umgehen (wie der Mauerring): blockiertes Feld -> Weg um das Hindernis zum naechsten freien Feld der Linie,
	//    geht es nicht, beginnt dort ein neues Stueck. Eigene Stellungen auf der Linie gehoeren dazu (werden nicht neu gebaut).
	TileSet used;
	auto ownFort = [&](const Vector2i &t) {
		const STRUCTURE *os = ownStructAt(player, t);
		return os != nullptr && modfort::isFort(os);
	};
	auto passable = [&](const Vector2i &t) { return used.count(keyOf(t)) == 0 && (freeFor(player, trench, t, 0, used) || ownFort(t)); };
	std::vector<std::vector<Vector2i>> pieces;
	{
		std::vector<Vector2i> cur;
		size_t i = 0;
		while (i < path.size())
		{
			const Vector2i t = path[i];
			if (passable(t)) { cur.push_back(t); used.insert(keyOf(t)); ++i; continue; }
			size_t j = i + 1;
			while (j < path.size() && j < i + 10 && !passable(path[j])) { ++j; }
			std::vector<Vector2i> detour;
			if (!cur.empty() && j < path.size() && passable(path[j])
			    && routeTiles(cur.back(), path[j], passable, static_cast<int>(j - i) * 2 + 6, detour))
			{
				for (const Vector2i &d : detour) { cur.push_back(d); used.insert(keyOf(d)); }
				i = j;
				continue;
			}
			if (!cur.empty()) { pieces.push_back(cur); cur.clear(); }
			i = j;
		}
		if (!cur.empty()) { pieces.push_back(cur); }
	}

	// Bewaffnung reihum nach Forschung; Grubenformen nach festem Zufall
	static const char *const SHAPE[3] = {"Square", "Hex", "Round"};
	auto pitFor = [&](const Vector2i &t, int armKind) -> STRUCTURE_STATS * {
		// armKind: -1 unbewaffnet, 0 MG, 1 Raketen, 2 Panzerabwehr
		const std::string shape = SHAPE[cellHash(t, 7) % 3u];
		std::vector<std::string> names;
		if (armKind == 0) { names = {"A0BaBaMGNest" + shape, "A0BaBaMGNestSquare"}; }
		else if (armKind == 1) { names = {"A0BaBaPit" + shape + "Rkt", "A0BaBaPitSquareRkt", "A0BaBaMGNest" + shape, "A0BaBaMGNestSquare"}; }
		else if (armKind == 2) { names = {"A0BaBaPit" + shape + "AT", "A0BaBaPitSquareAT", "A0BaBaMGNest" + shape, "A0BaBaMGNestSquare"}; }
		names.push_back("A0BaBaPit" + shape);
		names.push_back("A0BaBaPitSquare");
		for (const std::string &n : names) { if (STRUCTURE_STATS *st = availStat(player, n)) { return st; } }
		return nullptr;
	};
	static const int ARM_CYCLE[4] = {0, 1, 0, 2};   // MG, Raketen, MG, Panzerabwehr
	STRUCTURE_STATS *box = availStat(player, "A0BaBaAmmoBox");
	STRUCTURE_STATS *post = availStat(player, "A0BaBaMGNestPost");

	struct Item { double key; Placement p; };
	std::vector<Item> items;
	auto addItem = [&](double key, STRUCTURE_STATS *st, const Vector2i &a, const Vector2i &b, int faceSide) {
		Item it;
		it.key = key;
		it.p.stats = st; it.p.tile = a; it.p.tile2 = b; it.p.line = true; it.p.direction = dirOfSide(faceSide);
		items.push_back(it);
	};
	int branches = 0, armedCount = 0;
	double pieceBase = 0.0;
	for (const std::vector<Vector2i> &cells : pieces)
	{
		const size_t m = cells.size();
		// Feindseite je Feld: Normale der GEZEICHNETEN Linie am naechsten Punkt (Tangente ueber +-1,5 Kacheln) zur Seite der Querstriche,
		// auf N/O/S/W gerundet - mit Beharrung: die Seite des Vorgaengers bleibt, solange sie hoechstens 60 Grad abweicht (schraege Linien
		// und Umwege um Hindernisse wechseln nicht hin und her)
		std::vector<int> face(m, 0);
		{
			int prev = -1;
			for (size_t i = 0; i < m; ++i)
			{
				const Vector2i cw = worldOf(cells[i]);
				size_t best = 0;
				int64_t bd = INT64_MAX;
				for (size_t q = 0; q < dense.size(); ++q) { const int64_t dd = dist2i(dense[q], cw); if (dd < bd) { bd = dd; best = q; } }
				const Vector2i a = dense[best >= 6 ? best - 6 : 0], b = dense[std::min(dense.size() - 1, best + 6)];
				double tx = b.x - a.x, ty = b.y - a.y;
				if (tx == 0 && ty == 0) { tx = 1; }
				const double ex = -ty * side, ey = tx * side, el = std::max(1e-9, std::sqrt(ex * ex + ey * ey));
				auto dotSide = [&](int k) { return (ex * SDX[k] + ey * SDY[k]) / el; };
				int pick = 0;
				for (int k = 1; k < 4; ++k) { if (dotSide(k) > dotSide(pick) + 1e-9) { pick = k; } }
				if (prev >= 0 && dotSide(prev) >= 0.5) { pick = prev; }
				face[i] = pick;
				prev = pick;
			}
		}
		// Gruben im Hauptgraben: an den Enden und etwa alle 5 Felder
		std::vector<char> isPit(m, 0);
		const size_t nPit = (m <= 2) ? m : std::max<size_t>(2, 1 + (m - 1 + 2) / 5);
		for (size_t k = 0; k < nPit; ++k)
		{
			size_t idx = (nPit == 1) ? 0 : (k * (m - 1) + (nPit - 1) / 2) / (nPit - 1);
			// steht dort schon eine eigene Stellung: Nachbarfeld nehmen
			for (int d : {0, 1, -1, 2, -2})
			{
				const long j = static_cast<long>(idx) + d;
				if (j < 0 || j >= static_cast<long>(m) || isPit[static_cast<size_t>(j)] || ownFort(cells[static_cast<size_t>(j)])) { continue; }
				isPit[static_cast<size_t>(j)] = 1;
				break;
			}
		}
		// Hauptgraben: gerade Stuecke gleicher Blickrichtung zwischen den Gruben
		size_t i = 0;
		while (i < m)
		{
			if (isPit[i] || ownFort(cells[i])) { ++i; continue; }
			size_t j = i;
			while (j + 1 < m && !isPit[j + 1] && !ownFort(cells[j + 1]) && face[j + 1] == face[i]
			       && (j == i || (cells[j + 1].x - cells[j].x == cells[i + 1].x - cells[i].x && cells[j + 1].y - cells[j].y == cells[i + 1].y - cells[i].y))
			       && manhattan(cells[j], cells[j + 1]) == 1)
			{
				++j;
			}
			addItem(pieceBase + static_cast<double>(i), trench, cells[i], cells[j], face[i]);
			i = j + 1;
		}
		// Gruben (abwechselnd bewaffnet - die Enden immer) und Verbindungsgraeben nach hinten
		size_t pitNo = 0;
		const size_t lastPit = [&]() { size_t l = 0; for (size_t q = 0; q < m; ++q) { if (isPit[q]) { l = q; } } return l; }();
		for (size_t q = 0; q < m; ++q)
		{
			if (!isPit[q]) { continue; }
			const bool endPit = pitNo == 0 || q == lastPit;
			const bool armed = endPit || (pitNo % 2 == 0);
			const int k = face[q];
			STRUCTURE_STATS *pst = pitFor(cells[q], armed ? ARM_CYCLE[armedCount % 4] : -1);
			if (armed) { ++armedCount; }
			++pitNo;
			if (pst == nullptr) { pst = trench; }
			addItem(pieceBase + static_cast<double>(q), pst, cells[q], cells[q], k);
			if (armed || branches >= MAX_BRANCHES || m < 5) { continue; }
			// Verbindungsgraben: 3 Felder nach hinten, 2 zur Seite, Reservegrube am Ende (wie beim Infanterie-Netz)
			const int back = (k + 2) & 3;
			const int sideA = ((branches % 2) == 0) ? ((k + 1) & 3) : ((k + 3) & 3);
			const Vector2i r = cells[q];
			// ein Abzweigfeld darf nur an seinen Vorgaenger grenzen (sonst verschmilzt es mit dem Hauptgraben); das erste darf zusaetzlich
			// Hauptgraben-Felder direkt neben der Grube beruehren (schraege Linien: kleiner Knoten aus 2 x 2 Feldern an der Abzweigung)
			auto touchesOther = [&](const Vector2i &t, const Vector2i &pred) {
				for (int d = 0; d < 4; ++d)
				{
					const Vector2i n(t.x + SDX[d], t.y + SDY[d]);
					if (n == pred) { continue; }
					if (used.count(keyOf(n)) == 0 && !ownFort(n)) { continue; }
					if (pred == r && std::max(std::abs(n.x - r.x), std::abs(n.y - r.y)) <= 1) { continue; }
					return true;
				}
				return false;
			};
			std::vector<Vector2i> br;
			int usedSide = sideA;
			for (int attempt = 0; attempt < 2 && br.size() < 3; ++attempt)
			{
				const int sd = attempt == 0 ? sideA : ((sideA + 2) & 3);
				std::vector<Vector2i> want;
				for (int s1 = 1; s1 <= 3; ++s1) { want.emplace_back(r.x + SDX[back] * s1, r.y + SDY[back] * s1); }
				for (int s2 = 1; s2 <= 3; ++s2) { want.emplace_back(r.x + SDX[back] * 3 + SDX[sd] * s2, r.y + SDY[back] * 3 + SDY[sd] * s2); }
				std::vector<Vector2i> got;
				Vector2i pred = r;
				for (const Vector2i &t : want)
				{
					if (!freeFor(player, trench, t, 0, used) || touchesOther(t, pred)) { break; }
					got.push_back(t);
					pred = t;
				}
				if (got.size() > br.size()) { br = got; usedSide = sd; }
			}
			if (br.size() < 3) { continue; }   // kein Platz nach hinten
			++branches;
			for (const Vector2i &t : br) { used.insert(keyOf(t)); }
			const double bk = pieceBase + static_cast<double>(q) + 0.5;
			// letzte Zelle = Reservegrube (unbewaffnet), davor Graben (gerade Stuecke)
			const Vector2i endT = br.back();
			std::vector<Vector2i> run(br.begin(), br.end() - 1);
			size_t a0 = 0;
			while (a0 < run.size())
			{
				size_t b0 = a0;
				while (b0 + 1 < run.size() && (b0 == a0 || (run[b0 + 1].x - run[b0].x == run[a0 + 1].x - run[a0].x && run[b0 + 1].y - run[b0].y == run[a0 + 1].y - run[a0].y))) { ++b0; }
				addItem(bk, trench, run[a0], run[b0], k);
				a0 = b0 + 1;
			}
			STRUCTURE_STATS *reserve = pitFor(endT, -1);
			addItem(bk + 0.01, reserve != nullptr ? reserve : trench, endT, endT, k);
			// Munitionskiste neben dem 2. Grabenfeld (gegenueber der Abzweigung), einzelnes MG neben dem 3. (jeder zweite Abzweig)
			const int opp = (usedSide + 2) & 3;
			if (box != nullptr)
			{
				const Vector2i bx(br[1].x + SDX[opp], br[1].y + SDY[opp]);
				if (freeFor(player, box, bx, dirOfSide(k), used)) { used.insert(keyOf(bx)); addItem(bk + 0.02, box, bx, bx, k); }
			}
			if (post != nullptr && (branches % 2) == 0 && br.size() >= 3)
			{
				const Vector2i mg(br[2].x + SDX[opp], br[2].y + SDY[opp]);
				if (freeFor(player, post, mg, dirOfSide(k), used)) { used.insert(keyOf(mg)); addItem(bk + 0.03, post, mg, mg, k); }
			}
		}
		pieceBase += static_cast<double>(m) + 1.0;
	}
	std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.key < b.key; });
	for (const Item &it : items) { out.push_back(it.p); }
	return out;
}
}
