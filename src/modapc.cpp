// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Schuetzenpanzer (siehe modapc.h)
#include "modapc.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"

#include "console.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modoil.h"
#include "modrail.h"
#include "order.h"
#include "orderdef.h"
#include "stats.h"

namespace modapc
{
namespace
{
constexpr int BOARD_R = DIST_TILE * 13 / 10;
std::map<uint32_t, std::vector<uint32_t>> s_load;    // Transporter -> Insassen (Einstiegsreihenfolge = Sitzplatz)
std::unordered_map<uint32_t, uint32_t> s_seat;       // Insasse -> Transporter

DROID *findDroid(uint32_t id)
{
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : gameWorld.objects.droids[p]) { if (d->id == id && !d->died) { return d; } }
	}
	return nullptr;
}

bool isWagon(const DROID *d)
{
	const BODY_STATS *b = d != nullptr ? d->getBodyStats() : nullptr;
	return b != nullptr && b->id.startsWith("TrainWagonTroop");
}

/// Sitzplatz k im offenen Laderaum (hinten): zwei Reihen zu vier, quer leicht versetzt; Weltkoordinaten
Vector3i seatPos(const DROID *apc, int k)
{
	if (isWagon(apc))
	{
		// (21.11.) Truppenwaggon: zwei Reihen zu fuenf entlang des Wagens, Blick nach aussen
		const int side = (k % 2 == 0) ? -1 : 1;
		const int row = k / 2;
		const int right = side * 5;
		const int along = 18 - row * 9;                     // vorn -> hinten
		const int fx = iSinR(apc->rot.direction, 1024), fy = iCosR(apc->rot.direction, 1024);
		const int rx = fy, ry = -fx;
		return Vector3i(apc->pos.x + (rx * right + fx * along) / 1024, apc->pos.y + (ry * right + fy * along) / 1024, apc->pos.z + 8);
	}
	const int side = (k % 2 == 0) ? -1 : 1;
	const int row = k / 2;                                  // 0 = vorn im Laderaum
	const int right = side * 9;
	const int back = 4 + row * 9;                           // hinter der Rumpfmitte (das Fahrerhaus ist vorn)
	const int fx = iSinR(apc->rot.direction, 1024), fy = iCosR(apc->rot.direction, 1024);   // vorwaerts
	const int rx = fy, ry = -fx;                                                             // seitwaerts
	return Vector3i(apc->pos.x + (rx * right - fx * back) / 1024, apc->pos.y + (ry * right - fy * back) / 1024, apc->pos.z + 9);
}

void dismount(DROID *d, const DROID *apc, int k)
{
	s_seat.erase(d->id);
	d->flags.set(OBJECT_FLAG_UNSELECTABLE, false);
	int wx = d->pos.x, wy = d->pos.y;
	if (apc != nullptr)
	{
		// hinter dem Fahrzeug absitzen, Reihe neben Reihe
		const int fx = iSinR(apc->rot.direction, 1024), fy = iCosR(apc->rot.direction, 1024);
		const int back = 70 + (k / 4) * 28, right = ((k % 4) - 1) * 26 - 13;
		wx = apc->pos.x + (fy * right - fx * back) / 1024;
		wy = apc->pos.y + (-fx * right - fy * back) / 1024;
	}
	wx = std::clamp(wx, TILE_UNITS, world_coord(gameWorld.map.width - 1) - 1);
	wy = std::clamp(wy, TILE_UNITS, world_coord(gameWorld.map.height - 1) - 1);
	droidSetPosition(d, gameWorld.map, wx, wy);
}
} // namespace

bool isApc(const DROID *d)
{
	if (d == nullptr) { return false; }
	const BODY_STATS *b = d->getBodyStats();
	return b != nullptr && (b->id.startsWith("ScavAPC") || b->id.startsWith("TrainWagonTroop"));
}

int capacity(const DROID *d)
{
	return isWagon(d) ? 10 : CAPACITY;
}

bool aboard(const SIMPLE_OBJECT *o)
{
	return o != nullptr && !s_seat.empty() && s_seat.count(o->id) != 0;
}

int load(const DROID *apc)
{
	if (apc == nullptr) { return 0; }
	const auto it = s_load.find(apc->id);
	return it == s_load.end() ? 0 : static_cast<int>(it->second.size());
}

