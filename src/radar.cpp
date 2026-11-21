/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
#include <string.h>
#include <cstdlib>

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/math_ext.h"
#include "lib/ivis_opengl/pieblitfunc.h"
// FIXME Direct iVis implementation include!
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/piefunc.h"
#include "lib/ivis_opengl/bitimage.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "lib/gamelib/gtime.h"
#include "advvis.h"
#include "objects.h"
#include "display3d.h"
#include "map.h"
#include "component.h"
#include "console.h"
#include "radar.h"
#include "mapdisplay.h"
#include "hci.h"
#include "geometry.h"
#include "intimage.h"
#include "loop.h"
#include "warcam.h"
#include "display.h"
#include "mission.h"
#include "campaigninfo.h"
#include "multiplay.h"
#include "intdisplay.h"
#include "texture.h"
#include "warzoneconfig.h"
#include "order.h"
#include "cameratrace.h"
#include "modbigmap.h"
#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/transform.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

#define HIT_NOTIFICATION	(GAME_TICKS_PER_SEC * 2)
#define RADAR_FRAME_SKIP	10

static void applyMinimapOverlay();

bool bEnemyAllyRadarColor = false;     			/**< Enemy/ally radar color. */
RADAR_DRAW_MODE	radarDrawMode = RADAR_MODE_DEFAULT;	/**< Current mini-map mode. */
bool rotateRadar = false; ///< Rotate the radar?  MOD (18.11.): Minikarte dreht sich nicht mit
bool radarRotationArrow = true; ///< display arrow when radar rotation enabled?

static PIELIGHT		colRadarAlly, colRadarMe, colRadarEnemy;
static PIELIGHT		tileColours[MAX_TILES];
static iV_Image		radarBitmap;
static UDWORD		*radarOverlayBuffer = nullptr;
static Vector3i		playerpos = {0, 0, 0};

class RadarWidget : public WIDGET {
public:
	RadarWidget();

protected:
	void display(int xOffset, int yOffset) override;
	bool hitTest(int x, int y) const override;
	void clicked(W_CONTEXT *, WIDGET_KEY = WKEY_PRIMARY) override;
	void released(W_CONTEXT *, WIDGET_KEY = WKEY_PRIMARY) override;
	void highlight(W_CONTEXT *) override;
	void highlightLost() override;
	bool capturesMouseDrag(WIDGET_KEY) override;
	void mouseDragged(WIDGET_KEY, W_CONTEXT *start, W_CONTEXT *current) override;

public:
	bool hasHighlight() const { return m_hasHighlight; }
	bool isRadarDragging() const { return m_handlingDrag; }

private:
	std::array<optional<Vector2i>, 3> m_clickPosition;
	bool m_hasHighlight = false;
	bool m_handlingDrag = false;
};

static std::shared_ptr<RadarWidget> pRadarWidget;

PIELIGHT clanColours[] =
{
	// see frontend2.png for team color order.
	// [r,g,b,a]
	{{0, 255, 0, 255}},		// green  Player 0
	{{255, 192, 40, 255}},          // orange Player 1
	{{255, 255, 255, 255}},	// grey   Player 2
	{{0, 0, 0, 255}},			// black  Player 3
	{{255, 0, 0, 255}},		// red    Player 4
	{{20, 20, 255, 255}},		// blue   Player 5
	{{255, 0, 192, 255}},           // pink   Player 6
	{{0, 255, 255, 255}},		// cyan   Player 7
	{{255, 255, 0, 255}},           // yellow Player 8
	{{144, 0, 255, 255}},           // purple Player 9
	{{200, 255, 255, 255}},         // white  Player A (Should be brighter than grey, but grey is already maximum.)
	{{128, 128, 255, 255}},         // bright blue Player B
	{{128, 255, 128, 255}},         // neon green  Player C
	{{128, 0, 0, 255}},             // infrared    Player D
	{{64, 0, 128, 255}},            // ultraviolet Player E
	{{128, 128, 0, 255}},           // brown       Player F
	{{214, 186, 128, 255}},         // MOD: Wuestentarn (Farbe 16)
};

static PIELIGHT flashColours[] =
{
	//right now the flash color is all bright red
	{{254, 37, 37, 200}},	// Player 0
	{{254, 37, 37, 200}},	// Player 1
	{{254, 37, 37, 200}},	// Player 2
	{{254, 37, 37, 200}},	// Player 3
	{{254, 37, 37, 200}},	// Player 4  (notice, brighter red)
	{{254, 37, 37, 200}},	// Player 5
	{{254, 37, 37, 200}},	// Player 6
	{{254, 37, 37, 200}},   // Player 7
	{{254, 37, 37, 200}},   // Player 8
	{{254, 37, 37, 200}},   // Player 9
	{{254, 37, 37, 200}},   // Player A
	{{254, 37, 37, 200}},   // Player B
	{{254, 37, 37, 200}},   // Player C
	{{254, 37, 37, 200}},   // Player D
	{{254, 37, 37, 200}},   // Player E
	{{254, 37, 37, 200}},   // Player F
	{{254, 37, 37, 200}},   // MOD: Wuestentarn (Farbe 16)
};

static size_t radarWidth, radarHeight, radarTexWidth, radarTexHeight;
static SDWORD radarCenterX, radarCenterY;
static uint8_t RadarZoom;
static float RadarZoomMultiplier = 1.0f;
static size_t radarBufferSize = 0;
static int frameSkip = 0;
static UDWORD lastBlink = 0;
static const UDWORD BLINK_INTERVAL = GAME_TICKS_PER_SEC / 1;
static const UDWORD BLINK_HALF_INTERVAL = BLINK_INTERVAL / 2;
static const float OVERLAY_OPACITY = 0.5f;

// taken from https://en.wikipedia.org/wiki/Alpha_compositing
PIELIGHT inline mix(PIELIGHT over, PIELIGHT base)
{
	float overAlpha = over.byte.a / 255.0f;
	float overRed = over.byte.r / 255.0f;
	float overGreen = over.byte.g / 255.0f;
	float overBlue = over.byte.b / 255.0f;

	float baseAlpha = base.byte.a / 255.0f;
	float baseRed = base.byte.r / 255.0f;
	float baseGreen = base.byte.g / 255.0f;
	float baseBlue = base.byte.b / 255.0f;

	float newAlpha = overAlpha + baseAlpha * (1 - overAlpha);
	float newRed = (overRed * overAlpha + baseRed * baseAlpha * (1 - overAlpha)) / newAlpha;
	float newGreen = (overGreen * overAlpha + baseGreen * baseAlpha * (1 - overAlpha)) / newAlpha;
	float newBlue = (overBlue * overAlpha + baseBlue * baseAlpha * (1 - overAlpha)) / newAlpha;

	UBYTE returnAlpha = static_cast<UBYTE>(clip<int>(static_cast<int>(newAlpha * 255.0f), 0, 255));
	UBYTE returnRed = static_cast<UBYTE>(clip<int>(static_cast<int>(newRed * 255.0f), 0, 255));
	UBYTE returnGreen = static_cast<UBYTE>(clip<int>(static_cast<int>(newGreen * 255.0f), 0, 255));
	UBYTE returnBlue = static_cast<UBYTE>(clip<int>(static_cast<int>(newBlue * 255.0f), 0, 255));

	return { {returnRed, returnGreen, returnBlue, returnAlpha} };
}

