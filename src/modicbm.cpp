// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Atomraketen-Silo (siehe modicbm.h)
#include "modicbm.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/pieclip.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "component.h"
#include "console.h"
#include "display.h"
#include "effects.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "objects.h"
#include "power.h"
#include "projectile.h"
#include "stats.h"
#include "structure.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/transform.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace modicbm
{
namespace
{
constexpr int LAUNCH_COST = 1500;          // Energie je Start
constexpr unsigned RISE_MS = 6500;         // senkrechter Aufstieg aus dem Silo
constexpr float RISE_H = 2200.f;           // Hoehe am Ende des Aufstiegs (Weltmass)
constexpr float ARC_H = 2600.f;            // zusaetzliche Bogenhoehe auf der Flugstrecke
constexpr unsigned CLOUD_MS = 22000;       // Atompilz (Anzeige)

struct Flight
{
	int player = 0;
	Vector3i src;        // Silo (Boden)
	Vector3i dst;        // Ziel (Boden)
	unsigned launch = 0;
	unsigned impact = 0; // Spielzeit des Einschlags
	bool done = false;   // Einschlag ausgeloest (Spielwirkung)
};
std::vector<Flight> s_flights;

struct Cloud
{
	Vector3i pos;
	unsigned start = 0;   // graphicsTime
	unsigned nextPuff = 0;
	unsigned nextFire = 0;
};
std::vector<Cloud> s_clouds;

iIMDBaseShape *s_missile = nullptr;
iIMDBaseShape *s_flame = nullptr;
bool s_modelsTried = false;

unsigned flightMs(const Vector3i &a, const Vector3i &b)
{
	const int d = iHypot((b - a).xy());
	// etwa 10 Felder je Sekunde, mindestens 5 s, hoechstens 22 s
	return static_cast<unsigned>(std::clamp(d * 1000 / (10 * TILE_UNITS), 5000, 22000));
}

int blastWeapon()
{
	static int idx = -2;
	if (idx == -2) { idx = getCompFromName(COMP_WEAPON, WzString::fromUtf8("NukeBlast")); }
	return idx;
}

// Position und Flugrichtung der Rakete zur Zeit t (ms seit dem Start); false = noch nicht/nicht mehr in der Luft
bool missileAt(const Flight &f, float t, glm::vec3 &pos, glm::vec3 &dir)
{
	const float g0 = static_cast<float>(f.src.z), g1 = static_cast<float>(f.dst.z);
	const float total = static_cast<float>(f.impact - f.launch);
	if (t < 0.f || t > total) { return false; }
	if (t <= static_cast<float>(RISE_MS))
	{
		const float k = t / static_cast<float>(RISE_MS);
		const float h = RISE_H * k * k;   // beschleunigt
		pos = glm::vec3(static_cast<float>(f.src.x), g0 + h, -static_cast<float>(f.src.y));
		dir = glm::vec3(0.f, 1.f, 0.f);
		return true;
	}
	auto at = [&](float u) {
		const float x = static_cast<float>(f.src.x) + (static_cast<float>(f.dst.x) - static_cast<float>(f.src.x)) * u;
		const float y = static_cast<float>(f.src.y) + (static_cast<float>(f.dst.y) - static_cast<float>(f.src.y)) * u;
		const float base = (g0 + RISE_H) * (1.f - u) + g1 * u;
		const float h = base + ARC_H * 4.f * u * (1.f - u) * (1.f - 0.35f * u);
		return glm::vec3(x, h, -y);
	};
	const float u = std::clamp((t - static_cast<float>(RISE_MS)) / (total - static_cast<float>(RISE_MS)), 0.f, 1.f);
	// sanfter Uebergang aus der Senkrechten: zu Beginn der Flugphase zieht die Bahn noch etwas nach oben
	pos = at(u);
	const glm::vec3 ahead = at(std::min(1.f, u + 0.01f));
	glm::vec3 d = ahead - pos;
	if (u < 0.08f) { d = glm::mix(glm::vec3(0.f, glm::length(d), 0.f), d, u / 0.08f); }
	const float l = glm::length(d);
	dir = l > 1e-3f ? d / l : glm::vec3(0.f, -1.f, 0.f);
	return true;
}

glm::mat4 alignY(const glm::vec3 &pos, const glm::vec3 &dir)
{
	const glm::vec3 up(0.f, 1.f, 0.f);
	const glm::vec3 axis = glm::cross(up, dir);
	const float s = glm::length(axis);
	const float c = glm::dot(up, dir);
	glm::mat4 r(1.f);
	if (s > 1e-4f) { r = glm::rotate(std::atan2(s, c), axis / s); }
	else if (c < 0.f) { r = glm::rotate(3.14159265f, glm::vec3(1.f, 0.f, 0.f)); }
	return glm::translate(pos) * r;
}

void startCloud(const Vector3i &p)
{
	Cloud c;
	c.pos = p;
	c.start = graphicsTime;
	s_clouds.push_back(c);
	// Blitz, Druckwelle, Beben
	for (int k = 0; k < 6; ++k)
	{
		Vector3i e(p.x, p.z + 40 + k * 60, p.y);
		addEffect(&e, EFFECT_EXPLOSION, EXPLOSION_TYPE_SHOCKWAVE, false, nullptr, 0);
	}
	for (int k = 0; k < 16; ++k)
	{
		const int a = k * 4096 / 16;
		Vector3i e(p.x + iSinR(static_cast<uint16_t>(a * 16), 300), p.z + 30, p.y + iCosR(static_cast<uint16_t>(a * 16), 300));
		addEffect(&e, EFFECT_EXPLOSION, EXPLOSION_TYPE_LARGE, false, nullptr, 0);
	}
	shakeStart(2500);
	audio_PlayStaticTrack(p.x, p.y, ID_SOUND_EXPLOSION);
	audio_PlayStaticTrack(p.x, p.y, ID_SOUND_EXPLOSION_LASER);
}

void tickClouds()
{
	for (Cloud &c : s_clouds)
	{
		const unsigned age = graphicsTime - c.start;
		if (age > CLOUD_MS) { continue; }
		const float t = static_cast<float>(age) / 1000.f;
		// Stamm waechst schnell auf ~1500 hoch, Kappe darueber breitet sich aus
		const float stemH = 1500.f * (1.f - std::exp(-t / 2.2f));
		const float capR = 180.f + 520.f * (1.f - std::exp(-t / 3.f));
		if (graphicsTime >= c.nextPuff)
		{
			c.nextPuff = graphicsTime + (t < 6.f ? 35u : 90u);
			const unsigned seed = graphicsTime * 2654435761u;
			// Stamm
			const float sh = stemH * static_cast<float>((seed >> 8) % 1000u) / 1000.f;
			const int sr = 60 + static_cast<int>(sh * 0.05f);
			Vector3i s(c.pos.x + static_cast<int>((seed >> 3) % static_cast<unsigned>(2 * sr)) - sr, c.pos.z + static_cast<int>(sh),
			           c.pos.y + static_cast<int>((seed >> 13) % static_cast<unsigned>(2 * sr)) - sr);
			addEffect(&s, EFFECT_SMOKE, SMOKE_TYPE_BILLOW, false, nullptr, 0);
			// Kappe: Ring + Fuellung
			for (int k = 0; k < 3; ++k)
			{
				const uint16_t ang = static_cast<uint16_t>((seed >> (k * 5)) & 0xffffu);
				const float rr = capR * (k == 0 ? 1.f : 0.55f);
				Vector3i q(c.pos.x + static_cast<int>(iSinR(ang, static_cast<int32_t>(rr))), c.pos.z + static_cast<int>(stemH + 80.f + 120.f * static_cast<float>(k)),
				           c.pos.y + static_cast<int>(iCosR(ang, static_cast<int32_t>(rr))));
				addEffect(&q, EFFECT_SMOKE, SMOKE_TYPE_BILLOW, false, nullptr, 0);
			}
			if (t < 4.f)
			{
				// gluehender Feuerball im Kern der Kappe
				Vector3i f(c.pos.x, c.pos.z + static_cast<int>(stemH + 60.f), c.pos.y);
				addEffect(&f, EFFECT_EXPLOSION, EXPLOSION_TYPE_LARGE, false, nullptr, 0);
			}
			if (t < 3.f)
			{
				// Staubwelle am Boden
				const uint16_t ang = static_cast<uint16_t>(seed & 0xffffu);
				const int r = static_cast<int>(300.f + 1400.f * t / 3.f);
				Vector3i g(c.pos.x + iSinR(ang, r), c.pos.z + 20, c.pos.y + iCosR(ang, r));
				addEffect(&g, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING, false, nullptr, 0);
			}
		}
		if (t < 16.f && graphicsTime >= c.nextFire)
		{
			c.nextFire = graphicsTime + 160u;
			const unsigned seed = graphicsTime * 40503u + 17u;
			const uint16_t ang = static_cast<uint16_t>(seed & 0xffffu);
			const int r = static_cast<int>((seed >> 16) % 640u);
			Vector3i f(c.pos.x + iSinR(ang, r), c.pos.z, c.pos.y + iCosR(ang, r));
			f.y = map_Height(gameWorld.map, f.x, f.z);
			addEffect(&f, EFFECT_FIRE, FIRE_TYPE_SMOKY, false, nullptr, 0);
		}
	}
	s_clouds.erase(std::remove_if(s_clouds.begin(), s_clouds.end(), [](const Cloud &c) { return graphicsTime - c.start > CLOUD_MS; }), s_clouds.end());
}
} // namespace

bool isNukeSilo(const STRUCTURE_STATS *psStats)
{
	return psStats != nullptr && psStats->id.compare("A0NukeSilo") == 0;
}

const char *readyText(const STRUCTURE *psStruct)
{
	if (psStruct != nullptr && isNukeSilo(psStruct->pStructureType))
	{
		return _("Nuclear missile ready to launch!");
	}
	return _("Laser Satellite is ready to fire!");
}

bool launch(STRUCTURE *psSilo, const BASE_OBJECT *psTarget)
{
	if (psSilo == nullptr || !isNukeSilo(psSilo->pStructureType))
	{
		return false;
	}
	if (psTarget == nullptr || psSilo->status != SS_BUILT || psSilo->numWeaps == 0)
	{
		return true;
	}
	const int player = psSilo->player;
	const unsigned pause = static_cast<unsigned>(weaponFirePause(*psSilo->getWeaponStats(0), player));
	const unsigned last = psSilo->asWeaps[0].lastFired;
	if (last == 0 || gameTime - last < pause)
	{
		if (player == selectedPlayer)
		{
			const unsigned left = last == 0 ? pause / 1000 : (pause - (gameTime - last)) / 1000;
			char buf[160];
			ssprintf(buf, _("Nuclear missile still being built: %u:%02u"), left / 60, left % 60);
			addConsoleMessage(buf, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		}
		return true;
	}
	if (getPower(static_cast<unsigned>(player)) < LAUNCH_COST)
	{
		if (player == selectedPlayer)
		{
			char buf[160];
			ssprintf(buf, _("Not enough power to launch the nuclear missile (%d needed)"), LAUNCH_COST);
			addConsoleMessage(buf, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		}
		return true;
	}
	usePower(player, LAUNCH_COST);
	Flight f;
	f.player = player;
	f.src = Vector3i(psSilo->pos.x, psSilo->pos.y, map_Height(gameWorld.map, psSilo->pos.x, psSilo->pos.y));
	f.dst = Vector3i(psTarget->pos.x, psTarget->pos.y, map_Height(gameWorld.map, psTarget->pos.x, psTarget->pos.y));
	f.launch = gameTime;
	f.impact = gameTime + RISE_MS + flightMs(f.src, f.dst);
	s_flights.push_back(f);
	psSilo->asWeaps[0].lastFired = gameTime;   // die naechste Rakete wird gebaut
	psSilo->asWeaps[0].ammo = 1;               // "bereit"-Meldung, wenn sie fertig ist
	audio_PlayStaticTrack(f.src.x, f.src.y, ID_SOUND_ROCKET);
	audio_QueueTrackPos(ID_SOUND_LAS_SAT_COUNTDOWN, f.dst.x, f.dst.y, f.dst.z);
	if (player == selectedPlayer)
	{
		addConsoleMessage(_("Nuclear missile launched!"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
	else
	{
		addConsoleMessage(_("Warning: nuclear missile launch detected!"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
	return true;
}

void tick()
{
	// erste Rakete: Bau beginnt mit der Fertigstellung des Silos
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s != nullptr && !s->died && s->status == SS_BUILT && s->numWeaps > 0 && isNukeSilo(s->pStructureType) && s->asWeaps[0].lastFired == 0)
			{
				s->asWeaps[0].lastFired = gameTime;
				s->asWeaps[0].ammo = 1;
			}
		}
	}
	for (Flight &f : s_flights)
	{
		if (f.done || gameTime < f.impact) { continue; }
		f.done = true;
		const int w = blastWeapon();
		if (w > 0)
		{
			WEAPON wpn;
			wpn.nStat = static_cast<uint32_t>(w);
			wpn.ammo = 1;
			proj_SendProjectile(&wpn, nullptr, f.player, f.dst, nullptr, true, 0);
		}
		startCloud(f.dst);
	}
	s_flights.erase(std::remove_if(s_flights.begin(), s_flights.end(), [](const Flight &f) { return f.done && gameTime > f.impact + 1000; }), s_flights.end());
}

void draw3D(const glm::mat4 &viewMatrix)
{
	if (!s_clouds.empty()) { tickClouds(); }
	if (s_flights.empty()) { return; }
	if (!s_modelsTried)
	{
		s_modelsTried = true;
		s_missile = modelGet(WzString::fromUtf8("modnukemissile.pie"));
		s_flame = modelGet(WzString::fromUtf8("fxicbm.pie"));
	}
	if (s_missile == nullptr || modlayer::current() != modlayer::SURFACE) { return; }
	static unsigned s_nextTrail = 0;
	const glm::mat4 bill(glm::transpose(glm::mat3(viewMatrix)));
	for (const Flight &f : s_flights)
	{
		if (f.done) { continue; }
		const float t = static_cast<float>(graphicsTime) - static_cast<float>(f.launch);
		glm::vec3 pos, dir;
		if (!missileAt(f, t, pos, dir)) { continue; }
		const glm::mat4 m = alignY(pos, dir);
		pie_Draw3DShape(s_missile->displayModel(), 0, getPlayerColour(f.player), WZCOL_WHITE, 0, 0, m, viewMatrix, 0.f);
		if (s_flame != nullptr)
		{
			const float fl = 2.0f + 0.3f * static_cast<float>((graphicsTime / 40u) % 3u);
			const glm::mat4 fm = glm::translate(pos - dir * 14.f) * glm::mat4(bill) * glm::scale(glm::vec3(fl));
			pie_Draw3DShape(s_flame->displayModel(), 0, 0, WZCOL_WHITE, pie_ADDITIVE, 230, fm, viewMatrix, 0.f, true);
		}
		if (graphicsTime >= s_nextTrail)
		{
			const glm::vec3 tail = pos - dir * 30.f;
			Vector3i e(static_cast<int>(tail.x), static_cast<int>(tail.y), static_cast<int>(-tail.z));
			addEffect(&e, EFFECT_SMOKE, t < static_cast<float>(RISE_MS) * 0.3f ? SMOKE_TYPE_BILLOW : SMOKE_TYPE_TRAIL, false, nullptr, 0);
		}
	}
	if (graphicsTime >= s_nextTrail) { s_nextTrail = graphicsTime + 45u; }
}

void reset()
{
	s_flights.clear();
	s_clouds.clear();
	s_missile = nullptr;
	s_flame = nullptr;
	s_modelsTried = false;
}
}
