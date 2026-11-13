// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Stromnetz, siehe modgrid.h

#include "modgrid.h"
#include "modoil.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/netplay/netplay.h"

#include "ai.h"
#include "console.h"
#include "research.h"
#include "game_world.h"
#include "map.h"
#include "range_rings.h"
#include "structure.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace modgrid
{

namespace
{

constexpr int LINK_RANGE = 6 * 128;        // Masten bis 6 Felder voneinander werden verbunden
constexpr int SUPPLY_RANGE = 448;          // 3,5 Felder Versorgungsradius um Mast und Kraftwerk
constexpr int GEN_LINK_RANGE = 576;        // Kraftwerk speist Masten bis zu dieser Entfernung ein
constexpr int MAX_LINKS_PER_POLE = 3;
constexpr int CABLE_HEIGHT = 140;          // Hoehe der Kabelanschluesse am Mast
constexpr int GEN_CABLE_HEIGHT = 90;
constexpr int SAG_STEPS = 12;

struct Node
{
	uint32_t id;
	int x, y, z;
	bool big = false;   // Fabrik/Forschung: groesserer Radius um das Kraftwerk
};

struct Cable
{
	int player;
	int ax, ay, az, bx, by, bz;
	bool energized;
};

struct Marker
{
	int player;
	int x, y, z;
	bool powered;
	bool isPole;
};

std::unordered_map<uint32_t, bool> s_powered;
std::unordered_map<uint32_t, unsigned> s_upAt;       // PowerDown: Zeitpunkt, ab dem ein wieder versorgtes Gebaeude hochgefahren ist
std::unordered_map<uint32_t, bool> s_down;           // PowerDown: Gebaeude ist (laut Gate) abgeschaltet
bool s_hasGen[MAX_PLAYERS] = {};                   // PowerDown nur, wenn der Spieler ueberhaupt ein Kraftwerk hat (sonst kein Wiederanlauf moeglich)
std::unordered_map<uint32_t, bool> s_held;           // wir haben die Fabrik/Forschung selbst auf Halt gesetzt (nicht der Spieler)
constexpr int BASE_GEN_RANGE = 9 * 128;              // Fabriken/Forschung: 9 Felder um ein laufendes Kraftwerk
constexpr unsigned RESTART_STAGGER_MS = 1200;        // MF: Gebaeude fahren einzeln nacheinander hoch
std::vector<Cable> s_cables;
std::vector<Marker> s_markers;
bool s_have = false;
UDWORD s_lastSec = 0;
UDWORD s_lastTime = 0;

int find(std::vector<int> &par, int x)
{
	while (par[x] != x)
	{
		par[x] = par[par[x]];
		x = par[x];
	}
	return x;
}

void unite(std::vector<int> &par, int a, int b)
{
	a = find(par, a);
	b = find(par, b);
	if (a != b) { par[b] = a; }
}

int64_t dist2(const Node &a, const Node &b)
{
	const int64_t dx = a.x - b.x, dy = a.y - b.y;
	return dx * dx + dy * dy;
}

} // namespace

bool isPole(const STRUCTURE_STATS *psStats)
{
	return psStats != nullptr && psStats->type == REF_GENERIC && psStats->id.startsWith("A0PowerPole");
}

void reset()
{
	s_upAt.clear();
	s_down.clear();
	s_held.clear();
	s_powered.clear();
	s_cables.clear();
	s_markers.clear();
	s_have = false;
}

void update()
{
	if (s_have && gameTime >= s_lastTime && gameTime / 1000 == s_lastSec)
	{
		return;
	}
	s_have = true;
	s_lastTime = gameTime;
	s_lastSec = gameTime / 1000;
	s_powered.clear();
	s_cables.clear();
	s_markers.clear();

	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		unsigned restartIndex = 0;
		s_hasGen[p] = false;
		std::vector<Node> poles, gens, cons;
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->status != SS_BUILT) { continue; }
			const STRUCTURE_STATS *st = s->pStructureType;
			if (isPole(st)) { poles.push_back({s->id, s->pos.x, s->pos.y, s->pos.z}); }
			else if (st->type == REF_POWER_GEN) { s_hasGen[p] = true; if (modoil::generatorRunning(s)) { gens.push_back({s->id, s->pos.x, s->pos.y, s->pos.z}); } }
			else if (st->requiresPower)
			{
				const bool big = st->type == REF_FACTORY || st->type == REF_CYBORG_FACTORY || st->type == REF_VTOL_FACTORY || st->type == REF_RESEARCH || st->id.startsWith("A0BaBaRefinery");
				cons.push_back({s->id, s->pos.x, s->pos.y, s->pos.z, big});
			}
		}
		if (poles.empty() && gens.empty() && cons.empty()) { continue; }

		const int n = static_cast<int>(poles.size());
		const int g = static_cast<int>(gens.size());
		std::vector<int> par(n + g);
		std::iota(par.begin(), par.end(), 0);

		// Mast-Mast-Leitungen: jeder Mast verbindet sich mit seinen (bis zu 3) naechsten Nachbarn in Reichweite
		std::set<std::pair<int, int>> edges;
		for (int i = 0; i < n; i++)
		{
			std::vector<std::pair<int64_t, int>> cand;
			for (int j = 0; j < n; j++)
			{
				if (j == i) { continue; }
				const int64_t d = dist2(poles[i], poles[j]);
				if (d <= static_cast<int64_t>(LINK_RANGE) * LINK_RANGE) { cand.emplace_back(d, j); }
			}
			std::sort(cand.begin(), cand.end());
			for (size_t k = 0; k < cand.size() && k < static_cast<size_t>(MAX_LINKS_PER_POLE); k++)
			{
				const int j = cand[k].second;
				edges.insert({std::min(i, j), std::max(i, j)});
			}
		}
		std::vector<std::pair<int, int>> cableEdges(edges.begin(), edges.end());
		for (const auto &e : cableEdges) { unite(par, e.first, e.second); }

		// Kraftwerke: alle Masten in Reichweite gehoeren zum selben Netz, Kabel zum naechsten
		std::vector<std::pair<int, int>> genCable;   // (Kraftwerk, Mast)
		for (int k = 0; k < g; k++)
		{
			int best = -1;
			int64_t bestD = 0;
			for (int i = 0; i < n; i++)
			{
				const int64_t d = dist2(gens[k], poles[i]);
				if (d <= static_cast<int64_t>(GEN_LINK_RANGE) * GEN_LINK_RANGE)
				{
					unite(par, n + k, i);
					if (best < 0 || d < bestD) { best = i; bestD = d; }
				}
			}
			if (best >= 0) { genCable.emplace_back(k, best); }
		}
		std::vector<char> energized(n + g, 0);
		for (int k = 0; k < g; k++) { energized[find(par, n + k)] = 1; }

		const bool show = p == static_cast<int>(selectedPlayer) || aiCheckAlliances(p, static_cast<int>(selectedPlayer));
		if (show)
		{
			for (const auto &e : cableEdges)
			{
				const Node &a = poles[e.first], &b = poles[e.second];
				s_cables.push_back({p, a.x, a.y, a.z + CABLE_HEIGHT, b.x, b.y, b.z + CABLE_HEIGHT, energized[find(par, e.first)] != 0});
			}
			for (const auto &gc : genCable)
			{
				const Node &a = gens[gc.first], &b = poles[gc.second];
				s_cables.push_back({p, a.x, a.y, a.z + GEN_CABLE_HEIGHT, b.x, b.y, b.z + CABLE_HEIGHT, true});
			}
			for (int i = 0; i < n; i++)
			{
				s_markers.push_back({p, poles[i].x, poles[i].y, poles[i].z, energized[find(par, i)] != 0, true});
			}
		}

		// Verbraucher: im Versorgungsbereich eines gespeisten Mastes oder eines Kraftwerks
		for (const Node &c : cons)
		{
			bool ok = false;
			for (int i = 0; i < n && !ok; i++)
			{
				ok = energized[find(par, i)] != 0 && dist2(c, poles[i]) <= static_cast<int64_t>(SUPPLY_RANGE) * SUPPLY_RANGE;
			}
			const int64_t genR = c.big ? BASE_GEN_RANGE : GEN_LINK_RANGE;
			for (int k = 0; k < g && !ok; k++)
			{
				ok = dist2(c, gens[k]) <= genR * genR;
			}
			// PowerDown: kehrt die Versorgung zurueck, faehrt das Gebaeude nach Reihenfolge (ein Gebaeude je 1,2 s) wieder hoch
			const auto wasDown = s_down.find(c.id);
			if (ok && wasDown != s_down.end() && wasDown->second && s_upAt.find(c.id) == s_upAt.end())
			{
				s_upAt[c.id] = gameTime + RESTART_STAGGER_MS * (++restartIndex);
			}
			s_powered[c.id] = ok;
			if (show) { s_markers.push_back({p, c.x, c.y, c.z, ok, false}); }
		}
	}
}

