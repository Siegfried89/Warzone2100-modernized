// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: strategische Karte / geteilter Bildschirm wie in Supreme Commander (siehe modbigmap.h)
#include "modbigmap.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/gfx_api.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "lib/widget/button.h"
#include "lib/widget/form.h"
#include "lib/widget/widget.h"

#include "ai.h"
#include "component.h"
#include "display.h"
#include "display3d.h"
#include "feature.h"
#include "featuredef.h"
#include "droid.h"
#include "game_world.h"
#include "hci.h"
#include "map.h"
#include "modlayer.h"
#include "modpaint.h"
#include "modtactical.h"
#include "modui.h"
#include "order.h"
#include "radar.h"
#include "structure.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace modbigmap
{
namespace
{
int s_mode = 0;                  // 0 zu, 1 ganzer Bildschirm, 2 geteilt (Karte links)
float s_cx = 0.f, s_cy = 0.f;    // Weltmitte der Kartenansicht
float s_wpp = 16.f;              // Welteinheiten je Bildpunkt
float s_fit = 16.f;
// (Wunsch 17.11. 19:42) Anzeigearten wie in Supreme Commander: Gelaende, kartografisch (Papierkarte mit Hoehenlinien), Hoehen
// (Hoehenschichtfarben), strategisch (dunkel, nur Relief) - dazu die Einblendung der Rohstoffe (Oelquellen)
enum View { V_TERRAIN = 0, V_CARTO, V_ELEV, V_STRAT, V_COUNT };
int s_view = V_TERRAIN;
bool s_res = true;
int s_texView = -1;
GFX *s_gfx = nullptr;
iV_Image s_img;
int s_texW = 0, s_texH = 0;
unsigned s_texAt = 0;
// Maus
bool s_lDown = false, s_dragSel = false, s_mDown = false;
Vector2i s_lStart(0, 0), s_mLast(0, 0);
unsigned s_lastClick = 0;
Vector2i s_lastClickPos(0, 0);
// Knoepfe
std::shared_ptr<W_SCREEN> s_screen;
std::vector<std::shared_ptr<W_BUTTON>> s_btn;
enum Btn { B_MAP = 0, B_SYM, B_SPLIT, B_CLOSE, B_VIEW, B_RES, B_COUNT };

struct Rect { float x0, y0, x1, y1; };

Rect panel()
{
	const float W = static_cast<float>(pie_GetVideoBufferWidth()), H = static_cast<float>(pie_GetVideoBufferHeight());
	return s_mode == 2 ? Rect{ 0.f, 0.f, std::floor(W * 0.5f), H } : Rect{ 0.f, 0.f, W, H };
}

float barH() { return 26.f * modui::uiScale(); }

Rect mapArea()
{
	Rect r = panel();
	r.y0 += barH();
	return r;
}

float worldW() { return static_cast<float>(world_coord(gameWorld.map.width)); }
float worldH() { return static_cast<float>(world_coord(gameWorld.map.height)); }

glm::vec2 toScreen(float wx, float wy)
{
	const Rect a = mapArea();
	return glm::vec2((a.x0 + a.x1) * 0.5f + (wx - s_cx) / s_wpp, (a.y0 + a.y1) * 0.5f + (wy - s_cy) / s_wpp);
}

glm::vec2 toWorld(float sx, float sy)
{
	const Rect a = mapArea();
	return glm::vec2(s_cx + (sx - (a.x0 + a.x1) * 0.5f) * s_wpp, s_cy + (sy - (a.y0 + a.y1) * 0.5f) * s_wpp);
}

bool inRect(const Rect &r, float x, float y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

void fit()
{
	const Rect a = mapArea();
	s_fit = std::max(worldW() / std::max(1.f, a.x1 - a.x0), worldH() / std::max(1.f, a.y1 - a.y0)) * 1.04f;
	s_wpp = s_fit;
	s_cx = worldW() * 0.5f; s_cy = worldH() * 0.5f;
}

void clampView()
{
	s_wpp = std::clamp(s_wpp, s_fit * 0.06f, s_fit * 1.3f);
	s_cx = std::clamp(s_cx, 0.f, worldW());
	s_cy = std::clamp(s_cy, 0.f, worldH());
}

// ---------------------------------------------------------------- Gelaendebild (Kachelfarben, Hoehenschattierung, Nebel)
void rebuildTexture()
{
	const int mw = gameWorld.map.width, mh = gameWorld.map.height;
	if (mw <= 0 || mh <= 0) { return; }
	const int tw = mw * 2, th = mh * 2;   // doppelt aufgeloest + geglaettet
	if (s_gfx == nullptr || tw != s_texW || th != s_texH)
	{
		if (s_gfx == nullptr) { s_gfx = new GFX(GFX_TEXTURE, 2); }
		s_gfx->makeTexture(static_cast<size_t>(tw), static_cast<size_t>(th), gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8, "mem::modbigmap");
		s_img.clear();
		s_img.allocate(static_cast<unsigned>(tw), static_cast<unsigned>(th), 4, true);
		s_texW = tw; s_texH = th;
	}
	std::vector<float> rgb(static_cast<size_t>(mw) * mh * 3, 0.f);
	std::vector<float> hgt(static_cast<size_t>(mw) * mh, 0.f);
	std::vector<unsigned char> wat(static_cast<size_t>(mw) * mh, 0), see(static_cast<size_t>(mw) * mh, 0);
	float hMin = 1e9f, hMax = -1e9f;
	for (int y = 0; y < mh; ++y)
	{
		for (int x = 0; x < mw; ++x)
		{
			const MAPTILE *t = mapTile(gameWorld.map, x, y);
			const size_t i = static_cast<size_t>(y) * mw + x;
			hgt[i] = static_cast<float>(t->height);
			wat[i] = terrainType(t) == TER_WATER ? 1 : 0;
			see[i] = (selectedPlayer >= MAX_PLAYERS || TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(const_cast<MAPTILE *>(t))) ? 1 : 0;
			hMin = std::min(hMin, hgt[i]); hMax = std::max(hMax, hgt[i]);
		}
	}
	const float hRange = std::max(1.f, hMax - hMin);
	for (int y = 0; y < mh; ++y)
	{
		for (int x = 0; x < mw; ++x)
		{
			const size_t i = static_cast<size_t>(y) * mw + x;
			MAPTILE *t = mapTile(gameWorld.map, x, y);
			// Licht von Nordwest: Hoehenunterschied zum Nachbarn
			const float hn = hgt[static_cast<size_t>(std::max(0, y - 1)) * mw + std::max(0, x - 1)];
			const float shade = std::clamp(1.f + (hgt[i] - hn) / 260.f, 0.55f, 1.45f);
			const bool seen = see[i] != 0;
			const float e = (hgt[i] - hMin) / hRange;   // 0 tief .. 1 hoch
			float *o = &rgb[i * 3];
			switch (s_view)
			{
			case V_CARTO:
			{
				// Papierkarte: helles Beige, mit der Hoehe etwas dunkler/braeunlicher, Wasser hellblau (Hoehenlinien kommen unten dazu)
				const float sh = 1.f + (shade - 1.f) * 0.45f;
				o[0] = (236.f - 30.f * e) * sh; o[1] = (226.f - 42.f * e) * sh; o[2] = (196.f - 58.f * e) * sh;
				if (wat[i]) { o[0] = 168.f; o[1] = 206.f; o[2] = 232.f; }
				if (!seen) { o[0] *= 0.55f; o[1] *= 0.55f; o[2] *= 0.6f; }
				break;
			}
			case V_ELEV:
			{
				// Hoehenschichten: dunkelgruen - hellgruen - gelb - braun - grau/weiss
				static const float stops[6][3] = { { 40, 110, 60 }, { 110, 170, 80 }, { 215, 205, 110 }, { 175, 120, 70 }, { 135, 110, 100 }, { 235, 235, 235 } };
				const float f = std::clamp(e, 0.f, 1.f) * 5.f;
				const int k = std::min(4, static_cast<int>(f));
				const float a = f - static_cast<float>(k);
				for (int c = 0; c < 3; ++c) { o[c] = (stops[k][c] * (1.f - a) + stops[k + 1][c] * a) * shade; }
				if (wat[i]) { o[0] = 40.f; o[1] = 90.f; o[2] = 170.f; }
				if (!seen) { o[0] *= 0.35f; o[1] *= 0.35f; o[2] *= 0.4f; }
				break;
			}
			case V_STRAT:
			{
				// strategisch: tiefes Blau, nur das Relief, Wasser etwas heller
				const float sh = std::clamp(shade, 0.7f, 1.3f);
				o[0] = 14.f * sh + 10.f * e; o[1] = 24.f * sh + 14.f * e; o[2] = 44.f * sh + 18.f * e;
				if (wat[i]) { o[0] = 10.f; o[1] = 36.f; o[2] = 70.f; }
				if (!seen) { o[0] *= 0.5f; o[1] *= 0.5f; o[2] *= 0.5f; }
				break;
			}
			default:
			{
				const PIELIGHT c = radarTileColour(TileNumber_tile(t->texture));
				// leicht entsaettigt und abgedunkelt (strategische Karte), Unerforschtes fast schwarz
				const float lum = (c.byte.r * 0.3f + c.byte.g * 0.59f + c.byte.b * 0.11f);
				const float k = (seen ? 0.78f : 0.16f) * shade;
				o[0] = (c.byte.r * 0.7f + lum * 0.3f) * k;
				o[1] = (c.byte.g * 0.7f + lum * 0.3f) * k;
				o[2] = (c.byte.b * 0.7f + lum * 0.3f) * k + (seen ? 6.f : 3.f);
				if (wat[i]) { o[0] = 30.f * k; o[1] = 70.f * k; o[2] = 120.f * k + 10.f; }
				break;
			}
			}
		}
	}
	// Hoehe je Texel (fuer Hoehenlinien)
	std::vector<float> th2(static_cast<size_t>(tw) * th, 0.f);
	unsigned char *dst = s_img.bmp_w();
	for (int y = 0; y < th; ++y)
	{
		for (int x = 0; x < tw; ++x)
		{
			// bilinear zwischen den Kachelmitten
			const float fx = (static_cast<float>(x) + 0.5f) * 0.5f - 0.5f, fy = (static_cast<float>(y) + 0.5f) * 0.5f - 0.5f;
			const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, mw - 1), y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, mh - 1);
			const int x1 = std::min(mw - 1, x0 + 1), y1 = std::min(mh - 1, y0 + 1);
			const float ax = std::clamp(fx - static_cast<float>(x0), 0.f, 1.f), ay = std::clamp(fy - static_cast<float>(y0), 0.f, 1.f);
			unsigned char *p = dst + (static_cast<size_t>(y) * tw + x) * 4;
			for (int ch = 0; ch < 3; ++ch)
			{
				const float a = rgb[(static_cast<size_t>(y0) * mw + x0) * 3 + ch] * (1 - ax) + rgb[(static_cast<size_t>(y0) * mw + x1) * 3 + ch] * ax;
				const float b = rgb[(static_cast<size_t>(y1) * mw + x0) * 3 + ch] * (1 - ax) + rgb[(static_cast<size_t>(y1) * mw + x1) * 3 + ch] * ax;
				p[ch] = static_cast<unsigned char>(std::clamp(a * (1 - ay) + b * ay, 0.f, 255.f));
			}
			p[3] = 255;
			const float ha = hgt[static_cast<size_t>(y0) * mw + x0] * (1 - ax) + hgt[static_cast<size_t>(y0) * mw + x1] * ax;
			const float hb = hgt[static_cast<size_t>(y1) * mw + x0] * (1 - ax) + hgt[static_cast<size_t>(y1) * mw + x1] * ax;
			th2[static_cast<size_t>(y) * tw + x] = ha * (1 - ay) + hb * ay;
		}
	}
	// Hoehenlinien (kartografisch: braun, jede fuenfte kraeftiger; Hoehen/strategisch: zart)
	if (s_view != V_TERRAIN)
	{
		const float iv = std::max(12.f, hRange / 14.f);
		for (int y = 0; y + 1 < th; ++y)
		{
			for (int x = 0; x + 1 < tw; ++x)
			{
				const float h0 = th2[static_cast<size_t>(y) * tw + x];
				const int c0 = static_cast<int>(std::floor((h0 - hMin) / iv));
				const int cr = static_cast<int>(std::floor((th2[static_cast<size_t>(y) * tw + x + 1] - hMin) / iv));
				const int cd = static_cast<int>(std::floor((th2[static_cast<size_t>(y + 1) * tw + x] - hMin) / iv));
				if (c0 == cr && c0 == cd) { continue; }
				const bool index = (std::max({ c0, cr, cd }) % 5) == 0;
				unsigned char *p = dst + (static_cast<size_t>(y) * tw + x) * 4;
				if (wat[static_cast<size_t>(std::min(mh - 1, y / 2)) * mw + std::min(mw - 1, x / 2)]) { continue; }
				if (s_view == V_CARTO)
				{
					const float k = index ? 0.55f : 0.75f;
					p[0] = static_cast<unsigned char>(p[0] * k * 0.95f); p[1] = static_cast<unsigned char>(p[1] * k * 0.8f); p[2] = static_cast<unsigned char>(p[2] * k * 0.65f);
				}
				else if (s_view == V_STRAT)
				{
					const int add = index ? 46 : 24;
					p[0] = static_cast<unsigned char>(std::min(255, p[0] + add / 2)); p[1] = static_cast<unsigned char>(std::min(255, p[1] + add)); p[2] = static_cast<unsigned char>(std::min(255, p[2] + add + 10));
				}
				else
				{
					const float k = index ? 0.6f : 0.82f;
					p[0] = static_cast<unsigned char>(p[0] * k); p[1] = static_cast<unsigned char>(p[1] * k); p[2] = static_cast<unsigned char>(p[2] * k);
				}
			}
		}
	}
	s_gfx->updateTexture(s_img);
	s_texAt = realTime;
	s_texView = s_view;
}

void drawTerrain()
{
	if (s_gfx == nullptr) { return; }
	const Rect a = mapArea();
	// sichtbarer Ausschnitt der Karte (Schnitt aus Kartenflaeche und Anzeigebereich), Texturkoordinaten passend
	const glm::vec2 m0 = toScreen(0.f, 0.f), m1 = toScreen(worldW(), worldH());
	const float x0 = std::max(a.x0, m0.x), y0 = std::max(a.y0, m0.y), x1 = std::min(a.x1, m1.x), y1 = std::min(a.y1, m1.y);
	if (x1 <= x0 || y1 <= y0) { return; }
	const float u0 = (x0 - m0.x) / (m1.x - m0.x), u1 = (x1 - m0.x) / (m1.x - m0.x);
	const float v0 = (y0 - m0.y) / (m1.y - m0.y), v1 = (y1 - m0.y) / (m1.y - m0.y);
	gfx_api::gfxFloat tc[] = { u0, v0, u1, v0, u0, v1, u1, v1 };
	gfx_api::gfxFloat vx[] = { x0, y0, x1, y0, x0, y1, x1, y1 };
	s_gfx->buffers(4, vx, tc);
	const glm::mat4 mvp = glm::ortho(0.f, static_cast<float>(pie_GetVideoBufferWidth()), static_cast<float>(pie_GetVideoBufferHeight()), 0.f);
	gfx_api::RadarPSO::get().bind();
	gfx_api::RadarPSO::get().bind_constants({ mvp, glm::vec2(0), glm::vec2(0), glm::vec4(1), 0 });
	s_gfx->draw<gfx_api::RadarPSO>(mvp);
}

void line(const glm::vec2 &a, const glm::vec2 &b, float w, PIELIGHT c)
{
	const glm::vec2 d = b - a;
	const float len = glm::length(d);
	if (len < 0.01f) { return; }
	pie_DrawSoftRoundedBoxRotated((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, len + w, w, std::atan2(d.y, d.x), c, w * 0.5f, 0.9f);
}

bool visibleObj(const BASE_OBJECT *o)
{
	if (o == nullptr || o->died || selectedPlayer >= MAX_PLAYERS) { return false; }
	if (o->player == selectedPlayer) { return true; }
	return o->visible[selectedPlayer] == UBYTE_MAX;
}

float symbolScale() { return std::clamp(1.15f - s_wpp / 36.f, 0.5f, 1.0f); }

DROID *droidAt(float sx, float sy, bool ownOnly, bool enemyOnly)
{
	const float pick = 13.f * modui::uiScale();
	DROID *best = nullptr;
	float bestD = pick * pick;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		if (ownOnly && p != selectedPlayer) { continue; }
		if (enemyOnly && (p == selectedPlayer || (selectedPlayer < MAX_PLAYERS && aiCheckAlliances(p, selectedPlayer)))) { continue; }
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (!visibleObj(d) || modlayer::layerOf(d) != modlayer::current()) { continue; }
			const glm::vec2 s = toScreen(static_cast<float>(d->pos.x), static_cast<float>(d->pos.y));
			const float dd = (s.x - sx) * (s.x - sx) + (s.y - sy) * (s.y - sy);
			if (dd < bestD) { bestD = dd; best = d; }
		}
	}
	return best;
}

