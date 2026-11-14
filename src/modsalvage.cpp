// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Forschung durch erbeutete Teile, siehe modsalvage.h
#include "modsalvage.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "baseobject.h"
#include "console.h"
#include "droid.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "modcombot.h"
#include "modgrid.h"
#include "modlayer.h"
#include "multiplay.h"
#include "power.h"
#include "random.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace modsalvage
{
namespace
{
constexpr unsigned ANALYSE_MS = 25000;    // so lange dauert die Analyse eines Teils in einer Forschungsstation
constexpr int ARTEFACT_PARTS = 2;         // ein Artefakt aus einer zerstoerten Anlage verraet bis zu so viele Teile
constexpr int PLAIN_DROP_PCT = 5;         // Chance, dass eine zerstoerte normale Kampfeinheit ein Teil hinterlaesst

struct Comp
{
	const modcombot::Part *part = nullptr;
	COMPONENT_TYPE type = COMP_NUMCOMPONENTS;
	unsigned index = 0;
	std::string name;
	unsigned scrap = 10;
	bool basic = false;
};

std::vector<Comp> s_comps;
bool s_loaded = false;
std::unordered_map<unsigned, unsigned> s_prog[MAX_PLAYERS];   // Analysefortschritt in ms je Teil (Schluessel = Index in s_comps)

void load()
{
	if (s_loaded) { return; }
	s_loaded = true;
	s_comps.clear();
	if (!modcombot::ready()) { return; }
	for (const modcombot::Part &p : modcombot::parts())
	{
		const char *prefix = p.kind == modcombot::KIND_TORSO ? "MFT_" : (p.kind == modcombot::KIND_LEG ? "MFL_" : "MFA_");
		COMPONENT_STATS *cs = getCompStatsFromName(WzString::fromUtf8(std::string(prefix) + p.id));
		if (cs == nullptr || cs->compType >= COMP_NUMCOMPONENTS) { continue; }
		Comp c;
		c.part = &p;
		c.type = cs->compType;
		c.index = static_cast<unsigned>(cs->index);
		c.name = p.name;
		c.scrap = std::max(10u, cs->buildPower / 4u);
		c.basic = p.file == "Basic";
		s_comps.push_back(c);
	}
}

const Comp *compOf(const modcombot::Part *p)
{
	load();
	for (const Comp &c : s_comps) { if (c.part == p) { return &c; } }
	return nullptr;
}

const Comp *compOfFeature(const FEATURE *f)
{
	if (f == nullptr || f->psStats == nullptr) { return nullptr; }
	const std::string id = f->psStats->id.toUtf8();
	if (id.size() < 5 || id.compare(0, 4, "MFP_") != 0) { return nullptr; }
	return compOf(modcombot::find(id.substr(4)));
}

int availability(int player, const Comp &c)
{
	if (player < 0 || player >= MAX_PLAYERS) { return UNAVAILABLE; }
	const UBYTE *list = apCompLists[player][c.type];
	return list == nullptr ? static_cast<int>(UNAVAILABLE) : static_cast<int>(list[c.index]);
}

void setAvailability(int player, const Comp &c, int state)
{
	if (player < 0 || player >= MAX_PLAYERS || apCompLists[player][c.type] == nullptr) { return; }
	apCompLists[player][c.type][c.index] = static_cast<UBYTE>(state);
}

void say(int player, const std::string &text)
{
	if (player == static_cast<int>(selectedPlayer)) { addConsoleMessage(text.c_str(), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
}

void sound(int player, int id)
{
	if (player == static_cast<int>(selectedPlayer)) { audio_QueueTrack(id); }
}

int researchFacilities(int player)
{
	int n = 0;
	for (const STRUCTURE *s : gameWorld.objects.structures[player])
	{
		if (s == nullptr || s->died || s->pStructureType == nullptr || s->pStructureType->type != REF_RESEARCH || s->status != SS_BUILT) { continue; }
		if (modgrid::isPoweredDown(s)) { continue; }
		++n;
	}
	return n;
}

/// legt ein Beute-Feature fuer das Teil in die Naehe von (x, y)
void spawn(const Comp &c, int x, int y)
{
	const std::string fid = "MFP_" + c.part->id;
	FEATURE_STATS *fs = nullptr;
	for (auto &s : asFeatureStats)
	{
		if (s.id.toUtf8() == fid) { fs = &s; break; }
	}
	if (fs == nullptr || fs->psImd == nullptr) { return; }
	x += static_cast<int>(gameRand(129)) - 64;
	y += static_cast<int>(gameRand(129)) - 64;
	if (x < 0 || y < 0 || !tileOnMap(gameWorld.map, map_coord(x), map_coord(y))) { return; }
	FEATURE *f = buildFeature(gameWorld, fs, static_cast<UDWORD>(x), static_cast<UDWORD>(y), false);
	if (f != nullptr) { f->rot.direction = gameRand(DEG(360)); }
}
}   // namespace

bool researchMode()
{
	// Eine Regel fuer jede Spielart (Kampagne, Gefecht, Mehrspieler): Technikstufe 1 = Beute-Forschung.
	return game.techLevel < 2;
}

bool isLocked(int player, const modcombot::Part *part)
{
	if (part == nullptr || !researchMode()) { return false; }
	const Comp *c = compOf(part);
	return c != nullptr && availability(player, *c) != AVAILABLE;
}

bool isAnalysing(int player, const modcombot::Part *part)
{
	if (part == nullptr || !researchMode()) { return false; }
	const Comp *c = compOf(part);
	return c != nullptr && availability(player, *c) == FOUND;
}

bool onPickup(const FEATURE *psFeature, int player)
{
	if (!researchMode()) { return false; }
	const Comp *c = compOfFeature(psFeature);
	if (c == nullptr) { return false; }
	const int st = availability(player, *c);
	if (st == AVAILABLE || st == FOUND)
	{
		// schon bekannt: Schrott bringt etwas Energie
		addPower(player, static_cast<int32_t>(c->scrap));
		say(player, "Salvaged scrap from " + c->name + ": +" + std::to_string(c->scrap) + " power");
		return true;
	}
	setAvailability(player, *c, FOUND);
	s_prog[player][static_cast<unsigned>(c - s_comps.data())] = 0;
	const bool lab = researchFacilities(player) > 0;
	say(player, "Captured part: " + c->name + (lab ? ". Your research facility is analysing it." : ". Build a research facility to analyse it."));
	sound(player, ID_SOUND_ARTIFACT_RECOVERED);
	return true;
}

void onDroidDestroyed(const DROID *d)
{
	if (d == nullptr || !researchMode()) { return; }
	if (modlayer::layerOf(d) != modlayer::SURFACE) { return; }
	load();
	if (s_comps.empty()) { return; }
	std::vector<const Comp *> cand;
	if (modcombot::isModular(d))
	{
		// ein erhaltenes (nicht abgeschlagenes) Teil bleibt liegen; Basis-Teile kennt jeder, die lohnen nicht
		const modcombot::Loadout lo = modcombot::loadoutOf(d);
		const modcombot::Part *ps[4] = { lo.torso, (lo.severed & modcombot::SEV_LEGS) ? nullptr : lo.legs,
		                                 (lo.severed & modcombot::SEV_RARM) ? nullptr : lo.armR, (lo.severed & modcombot::SEV_LARM) ? nullptr : lo.armL };
		for (const modcombot::Part *p : ps)
		{
			const Comp *c = p != nullptr ? compOf(p) : nullptr;
			if (c != nullptr && !c->basic) { cand.push_back(c); }
		}
		if (cand.empty()) { return; }
	}
	else
	{
		if (d->numWeaps == 0 || d->isTransporter() || d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT) { return; }
		if (static_cast<int>(gameRand(100)) >= PLAIN_DROP_PCT) { return; }
		for (const Comp &c : s_comps) { if (!c.basic) { cand.push_back(&c); } }
		if (cand.empty()) { return; }
	}
	const Comp *pick = cand[gameRand(static_cast<UDWORD>(cand.size()))];
	spawn(*pick, d->pos.x, d->pos.y);
}

void onArtefact(int toPlayer, int fromPlayer)
{
	if (!researchMode() || toPlayer < 0 || toPlayer >= MAX_PLAYERS || fromPlayer < 0 || fromPlayer >= MAX_PLAYERS || toPlayer == fromPlayer) { return; }
	load();
	if (s_comps.empty()) { return; }
	std::vector<const Comp *> cand;
	for (const Comp &c : s_comps)
	{
		if (availability(fromPlayer, c) == AVAILABLE && availability(toPlayer, c) == UNAVAILABLE) { cand.push_back(&c); }
	}
	for (int n = 0; n < ARTEFACT_PARTS && !cand.empty(); ++n)
	{
		const size_t k = gameRand(static_cast<UDWORD>(cand.size()));
		const Comp *c = cand[k];
		cand.erase(cand.begin() + static_cast<long>(k));
		setAvailability(toPlayer, *c, FOUND);
		s_prog[toPlayer][static_cast<unsigned>(c - s_comps.data())] = 0;
		say(toPlayer, "Blueprint discovered: " + c->name + ". Analysis starts in your research facility.");
	}
}

void tick()
{
	if (!researchMode()) { return; }
	load();
	if (s_comps.empty()) { return; }
	for (int p = 0; p < MAX_PLAYERS; ++p)
	{
		// gefundene Teile in fester Reihenfolge; jede Forschungsstation analysiert ein Teil
		std::vector<unsigned> found;
		for (unsigned i = 0; i < s_comps.size(); ++i)
		{
			if (availability(p, s_comps[i]) == FOUND) { found.push_back(i); }
		}
		if (found.empty()) { continue; }
		const int labs = researchFacilities(p);
		const size_t slots = std::min(found.size(), static_cast<size_t>(std::max(0, labs)));
		for (size_t k = 0; k < slots; ++k)
		{
			unsigned &prog = s_prog[p][found[k]];
			prog += deltaGameTime;
			if (prog >= ANALYSE_MS)
			{
				const Comp &c = s_comps[found[k]];
				setAvailability(p, c, AVAILABLE);
				s_prog[p].erase(found[k]);
				say(p, "Analysis complete: " + c.name + " is now available in the combot designer.");
				sound(p, ID_SOUND_RESEARCH_COMPLETED);
			}
		}
	}
}

void reset()
{
	s_loaded = false;
	s_comps.clear();
	for (auto &m : s_prog) { m.clear(); }
}
}