PIELIGHT inline PLfromUDWORD(UDWORD value)
{
	PIELIGHT retColor = { {
		static_cast<UBYTE>((value & 0x000000FF) >> 0),
		static_cast<UBYTE>((value & 0x0000FF00) >> 8),
		static_cast<UBYTE>((value & 0x00FF0000) >> 16),
		static_cast<UBYTE>((value & 0xFF000000) >> 24),
	} };
	return retColor;
}

PIELIGHT inline applyAlpha(PIELIGHT color, float alpha)
{
	PIELIGHT ret = color;
	ret.byte.a = static_cast<uint8_t>(ret.byte.a * alpha);
	return ret;
}

static void DrawRadarTiles(WorldMapState& mapState);
static void DrawRadarObjects(GameWorld& world);
static void DrawRadarExtras(const glm::mat4 &modelViewProjectionMatrix);
static void DrawNorth(const glm::mat4 &modelViewProjectionMatrix);
static void modDrawRadarFrame(int x0, int y0, int x1, int y1);   // MOD (21.11.)
static void setViewingWindow(const WorldMapState& mapState);

static void radarSize(int ZoomLevel)
{
	float zoom = static_cast<float>(ZoomLevel) * RadarZoomMultiplier / 16.0f;
	radarWidth = static_cast<size_t>(radarTexWidth * zoom);
	radarHeight = static_cast<size_t>(radarTexHeight * zoom);
	if (rotateRadar)
	{
		radarCenterX = pie_GetVideoBufferWidth() - BASE_GAP * 4 - static_cast<int>(MAX(radarHeight, radarWidth)) / 2;
		radarCenterY = pie_GetVideoBufferHeight() - BASE_GAP * 4 - static_cast<int>(MAX(radarWidth, radarHeight)) / 2;
	}
	else
	{
		radarCenterX = pie_GetVideoBufferWidth() - BASE_GAP * 4 - static_cast<int>(radarWidth) / 2;
		radarCenterY = pie_GetVideoBufferHeight() - BASE_GAP * 4 - static_cast<int>(radarHeight) / 2;
	}
	debug(LOG_WZ, "radar=(%u,%u) tex=(%zu,%zu) size=(%zu,%zu)", radarCenterX, radarCenterY, radarTexWidth, radarTexHeight, radarWidth, radarHeight);

	if (pRadarWidget)
	{
		auto radarDiag = static_cast<size_t>(ceil(sqrt(double((radarWidth + 1) * (radarWidth + 1)) + double((radarHeight + 1) * (radarHeight + 1)))));
		int radarX0 = radarCenterX - (radarDiag / 2);
		int radarY0 = radarCenterY - (radarDiag / 2);
		pRadarWidget->setGeometry(radarX0, radarY0, radarDiag, radarDiag);
	}
}

void radarInitVars()
{
	radarTexWidth = 0;
	radarTexHeight = 0;
	RadarZoom = war_GetRadarZoom();
	debug(LOG_WZ, "Resetting radar zoom to %u", RadarZoom);
	radarSize(RadarZoom);
	playerpos = Vector3i(-1, -1, -1);
	frameSkip = 0;
}

bool InitRadar()
{
	// Ally/enemy/me colors
	colRadarAlly = WZCOL_YELLOW;
	colRadarEnemy = WZCOL_RED;
	colRadarMe = WZCOL_WHITE;
	if (!pRadarWidget)
	{
		pRadarWidget = std::make_shared<RadarWidget>();
	}
	return true;
}

bool resizeRadar(const WorldMapState& mapState)
{
	radarBitmap.clear();
	if (radarOverlayBuffer)
	{
		free(radarOverlayBuffer);
	}
	radarTexWidth = static_cast<size_t>(std::abs(mapState.scroll.maxX - mapState.scroll.minX));
	radarTexHeight = static_cast<size_t>(std::abs(mapState.scroll.maxY - mapState.scroll.minY));
	radarBufferSize = radarTexWidth * radarTexHeight * sizeof(UDWORD);
	radarBitmap.allocate(radarTexWidth, radarTexHeight, 4, true);
	radarOverlayBuffer = (uint32_t*)malloc(radarBufferSize);
	memset(radarOverlayBuffer, 0, radarBufferSize);
	frameSkip = 0;
	{
		// MOD (Wunsch 20.11. "Die Minimap soll immer gleich gross bleiben, auch bei grossen Maps, und sich bei extrem grossen Maps nur ein
		// bisschen vergroessern"): bisher war die nicht drehende Minikarte je Kachel einen Bildpunkt (x Zoom/16) gross - grosse Karten ergaben
		// eine riesige Minikarte. Jetzt ist ihre lange Seite immer so lang wie bisher bei einer Karte mit 112 Kacheln (bei Zoom 16: 112
		// Oberflaechen-Bildpunkte); erst ab 160 Original-Kacheln waechst sie langsam mit, hoechstens um ein Viertel (ab 512 Kacheln).
		// Auf dem feinen Raster (Viertel) gleich gross.
		constexpr float MOD_RADAR_LONG_SIDE = 112.0f;
		const size_t texMax = std::max<size_t>({radarTexWidth, radarTexHeight, 1});
		const float tilesOrig = static_cast<float>(texMax) * static_cast<float>(TILE_UNITS) / static_cast<float>(DIST_TILE);
		const float grow = 1.0f + 0.25f * std::clamp((tilesOrig - 160.0f) / 352.0f, 0.0f, 1.0f);
		RadarZoomMultiplier = MOD_RADAR_LONG_SIDE / static_cast<float>(texMax) * grow;
	}
	debug(LOG_WZ, "Setting radar zoom to %u", RadarZoom);
	radarSize(RadarZoom);
	pie_SetRadar(-static_cast<float>(radarWidth) / 2.0f - 1, -static_cast<float>(radarHeight) / 2.0f - 1, static_cast<float>(radarWidth), static_cast<float>(radarHeight), radarTexWidth, radarTexHeight);

	return true;
}

bool ShutdownRadar()
{
	radarBitmap.clear();
	free(radarOverlayBuffer);
	radarOverlayBuffer = nullptr;
	frameSkip = 0;
	if (pRadarWidget)
	{
		psWScreen->psForm->detach(pRadarWidget);
		pRadarWidget.reset();
	}
	return true;
}

void SetRadarZoom(const WorldMapState& mapState, uint8_t ZoomLevel)
{
	if (ZoomLevel > MAX_RADARZOOM)
	{
		ZoomLevel = MAX_RADARZOOM;
	}
	if (ZoomLevel < MIN_RADARZOOM)
	{
		ZoomLevel = MIN_RADARZOOM;
	}
	debug(LOG_WZ, "Setting radar zoom to %u from %u", ZoomLevel, RadarZoom);
	RadarZoom = ZoomLevel;
	radarSize(RadarZoom);
	frameSkip = 0;
	resizeRadar(mapState);
}

uint8_t GetRadarZoom()
{
	return RadarZoom;
}