STRUCTURE *enemyStructAt(float sx, float sy)
{
	const glm::vec2 w = toWorld(sx, sy);
	const int tx = map_coord(static_cast<int>(w.x)), ty = map_coord(static_cast<int>(w.y));
	if (tx < 0 || ty < 0 || tx >= gameWorld.map.width || ty >= gameWorld.map.height) { return nullptr; }
	BASE_OBJECT *o = mapTile(gameWorld.map, tx, ty)->psObject;
	if (o == nullptr || o->type != OBJ_STRUCTURE || !visibleObj(o) || selectedPlayer >= MAX_PLAYERS) { return nullptr; }
	if (o->player == selectedPlayer || aiCheckAlliances(o->player, selectedPlayer)) { return nullptr; }
	return static_cast<STRUCTURE *>(o);
}

// ---------------------------------------------------------------- Knoepfe
const char *btnLabel(int b)
{
	switch (b)
	{
	case B_MAP: return s_mode != 0 ? "Map: on" : "Map";
	case B_SYM: return modtactical::enabled() ? "Symbols: on" : "Symbols";
	case B_SPLIT: return s_mode == 2 ? "Full" : "Split";
	case B_CLOSE: return "Close";
	case B_VIEW:
		switch (s_view) { case V_CARTO: return "View: Cartographic"; case V_ELEV: return "View: Elevation"; case V_STRAT: return "View: Strategic"; default: return "View: Terrain"; }
	case B_RES: return s_res ? "Resources: on" : "Resources";
	default: return "";
	}
}

