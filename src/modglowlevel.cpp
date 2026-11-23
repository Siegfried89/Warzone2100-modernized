// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 22:54 "Glow maps"): Leucht-Ebenen in Modellen.
//  - Eine Modell-Ebene mit eigenem "TYPE 202" (additiv) und eigener Texturseite, deren Name "glowlvl" enthaelt, ist eine Leucht-Ebene
//    (Mod-Paket glowlevels.py): die leuchtenden Stellen des Modells noch einmal, hauchduenn davor, auf einer Seite, die ausser den
//    Leuchtstellen schwarz ist. Die Engine zeichnet sie additiv und unbeleuchtet - sie leuchtet auch im Schatten und nachts.
//  - "glowlvl-eye": ruhiges Leuchten mit leichtem Flackern, dazu ein kleiner Lichthof mit sehr kurzen, langsam drehenden Strahlen
//    (God-Rays wie beim Combot-Auge; effects/modeyerays.pie) an der Mitte der Ebene - die roten Kybernetik-Augen der Kollektiv-Infanterie.
//  - "glowlvl-eyecmd" (23.11.): rote Linse des Kommando-Turms - pulsiert langsam, rotes Licht, groesserer Lichthof mit Strahlen nach vorn.
//  - "glowlvl-blink" (23.11.): leuchtet ruhig und blinkt im Takt, den der Zeichner vorgibt (setBlinkAlpha) - rote Lampe der
//    Kommandeurs-Antennen (modcmdantenna.cpp).
//  - "glowlvl-pulse": glueht ab und zu auf (alle 6-13 Sekunden gut 2 Sekunden, je Gebaeude zu einem anderen Zeitpunkt) - die blauen
//    Scheiben des Indirect Weapons Lab und die langen roten Scheiben des Laser Optics Lab; dabei beleuchten sie die naehere Umgebung in
//    ihrer Farbe (Seitenname mit "blue"/"red").
// Nur Anzeige.
#include "modglowlevel.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/pielighting.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/piestate.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
int s_blinkAlpha = -1;   // vom Aufrufer gesetzter Blink-Wert fuer "glowlvl-blink" (-1: ruhig)

const iIMDShape *raysShape()
{
	static const iIMDShape *shape = nullptr;
	static unsigned gen = 0;
	if (modelCacheStale(gen))
	{
		const iIMDBaseShape *b = modelGet(WzString::fromUtf8("modeyerays.pie"));
		shape = b ? b->displayModel() : nullptr;
	}
	return shape;
}

unsigned seedOf(const glm::mat4 &model)
{
	// Lage des Objekts (grob, damit sich die Phase beim Laufen nicht staendig aendert)
	const int x = static_cast<int>(std::floor(model[3].x / 256.f)), z = static_cast<int>(std::floor(model[3].z / 256.f));
	unsigned h = static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(z) * 19349663u;
	h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
	return h;
}