/** Calculate the radar pixel sizes. Returns pixels per tile. */
static void CalcRadarPixelSize(float *SizeH, float *SizeV)
{
	*SizeH = (float)radarHeight / std::max<size_t>(radarTexHeight, 1);
	*SizeV = (float)radarWidth / std::max<size_t>(radarTexWidth, 1);
}

/** Given a position within the radar, return a world coordinate. */
void CalcRadarPosition(const WorldMapState& mapState, int mX, int mY, int *PosX, int *PosY)
{
	int		sPosX, sPosY;
	float		pixSizeH, pixSizeV;

	Vector2f pos;
	pos.x = mX - radarCenterX;
	pos.y = mY - radarCenterY;
	if (rotateRadar)
	{
		pos = Vector2f_Rotate2f(pos, -playerPos.r.y);
	}
	pos.x += radarWidth / 2.0f;
	pos.y += radarHeight / 2.0f;

	CalcRadarPixelSize(&pixSizeH, &pixSizeV);
	sPosX = static_cast<int>(pos.x / pixSizeH);	// adjust for pixel size
	sPosY = static_cast<int>(pos.y / pixSizeV);
	sPosX += mapState.scroll.minX;		// adjust for scroll limits
	sPosY += mapState.scroll.minY;

#if REALLY_DEBUG_RADAR
	debug(LOG_ERROR, "m=(%d,%d) radar=(%d,%d) pos(%d,%d), scroll=(%u-%u,%u-%u) sPos=(%d,%d), pixSize=(%f,%f)",
	      mX, mY, radarX, radarY, posX, posY, mapState.scroll.minX, mapState.scroll.maxX, mapState.scroll.minY, mapState.scroll.maxY, sPosX, sPosY, pixSizeH, pixSizeV);
#endif

	// old safety code -- still necessary?
	sPosX = clip<int>(sPosX, mapState.scroll.minX, mapState.scroll.maxX -1);
	sPosY = clip<int>(sPosY, mapState.scroll.minY, mapState.scroll.maxY -1);

	*PosX = sPosX;
	*PosY = sPosY;
}

void drawRadar(GameWorld& world)
{
	float	pixSizeH, pixSizeV;

	CalcRadarPixelSize(&pixSizeH, &pixSizeV);

	ASSERT_OR_RETURN(, radarBitmap.bmp_w(), "No radar buffer allocated");
	ASSERT_OR_RETURN(, radarOverlayBuffer, "No radar buffer allocated");

	// MOD: Ebenenwechsel (Untergrund/Orbit): die Karte der Ebene kann ein anderes Scrollfenster haben als die Oberflaeche - dann Puffer neu anlegen,
	// sonst schreibt die Minikarte ueber das Pufferende hinaus (Bild zerrissen, tausende Fehlermeldungen je Sekunde)
	{
		const size_t wantW = static_cast<size_t>(std::abs(world.map.scroll.maxX - world.map.scroll.minX));
		const size_t wantH = static_cast<size_t>(std::abs(world.map.scroll.maxY - world.map.scroll.minY));
		if (wantW != radarTexWidth || wantH != radarTexHeight)
		{
			resizeRadar(world.map);
		}
	}

	setViewingWindow(world.map);
	playerpos = playerPos.p; // cache position

	if (frameSkip <= 0)
	{
		DrawRadarTiles(world.map);
		DrawRadarObjects(world);
		applyMinimapOverlay();
		pie_DownLoadRadar(radarBitmap);
		frameSkip = RADAR_FRAME_SKIP;
		camtrace::markRadarRedraw();
	}
	frameSkip--;
	glm::mat4 radarMatrix = glm::translate(glm::vec3(radarCenterX, radarCenterY, 0));
	glm::mat4 orthoMatrix = glm::ortho(0.f, static_cast<float>(pie_GetVideoBufferWidth()), static_cast<float>(pie_GetVideoBufferHeight()), 0.f);
	if (rotateRadar)
	{
		// rotate the map
		radarMatrix *= glm::rotate(UNDEG(playerPos.r.y), glm::vec3(0.f, 0.f, 1.f));
		if (radarRotationArrow)
		{
			DrawNorth(orthoMatrix * radarMatrix);
		}
	}

	pie_RenderRadar(orthoMatrix * radarMatrix);
	DrawRadarExtras(orthoMatrix * radarMatrix * glm::translate(glm::vec3(-static_cast<float>(radarWidth) / 2.f - 1.f, -static_cast<float>(radarHeight) / 2.f - 1.f, 0.f)));
	if (!rotateRadar)   // MOD (21.11. 18:04): Metallrahmen um die Minikarte
	{
		const int rx0 = static_cast<int>(radarCenterX) - static_cast<int>(radarWidth) / 2 - 1, ry0 = static_cast<int>(radarCenterY) - static_cast<int>(radarHeight) / 2 - 1;
		modDrawRadarFrame(rx0, ry0, rx0 + static_cast<int>(radarWidth) + 2, ry0 + static_cast<int>(radarHeight) + 2);
	}
	drawRadarBlips(static_cast<int>(-static_cast<int>(radarWidth) / 2.f - 1), static_cast<int>(-static_cast<int>(radarHeight) / 2.f - 1), pixSizeH, pixSizeV, orthoMatrix * radarMatrix);
}

// MOD (Wunsch 21.11. 18:04 "Koennen wir auch schoenere Rahmen um die Minimap machen im StarCraft-Stil?"): eigener Industrie-Metallrahmen
// (eigene Gestaltung): gebuerstete Metallleiste mit Licht oben links und Schatten unten rechts, abgeschraegte Aussenecken, verstaerkte
// Eckwinkel mit Nieten, duenne leuchtende Innenkante und zwei Statuslampen. Bildschirmkoordinaten des Kartenrechtecks x0,y0 - x1,y1.
static PIELIGHT modMix(int r0, int g0, int b0, int r1, int g1, int b1, float t, int a = 255)
{
	t = std::clamp(t, 0.f, 1.f);
	return pal_RGBA(static_cast<UBYTE>(r0 + (r1 - r0) * t), static_cast<UBYTE>(g0 + (g1 - g0) * t), static_cast<UBYTE>(b0 + (b1 - b0) * t), static_cast<UBYTE>(a));
}

static void modFill(int x0, int y0, int x1, int y1, PIELIGHT c)
{
	if (x1 > x0 && y1 > y0) { pie_UniTransBoxFill(static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1), static_cast<float>(y1), c); }
}

static void modRivet(int cx, int cy, int r)
{
	for (int dy = -r - 1; dy <= r + 1; ++dy)   // dunkler Rand
	{
		const int hw = static_cast<int>(std::sqrt(std::max(0.f, static_cast<float>((r + 1) * (r + 1) - dy * dy))));
		modFill(cx - hw, cy + dy, cx + hw + 1, cy + dy + 1, pal_RGBA(10, 12, 16, 230));
	}
	for (int dy = -r; dy <= r; ++dy)
	{
		const int hw = static_cast<int>(std::sqrt(std::max(0.f, static_cast<float>(r * r - dy * dy))));
		modFill(cx - hw, cy + dy, cx + hw + 1, cy + dy + 1, modMix(185, 195, 205, 70, 78, 88, (dy + r) / (2.f * r + 0.01f)));
	}
	modFill(cx - std::max(1, r / 2), cy - std::max(1, r / 2), cx, cy, pal_RGBA(235, 240, 245, 220));   // Glanzpunkt
}

