// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Fraktions-Technik (siehe modfactech.h)

#include "modfactech.h"

#include "lib/framework/frame.h"
#include "lib/netplay/netplay.h"

#include "component.h"
#include "factionid.h"
#include "multiplay.h"
#include "multiint.h"
#include "statsdef.h"

#include <cstring>
#include <random>

namespace modfactech
{
namespace
{
/// Fraktion eines Original-Rumpfes, -1 = kein Fraktions-Rumpf (frei fuer alle)
int bodyFaction(const BODY_STATS *b)
{
	struct Entry { const char *id; int faction; };
	static const Entry kBodies[] = {
		{"Body1REC", FACTION_PROJECT}, {"Body5REC", FACTION_PROJECT}, {"Body11ABT", FACTION_PROJECT},
		{"Body13SUP", FACTION_PROJECT}, {"Body14SUP", FACTION_PROJECT},
		{"Body4ABT", FACTION_NEW_PARADIGM}, {"Body8MBT", FACTION_NEW_PARADIGM}, {"Body12SUP", FACTION_NEW_PARADIGM},
		{"Body2SUP", FACTION_COLLECTIVE}, {"Body6SUPP", FACTION_COLLECTIVE}, {"Body9REC", FACTION_COLLECTIVE},
		{"Body3MBT", FACTION_NEXUS}, {"Body7ABT", FACTION_NEXUS}, {"Body10MBT", FACTION_NEXUS},
		// (17.11. 16:10) New-Paradigm-Soldaten (Ex-Soldaten und Soeldner: Helm, Wuestentarn, Weste - npsoldier.py)
		{"B1NPSoldier01", FACTION_NEW_PARADIGM}, {"B1NPSoldierB01", FACTION_NEW_PARADIGM},
	};
	const std::string id = b->id.toUtf8();
	for (const Entry &e : kBodies)
	{
		if (id == e.id)
		{
			return e.faction;
		}
	}
	return -1;
}
}

bool bodyAllowed(unsigned player, const BODY_STATS *psBody)
{
	if (psBody == nullptr || player >= MAX_PLAYERS)
	{
		return true;
	}
	// Scavengers (und alle Spieler ohne gewaehlte Fraktion, z. B. die Kampagnen-Gegner) bleiben unbeschraenkt
	if (NetPlay.players[player].faction == FACTION_NORMAL)
	{
		return true;
	}
	const int owner = bodyFaction(psBody);
	if (owner < 0)
	{
		return true;
	}
	return static_cast<int>(NetPlay.players[player].faction) == owner;
}

void prepareStart()
{
	std::mt19937 rng(std::random_device{}());   // nur beim Host; das Ergebnis wird verschickt
	bool used[MAX_PLAYERS] = {};
	for (unsigned i = 0; i < game.maxPlayers && i < MAX_PLAYERS; ++i)
	{
		const PLAYER &p = NetPlay.players[i];
		used[i] = p.allocated || p.ai >= 0;
	}
	for (unsigned i = 0; i < game.maxPlayers && i < MAX_PLAYERS; ++i)
	{
		PLAYER &p = NetPlay.players[i];
		if (!p.allocated && p.ai >= 0 && p.faction == FACTION_NORMAL)
		{
			p.faction = static_cast<FactionID>(rng() % (static_cast<unsigned>(MAX_FACTION_ID) + 1u));
			NETBroadcastPlayerInfo(i);
		}
	}
	for (unsigned i = 0; i < game.maxPlayers && i < MAX_PLAYERS; ++i)
	{
		if (!used[i] || NetPlay.players[i].faction == FACTION_NORMAL)
		{
			continue;
		}
		const int want = factionColour(NetPlay.players[i].faction);
		if (getPlayerColour(i) == want)
		{
			continue;
		}
		bool taken = false;
		for (unsigned j = 0; j < game.maxPlayers && j < MAX_PLAYERS; ++j)
		{
			if (j != i && used[j] && getPlayerColour(j) == want) { taken = true; break; }
		}
		if (!taken)
		{
			setPlayerColour(i, want);
			NETBroadcastPlayerInfo(i);
		}
	}
}

int factionColour(int faction)
{
	switch (faction)
	{
	case FACTION_NEXUS: return 3;
	case FACTION_COLLECTIVE: return 2;
	case FACTION_NEW_PARADIGM: return 8;
	case FACTION_PROJECT: return 0;
	default: return 1;
	}
}
}