void drawBtn(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const int b = static_cast<int>(psWidget->UserData);
	const W_BUTTON *wb = static_cast<const W_BUTTON *>(psWidget);
	const float s = modui::uiScale();
	const bool on = (b == B_MAP && s_mode != 0) || (b == B_SYM && modtactical::enabled()) || (b == B_SPLIT && s_mode == 2) || (b == B_RES && s_res);
	static const PIELIGHT accent = pal_RGBA(110, 200, 255, 255);
	modui::wzButton(static_cast<int>(xOffset) + psWidget->x(), static_cast<int>(yOffset) + psWidget->y(), psWidget->width(), psWidget->height(),
	                btnLabel(b), on, (wb->getState() & WBUT_HIGHLIGHT) != 0, (wb->getState() & WBUT_DOWN) != 0, modui::sizeClassFor(s), &accent);
}

void open(int mode)
{
	const bool was = s_mode != 0;
	s_mode = mode;
	if (!was && mode != 0)
	{
		fit();
		s_texAt = 0;
		audio_PlayTrack(ID_SOUND_BUTTON_CLICK_5);
	}
	if (mode != 0 && was) { const float w = s_wpp; fit(); s_wpp = std::min(w, s_fit * 1.3f); }
}

void ensureScreen()
{
	if (s_screen) { return; }
	auto screen = W_SCREEN::make();
	screen->psForm->hide();   // durchlaessige Wurzel (siehe modpaint)
	s_btn.clear();
	static const char *tips[B_COUNT] = {
		"Strategic map (like Supreme Commander) - also opens with a click on the minimap",
		"Tactical symbols: at medium and far zoom the units are shown as military map symbols instead of models/sprites",
		"Split screen: strategic map on the left, normal view on the right",
		"Close the strategic map",
		"Display mode like in Supreme Commander: terrain / cartographic (paper map with contour lines) / elevation (height colours) / strategic (dark relief)",
		"Show the resources (oil resources and derricks) on the strategic map" };
	for (int b = 0; b < B_COUNT; ++b)
	{
		auto w = std::make_shared<W_BUTTON>();
		screen->psForm->attach(w);
		w->displayFunction = drawBtn;
		w->UserData = static_cast<UDWORD>(b);
		w->setTip(tips[b]);
		w->addOnClickHandler([b](W_BUTTON &) { widgScheduleTask([b]() {
			switch (b)
			{
			case B_MAP: open(s_mode != 0 ? 0 : 1); break;
			case B_SYM: modtactical::toggle(); break;
			case B_SPLIT: open(s_mode == 2 ? 1 : 2); break;
			case B_CLOSE: open(0); break;
			case B_VIEW: s_view = (s_view + 1) % V_COUNT; s_texAt = 0; break;
			case B_RES: s_res = !s_res; break;
			default: break;
			}
		}); });
		s_btn.push_back(w);
	}
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
}

