// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Zug-Transportsystem (siehe modrail.h)
#include "modrail.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <unordered_map>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piematrix.h"

#include "console.h"
#include "droid.h"
#include "game_world.h"
#include "geometry.h"
#include "map.h"
#include "modapc.h"
#include "move.h"
#include "objmem.h"
#include "order.h"
#include "orderdef.h"
#include "qtscript.h"
#include "stats.h"
#include "structure.h"
#include "template.h"
#include "visibility.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace modrail
{
namespace
{
// ---------------------------------------------------------------- Masse (Welteinheiten, 1 Feld = 128)
constexpr int FIX = 256;                     // Festkomma der Streckenlaenge
// (Wunsch 21.11. 15:48 "Alle Zugteile groesser ... dieselbe Groesse wie der Scav-Bus"): Modelle x1,8 (trains.py VS)
constexpr int HALF_LOCO = 57;                // halbe Laenge Lok / Waggon (Modelle in trains.py)
constexpr int HALF_WAGON = 50;
constexpr int COUPLER = 9;                   // Kupplung zwischen zwei Fahrzeugen
constexpr int ACCEL = 170;                   // Anfahren (Welteinheiten/s^2)
constexpr int BRAKE = 260;                   // Bremsen
constexpr int SPAWN_RADIUS = 9;              // Lokschuppen: Gleis hoechstens so viele Felder entfernt
constexpr int GOAL_RADIUS = 14;              // Ziel: Gleis hoechstens so viele Felder vom Klick
constexpr int KNEE[8][2] = { {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1} };   // Richtung k: N, NO, O, SO, S, SW, W, NW

struct Net
{
	int w = 0, h = 0;
	std::vector<uint8_t> rail;               // 0 = keins, 1 = fertiges Gleis, 2 = im Bau
	std::vector<uint8_t> link;               // je Feld 8 Bits: Verbindung in Richtung k (nur fertige Gleise)
};
Net s_net;

struct Train
{
	std::vector<Vector2i> line;              // Weg der Fahrzeuge (Weltpunkte), Fahrtrichtung = aufsteigend
	std::vector<int64_t> cum;                // Streckenlaenge bis Punkt i (x FIX)
	int64_t sLoco = 0;                       // Lage der Lok auf der Linie (x FIX)
	bool rev = false;                        // true: Waggons liegen vor der Lok (der Zug faehrt rueckwaerts, das Zugende voraus)
	int64_t speed = 0;                       // Welteinheiten/s x FIX
	std::vector<uint32_t> wagons;            // gekuppelte Waggons in Reihenfolge hinter der Lok
};
std::map<uint32_t, Train> s_trains;              // Lok -> Zug (map: feste Reihenfolge)
std::unordered_map<uint32_t, uint32_t> s_coupled; // Waggon -> Lok
bool s_afterLoad = true;                         // erster Schritt nach Laden/Spielbeginn: Waggons hinter Loks wieder ankuppeln

bool bodyStarts(const DROID *d, const char *prefix)
{
	if (d == nullptr || d->died) { return false; }
	const BODY_STATS *b = d->getBodyStats();
	return b != nullptr && b->id.startsWith(prefix);
}

DROID *findDroid(unsigned player, uint32_t id)
{
	if (player >= MAX_PLAYERS) { return nullptr; }
	for (DROID *d : gameWorld.objects.droids[player]) { if (d->id == id && !d->died) { return d; } }
	return nullptr;
}

DROID *findAny(uint32_t id)
{
	for (unsigned p = 0; p < MAX_PLAYERS; ++p) { if (DROID *d = findDroid(p, id)) { return d; } }
	return nullptr;
}

int halfLen(const DROID *d) { return bodyStarts(d, "TrainLoco") ? HALF_LOCO : HALF_WAGON; }

// ---------------------------------------------------------------- Gleisnetz
// Gleisraster = Original-Kacheln (DIST_TILE = 128 Welteinheiten), auch wenn das Spielraster feiner ist (Zweig viertel)
constexpr int RT = DIST_TILE;
inline int rc(int world) { return std::max(0, world) / RT; }
inline int rw(int cell) { return cell * RT; }
inline bool onMap(int x, int y) { return x >= 0 && y >= 0 && x < s_net.w && y < s_net.h; }
inline int idx(int x, int y) { return y * s_net.w + x; }
inline int netW() { return gameWorld.map.width * TILE_UNITS / RT; }
inline int netH() { return gameWorld.map.height * TILE_UNITS / RT; }

void buildNet()
{
	s_net.w = netW(); s_net.h = netH();
	const size_t n = static_cast<size_t>(std::max(0, s_net.w * s_net.h));
	s_net.rail.assign(n, 0); s_net.link.assign(n, 0);
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || !isRail(s)) { continue; }
			const int x = rc(s->pos.x), y = rc(s->pos.y);
			if (!onMap(x, y)) { continue; }
			uint8_t &r = s_net.rail[static_cast<size_t>(idx(x, y))];
			r = (s->status == SS_BUILT) ? 1 : std::max<uint8_t>(r, 2);
		}
	}
	for (int y = 0; y < s_net.h; ++y)
	{
		for (int x = 0; x < s_net.w; ++x)
		{
			if (s_net.rail[static_cast<size_t>(idx(x, y))] != 1) { continue; }
			uint8_t m = 0;
			for (int k = 0; k < 8; ++k)
			{
				const int nx = x + KNEE[k][0], ny = y + KNEE[k][1];
				if (!onMap(nx, ny) || s_net.rail[static_cast<size_t>(idx(nx, ny))] != 1) { continue; }
				if ((k & 1) != 0)
				{
					// schraeg nur, wenn kein gerader Weg ueber ein Nachbarfeld fuehrt (keine Dreiecke)
					const bool a = onMap(nx, y) && s_net.rail[static_cast<size_t>(idx(nx, y))] == 1;
					const bool b = onMap(x, ny) && s_net.rail[static_cast<size_t>(idx(x, ny))] == 1;
					if (a || b) { continue; }
				}
				m |= static_cast<uint8_t>(1u << k);
			}
			s_net.link[static_cast<size_t>(idx(x, y))] = m;
		}
	}
}