static void modDrawRadarFrame(int x0, int y0, int x1, int y1)
{
	const float s = std::max(1.f, static_cast<float>(pie_GetVideoBufferHeight()) / 900.f);
	const int T = std::max(9, static_cast<int>(std::lround(11.f * s)));   // Leistenbreite
	const int C = T + 3;                                                    // Abschraegung der Aussenecken
	const int X0 = x0 - T, Y0 = y0 - T, X1 = x1 + T, Y1 = y1 + T;
	const PIELIGHT edge = pal_RGBA(6, 8, 11, 255);
	// Schlagschatten
	modFill(X0 + 4, Y1 + 1, X1 + 4, Y1 + 5, pal_RGBA(0, 0, 0, 100));
	modFill(X1 + 1, Y0 + 4, X1 + 5, Y1 + 1, pal_RGBA(0, 0, 0, 100));
	// Leiste Ring fuer Ring (aussen -> innen): Profil = Glanzkante aussen, gebuerstete Flaeche, Rille, dunkle Innenkante;
	// oben/links im Licht, unten/rechts im Schatten
	for (int k = 0; k < T; ++k)
	{
		const float t = static_cast<float>(k) / static_cast<float>(T - 1);
		float lum;
		if (k == 0) { lum = 0.95f; }
		else if (k == 1) { lum = 0.75f; }
		else if (std::abs(t - 0.62f) < 0.5f / static_cast<float>(T)) { lum = 0.05f; }           // Rille
		else if (std::abs(t - 0.62f) < 1.5f / static_cast<float>(T)) { lum = 0.75f; }           // Kante neben der Rille
		else if (k >= T - 2) { lum = 0.18f; }                                                    // Innenkante
		else { lum = 0.45f + 0.25f * std::sin(t * 3.1415926f); }                                // gewoelbte Flaeche
		const PIELIGHT lit = modMix(34, 38, 46, 150, 160, 174, lum);
		const PIELIGHT dark = modMix(14, 16, 20, 92, 100, 112, lum);
		const PIELIGHT midL = modMix(26, 29, 35, 124, 134, 148, lum);
		const PIELIGHT midD = modMix(20, 23, 28, 108, 117, 130, lum);
		const int ox0 = X0 + k, oy0 = Y0 + k, ox1 = X1 - k, oy1 = Y1 - k;
		const int cut = std::max(0, C - k);
		modFill(ox0 + cut, oy0, ox1 - cut, oy0 + 1, lit);          // oben
		modFill(ox0, oy0 + cut, ox0 + 1, oy1 - cut, lit);          // links
		modFill(ox0 + cut, oy1 - 1, ox1 - cut, oy1, dark);         // unten
		modFill(ox1 - 1, oy0 + cut, ox1, oy1 - cut, dark);         // rechts
		if (cut > 0)   // 45-Grad-Schraegen der Ecken
		{
			iV_Line(ox0, oy0 + cut - 1, ox0 + cut - 1, oy0, lit);
			iV_Line(ox1 - cut, oy0, ox1 - 1, oy0 + cut - 1, midL);
			iV_Line(ox0, oy1 - cut, ox0 + cut - 1, oy1 - 1, midD);
			iV_Line(ox1 - cut, oy1 - 1, ox1 - 1, oy1 - cut, dark);
		}
	}
	// Umriss aussen
	iV_Line(X0 + C, Y0 - 1, X1 - C, Y0 - 1, edge);
	iV_Line(X0 + C, Y1, X1 - C, Y1, edge);
	iV_Line(X0 - 1, Y0 + C, X0 - 1, Y1 - C, edge);
	iV_Line(X1, Y0 + C, X1, Y1 - C, edge);
	iV_Line(X0 - 1, Y0 + C, X0 + C, Y0 - 1, edge);
	iV_Line(X1 - C, Y0 - 1, X1, Y0 + C, edge);
	iV_Line(X0 - 1, Y1 - C, X0 + C, Y1, edge);
	iV_Line(X1, Y1 - C, X1 - C, Y1, edge);
	// dunkle Fuge und leuchtende Innenkante an der Karte
	modFill(x0 - 2, y0 - 2, x1 + 2, y0 - 1, edge);
	modFill(x0 - 2, y1 + 1, x1 + 2, y1 + 2, edge);
	modFill(x0 - 2, y0 - 2, x0 - 1, y1 + 2, edge);
	modFill(x1 + 1, y0 - 2, x1 + 2, y1 + 2, edge);
	const PIELIGHT glow = pal_RGBA(80, 205, 255, 170);
	modFill(x0 - 1, y0 - 1, x1 + 1, y0, glow);
	modFill(x0 - 1, y1, x1 + 1, y1 + 1, glow);
	modFill(x0 - 1, y0, x0, y1, glow);
	modFill(x1, y0, x1 + 1, y1, glow);
	// Kopfplatte oben in der Mitte (Trapez mit Lueftungsschlitzen)
	{
		const int pw = std::max(5 * T, (x1 - x0) * 2 / 5), ph = T + std::max(4, T / 2);
		const int px0 = (x0 + x1) / 2 - pw / 2, py1 = Y0 + T - 1, py0 = py1 - ph;
		for (int r = 0; r < ph; ++r)
		{
			const int inset = std::max(0, ph - 1 - r - (ph - T));   // nur der ueberstehende Teil ist schraeg
			const float lum = (r == 0) ? 0.95f : (r == 1 ? 0.7f : 0.42f + 0.18f * std::sin(static_cast<float>(r) / static_cast<float>(ph) * 3.1415926f));
			modFill(px0 + inset, py0 + r, px0 + pw - inset, py0 + r + 1, modMix(34, 38, 46, 150, 160, 174, lum));
		}
		const int over = ph - T;
		iV_Line(px0 + over, py0 - 1, px0 + pw - over, py0 - 1, edge);
		iV_Line(px0 - 1, py0 + over, px0 + over, py0 - 1, edge);
		iV_Line(px0 + pw - over, py0 - 1, px0 + pw, py0 + over, edge);
		const int slots = 5, sw = std::max(2, T / 3), sh = std::max(3, ph / 2);
		const int gap = std::max(2, T / 3);
		const int total = slots * sw + (slots - 1) * gap;
		int sx = (x0 + x1) / 2 - total / 2;
		for (int i = 0; i < slots; ++i, sx += sw + gap)
		{
			modFill(sx, py0 + (ph - sh) / 2, sx + sw, py0 + (ph - sh) / 2 + sh, edge);
			modFill(sx, py0 + (ph - sh) / 2 + sh, sx + sw, py0 + (ph - sh) / 2 + sh + 1, pal_RGBA(150, 160, 172, 160));
		}
	}
	// verstaerkte Eckwinkel mit je zwei Nieten
	const int L = 3 * T + 2, W2 = T + 4;
	const int cx[4] = {X0 - 3, X1 + 3, X0 - 3, X1 + 3}, cy[4] = {Y0 - 3, Y0 - 3, Y1 + 3, Y1 + 3};
	for (int c = 0; c < 4; ++c)
	{
		const int sx = (c & 1) ? -1 : 1, sy = (c & 2) ? -1 : 1;
		const int ax = cx[c], ay = cy[c];
		auto rect = [&](int u0, int v0, int u1, int v1, PIELIGHT col) {
			const int ra = ax + sx * u0, rb = ax + sx * u1, rc = ay + sy * v0, rd = ay + sy * v1;
			modFill(std::min(ra, rb), std::min(rc, rd), std::max(ra, rb), std::max(rc, rd), col);
		};
		const int ch = C / 2 + 1;   // Schraege am Winkel
		rect(ch, 0, L, W2, edge);
		rect(0, ch, W2, L, edge);
		for (int j = 0; j < ch; ++j) { rect(ch - j, j, ch - j + 1, j + 1, edge); }
		const float tone = (c == 0) ? 0.8f : ((c == 3) ? 0.35f : 0.55f);
		rect(ch + 1, 1, L - 1, W2 - 1, modMix(40, 45, 53, 140, 150, 164, tone));
		rect(1, ch + 1, W2 - 1, L - 1, modMix(40, 45, 53, 140, 150, 164, tone - 0.08f));
		for (int j = 1; j < ch; ++j) { rect(ch - j + 1, j, W2 - 1, j + 1, modMix(40, 45, 53, 140, 150, 164, tone)); }
		rect(ch + 1, 1, L - 1, 2, pal_RGBA(190, 200, 212, 210));
		rect(1, ch + 1, 2, L - 1, pal_RGBA(170, 180, 194, 180));
		rect(L - 1, 1, L, W2 - 1, pal_RGBA(12, 14, 18, 200));
		rect(1, L - 1, W2 - 1, L, pal_RGBA(12, 14, 18, 200));
		const int rr = std::max(1, T / 4);
		modRivet(ax + sx * (L - T / 2 - 2), ay + sy * (W2 / 2), rr);
		modRivet(ax + sx * (W2 / 2), ay + sy * (L - T / 2 - 2), rr);
	}
	// Statuslampen auf der unteren Leiste (gruen = Karte aktiv, bernstein pulsiert langsam)
	const int ly = Y1 - T / 2 - 1, lx = X1 - L - 8;
	const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(realTime % 2400) / 2400.f * 6.2831853f);
	modFill(lx - 3, ly - 3, lx + 4, ly + 4, edge);
	modFill(lx - 2, ly - 2, lx + 3, ly + 3, pal_RGBA(60, 220, 100, 255));
	modFill(lx - 1, ly - 2, lx + 1, ly - 1, pal_RGBA(200, 255, 210, 255));
	modFill(lx - 13, ly - 3, lx - 6, ly + 4, edge);
	modFill(lx - 12, ly - 2, lx - 7, ly + 3, pal_RGBA(255, 165, 35, static_cast<UBYTE>(110 + 145 * pulse)));
}

