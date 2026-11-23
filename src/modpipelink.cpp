// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Knopf "Mit naechstem Kraftwerk verbinden" am gewaehlten Oelturm (siehe modpipelink.h)

#include "modpipelink.h"
#include "modoil.h"
#include "modui.h"
#include "modwzhd.h"

#include "lib/framework/frame.h"
#include "lib/framework/wzstring.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/button.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/ivis_opengl/bitimage.h"
#include "lib/netplay/netplay.h"

#include "console.h"
#include "droid.h"
#include "game_world.h"
#include "hci.h"
#include "intfac.h"
#include "intimage.h"
#include "map.h"
#include "order.h"
#include "orderdef.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace modpipelink
{

namespace
{

constexpr int BTN_W = 236, BTN_H = 34;
constexpr int MAX_VISIT = 60000;      // Suchgrenze (Felder) fuer den Rohrweg

std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_BUTTON> s_btn;
uint32_t s_derrickId = 0;

using Key = std::pair<int, int>;

STRUCTURE *selectedDerrick()
{
	if (selectedPlayer >= MAX_PLAYERS || !modoil::manages(static_cast<int>(selectedPlayer))) { return nullptr; }
	STRUCTURE *found = nullptr;
	for (STRUCTURE *s : gameWorld.objects.structures[selectedPlayer])
	{
		if (s == nullptr || s->died || !s->selected) { continue; }
		if (found != nullptr) { return nullptr; }   // mehrere gewaehlt
		found = s;
	}
	if (found == nullptr || found->status != SS_BUILT || found->pStructureType == nullptr || found->pStructureType->type != REF_RESOURCE_EXTRACTOR) { return nullptr; }
	return found;
}

int ufind(std::vector<int> &par, int x)
{
	while (par[static_cast<size_t>(x)] != x)
	{
		par[static_cast<size_t>(x)] = par[static_cast<size_t>(par[static_cast<size_t>(x)])];
		x = par[static_cast<size_t>(x)];
	}
	return x;
}

void say(const std::string &msg)
{
	addConsoleMessage(msg.c_str(), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
}

/// Bautrupp fuer Rohre: echter Bau-Lkw / Bau-Cyborg, kein Fracht-Lkw, kein Bohr-/Abrissfahrzeug
bool isBuilder(const DROID *d)
{
	if (d == nullptr || d->died || (d->droidType != DROID_CONSTRUCT && d->droidType != DROID_CYBORG_CONSTRUCT)) { return false; }
	if (modoil::isCargoVehicle(d)) { return false; }
	const CONSTRUCT_STATS *cs = d->getConstructStats();
	if (cs == nullptr) { return false; }
	const std::string id = cs->id.toUtf8();
	return id != "DrillRig" && id != "WreckBall" && id.find("Bucket") == std::string::npos && id.find("ScavBed") == std::string::npos && id != "ScavTank";
}

bool idle(const DROID *d)
{
	return d->order.type == DORDER_NONE || d->order.type == DORDER_HOLD || d->order.type == DORDER_STOP
	       || (d->order.type == DORDER_GUARD && d->order.psObj == nullptr);
}

/// Rohrweg vom Oelturm zum naechsten Netz mit freiem Kraftwerksplatz bzw. zum naechsten Kraftwerk mit Platz planen und bauen lassen
void connect(STRUCTURE *der)
{
	if (der == nullptr || der->died) { return; }
	const int P = static_cast<int>(der->player);
	if (modoil::derrickPiped(der)) { say("Dieser \xC3\x96lturm h\xC3\xA4ngt schon per Pipeline an einem Kraftwerk - er flie\xC3\x9Ft st\xC3\xA4ndig."); return; }
	const int statIdx = getStructStatFromName(WzString::fromUtf8("A0BaBaPipeline"));
	if (statIdx < 0) { return; }
	STRUCTURE_STATS *pipeStats = &asStructureStats[static_cast<size_t>(statIdx)];
	if (apStructTypeLists[P][statIdx] != AVAILABLE) { say("Die Pipeline ist noch nicht verf\xC3\xBC" "gbar."); return; }
	const int PS = modoil::pipeStep();

	// Rohrnetze (Kante an Kante wie in modoil)
	std::map<Key, int> idx;
	std::vector<int> par;
	std::vector<STRUCTURE *> gens, ders;
	for (STRUCTURE *s : gameWorld.objects.structures[P])
	{
		if (s == nullptr || s->died || s->status != SS_BUILT || s->pStructureType == nullptr) { continue; }
		if (modoil::isPipeStructure(s))
		{
			const Vector2i k = modoil::pipeKey(s);
			const Key kk(k.x, k.y);
			if (idx.find(kk) == idx.end()) { idx[kk] = static_cast<int>(par.size()); par.push_back(static_cast<int>(par.size())); }
		}
		else if (s->pStructureType->type == REF_POWER_GEN) { gens.push_back(s); }
		else if (s->pStructureType->type == REF_RESOURCE_EXTRACTOR && s != der) { ders.push_back(s); }
	}
	for (const auto &kv : idx)
	{
		for (int ddx = -PS; ddx <= PS; ddx++)
		{
			for (int ddy = -PS; ddy <= PS; ddy++)
			{
				if (std::max(std::abs(ddx), std::abs(ddy)) != PS || std::min(std::abs(ddx), std::abs(ddy)) >= PS) { continue; }
				const auto r = idx.find(Key(kv.first.first + ddx, kv.first.second + ddy));
				if (r != idx.end())
				{
					const int a = ufind(par, kv.second), b = ufind(par, r->second);
					if (a != b) { par[static_cast<size_t>(a)] = b; }
				}
			}
		}
	}
	auto rootsOf = [&](const STRUCTURE *s)
	{
		std::set<int> roots;
		for (const auto &kv : idx)
		{
			if (modoil::pipeTouches(s, Vector2i(kv.first.first, kv.first.second))) { roots.insert(ufind(par, kv.second)); }
		}
		return roots;
	};
	// freie Kraftwerksplaetze je Netz
	std::map<int, int> cap;
	std::vector<std::set<int>> genRoots;
	for (const STRUCTURE *g : gens)
	{
		genRoots.push_back(rootsOf(g));
		for (int r : genRoots.back()) { cap[r] += modoil::PIPE_PER_GEN; }
	}
	for (const STRUCTURE *d : ders)
	{
		for (int r : rootsOf(d)) { auto it = cap.find(r); if (it != cap.end()) { it->second--; } }
	}
	for (int r : rootsOf(der))
	{
		const auto it = cap.find(r);
		if (it == cap.end()) { continue; }
		if (it->second > 0) { say("Der \xC3\x96lturm h\xC3\xA4ngt schon am Rohrnetz eines Kraftwerks - er wird gleich angeschlossen."); return; }
		say("Der \xC3\x96lturm h\xC3\xA4ngt am Rohrnetz, aber alle Kraftwerke dort sind voll (je Kraftwerk 4) - ein weiteres Kraftwerk ans Netz anschlie\xC3\x9F" "en.");
		return;
	}

	// Ziele: Felder Kante an Kante neben einem Rohr eines Netzes mit freiem Platz, oder neben einem Kraftwerk mit Platz
	std::set<Key> goals;
	for (const auto &kv : idx)
	{
		const auto it = cap.find(ufind(par, kv.second));
		if (it == cap.end() || it->second <= 0) { continue; }
		for (int o = -(PS - 1); o <= PS - 1; o++)
		{
			goals.insert(Key(kv.first.first + PS, kv.first.second + o));
			goals.insert(Key(kv.first.first - PS, kv.first.second + o));
			goals.insert(Key(kv.first.first + o, kv.first.second + PS));
			goals.insert(Key(kv.first.first + o, kv.first.second - PS));
		}
	}
	std::vector<const STRUCTURE *> goalGens;
	for (size_t k = 0; k < gens.size(); k++)
	{
		bool free = genRoots[k].empty();
		for (int r : genRoots[k]) { if (cap[r] > 0) { free = true; } }
		if (free) { goalGens.push_back(gens[k]); }
	}
	if (goals.empty() && goalGens.empty())
	{
		say(gens.empty() ? "Kein Kraftwerk - zuerst ein Kraftwerk bauen." : "Alle Kraftwerke sind voll (je Kraftwerk 4 \xC3\x96lt\xC3\xBCrme) - ein weiteres Kraftwerk bauen.");
		return;
	}
	auto isGoal = [&](const Key &c)
	{
		if (goals.count(c) != 0) { return true; }
		for (const STRUCTURE *g : goalGens) { if (modoil::pipeTouches(g, Vector2i(c.first, c.second))) { return true; } }
		return false;
	};
	auto placeable = [&](const Key &c)
	{
		if (idx.count(c) != 0) { return false; }
		const Vector2i w(modoil::pipeKeyToWorld(c.first), modoil::pipeKeyToWorld(c.second));
		return validLocation(gameWorld, pipeStats, w, 0, static_cast<unsigned>(P), true);
	};

	// Suchfeld: Oelturm und Ziele mit Rand
	const Vector2i dk(map_coord(der->pos.x), map_coord(der->pos.y));
	int minX = dk.x, maxX = dk.x, minY = dk.y, maxY = dk.y;
	for (const Key &g : goals) { minX = std::min(minX, g.first); maxX = std::max(maxX, g.first); minY = std::min(minY, g.second); maxY = std::max(maxY, g.second); }
	for (const STRUCTURE *g : goalGens)
	{
		const Vector2i gk(map_coord(g->pos.x), map_coord(g->pos.y));
		minX = std::min(minX, gk.x); maxX = std::max(maxX, gk.x); minY = std::min(minY, gk.y); maxY = std::max(maxY, gk.y);
	}
	const int margin = 10 * PS;
	minX -= margin; maxX += margin; minY -= margin; maxY += margin;

	// Breitensuche in Rohr-Schritten (Kante an Kante), Start = Felder, die den Oelturm beruehren
	std::map<Key, Key> from;
	std::deque<Key> q;
	const StructureBounds db = getStructureBounds(der);
	for (int x = db.map.x - PS; x <= db.map.x + db.size.x - 1 + PS; x++)
	{
		for (int y = db.map.y - PS; y <= db.map.y + db.size.y - 1 + PS; y++)
		{
			const Key c(x, y);
			if (!placeable(c)) { continue; }
			from[c] = c;
			q.push_back(c);
		}
	}
	Key end(INT32_MIN, INT32_MIN);
	int visited = 0;
	while (!q.empty() && visited < MAX_VISIT)
	{
		const Key c = q.front();
		q.pop_front();
		++visited;
		if (isGoal(c)) { end = c; break; }
		const Key nb[4] = {Key(c.first + PS, c.second), Key(c.first - PS, c.second), Key(c.first, c.second + PS), Key(c.first, c.second - PS)};
		for (const Key &n : nb)
		{
			if (n.first < minX || n.first > maxX || n.second < minY || n.second > maxY || from.count(n) != 0) { continue; }
			if (!placeable(n)) { continue; }
			from[n] = c;
			q.push_back(n);
		}
	}
	if (end.first == INT32_MIN)
	{
		say("Kein Weg f\xC3\xBCr die Pipeline gefunden - Hindernisse zwischen \xC3\x96lturm und Kraftwerk.");
		return;
	}
	std::vector<Key> path;
	for (Key c = end;; c = from[c])
	{
		path.push_back(c);
		if (from[c] == c) { break; }
	}
	std::reverse(path.begin(), path.end());   // vom Oelturm zum Netz / Kraftwerk

	// die naechsten Bautrupps (bevorzugt freie) legen die Pipeline Stueck fuer Stueck
	const Vector2i start(modoil::pipeKeyToWorld(path.front().first), modoil::pipeKeyToWorld(path.front().second));
	std::vector<std::pair<int64_t, DROID *>> cand;
	for (DROID *d : gameWorld.objects.droids[P])
	{
		if (!isBuilder(d)) { continue; }
		const int64_t dx = d->pos.x - start.x, dy = d->pos.y - start.y;
		const int64_t dd = dx * dx + dy * dy + (idle(d) ? 0 : (int64_t(1) << 40));   // freie zuerst
		cand.emplace_back(dd, d);
	}
	if (cand.empty()) { say("Kein Bautrupp da, der die Pipeline legen k\xC3\xB6nnte."); return; }
	std::sort(cand.begin(), cand.end(), [](const auto &a, const auto &b) { return a.first != b.first ? a.first < b.first : a.second->id < b.second->id; });
	const size_t crews = std::min<size_t>(cand.size(), idle(cand.front().second) && cand.size() > 1 && idle(cand[1].second) ? 2 : 1);
	for (size_t i = 0; i < crews; i++)
	{
		DROID *d = cand[i].second;
		bool add = !idle(d);
		for (const Key &c : path)
		{
			orderDroidStatsLocDirAdd(d, DORDER_BUILD, pipeStats, static_cast<UDWORD>(modoil::pipeKeyToWorld(c.first)), static_cast<UDWORD>(modoil::pipeKeyToWorld(c.second)), 0, add);
			add = true;
		}
	}
	say("Pipeline zum Kraftwerk geplant: " + std::to_string(path.size()) + " St\xC3\xBC" "ck" + (crews > 1 ? " - zwei Bautrupps sind unterwegs." : " - ein Bautrupp ist unterwegs."));
}

void drawButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const STRUCTURE *der = nullptr;
	if (selectedPlayer < MAX_PLAYERS)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[selectedPlayer]) { if (s != nullptr && s->id == s_derrickId) { der = s; break; } }
	}
	const bool piped = der != nullptr && modoil::derrickPiped(der);
	const bool hover = b->isMouseOverWidget() && !piped;
	const bool down = (b->getState() & WBUT_DOWN) != 0 && !piped;
	modwzhd::textButton(x0, y0, w, h, "", piped, hover, down);
	// Sinnbild: WZ-Blitz (Kraftwerk) - mit Rohr-Strich davor
	const int is = h - 8;
	const int iw = iV_GetImageWidth(IntImages, IMAGE_DES_POWER), ih = iV_GetImageHeight(IntImages, IMAGE_DES_POWER);
	const float k = std::min(static_cast<float>(is) / std::max(1, iw), static_cast<float>(is) / std::max(1, ih));
	const int dw = std::max(1, static_cast<int>(iw * k)), dh = std::max(1, static_cast<int>(ih * k));
	const int ix = x0 + 6, iy = y0 + (h - dh) / 2;
	const PIELIGHT pipe = pal_RGBA(150, 150, 160, 255), pipeHi = pal_RGBA(210, 210, 220, 255);
	pie_UniTransBoxFill(static_cast<float>(ix), static_cast<float>(y0 + h / 2 - 2), static_cast<float>(ix + is + 4), static_cast<float>(y0 + h / 2 + 3), pipe);
	pie_UniTransBoxFill(static_cast<float>(ix), static_cast<float>(y0 + h / 2 - 2), static_cast<float>(ix + is + 4), static_cast<float>(y0 + h / 2 - 1), pipeHi);
	modwzhd::drawIntfac(IMAGE_DES_POWER, ix + is + 4, iy, dw, dh, hover);
	const std::string label = piped ? "L\xC3\xA4uft per Pipeline" : "Mit n\xC3\xA4" "chstem Kraftwerk verbinden";
	const int tx = ix + is + dw + 12;
	modui::text(modui::fit(label, x0 + w - tx - 6, 0), tx, y0 + (h - 12) / 2, piped ? pal_RGBA(150, 230, 160, 255) : pal_RGBA(220, 228, 255, 255), 0);
}

