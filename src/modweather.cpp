// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Wetter (siehe modweather.h)

#include "modweather.h"

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/sound/audio.h"

#include "atmos.h"
#include "display3d.h"
#include "map.h"
#include "modlayer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace modweather
{
namespace
{
enum Mode { M_CLEAR = 0, M_RAIN, M_THUNDER, M_SAND, M_SNOW, M_BLIZZARD, M_COUNT };

struct Def
{
	WT_CLASS part;
	AtmosStyle style;
	float intensity;
	float windX, windZ;
	float tintR, tintG, tintB, tintA;        // Vollbildfaerbung (0..1)
	float ambR, ambG, ambB;                  // Umgebungslicht-Faktor bei voller Staerke
	float difR, difG, difB;                  // Sonnenlicht-Faktor
	const char *sky;                         // Himmelstextur (nullptr = Grundhimmel)
	float skyWind;                           // Wolkenzug (Grad/s)
	bool thunder;
	int bed;                                 // 0 keine, 1 Regen, 2 Wind, 3 Sand
};

const Def kDefs[M_COUNT] = {
	// CLEAR
	{WT_NONE,    AS_NORMAL,     0.f, 0.f,    0.f,   0.f,  0.f,  0.f,  0.00f, 1.f,  1.f,  1.f,  1.f,  1.f,  1.f,  nullptr,                           0.f,  false, 0},
	// RAIN
	{WT_RAINING, AS_NORMAL,     1.0f, 60.f,  25.f,  0.30f, 0.34f, 0.40f, 0.14f, 0.86f, 0.90f, 0.98f, 0.70f, 0.74f, 0.82f, "texpages/page-436-sky-storm.png", 1.2f, false, 1},
	// THUNDER
	{WT_RAINING, AS_STORM_RAIN, 1.2f, 300.f, 130.f, 0.10f, 0.13f, 0.20f, 0.30f, 0.62f, 0.68f, 0.86f, 0.34f, 0.38f, 0.52f, "texpages/page-436-sky-storm.png", 2.6f, true,  1},
	// SAND
	{WT_SNOWING, AS_SAND,       1.0f, 1500.f, 520.f, 0.80f, 0.62f, 0.34f, 0.34f, 0.96f, 0.80f, 0.56f, 0.78f, 0.62f, 0.42f, "texpages/page-437-sky-sand.png",  3.4f, false, 3},
	// SNOW
	{WT_SNOWING, AS_NORMAL,     1.0f, 0.f,    0.f,   0.86f, 0.91f, 1.00f, 0.10f, 0.92f, 0.96f, 1.06f, 0.80f, 0.86f, 0.98f, "texpages/page-439-sky-snow.png",  1.0f, false, 0},
	// BLIZZARD
	{WT_SNOWING, AS_BLIZZARD,   1.0f, 950.f,  420.f, 0.92f, 0.95f, 1.00f, 0.40f, 0.88f, 0.92f, 1.02f, 0.56f, 0.62f, 0.80f, "texpages/page-439-sky-snow.png",  3.0f, false, 2},
};

constexpr float NIGHT_AMB[3] = {0.40f, 0.48f, 0.80f};
constexpr float NIGHT_DIF[3] = {0.20f, 0.26f, 0.52f};
const char *const NIGHT_SKY_KEY = "sky-night";
const char *const THUNDER_CLIPS[] = {"wx_thunder_0.ogg", "wx_thunder_1.ogg", "wx_thunder_2.ogg", "wx_thunder_3.ogg", "wx_thunder_4.ogg", "wx_thunder_5.ogg"};
const char *const BED_RAIN[] = {"wx_rain_0.ogg", "wx_rain_1.ogg", "wx_rain_2.ogg"};
const char *const BED_WIND[] = {"wx_wind_0.ogg", "wx_wind_1.ogg", "wx_wind_2.ogg", "wx_wind_3.ogg"};
const char *const BED_SAND[] = {"wx_sand_0.ogg", "wx_sand_1.ogg", "wx_sand_2.ogg"};
constexpr unsigned RAMP_MS = 20000;

enum Phase { P_IDLE, P_UP, P_HOLD, P_DOWN };

Mode s_mode = M_CLEAR;
Phase s_phase = P_IDLE;
unsigned s_phaseStart = 0, s_phaseLen = 0;
float s_level = 0.f;
bool s_init = false;
int s_lastBase = -1;
unsigned s_rng = 0xC0FFEE11u;
std::string s_basePage;              // Himmel, den Karte/Skript gesetzt hat
std::string s_curPage;               // aktuell aufgelegter Himmel
bool s_night = false;
bool s_wasActive = false;
bool s_layerOk = true;

// Blitz
unsigned s_nextFlash = 0;
unsigned s_flashT0[3] = {0, 0, 0};
int s_flashN = 0;
float s_flash = 0.f;
struct Thunder { unsigned at; int clip; };
std::vector<Thunder> s_thunder;
unsigned s_nextBed = 0;
std::vector<int> s_trackCache;       // [Donner 6][Regen 3][Wind 4][Sand 3]
[[maybe_unused]] unsigned s_lastWindDeg = 0;

unsigned rnd()
{
	s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
	return s_rng;
}
[[maybe_unused]] float rnd01() { return (rnd() & 0xFFFFu) / 65535.f; }
unsigned rndRange(unsigned a, unsigned b) { return a + rnd() % (b - a + 1); }

float smooth(float t) { t = std::min(1.f, std::max(0.f, t)); return t * t * (3.f - 2.f * t); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }

int trackOf(int slot, const char *name)
{
	if (s_trackCache.empty()) { s_trackCache.assign(16, -2); }
	int &t = s_trackCache[static_cast<size_t>(slot)];
	if (t == -2) { t = audio_GetTrackID(name); }
	return t;
}

void playClip(const char *const *list, int n, int slotBase)
{
	const int i = static_cast<int>(rnd() % static_cast<unsigned>(n));
	const int t = trackOf(slotBase + i, list[i]);
	if (t >= 0) { audio_PlayTrack(t); }
}

bool skyExists(const char *page)
{
	return page != nullptr && PHYSFS_exists(page) != 0;
}

/// Mode fuer die naechste Unwetterphase waehlen
Mode pickEvent()
{
	const WT_CLASS base = atmosGetBaseWeatherType();
	if (base == WT_RAINING) { return (rnd() % 100 < 55) ? M_THUNDER : M_RAIN; }
	if (base == WT_SNOWING) { return (rnd() % 100 < 50) ? M_BLIZZARD : M_SNOW; }
	const unsigned r = rnd() % 100;
	switch (currentMapTileset)
	{
	case MAP_TILESET::ARIZONA:  return r < 60 ? M_SAND : (r < 85 ? M_THUNDER : M_RAIN);
	case MAP_TILESET::URBAN:    return r < 42 ? M_RAIN : (r < 78 ? M_THUNDER : M_SAND);
	case MAP_TILESET::ROCKIES:  return r < 55 ? M_SNOW : M_BLIZZARD;
	}
	return M_RAIN;
}

void startIdle(unsigned now)
{
	s_phase = P_IDLE;
	s_phaseStart = now;
	const bool scripted = atmosGetBaseWeatherType() != WT_NONE;
	s_phaseLen = scripted ? rndRange(180000, 420000) : rndRange(200000, 560000);
}

void startEvent(unsigned now)
{
	s_mode = pickEvent();
	s_phase = P_UP;
	s_phaseStart = now;
	s_phaseLen = RAMP_MS;
}

void applySky(const Def &d, bool wantStorm)
{
	const char *target = (wantStorm && d.sky != nullptr && skyExists(d.sky)) ? d.sky : (s_basePage.empty() ? nullptr : s_basePage.c_str());
	if (target == nullptr) { return; }
	if (s_curPage == target) { return; }
	s_curPage = target;
	modSwapSkyBox(target, wantStorm ? d.skyWind : 0.f);
}

void applyLighting(float level, float flash)
{
	const Def &d = kDefs[s_mode];
	const float l = smooth(level);
	float amb[3] = {lerp(1.f, d.ambR, l), lerp(1.f, d.ambG, l), lerp(1.f, d.ambB, l)};
	float dif[3] = {lerp(1.f, d.difR, l), lerp(1.f, d.difG, l), lerp(1.f, d.difB, l)};
	if (s_night)
	{
		for (int i = 0; i < 3; i++) { amb[i] *= NIGHT_AMB[i]; dif[i] *= NIGHT_DIF[i]; }
	}
	if (flash > 0.f)
	{
		for (int i = 0; i < 3; i++) { amb[i] *= 1.f + 2.2f * flash; dif[i] *= 1.f + 3.0f * flash; }
	}
	pie_SetLightingScale(glm::vec3(amb[0], amb[1], amb[2]), glm::vec3(dif[0], dif[1], dif[2]));
}

void lightningTick(unsigned now, float level)
{
	// Donner nach Verzoegerung abspielen
	for (size_t i = 0; i < s_thunder.size();)
	{
		if (now >= s_thunder[i].at)
		{
			const int t = trackOf(s_thunder[i].clip, THUNDER_CLIPS[s_thunder[i].clip]);
			if (t >= 0) { audio_PlayTrack(t); }
			s_thunder.erase(s_thunder.begin() + static_cast<long>(i));
		}
		else { ++i; }
	}
	// Blitzwert
	s_flash = 0.f;
	for (int i = 0; i < s_flashN; i++)
	{
		if (now >= s_flashT0[i])
		{
			const float t = static_cast<float>(now - s_flashT0[i]);
			s_flash = std::max(s_flash, std::exp(-t / 95.f) * (i == 0 ? 1.0f : 0.75f));
		}
	}
	if (s_flashN > 0 && now > s_flashT0[s_flashN - 1] + 900) { s_flashN = 0; }
	if (kDefs[s_mode].thunder && level > 0.55f && s_flashN == 0 && now >= s_nextFlash)
	{
		const int strokes = 1 + static_cast<int>(rnd() % 3);
		unsigned t0 = now;
		for (int i = 0; i < strokes; i++) { s_flashT0[i] = t0; t0 += rndRange(70, 170); }
		s_flashN = strokes;
		const unsigned delay = rndRange(250, 3600);
		if (s_thunder.size() < 4) { s_thunder.push_back({now + delay, static_cast<int>(rnd() % 6)}); }
		s_nextFlash = now + rndRange(3500, 12000);
	}
}

void bedTick(unsigned now, float level)
{
	const int bed = kDefs[s_mode].bed;
	if (bed == 0 || level < 0.35f || now < s_nextBed) { return; }
	s_nextBed = now + rndRange(4600, 6400);
	if (bed == 1) { playClip(BED_RAIN, 3, 6); }
	else if (bed == 2) { playClip(BED_WIND, 4, 9); }
	else { playClip(BED_SAND, 3, 13); }
}

void deactivate()
{
	if (s_wasActive)
	{
		// Grundwetter wiederherstellen (setzt auch den aktiven Zustand zurueck)
		atmosSetWeatherType(atmosGetBaseWeatherType());
		s_wasActive = false;
	}
}
} // namespace

void reset()
{
	s_init = false;
	s_mode = M_CLEAR;
	s_phase = P_IDLE;
	s_level = 0.f;
	s_lastBase = -1;
	s_night = false;
	s_flashN = 0;
	s_flash = 0.f;
	s_thunder.clear();
	s_basePage.clear();
	s_curPage.clear();
	s_wasActive = false;
	s_nextBed = 0;
	pie_SetLightingScale(glm::vec3(1.f), glm::vec3(1.f));
}

void onSkybox(const char *page)
{
	if (page == nullptr) { return; }
	s_basePage = page;
	s_curPage = page;
	s_night = std::strstr(page, NIGHT_SKY_KEY) != nullptr;
	if (!s_night) { applyLighting(s_level, 0.f); }
}

const char *baseSkyPage(const char *current)
{
	return s_basePage.empty() ? current : s_basePage.c_str();
}

void tick()
{
	const unsigned now = realTime;
	if (!s_init)
	{
		s_init = true;
		s_rng ^= now * 2654435761u;
		startIdle(now);
		s_phaseLen = rndRange(60000, 240000);          // erste Unwetterphase nicht erst nach Ewigkeiten
	}
	const bool layerOk = (modlayer::current() == modlayer::SURFACE);
	if (!atmosGetWeatherEnabled() || !layerOk)
	{
		// Wetter aus oder Untergrund/Orbit: nichts anzeigen, Nacht-Licht bleibt (Himmel gilt nur an der Oberflaeche)
		if (s_level > 0.f || s_wasActive)
		{
			s_level = 0.f;
			deactivate();
			applySky(kDefs[M_CLEAR], false);
		}
		s_flash = 0.f;
		applyLighting(0.f, 0.f);
		s_layerOk = layerOk;
		return;
	}
	s_layerOk = true;
	if (gameTimeIsStopped()) { applyLighting(s_level, s_flash); return; }

	// Grundwetter wurde von Skript/Taste/Spielstand veraendert: neu planen
	const int base = static_cast<int>(atmosGetBaseWeatherType());
	if (base != s_lastBase)
	{
		s_lastBase = base;
		if (s_phase != P_IDLE) { s_phase = P_DOWN; s_phaseStart = now; s_phaseLen = RAMP_MS / 2; }
	}

	// Phasenablauf
	const unsigned el = now - s_phaseStart;
	switch (s_phase)
	{
	case P_IDLE:
		s_level = 0.f;
		if (el >= s_phaseLen) { startEvent(now); }
		break;
	case P_UP:
		s_level = std::min(1.f, static_cast<float>(el) / RAMP_MS);
		if (el >= RAMP_MS) { s_phase = P_HOLD; s_phaseStart = now; s_phaseLen = rndRange(100000, 260000); }
		break;
	case P_HOLD:
		s_level = 1.f;
		if (el >= s_phaseLen) { s_phase = P_DOWN; s_phaseStart = now; s_phaseLen = RAMP_MS; }
		break;
	case P_DOWN:
		s_level = 1.f - std::min(1.f, static_cast<float>(el) / std::max(1u, s_phaseLen));
		if (el >= s_phaseLen) { s_level = 0.f; startIdle(now); }
		break;
	}

	const Def &d = kDefs[s_mode];
	if (s_level > 0.02f)
	{
		atmosModSet(d.part, d.style, d.intensity * std::min(1.f, s_level * 1.3f), d.windX, d.windZ);
		s_wasActive = true;
		applySky(d, s_level > 0.4f);
	}
	else
	{
		deactivate();
		applySky(kDefs[M_CLEAR], false);
	}
	lightningTick(now, s_level);
	bedTick(now, s_level);
	applyLighting(s_level, s_flash);
}

void drawOverlay()
{
	if (!s_layerOk) { return; }
	const Def &d = kDefs[s_mode];
	const float l = smooth(s_level);
	const float w = static_cast<float>(screenWidth) * 1.5f, h = static_cast<float>(screenHeight) * 1.5f;
	float night = 0.f;
	if (s_night) { night = 0.20f; }
	if (l > 0.01f && d.tintA > 0.f)
	{
		const float a = d.tintA * l;
		pie_UniTransBoxFill(0.f, 0.f, w, h, pal_RGBA(static_cast<UBYTE>(d.tintR * 255.f), static_cast<UBYTE>(d.tintG * 255.f), static_cast<UBYTE>(d.tintB * 255.f), static_cast<UBYTE>(std::min(1.f, a) * 255.f)));
	}
	if (night > 0.f)
	{
		pie_UniTransBoxFill(0.f, 0.f, w, h, pal_RGBA(10, 16, 48, static_cast<UBYTE>(night * 255.f)));
	}
	if (s_flash > 0.02f)
	{
		pie_UniTransBoxFill(0.f, 0.f, w, h, pal_RGBA(214, 226, 255, static_cast<UBYTE>(std::min(1.f, 0.62f * s_flash) * 255.f)));
	}
}
}