void ensureNet()
{
	if (s_net.w != netW() || s_net.h != netH() || s_net.rail.empty()) { buildNet(); }
}

bool railTile(int x, int y) { return onMap(x, y) && s_net.rail[static_cast<size_t>(idx(x, y))] == 1; }

Vector2i centre(int i) { return Vector2i(rw(i % s_net.w) + RT / 2, rw(i / s_net.w) + RT / 2); }

/// naechstes fertiges Gleisfeld zu einem Weltpunkt (Ring-Suche bis r Felder), -1 = keins
int nearestRail(Vector2i world, int r)
{
	const int cx = rc(world.x), cy = rc(world.y);
	int best = -1;
	int64_t bd = INT64_MAX;
	for (int y = cy - r; y <= cy + r; ++y)
	{
		for (int x = cx - r; x <= cx + r; ++x)
		{
			if (!railTile(x, y)) { continue; }
			const int i = idx(x, y);
			const Vector2i c = centre(i) - world;
			const int64_t d = static_cast<int64_t>(c.x) * c.x + static_cast<int64_t>(c.y) * c.y;
			if (d < bd || (d == bd && i < best)) { bd = d; best = i; }
		}
	}
	return best;
}

/// kuerzeste Wege im Gleisnetz ab Feld 'start' (Kosten gerade 10, schraeg 14); dist -1 = nicht erreichbar
void dijkstra(int start, std::vector<int32_t> &dist, std::vector<int32_t> &prev)
{
	const size_t n = s_net.rail.size();
	dist.assign(n, -1); prev.assign(n, -1);
	if (start < 0) { return; }
	using QE = std::pair<int32_t, int32_t>;
	std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
	dist[static_cast<size_t>(start)] = 0;
	q.emplace(0, start);
	while (!q.empty())
	{
		const QE e = q.top(); q.pop();
		const int i = e.second;
		if (e.first != dist[static_cast<size_t>(i)]) { continue; }
		const int x = i % s_net.w, y = i / s_net.w;
		const uint8_t m = s_net.link[static_cast<size_t>(i)];
		for (int k = 0; k < 8; ++k)
		{
			if ((m & (1u << k)) == 0) { continue; }
			const int j = idx(x + KNEE[k][0], y + KNEE[k][1]);
			const int32_t nd = e.first + ((k & 1) ? 14 : 10);
			int32_t &dj = dist[static_cast<size_t>(j)];
			if (dj < 0 || nd < dj || (nd == dj && i < prev[static_cast<size_t>(j)]))
			{
				dj = nd; prev[static_cast<size_t>(j)] = i;
				q.emplace(nd, j);
			}
		}
	}
}

std::vector<int> pathTo(const std::vector<int32_t> &prev, int start, int goal)
{
	std::vector<int> out;
	for (int i = goal; i >= 0; i = prev[static_cast<size_t>(i)])
	{
		out.push_back(i);
		if (i == start || out.size() > s_net.rail.size()) { break; }
	}
	std::reverse(out.begin(), out.end());
	return out;
}

// ---------------------------------------------------------------- Linie
int64_t segLen(const Vector2i &a, const Vector2i &b)
{
	const int64_t dx = b.x - a.x, dy = b.y - a.y;
	return static_cast<int64_t>(std::llround(std::sqrt(static_cast<double>(dx * dx + dy * dy)) * FIX));
}

void recum(Train &t)
{
	t.cum.assign(t.line.size(), 0);
	for (size_t i = 1; i < t.line.size(); ++i) { t.cum[i] = t.cum[i - 1] + segLen(t.line[i - 1], t.line[i]); }
}

int64_t lineLen(const Train &t) { return t.cum.empty() ? 0 : t.cum.back(); }

Vector2i pointAt(const Train &t, int64_t s)
{
	if (t.line.empty()) { return Vector2i(0, 0); }
	if (t.line.size() == 1 || s <= 0) { return t.line.front(); }
	if (s >= t.cum.back()) { return t.line.back(); }
	const size_t i = static_cast<size_t>(std::upper_bound(t.cum.begin(), t.cum.end(), s) - t.cum.begin());   // cum[i-1] <= s < cum[i]
	const int64_t a = t.cum[i - 1], L = std::max<int64_t>(1, t.cum[i] - a);
	const Vector2i p = t.line[i - 1], q = t.line[i];
	return Vector2i(p.x + static_cast<int>((q.x - p.x) * (s - a) / L), p.y + static_cast<int>((q.y - p.y) * (s - a) / L));
}

/// Punkte der Linie zwischen s0 und s1 (s0 > s1: rueckwaerts)
std::vector<Vector2i> extract(const Train &t, int64_t s0, int64_t s1)
{
	std::vector<Vector2i> out;
	const int64_t lo = std::min(s0, s1), hi = std::max(s0, s1);
	out.push_back(pointAt(t, lo));
	for (size_t i = 0; i < t.line.size(); ++i)
	{
		if (t.cum[i] > lo && t.cum[i] < hi) { out.push_back(t.line[i]); }
	}
	const Vector2i e = pointAt(t, hi);
	if (e != out.back()) { out.push_back(e); }
	if (s0 > s1) { std::reverse(out.begin(), out.end()); }
	return out;
}

