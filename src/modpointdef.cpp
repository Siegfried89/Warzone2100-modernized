// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Punktverteidigung (Metal Fatigue) und Raketenabwehr (AMS) - siehe modpointdef.h
#include "modpointdef.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/framework/math_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "ai.h"
#include "droid.h"
#include "effects.h"
#include "game_world.h"
#include "geometry.h"
#include "modarmorhit.h"
#include "modcompbody.h"
#include "modlayer.h"
#include "objmem.h"
#include "projectile.h"
#include "random.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace modpointdef
{
namespace
{
constexpr unsigned COOLDOWN_MS = 1200;
constexpr unsigned CHANCE_PCT = 60;
constexpr int MIN_AGE_MS = 150;                 // nicht schon im Lauf der Abschussrampe abfangen

struct Station
{
	BASE_OBJECT *obj;
	int slot;                                   // Waffenplatz (AMS) bzw. 0 (Punktverteidigung)
	const WEAPON_STATS *ws;
	int range;
	modlayer::Layer layer;
	bool ams;                                   // AMS: nur Artillerie-Raketen; Punktverteidigung (MF): alle Steilfeuer-/Flug-Geschosse
};

struct Pending
{
	uint32_t projId;
	uint32_t killTime;                          // Spielzeit, zu der die Leuchtspur das Geschoss erreicht
};

int s_wpn = -2;                                 // Index der Waffe "MFPointDefense" (-2 = noch nicht gesucht, -1 = gibt es nicht)
uint32_t s_tick = UINT32_MAX;
std::vector<Station> s_stations;
std::unordered_map<uint64_t, uint32_t> s_ready;   // (Objekt-ID, Platz) -> fruehester naechster Abschuss
std::vector<Pending> s_pending;
bool s_anyAms = false;
size_t s_amsScanned = SIZE_MAX;

int pdWeapon()
{
	if (s_wpn == -2)
	{
		s_wpn = -1;
		for (unsigned i = 0; i < asWeaponStats.size(); i++)
		{
			if (asWeaponStats[i].id == "MFPointDefense") { s_wpn = static_cast<int>(i); break; }
		}
	}
	return s_wpn;
}

bool anyAmsWeapon()
{
	if (s_amsScanned != asWeaponStats.size())
	{
		s_amsScanned = asWeaponStats.size();
		s_anyAms = std::any_of(asWeaponStats.begin(), asWeaponStats.end(), [](const WEAPON_STATS &w) { return w.modAms; });
	}
	return s_anyAms;
}

bool interceptable(const WEAPON_STATS *w)
{
	if (w == nullptr) { return false; }
	if (w->movementModel == MM_INDIRECT || w->movementModel == MM_HOMINGINDIRECT) { return true; }
	switch (w->weaponSubClass)
	{
	case WSC_MISSILE: case WSC_ROCKET: case WSC_SLOWMISSILE: case WSC_SLOWROCKET:
	case WSC_MORTARS: case WSC_HOWITZERS: case WSC_BOMB:
		return true;
	default:
		return false;
	}
}

/// AMS (Wunsch 23.11. 03:05 "feindliche Raketenartilleriegeschosse"): Raketen/Lenkflugkoerper im Steilfeuer
bool rocketArtillery(const WEAPON_STATS *w)
{
	if (w == nullptr) { return false; }
	const bool rocket = w->weaponSubClass == WSC_ROCKET || w->weaponSubClass == WSC_MISSILE || w->weaponSubClass == WSC_SLOWROCKET || w->weaponSubClass == WSC_SLOWMISSILE;
	return rocket && (w->movementModel == MM_INDIRECT || w->movementModel == MM_HOMINGINDIRECT);
}

uint64_t key(const BASE_OBJECT *o, int slot)
{
	return (static_cast<uint64_t>(o->id) << 8) | static_cast<uint64_t>(slot & 0xFF);
}

const iIMDShape *tracerModel(const WEAPON_STATS *ws)
{
	static std::unordered_map<std::string, iIMDBaseShape *> s_cache;
	static unsigned s_tried = 0;
	if (modelCacheStale(s_tried)) { s_cache.clear(); }
	if (!ws->modAmsTracer.isEmpty())
	{
		const std::string k = ws->modAmsTracer.toStdString();
		auto it = s_cache.find(k);
		if (it == s_cache.end()) { it = s_cache.emplace(k, modelGet(ws->modAmsTracer)).first; }
		if (it->second != nullptr) { return it->second->displayModel(); }
	}
	return ws->pInFlightGraphic ? ws->pInFlightGraphic->displayModel() : nullptr;
}

/// Leuchtspur(en) vom Waffenlauf zum (vorausberechneten) Geschoss - nur Anzeige
void showShot(const Station &st, const Vector3i &muzzle, const Vector3i &target, unsigned travelMs)
{
	const iIMDShape *m = tracerModel(st.ws);
	if (m == nullptr) { return; }
	const Vector3f a(static_cast<float>(muzzle.x), static_cast<float>(muzzle.z), static_cast<float>(muzzle.y));   // Effekt: x, Hoehe, Karten-y
	const Vector3f b(static_cast<float>(target.x), static_cast<float>(target.z), static_cast<float>(target.y));
	const Vector3f d = b - a;
	const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	if (len < 1.f || travelMs == 0) { return; }
	const Vector3f dir = d / len;
	const float speed = len * 1000.f / static_cast<float>(travelMs);
	const bool laser = st.ws->weaponSubClass == WSC_ENERGY;
	const int n = laser ? 1 : 5;                 // Assault Gun: kurze Salve, Laser: ein Puls
	const float spacing = laser ? 0.f : 22.f;
	for (int k = 0; k < n; ++k)
	{
		const float off = static_cast<float>(k) * spacing;
		if (off >= len) { break; }
		Vector3i p(static_cast<int>(a.x + dir.x * off), static_cast<int>(a.y + dir.y * off), static_cast<int>(a.z + dir.z * off));
		Vector3f vel = dir * speed;
		effectGiveAuxVar(static_cast<UDWORD>(std::max(40.f, (len - off) * 1000.f / speed)));
		addEffect(&p, EFFECT_GRAVITON, GRAVITON_TYPE_TRACER, true, m, 0, graphicsTime, nullptr, &vel);
	}
	if (laser)
	{
		Vector3i f(muzzle.x, muzzle.z, muzzle.y);
		addEffect(&f, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER, false, nullptr, 0, graphicsTime);
	}
}

/// Turm zum Geschoss drehen und Schuss vermerken (Spielzustand: auf allen Rechnern gleich)
void aimAndFire(const Station &st, const PROJECTILE *proj)
{
	WEAPON *w = nullptr;
	if (st.obj->type == OBJ_DROID) { w = &static_cast<DROID *>(st.obj)->asWeaps[st.slot]; }
	else if (st.obj->type == OBJ_STRUCTURE) { w = &static_cast<STRUCTURE *>(st.obj)->asWeaps[st.slot]; }
	if (w == nullptr) { return; }
	const uint16_t dir = calcDirection(st.obj->pos.x, st.obj->pos.y, proj->pos.x, proj->pos.y);
	w->rot.direction = static_cast<uint16_t>(dir - st.obj->rot.direction);
	const int dxy = iHypot((proj->pos - st.obj->pos).xy());
	const int pitch = clip(static_cast<int>(static_cast<int16_t>(iAtan2(proj->pos.z - st.obj->pos.z, std::max(1, dxy)))), 0, DEG(80));
	w->rot.pitch = static_cast<uint16_t>(pitch);
	w->lastFired = gameTime;
}

/// liegt das Geschoss im 180-Grad-Bereich einer Waffe auf einem Verbundrumpf?
bool inArc(const Station &st, const PROJECTILE *proj)
{
	uint16_t yaw = 0;
	if (!modcompbody::weaponArc(st.obj, st.slot, yaw)) { return true; }
	const uint16_t dir = calcDirection(st.obj->pos.x, st.obj->pos.y, proj->pos.x, proj->pos.y);
	return std::abs(angleDelta(static_cast<uint16_t>(dir - st.obj->rot.direction - yaw))) <= static_cast<int>(modcompbody::ARC_HALF_DEG * 65536.f / 360.f);
}

void addStation(BASE_OBJECT *o, int slot, const WEAPON_STATS *ws, bool ams)
{
	s_stations.push_back({o, slot, ws, proj_GetLongRange(*ws, static_cast<int>(o->player)), modlayer::layerOf(o), ams});
}
}

void beginTick()
{
	if (s_tick == gameTime) { return; }
	s_tick = gameTime;
	s_stations.clear();
	// erledigte/vergessene Abschuesse aufraeumen
	s_pending.erase(std::remove_if(s_pending.begin(), s_pending.end(), [](const Pending &p) { return gameTime > p.killTime + 3000; }), s_pending.end());
	const int wpn = pdWeapon();
	const bool ams = anyAmsWeapon();
	if (wpn < 0 && !ams) { return; }
	for (unsigned p = 0; p < MAX_PLAYERS; p++)
	{
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || s->numWeaps == 0) { continue; }
			if (wpn >= 0 && static_cast<int>(s->asWeaps[0].nStat) == wpn) { addStation(s, 0, &asWeaponStats[wpn], false); continue; }
			if (!ams) { continue; }
			for (unsigned i = 0; i < s->numWeaps && i < MAX_WEAPONS; ++i)
			{
				const unsigned st = s->asWeaps[i].nStat;
				if (st > 0 && st < asWeaponStats.size() && asWeaponStats[st].modAms) { addStation(s, static_cast<int>(i), &asWeaponStats[st], true); }
			}
		}
		if (!ams) { continue; }
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d->died || d->numWeaps == 0) { continue; }
			for (unsigned i = 0; i < d->numWeaps && i < MAX_WEAPONS; ++i)
			{
				const unsigned st = d->asWeaps[i].nStat;
				if (st > 0 && st < asWeaponStats.size() && asWeaponStats[st].modAms) { addStation(d, static_cast<int>(i), &asWeaponStats[st], true); }
			}
		}
	}
}

