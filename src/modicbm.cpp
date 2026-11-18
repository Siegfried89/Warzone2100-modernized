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
#include <unordered_map>
#include <vector>

#include "droid.h"
#include "move.h"

namespace modicbm
{
namespace
{
constexpr int LAUNCH_COST = 1500;          // Energie je Start
constexpr unsigned RISE_MS = 6500;         // senkrechter Aufstieg aus dem Silo
constexpr float RISE_H = 2200.f;           // Hoehe am Ende des Aufstiegs (Weltmass)
constexpr float ARC_H = 2600.f;            // zusaetzliche Bogenhoehe auf der Flugstrecke
constexpr unsigned CLOUD_MS = 75000;       // Atompilz (Anzeige) - (18.11. 20:46 "der einige Zeit anhalten soll")

struct Flight
{
	int player = 0;
	Vector3i src;        // Silo (Boden)
	Vector3i dst;        // Ziel (Boden)
	unsigned launch = 0;
	unsigned impact = 0; // Spielzeit des Einschlags
	bool done = false;   // Einschlag ausgeloest (Spielwirkung)
	float startH = 0.f;  // Starthoehe ueber dem Boden (mobiler Werfer: Mitte der aufgerichteten Rakete)
	float scale = 1.f;   // Groesse der Rakete im Flug (mobiler Werfer: kleiner)
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
		const float h = f.startH + (RISE_H - f.startH) * k * k;   // beschleunigt
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
// (18.11. 20:46 "Und der Atompilz, der einige Zeit anhalten soll"): fester Pilz aus drei Modellen (nukecloud.py: Stamm, Kappe,
// Staubring am Boden) mit der Rauchtextur der WZ-Effekte - gluehend in den ersten Sekunden, dann grau-braun, waechst, dreht sich langsam,
// steht gut eine Minute und verblasst dann. Die bisherigen Rauchballen und Braende kommen dazu.
iIMDBaseShape *s_cStem = nullptr, *s_cCap = nullptr, *s_cRing = nullptr;
unsigned s_cGen = 0;

float smooth01(float a, float b, float x)
{
	const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

PIELIGHT rgb(float r, float g, float b)
{
	PIELIGHT c;
	c.byte.r = static_cast<UBYTE>(std::clamp(r, 0.f, 255.f)); c.byte.g = static_cast<UBYTE>(std::clamp(g, 0.f, 255.f));
	c.byte.b = static_cast<UBYTE>(std::clamp(b, 0.f, 255.f)); c.byte.a = 255;
	return c;
}

void drawClouds(const glm::mat4 &viewMatrix)
{
	if (s_clouds.empty()) { return; }
	if (modelCacheStale(s_cGen))
	{
		s_cStem = modelGet(WzString::fromUtf8("modnukecloud_stem.pie"));
		s_cCap = modelGet(WzString::fromUtf8("modnukecloud_cap.pie"));
		s_cRing = modelGet(WzString::fromUtf8("modnukecloud_ring.pie"));
	}
	if (s_cStem == nullptr || s_cCap == nullptr || s_cRing == nullptr) { return; }
	for (const Cloud &c : s_clouds)
	{
		const float t = static_cast<float>(graphicsTime - c.start) / 1000.f;
		if (t < 0.f || t * 1000.f > static_cast<float>(CLOUD_MS)) { continue; }
		const float grow = 1.f - std::exp(-t / 3.5f);
		const float stemH = 1700.f * grow + 6.f * t;
		const float capR = 240.f + 660.f * (1.f - std::exp(-t / 5.f)) + 3.f * t;
		const float ringR = 380.f + 1500.f * (1.f - std::exp(-t / 2.5f));
		const float heat = std::exp(-t / 4.f);                                     // 1 = weissglueheend .. 0 = kalter Rauch
		const float life = static_cast<float>(CLOUD_MS) / 1000.f;
		const float fade = 1.f - smooth01(life - 22.f, life, t);                    // verblassen am Ende
		const float yaw = 0.035f * t;
		const glm::vec3 base(static_cast<float>(c.pos.x), static_cast<float>(c.pos.z), -static_cast<float>(c.pos.y));
		// Farbe: gluehend orange -> grau-braun
		const PIELIGHT smoke = rgb(140.f + 115.f * heat, 128.f + 50.f * heat, 118.f - 40.f * heat);
		const int alpha = static_cast<int>(235.f * fade);
		if (alpha <= 2) { continue; }
		const glm::mat4 R = glm::rotate(yaw, glm::vec3(0.f, 1.f, 0.f));
		// Stamm (Modell 100 hoch, Radius ~20)
		const float sr = 0.6f + 0.4f * grow;                                        // Stamm ~1/3 so breit wie die Kappe
		const glm::mat4 Ms = glm::translate(base) * R * glm::scale(glm::vec3(sr * capR / 135.f, stemH / 100.f, sr * capR / 135.f));
		pie_Draw3DShape(s_cStem->displayModel(), 0, 0, smoke, pie_TRANSLUCENT, alpha, Ms, viewMatrix, 0.f, true);
		// Kappe (Modell Radius 100, Hoehe ~70) auf dem Stamm
		const glm::mat4 Mc = glm::translate(base + glm::vec3(0.f, stemH - 0.22f * capR, 0.f)) * R * glm::scale(glm::vec3(capR / 100.f, capR * 0.72f / 100.f, capR / 100.f));
		pie_Draw3DShape(s_cCap->displayModel(), 0, 0, smoke, pie_TRANSLUCENT, alpha, Mc, viewMatrix, 0.f, true);
		if (heat > 0.05f)
		{
			// gluehender Kern (additiv), anfangs grell
			const PIELIGHT glow = rgb(255.f, 150.f + 90.f * heat, 60.f + 120.f * heat * heat);
			const glm::mat4 Mg = glm::translate(base + glm::vec3(0.f, stemH - 0.25f * capR, 0.f)) * R * glm::scale(glm::vec3(capR * 0.86f / 100.f, capR * 0.6f / 100.f, capR * 0.86f / 100.f));
			pie_Draw3DShape(s_cCap->displayModel(), 0, 0, glow, pie_ADDITIVE, static_cast<int>(230.f * heat * fade), Mg, viewMatrix, 0.f, true);
			pie_Draw3DShape(s_cStem->displayModel(), 0, 0, glow, pie_ADDITIVE, static_cast<int>(160.f * heat * fade), Ms * glm::scale(glm::vec3(0.7f, 1.f, 0.7f)), viewMatrix, 0.f, true);
		}
		// Staubring am Boden (breitet sich schnell aus, verblasst nach ~30 s)
		const int ra = static_cast<int>(220.f * fade * (1.f - smooth01(8.f, 32.f, t)));
		if (ra > 2)
		{
			const PIELIGHT dust = rgb(150.f + 60.f * heat, 132.f + 20.f * heat, 110.f);
			const glm::mat4 Mr = glm::translate(base) * R * glm::scale(glm::vec3(ringR / 100.f, 0.9f + 0.5f * grow, ringR / 100.f));
			pie_Draw3DShape(s_cRing->displayModel(), 0, 0, dust, pie_TRANSLUCENT, ra, Mr, viewMatrix, 0.f, true);
		}
	}
}

// ---------------------------------------------------------------- mobiler Werfer (TEL)
// (18.11. 20:46 "Die mobile Nuklearraketen-Abschussanimation funktioniert gar nicht. Es fehlen VIELE Sachen."): Ablauf wie bei den
// russischen Werfern - anhalten, vier Stuetzen ausfahren, Rakete senkrecht aufrichten, Start mit grosser Rauchwolke am Boden, Rakete
// steigt senkrecht und schwenkt dann zum Ziel (wie die Silo-Rakete, Einschlag mit Atompilz), leerer Aufrichter legt sich wieder hin;
// 6 min Nachladen (die Rakete ist so lange nicht auf dem Fahrzeug). Spielwirkung (Anhalten, Start, Einschlag) im Spielschritt.
enum TelMode : uint8_t { TEL_IDLE = 0, TEL_ERECT, TEL_LOWER };
struct Tel
{
	TelMode mode = TEL_IDLE;
	UDWORD since = 0;
	Vector3i target;
	float from = 0.f;    // Aufrichte-Stand beim Einfahren (abgebrochen: von dort zurueck)
};
std::unordered_map<uint32_t, Tel> s_tel;
constexpr UDWORD TEL_ERECT_MS = 8000;      // Stuetzen (erste 1,8 s), dann aufrichten, kurz stehen
constexpr UDWORD TEL_LOWER_MS = 5000;      // leeren Aufrichter hinlegen, Stuetzen ein
constexpr UDWORD TEL_RELOAD_MS = 360000;   // 6 min Nachladen
int s_telWeapon = -2;
struct LaunchFx { Vector3i pos; unsigned start = 0; unsigned next = 0; };
std::vector<LaunchFx> s_launchFx;

int telWeapon()
{
	if (s_telWeapon == -2) { s_telWeapon = getCompFromID(COMP_WEAPON, WzString::fromUtf8("NukeTEL")); }
	return s_telWeapon;
}

bool telStat(const DROID *d)
{
	const int w = telWeapon();
	return d != nullptr && w > 0 && d->numWeaps > 0 && d->asWeaps[0].nStat == static_cast<uint32_t>(w);
}

/// Aufrichte-Stand 0 (liegt) .. 1 (senkrecht) und Stuetzen 0..1
void telPose(const DROID *d, UDWORD time, float &erect, float &jacks)
{
	erect = 0.f; jacks = 0.f;
	if (s_tel.empty()) { return; }
	const auto it = s_tel.find(d->id);
	if (it == s_tel.end()) { return; }
	const Tel &t = it->second;
	const float age = static_cast<float>(static_cast<int>(time - t.since));
	if (t.mode == TEL_ERECT)
	{
		jacks = smooth01(0.f, 1800.f, age);
		erect = smooth01(1800.f, 6800.f, age);
	}
	else if (t.mode == TEL_LOWER)
	{
		const float k = 1.f - smooth01(0.f, static_cast<float>(TEL_LOWER_MS) - 1200.f, age);
		erect = t.from * k;
		jacks = 1.f - smooth01(static_cast<float>(TEL_LOWER_MS) - 1400.f, static_cast<float>(TEL_LOWER_MS), age);
	}
}

/// Weltpunkt des Drehpunkts der Rakete (hinten auf der Ladeflaeche) und Mitte der aufgerichteten Rakete darueber
Vector3i telLaunchPoint(const DROID *d, float &centreH)
{
	float pivotZ = 72.f, pivotY = 18.f, length = 130.f;
	const iIMDBaseShape *bodyShape = BODY_IMD(d, d->player);
	if (bodyShape != nullptr && !bodyShape->connectors.empty())
	{
		const glm::vec3 c = glm::vec3(bodyShape->connectors[0].xzy());
		pivotY = c.y; pivotZ = c.z;
	}
	const iIMDBaseShape *w = WEAPON_IMD(d, 0);
	if (w != nullptr && !w->connectors.empty())
	{
		length = std::abs(w->connectors[0].y);    // Datei: x, Tiefe, Hoehe -> Tiefe = Laenge bis zur Spitze
	}
	centreH = pivotY + length * 0.5f;
	// Modell +z = hinten
	const int dx = -iSinR(d->rot.direction, static_cast<int>(pivotZ));
	const int dy = -iCosR(d->rot.direction, static_cast<int>(pivotZ));
	return Vector3i(d->pos.x + dx, d->pos.y + dy, map_Height(gameWorld.map, d->pos.x + dx, d->pos.y + dy));
}

bool telWantsMove(const DROID *d)
{
	return d->sMove.Status != MOVEINACTIVE && d->sMove.Status != MOVEHOVER;
}

void telLaunch(DROID *d, Tel &t)
{
	float centreH = 80.f;
	Flight f;
	f.player = d->player;
	f.src = telLaunchPoint(d, centreH);
	f.dst = t.target;
	f.launch = gameTime;
	f.impact = gameTime + RISE_MS + flightMs(f.src, f.dst);
	f.startH = centreH;
	f.scale = 0.55f;
	s_flights.push_back(f);
	d->asWeaps[0].lastFired = gameTime;
	LaunchFx fx; fx.pos = f.src; fx.start = graphicsTime; s_launchFx.push_back(fx);
	audio_PlayStaticTrack(f.src.x, f.src.y, ID_SOUND_ROCKET);
	audio_PlayStaticTrack(f.src.x, f.src.y, ID_SOUND_EXPLOSION);
	audio_QueueTrackPos(ID_SOUND_LAS_SAT_COUNTDOWN, f.dst.x, f.dst.y, f.dst.z);
	if (d->player == selectedPlayer) { addConsoleMessage(_("Nuclear missile launched!"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
	else { addConsoleMessage(_("Warning: nuclear missile launch detected!"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
	t.mode = TEL_LOWER;
	t.since = gameTime;
	t.from = 1.f;
}

void tickTel()
{
	if (s_tel.empty()) { return; }
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d->died) { continue; }
			const auto it = s_tel.find(d->id);
			if (it == s_tel.end()) { continue; }
			Tel &t = it->second;
			const UDWORD age = gameTime - t.since;
			if (t.mode == TEL_ERECT)
			{
				if (telWantsMove(d))
				{
					// abgebrochen: vom erreichten Stand zurueck
					t.from = smooth01(1800.f, 6800.f, static_cast<float>(age));
					t.mode = TEL_LOWER; t.since = gameTime;
				}
				else if (age >= TEL_ERECT_MS)
				{
					telLaunch(d, t);
				}
			}
			else if (t.mode == TEL_LOWER && age >= TEL_LOWER_MS)
			{
				t.mode = TEL_IDLE;
			}
		}
	}
	for (auto it = s_tel.begin(); it != s_tel.end();)
	{
		it = it->second.mode == TEL_IDLE ? s_tel.erase(it) : std::next(it);
	}
}

void drawLaunchFx()
{
	for (LaunchFx &fx : s_launchFx)
	{
		const unsigned age = graphicsTime - fx.start;
		if (age > 5000u || graphicsTime < fx.next) { continue; }
		fx.next = graphicsTime + 40u;
		const unsigned seed = graphicsTime * 2246822519u + fx.pos.x;
		// grosse Rauchwolke rings um den Werfer, unten Feuer
		for (int k = 0; k < 3; ++k)
		{
			const uint16_t ang = static_cast<uint16_t>((seed >> (k * 7)) & 0xffffu);
			const int r = 40 + static_cast<int>((seed >> (k * 3 + 11)) % (60u + age / 12u));
			Vector3i e(fx.pos.x + iSinR(ang, r), fx.pos.z + 10 + static_cast<int>((seed >> 5) % 60u), fx.pos.y + iCosR(ang, r));
			addEffect(&e, EFFECT_SMOKE, SMOKE_TYPE_BILLOW, false, nullptr, 0);
		}
		if (age < 1600u)
		{
			Vector3i f(fx.pos.x, fx.pos.z + 20, fx.pos.y);
			addEffect(&f, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM, false, nullptr, 0);
		}
	}
	s_launchFx.erase(std::remove_if(s_launchFx.begin(), s_launchFx.end(), [](const LaunchFx &fx) { return graphicsTime - fx.start > 5200u; }), s_launchFx.end());
}
} // namespace

bool telIntercept(DROID *psDroid, const BASE_OBJECT *psTarget)
{
	if (!telStat(psDroid)) { return false; }
	if (psTarget == nullptr) { return true; }
	const WEAPON &w = psDroid->asWeaps[0];
	if (w.lastFired != 0 && gameTime - w.lastFired < TEL_RELOAD_MS) { return true; }   // Rakete wird nachgeladen
	if (s_tel.count(psDroid->id) != 0) { return true; }                                // richtet sich gerade auf / legt sich hin
	if (telWantsMove(psDroid)) { return true; }                                        // nur im Stand
	Tel t;
	t.mode = TEL_ERECT;
	t.since = gameTime;
	t.target = Vector3i(psTarget->pos.x, psTarget->pos.y, map_Height(gameWorld.map, psTarget->pos.x, psTarget->pos.y));
	s_tel[psDroid->id] = t;
	return true;
}

bool telHalted(const DROID *psDroid)
{
	return !s_tel.empty() && s_tel.count(psDroid->id) != 0;
}

uint16_t telPitch(const DROID *psDroid, uint16_t aimPitch, UDWORD time)
{
	if (!telStat(psDroid)) { return aimPitch; }
	float erect, jacks;
	telPose(psDroid, time, erect, jacks);
	return static_cast<uint16_t>(static_cast<int>(erect * static_cast<float>(DEG(89))));
}

bool telRocketLoaded(const DROID *psDroid)
{
	if (!telStat(psDroid)) { return false; }
	const UDWORD last = psDroid->asWeaps[0].lastFired;
	return last == 0 || gameTime - last >= TEL_RELOAD_MS;
}

bool drawTelRocket(const DROID *psDroid, const glm::mat4 &weaponMatrix, const glm::mat4 &viewMatrix, int colour, PIELIGHT brightness, int pieFlag, int pieFlagData, float heightAboveMap)
{
	if (!telRocketLoaded(psDroid)) { return false; }
	static const iIMDBaseShape *s_rocket = nullptr;
	static unsigned s_gen = 0;
	if (modelCacheStale(s_gen)) { s_rocket = modelGet(WzString::fromUtf8("modtelrocket.pie")); }
	if (s_rocket == nullptr) { return false; }
	return pie_Draw3DShape(s_rocket->displayModel(), 0, colour, brightness, pieFlag, pieFlagData, weaponMatrix, viewMatrix, -heightAboveMap);
}

bool drawTelJacks(const DROID *psDroid, const glm::mat4 &bodyMatrix, const glm::mat4 &viewMatrix, int colour, PIELIGHT brightness, int pieFlag, float heightAboveMap)
{
	if (!telStat(psDroid)) { return false; }
	float erect, jacks;
	telPose(psDroid, graphicsTime, erect, jacks);
	if (jacks <= 0.f) { return false; }
	static const iIMDBaseShape *s_cyl = nullptr, *s_pis = nullptr, *s_foot = nullptr;
	static unsigned s_gen = 0;
	if (modelCacheStale(s_gen))
	{
		s_cyl = modelGet(WzString::fromUtf8("modgdistrut_c.pie"));
		s_pis = modelGet(WzString::fromUtf8("modgdistrut_p.pie"));
		s_foot = modelGet(WzString::fromUtf8("modgdistrut_f.pie"));
	}
	if (s_cyl == nullptr || s_pis == nullptr || s_foot == nullptr) { return false; }
	// vier senkrechte Stuetzen seitlich am Rahmen (vorn hinter dem Fuehrerhaus, hinten am Heck)
	constexpr float X = 15.5f, TOP = 13.f, K = 1.5f, CYL = 8.5f;
	bool drew = false;
	for (int sx = -1; sx <= 1; sx += 2)
	{
		for (float z : { -40.f, 62.f })
		{
			const glm::mat4 S = bodyMatrix * glm::translate(glm::vec3(sx * X, TOP, z)) * glm::scale(glm::vec3(K));
			drew |= pie_Draw3DShape(s_cyl->displayModel(), 0, colour, brightness, pieFlag, 0, S, viewMatrix, -heightAboveMap);
			const float slide = (TOP / K - CYL) * jacks;
			drew |= pie_Draw3DShape(s_pis->displayModel(), 0, colour, brightness, pieFlag, 0, S * glm::translate(glm::vec3(0.f, -(1.f + slide), 0.f)), viewMatrix, -heightAboveMap);
			const glm::mat4 F = bodyMatrix * glm::translate(glm::vec3(sx * X, TOP - (CYL + slide) * K, z)) * glm::scale(glm::vec3(K));
			drew |= pie_Draw3DShape(s_foot->displayModel(), 0, colour, brightness, pieFlag, 0, F, viewMatrix, -heightAboveMap);
		}
	}
	return drew;
}

void cloudAt(const Vector3i &groundPos)
{
	startCloud(groundPos);
}

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
	tickTel();
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
	if (!s_clouds.empty()) { tickClouds(); drawClouds(viewMatrix); }
	if (!s_launchFx.empty()) { drawLaunchFx(); }
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
		const glm::mat4 m = alignY(pos, dir) * glm::scale(glm::vec3(f.scale));
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
	s_tel.clear();
	s_launchFx.clear();
	s_telWeapon = -2;
	s_missile = nullptr;
	s_flame = nullptr;
	s_modelsTried = false;
}
}
