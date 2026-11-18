// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Metal-Fatigue-Besatzung als eigene Einheiten (siehe modcrew.h)
#include "modcrew.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/math_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/sync_debug.h"

#include "action.h"
#include "console.h"
#include "droid.h"
#include "game_world.h"
#include "group.h"
#include "map.h"
#include "mission.h"
#include "modcombot.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "objmem.h"
#include "order.h"
#include "random.h"
#include "stats.h"
#include "structure.h"
#include "template.h"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace modcrew
{
namespace
{
constexpr int BOARD_R = DIST_TILE * 3 / 2;     // so nah muss die Besatzung am Combot sein, um einzusteigen
constexpr unsigned HOME_CHECK_MS = 1000;       // so oft sieht sich eine untaetige Besatzung nach der Halle um
constexpr int SURVIVE_PCT = 75;                 // so oft ueberlebt der Pilot die Zerstoerung seines Combots
constexpr int64_t SAVE_VERSION = 1;

// Rufnamen der Piloten (eigene Liste, international)
const char *const NAMES[] = {
	"Anders", "Bianca", "Carlos", "Dana", "Emil", "Farah", "Goran", "Hana", "Igor", "Jana", "Kai", "Lena", "Marek", "Nadia", "Olaf", "Paula",
	"Quinn", "Rosa", "Sven", "Tamara", "Uwe", "Vera", "Wim", "Xenia", "Yusuf", "Zora", "Aiko", "Bruno", "Cleo", "Dario", "Elif", "Finn",
	"Greta", "Hiro", "Ines", "Jonas", "Kira", "Lars", "Mira", "Nils", "Oskar", "Petra", "Rafael", "Sanna", "Theo", "Ulla", "Viktor", "Wanda",
	"Yara", "Zeno", "Arvid", "Bea", "Cyrus", "Dilan", "Esra", "Fynn", "Gisela", "Henrik", "Ilka", "Jari", "Karla", "Linus", "Malik", "Noor",
};
constexpr uint32_t NAME_COUNT = sizeof(NAMES) / sizeof(NAMES[0]);

struct Member
{
	uint32_t id = 0;
	uint32_t player = 0;
	uint32_t name = 0;
	uint32_t xp = 0;       // WZ-Erfahrung (Stufe = Rangabzeichen), wandert mit dem Piloten
	uint32_t combot = 0;   // fliegt diesen Combot
	uint32_t unit = 0;     // ist als Besatzungs-Einheit zu Fuss unterwegs
};

std::vector<Member> s_members;                    // in Entstehungsreihenfolge (deterministisch)
uint32_t s_nextId = 1;
std::set<uint32_t> s_crewless;                    // Combots ohne Besatzung (neutral)
std::unordered_map<uint32_t, Position> s_lastPos; // Combot -> letzte Lage (zum Aussteigen nach der Zerstoerung)
std::unordered_map<uint32_t, int> s_lastLayer;
std::unordered_map<uint32_t, unsigned> s_nextHome;   // Besatzung -> naechste Umschau nach der Halle
std::unordered_map<uint32_t, std::string> s_baseName;   // Combot -> Name ohne Pilot (nur Anzeige)
std::unordered_map<uint32_t, uint32_t> s_ejectedFrom;   // Besatzung -> Combot, aus dem sie gerade ausgestiegen ist (dort nicht von selbst wieder hinein)
int s_crewBody = -2;

int crewBodyIndex()
{
	if (s_crewBody == -2) { s_crewBody = getCompFromID(COMP_BODY, WzString::fromUtf8("MFCrewBody")); }
	return s_crewBody;
}

const char *nameOf(const Member &m)
{
	return NAMES[m.name % NAME_COUNT];
}

std::string baseName(const DROID *c)
{
	const auto it = s_baseName.find(c->id);
	if (it != s_baseName.end()) { return it->second; }
	std::string n = c->aName;
	const size_t p = n.rfind(" [");
	if (p != std::string::npos && !n.empty() && n.back() == ']') { n.erase(p); }
	s_baseName[c->id] = n;
	return n;
}

void nameCombot(DROID *c, const char *pilot)
{
	const std::string n = baseName(c) + " [" + pilot + "]";
	droidSetName(c, n.c_str());
}

void say(uint32_t player, const std::string &text)
{
	if (player == selectedPlayer) { addConsoleMessage(text.c_str(), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
}

uint32_t newName(uint32_t player)
{
	uint32_t start = gameRand(NAME_COUNT);
	for (uint32_t k = 0; k < NAME_COUNT; ++k)
	{
		const uint32_t n = (start + k) % NAME_COUNT;
		bool used = false;
		for (const Member &m : s_members) { if (m.player == player && m.name == n) { used = true; break; } }
		if (!used) { return n; }
	}
	return start;
}

Member &newMember(uint32_t player, uint32_t xp)
{
	Member m;
	m.id = s_nextId++;
	m.player = player;
	m.name = newName(player);
	m.xp = xp;
	s_members.push_back(m);
	syncDebug("modcrew: neuer Pilot %u (Spieler %u, Name %u)", m.id, player, m.name);
	return s_members.back();
}

/// Besatzungs-Einheit neben einer Stelle absetzen (in der Karte der Ebene)
DROID *spawnCrew(const Member &m, const Position &pos, int layer)
{
	const DROID_TEMPLATE *t = getTemplateFromTranslatedNameNoPlayer("MFCrew");
	if (t == nullptr || m.player >= MAX_PLAYERS) { return nullptr; }
	modlayermap::Scope sc(static_cast<modlayer::Layer>(layer));
	Vector2i tile = map_coord(pos.xy());
	if (!pickATileGen(gameWorld, &tile, LOOK_FOR_EMPTY_TILE, zonedPAT)) { return nullptr; }
	const Position at(world_coord(tile.x) + TILE_UNITS / 2, world_coord(tile.y) + TILE_UNITS / 2, 0);
	DROID *u = reallyBuildDroid(gameWorld, t, at, m.player, false);
	if (u == nullptr) { return nullptr; }
	addDroid(u, gameWorld.objects.droids);
	modlayer::assignLayer(u, static_cast<modlayer::Layer>(layer));
	u->experience = m.xp;
	const std::string n = std::string(_("Pilot")) + " " + nameOf(m);
	droidSetName(u, n.c_str());
	return u;
}

/// naechste eigene Halle (Fabrik, Cyborg-Fabrik oder Kryo-Farm) in der Ebene der Besatzung
const STRUCTURE *nearestHome(const DROID *u)
{
	const STRUCTURE *best = nullptr;
	int64_t bestD = INT64_MAX;
	for (const STRUCTURE *s : gameWorld.objects.structures[u->player])
	{
		if (s->died || s->status != SS_BUILT || s->isMobileCore() || !modlayer::canInteract(u, s)) { continue; }
		const STRUCTURE_STATS *st = s->pStructureType;
		const bool hall = st->type == REF_FACTORY || st->type == REF_CYBORG_FACTORY || st->crewProvide > 0;
		if (!hall) { continue; }
		const int64_t dx = s->pos.x - u->pos.x, dy = s->pos.y - u->pos.y, dd = dx * dx + dy * dy;
		if (dd < bestD) { bestD = dd; best = s; }
	}
	return best;
}

int homeReach(const STRUCTURE *s)
{
	const int fp = static_cast<int>(std::max(s->pStructureType->baseWidth, s->pStructureType->baseBreadth));
	return DIST_TILE * 3 / 2 + fp * DIST_TILE / 2;
}

void forgetCombot(uint32_t id)
{
	s_lastPos.erase(id);
	s_lastLayer.erase(id);
	s_baseName.erase(id);
	s_crewless.erase(id);
}
} // namespace

bool isCrew(const DROID *d)
{
	if (d == nullptr || d->droidType != DROID_PERSON) { return false; }
	const int b = crewBodyIndex();
	return b >= 0 && d->asBits[COMP_BODY] == b;
}

bool crewless(const SIMPLE_OBJECT *o)
{
	return o != nullptr && !s_crewless.empty() && o->type == OBJ_DROID && s_crewless.count(o->id) != 0;
}

bool canBoard(const DROID *u, const BASE_OBJECT *t)
{
	return isCrew(u) && crewless(t) && modlayer::canInteract(u, t);
}

void tick()
{
	if (s_members.empty() && crewBodyIndex() < 0) { return; }   // altes Mod-Paket ohne Besatzung
	// 1. Combots und Besatzungen einsammeln (auch Combots im Laderaum eines Transporters)
	std::vector<DROID *> combots, crews;
	std::unordered_map<uint32_t, DROID *> byId;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d->died) { continue; }
			if (modcombot::isModular(d)) { combots.push_back(d); byId[d->id] = d; }
			else if (isCrew(d)) { crews.push_back(d); byId[d->id] = d; }
			if (d->isTransporter() && d->psGroup != nullptr)
			{
				for (DROID *x : d->psGroup->psList) { if (x != d && !x->died) { byId[x->id] = x; } }
			}
		}
		for (DROID *d : mission.gameWorld.objects.droids[p]) { if (!d->died) { byId[d->id] = d; } }
	}
	if (combots.empty() && crews.empty() && s_members.empty()) { return; }

	// 2. Piloten nachfuehren: Combot zerstoert -> aussteigen (meist lebend), Besatzung zu Fuss getoetet -> tot
	for (size_t i = 0; i < s_members.size();)
	{
		Member &m = s_members[i];
		if (m.combot != 0)
		{
			const auto it = byId.find(m.combot);
			if (it == byId.end())
			{
				const uint32_t lost = m.combot;
				m.combot = 0;
				const auto lp = s_lastPos.find(lost);
				const bool survive = static_cast<int>(gameRand(100)) < SURVIVE_PCT;
				DROID *u = nullptr;
				if (survive && lp != s_lastPos.end())
				{
					const auto ll = s_lastLayer.find(lost);
					u = spawnCrew(m, lp->second, ll != s_lastLayer.end() ? ll->second : static_cast<int>(modlayer::SURFACE));
				}
				forgetCombot(lost);
				if (u == nullptr)
				{
					say(m.player, astringf(_("Pilot %s was killed with the combot"), nameOf(m)));
					s_members.erase(s_members.begin() + static_cast<std::ptrdiff_t>(i));
					continue;
				}
				m.unit = u->id;
				say(m.player, astringf(_("Pilot %s bailed out of the destroyed combot"), nameOf(m)));
			}
			else
			{
				DROID *c = it->second;
				m.player = c->player;   // Combot elektronisch uebernommen: der Pilot gehoert mit dazu
				m.xp = c->experience;
				s_lastPos[c->id] = c->pos;
				s_lastLayer[c->id] = static_cast<int>(modlayer::layerOf(c));
			}
		}
		else if (m.unit != 0)
		{
			const auto it = byId.find(m.unit);
			if (it == byId.end())
			{
				say(m.player, astringf(_("Pilot %s was killed"), nameOf(m)));
				s_nextHome.erase(m.unit);
				s_members.erase(s_members.begin() + static_cast<std::ptrdiff_t>(i));
				continue;
			}
			m.player = it->second->player;
		}
		++i;
	}
	// verschwundene Combots ohne Besatzung vergessen
	for (auto it = s_crewless.begin(); it != s_crewless.end();)
	{
		if (byId.count(*it) == 0) { const uint32_t id = *it; ++it; forgetCombot(id); }
		else { ++it; }
	}
	std::unordered_map<uint32_t, size_t> pilotOf, memberOfUnit;
	for (size_t i = 0; i < s_members.size(); ++i)
	{
		if (s_members[i].combot != 0) { pilotOf[s_members[i].combot] = i; }
		if (s_members[i].unit != 0) { memberOfUnit[s_members[i].unit] = i; }
	}

	// 3. Combots: Aussteigen auf Wunsch, ohne Besatzung stillhalten, neue Combots bekommen einen Piloten
	for (DROID *c : combots)
	{
		const auto po = pilotOf.find(c->id);
		if ((c->secondaryOrder & DSS_MINECLEAR_MASK) != 0)   // Knopf "Aussteigen" (Zweitbefehl, ueber das Netz abgeglichen)
		{
			c->secondaryOrder &= ~static_cast<UDWORD>(DSS_MINECLEAR_MASK);
			c->secondaryOrderPending &= ~static_cast<uint32_t>(DSS_MINECLEAR_MASK);
			if (po != pilotOf.end() && !modlayer::inTransition(c))
			{
				Member &m = s_members[po->second];
				m.xp = c->experience;
				DROID *u = spawnCrew(m, c->pos, static_cast<int>(modlayer::layerOf(c)));
				if (u != nullptr)
				{
					m.combot = 0;
					m.unit = u->id;
					memberOfUnit[u->id] = po->second;
					s_ejectedFrom[u->id] = c->id;
					pilotOf.erase(po);
					c->experience = 0;
					s_crewless.insert(c->id);
					orderDroid(c, DORDER_STOP, ModeImmediate);
					nameCombot(c, _("no crew"));
					say(m.player, astringf(_("Pilot %s ejected - the combot stands unmanned (any crew can take it over)"), nameOf(m)));
					syncDebug("modcrew: Pilot %u steigt aus Combot %u", m.id, c->id);
				}
				continue;
			}
		}
		if (s_crewless.count(c->id) != 0)
		{
			// ohne Besatzung: nimmt keine Befehle an und schiesst nicht
			if (c->order.type != DORDER_NONE || c->action != DACTION_NONE) { orderDroid(c, DORDER_STOP, ModeImmediate); }
			s_lastPos[c->id] = c->pos;
			continue;
		}
		if (po != pilotOf.end()) { continue; }
		// neuer Combot (Fabrik, Spielbeginn, Skript): den erfahrensten Piloten aus der Halle, sonst einen neuen Rekruten
		size_t pick = s_members.size();
		for (size_t i = 0; i < s_members.size(); ++i)
		{
			const Member &m = s_members[i];
			if (m.player != c->player || m.combot != 0 || m.unit != 0) { continue; }
			if (pick == s_members.size() || m.xp > s_members[pick].xp) { pick = i; }
		}
		Member *m = nullptr;
		if (pick < s_members.size())
		{
			m = &s_members[pick];
			c->experience = std::max(c->experience, m->xp);
		}
		else
		{
			m = &newMember(c->player, c->experience);
		}
		m->combot = c->id;
		m->xp = c->experience;
		pilotOf[c->id] = static_cast<size_t>(m - s_members.data());
		nameCombot(c, nameOf(*m));
		s_lastPos[c->id] = c->pos;
		s_lastLayer[c->id] = static_cast<int>(modlayer::layerOf(c));
	}

	// 4. Besatzungen zu Fuss: in einen Combot ohne Besatzung einsteigen, sonst zur naechsten Halle laufen
	for (DROID *u : crews)
	{
		if (u->died) { continue; }
		auto mo = memberOfUnit.find(u->id);
		if (mo == memberOfUnit.end())
		{
			// Besatzung ohne Akte (z. B. von einem Skript erzeugt): bekommt einen Namen
			Member &nm = newMember(u->player, u->experience);
			nm.unit = u->id;
			mo = memberOfUnit.emplace(u->id, s_members.size() - 1).first;
			const std::string n = std::string(_("Pilot")) + " " + nameOf(nm);
			droidSetName(u, n.c_str());
		}
		Member &m = s_members[mo->second];
		DROID *target = nullptr;
		for (DROID *c : combots)
		{
			if (c->died || s_crewless.count(c->id) == 0 || !modlayer::canInteract(u, c)) { continue; }
			if (iHypot(u->pos.xy() - c->pos.xy()) > BOARD_R) { continue; }
			const bool sent = (u->order.type == DORDER_MOVE || u->order.type == DORDER_SCOUT) && iHypot(u->order.pos - c->pos.xy()) <= BOARD_R;
			const auto ef = s_ejectedFrom.find(u->id);
			const bool idle = u->order.type == DORDER_NONE && (ef == s_ejectedFrom.end() || ef->second != c->id);
			if (sent || idle) { target = c; break; }
		}
		if (target != nullptr)
		{
			const uint32_t oldId = target->id;
			const uint32_t oldOwner = target->player;
			DROID *c = target;
			if (c->player != u->player)
			{
				c = giftSingleDroid(target, u->player, false);   // fremder Combot wechselt den Besitzer
				if (c == nullptr) { continue; }
			}
			s_crewless.erase(oldId);
			if (c->id != oldId) { forgetCombot(oldId); }
			m.combot = c->id;
			m.unit = 0;
			c->experience = m.xp;
			orderDroid(c, DORDER_STOP, ModeImmediate);
			nameCombot(c, nameOf(m));
			s_lastPos[c->id] = c->pos;
			s_lastLayer[c->id] = static_cast<int>(modlayer::layerOf(c));
			s_nextHome.erase(u->id);
			s_ejectedFrom.erase(u->id);
			if (oldOwner != m.player)
			{
				say(m.player, astringf(_("Pilot %s captured an unmanned combot"), nameOf(m)));
				say(oldOwner, _("An unmanned combot was captured by the enemy"));
			}
			else
			{
				say(m.player, astringf(_("Pilot %s boarded the combot"), nameOf(m)));
			}
			syncDebug("modcrew: Pilot %u steigt in Combot %u", m.id, c->id);
			vanishDroid(u, gameWorld.objects);
			continue;
		}
		if (u->order.type != DORDER_NONE || u->action != DACTION_NONE) { continue; }
		unsigned &next = s_nextHome[u->id];
		if (gameTime < next) { continue; }
		next = gameTime + HOME_CHECK_MS;
		const STRUCTURE *home = nearestHome(u);
		if (home == nullptr) { continue; }
		if (iHypot(u->pos.xy() - home->pos.xy()) <= homeReach(home))
		{
			// in der Halle: wartet dort auf den naechsten Combot
			m.unit = 0;
			s_nextHome.erase(u->id);
			s_ejectedFrom.erase(u->id);
			syncDebug("modcrew: Pilot %u zurueck in der Halle", m.id);
			vanishDroid(u, gameWorld.objects);
			continue;
		}
		orderDroidLoc(u, DORDER_MOVE, home->pos.x, home->pos.y, ModeImmediate);
	}
	if (s_nextHome.size() > 4096) { s_nextHome.clear(); }
	if (s_ejectedFrom.size() > 4096) { s_ejectedFrom.clear(); }
}