void layout()
{
	const float s = modui::uiScale();
	const int bw = static_cast<int>(70 * s), bh = static_cast<int>(18 * s), gap = static_cast<int>(3 * s);
	const int W = static_cast<int>(screenWidth);
	// "Map" und "Symbols" ueber der Minikarte
	// (18.11. 12:10) ohne Minikarte: rechts unten ueber dem Schlachtplan (nicht darauf)
	int rx = W - 2 * bw - gap - 6, ry = modpaint::stackTop() - bh - gap;
	const std::shared_ptr<WIDGET> radar = getRadarWidget();
	if (radar && radar->visible() && radar->width() > 0)
	{
		// (18.11. 01:14 "Das Menue rechts liegt teilweise uebereinander"): die gedrehte Minikarte ragt mit den Ecken bis zu 21 % ueber ihren
		// Rahmen hinaus - die Knoepfe liegen darueber mit Abstand
		const int m = rotateRadar ? static_cast<int>(radar->height() * 0.21f) : 0;
		rx = radar->x() + std::max(0, (radar->width() - (2 * bw + gap)) / 2);
		ry = radar->y() - bh - gap - m;
	}
	s_btn[B_MAP]->setGeometry(rx, ry, bw, bh);
	s_btn[B_SYM]->setGeometry(rx + bw + gap, ry, bw, bh);
	// "Split"/"Close" in der Titelleiste der Karte
	const Rect p = panel();
	const int by = static_cast<int>(p.y0 + (barH() - static_cast<float>(bh)) * 0.5f);
	s_btn[B_CLOSE]->setGeometry(static_cast<int>(p.x1) - bw - gap * 2, by, bw, bh);
	s_btn[B_SPLIT]->setGeometry(static_cast<int>(p.x1) - 2 * bw - gap * 3, by, bw, bh);
	const int vw = static_cast<int>(120 * s), rw = static_cast<int>(84 * s);
	s_btn[B_VIEW]->setGeometry(static_cast<int>(p.x0) + static_cast<int>(130 * s), by, vw, bh);
	s_btn[B_RES]->setGeometry(static_cast<int>(p.x0) + static_cast<int>(130 * s) + vw + gap, by, rw, bh);
	s_btn[B_SPLIT]->show(s_mode != 0);
	s_btn[B_CLOSE]->show(s_mode != 0);
	s_btn[B_VIEW]->show(s_mode != 0);
	s_btn[B_RES]->show(s_mode != 0);
}
} // namespace