static void DrawNorth(const glm::mat4 &modelViewProjectionMatrix)
{
	iV_DrawImage(IntImages, RADAR_NORTH, static_cast<int>(-((radarWidth / 2.f) + iV_GetImageWidth(IntImages, RADAR_NORTH) + 1)), static_cast<int>(-(radarHeight / 2.f)), modelViewProjectionMatrix);
}

static PIELIGHT inline appliedRadarColour(RADAR_DRAW_MODE drawMode, MAPTILE *WTile)
{
	PIELIGHT WScr = WZCOL_BLACK;	// squelch warning

	// draw radar on/off feature
	if (!getRevealStatus() && !TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(WTile))
	{
		return WZCOL_TRANSPARENT_BOX;
	}

	switch (drawMode)
	{
	case RADAR_MODE_TERRAIN:
		{
			// draw radar terrain on/off feature
			PIELIGHT col = tileColours[TileNumber_tile(WTile->texture)];

			col.byte.r = static_cast<uint8_t>(sqrtf(col.byte.r * WTile->illumination));
			col.byte.b = static_cast<uint8_t>(sqrtf(col.byte.b * WTile->illumination));
			col.byte.g = static_cast<uint8_t>(sqrtf(col.byte.g * WTile->illumination));
			if (terrainType(WTile) == TER_CLIFFFACE)
			{
				col.byte.r /= 2;
				col.byte.g /= 2;
				col.byte.b /= 2;
			}
			if (!hasSensorOnTile(WTile, selectedPlayer))
			{
				col.byte.r = col.byte.r * 2 / 3;
				col.byte.g = col.byte.g * 2 / 3;
				col.byte.b = col.byte.b * 2 / 3;
			}
			if (!TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(WTile))
			{
				col.byte.r /= 2;
				col.byte.g /= 2;
				col.byte.b /= 2;
			}
			WScr = col;
		}
		break;
	case RADAR_MODE_COMBINED:
		{
			// draw radar terrain on/off feature
			PIELIGHT col = tileColours[TileNumber_tile(WTile->texture)];

			col.byte.r = static_cast<uint8_t>(sqrtf(col.byte.r * (WTile->illumination + WTile->height / ELEVATION_SCALE) / 2));
			col.byte.b = static_cast<uint8_t>(sqrtf(col.byte.b * (WTile->illumination + WTile->height / ELEVATION_SCALE) / 2));
			col.byte.g = static_cast<uint8_t>(sqrtf(col.byte.g * (WTile->illumination + WTile->height / ELEVATION_SCALE) / 2));
			if (terrainType(WTile) == TER_CLIFFFACE)
			{
				col.byte.r /= 2;
				col.byte.g /= 2;
				col.byte.b /= 2;
			}
			if (!hasSensorOnTile(WTile, selectedPlayer))
			{
				col.byte.r = col.byte.r * 2 / 3;
				col.byte.g = col.byte.g * 2 / 3;
				col.byte.b = col.byte.b * 2 / 3;
			}
			if (!TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(WTile))
			{
				col.byte.r /= 2;
				col.byte.g /= 2;
				col.byte.b /= 2;
			}
			WScr = col;
		}
		break;
	case RADAR_MODE_HEIGHT_MAP:
		{
			WScr.byte.r = WScr.byte.g = WScr.byte.b = WTile->height / ELEVATION_SCALE;
		}
		break;
	case RADAR_MODE_NO_TERRAIN:
		{
			WScr = WZCOL_RADAR_BACKGROUND;
		}
		break;
	case NUM_RADAR_MODES:
		{
			assert(false);
		}
		break;
	}
	return WScr;
}