void tick()
{
	// Transporter suchen: Absitz-Wunsch (Zweitbefehl-Bit), Einsteigen, Insassen mitfuehren
	std::vector<uint32_t> liveApcs;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *apc : gameWorld.objects.droids[p])
		{
			if (apc->died || !isApc(apc)) { continue; }
			liveApcs.push_back(apc->id);
			std::vector<uint32_t> &crew = s_load[apc->id];
			// Absitzen (Knopf in der Rosette: Zweitbefehl DSO_MINECLEAR auf dem Transporter, danach hier zurueckgesetzt)
			if ((apc->secondaryOrder & DSS_MINECLEAR_MASK) != 0)
			{
				apc->secondaryOrder &= ~static_cast<UDWORD>(DSS_MINECLEAR_MASK);
				apc->secondaryOrderPending &= ~static_cast<uint32_t>(DSS_MINECLEAR_MASK);
				for (size_t k = 0; k < crew.size(); ++k)
				{
					DROID *d = findDroid(crew[k]);
					if (d != nullptr) { dismount(d, apc, static_cast<int>(k)); }
					else { s_seat.erase(crew[k]); }
				}
				if (p == selectedPlayer && !crew.empty()) { addConsoleMessage(_("APC: infantry dismounted"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
				crew.clear();
			}
			// Einsteigen: eigene Infanterie, die zum Transporter will (begleiten oder hingeschickt) und angekommen ist
			const size_t cap = static_cast<size_t>(capacity(apc));
			if (crew.size() < cap)
			{
				for (DROID *d : gameWorld.objects.droids[p])
				{
					if (crew.size() >= cap) { break; }
					if (d->died || aboard(d) || !modoil::isInfantry(d) || !modlayer::canInteract(d, apc)) { continue; }
					if (iHypot(d->pos.xy() - apc->pos.xy()) > BOARD_R) { continue; }
					// (21.11.) Zug: Infanterie auf die Lok geklickt -> steigt in einen Truppenwaggon dieses Zuges
					const bool follow = d->order.type == DORDER_GUARD && (d->order.psObj == apc
					                    || (d->order.psObj != nullptr && d->order.psObj->type == OBJ_DROID && modrail::isLoco(static_cast<const DROID *>(d->order.psObj))
					                        && modrail::locoOf(apc) == d->order.psObj->id));
					const bool sent = (d->order.type == DORDER_MOVE || d->order.type == DORDER_SCOUT) && iHypot(d->order.pos - apc->pos.xy()) <= BOARD_R;
					if (!follow && !sent) { continue; }
					orderDroid(d, DORDER_STOP, ModeImmediate);
					d->selected = false;
					d->flags.set(OBJECT_FLAG_UNSELECTABLE, true);
					s_seat[d->id] = apc->id;
					crew.push_back(d->id);
				}
			}
			// Insassen stehen im Laderaum und fahren mit; wer einen neuen Befehl bekommt, steigt aus
			for (size_t k = 0; k < crew.size();)
			{
				DROID *d = findDroid(crew[k]);
				if (d == nullptr) { s_seat.erase(crew[k]); crew.erase(crew.begin() + static_cast<std::ptrdiff_t>(k)); continue; }
				if (d->order.type != DORDER_NONE && d->order.type != DORDER_STOP && d->order.type != DORDER_HOLD)
				{
					// neuer Befehl: absitzen (der Befehl selbst bleibt und wird danach ausgefuehrt)
					DroidOrder keep = d->order;
					dismount(d, apc, static_cast<int>(k));
					crew.erase(crew.begin() + static_cast<std::ptrdiff_t>(k));
					orderDroidBase(d, &keep);
					continue;
				}
				const Vector3i sp = seatPos(apc, static_cast<int>(k));
				d->pos = sp;
				d->rot = apc->rot;
				d->sMove.speed = 0;
				++k;
			}
		}
	}
	// zerstoerte Transporter: alle springen ab (stehen dort, wo der Laderaum war)
	for (auto it = s_load.begin(); it != s_load.end();)
	{
		if (std::find(liveApcs.begin(), liveApcs.end(), it->first) != liveApcs.end()) { ++it; continue; }
		for (size_t k = 0; k < it->second.size(); ++k)
		{
			DROID *d = findDroid(it->second[k]);
			if (d != nullptr) { dismount(d, nullptr, static_cast<int>(k)); }
			else { s_seat.erase(it->second[k]); }
		}
		it = s_load.erase(it);
	}
}

void reset()
{
	s_load.clear();
	s_seat.clear();
}
}