void tick()
{
	if (selectedPlayer >= MAX_PLAYERS || gameWorld.map.width <= 0) { return; }
	ensureScreen();
	if (s_btn.size() != B_COUNT) { return; }
	// (18.11. 12:30) im Design-Bildschirm keine Map/Symbol-Knoepfe (lagen ueber der Vorlagenliste)
	const bool design = intMode == INT_DESIGN;
	s_btn[B_MAP]->show(!design);
	s_btn[B_SYM]->show(!design);
	if (design) { return; }
	layout();
	if (s_mode != 0 && (s_texAt == 0 || s_texView != s_view || realTime - s_texAt > 3000u)) { rebuildTexture(); }
}

int viewCenterX(int screenW)
{
	return s_mode == 2 ? screenW * 3 / 4 : screenW / 2;
}

bool isOpen() { return s_mode != 0; }

void radarClicked(int tileX, int tileY)
{
	(void)tileX; (void)tileY;
	open(s_mode != 0 ? 0 : 1);
}

bool zoom(int multiplier)
{
	if (s_mode == 0 || !inRect(mapArea(), static_cast<float>(mouseX()), static_cast<float>(mouseY()))) { return false; }
	const glm::vec2 before = toWorld(static_cast<float>(mouseX()), static_cast<float>(mouseY()));
	s_wpp *= multiplier > 0 ? 1.18f : (1.f / 1.18f);   // wie die Kamera: positiv = heraus
	clampView();
	const glm::vec2 after = toWorld(static_cast<float>(mouseX()), static_cast<float>(mouseY()));
	s_cx += before.x - after.x; s_cy += before.y - after.y;
	clampView();
	return true;
}

