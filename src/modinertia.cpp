// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Anlassen und Traegheit der Panzer (siehe modinertia.h)
#include "modinertia.h"

#include <unordered_map>

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "droid.h"
#include "stats.h"
#include "modrecoil.h"

namespace modinertia
{
namespace
{
std::unordered_map<unsigned, unsigned> s_until;   // Einheit -> Spielzeit, ab der sie nach dem Anlassen losfaehrt

/// 0 = keine Traegheit (leicht, Infanterie, Luft, Schweber ...), 1 = mittel, 2 = schwer, 3 = superschwer
int weightClass(const DROID *d, bool tracksOnly)
{
	if (d == nullptr || d->droidType == DROID_PERSON || d->isCyborg() || d->isTransporter() || d->isVtol()) { return 0; }
	const PROPULSION_STATS *ps = d->getPropulsionStats();
	const BODY_STATS *bs = d->getBodyStats();
	if (ps == nullptr || bs == nullptr || bs->size < SIZE_MEDIUM || bs->size >= SIZE_NUM) { return 0; }
	const bool tracks = ps->propulsionType == PROPULSION_TYPE_TRACKED || ps->propulsionType == PROPULSION_TYPE_HALF_TRACKED;
	if (!tracks && (tracksOnly || ps->propulsionType != PROPULSION_TYPE_WHEELED)) { return 0; }
	return bs->size == SIZE_MEDIUM ? 1 : (bs->size == SIZE_HEAVY ? 2 : 3);
}
} // namespace

int accelPercent(const DROID *d)
{
	static const int k[4] = {100, 60, 42, 35};
	return k[weightClass(d, false)];
}

int decelPercent(const DROID *d)
{
	static const int k[4] = {100, 85, 70, 62};
	return k[weightClass(d, false)];
}

bool holdStart(DROID *d, bool started)
{
	if (d == nullptr) { return false; }
	constexpr unsigned WARM_MS = 2500;   // kurz nach dem Anlassen ist der Motor noch warm: ein neuer Befehl laesst ihn nicht erneut anlaufen
	const auto it = s_until.empty() ? s_until.end() : s_until.find(d->id);
	if (started)
	{
		if (it != s_until.end() && gameTime < it->second + WARM_MS)
		{
			return gameTime < it->second;                        // laeuft schon an (Befehl verlaengert nichts) bzw. Motor noch warm
		}
		const int cls = weightClass(d, true);
		if (cls == 0) { return false; }
		static const unsigned kDelay[4] = {0, 350, 600, 750};
		if (s_until.size() > 4000) { s_until.clear(); }             // verwaiste Eintraege (zerstoerte Einheiten) - auf allen Rechnern gleich
		s_until[d->id] = gameTime + kDelay[cls];
		modrecoil::engineStart(d, cls);
		return true;
	}
	if (it == s_until.end()) { return false; }
	if (gameTime < it->second) { return true; }
	if (gameTime > it->second + WARM_MS) { s_until.erase(it); }
	return false;
}

void reset()
{
	s_until.clear();
}
}