/** Draw the map tiles on the radar. */
static void DrawRadarTiles(WorldMapState& mapState)
{
	SDWORD	x, y;
	size_t radarBufferSize2 = radarBitmap.size_in_bytes();
	unsigned char* pRaderBuffer = radarBitmap.bmp_w();

	for (x = mapState.scroll.minX; x < mapState.scroll.maxX; x++)
	{
		for (y = mapState.scroll.minY; y < mapState.scroll.maxY; y++)
		{
			MAPTILE	*psTile = mapTile(mapState, x, y);
			size_t pixelStartPos = (radarTexWidth * (y - mapState.scroll.minY) + (x - mapState.scroll.minX)) * 4;

			ASSERT(pixelStartPos < radarBufferSize2, "Buffer overrun");
			if (y == mapState.scroll.minY || x == mapState.scroll.minX || y == mapState.scroll.maxY - 1 || x == mapState.scroll.maxX - 1)
			{
				pRaderBuffer[pixelStartPos] = WZCOL_BLACK.byte.r;
				pRaderBuffer[pixelStartPos + 1] = WZCOL_BLACK.byte.g;
				pRaderBuffer[pixelStartPos + 2] = WZCOL_BLACK.byte.b;
				pRaderBuffer[pixelStartPos + 3] = WZCOL_BLACK.byte.a;
				continue;
			}
			auto radarColor = appliedRadarColour(radarDrawMode, psTile);
			pRaderBuffer[pixelStartPos] = radarColor.byte.r;
			pRaderBuffer[pixelStartPos + 1] = radarColor.byte.g;
			pRaderBuffer[pixelStartPos + 2] = radarColor.byte.b;
			pRaderBuffer[pixelStartPos + 3] = radarColor.byte.a;
		}
	}
}

/** Draw the droids and structure positions on the radar. */
static void DrawRadarObjects(GameWorld& world)
{
	UBYTE				clan;
	PIELIGHT			playerCol;
	PIELIGHT			flashCol;
	memset(radarOverlayBuffer, 0, radarBufferSize);
	bool blinkState = (gameTime - lastBlink) / BLINK_HALF_INTERVAL;

	/* Show droids on map - go through all players */
	for (clan = 0; clan < MAX_PLAYERS; clan++)
	{
		//see if have to draw enemy/ally color
		if (bEnemyAllyRadarColor)
		{
			if (clan == selectedPlayer)
			{
				playerCol = colRadarMe;
			}
			else
			{
				playerCol = (selectedPlayer < MAX_PLAYERS && aiCheckAlliances(selectedPlayer, clan) ? colRadarAlly : colRadarEnemy);
			}
		}
		else
		{
			//original 8-color mode
			STATIC_ASSERT(MAX_PLAYERS <= ARRAY_SIZE(clanColours));
			playerCol = clanColours[getPlayerColour(clan)];
		}

		STATIC_ASSERT(MAX_PLAYERS <= ARRAY_SIZE(flashColours));
		flashCol = flashColours[getPlayerColour(clan)];

		/* Go through all droids */
		for (const DROID* psDroid : world.objects.droids[clan])
		{
			if (psDroid->pos.x < world_coord(world.map.scroll.minX) || psDroid->pos.y < world_coord(world.map.scroll.minY)
			    || psDroid->pos.x >= world_coord(world.map.scroll.maxX) || psDroid->pos.y >= world_coord(world.map.scroll.maxY))
			{
				continue;
			}
			if (psDroid->visibleForLocalDisplay()
			    || (bMultiPlayer && alliancesSharedVision(game.alliance)
					&& selectedPlayer < MAX_PLAYERS && aiCheckAlliances(selectedPlayer, psDroid->player)))
			{
				int	x = psDroid->pos.x / TILE_UNITS;
				int	y = psDroid->pos.y / TILE_UNITS;
				size_t	pos = (x - world.map.scroll.minX) + (y - world.map.scroll.minY) * radarTexWidth;

				ASSERT(pos * sizeof(*radarOverlayBuffer) < radarBufferSize, "Buffer overrun");
				if (clan == selectedPlayer && gameTime > HIT_NOTIFICATION && gameTime - psDroid->timeLastHit < HIT_NOTIFICATION)
				{
					if (psDroid->selected && !blinkState)
						radarOverlayBuffer[pos] = applyAlpha(flashCol, OVERLAY_OPACITY).rgba();
					else
						radarOverlayBuffer[pos] = flashCol.rgba();
				}
				else
				{
					if (psDroid->selected && !blinkState)
						radarOverlayBuffer[pos] = applyAlpha(playerCol, OVERLAY_OPACITY).rgba();
					else
						radarOverlayBuffer[pos] = playerCol.rgba();
				}
			}
		}
	}

	/* Do the same for structures */
	for (SDWORD x = world.map.scroll.minX; x < world.map.scroll.maxX; x++)
	{
		for (SDWORD y = world.map.scroll.minY; y < world.map.scroll.maxY; y++)
		{
			MAPTILE		*psTile = mapTile(world.map, x, y);
			STRUCTURE	*psStruct;
			size_t		pos = (x - world.map.scroll.minX) + (y - world.map.scroll.minY) * radarTexWidth;

			ASSERT(pos * sizeof(*radarOverlayBuffer) < radarBufferSize, "Buffer overrun");
			if (!TileHasStructure(psTile))
			{
				continue;
			}
			psStruct = (STRUCTURE *)psTile->psObject;
			clan = psStruct->player;

			//see if have to draw enemy/ally color
			if (bEnemyAllyRadarColor)
			{
				if (clan == selectedPlayer)
				{
					playerCol = colRadarMe;
				}
				else
				{
					playerCol = (selectedPlayer < MAX_PLAYERS && aiCheckAlliances(selectedPlayer, clan) ? colRadarAlly : colRadarEnemy);
				}
			}
			else
			{
				//original 8-color mode
				playerCol = clanColours[getPlayerColour(clan)];
			}
			flashCol = flashColours[getPlayerColour(clan)];

			if (psStruct->visibleForLocalDisplay()
			    || (bMultiPlayer && alliancesSharedVision(game.alliance)
			        && selectedPlayer < MAX_PLAYERS && aiCheckAlliances(selectedPlayer, psStruct->player)))
			{
				if (clan == selectedPlayer && gameTime > HIT_NOTIFICATION && gameTime - psStruct->timeLastHit < HIT_NOTIFICATION)
				{
					if (psStruct->player == selectedPlayer && psStruct->selected && !blinkState)
						radarOverlayBuffer[pos] = applyAlpha(flashCol, OVERLAY_OPACITY).rgba();
					else
						radarOverlayBuffer[pos] = flashCol.rgba();
				}
				else
				{
					if (psStruct->player == selectedPlayer && psStruct->selected && !blinkState)
						radarOverlayBuffer[pos] = applyAlpha(playerCol, OVERLAY_OPACITY).rgba();
					else
						radarOverlayBuffer[pos] = playerCol.rgba();
				}
			}
		}
	}
	if (gameTime - lastBlink >= BLINK_INTERVAL)
		lastBlink = gameTime;
}