void ensureScreen()
{
	if (s_screen) { return; }
	auto screen = W_SCREEN::make();
	screen->psForm->hide();   // verborgene Wurzel = durchlaessig fuer Mausklicks
	auto b = std::make_shared<W_BUTTON>();
	screen->psForm->attach(b);
	b->displayFunction = drawButton;
	b->setTip("Pipeline vom \xC3\x96lturm zum n\xC3\xA4" "chsten Kraftwerk mit freiem Platz legen (k\xC3\xBCrzester Weg, auch an ein bestehendes Rohrnetz). "
	          "Per Pipeline flie\xC3\x9Ft das \xC3\x96l st\xC3\xA4ndig - ohne F\xC3\xA4sser. Je Kraftwerk 4 \xC3\x96lt\xC3\xBCrme.");
	b->addOnClickHandler([](W_BUTTON &)
	{
		const uint32_t id = s_derrickId;
		widgScheduleTask([id]()
		{
			if (selectedPlayer >= MAX_PLAYERS) { return; }
			for (STRUCTURE *s : gameWorld.objects.structures[selectedPlayer])
			{
				if (s != nullptr && s->id == id && !s->died) { connect(s); return; }
			}
		});
	});
	b->hide();
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_btn = b;
}

} // namespace

void tick()
{
	STRUCTURE *der = selectedDerrick();
	if (der == nullptr && !s_screen) { return; }
	ensureScreen();
	if (der == nullptr)
	{
		s_btn->hide();
		s_derrickId = 0;
		return;
	}
	s_derrickId = der->id;
	const float sc = modui::uiScale();
	const int w = static_cast<int>(BTN_W * sc), h = static_cast<int>(BTN_H * sc);
	s_btn->setGeometry((static_cast<int>(screenWidth) - w) / 2, static_cast<int>(screenHeight) - h - static_cast<int>(150 * sc), w, h);
	s_btn->show();
}

void shutdown()
{
	if (s_screen)
	{
		widgRemoveOverlayScreen(s_screen);
	}
	s_btn.reset();
	s_screen.reset();
	s_derrickId = 0;
}

}
