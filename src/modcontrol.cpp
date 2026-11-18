// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Vorrang fuer Spieler-Befehle (siehe modcontrol.h)

#include "modcontrol.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "droid.h"
#include "orderdef.h"

#include <unordered_map>

namespace modcontrol
{
namespace
{
std::unordered_map<uint32_t, unsigned> s_last;   // Einheit -> Spielzeit des letzten Spieler-Befehls (nur Abfrage, keine Reihenfolge-Abhaengigkeit)
}

void onPlayerOrder(const DROID *d)
{
	if (d == nullptr) { return; }
	s_last[d->id] = gameTime;
	if (s_last.size() > 4096)
	{
		for (auto it = s_last.begin(); it != s_last.end();)
		{
			it = (gameTime - it->second > GRACE_MS) ? s_last.erase(it) : std::next(it);
		}
	}
}

bool manual(const DROID *d)
{
	if (d == nullptr) { return false; }
	auto it = s_last.find(d->id);
	if (it == s_last.end()) { return false; }
	if (gameTime - it->second < GRACE_MS) { return true; }
	// der Befehl laeuft noch (Fahrt, Angriff, Bau ...): weiterhin Spieler-Kontrolle
	return d->order.type != DORDER_NONE && d->order.type != DORDER_GUARD && d->order.type != DORDER_HOLD;
}

unsigned sincePlayerOrder(const DROID *d)
{
	if (d == nullptr) { return 0xffffffffu; }
	auto it = s_last.find(d->id);
	if (it == s_last.end()) { return 0xffffffffu; }
	return gameTime - it->second;
}

void reset()
{
	s_last.clear();
}
}