bool tryIntercept(PROJECTILE *proj)
{
	if (proj == nullptr || proj->state != PROJ_INFLIGHT) { return false; }
	// schon von einer Raketenabwehr getroffen: verschwindet, wenn die Leuchtspur ankommt
	for (auto it = s_pending.begin(); it != s_pending.end(); ++it)
	{
		if (it->projId != proj->id) { continue; }
		if (gameTime < it->killTime) { return false; }
		s_pending.erase(it);
		const Vector3i ep(proj->pos.x, proj->pos.z, proj->pos.y);
		addEffect(&ep, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL, false, nullptr, 0, gameTime);
		modarmorhit::sparks(ep, 8, 220.f);
		audio_PlayStaticTrack(proj->pos.x, proj->pos.y, ID_SOUND_EXPLOSION_SMALL);
		return true;
	}
	if (s_stations.empty() || !interceptable(proj->psWStats)) { return false; }
	if (static_cast<int>(gameTime - proj->born) < MIN_AGE_MS) { return false; }
	const int owner = static_cast<int>(proj->player);
	const modlayer::Layer layer = modlayer::projectileLayer(proj);
	const bool rocketArty = rocketArtillery(proj->psWStats);
	for (const Station &st : s_stations)
	{
		if (st.ams && !rocketArty) { continue; }
		if (st.obj->died) { continue; }
		const int sp = static_cast<int>(st.obj->player);
		if (sp == owner || aiCheckAlliancesInRange(sp, owner) || st.layer != layer) { continue; }
		const Vector2i d = proj->pos.xy() - st.obj->pos.xy();
		if (std::abs(d.x) > st.range || std::abs(d.y) > st.range || iHypot(d) > st.range) { continue; }
		if (st.ams && !inArc(st, proj)) { continue; }
		uint32_t &ready = s_ready[key(st.obj, st.slot)];
		if (gameTime < ready) { continue; }
		ready = gameTime + (st.ams ? st.ws->modAmsReload : COOLDOWN_MS);
		const bool hit = gameRand(100) < (st.ams ? st.ws->modAmsChance : CHANCE_PCT);
		if (!st.ams)
		{
			if (!hit) { continue; }   // daneben - diese Stellung muss nachladen
			// abgeschossen: kleine Explosion in der Luft (nur Anzeige), Knall
			const Vector3i ep(proj->pos.x, proj->pos.z, proj->pos.y);
			addEffect(&ep, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL, false, nullptr, 0, gameTime);
			audio_PlayStaticTrack(proj->pos.x, proj->pos.y, ID_SOUND_EXPLOSION_SMALL);
			return true;
		}
		// Raketenabwehr: Turm richtet sich aus und feuert eine Leuchtspur-Salve; trifft sie, verschwindet das Geschoss bei ihrer Ankunft
		aimAndFire(st, proj);
		Vector3i muzzle = st.obj->pos + Vector3i(0, 0, 20);
		if (st.obj->type == OBJ_DROID) { calcDroidMuzzleLocation(static_cast<DROID *>(st.obj), &muzzle, st.slot); }
		else if (st.obj->type == OBJ_STRUCTURE) { calcStructureMuzzleLocation(static_cast<STRUCTURE *>(st.obj), &muzzle, st.slot); }
		const float speed = static_cast<float>(std::clamp<unsigned>(st.ws->flightSpeed, 1500, 8000)) * (st.ws->weaponSubClass == WSC_ENERGY ? 1.6f : 1.f);
		const Vector3i dd = proj->pos - muzzle;
		const float dist = std::sqrt(static_cast<float>(dd.x) * dd.x + static_cast<float>(dd.y) * dd.y + static_cast<float>(dd.z) * dd.z);
		const unsigned travel = static_cast<unsigned>(std::clamp(dist * 1000.f / speed, 60.f, 1500.f));
		// Ziel der Leuchtspur: wo das Geschoss bei ihrer Ankunft etwa ist (waagerecht Richtung Ziel, Steilfeuer-Tempo vXY)
		Vector3i aimAt = proj->pos;
		const Vector2i toDst = proj->dst.xy() - proj->pos.xy();
		const int dl = std::max(1, iHypot(toDst));
		const int adv = std::min(dl, static_cast<int>(static_cast<int64_t>(std::max(0, proj->vXY)) * travel / 1000));
		aimAt.x += toDst.x * adv / dl;
		aimAt.y += toDst.y * adv / dl;
		if (st.obj->visibleForLocalDisplay() || proj->bVisible)
		{
			showShot(st, muzzle, hit ? aimAt : aimAt + Vector3i(rand() % 81 - 40, rand() % 81 - 40, rand() % 41 - 10), travel);   // nur Anzeige: rand()
			if (st.ws->iAudioFireID != NO_SOUND) { audio_PlayStaticTrack(st.obj->pos.x, st.obj->pos.y, st.ws->iAudioFireID); }
		}
		if (hit)
		{
			s_pending.push_back({proj->id, gameTime + travel});
			return false;
		}
	}
	return false;
}

void reset()
{
	s_wpn = -2;
	s_tick = UINT32_MAX;
	s_stations.clear();
	s_ready.clear();
	s_pending.clear();
	s_amsScanned = SIZE_MAX;
}
}