static void applyMinimapOverlay()
{
	size_t radarTexCount = radarTexWidth * radarTexHeight;
	size_t radarBufferSize2 = radarBitmap.size_in_bytes();
	unsigned char* pRaderBuffer = radarBitmap.bmp_w();
	ASSERT(radarTexCount * static_cast<size_t>(radarBitmap.channels()) <= radarBufferSize2, "Buffer overrun");
	ASSERT(radarTexCount * static_cast<size_t>(radarBitmap.channels()) <= radarBufferSize, "Buffer overrun");
	for (size_t i = 0; i < radarTexCount; i++)
	{
		if (radarOverlayBuffer[i] == 0)
			continue;
		size_t pixelStartPos = (i * 4);
		PIELIGHT baseColor = pal_RGBA(pRaderBuffer[pixelStartPos], pRaderBuffer[pixelStartPos + 1], pRaderBuffer[pixelStartPos + 2], pRaderBuffer[pixelStartPos + 3]);
		PIELIGHT overColor = PLfromUDWORD(radarOverlayBuffer[i]);
		PIELIGHT mixedColor = mix(overColor, baseColor);
		pRaderBuffer[pixelStartPos] = mixedColor.byte.r;
		pRaderBuffer[pixelStartPos + 1] = mixedColor.byte.g;
		pRaderBuffer[pixelStartPos + 2] = mixedColor.byte.b;
		pRaderBuffer[pixelStartPos + 3] = mixedColor.byte.a;
	}
}

/** Rotate an array of 2d vectors about a given angle, also translates them after rotating. */
[[maybe_unused]] static void RotateVector2D(Vector3i *Vector, Vector3i *TVector, Vector3i *Pos, int Angle, int Count)
{
	int64_t Cos = iCos(Angle);
	int64_t Sin = iSin(Angle);
	int ox = 0;
	int oy = 0;
	int i;
	Vector3i *Vec = Vector;
	Vector3i *TVec = TVector;

	if (Pos)
	{
		ox = Pos->x;
		oy = Pos->y;
	}

	for (i = 0; i < Count; i++)
	{
		TVec->x = ((Vec->x * Cos + Vec->y * Sin) >> 16) + ox;
		TVec->y = ((Vec->y * Cos - Vec->x * Sin) >> 16) + oy;
		Vec++;
		TVec++;
	}
}

[[maybe_unused]] static SDWORD getDistanceAdjust()
{
	int dif = std::max<int>(static_cast<int>(MAXDISTANCE - getViewDistance()), 0);

	return dif / 100;
}

[[maybe_unused]] static SDWORD getLengthAdjust()
{
	const int pitch = static_cast<int>(360 - (playerPos.r.x / DEG_1));

	// Max at
	const int lookingDown = (0 - MIN_PLAYER_X_ANGLE);
	const int lookingFar = (0 - MAX_PLAYER_X_ANGLE);

	int dif = MAX(pitch - lookingFar, 0);
	if (dif > (lookingDown - lookingFar))
	{
		dif = (lookingDown - lookingFar);
	}

	return dif / 2;
}

/** Draws a Myth/FF7 style viewing window */
// MOD (17.11., Wunsch): Kameramarker auf der Minikarte - statt des weissen Sichtfenster-Trapezes ein dickes, rot leuchtendes QUADRAT
// um den Blickpunkt (Groesse folgt dem Zoom) mit einem kleinen Pfeil an der Vorderkante, der die Blickrichtung zeigt.
// Gezeichnet als eine Dreiecksfolge: weicher Schein aussen und innen, kraeftiger Rahmen, ganz leichte rote Fuellung, Pfeil.
static void setViewingWindow(const WorldMapState& mapState)
{
	float pixSizeH, pixSizeV;
	CalcRadarPixelSize(&pixSizeH, &pixSizeV);
	const float cx = playerPos.p.x * pixSizeH / TILE_UNITS - mapState.scroll.minX * pixSizeH;
	const float cy = playerPos.p.z * pixSizeV / TILE_UNITS - mapState.scroll.minY * pixSizeV;
	const float halfWorld = std::max(900.f, std::min(3600.f, getViewDistance() * 0.55f + 640.f));
	const float hx = std::max(6.f, halfWorld / TILE_UNITS * pixSizeH);
	const float hy = std::max(6.f, halfWorld / TILE_UNITS * pixSizeV);
	const float ang = static_cast<float>(playerPos.r.y) * (2.f * 3.14159265f / 65536.f);
	const float c = std::cos(ang), sn = std::sin(ang);
	const float pulse = 0.78f + 0.22f * std::sin(static_cast<float>(realTime % 1200u) * (2.f * 3.14159265f / 1200.f));
	const float T = 2.6f;      // Rahmenstaerke (Pixel)
	const float GLOW_OUT = 5.f, GLOW_IN = 3.f;

	std::vector<float> xy;
	std::vector<uint8_t> rgba;
	auto push = [&](float lx, float ly, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
		// gleiche Drehung wie das alte Sichtfenster (RotateVector2D)
		xy.push_back(lx * c + ly * sn + cx);
		xy.push_back(ly * c - lx * sn + cy);
		rgba.push_back(r); rgba.push_back(g); rgba.push_back(b); rgba.push_back(a);
	};
	auto repeatLast = [&]() {
		if (xy.empty()) { return; }
		const size_t n = xy.size();
		xy.push_back(xy[n - 2]); xy.push_back(xy[n - 1]);
		const size_t m = rgba.size();
		for (int k = 0; k < 4; ++k) { rgba.push_back(rgba[m - 4 + k]); }
	};
	static const float CX[5] = {-1.f, 1.f, 1.f, -1.f, -1.f};
	static const float CY[5] = {-1.f, -1.f, 1.f, 1.f, -1.f};
	// Ring zwischen zwei Quadraten (Abstand dA/dB zur Rahmenmitte, Pixel), je mit eigener Farbe/Deckkraft
	auto ring = [&](float dA, uint8_t aA, float dB, uint8_t aB, uint8_t r, uint8_t g, uint8_t b) {
		bool first = true;
		for (int k = 0; k < 5; ++k)
		{
			const float bx = CX[k] * std::max(0.5f, hx + dB), by = CY[k] * std::max(0.5f, hy + dB);
			const float ax = CX[k] * std::max(0.5f, hx + dA), ay = CY[k] * std::max(0.5f, hy + dA);
			if (first && !xy.empty()) { repeatLast(); push(bx, by, r, g, b, aB); }   // Uebergang (entartete Dreiecke)
			first = false;
			push(bx, by, r, g, b, aB);
			push(ax, ay, r, g, b, aA);
		}
	};
	const uint8_t glowA = static_cast<uint8_t>(150.f * pulse);
	// ganz leichte rote Fuellung
	{
		const float fx = std::max(0.5f, hx - T * 0.5f), fy = std::max(0.5f, hy - T * 0.5f);
		push(-fx, -fy, 255, 40, 30, 22);
		push(-fx, -fy, 255, 40, 30, 22);
		push(fx, -fy, 255, 40, 30, 22);
		push(-fx, fy, 255, 40, 30, 22);
		push(fx, fy, 255, 40, 30, 22);
	}
	ring(T * 0.5f, glowA, T * 0.5f + GLOW_OUT, 0, 255, 50, 35);       // Schein aussen
	ring(-T * 0.5f, glowA, -T * 0.5f - GLOW_IN, 0, 255, 50, 35);      // Schein innen
	ring(-T * 0.5f, 245, T * 0.5f, 245, 255, 36, 24);                 // Rahmen
	// Blickpfeil an der Vorderkante (vorn = -y vor der Drehung)
	const float baseY = -(hy + T * 0.5f);
	const float aw = 5.5f, al = 8.f;
	repeatLast();
	push(-aw - 2.5f, baseY + 1.f, 255, 60, 40, 0);
	push(-aw - 2.5f, baseY + 1.f, 255, 60, 40, 0);
	push(aw + 2.5f, baseY + 1.f, 255, 60, 40, 0);
	push(0.f, baseY - al - 4.f, 255, 60, 40, glowA);
	repeatLast();
	push(-aw, baseY + 0.5f, 255, 70, 40, 250);
	push(-aw, baseY + 0.5f, 255, 70, 40, 250);
	push(aw, baseY + 0.5f, 255, 70, 40, 250);
	push(0.f, baseY - al, 255, 120, 70, 255);

	pie_SetViewingMarker(xy.data(), rgba.data(), static_cast<int>(xy.size() / 2));
}