bool processMouse(bool overUi)
{
	if (s_mode == 0 || selectedPlayer >= MAX_PLAYERS) { return false; }
	if (overUi && !s_lDown && !s_mDown) { return false; }
	const float mx = static_cast<float>(mouseX()), my = static_cast<float>(mouseY());
	const Rect pr = panel(), a = mapArea();
	const bool over = inRect(pr, mx, my);
	if (!over && !s_lDown && !s_mDown) { return false; }
	const glm::vec2 w = toWorld(mx, my);
	const Vector2i wpos(std::clamp(static_cast<int>(w.x), 0, static_cast<int>(worldW()) - 1), std::clamp(static_cast<int>(w.y), 0, static_cast<int>(worldH()) - 1));
	// Schlachtplan-Zeichnen auf der grossen Karte
	if (inRect(a, mx, my) && modpaint::processMouse(false, wpos)) { return true; }
	// verschieben: mittlere Maustaste
	if (mousePressed(MOUSE_MMB) && inRect(a, mx, my)) { s_mDown = true; s_mLast = Vector2i(mouseX(), mouseY()); }
	if (s_mDown)
	{
		if (!mouseDown(MOUSE_MMB)) { s_mDown = false; }
		else
		{
			s_cx -= static_cast<float>(mouseX() - s_mLast.x) * s_wpp; s_cy -= static_cast<float>(mouseY() - s_mLast.y) * s_wpp;
			s_mLast = Vector2i(mouseX(), mouseY());
			clampView();
		}
	}
	const bool shift = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	// Auswahl: Klick / Rahmen
	if (mousePressed(MOUSE_LMB) && inRect(a, mx, my)) { s_lDown = true; s_dragSel = false; s_lStart = Vector2i(mouseX(), mouseY()); }
	if (s_lDown && mouseDown(MOUSE_LMB) && (std::abs(mouseX() - s_lStart.x) > 5 || std::abs(mouseY() - s_lStart.y) > 5)) { s_dragSel = true; }
	if (s_lDown && mouseReleased(MOUSE_LMB))
	{
		s_lDown = false;
		if (s_dragSel)
		{
			const float x0 = std::min<float>(s_lStart.x, mx), x1 = std::max<float>(s_lStart.x, mx);
			const float y0 = std::min<float>(s_lStart.y, my), y1 = std::max<float>(s_lStart.y, my);
			if (!shift) { clearSelection(); }
			for (DROID *d : gameWorld.objects.droids[selectedPlayer])
			{
				if (d->died || modlayer::layerOf(d) != modlayer::current()) { continue; }
				const glm::vec2 s = toScreen(static_cast<float>(d->pos.x), static_cast<float>(d->pos.y));
				if (s.x >= x0 && s.x <= x1 && s.y >= y0 && s.y <= y1) { SelectDroid(d); }
			}
		}
		else
		{
			DROID *d = droidAt(mx, my, true, false);
			const bool dbl = realTime - s_lastClick < 350u && std::abs(s_lastClickPos.x - mouseX()) < 8 && std::abs(s_lastClickPos.y - mouseY()) < 8;
			s_lastClick = realTime; s_lastClickPos = Vector2i(mouseX(), mouseY());
			if (d != nullptr)
			{
				if (!shift) { clearSelection(); }
				SelectDroid(d);
			}
			else if (dbl)
			{
				// Doppelklick: Kamera dorthin (ganzer Bildschirm: Karte schliessen)
				setViewPos(static_cast<UDWORD>(map_coord(wpos.x)), static_cast<UDWORD>(map_coord(wpos.y)), true);
				if (s_mode == 1) { s_mode = 0; }
			}
			else if (!shift) { clearSelection(); }
		}
	}
	// Befehl: Rechtsklick
	if (mouseReleased(MOUSE_RMB) && inRect(a, mx, my))
	{
		BASE_OBJECT *target = droidAt(mx, my, false, true);
		if (target == nullptr) { target = enemyStructAt(mx, my); }
		if (target != nullptr) { orderSelectedObjAdd(selectedPlayer, target, shift); }
		else { orderSelectedLoc(selectedPlayer, static_cast<uint32_t>(wpos.x), static_cast<uint32_t>(wpos.y), shift); }
		audio_PlayTrack(ID_SOUND_MESSAGEEND);
	}
	return true;
}

