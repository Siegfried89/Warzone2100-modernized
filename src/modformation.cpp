// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Formationsbefehle (Ring um den Kommandanten, Linie, Keil, Kolonne, Artillerie-Staffel hinter dem Sensor)

#include "modformation.h"

#include "lib/framework/frame.h"
#include "lib/framework/math_ext.h"
#include "lib/netplay/netplay.h"

#include "console.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "order.h"
#include "projectile.h"
#include "stats.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace modformation
{

namespace
{

constexpr float SPACING = 56.f;          // Abstand zwischen Einheiten (Welteinheiten, Kachel = 128)
constexpr float ARTY_BACK = 9.f * 128.f; // Artillerie bleibt so weit hinter dem Sensor
constexpr float ARTY_SPREAD = 80.f;

struct Vec { float x, y; };

bool isArtillery(const DROID *d)
{
	if (d->numWeaps == 0 || d->asWeaps[0].nStat == 0 || d->asWeaps[0].nStat >= asWeaponStats.size())
	{
		return false;
	}
	return !proj_Direct(&asWeaponStats[d->asWeaps[0].nStat]);
}

/// (19.11. 00:13 "Bei Formationen sollen immer die Panzer an der Spitze oder in der Mitte sein"): Rang je Einheit, kleiner = weiter vorn
/// bzw. weiter in der Mitte. 0 Panzer (Kette/Halbkette mit Waffe), 1 andere Kampffahrzeuge (Raeder, Luftkissen, Combots), 2 Cyborgs und
/// Infanterie, 3 Hilfsfahrzeuge (Sensor, Bau, Reparatur, Stoersender), 4 Artillerie (indirekt) ganz hinten/aussen.
int rankOf(const DROID *d)
{
	if (isArtillery(d)) { return 4; }
	switch (d->droidType)
	{
	case DROID_PERSON: case DROID_CYBORG: case DROID_CYBORG_SUPER: return 2;
	case DROID_SENSOR: case DROID_CONSTRUCT: case DROID_REPAIR: case DROID_ECM: case DROID_CYBORG_CONSTRUCT: case DROID_CYBORG_REPAIR:
	case DROID_TRANSPORTER: case DROID_SUPERTRANSPORTER: return 3;
	default: break;
	}
	if (d->numWeaps == 0) { return 3; }
	const PROPULSION_STATS *p = d->getPropulsionStats();
	if (p != nullptr && (p->propulsionType == PROPULSION_TYPE_TRACKED || p->propulsionType == PROPULSION_TYPE_HALF_TRACKED)) { return 0; }
	return 1;
}

/// Panzer zuerst, innerhalb eines Rangs die schwersten (meiste Trefferpunkte) zuerst; bei Gleichstand nach Nummer (auf allen Rechnern gleich)
void sortForFormation(std::vector<DROID *> &v)
{
	std::stable_sort(v.begin(), v.end(), [](const DROID *a, const DROID *b) {
		const int ra = rankOf(a), rb = rankOf(b);
		if (ra != rb) { return ra < rb; }
		if (a->originalBody != b->originalBody) { return a->originalBody > b->originalBody; }
		return a->id < b->id;
	});
}

/// Plaetze einer Reihe von der Mitte nach aussen: 0, +1, -1, +2, -2 ... (Index in die Reihe 0..n-1)
std::vector<size_t> centreOut(size_t n)
{
	std::vector<size_t> idx(n);
	for (size_t i = 0; i < n; ++i) { idx[i] = i; }
	const float mid = (static_cast<float>(n) - 1.f) * 0.5f;
	std::stable_sort(idx.begin(), idx.end(), [mid](size_t a, size_t b) {
		const float da = std::fabs(static_cast<float>(a) - mid), db = std::fabs(static_cast<float>(b) - mid);
		if (da != db) { return da < db; }
		return a > b;
	});
	return idx;
}

Vec forwardOf(const DROID *d)
{
	return { static_cast<float>(iSinR(d->rot.direction, 1000)) / 1000.f, static_cast<float>(iCosR(d->rot.direction, 1000)) / 1000.f };
}

void place(DROID *d, Vec p)
{
	const float maxX = static_cast<float>(world_coord(gameWorld.map.width)) - 128.f;
	const float maxY = static_cast<float>(world_coord(gameWorld.map.height)) - 128.f;
	p.x = std::min(std::max(p.x, 128.f), maxX);
	p.y = std::min(std::max(p.y, 128.f), maxY);
	orderDroidLoc(d, DORDER_MOVE, static_cast<UDWORD>(p.x), static_cast<UDWORD>(p.y), ModeQueue);
}

} // namespace

const char *tip(Type t)
{
	switch (t)
	{
	case RING: return _("Formation: ring around the commander");
	case LINE: return _("Formation: line abreast");
	case WEDGE: return _("Formation: wedge");
	case COLUMN: return _("Formation: column");
	case ARTY: return _("Formation: artillery far behind the sensor unit");
	default: return "";
	}
}

void apply(Type type)
{
	if (selectedPlayer >= MAX_PLAYERS)
	{
		return;
	}
	std::vector<DROID *> sel;
	for (DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d != nullptr && d->selected)
		{
			sel.push_back(d);
		}
	}
	if (sel.size() < 2)
	{
		return;
	}

	// Anker: Kommandant, sonst Sensor, sonst Schwerpunkt
	DROID *anchor = nullptr, *sensor = nullptr;
	Vec c { 0.f, 0.f };
	for (DROID *d : sel)
	{
		c.x += d->pos.x;
		c.y += d->pos.y;
		if (d->droidType == DROID_COMMAND && !anchor) { anchor = d; }
		if (d->droidType == DROID_SENSOR && !sensor) { sensor = d; }
	}
	c.x /= sel.size();
	c.y /= sel.size();
	DROID *lead = anchor ? anchor : (sensor ? sensor : sel.front());
	if (anchor) { c = { static_cast<float>(anchor->pos.x), static_cast<float>(anchor->pos.y) }; }
	Vec f = forwardOf(lead);
	const Vec r { f.y, -f.x }; // rechts von der Blickrichtung

	std::vector<DROID *> rest;
	for (DROID *d : sel)
	{
		if (d != anchor) { rest.push_back(d); }
	}
	sortForFormation(rest);   // Panzer an die Spitze (Keil, Kolonne) bzw. in die Mitte (Ring innen, Linie mittig)
	const size_t n = rest.size();

	switch (type)
	{
	case RING:
	{
		size_t i = 0;
		for (int ring = 1; i < n; ++ring)
		{
			const float rad = SPACING * ring;
			const size_t cap = std::max<size_t>(4, static_cast<size_t>(6.2831853f * rad / SPACING));
			const size_t cnt = std::min(cap, n - i);
			for (size_t k = 0; k < cnt; ++k, ++i)
			{
				const float a = 6.2831853f * k / cnt + ring * 0.5f;
				place(rest[i], { c.x + rad * std::cos(a), c.y + rad * std::sin(a) });
			}
		}
		break;
	}
	case LINE:
	{
		const std::vector<size_t> slot = centreOut(n);
		for (size_t i = 0; i < n; ++i)
		{
			const float o = (static_cast<float>(slot[i]) - (n - 1) * 0.5f) * SPACING;
			place(rest[i], { c.x + r.x * o + f.x * SPACING, c.y + r.y * o + f.y * SPACING });
		}
		break;
	}
	case WEDGE:
		for (size_t i = 0; i < n; ++i)
		{
			const float row = static_cast<float>(i / 2 + 1);
			const float side = (i % 2 == 0) ? 1.f : -1.f;
			place(rest[i], { c.x + r.x * side * row * SPACING * 0.8f - f.x * row * SPACING * 0.8f,
			                 c.y + r.y * side * row * SPACING * 0.8f - f.y * row * SPACING * 0.8f });
		}
		break;
	case COLUMN:
		for (size_t i = 0; i < n; ++i)
		{
			const float o = static_cast<float>(i + 1) * SPACING;
			place(rest[i], { c.x - f.x * o, c.y - f.y * o });
		}
		break;
	case ARTY:
	{
		// Schnelle Einheiten vor dem Sensor in einer Linie, Artillerie weit dahinter und nebeneinander
		const Vec s = sensor ? Vec { static_cast<float>(sensor->pos.x), static_cast<float>(sensor->pos.y) } : c;
		std::vector<DROID *> arty, front;
		for (DROID *d : rest)
		{
			(isArtillery(d) ? arty : front).push_back(d);
		}
		const std::vector<size_t> slot = centreOut(front.size());
		for (size_t i = 0; i < front.size(); ++i)
		{
			const float o = (static_cast<float>(slot[i]) - (front.size() - 1) * 0.5f) * SPACING;
			place(front[i], { s.x + r.x * o + f.x * 2.f * SPACING, s.y + r.y * o + f.y * 2.f * SPACING });
		}
		for (size_t i = 0; i < arty.size(); ++i)
		{
			const float o = (static_cast<float>(i) - (arty.size() - 1) * 0.5f) * ARTY_SPREAD;
			place(arty[i], { s.x - f.x * ARTY_BACK + r.x * o, s.y - f.y * ARTY_BACK + r.y * o });
		}
		break;
	}
	default: break;
	}
	console("%s", tip(type));
}

} // namespace modformation
