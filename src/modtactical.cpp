// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: taktische Symbole (siehe modtactical.h)
#include "modtactical.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"

#include "ai.h"
#include "component.h"
#include "display3d.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "modarty.h"
#include "modcombot.h"
#include "modlayer.h"
#include "modui.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cmath>

namespace modtactical
{
namespace
{
bool s_on = false;

enum Frame { F_GROUND, F_AIR, F_SEA };
enum Glyph { G_NONE, G_INF, G_ARMOR, G_WHEELED, G_HOVER, G_ARTY, G_AA, G_AT, G_RECON, G_ENGINEER, G_REPAIR, G_HQ, G_TRANSPORT,
             G_MECH, G_FIXEDWING, G_ROTARY, G_EW,
             // Bauwerke
             G_FACTORY, G_RESEARCH, G_POWER, G_OIL, G_DEFENSE };

struct Sym
{
	Frame frame = F_GROUND;
	Glyph glyph = G_NONE;
	int echelon = 0;            // Punkte ueber dem Rahmen
	bool hostile = false;
	bool installation = false;  // Bauwerk: Balken oben auf dem Rahmen
	PIELIGHT team = WZCOL_WHITE;
	float health = 1.f;
	bool selected = false;
};

const PIELIGHT kInk = { 14, 16, 22, 255 };   // Zeichen/Rahmen (fast schwarz)

PIELIGHT rgba(int r, int g, int b, int a) { return pal_RGBA(static_cast<UBYTE>(r), static_cast<UBYTE>(g), static_cast<UBYTE>(b), static_cast<UBYTE>(std::clamp(a, 0, 255))); }

void line(float x0, float y0, float x1, float y1, float w, PIELIGHT c)
{
	const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
	pie_DrawSoftRoundedBoxRotated((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, len + w, w, std::atan2(dy, dx), c, w * 0.5f, 0.8f);
}

void disc(float cx, float cy, float r, PIELIGHT c)
{
	pie_DrawSoftRoundedBox(cx - r, cy - r, cx + r, cy + r, c, r, 0.9f);
}

void ellipseRing(float cx, float cy, float rx, float ry, float w, PIELIGHT ink, PIELIGHT fill)
{
	pie_DrawSoftRoundedBox(cx - rx, cy - ry, cx + rx, cy + ry, ink, ry, 0.9f);
	pie_DrawSoftRoundedBox(cx - rx + w, cy - ry + w, cx + rx - w, cy + ry - w, fill, std::max(0.5f, ry - w), 0.9f);
}

// Zeichen in das Feld (x0..x1, y0..y1) - Strichstaerke lw
void drawGlyph(Glyph g, float x0, float y0, float x1, float y1, float lw, PIELIGHT fill)
{
	const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, w = x1 - x0, h = y1 - y0;
	switch (g)
	{
	case G_INF:
		line(x0, y0, x1, y1, lw, kInk); line(x0, y1, x1, y0, lw, kInk); break;
	case G_ARMOR:
		ellipseRing(cx, cy, w * 0.34f, h * 0.27f, lw, kInk, fill); break;
	case G_WHEELED:
		ellipseRing(cx, cy - h * 0.08f, w * 0.32f, h * 0.22f, lw, kInk, fill);
		disc(cx - w * 0.18f, y1 - h * 0.14f, lw * 1.05f, kInk); disc(cx + w * 0.18f, y1 - h * 0.14f, lw * 1.05f, kInk); break;
	case G_HOVER:
		ellipseRing(cx, cy - h * 0.08f, w * 0.32f, h * 0.22f, lw, kInk, fill);
		line(cx - w * 0.3f, y1 - h * 0.14f, cx + w * 0.3f, y1 - h * 0.14f, lw * 0.8f, kInk); break;
	case G_MECH:
		ellipseRing(cx, cy, w * 0.34f, h * 0.27f, lw, kInk, fill);
		line(x0, y0, x1, y1, lw, kInk); line(x0, y1, x1, y0, lw, kInk); break;
	case G_ARTY:
		disc(cx, cy, std::min(w, h) * 0.17f, kInk); break;
	case G_AA:
		// Kuppel unten im Rahmen
		ellipseRing(cx, y1, w * 0.3f, h * 0.36f, lw, kInk, fill);
		pie_DrawSoftRoundedBox(x0 + lw, y1 - lw * 0.2f, x1 - lw, y1 + h * 0.4f, fill, 0.f, 0.8f); break;
	case G_AT:
		line(x0, y1, cx, y0 + h * 0.15f, lw, kInk); line(cx, y0 + h * 0.15f, x1, y1, lw, kInk); break;
	case G_RECON:
		line(x0, y1, x1, y0, lw, kInk); break;
	case G_ENGINEER:
		line(cx - w * 0.28f, cy - h * 0.08f, cx + w * 0.28f, cy - h * 0.08f, lw, kInk);
		line(cx - w * 0.28f, cy - h * 0.08f, cx - w * 0.28f, cy + h * 0.22f, lw, kInk);
		line(cx, cy - h * 0.08f, cx, cy + h * 0.22f, lw, kInk);
		line(cx + w * 0.28f, cy - h * 0.08f, cx + w * 0.28f, cy + h * 0.22f, lw, kInk); break;
	case G_REPAIR:
		line(cx - w * 0.22f, cy, cx + w * 0.22f, cy, lw, kInk);
		ellipseRing(cx - w * 0.26f, cy, h * 0.17f, h * 0.17f, lw * 0.8f, kInk, fill);
		ellipseRing(cx + w * 0.26f, cy, h * 0.17f, h * 0.17f, lw * 0.8f, kInk, fill); break;
	case G_HQ:
		line(x0 + w * 0.3f, y1 - h * 0.12f, x0 + w * 0.3f, y0 + h * 0.15f, lw, kInk);
		pie_DrawSoftRoundedBox(x0 + w * 0.3f, y0 + h * 0.15f, x0 + w * 0.68f, y0 + h * 0.48f, kInk, 0.f, 0.8f); break;
	case G_TRANSPORT:
		ellipseRing(cx, cy, h * 0.3f, h * 0.3f, lw, kInk, fill);
		line(cx - h * 0.3f, cy, cx + h * 0.3f, cy, lw * 0.7f, kInk); line(cx, cy - h * 0.3f, cx, cy + h * 0.3f, lw * 0.7f, kInk); break;
	case G_FIXEDWING:
		line(cx - w * 0.3f, cy, cx + w * 0.3f, cy, lw, kInk);
		line(cx, cy - h * 0.25f, cx, cy + h * 0.3f, lw, kInk);
		line(cx - w * 0.12f, cy + h * 0.3f, cx + w * 0.12f, cy + h * 0.3f, lw * 0.8f, kInk); break;
	case G_ROTARY:
		line(cx - w * 0.3f, cy - h * 0.18f, cx + w * 0.3f, cy + h * 0.18f, lw, kInk);
		line(cx - w * 0.3f, cy + h * 0.18f, cx + w * 0.3f, cy - h * 0.18f, lw, kInk);
		line(cx - w * 0.3f, cy - h * 0.18f, cx - w * 0.3f, cy + h * 0.18f, lw, kInk);
		line(cx + w * 0.3f, cy - h * 0.18f, cx + w * 0.3f, cy + h * 0.18f, lw, kInk); break;
	case G_EW:
		line(cx - w * 0.3f, cy + h * 0.1f, cx - w * 0.05f, cy - h * 0.2f, lw, kInk);
		line(cx - w * 0.05f, cy - h * 0.2f, cx + w * 0.05f, cy + h * 0.2f, lw, kInk);
		line(cx + w * 0.05f, cy + h * 0.2f, cx + w * 0.3f, cy - h * 0.1f, lw, kInk); break;
	case G_FACTORY:
		disc(cx, cy, h * 0.28f, kInk); disc(cx, cy, h * 0.13f, fill);
		for (int i = 0; i < 4; ++i) { const float a = static_cast<float>(i) * 0.785398f; line(cx - std::cos(a) * h * 0.36f, cy - std::sin(a) * h * 0.36f, cx + std::cos(a) * h * 0.36f, cy + std::sin(a) * h * 0.36f, lw * 0.9f, kInk); }
		disc(cx, cy, h * 0.13f, fill); break;
	case G_RESEARCH:
		line(cx - w * 0.08f, y0 + h * 0.18f, cx - w * 0.08f, cy, lw, kInk); line(cx + w * 0.08f, y0 + h * 0.18f, cx + w * 0.08f, cy, lw, kInk);
		line(cx - w * 0.08f, cy, cx - w * 0.22f, y1 - h * 0.15f, lw, kInk); line(cx + w * 0.08f, cy, cx + w * 0.22f, y1 - h * 0.15f, lw, kInk);
		line(cx - w * 0.22f, y1 - h * 0.15f, cx + w * 0.22f, y1 - h * 0.15f, lw, kInk); break;
	case G_POWER:
		line(cx + w * 0.08f, y0 + h * 0.12f, cx - w * 0.1f, cy + h * 0.02f, lw, kInk);
		line(cx - w * 0.1f, cy + h * 0.02f, cx + w * 0.1f, cy - h * 0.02f, lw, kInk);
		line(cx + w * 0.1f, cy - h * 0.02f, cx - w * 0.08f, y1 - h * 0.12f, lw, kInk); break;
	case G_OIL:
		disc(cx, cy + h * 0.08f, h * 0.2f, kInk);
		line(cx, y0 + h * 0.15f, cx - h * 0.18f, cy + h * 0.05f, lw, kInk); line(cx, y0 + h * 0.15f, cx + h * 0.18f, cy + h * 0.05f, lw, kInk); break;
	case G_DEFENSE:
		pie_DrawSoftRoundedBox(cx - w * 0.16f, cy - h * 0.05f, cx + w * 0.16f, y1 - h * 0.15f, kInk, 0.f, 0.8f);
		line(cx, cy - h * 0.05f, cx, y0 + h * 0.15f, lw, kInk); break;
	default:
		break;
	}
}

void drawSym(const Sym &s, float cx, float cy, float scale)
{
	const float sc = modui::uiScale() * scale;
	const PIELIGHT fill = s.hostile ? rgba(255, 128, 128, 255) : rgba(128, 224, 255, 255);
	const PIELIGHT fillHi = s.hostile ? rgba(255, 178, 170, 255) : rgba(190, 240, 255, 255);
	const float lw = std::max(1.2f, 1.5f * sc);
	float gx0, gy0, gx1, gy1;   // Feld fuer das Zeichen
	float top, bottom, halfW;   // Umriss fuer Groessenpunkte/Balken
	if (s.selected)
	{
		pie_DrawSoftRoundedBox(cx - 22.f * sc, cy - 17.f * sc, cx + 22.f * sc, cy + 17.f * sc, rgba(255, 255, 255, 120), 14.f * sc, 9.f * sc);
	}
	// weicher Schatten (hebt das Symbol vom Gelaende ab)
	pie_DrawSoftRoundedBox(cx - 18.f * sc, cy - 13.f * sc, cx + 18.f * sc, cy + 14.f * sc, rgba(0, 0, 0, 90), 8.f * sc, 6.f * sc);
	if (s.hostile)
	{
		// Raute
		const float d = 21.f * sc;
		pie_DrawSoftRoundedBoxRotated(cx, cy, d, d, 0.785398f, s.selected ? WZCOL_WHITE : kInk, 1.2f, 0.9f);
		pie_DrawSoftRoundedBoxRotated(cx, cy, d - 2.f * lw, d - 2.f * lw, 0.785398f, fill, 0.8f, 0.9f);
		pie_DrawSoftRoundedBoxRotated(cx - d * 0.18f, cy - d * 0.18f, d * 0.55f, d * 0.12f, 0.785398f - 1.5708f, rgba(255, 255, 255, 60), d * 0.06f, 2.f);
		const float k = d * 0.36f;
		gx0 = cx - k; gx1 = cx + k; gy0 = cy - k * 0.72f; gy1 = cy + k * 0.72f;
		top = cy - d * 0.72f; bottom = cy + d * 0.72f; halfW = d * 0.6f;
		// Teamfarbe: kleiner Strich in der unteren Spitze
		line(cx - d * 0.12f, cy + d * 0.47f, cx + d * 0.12f, cy + d * 0.47f, 2.f * sc, s.team);
	}
	else
	{
		const float w = (s.frame == F_SEA ? 19.f : 26.f) * sc, h = (s.frame == F_SEA ? 19.f : 17.f) * sc;
		const float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f, y0 = cy - h * 0.5f, y1 = cy + h * 0.5f;
		glm::vec4 radii(1.5f * sc);
		if (s.frame == F_AIR) { radii = glm::vec4(w * 0.5f, w * 0.5f, 1.5f * sc, 1.5f * sc); }
		if (s.frame == F_SEA) { radii = glm::vec4(w * 0.5f); }
		pie_DrawGradientRoundedBox(x0, y0, x1, y1, fillHi, fill, false, 1.4f, radii, s.selected ? WZCOL_WHITE : kInk, lw);
		// Teamfarbe: schmaler Streifen unten im Rahmen
		pie_DrawSoftRoundedBox(x0 + lw, y1 - lw - 2.2f * sc, x1 - lw, y1 - lw, s.team, 0.f, 0.8f);
		gx0 = x0 + lw * 1.6f; gx1 = x1 - lw * 1.6f; gy0 = y0 + lw * 1.4f; gy1 = y1 - lw - 2.6f * sc;
		if (s.frame == F_AIR) { gy0 = y0 + h * 0.3f; }
		top = y0; bottom = y1; halfW = w * 0.5f;
	}
	drawGlyph(s.glyph, gx0, gy0, gx1, gy1, lw, fill);
	if (s.installation)
	{
		pie_DrawSoftRoundedBox(cx - 4.f * sc, top - 3.5f * sc, cx + 4.f * sc, top + 0.5f * sc, kInk, 0.f, 0.8f);
	}
	// Groesse: Punkte ueber dem Rahmen
	for (int i = 0; i < s.echelon; ++i)
	{
		const float x = cx + (static_cast<float>(i) - static_cast<float>(s.echelon - 1) * 0.5f) * 5.f * sc;
		disc(x, top - (s.installation ? 7.f : 4.f) * sc, 1.7f * sc, kInk);
	}
	// Zustandsbalken (gruen / gelb / rot)
	if (s.health < 0.999f || s.selected)
	{
		const float bw = halfW * 2.f, bx = cx - halfW, by = bottom + 2.5f * sc;
		const float f = std::clamp(s.health, 0.f, 1.f);
		const PIELIGHT hc = f > 0.66f ? rgba(60, 220, 70, 255) : (f > 0.33f ? rgba(250, 210, 40, 255) : rgba(240, 60, 50, 255));
		pie_DrawSoftRoundedBox(bx - 1.f, by - 1.f, bx + bw + 1.f, by + 3.5f * sc + 1.f, rgba(0, 0, 0, 170), 1.f, 0.8f);
		pie_DrawSoftRoundedBox(bx, by, bx + bw * f, by + 3.5f * sc, hc, 0.5f, 0.8f);
	}
}

Glyph weaponGlyph(const DROID *d)
{
	if (d->numWeaps == 0 || d->asWeaps[0].nStat == 0) { return G_NONE; }
	const WEAPON_STATS *w = d->getWeaponStats(0);
	if (w == nullptr) { return G_NONE; }
	if (w->movementModel == MM_INDIRECT || w->movementModel == MM_HOMINGINDIRECT || modarty::isArtyWeapon(d->asWeaps[0].nStat)) { return G_ARTY; }
	if ((w->surfaceToAir & SHOOT_IN_AIR) && !(w->surfaceToAir & SHOOT_ON_GROUND)) { return G_AA; }
	if (w->weaponEffect == WE_ANTI_TANK && (w->weaponSubClass == WSC_ROCKET || w->weaponSubClass == WSC_MISSILE || w->weaponSubClass == WSC_SLOWROCKET)) { return G_AT; }
	return G_NONE;
}

Sym classify(const DROID *d)
{
	Sym s;
	s.hostile = selectedPlayer < MAX_PLAYERS && d->player != selectedPlayer && !aiCheckAlliances(d->player, selectedPlayer);
	s.team = pal_GetTeamColour(getPlayerColour(d->player));
	s.health = static_cast<float>(d->body) / static_cast<float>(std::max(1u, d->originalBody));
	s.selected = d->selected;
	const PROPULSION_STATS *p = d->getPropulsionStats();
	const BODY_STATS *b = d->getBodyStats();
	const bool air = d->isVtol() || modlayer::isFlier(d) || (p != nullptr && p->propulsionType == PROPULSION_TYPE_LIFT);
	const bool sea = p != nullptr && p->propulsionType == PROPULSION_TYPE_PROPELLOR;
	s.frame = air ? F_AIR : (sea ? F_SEA : F_GROUND);
	if (b != nullptr) { s.echelon = b->size == SIZE_LIGHT ? 1 : (b->size == SIZE_MEDIUM ? 2 : 3); }
	switch (d->droidType)
	{
	case DROID_PERSON: case DROID_CYBORG: case DROID_CYBORG_SUPER: s.glyph = G_INF; s.echelon = d->droidType == DROID_CYBORG_SUPER ? 2 : 1; break;
	case DROID_CONSTRUCT: case DROID_CYBORG_CONSTRUCT: s.glyph = G_ENGINEER; break;
	case DROID_REPAIR: case DROID_CYBORG_REPAIR: s.glyph = G_REPAIR; break;
	case DROID_SENSOR: s.glyph = G_RECON; break;
	case DROID_ECM: s.glyph = G_EW; break;
	case DROID_COMMAND: s.glyph = G_HQ; break;
	case DROID_TRANSPORTER: case DROID_SUPERTRANSPORTER: s.glyph = G_TRANSPORT; s.frame = F_AIR; break;
	default:
	{
		if (modcombot::isModular(d)) { s.glyph = G_MECH; break; }
		const Glyph wg = weaponGlyph(d);
		if (wg != G_NONE && wg != G_AT) { s.glyph = wg; break; }
		if (air) { s.glyph = (p != nullptr && p->id.toStdString().find("eli") != std::string::npos) ? G_ROTARY : G_FIXEDWING; break; }
		if (wg == G_AT) { s.glyph = G_AT; break; }
		if (p == nullptr) { s.glyph = G_ARMOR; break; }
		switch (p->propulsionType)
		{
		case PROPULSION_TYPE_WHEELED: case PROPULSION_TYPE_HALF_TRACKED: s.glyph = G_WHEELED; break;
		case PROPULSION_TYPE_HOVER: case PROPULSION_TYPE_PROPELLOR: s.glyph = G_HOVER; break;
		case PROPULSION_TYPE_LEGGED: s.glyph = G_MECH; break;
		default: s.glyph = G_ARMOR; break;
		}
		break;
	}
	}
	return s;
}
}

bool enabled() { return s_on; }
void toggle() { s_on = !s_on; }

bool active()
{
	// MOD (Wunsch 19.11. 20:27): solange die Alt-Taste gedrueckt ist, zeigt das Spiel die taktischen Symbole - unabhaengig vom Knopf
	// "Symbols" und von der Zoomstufe; beim Loslassen wieder wie eingestellt (nur Anzeige)
	if (keyDown(KEY_LALT) || keyDown(KEY_RALT)) { return true; }
	return s_on && getViewDistance() >= SYMBOL_DIST;
}

void drawDroidSymbol(const DROID *psDroid, float cx, float cy, float scale)
{
	if (psDroid == nullptr) { return; }
	drawSym(classify(psDroid), cx, cy, scale);
}

void drawStructureSymbol(const STRUCTURE *psStruct, float cx, float cy, float scale)
{
	if (psStruct == nullptr || psStruct->pStructureType == nullptr) { return; }
	Sym s;
	s.installation = true;
	s.hostile = selectedPlayer < MAX_PLAYERS && psStruct->player != selectedPlayer && !aiCheckAlliances(psStruct->player, selectedPlayer);
	s.team = pal_GetTeamColour(getPlayerColour(psStruct->player));
	s.health = static_cast<float>(psStruct->body) / static_cast<float>(std::max(1u, psStruct->structureBody()));
	s.selected = psStruct->selected;
	switch (psStruct->pStructureType->type)
	{
	case REF_FACTORY: case REF_CYBORG_FACTORY: case REF_VTOL_FACTORY: s.glyph = G_FACTORY; break;
	case REF_RESEARCH: s.glyph = G_RESEARCH; break;
	case REF_POWER_GEN: s.glyph = G_POWER; break;
	case REF_RESOURCE_EXTRACTOR: s.glyph = G_OIL; break;
	case REF_HQ: case REF_COMMAND_CONTROL: s.glyph = G_HQ; break;
	case REF_REPAIR_FACILITY: s.glyph = G_REPAIR; break;
	case REF_DEFENSE: case REF_FORTRESS: s.glyph = G_DEFENSE; break;
	default: s.glyph = G_NONE; break;
	}
	drawSym(s, cx, cy, scale);
}

void drawOverlay(const glm::mat4 &m)
{
	if (!active() || selectedPlayer >= MAX_PLAYERS) { return; }
	const modlayer::Layer view = modlayer::current();
	// kleinere Symbole, je weiter herausgezoomt
	const float scale = std::clamp(3600.f / std::max(1.f, getViewDistance()), 0.75f, 1.1f);
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const DROID *d : gameWorld.objects.droids[p])
		{
			if (d == nullptr || d->died || d->visibleForLocalDisplay() != UBYTE_MAX || modlayer::layerOf(d) != view) { continue; }
			if (d->sDisplay.frameNumber + 1 < frameGetFrameNumber()) { continue; }   // nicht im Bild (Sichtpruefung von displayComponentObject)
			Vector2i sp(0, 0);
			const Vector3i pos(d->pos.x, d->pos.z + 12, -d->pos.y);
			if (pie_RotateProjectWithPerspective(&pos, m, &sp) <= 0) { continue; }
			drawDroidSymbol(d, static_cast<float>(sp.x), static_cast<float>(sp.y), scale);
		}
	}
}
}