static void DrawRadarExtras(const glm::mat4 &modelViewProjectionMatrix)
{
	pie_DrawViewingWindow(modelViewProjectionMatrix);
	RenderWindowFrame(FRAME_RADAR, -1, -1, radarWidth + 2, radarHeight + 2, modelViewProjectionMatrix);
}

/** Does a screen coordinate lie within the radar area? */
bool CoordInRadar(int x, int y)
{
	Vector2f pos;
	pos.x = x - radarCenterX;
	pos.y = y - radarCenterY;
	if (rotateRadar)
	{
		pos = Vector2f_Rotate2f(pos, -playerPos.r.y);
	}
	pos.x += radarWidth / 2.f;
	pos.y += radarHeight / 2.f;

	if (pos.x < 0 || pos.y < 0 || pos.x >= radarWidth || pos.y >= radarHeight)
	{
		return false;
	}
	return true;
}

PIELIGHT radarTileColour(UDWORD tileNumber)   // MOD 17.11.: Kachelfarbe (Mittelwert) fuer den Orbit-Planetenboden
{
	return tileColours[tileNumber < MAX_TILES ? tileNumber : 0];
}

void radarColour(UDWORD tileNumber, uint8_t r, uint8_t g, uint8_t b)
{
	ASSERT_OR_RETURN(, tileNumber < MAX_TILES, "Invalid tile number: %u", tileNumber);
	tileColours[tileNumber].byte.r = r;
	tileColours[tileNumber].byte.g = g;
	tileColours[tileNumber].byte.b = b;
	tileColours[tileNumber].byte.a = 255;
}


// MARK: - RadarWidget

std::shared_ptr<WIDGET> getRadarWidget()
{
	return pRadarWidget;
}

RadarWidget::RadarWidget()
:  WIDGET()
{ }

#define WKEY_ORDER (getRightClickOrders()?WKEY_SECONDARY:WKEY_PRIMARY)
#define WKEY_SELECT (getRightClickOrders()?WKEY_PRIMARY:WKEY_SECONDARY)

/* How far the mouse has to move to start a drag */
#define DRAG_THRESHOLD	5

bool RadarWidget::capturesMouseDrag(WIDGET_KEY wkey)
{
	return (wkey == WKEY_SELECT);
}

void RadarWidget::mouseDragged(WIDGET_KEY wkey, W_CONTEXT *psStartContext, W_CONTEXT *psContext)
{
	if (wkey != WKEY_SELECT)
	{
		return;
	}

	if (!getRotActive())
	{
		if (!m_handlingDrag)
		{
			if (ABSDIF(psStartContext->mx, psContext->mx) > DRAG_THRESHOLD ||
				ABSDIF(psStartContext->my, psContext->my) > DRAG_THRESHOLD)
			{
				// start processing as drag
				m_handlingDrag = true;
			}
		}

		if (m_handlingDrag)
		{
			W_CONTEXT screenContext = psContext->convertToScreenContext();
			int x = screenContext.mx;
			int y = screenContext.my;
			int PosX, PosY;
			CalcRadarPosition(gameWorld.map, x, y, &PosX, &PosY);
			setViewPos(PosX, PosY, true);
			if (ctrlShiftDown())
			{
				playerPos.r.y = 0;
			}
		}
	}
}

void RadarWidget::display(int xOffset, int yOffset)
{
	if (!radarVisible()) { return; }

	gfx_api::context::get().debugStringMarker("Draw 3D scene - radar");
	drawRadar(gameWorld);
}

bool RadarWidget::hitTest(int x, int y) const
{
	// NOTE: CoordInRadar expects x, y in *screen* coordinates - this is _currently_ the case as a byproduct of how the radar widget is added to the UI screen
	return WIDGET::hitTest(x, y) && CoordInRadar(x, y);
}

void RadarWidget::clicked(W_CONTEXT *context, WIDGET_KEY key)
{
	int x = context->xOffset + context->mx;
	int y = context->yOffset + context->my;

	m_clickPosition[key] = Vector2i(x, y);
}

void RadarWidget::highlight(W_CONTEXT *)
{
	m_hasHighlight = true;
}

void RadarWidget::highlightLost()
{
	m_hasHighlight = false;
	for (auto& keyClickPos : m_clickPosition)
	{
		keyClickPos.reset();
	}
}

void RadarWidget::released(W_CONTEXT *context, WIDGET_KEY key)
{
	int PosX, PosY;
	int x = context->xOffset + context->mx;
	int y = context->yOffset + context->my;

	if (key == WKEY_ORDER)
	{
		if (m_clickPosition[WKEY_ORDER].has_value())
		{
			x = m_clickPosition[WKEY_ORDER].value().x;
			y = m_clickPosition[WKEY_ORDER].value().y;

			/* If we're tracking a droid, then cancel that */
			CalcRadarPosition(gameWorld.map, x, y, &PosX, &PosY);
			if (selectedPlayer < MAX_PLAYERS)
			{
				// MARKER
				// Send all droids to that location
				orderSelectedLoc(selectedPlayer, (PosX * TILE_UNITS) + TILE_UNITS / 2,
								 (PosY * TILE_UNITS) + TILE_UNITS / 2, ctrlShiftDown()); // ctrlShiftDown() = ctrl clicked a destination, add an order

			}
			CheckScrollLimits(gameWorld.map);
			audio_PlayTrack(ID_SOUND_MESSAGEEND);
		}
	}

	if (key == WKEY_SELECT && !m_handlingDrag)
	{
		if (m_clickPosition[WKEY_SELECT].has_value())
		{
			x = m_clickPosition[WKEY_SELECT].value().x;
			y = m_clickPosition[WKEY_SELECT].value().y;

			CalcRadarPosition(gameWorld.map, x, y, &PosX, &PosY);

			modbigmap::radarClicked(PosX, PosY);   // MOD (Wunsch 17.11. 18:48): Klick auf die Minikarte oeffnet die strategische Karte
			if (war_GetRadarJump())
			{
				/* Go instantly */
				setViewPos(PosX, PosY, true);
			}
			else
			{
				/* Pan to it */
				requestRadarTrack(PosX * TILE_UNITS, PosY * TILE_UNITS);
			}
		}
	}

	m_clickPosition[key].reset();
	m_handlingDrag = false;
}

bool isMouseOverRadar()
{
	if (!pRadarWidget) { return false; }
	return pRadarWidget->hasHighlight();
}

bool isRadarDragging()
{
	if (!pRadarWidget) { return false; }
	return pRadarWidget->isRadarDragging();
}