int glowHook(const iIMDShape *lvl, int pieFlag, const glm::mat4 &model, const glm::mat4 &view)
{
	const std::string &tex = lvl->tilesetTextureFiles[0].texfile;
	if (tex.find("glowlvl") == std::string::npos) { return -1; }
	if (pieFlag & (pie_BUTTON | pie_TRANSLUCENT | pie_PREMULTIPLIED | pie_SHIELD | pie_HEIGHT_SCALED | pie_RAISE)) { return 0; }
	const unsigned seed = seedOf(model);
	const float ph = static_cast<float>(seed % 1000u) * 0.0063f;
	const float t = static_cast<float>(graphicsTime % 3600000u) * 0.001f;
	// (Wunsch 23.11. 04:23 "das Rot soll leuchten und ab und zu blinken"): Kommandeurs-Antennen (modcmdantenna.cpp gibt den Takt vor:
	// 255 = heller Blitz, 40 = kurz dunkel, sonst ruhiges rotes Leuchten mit leichtem Flackern)
	if (tex.find("blink") != std::string::npos)
	{
		if (s_blinkAlpha >= 0) { return s_blinkAlpha; }
		return std::clamp(static_cast<int>(150.f + 20.f * std::sin(t * 4.3f + ph) + 10.f * std::sin(t * 19.f + ph)), 0, 255);
	}
	if (tex.find("pulse") != std::string::npos)
	{
		const unsigned period = 6000u + seed % 7000u;
		const unsigned local = (graphicsTime + seed * 977u) % period;
		constexpr unsigned GLOW_MS = 2200;
		if (local >= GLOW_MS) { return 0; }
		const float x = static_cast<float>(local) / static_cast<float>(GLOW_MS);
		const float env = std::pow(std::sin(x * 3.14159265f), 1.4f);
		const float flick = 0.9f + 0.1f * std::sin(t * 23.f + ph);
		const int a = std::clamp(static_cast<int>(255.f * env * flick), 0, 255);
		// (Wunsch 23:16 "soll ausserdem einen kleinen Bereich in der naeheren Umgebung in diesem Blau beleuchten - das rote Labor auch"):
		// Punktlicht in der Farbe der Scheiben an der Mitte der Leucht-Ebene, so hell wie sie gerade glueht
		if (a > 8)
		{
			const glm::vec3 c = (glm::vec3(lvl->min) + glm::vec3(lvl->max)) * 0.5f;
			const glm::vec3 wp = glm::vec3(model * glm::vec4(c, 1.f));
			LIGHT l;
			l.position = Vector3i(static_cast<int>(wp.x), static_cast<int>(wp.y), static_cast<int>(-wp.z));
			l.range = 300;
			l.colour = tex.find("blue") != std::string::npos ? pal_Colour(80, 165, 255) : (tex.find("red") != std::string::npos ? pal_Colour(255, 70, 40) : pal_Colour(255, 240, 220));
			l.intensity = 1.5f * static_cast<float>(a) / 255.f;
			getCurrentLightingData().lights.push_back(l);
		}
		return a;
	}
	// ruhiges Leuchten (Augen): leichtes Flackern
	// (Wunsch 23.11. 04:23 "Kommandoturm 'Auge' leuchtet, pulsiert und hat Godrays"): "eyecmd" pulsiert langsam und kraeftig
	const bool cmdEye = tex.find("eyecmd") != std::string::npos;
	const float cmdPulse = 0.5f + 0.5f * std::sin(t * 2.4f);   // ohne Ortsphase: fahrende Kommandeure pulsieren gleichmaessig weiter
	int alpha = std::clamp(static_cast<int>(205.f + 35.f * std::sin(t * 6.1f + ph) + 15.f * std::sin(t * 17.3f + ph * 1.7f)), 0, 255);
	if (cmdEye)
	{
		alpha = std::clamp(static_cast<int>(120.f + 135.f * cmdPulse), 0, 255);
		if (!(pieFlag & pie_BUTTON))
		{
			const glm::vec3 c = (glm::vec3(lvl->min) + glm::vec3(lvl->max)) * 0.5f;
			const glm::vec3 wp = glm::vec3(model * glm::vec4(c, 1.f));
			LIGHT l;
			l.position = Vector3i(static_cast<int>(wp.x), static_cast<int>(wp.y), static_cast<int>(-wp.z));
			l.range = 220;
			l.colour = pal_Colour(255, 50, 35);
			l.intensity = 0.5f + 0.9f * cmdPulse;
			getCurrentLightingData().lights.push_back(l);
		}
	}
	if (tex.find("eye") != std::string::npos)
	{
		const iIMDShape *rays = raysShape();
		if (rays != nullptr)
		{
			const glm::vec3 c = (glm::vec3(lvl->min) + glm::vec3(lvl->max)) * 0.5f;
			// (Wunsch 23.11. 04:23 "Der Godrays vom Kollektiv-Soldat ... geht auch nach hinten. Er soll nur nach vorne gehen"):
			// das Gesicht schaut im Modell nach -z (Augen bei z ~ -2.8). Der Lichthof sitzt ein Stueck vor dem Auge und wird nur gezeigt,
			// wenn man das Gesicht sieht - von der Seite schwaecher, von hinten gar nicht (vorher ragten die Strahlen hinter dem Kopf hervor).
			const glm::vec3 fwdM(0.f, 0.f, -1.f);
			const glm::mat4 mv = view * model;
			const glm::vec3 ve = glm::vec3(mv * glm::vec4(c, 1.f));
			const glm::vec3 vf = glm::mat3(mv) * fwdM;
			const float lf = glm::length(vf), le = glm::length(ve);
			const float facing = (lf > 1e-4f && le > 1e-4f) ? glm::dot(vf / lf, -ve / le) : 1.f;
			const float fade = std::clamp((facing + 0.05f) / 0.55f, 0.f, 1.f);
			if (fade > 0.02f)
			{
				// Groesse nach der Leucht-Ebene: Infanterie-Auge ~1.5 breit = 1; Kommando-Linse ~6 -> Strahlen gut dreimal so lang
				const float ext = static_cast<float>(std::max(lvl->max.x - lvl->min.x, lvl->max.y - lvl->min.y));
				const float sizeK = cmdEye ? std::clamp(ext / 1.8f, 1.f, 6.f) : 1.f;
				const glm::vec3 wp = glm::vec3(model * glm::vec4(c + fwdM * (1.2f * sizeK), 1.f));
				// wie das Combot-Auge (modcombot drawEffectPiece): leichtes Pulsieren, langsam drehende Strahlen, Deckkraft 130..200
				const float k = (1.f + 0.07f * std::sin(t * 7.3f + ph) + 0.04f * std::sin(t * 17.1f + ph * 0.5f)) * (cmdEye ? (0.85f + 0.3f * cmdPulse) : 1.f);
				const float sc = glm::length(glm::vec3(model[0])) * k * sizeK;
				const glm::mat3 camRot(view);
				const glm::mat4 bb = glm::translate(glm::mat4(1.f), wp) * glm::mat4(glm::transpose(camRot))
				                     * glm::rotate(glm::mat4(1.f), t * 0.35f + ph, glm::vec3(0.f, 0.f, 1.f)) * glm::scale(glm::mat4(1.f), glm::vec3(sc));
				const float a = (cmdEye ? (110.f + 120.f * cmdPulse) : (165.f + 35.f * std::sin(t * 5.1f + ph))) * fade;
				pie_Draw3DShape(rays, 0, 0, WZCOL_WHITE, pie_ADDITIVE, std::clamp(static_cast<int>(a), 0, 255), bb, view, 0.f, true);
			}
		}
	}
	return alpha;
}

const bool s_registered = (pie_SetGlowLevelHook(&glowHook), true);
}

namespace modglowlevel
{
void setBlinkAlpha(int alpha)
{
	s_blinkAlpha = alpha;
}

/// sicherstellen, dass der Rueckruf eingetragen ist (wird beim Spielstart aufgerufen)
void init()
{
	(void)s_registered;
	pie_SetGlowLevelHook(&glowHook);
}
}