/// Abstand der Fahrzeugmitte k vom Lok-Mittelpunkt entlang des Zuges (k = 0: Lok)
std::vector<int64_t> offsets(const std::vector<DROID *> &veh)
{
	std::vector<int64_t> off(veh.size(), 0);
	for (size_t k = 1; k < veh.size(); ++k)
	{
		off[k] = off[k - 1] + static_cast<int64_t>(halfLen(veh[k - 1]) + COUPLER + halfLen(veh[k])) * FIX;
	}
	return off;
}

int64_t arcOf(const Train &t, const std::vector<int64_t> &off, size_t k) { return t.sLoco + (t.rev ? off[k] : -off[k]); }

/// Linie am Anfang (hinter dem Zugende bei Vorwaertsfahrt) entlang der Gleise verlaengern, bis 'need' (x FIX) vor s = 0 Platz ist
void extendBack(Train &t, int64_t need)
{
	int guard = 0;
	while (need > 0 && guard++ < 24 && !t.line.empty())
	{
		const Vector2i a = t.line.front();
		const Vector2i b = t.line.size() > 1 ? t.line[1] : a;
		const int ax = rc(a.x), ay = rc(a.y);
		if (!railTile(ax, ay)) { break; }
		// Richtung weg von der Linie: Nachbar mit der kleinsten Abweichung von (a - b)
		const Vector2i dir = a - b;
		int bestK = -1;
		int64_t bestDot = INT64_MIN;
		const uint8_t m = s_net.link[static_cast<size_t>(idx(ax, ay))];
		for (int k = 0; k < 8; ++k)
		{
			if ((m & (1u << k)) == 0) { continue; }
			const Vector2i c = centre(idx(ax + KNEE[k][0], ay + KNEE[k][1]));
			const Vector2i v = c - a;
			const int64_t dot = static_cast<int64_t>(v.x) * dir.x + static_cast<int64_t>(v.y) * dir.y;
			if (t.line.size() > 1 && dot <= 0) { continue; }       // nicht zurueck in den Zug
			if (dot > bestDot) { bestDot = dot; bestK = k; }
		}
		if (bestK < 0) { break; }
		const Vector2i c = centre(idx(ax + KNEE[bestK][0], ay + KNEE[bestK][1]));
		// liegt a nicht auf der Feldmitte, zuerst die Mitte
		const Vector2i ac = centre(idx(ax, ay));
		std::vector<Vector2i> pre;
		if (ac != a && (static_cast<int64_t>(ac.x - a.x) * dir.x + static_cast<int64_t>(ac.y - a.y) * dir.y) > 0) { pre.push_back(ac); }
		pre.insert(pre.begin(), c);
		const int64_t add = segLen(c, pre.size() > 1 ? pre[1] : a) + (pre.size() > 1 ? segLen(pre[1], a) : 0);
		t.line.insert(t.line.begin(), pre.begin(), pre.end());
		t.sLoco += add;
		need -= add;
		recum(t);
	}
}

/// Linie fuer eine neue Lok: durch die Mitte ihres Gleisfeldes, ausgerichtet nach ihrer Blickrichtung
void initLine(Train &t, const DROID *loco)
{
	t.line.clear();
	const Vector2i p = loco->pos.xy();
	const int i = railTile(rc(p.x), rc(p.y)) ? idx(rc(p.x), rc(p.y)) : nearestRail(p, 2);
	const Vector2i f(iSinR(loco->rot.direction, 64), iCosR(loco->rot.direction, 64));
	if (i < 0)
	{
		t.line = { p - f, p };
	}
	else
	{
		const Vector2i c = centre(i);
		t.line = { c - f, c };
	}
	recum(t);
	t.sLoco = lineLen(t);
	t.rev = false;
	t.speed = 0;
}

std::vector<DROID *> vehicles(const DROID *loco, Train &t)
{
	std::vector<DROID *> v{ const_cast<DROID *>(loco) };
	for (size_t k = 0; k < t.wagons.size();)
	{
		DROID *w = findDroid(loco->player, t.wagons[k]);
		if (w == nullptr || !isWagon(w)) { s_coupled.erase(t.wagons[k]); t.wagons.erase(t.wagons.begin() + static_cast<std::ptrdiff_t>(k)); continue; }
		v.push_back(w);
		++k;
	}
	return v;
}

Train &trainOf(DROID *loco)
{
	auto it = s_trains.find(loco->id);
	if (it == s_trains.end())
	{
		it = s_trains.emplace(loco->id, Train()).first;
		initLine(it->second, loco);
	}
	return it->second;
}

/// Fahrzeuge auf ihre Lage entlang der Linie setzen
void place(Train &t, const std::vector<DROID *> &veh, int64_t speedFix)
{
	const std::vector<int64_t> off = offsets(veh);
	for (size_t k = 0; k < veh.size(); ++k)
	{
		DROID *d = veh[k];
		const int64_t s = std::clamp<int64_t>(arcOf(t, off, k), 0, lineLen(t));
		const Vector2i p = pointAt(t, s);
		const Vector2i a = pointAt(t, std::max<int64_t>(0, s - 20 * FIX)), b = pointAt(t, std::min(lineLen(t), s + 20 * FIX));
		const int oldx = d->pos.x, oldy = d->pos.y;
		d->pos.x = std::clamp(p.x, TILE_UNITS, world_coord(gameWorld.map.width - 1) - 1);
		d->pos.y = std::clamp(p.y, TILE_UNITS, world_coord(gameWorld.map.height - 1) - 1);
		d->pos.z = map_Height(gameWorld.map, d->pos.x, d->pos.y);
		if (a != b)
		{
			uint16_t dir = calcDirection(a.x, a.y, b.x, b.y);
			if (t.rev) { dir = static_cast<uint16_t>(dir + 0x8000); }
			d->rot.direction = dir;
			d->sMove.moveDir = dir;
		}
		updateDroidOrientation(d, gameWorld.map);
		d->sMove.speed = static_cast<int>(speedFix / FIX);
		if (k > 0) { d->sMove.Status = MOVEINACTIVE; }
		if (rc(oldx) != rc(d->pos.x) || rc(oldy) != rc(d->pos.y))
		{
			visTilesUpdate(d, gameWorld.map);
			triggerEventDroidMoved(d, oldx, oldy);
		}
	}
}

