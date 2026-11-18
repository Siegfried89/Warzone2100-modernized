// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Stromnetz, siehe modgrid.h

#include "modgrid.h"
#include "modoil.h"
#include "modlayer.h"
#include "modweather.h"
#include "modnuke.h"

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
#include <unordered_set>
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
constexpr int CABLE_HEIGHT = 150;          // Hoehe der Kabelanschluesse am Mast (16.11. 19:07: WZ-Stahlgittermast in voller Groesse - Ausleger bei 150)
constexpr int GEN_CABLE_HEIGHT = 90;
constexpr int SAG_STEPS = 12;
// (Wunsch 17.11. 20:35) Umspannwerk: zentraler Knoten. Leistung in "Einheiten": Kraftwerk 40 (+20 mit Modul), Solarpaneel 1;
// Verbraucher 1, grosse (Fabrik/Forschung/Raffinerie) 3.
constexpr int GEN_CAPACITY = 40;
constexpr int GEN_MODULE_CAPACITY = 20;
constexpr int PANEL_CAPACITY = 1;
// (Wunsch 18.11. 12:54 "Ausbilden geht immer noch nicht. Bei KEINER Einheit.") Die Fabrik stand im PowerDown: das Kraftwerk hatte kein
// Oel mehr (oder die Fabrik lag ausserhalb des Netzes) - dann lief gar nichts. Die Kommandozentrale liefert jetzt Notstrom: eine stetige
// kleine Quelle (reicht fuer Fabrik + Forschung in ihrer Naehe), auch ohne Oel. Volle Leistung weiterhin nur mit laufendem Kraftwerk.
constexpr int NOTSTROM_CAPACITY = 6;
constexpr int SMALL_DEMAND = 1;
constexpr int BIG_DEMAND = 3;
constexpr int PANEL_LINK_RANGE = 2 * 128;     // Paneele bis 2 Felder auseinander bilden ein Solarfeld
constexpr int SUB_SUPPLY_RANGE = 5 * 128;     // Versorgungsradius direkt um das Umspannwerk
constexpr int SUB_CABLE_HEIGHT = 120;
constexpr int PANEL_CABLE_HEIGHT = 30;

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
bool s_hasSub[MAX_PLAYERS] = {};                   // (20:35) Spieler hat ein Umspannwerk
std::set<uint32_t> s_standby;                      // Kraftwerke in Bereitschaft (Solarstrom reicht)
std::set<int> s_standbyNet;
bool s_hasGen[MAX_PLAYERS] = {};                   // PowerDown nur, wenn der Spieler ueberhaupt ein Kraftwerk hat (sonst kein Wiederanlauf moeglich)
std::unordered_map<uint32_t, bool> s_held;           // wir haben die Fabrik/Forschung selbst auf Halt gesetzt (nicht der Spieler)
constexpr int BASE_GEN_RANGE = 9 * 128;              // Fabriken/Forschung: 9 Felder um ein laufendes Kraftwerk
constexpr unsigned RESTART_STAGGER_MS = 1200;        // MF: Gebaeude fahren einzeln nacheinander hoch
// Besatzungskonto (MF-Manpower): Kryo-Farm stellt Besatzung, Fabriken/Forschung/Raffinerie/Aussenposten und MF-Einheiten binden welche
std::unordered_map<uint32_t, unsigned> s_crewBorn;   // Zeitpunkt, zu dem eine Kryo-Farm erstmals fertig gesehen wurde (Wachstum +1/s)
std::unordered_set<uint32_t> s_crewOff;              // wegen Besatzungsmangel abgeschaltete Gebaeude (jede Spielsekunde neu berechnet, neueste zuerst)
int s_crewUsed[MAX_PLAYERS] = {};
int s_crewSupply[MAX_PLAYERS] = {};
bool s_crewActive[MAX_PLAYERS] = {};
int s_crewOffCount[MAX_PLAYERS] = {};
std::vector<Cable> s_cables;
std::vector<Marker> s_markers;
bool s_have = false;
UDWORD s_lastSec = 0;
UDWORD s_lastTime = 0;