bool isPowered(const STRUCTURE *psStruct)
{
	const auto it = s_powered.find(psStruct->id);
	return it != s_powered.end() && it->second;
}

bool isPoweredDown(const STRUCTURE *psStruct)
{
	const auto it = s_down.find(psStruct->id);
	return it != s_down.end() && it->second;
}

bool powerDownGate(STRUCTURE *s)
{
	const STRUCTURE_STATS *st = s->pStructureType;
	if (!st->requiresPower || s->status != SS_BUILT || !modoil::manages(s->player) || !s_hasGen[s->player]) { return false; }
	const auto it = s_powered.find(s->id);
	if (it == s_powered.end()) { return false; }          // gerade erst fertig: bis zur naechsten Berechnung Gnadenfrist
	bool down = !it->second;
	if (!down)
	{
		const auto up = s_upAt.find(s->id);
		if (up != s_upAt.end())
		{
			if (gameTime < up->second) { down = true; }   // noch nicht an der Reihe
			else { s_upAt.erase(up); }
		}
	}
	const bool wasDown = s_down[s->id];
	if (down)
	{
		if (!wasDown)
		{
			s_down[s->id] = true;
			s_upAt.erase(s->id);
			if (s->player == selectedPlayer)
			{
				addConsoleMessage(("PowerDown: " + std::string(getStatsName(st))).c_str(), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
			}
			if (s->isFactory() && s->pFunctionality->factory.psSubject && !s->pFunctionality->factory.timeStartHold)
			{
				holdProduction(s, ModeImmediate);
				s_held[s->id] = true;
			}
			else if (st->type == REF_RESEARCH && s->pFunctionality->researchFacility.psSubject && !s->pFunctionality->researchFacility.timeStartHold)
			{
				holdResearch(s, ModeImmediate);
				s_held[s->id] = true;
			}
		}
		return true;
	}
	if (wasDown)
	{
		s_down[s->id] = false;
		if (s_held[s->id])
		{
			if (s->isFactory()) { releaseProduction(s, ModeImmediate); }
			else if (st->type == REF_RESEARCH) { releaseResearch(s, ModeImmediate); }
			s_held[s->id] = false;
		}
	}
	return false;
}

namespace
{

bool project(const Vector3i &world, const glm::mat4 &m, Vector2i &out)
{
	const Vector3i pos(world.x, world.z, -world.y);   // wie clipXYZ(): Hoehe wird y, Kartenrichtung z (negiert)
	return pie_RotateProjectWithPerspective(&pos, m, &out) > 0;
}

void addThick(std::vector<glm::ivec4> &lines, const Vector2i &a, const Vector2i &b)
{
	lines.emplace_back(a.x, a.y, b.x, b.y);
	lines.emplace_back(a.x + 1, a.y, b.x + 1, b.y);
	lines.emplace_back(a.x, a.y + 1, b.x, b.y + 1);
}

} // namespace

void drawOverlay(const glm::mat4 &m)
{
	if (s_cables.empty() && s_markers.empty()) { return; }
	std::vector<glm::ivec4> black, grey;
	for (const Cable &c : s_cables)
	{
		const double dx = c.bx - c.ax, dy = c.by - c.ay;
		const double len = std::sqrt(dx * dx + dy * dy);
		const double sag = std::min(48.0, len * 0.09);
		Vector2i prev(0, 0);
		bool havePrev = false;
		for (int k = 0; k <= SAG_STEPS; k++)
		{
			const double t = static_cast<double>(k) / SAG_STEPS;
			Vector3i w(static_cast<int>(c.ax + (c.bx - c.ax) * t), static_cast<int>(c.ay + (c.by - c.ay) * t),
			           static_cast<int>(c.az + (c.bz - c.az) * t - sag * 4.0 * t * (1.0 - t)));
			Vector2i s(0, 0);
			const bool ok = project(w, m, s);
			if (ok && havePrev) { addThick(c.energized ? black : grey, prev, s); }
			prev = s;
			havePrev = ok;
		}
	}
	if (!black.empty()) { iV_Lines(black, pal_RGBA(0, 0, 0, 255)); }
	if (!grey.empty()) { iV_Lines(grey, pal_RGBA(70, 70, 76, 255)); }

	if (range_rings::getMode() != 4) { return; }
	// Anzeigemodus "Stromnetz": Versorgungskreise (gruen = gespeist, rot = ohne Strom), Verbraucher als Kreuz
	std::vector<glm::ivec4> green, red;
	for (const Marker &mk : s_markers)
	{
		auto &dst = mk.powered ? green : red;
		if (mk.isPole)
		{
			Vector2i prev(0, 0);
			bool havePrev = false;
			for (int k = 0; k <= 32; k++)
			{
				const double a = k * 6.283185307 / 32.0;
				const int wx = static_cast<int>(mk.x + std::cos(a) * SUPPLY_RANGE), wy = static_cast<int>(mk.y + std::sin(a) * SUPPLY_RANGE);
				Vector2i s(0, 0);
				const bool ok = project(Vector3i(wx, wy, map_Height(gameWorld.map, wx, wy) + 4), m, s);
				if (ok && havePrev) { dst.emplace_back(prev.x, prev.y, s.x, s.y); }
				prev = s;
				havePrev = ok;
			}
		}
		else
		{
			Vector2i s(0, 0);
			if (project(Vector3i(mk.x, mk.y, mk.z + 220), m, s))
			{
				dst.emplace_back(s.x - 7, s.y - 7, s.x + 7, s.y + 7);
				dst.emplace_back(s.x - 7, s.y + 7, s.x + 7, s.y - 7);
			}
		}
	}
	if (!green.empty()) { iV_Lines(green, pal_RGBA(70, 255, 90, 255)); }
	if (!red.empty()) { iV_Lines(red, pal_RGBA(255, 60, 50, 255)); }
}

} // namespace modgrid