/// Linie hinter dem Fahrzeug am weitesten hinten kuerzen (Speicher, kleine Zahlen)
void trim(Train &t, const std::vector<int64_t> &off)
{
	const int64_t rear = t.rev ? t.sLoco : t.sLoco - off.back();
	const int64_t keep = rear - 3 * RT * FIX;
	if (keep <= 0 || t.line.size() < 3) { return; }
	size_t i = 0;
	while (i + 2 < t.line.size() && t.cum[i + 1] < keep) { ++i; }
	if (i == 0) { return; }
	const int64_t cut = t.cum[i];
	t.line.erase(t.line.begin(), t.line.begin() + static_cast<std::ptrdiff_t>(i));
	t.sLoco -= cut;
	recum(t);
}

void couple(DROID *loco, DROID *wagon)
{
	if (loco == nullptr || wagon == nullptr || loco->player != wagon->player || s_coupled.count(wagon->id) != 0) { return; }
	Train &t = trainOf(loco);
	if (t.wagons.size() >= 8) { return; }                     // hoechstens 8 Waggons je Lok
	t.wagons.push_back(wagon->id);
	s_coupled[wagon->id] = loco->id;
	std::vector<DROID *> veh = vehicles(loco, t);
	const std::vector<int64_t> off = offsets(veh);
	if (!t.rev)
	{
		const int64_t need = off.back() - t.sLoco + 8 * FIX;
		if (need > 0) { extendBack(t, need); }
	}
	orderDroid(wagon, DORDER_STOP, ModeImmediate);
	wagon->sMove.Status = MOVEINACTIVE;
	place(t, veh, t.speed);
	if (wagon->player == selectedPlayer) { addConsoleMessage(_("Train: wagon coupled"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
}

DROID *locoDroidOf(const DROID *d)
{
	if (d == nullptr) { return nullptr; }
	if (isLoco(d)) { return const_cast<DROID *>(d); }
	const auto it = s_coupled.find(d->id);
	return it == s_coupled.end() ? nullptr : findDroid(d->player, it->second);
}
} // namespace

// ---------------------------------------------------------------- Abfragen
bool isRailStats(const STRUCTURE_STATS *s) { return s != nullptr && s->id.startsWith("A0RailTrack"); }
bool isRail(const STRUCTURE *s) { return s != nullptr && isRailStats(s->pStructureType); }
bool isTrain(const DROID *d) { return bodyStarts(d, "Train"); }
bool isLoco(const DROID *d) { return bodyStarts(d, "TrainLoco"); }
bool isWagon(const DROID *d) { return bodyStarts(d, "TrainWagon"); }

bool isTrainTemplate(const DROID_TEMPLATE *t)
{
	return t != nullptr && t->asParts[COMP_BODY] && t->getBodyStats() != nullptr && t->getBodyStats()->id.startsWith("Train");
}

uint32_t locoOf(const DROID *d)
{
	const DROID *l = locoDroidOf(d);
	return l != nullptr ? l->id : 0;
}

int freeSeats(const DROID *loco)
{
	if (!isLoco(loco)) { return 0; }
	const auto it = s_trains.find(loco->id);
	if (it == s_trains.end()) { return 0; }
	int n = 0;
	for (uint32_t w : it->second.wagons)
	{
		const DROID *d = findDroid(loco->player, w);
		if (d != nullptr && modapc::isApc(d)) { n += std::max(0, modapc::capacity(d) - modapc::load(d)); }
	}
	return n;
}

// ---------------------------------------------------------------- Fahren
bool routeTrain(DROID *d, int wx, int wy)
{
	if (!isTrain(d)) { return false; }
	ensureNet();
	DROID *loco = locoDroidOf(d);
	if (loco == nullptr)
	{
		// Waggon ohne Lok: steht
		d->sMove.Status = MOVEINACTIVE;
		return false;
	}
	if (loco != d) { d->sMove.Status = MOVEINACTIVE; }
	Train &t = trainOf(loco);
	std::vector<DROID *> veh = vehicles(loco, t);
	const std::vector<int64_t> off = offsets(veh);
	const size_t last = veh.size() - 1;
	const Vector2i click(wx, wy);

	const auto tileOfVeh = [&](size_t k) {
		const Vector2i p = veh[k]->pos.xy();
		return railTile(rc(p.x), rc(p.y)) ? idx(rc(p.x), rc(p.y)) : nearestRail(p, 2);
	};
	const int tA = tileOfVeh(0);
	if (tA < 0)
	{
		loco->sMove.Status = MOVEINACTIVE;
		if (loco->player == selectedPlayer) { addConsoleMessage(_("Train: the locomotive is not on a rail track"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
		return false;
	}
	std::vector<int32_t> distA, prevA;
	dijkstra(tA, distA, prevA);
	// Ziel: erreichbares Gleisfeld naechst dem Klick
	int goal = -1;
	int64_t bd = INT64_MAX;
	const int cx = rc(click.x), cy = rc(click.y);
	for (int y = std::max(0, cy - GOAL_RADIUS); y <= std::min(s_net.h - 1, cy + GOAL_RADIUS); ++y)
	{
		for (int x = std::max(0, cx - GOAL_RADIUS); x <= std::min(s_net.w - 1, cx + GOAL_RADIUS); ++x)
		{
			const int i = idx(x, y);
			if (distA[static_cast<size_t>(i)] < 0) { continue; }
			const Vector2i c = centre(i) - click;
			const int64_t dd = static_cast<int64_t>(c.x) * c.x + static_cast<int64_t>(c.y) * c.y;
			if (dd < bd || (dd == bd && i < goal)) { bd = dd; goal = i; }
		}
	}
	if (goal < 0)
	{
		loco->sMove.Status = MOVEINACTIVE;
		if (loco->player == selectedPlayer) { addConsoleMessage(_("Train: no connected rail track near the destination"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
		return false;
	}
	// Wer faehrt voraus? Liegt das Ziel naeher am Zugende, schiebt die Lok den Zug rueckwaerts
	bool tailLeads = false;
	std::vector<int> route;
	if (last > 0)
	{
		const int tB = tileOfVeh(last);
		std::vector<int32_t> distB, prevB;
		dijkstra(tB, distB, prevB);
		if (tB >= 0 && distB[static_cast<size_t>(goal)] >= 0 && distB[static_cast<size_t>(goal)] < distA[static_cast<size_t>(goal)])
		{
			tailLeads = true;
			route = pathTo(prevB, tB, goal);
		}
	}
	if (!tailLeads) { route = pathTo(prevA, tA, goal); }

	const size_t leadK = tailLeads ? last : 0;
	const size_t curLeadK = t.rev ? last : 0;
	const int64_t lineEnd = lineLen(t);
	const int64_t sLead = std::clamp<int64_t>(arcOf(t, off, curLeadK), 0, lineEnd);
	const int64_t sRear = std::clamp<int64_t>(arcOf(t, off, curLeadK == 0 ? last : 0), 0, lineEnd);
	const Vector2i pLeadOld = pointAt(t, sLead);
	// Einzelne Lok: Ziel hinter ihr -> sie wendet (Linie umdrehen)
	bool flip = leadK != curLeadK;
	if (last == 0 && route.size() >= 2)
	{
		const Vector2i nxt = centre(route[1]);
		const Vector2i ahead = pointAt(t, std::min(lineEnd, sLead + 24 * FIX)) - pointAt(t, std::max<int64_t>(0, sLead - 24 * FIX));
		const Vector2i to = nxt - pLeadOld;
		flip = static_cast<int64_t>(to.x) * ahead.x + static_cast<int64_t>(to.y) * ahead.y < 0;
	}
	std::vector<Vector2i> pts;
	if (!flip)
	{
		pts = extract(t, std::max<int64_t>(0, sRear - 16 * FIX), sLead);
	}
	else
	{
		// Linie umdrehen: das bisherige Zugende (bzw. die einzelne Lok in Gegenrichtung) faehrt voraus
		pts = extract(t, std::min(lineEnd, sLead + 16 * FIX), sRear);
	}
	if (pts.empty()) { pts.push_back(veh[leadK]->pos.xy()); }
	const Vector2i pLead = pts.back();
	const size_t leadIdx = pts.size() - 1;
	// Weg im Gleisnetz anhaengen (Feldmitten)
	for (size_t r = 0; r < route.size(); ++r)
	{
		const Vector2i c = centre(route[r]);
		if (r == 0)
		{
			if (route.size() < 2) { if (c != pLead) { pts.push_back(c); } continue; }
			const Vector2i c1 = centre(route[1]);
			const int64_t dot = static_cast<int64_t>(c.x - pLead.x) * (c1.x - c.x) + static_cast<int64_t>(c.y - pLead.y) * (c1.y - c.y);
			if (dot > 0 && iHypot(c - pLead) > 2) { pts.push_back(c); }
			continue;
		}
		if (c != pts.back()) { pts.push_back(c); }
	}
	t.line = pts;
	recum(t);
	// Lage der Fahrzeuge: das voraus fahrende steht am Ende des alten Teilstuecks
	const int64_t sLeadNew = t.cum[leadIdx];
	t.rev = last > 0 && tailLeads;
	t.sLoco = t.rev ? sLeadNew - off[last] : sLeadNew;
	if (flip) { t.speed = 0; }          // Richtungswechsel: aus dem Stand
	place(t, veh, t.speed);

	loco->sMove.destination = centre(goal);
	loco->sMove.asPath.clear();
	for (int r : route) { loco->sMove.asPath.push_back(centre(r)); }
	loco->sMove.pathIndex = 0;
	loco->sMove.Status = MOVENAVIGATE;
	return true;
}

void updateTrain(DROID *d)
{
	if (!isTrain(d)) { return; }
	ensureNet();
	if (isWagon(d))
	{
		// Waggons fahren nicht selbst (die Lok setzt sie); ohne Lok stehen sie
		if (s_coupled.count(d->id) == 0) { d->sMove.Status = MOVEINACTIVE; d->sMove.speed = 0; }
		return;
	}
	Train &t = trainOf(d);
	std::vector<DROID *> veh = vehicles(d, t);
	const std::vector<int64_t> off = offsets(veh);
	const size_t leadK = t.rev ? veh.size() - 1 : 0;
	const int64_t end = lineLen(t);
	int64_t sLead = arcOf(t, off, leadK);
	// Fahrbares Ende: Linienende oder das erste Feld voraus, das kein fertiges Gleis mehr ist (zerstoert/abgerissen)
	int64_t stop = end;
	const int64_t look = std::min(end, sLead + static_cast<int64_t>(8 * RT) * FIX);   // 8 Felder voraus (Bremsweg aus voller Fahrt ~6 Felder)
	for (int64_t s = sLead + 16 * FIX; s <= look; s += 16 * FIX)
	{
		const Vector2i p = pointAt(t, s);
		if (!railTile(rc(p.x), rc(p.y)))
		{
			// die Feldgrenze liegt zwischen s - 16 und s; die Fahrzeugspitze bleibt davor
			stop = std::max(sLead, s - static_cast<int64_t>(16 + halfLen(veh[leadK])) * FIX);
			break;
		}
	}
	const bool wantMove = d->sMove.Status != MOVEINACTIVE && d->sMove.Status != MOVEHOVER;
	const BODY_STATS *body = d->getBodyStats();
	const int64_t vmax = static_cast<int64_t>(std::max(200, std::min(900, body != nullptr && body->id.startsWith("TrainLocoFire") ? 640 : 540))) * FIX;
	const int64_t remain = std::max<int64_t>(0, stop - sLead);
	const int64_t dt = static_cast<int64_t>(deltaGameTime);
	// Geschwindigkeit, mit der der Zug bis zum Ende noch zum Stehen kommt: v = sqrt(2 a s)
	const double vStopD = std::sqrt(2.0 * BRAKE * (static_cast<double>(remain) / FIX)) * FIX;
	const int64_t vStop = static_cast<int64_t>(vStopD);
	int64_t v = t.speed;
	if (wantMove && remain > 0)
	{
		const int64_t target = std::min(vmax, vStop);
		if (v < target) { v = std::min(target, v + static_cast<int64_t>(ACCEL) * FIX * dt / GAME_TICKS_PER_SEC); }
		else { v = std::max(target, v - static_cast<int64_t>(BRAKE) * FIX * dt / GAME_TICKS_PER_SEC); }
	}
	else
	{
		v = std::max<int64_t>(0, std::min(vStop, v - static_cast<int64_t>(BRAKE) * 2 * FIX * dt / GAME_TICKS_PER_SEC));
	}
	int64_t ds = v * dt / GAME_TICKS_PER_SEC;
	if (ds > remain) { ds = remain; }
	t.sLoco += ds;
	t.speed = v;
	sLead += ds;
	if (wantMove && (stop - sLead) < 2 * FIX && v < 40 * FIX)
	{
		t.speed = 0;
		d->sMove.Status = MOVEINACTIVE;
		if (stop < end - 4 * FIX && d->player == selectedPlayer) { addConsoleMessage(_("Train: the track ahead is interrupted"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
	}
	place(t, veh, t.speed);
	trim(t, off);
}

void onFactoryDroid(DROID *d, const STRUCTURE *factory)
{
	if (!isTrain(d) || factory == nullptr) { return; }
	buildNet();
	// naechstes freies Gleisfeld (nicht unter einem anderen Schienenfahrzeug)
	const Vector2i fp = factory->pos.xy();
	int best = -1;
	int64_t bd = INT64_MAX;
	const int cx = rc(fp.x), cy = rc(fp.y);
	for (int y = cy - SPAWN_RADIUS; y <= cy + SPAWN_RADIUS; ++y)
	{
		for (int x = cx - SPAWN_RADIUS; x <= cx + SPAWN_RADIUS; ++x)
		{
			if (!railTile(x, y)) { continue; }
			const int i = idx(x, y);
			const Vector2i c = centre(i);
			bool busy = false;
			for (const DROID *o : gameWorld.objects.droids[d->player])
			{
				if (o != d && !o->died && isTrain(o) && iHypot(o->pos.xy() - c) < HALF_LOCO + HALF_WAGON) { busy = true; break; }
			}
			const Vector2i dv = c - fp;
			const int64_t dd = static_cast<int64_t>(dv.x) * dv.x + static_cast<int64_t>(dv.y) * dv.y + (busy ? static_cast<int64_t>(1) << 40 : 0);
			if (dd < bd || (dd == bd && i < best)) { bd = dd; best = i; }
		}
	}
	if (best < 0)
	{
		if (d->player == selectedPlayer) { addConsoleMessage(_("Train depot: no rail track nearby - lay rails next to the depot"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
		return;
	}
	const Vector2i c = centre(best);
	droidSetPosition(d, gameWorld.map, c.x, c.y);
	const uint8_t m = s_net.link[static_cast<size_t>(best)];
	for (int k = 0; k < 8; ++k)
	{
		if ((m & (1u << k)) != 0) { d->rot.direction = calcDirection(0, 0, KNEE[k][0], KNEE[k][1]); break; }
	}
	if (isLoco(d))
	{
		s_trains.erase(d->id);
		trainOf(d);
		return;
	}
	// Waggon: an die naechste eigene Lok in der Naehe (ihr Zugende nah genug) kuppeln
	DROID *bestLoco = nullptr;
	int bestD = RT * 6;
	for (DROID *o : gameWorld.objects.droids[d->player])
	{
		if (o->died || !isLoco(o)) { continue; }
		const int dd = iHypot(o->pos.xy() - c);
		if (dd < bestD) { bestD = dd; bestLoco = o; }
	}
	if (bestLoco != nullptr) { couple(bestLoco, d); }
}

void tick()
{
	buildNet();
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *w : gameWorld.objects.droids[p])
		{
			if (w->died || !isWagon(w) || s_coupled.count(w->id) != 0) { continue; }
			// Waggon auf eine Lok (oder einen gekuppelten Waggon) geklickt bzw. dorthin geschickt -> ankuppeln
			DROID *target = nullptr;
			if (w->order.type == DORDER_GUARD && w->order.psObj != nullptr && w->order.psObj->type == OBJ_DROID)
			{
				target = locoDroidOf(static_cast<DROID *>(w->order.psObj));
			}
			else if (w->order.type == DORDER_MOVE || w->order.type == DORDER_SCOUT)
			{
				for (DROID *o : gameWorld.objects.droids[p])
				{
					if (o == w || o->died || !isTrain(o)) { continue; }
					if (iHypot(o->pos.xy() - w->order.pos) < RT * 3 / 4) { target = locoDroidOf(o); if (target) { break; } }
				}
			}
			if (target != nullptr && target->player == w->player) { couple(target, w); }
		}
		// Absitzen-Knopf an der Lok: an alle Truppenwaggons des Zuges weitergeben (modapc laesst sie aussteigen)
		for (DROID *l : gameWorld.objects.droids[p])
		{
			if (l->died || !isLoco(l) || (l->secondaryOrder & DSS_MINECLEAR_MASK) == 0) { continue; }
			l->secondaryOrder &= ~static_cast<UDWORD>(DSS_MINECLEAR_MASK);
			l->secondaryOrderPending &= ~static_cast<uint32_t>(DSS_MINECLEAR_MASK);
			const auto it = s_trains.find(l->id);
			if (it == s_trains.end()) { continue; }
			for (uint32_t wid : it->second.wagons)
			{
				DROID *w = findDroid(p, wid);
				if (w != nullptr && modapc::isApc(w) && modapc::load(w) > 0)
				{
					w->secondaryOrder |= DSS_MINECLEAR_SET;
				}
			}
		}
		// Unload-Knopf an einem LEEREN gekuppelten Waggon (Truppenwaggon ohne Insassen, MG-Flachwagen): abkuppeln - er und die Waggons
		// dahinter bleiben auf dem Gleis stehen (Truppenwaggons mit Insassen: modapc laesst sie aussteigen)
		for (DROID *w : gameWorld.objects.droids[p])
		{
			if (w->died || !isWagon(w) || (w->secondaryOrder & DSS_MINECLEAR_MASK) == 0) { continue; }
			if (modapc::isApc(w) && modapc::load(w) > 0) { continue; }
			w->secondaryOrder &= ~static_cast<UDWORD>(DSS_MINECLEAR_MASK);
			w->secondaryOrderPending &= ~static_cast<uint32_t>(DSS_MINECLEAR_MASK);
			const auto c = s_coupled.find(w->id);
			if (c == s_coupled.end()) { continue; }
			const auto it = s_trains.find(c->second);
			if (it == s_trains.end()) { s_coupled.erase(c); continue; }
			std::vector<uint32_t> &wl = it->second.wagons;
			const auto at = std::find(wl.begin(), wl.end(), w->id);
			const int n = static_cast<int>(wl.end() - at);
			for (auto q = at; q != wl.end(); ++q) { s_coupled.erase(*q); }
			wl.erase(at, wl.end());
			if (p == selectedPlayer) { addConsoleMessage(n > 1 ? _("Train: wagons uncoupled") : _("Train: wagon uncoupled"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
		}
	}
	// erster Schritt nach Laden/Spielbeginn: Waggons, die direkt hinter einer Lok stehen, kuppeln wieder an (Zuege stehen nicht im Spielstand)
	if (s_afterLoad)
	{
		s_afterLoad = false;
		for (unsigned p = 0; p < MAX_PLAYERS; ++p)
		{
			for (DROID *l : gameWorld.objects.droids[p])
			{
				if (l->died || !isLoco(l)) { continue; }
				DROID *tail = l;
				for (int guard = 0; guard < 8; ++guard)
				{
					DROID *next = nullptr;
					int nd = (HALF_LOCO + HALF_WAGON + COUPLER) * 3 / 2;
					for (DROID *w : gameWorld.objects.droids[p])
					{
						if (w->died || !isWagon(w) || s_coupled.count(w->id) != 0) { continue; }
						const int dd = iHypot(w->pos.xy() - tail->pos.xy());
						if (dd < nd) { nd = dd; next = w; }
					}
					if (next == nullptr) { break; }
					couple(l, next);
					tail = next;
				}
			}
		}
	}
	// tote Loks: Zug aufloesen (Waggons bleiben stehen)
	for (auto it = s_trains.begin(); it != s_trains.end();)
	{
		if (findAny(it->first) != nullptr) { ++it; continue; }
		for (uint32_t w : it->second.wagons) { s_coupled.erase(w); }
		it = s_trains.erase(it);
	}
}

void reset()
{
	s_trains.clear();
	s_coupled.clear();
	s_net = Net();
	s_afterLoad = true;
}

// ---------------------------------------------------------------- Zeichnen
bool drawRail(const STRUCTURE *s, PIELIGHT brightness, const glm::mat4 &viewMatrix)
{
	// (Wunsch 21.11. 15:40) Damm mit Schotter, Schwellen, Schienen; Erde am Dammfuss durchscheinend auslaufend (eigene Modelle mit Alpha);
	// Gleisende = gerader, harter Abschluss (modrail_end); das achteckige Mittelstueck nur noch an Kurven und Weichen; Hellholz-Variante
	// fuer das Bauwerk "A0RailTrackLight"
	enum { HALF = 0, DIAG, END, HUB, E_HALF, E_DIAG, E_END, E_HUB, NSHAPE };
	static const char *const names[2][NSHAPE] = {
		{ "modrail_half.pie", "modrail_diag.pie", "modrail_end.pie", "modrail_hub.pie", "modrail_earth_half.pie", "modrail_earth_diag.pie", "modrail_earth_end.pie", "modrail_earth_hub.pie" },
		{ "modrail_half_l.pie", "modrail_diag_l.pie", "modrail_end_l.pie", "modrail_hub.pie", "modrail_earth_half.pie", "modrail_earth_diag.pie", "modrail_earth_end.pie", "modrail_earth_hub.pie" } };
	static const iIMDShape *shapes[2][NSHAPE] = {};
	static unsigned s_gen = 0;
	if (modelCacheStale(s_gen))
	{
		for (int v = 0; v < 2; ++v)
		{
			for (int k = 0; k < NSHAPE; ++k)
			{
				const iIMDBaseShape *b = modelGet(WzString::fromUtf8(names[v][k]));
				shapes[v][k] = b ? b->displayModel() : nullptr;
			}
		}
	}
	const int var = s->pStructureType->id.endsWith("Light") ? 1 : 0;
	const iIMDShape *const *sh = shapes[var];
	if (sh[HALF] == nullptr || sh[DIAG] == nullptr) { return false; }
	ensureNet();
	const int tx = rc(s->pos.x), ty = rc(s->pos.y);
	if (!onMap(tx, ty)) { return false; }
	const auto any = [](int x, int y) { return onMap(x, y) && s_net.rail[static_cast<size_t>(idx(x, y))] != 0; };
	uint8_t m = 0;
	for (int k = 0; k < 8; ++k)
	{
		const int nx = tx + KNEE[k][0], ny = ty + KNEE[k][1];
		if (!any(nx, ny)) { continue; }
		if ((k & 1) != 0 && (any(nx, ty) || any(tx, ny))) { continue; }
		m |= static_cast<uint8_t>(1u << k);
	}
	int links[8];
	int nl = 0;
	for (int k = 0; k < 8; ++k) { if ((m & (1u << k)) != 0) { links[nl++] = k; } }
	const bool built = s->status == SS_BUILT;
	const int flags = built ? 0 : pie_TRANSLUCENT;
	const int flagData = built ? 0 : 110;
	const int cx = rw(tx) + RT / 2, cy = rw(ty) + RT / 2;
	const float h0 = static_cast<float>(map_Height(gameWorld.map, cx, cy));
	const glm::vec3 c(static_cast<float>(cx), h0, static_cast<float>(-cy));
	const auto piece = [&](int k, const iIMDShape *model, float len, int fl, int fd)
	{
		if (model == nullptr) { return; }
		const int dx = KNEE[k][0], dy = KNEE[k][1];
		const float ex = static_cast<float>(cx) + static_cast<float>(dx) * 64.f, ey = static_cast<float>(cy) + static_cast<float>(dy) * 64.f;
		const float h1 = static_cast<float>(map_Height(gameWorld.map, static_cast<int>(ex), static_cast<int>(ey)));
		const float yaw = std::atan2(static_cast<float>(dx), static_cast<float>(-dy));   // Modell +z -> Richtung (dx, -dy) im Bild (Bild-z = -Welt-y)
		const float pitch = -std::atan2(h1 - h0, len);
		glm::mat4 M = glm::translate(glm::mat4(1.f), c);
		M = glm::rotate(M, yaw, glm::vec3(0.f, 1.f, 0.f));
		M = glm::rotate(M, pitch, glm::vec3(1.f, 0.f, 0.f));
		pie_Draw3DShape(model, 0, 0, brightness, fl, fd, M, viewMatrix);
	};
	const int eFlags = pie_TRANSLUCENT;                 // Erde: Alpha der Textur laesst sie in den Boden auslaufen
	const int eData = built ? 255 : 90;
	if (nl == 0)
	{
		// einzelnes Feld: gerades Stueck in Bauwerks-Richtung mit zwei harten Enden
		const bool ew = ((s->rot.direction + 0x2000) & 0x4000) != 0;
		const int a = ew ? 2 : 0;
		for (int k : { a, a + 4 })
		{
			piece(k, sh[END], 40.f, flags, flagData);
			piece(k, sh[E_END], 40.f, eFlags, eData);
		}
		return true;
	}
	for (int i = 0; i < nl; ++i)
	{
		const int k = links[i];
		piece(k, (k & 1) ? sh[DIAG] : sh[HALF], (k & 1) ? 90.51f : 64.f, flags, flagData);
		piece(k, (k & 1) ? sh[E_DIAG] : sh[E_HALF], (k & 1) ? 90.51f : 64.f, eFlags, eData);
	}
	if (nl == 1)
	{
		// Gleisende: gerader Abschluss in Gegenrichtung
		const int k = (links[0] + 4) % 8;
		piece(k, sh[END], 40.f, flags, flagData);
		piece(k, sh[E_END], 40.f, eFlags, eData);
	}
	else if (!(nl == 2 && links[1] - links[0] == 4))
	{
		// Kurve oder Weiche: Mittelstueck fuellt die Ecke
		if (sh[HUB] != nullptr) { pie_Draw3DShape(sh[HUB], 0, 0, brightness, flags, flagData, glm::translate(glm::mat4(1.f), c), viewMatrix); }
		if (sh[E_HUB] != nullptr) { pie_Draw3DShape(sh[E_HUB], 0, 0, brightness, eFlags, eData, glm::translate(glm::mat4(1.f), c), viewMatrix); }
	}
	return true;
}
}