// Besatzungskonto eines Spielers neu rechnen; schaltet bei Mangel die neuesten Verbraucher ab (s_crewOff)
void updateCrew(int p)
{
	const bool wasActive = s_crewActive[p];
	s_crewActive[p] = false;
	s_crewUsed[p] = 0;
	s_crewSupply[p] = 0;
	int off = 0;
	if (modoil::manages(p))
	{
		int supply = 0, used = 0;
		bool activator = false;
		std::vector<const STRUCTURE *> consumers;
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT) { continue; }
			const STRUCTURE_STATS *st = s->pStructureType;
			if (st->crewProvide > 0)
			{
				int now = st->crewProvide;
				if (st->crewStart > 0)
				{
					activator = true;
					auto it = s_crewBorn.find(s->id);
					if (it == s_crewBorn.end()) { it = s_crewBorn.emplace(s->id, gameTime).first; }
					now = std::min(st->crewProvide, st->crewStart + static_cast<int>((gameTime - it->second) / 1000));
				}
				supply += now;
			}
			if (st->crewCost > 0)
			{
				used += st->crewCost;
				if (st->requiresPower) { consumers.push_back(s); }
			}
		}
		if (activator)
		{
			for (const DROID *d : gameWorld.objects.droids[p])
			{
				if (!d->died && d->getBodyStats() != nullptr) { used += d->getBodyStats()->crewCost; }
			}
			s_crewActive[p] = true;
			s_crewUsed[p] = used;
			s_crewSupply[p] = supply;
			if (used > supply)
			{
				std::sort(consumers.begin(), consumers.end(), [](const STRUCTURE *a, const STRUCTURE *b) { return a->id > b->id; });
				for (const STRUCTURE *s : consumers)
				{
					if (used <= supply) { break; }
					s_crewOff.insert(s->id);
					used -= s->pStructureType->crewCost;
					off++;
				}
			}
		}
	}
	if (p == static_cast<int>(selectedPlayer))
	{
		char buf[160];
		if (s_crewActive[p] && !wasActive)
		{
			snprintf(buf, sizeof(buf), "Besatzungskonto aktiv: %d/%d", s_crewUsed[p], s_crewSupply[p]);
			addConsoleMessage(buf, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		}
		else if (off > s_crewOffCount[p])
		{
			snprintf(buf, sizeof(buf), "Besatzung reicht nicht (%d/%d): %d Gebaeude werden abgeschaltet", s_crewUsed[p], s_crewSupply[p], off);
			addConsoleMessage(buf, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		}
		else if (off == 0 && s_crewOffCount[p] > 0)
		{
			snprintf(buf, sizeof(buf), "Besatzung wieder ausreichend (%d/%d)", s_crewUsed[p], s_crewSupply[p]);
			addConsoleMessage(buf, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		}
	}
	s_crewOffCount[p] = off;
}

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
	s_crewBorn.clear();
	s_crewOff.clear();
	for (int p = 0; p < MAX_PLAYERS; p++) { s_crewActive[p] = false; s_crewUsed[p] = 0; s_crewSupply[p] = 0; s_crewOffCount[p] = 0; }
	s_held.clear();
	s_standby.clear();
	for (int p = 0; p < MAX_PLAYERS; p++) { s_hasSub[p] = false; }
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
	s_crewOff.clear();
	s_standby.clear();
	for (int p = 0; p < MAX_PLAYERS; p++) { updateCrew(p); }
	const bool night = modweather::isNight(gameTime);

	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		unsigned restartIndex = 0;
		s_hasGen[p] = false;
		s_hasSub[p] = false;
		std::vector<Node> poles, gens, subs, panels, cons;
		std::vector<int> genCap;
		std::vector<int> panelCap;   // Solarpaneel 1, laufendes Atomkraftwerk 150 (stetige Quellen)
		std::vector<char> panelNuke;
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->status != SS_BUILT) { continue; }
			const STRUCTURE_STATS *st = s->pStructureType;
			if (isPole(st)) { poles.push_back({s->id, s->pos.x, s->pos.y, s->pos.z}); }
			else if (isSubstation(st)) { subs.push_back({s->id, s->pos.x, s->pos.y, s->pos.z}); s_hasSub[p] = true; }
			else if (st->type == REF_POWER_GEN)
			{
				s_hasGen[p] = true;
				// alle Kraftwerke haengen am Netz; Leistung liefern nur die mit Brennstoff (oder in Bereitschaft)
				gens.push_back({s->id, s->pos.x, s->pos.y, s->pos.z});
				genCap.push_back(modoil::generatorRunning(s) ? GEN_CAPACITY + GEN_MODULE_CAPACITY * static_cast<int>(s->capacity) : 0);
			}
			else if (st->type == REF_HQ)
			{
				panels.push_back({s->id, s->pos.x, s->pos.y, s->pos.z});
				panelCap.push_back(NOTSTROM_CAPACITY);
				panelNuke.push_back(2);   // 2 = Notstrom der Kommandozentrale
			}
			else if (st->incomeProvide > 0)
			{
				// (Wunsch 17.11. 20:35) Solarpaneele speisen das Netz: an der Oberflaeche nur tagsueber, auf den Orbit-Inseln immer
				const modlayer::Layer ly = modlayer::layerOf(s);
				if (ly == modlayer::ORBIT || (ly == modlayer::SURFACE && !night))
				{
					panels.push_back({s->id, s->pos.x, s->pos.y, s->pos.z});
					panelCap.push_back(PANEL_CAPACITY);
					panelNuke.push_back(0);
				}
			}
			else if (modnuke::isPlant(st) || modnuke::isHydro(st))
			{
				// (Wunsch 17.11. 21:13) Atomkraftwerk: stetige Quelle wie die Sonne, aber viel staerker (Tag und Nacht, solange Uran da ist)
				s_hasGen[p] = true;
				panels.push_back({s->id, s->pos.x, s->pos.y, s->pos.z});
				panelCap.push_back(modnuke::gridCapacity(s));
				panelNuke.push_back(1);
			}
			if (st->requiresPower)
			{
				const bool big = st->type == REF_FACTORY || st->type == REF_CYBORG_FACTORY || st->type == REF_VTOL_FACTORY || st->type == REF_RESEARCH || st->id.startsWith("A0BaBaRefinery");
				cons.push_back({s->id, s->pos.x, s->pos.y, s->pos.z, big});
			}
		}
		if (poles.empty() && gens.empty() && cons.empty() && subs.empty()) { continue; }
		const bool subMode = !subs.empty();   // (20:35) ab dem ersten Umspannwerk ist es der zentrale Knoten des Netzes

		const int n = static_cast<int>(poles.size());
		const int g = static_cast<int>(gens.size());
		const int m = static_cast<int>(subs.size());
		const int q = static_cast<int>(panels.size());
		const int G0 = n, S0 = n + g, P0 = n + g + m;
		std::vector<int> par(n + g + m + q);
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

		// Kraftwerke, Umspannwerke und Solarpaneele haengen sich an die Masten (und Umspannwerke) in Reichweite; Kabel zum naechsten
		struct Link { int node; int x, y, z; int hb; int tx, ty, tz; int ht; };
		std::vector<Link> extraCables;
		auto attach = [&](int node, const Node &a, int range, int heightA)
		{
			int best = -1;
			int64_t bestD = 0;
			bool bestIsSub = false;
			for (int i = 0; i < n; i++)
			{
				const int64_t d = dist2(a, poles[i]);
				if (d <= static_cast<int64_t>(range) * range)
				{
					unite(par, node, i);
					if (best < 0 || d < bestD) { best = i; bestD = d; bestIsSub = false; }
				}
			}
			for (int k = 0; k < m; k++)
			{
				if (S0 + k == node) { continue; }
				const int64_t d = dist2(a, subs[k]);
				if (d <= static_cast<int64_t>(range) * range)
				{
					unite(par, node, S0 + k);
					if (best < 0 || d < bestD) { best = k; bestD = d; bestIsSub = true; }
				}
			}
			if (best >= 0)
			{
				const Node &b = bestIsSub ? subs[best] : poles[best];
				extraCables.push_back({node, a.x, a.y, a.z, heightA, b.x, b.y, b.z, bestIsSub ? SUB_CABLE_HEIGHT : CABLE_HEIGHT});
			}
		};
		for (int k = 0; k < m; k++) { attach(S0 + k, subs[k], LINK_RANGE, SUB_CABLE_HEIGHT); }
		for (int k = 0; k < g; k++) { attach(G0 + k, gens[k], GEN_LINK_RANGE, GEN_CABLE_HEIGHT); }
		for (int k = 0; k < q; k++)
		{
			// Solarfeld: Paneele dicht beieinander bilden ein Feld (ein Mast/Umspannwerk genuegt fuer das ganze Feld)
			for (int j = 0; j < k; j++)
			{
				if (dist2(panels[k], panels[j]) <= static_cast<int64_t>(PANEL_LINK_RANGE) * PANEL_LINK_RANGE) { unite(par, P0 + k, P0 + j); }
			}
			attach(P0 + k, panels[k], GEN_LINK_RANGE, PANEL_CABLE_HEIGHT);
		}

		// Netze: Quellen (laufende Kraftwerke, Solarpaneele), Umspannwerke, Leistung
		const int total = n + g + m + q;
		std::vector<int> solarCap(total, 0), genCapC(total, 0), demand(total, 0);
		std::vector<char> hasSub(total, 0), hasGenC(total, 0), realSrc(total, 0);
		std::vector<int> emergCap(total, 0);
		for (int k = 0; k < q; k++)
		{
			const int r = find(par, P0 + k);
			solarCap[r] += panelCap[static_cast<size_t>(k)];
			if (panelNuke[static_cast<size_t>(k)] && panelCap[static_cast<size_t>(k)] > 0) { hasGenC[r] = 1; }   // ohne Umspannwerk: speist wie ein Kraftwerk
			if (panelNuke[static_cast<size_t>(k)] == 1 && panelCap[static_cast<size_t>(k)] > 0) { realSrc[r] = 1; }
			if (panelNuke[static_cast<size_t>(k)] == 2) { emergCap[r] += panelCap[static_cast<size_t>(k)]; }
		}
		for (int k = 0; k < g; k++)
		{
			if (genCap[static_cast<size_t>(k)] > 0) { realSrc[find(par, G0 + k)] = 1; }
		}
		for (int k = 0; k < g; k++)
		{
			genCapC[find(par, G0 + k)] += genCap[static_cast<size_t>(k)];
			if (genCap[static_cast<size_t>(k)] > 0) { hasGenC[find(par, G0 + k)] = 1; }
		}
		std::vector<char> anyGen(total, 0);
		for (int k = 0; k < g; k++) { anyGen[find(par, G0 + k)] = 1; }
		for (int k = 0; k < m; k++) { hasSub[find(par, S0 + k)] = 1; }

		// Netze, die Strom haben koennten (ohne Umspannwerk: ein laufendes Kraftwerk; mit: Umspannwerk + Quelle)
		std::vector<char> live(total, 0);
		for (int r = 0; r < total; r++)
		{
			if (find(par, r) != r) { continue; }
			live[r] = subMode ? (hasSub[r] && (solarCap[r] > 0 || genCapC[r] > 0)) : hasGenC[r];
		}
		// Verbraucher einem Netz zuordnen: Versorgungsbereich eines Mastes oder Umspannwerks (ohne Umspannwerk wie bisher auch eines Kraftwerks);
		// liegen mehrere in Reichweite, gewinnt ein Netz mit Strom, dann das naechste
		std::vector<int> consNet(cons.size(), -1);
		for (size_t ci = 0; ci < cons.size(); ci++)
		{
			const Node &c = cons[ci];
			int net = -1;
			int64_t bestD = INT64_MAX;
			bool bestLive = false;
			auto consider = [&](int r, int64_t d)
			{
				const bool lv = live[r] != 0;
				if (net < 0 || (lv && !bestLive) || (lv == bestLive && d < bestD)) { net = r; bestD = d; bestLive = lv; }
			};
			for (int i = 0; i < n; i++)
			{
				const int64_t d = dist2(c, poles[i]);
				if (d <= static_cast<int64_t>(SUPPLY_RANGE) * SUPPLY_RANGE) { consider(find(par, i), d); }
			}
			for (int k = 0; k < m; k++)
			{
				const int64_t d = dist2(c, subs[k]);
				if (d <= static_cast<int64_t>(SUB_SUPPLY_RANGE) * SUB_SUPPLY_RANGE) { consider(find(par, S0 + k), d); }
			}
			if (!subMode)
			{
				const int64_t genR = c.big ? BASE_GEN_RANGE : GEN_LINK_RANGE;
				for (int k = 0; k < g; k++)
				{
					const int64_t d = dist2(c, gens[k]);
					if (d <= genR * genR) { consider(find(par, G0 + k), d); }
				}
				for (int k = 0; k < q; k++)
				{
					if (!panelNuke[static_cast<size_t>(k)]) { continue; }
					const int64_t d = dist2(c, panels[k]);
					if (d <= genR * genR) { consider(find(par, P0 + k), d); }
				}
			}
			consNet[ci] = net;
			if (net >= 0) { demand[net] += c.big ? BIG_DEMAND : SMALL_DEMAND; }
		}

		// Leistung je Netz: reicht der Solarstrom, gehen die Kraftwerke in Bereitschaft (verbrauchen kein Oel - das Oel geht an die Tankstellen)
		std::vector<int> capacity(total, 0);
		std::vector<char> fed(total, 0);
		for (int r = 0; r < total; r++)
		{
			if (find(par, r) != r) { continue; }
			if (subMode)
			{
				if (!hasSub[r]) { continue; }
				if (solarCap[r] > 0 && solarCap[r] >= demand[r] && anyGen[r]) { capacity[r] = solarCap[r]; s_standbyNet.insert(r); }
				else { capacity[r] = solarCap[r] + genCapC[r]; }
				fed[r] = capacity[r] > 0;
			}
			else
			{
				fed[r] = hasGenC[r];
				capacity[r] = fed[r] ? INT32_MAX / 4 : 0;   // ohne Umspannwerk wie bisher: ein laufendes Kraftwerk versorgt alles am Netz
				if (fed[r] && !realSrc[r]) { capacity[r] = emergCap[r]; }   // nur Notstrom der Kommandozentrale
			}
		}
		for (int k = 0; k < g; k++)
		{
			if (s_standbyNet.count(find(par, G0 + k))) { s_standby.insert(gens[k].id); }
		}
		s_standbyNet.clear();

		const bool show = p == static_cast<int>(selectedPlayer) || aiCheckAlliances(p, static_cast<int>(selectedPlayer));
		if (show)
		{
			for (const auto &e : cableEdges)
			{
				const Node &a = poles[e.first], &b = poles[e.second];
				s_cables.push_back({p, a.x, a.y, a.z + CABLE_HEIGHT, b.x, b.y, b.z + CABLE_HEIGHT, fed[find(par, e.first)] != 0});
			}
			for (const Link &l : extraCables)
			{
				s_cables.push_back({p, l.x, l.y, l.z + l.hb, l.tx, l.ty, l.tz + l.ht, fed[find(par, l.node)] != 0});
			}
			for (int i = 0; i < n; i++)
			{
				s_markers.push_back({p, poles[i].x, poles[i].y, poles[i].z, fed[find(par, i)] != 0, true});
			}
			for (int k = 0; k < m; k++)
			{
				s_markers.push_back({p, subs[k].x, subs[k].y, subs[k].z, fed[find(par, S0 + k)] != 0, true});
			}
		}

		// Verbraucher: versorgt, wenn ihr Netz gespeist ist und die Leistung reicht (in fester Reihenfolge - bei Ueberlast fallen die letzten aus)
		std::vector<int> used(total, 0);
		for (size_t ci = 0; ci < cons.size(); ci++)
		{
			const Node &c = cons[ci];
			const int net = consNet[ci];
			bool ok = false;
			if (net >= 0 && fed[net])
			{
				const int need = c.big ? BIG_DEMAND : SMALL_DEMAND;
				if (used[net] + need <= capacity[net]) { used[net] += need; ok = true; }
			}
			if (s_crewOff.find(c.id) != s_crewOff.end()) { ok = false; }   // Besatzungsmangel: wie ohne Strom abgeschaltet
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

bool generatorStandby(const STRUCTURE *psGen)
{
	return psGen != nullptr && s_standby.count(psGen->id) != 0;
}

bool isSubstation(const STRUCTURE_STATS *psStats)
{
	return psStats != nullptr && psStats->id.startsWith("A0Substation");
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
	if (!st->requiresPower || s->status != SS_BUILT || !modoil::manages(s->player) || !(s_hasGen[s->player] || s_hasSub[s->player])) { return false; }
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
				addConsoleMessage(("PowerDown: " + std::string(getStatsName(st)) + (s_crewOff.count(s->id) ? " (Besatzung)" : "")).c_str(), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
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