void reset()
{
	s_members.clear();
	s_nextId = 1;
	s_crewless.clear();
	s_lastPos.clear();
	s_lastLayer.clear();
	s_nextHome.clear();
	s_baseName.clear();
	s_ejectedFrom.clear();
	s_crewBody = -2;
}

void saveState(std::vector<int64_t> &out)
{
	out.clear();
	out.push_back(SAVE_VERSION);
	out.push_back(s_nextId);
	out.push_back(static_cast<int64_t>(s_members.size()));
	for (const Member &m : s_members)
	{
		out.push_back(m.id); out.push_back(m.player); out.push_back(m.name);
		out.push_back(m.xp); out.push_back(m.combot); out.push_back(m.unit);
	}
	out.push_back(static_cast<int64_t>(s_crewless.size()));
	for (uint32_t id : s_crewless) { out.push_back(id); }
}

void loadState(const std::vector<int64_t> &in)
{
	reset();
	if (in.size() < 3 || in[0] != SAVE_VERSION) { return; }
	size_t k = 1;
	s_nextId = static_cast<uint32_t>(in[k++]);
	const int64_t n = in[k++];
	for (int64_t i = 0; i < n && k + 6 <= in.size(); ++i)
	{
		Member m;
		m.id = static_cast<uint32_t>(in[k++]); m.player = static_cast<uint32_t>(in[k++]); m.name = static_cast<uint32_t>(in[k++]);
		m.xp = static_cast<uint32_t>(in[k++]); m.combot = static_cast<uint32_t>(in[k++]); m.unit = static_cast<uint32_t>(in[k++]);
		s_members.push_back(m);
	}
	if (k < in.size())
	{
		const int64_t c = in[k++];
		for (int64_t i = 0; i < c && k < in.size(); ++i) { s_crewless.insert(static_cast<uint32_t>(in[k++])); }
	}
}
}