void draw()
{
	if (s_mode == 0 || selectedPlayer >= MAX_PLAYERS || gameWorld.map.width <= 0) { return; }
	const float sc = modui::uiScale();
	const Rect p = panel(), a = mapArea();
	// Grund und Gelaende
	pie_BoxFill_alpha(static_cast<int>(p.x0), static_cast<int>(p.y0), static_cast<int>(p.x1), static_cast<int>(p.y1), pal_RGBA(8, 12, 22, 255));
	if (s_gfx == nullptr) { rebuildTexture(); }
	drawTerrain();
	// strategischer Blauschleier (nur Gelaende-Ansicht) + Gitter (alle 16 Felder)
	if (s_view == V_TERRAIN)
	{
		pie_BoxFill_alpha(static_cast<int>(a.x0), static_cast<int>(a.y0), static_cast<int>(a.x1), static_cast<int>(a.y1), pal_RGBA(10, 30, 70, 50));
	}
	const PIELIGHT gridCol = s_view == V_CARTO ? pal_RGBA(90, 70, 50, 45) : pal_RGBA(150, 200, 255, 38);
	const float step = static_cast<float>(world_coord(16));
	for (float gx = 0.f; gx <= worldW(); gx += step)
	{
		const glm::vec2 s0 = toScreen(gx, 0.f), s1 = toScreen(gx, worldH());
		if (s0.x < a.x0 || s0.x > a.x1) { continue; }
		line(glm::vec2(s0.x, std::max(a.y0, s0.y)), glm::vec2(s1.x, std::min(a.y1, s1.y)), 1.f, gridCol);
	}
	for (float gy = 0.f; gy <= worldH(); gy += step)
	{
		const glm::vec2 s0 = toScreen(0.f, gy), s1 = toScreen(worldW(), gy);
		if (s0.y < a.y0 || s0.y > a.y1) { continue; }
		line(glm::vec2(std::max(a.x0, s0.x), s0.y), glm::vec2(std::min(a.x1, s1.x), s1.y), 1.f, gridCol);
	}
	const modlayer::Layer view = modlayer::current();
	const float ss = symbolScale();
	// Rohstoffe (wie die Massepunkte in Supreme Commander): freie Oelquellen als leuchtende Ringe, Bohrtuerme mit Punkt
	if (s_res)
	{
		const float r = std::clamp(5.5f * sc * (s_fit / std::max(1.f, s_wpp)) * 0.5f, 3.5f * sc, 9.f * sc);
		for (const FEATURE *f : gameWorld.objects.features[0])
		{
			if (f == nullptr || f->died || f->psStats == nullptr || f->psStats->subType != FEAT_OIL_RESOURCE) { continue; }
			if (selectedPlayer < MAX_PLAYERS && !TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(mapTile(gameWorld.map, map_coord(f->pos.x), map_coord(f->pos.y)))) { continue; }
			const glm::vec2 c = toScreen(static_cast<float>(f->pos.x), static_cast<float>(f->pos.y));
			if (!inRect(a, c.x, c.y)) { continue; }
			pie_DrawSoftRoundedBox(c.x - r * 1.8f, c.y - r * 1.8f, c.x + r * 1.8f, c.y + r * 1.8f, pal_RGBA(120, 255, 90, 50), r * 1.8f, r);
			pie_DrawSoftRoundedBox(c.x - r, c.y - r, c.x + r, c.y + r, pal_RGBA(20, 40, 10, 230), r, 1.f);
			pie_DrawSoftRoundedBox(c.x - r + 1.5f * sc, c.y - r + 1.5f * sc, c.x + r - 1.5f * sc, c.y + r - 1.5f * sc, pal_RGBA(130, 255, 90, 255), r, 1.f);
			pie_DrawSoftRoundedBox(c.x - r * 0.45f, c.y - r * 0.45f, c.x + r * 0.45f, c.y + r * 0.45f, pal_RGBA(20, 40, 10, 255), r * 0.45f, 1.f);
		}
	}
	// Bauwerke: kleine Flaechen in Spielerfarbe, wichtige als Symbol
	for (unsigned pl = 0; pl < MAX_PLAYERS; ++pl)
	{
		const PIELIGHT tc = pal_GetTeamColour(getPlayerColour(pl));
		for (const STRUCTURE *s : gameWorld.objects.structures[pl])
		{
			if (!visibleObj(s) || modlayer::layerOf(s) != view || s->pStructureType == nullptr) { continue; }
			const glm::vec2 c = toScreen(static_cast<float>(s->pos.x), static_cast<float>(s->pos.y));
			if (!inRect(a, c.x, c.y)) { continue; }
			const STRUCTURE_TYPE t = s->pStructureType->type;
			const bool key = t == REF_FACTORY || t == REF_CYBORG_FACTORY || t == REF_VTOL_FACTORY || t == REF_RESEARCH || t == REF_POWER_GEN
			                 || t == REF_HQ || t == REF_REPAIR_FACILITY || t == REF_RESOURCE_EXTRACTOR || (t == REF_DEFENSE && s->numWeaps > 0) || t == REF_FORTRESS;
			if (key && s_wpp < s_fit * 0.75f)
			{
				modtactical::drawStructureSymbol(s, c.x, c.y, ss * 0.85f);
			}
			else
			{
				const Vector2i size = s->size();
				const float hx = std::max(1.5f, static_cast<float>(size.x * TILE_UNITS) * 0.5f / s_wpp), hy = std::max(1.5f, static_cast<float>(size.y * TILE_UNITS) * 0.5f / s_wpp);
				pie_BoxFill_alpha(static_cast<int>(c.x - hx), static_cast<int>(c.y - hy), static_cast<int>(c.x + hx + 1.f), static_cast<int>(c.y + hy + 1.f), pal_RGBA(tc.byte.r, tc.byte.g, tc.byte.b, 220));
			}
		}
	}
	// Sichtfenster der Kamera (wie auf der Minikarte, gedrehtes Quadrat)
	{
		const float half = std::max(900.f, std::min(3600.f, getViewDistance() * 0.55f + 640.f));
		const float ang = static_cast<float>(playerPos.r.y) * (2.f * 3.14159265f / 65536.f);
		const float c = std::cos(ang), sn = std::sin(ang);
		glm::vec2 q[4];
		static const float CX[4] = { -1.f, 1.f, 1.f, -1.f }, CY[4] = { -1.f, -1.f, 1.f, 1.f };
		for (int k = 0; k < 4; ++k)
		{
			const float lx = CX[k] * half, ly = CY[k] * half;
			q[k] = toScreen(static_cast<float>(playerPos.p.x) + lx * c + ly * sn, static_cast<float>(playerPos.p.z) + ly * c - lx * sn);
		}
		for (int k = 0; k < 4; ++k) { line(q[k], q[(k + 1) % 4], 6.f * sc, pal_RGBA(255, 255, 255, 40)); }
		for (int k = 0; k < 4; ++k) { line(q[k], q[(k + 1) % 4], 1.8f * sc, pal_RGBA(255, 255, 255, 220)); }
	}
	// Schlachtplan-Zeichnungen auf der Karte
	modpaint::drawOnMap([](const Vector2i &wp, Vector2i &out) {
		const glm::vec2 s = toScreen(static_cast<float>(wp.x), static_cast<float>(wp.y));
		out = Vector2i(static_cast<int>(s.x), static_cast<int>(s.y));
		return true;
	});
	// Einheiten als taktische Symbole
	for (unsigned pl = 0; pl < MAX_PLAYERS; ++pl)
	{
		for (const DROID *d : gameWorld.objects.droids[pl])
		{
			if (!visibleObj(d) || modlayer::layerOf(d) != view) { continue; }
			const glm::vec2 c = toScreen(static_cast<float>(d->pos.x), static_cast<float>(d->pos.y));
			if (!inRect(a, c.x, c.y)) { continue; }
			modtactical::drawDroidSymbol(d, c.x, c.y, ss);
		}
	}
	// Auswahlrahmen
	if (s_lDown && s_dragSel)
	{
		const float x0 = std::min<float>(s_lStart.x, mouseX()), x1 = std::max<float>(s_lStart.x, mouseX());
		const float y0 = std::min<float>(s_lStart.y, mouseY()), y1 = std::max<float>(s_lStart.y, mouseY());
		pie_BoxFill_alpha(static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1), static_cast<int>(y1), pal_RGBA(120, 220, 255, 40));
		line(glm::vec2(x0, y0), glm::vec2(x1, y0), 1.5f, pal_RGBA(150, 230, 255, 220)); line(glm::vec2(x1, y0), glm::vec2(x1, y1), 1.5f, pal_RGBA(150, 230, 255, 220));
		line(glm::vec2(x1, y1), glm::vec2(x0, y1), 1.5f, pal_RGBA(150, 230, 255, 220)); line(glm::vec2(x0, y1), glm::vec2(x0, y0), 1.5f, pal_RGBA(150, 230, 255, 220));
	}
	// Titelleiste (WZ-Stil) und Trennlinie zur 3D-Ansicht
	modui::wzPanel(static_cast<int>(p.x0), static_cast<int>(p.y0), static_cast<int>(p.x1), static_cast<int>(p.y0 + barH()), "Strategic map", modui::sizeClassFor(sc));
	if (s_mode == 2)
	{
		pie_BoxFill_alpha(static_cast<int>(p.x1) - 2, 0, static_cast<int>(p.x1) + 2, static_cast<int>(p.y1), pal_RGBA(150, 200, 255, 200));
	}
}

void shutdown()
{
	s_mode = 0;
	delete s_gfx;
	s_gfx = nullptr;
	s_texW = s_texH = 0;
	s_img.clear();
	if (s_screen) { widgRemoveOverlayScreen(s_screen); }
	s_screen = nullptr; s_btn.clear();
}
}
