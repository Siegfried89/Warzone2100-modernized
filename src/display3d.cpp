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
/**
 * @file display3d.c
 * Draws the 3D view.
 * Originally by Alex McLean & Jeremy Sallis, Pumpkin Studios, EIDOS INTERACTIVE
 */

#include "lib/framework/frame.h"
#include "lib/framework/math_ext.h"
#include "lib/framework/stdio_ext.h"

/* Includes direct access to render library */
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piedraw.h"
#include "lib/ivis_opengl/pietypes.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piemode.h"
#include "lib/framework/fixedpoint.h"
#include "lib/ivis_opengl/piefunc.h"
#include "lib/framework/physfs_ext.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/ivis_opengl/smaa_luts.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/pieclip.h"

#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "lib/netplay/netplay.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/transform.hpp>
#include <glm/gtx/matrix_interpolation.hpp>

#include "loop.h"
#include "atmos.h"
#include "map.h"
#include "ai.h"   // MOD (21.11.): Eckmauern - Buendnis der Nachbarmauer
#include "modfacing.h"
#include <unordered_map>
#include "droid.h"
#include "group.h"
#include "visibility.h"
#include "geometry.h"
#include "messagedef.h"
#include "miscimd.h"
#include "effects.h"
#include "edit3d.h"
#include "feature.h"
#include "mapgrid.h"
#include "modfort.h"
#include "modterrain.h"
#include "keybind.h"
#include "main.h"
#include "hci.h"
#include "display.h"
#include "intdisplay.h"
#include "radar.h"
#include "display3d.h"
#include "modconcrete.h"
#include "uiobjectrefs.h"
#include "display3d_render_graph.h"
#include "display3d_render_internal.h"
#include "dynamicresolution.h"
#include "lighting.h"
#include "console.h"
#include "projectile.h"
#include "bucket3d.h"
#include "message.h"
#include "component.h"
#include "warcam.h"
#include "order.h"
#include "scores.h"
#include "multiplay.h"
#include "advvis.h"
#include "cmddroid.h"
#include "effectlights.h"
#include "terrain.h"
#include "profiling.h"
#include "warzoneconfig.h"
#include "ssao.h"
#include "ssr.h"
#include "range_rings.h"
#include "modgrid.h"
#include "modpaint.h"
#include "modtunnel.h"
#include "modferry.h"
#include "modrail.h"   // MOD (21.11.): Gleise
#include "modbridge.h"   // MOD (22.11.): Bruecken
#include "modroad.h"   // MOD (22.11.): Strassen
#include "modheadlights.h"
#include "modnightlights.h"
#include "modrocket.h"
#include "modnuke.h"
#include "modicbm.h"
#include "modsat.h"
#include "modrailbeam.h"
#include "modmines.h"
#include "modtactical.h"
#include "modbigmap.h"
#include "modoil.h"
#include "modcrates.h"
#include "modshield.h"
#include "modshockwave.h"
#include "modapc.h"
#include "transporter.h"
#include "modtracks.h"
#include "modrecover.h"
#include "modnestcrew.h"
#include "modwreck.h"
#include "modgroundfire.h"
#include "modbuild.h"
#include "modammo.h"
#include "modfuel.h"
#include "modweather.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "scene_effect_surfaces.h"
#include "multistat.h"
#include "animation.h"
#include "faction.h"
#include "wzcrashhandlingproviders.h"
#include "shadowcascades.h"
#include "profiling.h"
#include "game_world.h"
#include "corridor_map.h"
#include "perfcounters.h"
#include "cameratrace.h"
#include "modpick.h"
#include "modoilui.h"


/********************  Prototypes  ********************/

static void displayDelivPoints(const glm::mat4& viewMatrix, const glm::mat4 &perspectiveViewMatrix);
static void displayProximityMsgs(const glm::mat4& viewMatrix, const glm::mat4 &perspectiveViewMatrix);
static void displayDynamicObjects(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix);
static void displayStaticObjects(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix);
static void displayFeatures(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix);
static void displayCraterPiles(const glm::mat4 &viewMatrix);
static void displayOrbitAsteroids(const glm::mat4 &viewMatrix);
static UDWORD	getTargettingGfx();
static void	drawGroupNumber(BASE_OBJECT *psObject);
static void	trackHeight(float desiredHeight);
static void	updateCameraForFrame();
static void	renderSurroundings(const glm::mat4& projectionMatrix, const glm::mat4 &skyboxViewMatrix);
static void	locateMouse();
static bool	renderWallSection(STRUCTURE *psStructure, const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix);
static void	drawDragBox();
static void	calcFlagPosScreenCoords(SDWORD *pX, SDWORD *pY, SDWORD *pR, const glm::mat4 &perspectiveViewModelMatrix);
static void	drawTiles(iView *player, LightingData& lightData, LightMap& lightmap, ILightingManager& lightManager);
static void	display3DProjectiles(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix);
static void	emitProjectileLights(LightingData &lightData);
static void	drawDroidAndStructureSelections();
static void	drawDroidSelections();
static void	drawStructureSelections();
static void displayBlueprints(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix);
static void	processSensorTarget();
static void	processDestinationTarget();
static bool	eitherSelected(DROID *psDroid);
static void	structureEffects();
static void	addConstructionLine(DROID *psDroid, STRUCTURE *psStructure, const glm::mat4 &viewMatrix);
static void	doConstructionLines(const glm::mat4 &viewMatrix);
static bool s_ring3DActive = false;   // MOD (18.11.): Auswahlring als 3D-Ring am Boden (modDrawSelectionRings3D)
static void	drawDroidCmndNo(DROID *psDroid);
static void	drawDroidOrder(const DROID *psDroid);
static void	drawDroidRank(DROID *psDroid);
static void	drawDroidSensorLock(DROID *psDroid);
static	int	calcAverageTerrainHeight(WorldMapState& mapState, int tileX, int tileZ);
static	float	calculateCameraHeight(float averageTerrainHeight);
static void	updatePlayerAverageCentreTerrainHeight();
bool	doWeDrawProximitys();
static PIELIGHT getBlueprintColour(STRUCT_STATES state);

static void NetworkDisplayImage(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset);
extern bool writeGameInfo(const char *pFileName); // Used to help debug issues when we have fatal errors & crash handler testing.

static WzText txtLevelName;
static WzText txtDebugStatus;
static WzText txtCurrentTime;
static WzText txtShowFPS;
static WzText txtUnits;
// show Samples text
static WzText txtShowSamples_Que;
static WzText txtShowSamples_Lst;
static WzText txtShowSamples_Act;
// show Orders text
static WzText txtShowOrders;
static WzText txtModTime;       // MOD: Zeitfaktor-Anzeige (beim Beenden freigeben, sonst Absturz nach dem Abbau der Grafik)
static WzText modTxtCmdLabel;   // MOD: Beschriftung "Area Bombardment: Attack Area" / "Fortify" am Mauszeiger
// show Droid visible/draw counts text
static WzText droidText;

static gfx_api::buffer* pScreenTriangleVBO = nullptr;
/// Lighting managers, owned here and kept across frames (see the selection in drawTiles' caller)
static std::unique_ptr<renderingNew::LightingManager> perPixelLightingManager;
static std::unique_ptr<rendering1999::LightingManager> lightmapLightingManager;


/********************  Variables  ********************/
// Should be cleaned up properly and be put in structures.

// Initialised at start of drawTiles().
// In model coordinates where x is east, y is up and z is north, rather than world coordinates where x is east, y is south and z is up.
// To get the real camera position, still need to add Vector3i(player.p.x, 0, player.p.z).
static Vector3i actualCameraPosition;

static bool	bDrawProximitys = true;
bool	godMode;
bool	showGateways = false;
bool	showPath = false;
bool	showCorridors = false;

// Skybox data
static float wind = 0.0f;
static float windSpeed = 0.0f;
static float skybox_baseWindSpeed = 0.0f; // MOD: Wolkenzug der Karte (ohne Unwetter-Zuschlag), wird gespeichert
static float skybox_scale = 10000.0f;
static std::string skybox_page; // current skybox texture (for save/restore)

//
static bool bDrawTerrainShadows = true;

/// When to display HP bars
UWORD barMode;

/// Have we made a selection by clicking the mouse? - used for dragging etc
bool	selectAttempt = false;

/// Vectors that hold the player and camera directions and positions
iView	playerPos;

/// How far away are we from the terrain
static float distance;
static bool bIsCloseDistance = false;

/// Stores the screen coordinates of the transformed terrain tiles
static Vector3i tileScreenInfo[VISIBLE_YTILES + 1][VISIBLE_XTILES + 1];
static bool tileScreenVisible[VISIBLE_YTILES + 1][VISIBLE_XTILES + 1] = {false};

/// Records the present X and Y values for the current mouse tile (in tiles)
SDWORD mouseTileX, mouseTileY;
Vector2i mousePos(0, 0);

/// Do we want the radar to be rendered
bool radarOnScreen = true;
bool radarPermitted = true;

bool radarVisible()
{
	if (radarOnScreen && radarPermitted && getWidgetsStatus())
	{
		return true;
	}
	else
	{
		return false;
	}
}

/// Show unit/building gun/sensor range
bool rangeOnScreen = false;  // For now, most likely will change later!  -Q 5-10-05   A very nice effect - Per

void setRangeOnScreen(bool enabled)
{
	if (rangeOnScreen == enabled)
	{
		return;
	}
	rangeOnScreen = enabled;
	applySceneEffectSurfaces();
}

/// Tactical UI: show/hide target origin icon
bool tuiTargetOrigin = false;

/// Temporary values for the terrain render - center of grid to be rendered
static int playerXTile, playerZTile;

/// The cached value of frameGetFrameNumber()
static UDWORD currentGameFrame;
/// The box used for multiple selection - present screen coordinates
static QUAD dragQuad;

/** Number of tiles visible
 * \todo This should become dynamic! (A function of resolution, angle and zoom maybe.)
 */
const Vector2i visibleTiles(VISIBLE_XTILES, VISIBLE_YTILES);

/// The tile we use for drawing the bottom of a body of water
static unsigned int underwaterTile = WATER_TILE;
/** The tile we use for drawing rubble
 * \note Unused.
 */
static unsigned int rubbleTile = BLOCKING_RUBBLE_TILE;

/** Show how many frames we are rendering per second
 * default OFF, turn ON via console command 'showfps'
 */
bool showFPS = false;       //
/** Show how many samples we are rendering per second
 * default OFF, turn ON via console command 'showsamples'
 */
bool showUNITCOUNT = false;
/** Show how many kills/deaths (produced units) made
 * default OFF, turn ON via console command 'showunits'
 */
bool showSAMPLES = false;
/**  Show the current selected units order / action
 *  default OFF, turn ON via console command 'showorders'
 */
bool showORDERS = false;
/**  Show the drawn/undrawn counts for droids
  * default OFF, turn ON by flipping it here
  */
bool showDROIDcounts = false;

/**  Speed of blueprints animation (moving from one tile to another)
  * default 20, change in config
  */
int BlueprintTrackAnimationSpeed = 20;

/** When we have a connection issue, we will flash a message on screen
*/
static const char *errorWaiting = nullptr;
static uint32_t lastErrorTime = 0;

#define NETWORK_FORM_ID 0xFAAA
#define NETWORK_BUT_ID 0xFAAB
/** When enabled, this causes a segfault in the game, to test out the crash handler */
bool CauseCrash = false;

/** tells us in realtime, what droid is doing (order / action)
*/
char DROIDDOING[512];

/// Geometric offset which will be passed to pie_SetGeometricOffset
static const int geoOffset = 192;

/// The average terrain height for the center of the area the camera is looking at
static float averageCentreTerrainHeight;

/// Set by init3DView() to start the next height update at the target
static bool trackHeightStartAtTarget = true;

/** The time at which a sensor target was last assigned
 * Used to draw a visual effect.
 */
static UDWORD	lastTargetAssignation = 0;
/** The time at which an order concerning a destination was last given
 * Used to draw a visual effect.
 */
static UDWORD	lastDestAssignation = 0;
static bool	bSensorTargetting = false;
static bool	bDestTargetting = false;
static BASE_OBJECT *psSensorObj = nullptr;
static UDWORD	destTargetX, destTargetY;
static UDWORD	destTileX = 0, destTileY = 0;

struct Blueprint
{
	Blueprint()
		: stats(nullptr)
		, pos({0,0,0})
		, dir(0)
		, index(0)
		, state(SS_BLUEPRINT_INVALID)
		, player(selectedPlayer)
	{}
	Blueprint(STRUCTURE_STATS const *stats, Vector3i pos, uint16_t dir, uint32_t index, STRUCT_STATES state, uint8_t player)
		: stats(stats)
		, pos(pos)
		, dir(dir)
		, index(index)
		, state(state)
		, player(player)
	{}
	int compare(Blueprint const &b) const
	{
		if (stats->ref != b.stats->ref)
		{
			return stats->ref < b.stats->ref ? -1 : 1;
		}
		if (pos.x != b.pos.x)
		{
			return pos.x < b.pos.x ? -1 : 1;
		}
		if (pos.y != b.pos.y)
		{
			return pos.y < b.pos.y ? -1 : 1;
		}
		if (pos.z != b.pos.z)
		{
			return pos.z < b.pos.z ? -1 : 1;
		}
		if (dir != b.dir)
		{
			return dir < b.dir ? -1 : 1;
		}
		if (index != b.index)
		{
			return index < b.index ? -1 : 1;
		}
		if (state != b.state)
		{
			return state < b.state ? -1 : 1;
		}
		return 0;
	}
	bool operator <(Blueprint const &b) const
	{
		return compare(b) < 0;
	}
	bool operator ==(Blueprint const &b) const
	{
		return compare(b) == 0;
	}
	optional<STRUCTURE> buildBlueprint() const
	{
		return ::buildBlueprint(gameWorld.map, stats, pos, dir, index, state, player);
	}
	void renderBlueprint(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix) const
	{
		if (clipXY(pos.x, pos.y))
		{
			auto optStruct = buildBlueprint();
			ASSERT_OR_RETURN(, optStruct.has_value(), "buildBlueprint returned nullopt");
			renderStructure(&*optStruct, viewMatrix, perspectiveViewMatrix);
		}
	}

	STRUCTURE_STATS const *stats;
	Vector3i pos;
	uint16_t dir;
	uint32_t index;
	STRUCT_STATES state;
	uint8_t player;
};

static std::vector<Blueprint> blueprints;

#define	TARGET_TO_SENSOR_TIME	((4*(GAME_TICKS_PER_SEC))/5)
#define	DEST_TARGET_TIME	(GAME_TICKS_PER_SEC/4)

/// The distance the selection box will pulse
static const float BOX_PULSE_SIZE = 30;

/// the opacity at which building blueprints will be drawn
static const int BLUEPRINT_OPACITY = 120;

/********************  Functions  ********************/

void display3dScreenSizeDidChange(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth, unsigned int newHeight)
{
	if (psWScreen == nullptr) return;
	resizeRadar(gameWorld.map); // recalculate radar position
}

static void queueDroidPowerBarsRects(DROID *psDroid, bool drawBox, BatchedMultiRectRenderer& batchedMultiRectRenderer, size_t rectGroup);
static void modBarColors(int player, PIELIGHT &col, PIELIGHT &colShadow);   // MOD
static void queueDroidEnemyHealthBarsRects(DROID *psDroid, BatchedMultiRectRenderer& batchedMultiRectRenderer, size_t rectGroup);
static void queueWeaponReloadBarRects(BASE_OBJECT *psObj, WEAPON *psWeap, int weapon_slot, BatchedMultiRectRenderer& batchedMultiRectRenderer, size_t rectGroup);
static void queueStructureHealth(STRUCTURE *psStruct, BatchedMultiRectRenderer& batchedMultiRectRenderer, size_t rectGroup);
static void queueStructureBuildProgress(STRUCTURE *psStruct, BatchedMultiRectRenderer& batchedMultiRectRenderer, size_t rectGroup);
static void drawStructureTargetOriginIcon(STRUCTURE *psStruct, int weapon_slot);

class BatchedObjectStatusRenderer
{
public:
	void addDroidSelectionsToRender(uint32_t player, BASE_OBJECT *psClickedOn, bool bMouseOverOwnDroid)
	{
		bool bDrawAll = barMode == BAR_DROIDS || barMode == BAR_DROIDS_AND_STRUCTURES;

		// first, for all droids, queue the various rects
		for (DROID *psDroid : gameWorld.objects.droids[player])
		{
			if (psDroid->sDisplay.frameNumber == currentGameFrame)
			{
				/* If it's selected and on screen or it's the one the mouse is over */
				if (bDrawAll ||
					eitherSelected(psDroid) ||
					(bMouseOverOwnDroid && psDroid == (DROID *) psClickedOn) ||
					droidUnderRepair(psDroid)
					)
				{
					queueDroidPowerBarsRects(psDroid, psDroid->selected, batchedMultiRectRenderer, 0);

					for (unsigned i = 0; i < psDroid->numWeaps; i++)
					{
						queueWeaponReloadBarRects((BASE_OBJECT *)psDroid, &psDroid->asWeaps[i], i, batchedMultiRectRenderer, 1);
					}

					droidsToDraw.push_back(psDroid);
				}
			}
		}
	}

	void addEnemyDroidHealthToRender(DROID *psDroid)
	{
		queueDroidEnemyHealthBarsRects(psDroid, batchedMultiRectRenderer, 4);
	}

	void addEnemyStructureHealthToRender(STRUCTURE* psStruct)
	{
		queueStructureHealth(psStruct, batchedMultiRectRenderer, 4);
		if (psStruct->status == SS_BEING_BUILT)
		{
			queueStructureBuildProgress(psStruct, batchedMultiRectRenderer, 4);
		}
	}

	void addStructureSelectionsToRender(uint32_t player, BASE_OBJECT *psClickedOn, bool bMouseOverOwnStructure)
	{
		/* Go thru' all the buildings */
		for (STRUCTURE *psStruct : gameWorld.objects.structures[player])
		{
			if (psStruct->sDisplay.frameNumber == currentGameFrame)
			{
				/* If it's selected */
				if (psStruct->selected ||
					(barMode == BAR_DROIDS_AND_STRUCTURES && psStruct->pStructureType->type != REF_WALL && psStruct->pStructureType->type != REF_WALLCORNER) ||
					(bMouseOverOwnStructure && psStruct == (STRUCTURE *)psClickedOn)
				   )
				{
					queueStructureHealth(psStruct, batchedMultiRectRenderer, 2);

					for (unsigned i = 0; i < psStruct->numWeaps; i++)
					{
						queueWeaponReloadBarRects((BASE_OBJECT *)psStruct, &psStruct->asWeaps[i], i, batchedMultiRectRenderer, 3);
						// drawStructureTargetOriginIcon(psStruct, i); // in the future, use instanced rendering here? (for now, add to structsToDraw below and handle later)
					}

					structsToDraw.push_back(psStruct);
				}

				if (psStruct->status == SS_BEING_BUILT)
				{
					queueStructureBuildProgress(psStruct, batchedMultiRectRenderer, 2);
				}
			}
		}
	}

	void addStructureTargetingGfxToRender()
	{
		// Future todo: make this instanced as well?
		renderStructureTargetingGfx = true;
	}

	void drawAllSelections()
	{
		// droid selection / health draw
		// 1. box + power bars rects
		batchedMultiRectRenderer.drawRects(0);
		// 2. droid rank images
		for (DROID *psDroid : droidsToDraw)
		{
			/* Write the droid rank out */
			if ((psDroid->sDisplay.screenX + psDroid->sDisplay.screenR) > 0
				&&	(psDroid->sDisplay.screenX - psDroid->sDisplay.screenR) < pie_GetVideoBufferWidth()
				&&	(psDroid->sDisplay.screenY + psDroid->sDisplay.screenR) > 0
				&&	(psDroid->sDisplay.screenY - psDroid->sDisplay.screenR) < pie_GetVideoBufferHeight())
			{
				drawDroidRank(psDroid);
				drawDroidSensorLock(psDroid);
				drawDroidCmndNo(psDroid);
				drawGroupNumber(psDroid);
			}
		}
		// 3. weapon reload bar
		batchedMultiRectRenderer.drawRects(1);

		// structure selection / health draw
		// 1. structure health / build progress rects
		batchedMultiRectRenderer.drawRects(2);
		// 2. structure target origin icons
		for (STRUCTURE *psStruct : structsToDraw)
		{
			for (unsigned i = 0; i < psStruct->numWeaps; i++)
			{
				drawStructureTargetOriginIcon(psStruct, i);
			}
			if (psStruct->isFactory())
			{
				drawGroupNumber(psStruct);
			}
		}
		// 3. structure weapon reload bars
		batchedMultiRectRenderer.drawRects(3);

		if (renderStructureTargetingGfx)
		{
			SDWORD scrX, scrY;
			for (uint32_t i = 0; i < MAX_PLAYERS; i++)
			{
				for (const STRUCTURE* psStruct : gameWorld.objects.structures[i])
				{
					/* If it's targetted and on-screen */
					if (psStruct->flags.test(OBJECT_FLAG_TARGETED)
						&& psStruct->sDisplay.frameNumber == currentGameFrame)
					{
						scrX = psStruct->sDisplay.screenX;
						scrY = psStruct->sDisplay.screenY;
						iV_DrawImage(IntImages, getTargettingGfx(), scrX, scrY);
					}
				}
			}
		}

		// draw last rects
		batchedMultiRectRenderer.drawRects(4);
	}

	void initialize()
	{
		batchedMultiRectRenderer.initialize();
		// 2 for droid selection rect groups, + 2 for structure selection rect groups, + 1 for "mouse over" rect groups (drawn last)
		batchedMultiRectRenderer.resizeRectGroups(5);
	}

	void clear()
	{
		droidsToDraw.clear();
		structsToDraw.clear();
		renderStructureTargetingGfx = false;
		batchedMultiRectRenderer.clear();
	}

	void reset()
	{
		clear();
		batchedMultiRectRenderer.reset();
	}
private:
	std::vector<DROID *> droidsToDraw;
	std::vector<STRUCTURE *> structsToDraw;
	bool renderStructureTargetingGfx = false;
	BatchedMultiRectRenderer batchedMultiRectRenderer;
};

static BatchedObjectStatusRenderer batchedObjectStatusRenderer;

float interpolateAngleDegrees(int a, int b, float t)
{
	if(a > 180)
	{
		a -= 360;
	}
	if(b > 180)
	{
		b -= 360;
	}

	float d = b - a;

	return a + d * t;
}

// Berechnet die Animationsmatrix einer einzelnen Ebene; false = Ebene in diesem Bild ausgeblendet
static bool levelAnimMatrix(const iIMDShape *lvl, UDWORD timeAnimationStarted, glm::mat4 &modelMatrix)
{
	if (!lvl->objanimframes)
	{
		return true;
	}
	int elapsed = graphicsTime - timeAnimationStarted;
	if (elapsed < 0)
	{
		elapsed = 0; // Animation hasn't started yet.
	}

	const int frame = (elapsed / lvl->objanimtime) % lvl->objanimframes;
	ASSERT(frame < lvl->objanimframes, "Bad index %d >= %d", frame, lvl->objanimframes);

	const ANIMFRAME &state = lvl->objanimdata.at(frame);

	if (state.scale.x == -1.0f) // disabled frame, for implementing key frame animation
	{
		return false;
	}

	if (lvl->interpolate == 1)
	{
		const float frameFraction = static_cast<float>(fmod(elapsed / (float)lvl->objanimtime, lvl->objanimframes) - frame);
		const int nextFrame = (frame + 1) % lvl->objanimframes;
		const ANIMFRAME &nextState = lvl->objanimdata.at(nextFrame);

		modelMatrix *=
			glm::interpolate(glm::translate(glm::vec3(state.pos)), glm::translate(glm::vec3(nextState.pos)), frameFraction) *
			glm::rotate(RADIANS(interpolateAngleDegrees(state.rot.pitch / DEG(1), nextState.rot.pitch / DEG(1), frameFraction)), glm::vec3(1.f, 0.f, 0.f)) *
			glm::rotate(RADIANS(interpolateAngleDegrees(state.rot.direction / DEG(1), nextState.rot.direction / DEG(1), frameFraction)), glm::vec3(0.f, 1.f, 0.f)) *
			glm::rotate(RADIANS(interpolateAngleDegrees(state.rot.roll / DEG(1), nextState.rot.roll / DEG(1), frameFraction)), glm::vec3(0.f, 0.f, 1.f)) *
			glm::scale(state.scale);
	}
	else
	{
		modelMatrix *= glm::translate(glm::vec3(state.pos)) *
			glm::rotate(UNDEG(state.rot.pitch), glm::vec3(1.f, 0.f, 0.f)) *
			glm::rotate(UNDEG(state.rot.direction), glm::vec3(0.f, 1.f, 0.f)) *
			glm::rotate(UNDEG(state.rot.roll), glm::vec3(0.f, 0.f, 1.f)) *
			glm::scale(state.scale);
	}
	return true;
}

bool drawShape(const iIMDShape *strImd, UDWORD timeAnimationStarted, int colour, PIELIGHT buildingBrightness, int pieFlag, int pieFlagData, const glm::mat4& modelMatrix, const glm::mat4& viewMatrix, float stretchDepth)
{
	glm::mat4 modifiedModelMatrix = modelMatrix;
	int animFrame = 0; // for texture animation
	if (strImd->numFrames > 0)	// Calculate an animation frame
	{
		animFrame = getModularScaledGraphicsTime(strImd->animInterval, strImd->numFrames);
	}

	// Skelett-Animation (Combots): hat die zweite Ebene eine eigene Animation, werden ALLE Ebenen
	// gezeichnet, jede mit ihrer eigenen Animationsmatrix (jedes Gelenk ist eine Ebene).
	if (strImd->next && strImd->next->objanimframes > 0)
	{
		bool drawn = false;
		for (const iIMDShape *lvl = strImd; lvl != nullptr; lvl = lvl->next.get())
		{
			glm::mat4 levelMatrix = modelMatrix;
			if (!levelAnimMatrix(lvl, timeAnimationStarted, levelMatrix))
			{
				continue;
			}
			drawn |= pie_Draw3DShape(lvl, animFrame, colour, buildingBrightness, pieFlag, pieFlagData, levelMatrix, viewMatrix, stretchDepth, true);
		}
		return drawn;
	}

	if (!levelAnimMatrix(strImd, timeAnimationStarted, modifiedModelMatrix))
	{
		return false;
	}

	return pie_Draw3DShape(strImd, animFrame, colour, buildingBrightness, pieFlag, pieFlagData, modifiedModelMatrix, viewMatrix, stretchDepth, true);
}

static void setScreenDispWithPerspective(SCREEN_DISP_DATA *sDisplay, const glm::mat4 &perspectiveViewModelMatrix)
{
	Vector3i zero(0, 0, 0);
	Vector2i s(0, 0);

	pie_RotateProjectWithPerspective(&zero, perspectiveViewModelMatrix, &s);
	sDisplay->screenX = s.x;
	sDisplay->screenY = s.y;
}

void setSkyBox(const char *page, float mywind, float myscale)
{
	windSpeed = mywind;
	skybox_baseWindSpeed = mywind;
	wind = 0.0f;
	skybox_scale = myscale;
	skybox_page = page;
	pie_Skybox_Texture(page);
	modweather::onSkybox(page); // MOD: Grundhimmel merken, Nacht erkennen
}

// MOD: Wetter legt zeitweise einen anderen Himmel auf (Unwetter) bzw. beschleunigt die Wolken; Grundhimmel bleibt unveraendert
void modSwapSkyBox(const char *page, float extraWindSpeed)
{
	windSpeed = skybox_baseWindSpeed + extraWindSpeed;
	if (page != nullptr && skybox_page != page)
	{
		skybox_page = page;
		pie_Skybox_Texture(page);
	}
}

// Accessors for the current skybox parameters (used by GameState presentation serialization).
const std::string &getCurrentSkyboxPage()
{
	// MOD: gespeichert wird der Himmel der Karte, nicht der gerade aufgelegte Unwetterhimmel
	static std::string baseCopy;
	baseCopy = modweather::baseSkyPage(skybox_page.c_str());
	return baseCopy;
}

float getCurrentSkyboxWindSpeed()
{
	return skybox_baseWindSpeed;
}

float getCurrentSkyboxScale()
{
	return skybox_scale;
}

float getCurrentSkyboxWindAngle()
{
	return wind;
}

static inline void rotateSomething(int &x, int &y, uint16_t angle)
{
	int64_t cra = iCos(angle), sra = iSin(angle);
	int newX = (x * cra - y * sra) >> 16;
	int newY = (x * sra + y * cra) >> 16;
	x = newX;
	y = newY;
}

static Blueprint getTileBlueprint(int mapX, int mapY)
{
	Vector2i mouse(world_coord(mapX) + TILE_UNITS / 2, world_coord(mapY) + TILE_UNITS / 2);

	for (std::vector<Blueprint>::const_iterator blueprint = blueprints.begin(); blueprint != blueprints.end(); ++blueprint)
	{
		const Vector2i size = blueprint->stats->size(blueprint->dir) * TILE_UNITS;
		if (abs(mouse.x - blueprint->pos.x) < size.x / 2 && abs(mouse.y - blueprint->pos.y) < size.y / 2)
		{
			return *blueprint;
		}
	}

	return Blueprint(nullptr, Vector3i(), 0, 0, SS_BEING_BUILT, selectedPlayer);
}

STRUCTURE *getTileBlueprintStructure(int mapX, int mapY)
{
	static optional<STRUCTURE> optStruct;

	Blueprint blueprint = getTileBlueprint(mapX, mapY);
	if (blueprint.state == SS_BLUEPRINT_PLANNED)
	{
		if (optStruct)
		{
			// Delete previously returned structure, if any.
			optStruct.reset();
		}
		auto b = blueprint.buildBlueprint();
		// Don't use `operator=(optional&&)` to avoid declaring custom copy/move-assignment operators
		// for `STRUCTURE` (more specifically, for `SIMPLE_OBJECT`, which has some const-qualified data members).
		if (b)
		{
			optStruct.emplace(*b);
		}
		else
		{
			optStruct.reset();
		}
		return optStruct.has_value() ? &optStruct.value() : nullptr;  // This blueprint was clicked on.
	}

	return nullptr;
}

STRUCTURE_STATS const *getTileBlueprintStats(int mapX, int mapY)
{
	return getTileBlueprint(mapX, mapY).stats;
}

bool anyBlueprintTooClose(STRUCTURE_STATS const *stats, Vector2i pos, uint16_t dir)
{
	for (std::vector<Blueprint>::const_iterator blueprint = blueprints.begin(); blueprint != blueprints.end(); ++blueprint)
	{
		if ((blueprint->state == SS_BLUEPRINT_PLANNED || blueprint->state == SS_BLUEPRINT_PLANNED_BY_ALLY)
		    && isBlueprintTooClose(stats, pos, dir, blueprint->stats, blueprint->pos, blueprint->dir))
		{
			return true;
		}
	}
	return false;
}

void clearBlueprints()
{
	blueprints.clear();
}

static PIELIGHT selectionBrightness()
{
	int brightVar;
	if (!gamePaused())
	{
		brightVar = getModularScaledGraphicsTime(990, 110);
		if (brightVar > 55)
		{
			brightVar = 110 - brightVar;
		}
	}
	else
	{
		brightVar = 55;
	}
	return pal_SetBrightness(200 + brightVar);
}

static PIELIGHT structureBrightness(STRUCTURE *psStructure)
{
	PIELIGHT buildingBrightness;

	if (psStructure->isBlueprint())
	{
		buildingBrightness = getBlueprintColour(psStructure->status);
	}
	else
	{
		buildingBrightness = pal_SetBrightness(static_cast<UBYTE>(200 - 100 / 65536.f * getStructureDamage(psStructure)));

		/* If it's selected, then it's brighter */
		if (psStructure->selected)
		{
			buildingBrightness = selectionBrightness();
		}
		if (!getRevealStatus())
		{
			buildingBrightness = pal_SetBrightness(avGetObjLightLevel((BASE_OBJECT *)psStructure, buildingBrightness.byte.r));
		}
		if (!hasSensorOnTile(mapTile(gameWorld.map, map_coord(psStructure->pos.x), map_coord(psStructure->pos.y)), selectedPlayer))
		{
			buildingBrightness.byte.r /= 2;
			buildingBrightness.byte.g /= 2;
			buildingBrightness.byte.b /= 2;
		}
	}
	return buildingBrightness;
}


/// Show all droid movement parts by displaying an explosion at every step
static void showDroidPaths(const GameWorld& world)
{
	if ((graphicsTime / 250 % 2) != 0)
	{
		return;
	}

	if (selectedPlayer >= MAX_PLAYERS)
	{
		return; // no-op for now
	}

	for (const DROID *psDroid : world.objects.droids[selectedPlayer])
	{
		if (psDroid->selected && psDroid->sMove.Status != MOVEINACTIVE)
		{
			const int len = psDroid->sMove.asPath.size();
			for (int i = std::max(psDroid->sMove.pathIndex - 1, 0); i < len; i++)
			{
				Vector3i pos;

				ASSERT(worldOnMap(world.map, psDroid->sMove.asPath[i].x, psDroid->sMove.asPath[i].y), "Path off map!");
				pos.x = psDroid->sMove.asPath[i].x;
				pos.z = psDroid->sMove.asPath[i].y;
				pos.y = map_Height(world.map, pos.x, pos.z) + 16;

				effectGiveAuxVar(80);
				addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER, false, nullptr, 0);
			}
		}
	}
}

/// Marks detected corridor centerlines in the world, a marker at each centerline
/// point and a larger one at each mouth. Placed at the exact tile-center world
/// coordinates, so it shows the true geometry rather than the half-tile-shifted
/// lightmap. Only points near the camera are drawn, so a large map stays cheap.
static void showCorridorCenterlines(const GameWorld& world)
{
	if ((graphicsTime / 250 % 2) != 0)
	{
		return;
	}
	const CorridorMap *cmap = world.map.corridors.get();
	if (cmap == nullptr)
	{
		return;
	}
	const Vector2i cam(playerPos.p.x, playerPos.p.z);
	const int64_t cullSq = static_cast<int64_t>(30 * DIST_TILE) * static_cast<int64_t>(30 * DIST_TILE);
	for (const Corridor &c : cmap->corridors)
	{
		const size_t last = c.centerline.size() - 1;
		for (size_t i = 0; i < c.centerline.size(); ++i)
		{
			const Vector2i w = c.centerline[i];
			const int64_t dx = w.x - cam.x, dy = w.y - cam.y;
			if (dx * dx + dy * dy > cullSq || !worldOnMap(world.map, w.x, w.y))
			{
				continue;
			}
			const bool mouth = (i == 0 || i == last);
			Vector3i pos;
			pos.x = w.x;
			pos.z = w.y;
			pos.y = map_Height(world.map, w.x, w.y) + 8;
			effectGiveAuxVar(mouth ? 120 : 60);
			addEffect(&pos, EFFECT_EXPLOSION, mouth ? EXPLOSION_TYPE_SMALL : EXPLOSION_TYPE_LASER, false, nullptr, 0);
		}
	}
}

/// Displays an image for the Network Issue button
static void NetworkDisplayImage(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	int x = xOffset + psWidget->x();
	int y = yOffset + psWidget->y();
	UWORD ImageID;
	CONNECTION_STATUS status = (CONNECTION_STATUS)UNPACKDWORD_TRI_A(psWidget->UserData);

	ASSERT(psWidget->type == WIDG_BUTTON, "Not a button");

	// cheap way to do a button flash
	if ((realTime / 250) % 2 == 0)
	{
		ImageID = UNPACKDWORD_TRI_B(psWidget->UserData);
	}
	else
	{
		ImageID = UNPACKDWORD_TRI_C(psWidget->UserData);
	}

	if (NETcheckPlayerConnectionStatus(status, NET_ALL_PLAYERS))
	{
		unsigned c = 0;
		char players[MAX_PLAYERS + 1];
		PlayerMask playerMaskMapped = 0;
		for (unsigned n = 0; n < MAX_PLAYERS; ++n)
		{
			if (NETcheckPlayerConnectionStatus(status, n))
			{
				playerMaskMapped |= 1 << NetPlay.players[n].position;
			}
		}
		for (unsigned n = 0; n < MAX_PLAYERS; ++n)
		{
			if ((playerMaskMapped & 1 << n) != 0)
			{
				STATIC_ASSERT(MAX_PLAYERS <= 32);  // If increasing MAX_PLAYERS, check all the 1<<playerNumber shifts, since the 1 is usually a 32-bit type.
				players[c++] = "0123456789ABCDEFGHIJKLMNOPQRSTUV"[n];
			}
		}
		players[c] = '\0';
		const unsigned width = iV_GetTextWidth(players, font_regular) + 10;
		const unsigned height = iV_GetTextHeight(players, font_regular) + 10;
		iV_SetTextColour(WZCOL_TEXT_BRIGHT);
		iV_DrawText(players, x - width, y + height, font_regular);
	}

	iV_DrawImage(IntImages, ImageID, x, y);
}

static void setupConnectionStatusForm()
{
	static unsigned          prevStatusMask = 0;

	const int separation = 3;
	unsigned statusMask = 0;
	unsigned total = 0;

	for (unsigned i = 0; i < CONNECTIONSTATUS_NORMAL; ++i)
	{
		if (NETcheckPlayerConnectionStatus((CONNECTION_STATUS)i, NET_ALL_PLAYERS))
		{
			statusMask |= 1 << i;
			++total;
		}
	}

	if (prevStatusMask != 0 && statusMask != prevStatusMask)
	{
		// Remove the icons.
		for (unsigned i = 0; i < CONNECTIONSTATUS_NORMAL; ++i)
		{
			if ((statusMask & 1 << i) != 0)
			{
				widgDelete(psWScreen, NETWORK_BUT_ID + i);   // kill button
			}
		}
		widgDelete(psWScreen, NETWORK_FORM_ID);  // kill form

		prevStatusMask = 0;
	}

	if (prevStatusMask == 0 && statusMask != 0)
	{
		unsigned n = 0;
		// Create the basic form
		W_FORMINIT sFormInit;
		sFormInit.formID = 0;
		sFormInit.id = NETWORK_FORM_ID;
		sFormInit.style = WFORM_PLAIN;
		sFormInit.calcLayout = LAMBDA_CALCLAYOUT_SIMPLE({
			psWidget->move((int)(pie_GetVideoBufferWidth() - 52), 80);
		});
		sFormInit.width = 36;
		sFormInit.height = (24 + separation) * total - separation;
		if (!widgAddForm(psWScreen, &sFormInit))
		{
			//return false;
		}

		/* Now add the buttons */
		for (unsigned i = 0; i < CONNECTIONSTATUS_NORMAL; ++i)
		{
			if ((statusMask & 1 << i) == 0)
			{
				continue;
			}

			//set up default button data
			W_BUTINIT sButInit;
			sButInit.formID = NETWORK_FORM_ID;
			sButInit.id = NETWORK_BUT_ID + i;
			sButInit.width = 36;
			sButInit.height = 24;

			//add button
			sButInit.style = WBUT_PLAIN;
			sButInit.x = 0;
			sButInit.y = (24 + separation) * n;
			sButInit.pDisplay = NetworkDisplayImage;
			// Note we would set the image to be different based on which issue it is.
			switch (i)
			{
			default:
				ASSERT(false, "Bad connection status value.");
				sButInit.pTip = "Bug";
				sButInit.UserData = PACKDWORD_TRI(0, IMAGE_DESYNC_HI, IMAGE_PLAYER_LEFT_LO);
				break;
			case CONNECTIONSTATUS_PLAYER_LEAVING:
				sButInit.pTip = _("Player left");
				sButInit.UserData = PACKDWORD_TRI(i, IMAGE_PLAYER_LEFT_HI, IMAGE_PLAYER_LEFT_LO);
				break;
			case CONNECTIONSTATUS_PLAYER_DROPPED:
				sButInit.pTip = _("Player dropped");
				sButInit.UserData = PACKDWORD_TRI(i, IMAGE_DISCONNECT_LO, IMAGE_DISCONNECT_HI);
				break;
			case CONNECTIONSTATUS_WAITING_FOR_PLAYER:
				sButInit.pTip = _("Waiting for other players");
				sButInit.UserData = PACKDWORD_TRI(i, IMAGE_WAITING_HI, IMAGE_WAITING_LO);

				break;
			case CONNECTIONSTATUS_DESYNC:
				sButInit.pTip = _("Out of sync");
				sButInit.UserData = PACKDWORD_TRI(i, IMAGE_DESYNC_HI, IMAGE_DESYNC_LO);
				break;
			}

			if (!widgAddButton(psWScreen, &sButInit))
			{
				//return false;
			}

			++n;
		}

		prevStatusMask = statusMask;
	}
}

/// Render the 3D world
void draw3DScene()
{
	WZ_PROFILE_SCOPE(draw3DScene);
	wzPerfBegin(PERF_START_FRAME, "Start 3D scene");

	dynamicResolutionUpdate();

	/* What frame number are we on? */
	currentGameFrame = frameGetFrameNumber();

	// Tell shader system what the time is
	pie_SetShaderTime(graphicsTime);

	/* Build the drag quad */
	if (dragBox3D.status == DRAG_RELEASED)
	{
		dragQuad.coords[0].x = dragBox3D.x1; // TOP LEFT
		dragQuad.coords[0].y = dragBox3D.y1;

		dragQuad.coords[1].x = dragBox3D.x2; // TOP RIGHT
		dragQuad.coords[1].y = dragBox3D.y1;

		dragQuad.coords[2].x = dragBox3D.x2; // BOTTOM RIGHT
		dragQuad.coords[2].y = dragBox3D.y2;

		dragQuad.coords[3].x = dragBox3D.x1; // BOTTOM LEFT
		dragQuad.coords[3].y = dragBox3D.y2;
	}

	pie_Begin3DScene();
	/* Set 3D world origins */
	pie_SetGeometricOffset(modbigmap::viewCenterX(rendSurface.width), geoOffset);   // MOD: geteilter Bildschirm - 3D-Mitte rechts

	updateFogDistance(distance);

	// Set light manager. Both are kept alive across frames so the containers they cache
	// internally keep their capacity instead of being rebuilt from empty every frame.
	{
		if (war_getPointLightPerPixelLighting() && getTerrainShaderQuality() == TerrainShaderQuality::NORMAL_MAPPING)
		{
			if (!perPixelLightingManager)
			{
				perPixelLightingManager = std::make_unique<renderingNew::LightingManager>();
			}
			setLightingManager(perPixelLightingManager.get());
		}
		else
		{
			if (!lightmapLightingManager)
			{
				lightmapLightingManager = std::make_unique<rendering1999::LightingManager>();
			}
			setLightingManager(lightmapLightingManager.get());
		}
	}

	/* Finish moving the camera for this frame, then draw from where it is */
	updateCameraForFrame();

	/* Now, draw the terrain */
	drawTiles(&playerPos, getCurrentLightingData(), getCurrentLightmapData(), getCurrentLightingManager());

	wzPerfBegin(PERF_MISC, "3D scene - misc and text");

	structureEffects(); // add fancy effects to structures

	wzPerfEnd(PERF_MISC);

	camtrace::markStage(camtrace::Stage::SceneEnd);
	perf::endOfFrame();
}


/***************************************************************************/
bool	doWeDrawProximitys()
{
	return (bDrawProximitys);
}
/***************************************************************************/
void	setProximityDraw(bool val)
{
	bDrawProximitys = val;
}
/***************************************************************************/
/// Calculate the average terrain height for the area directly below the tile
static int calcAverageTerrainHeight(WorldMapState& mapState, int tileX, int tileZ)
{
	int numTilesAveraged = 0;

	/**
	 * We track the height here - so make sure we get the average heights
	 * of the tiles directly underneath us
	 */
	int result = 0;
	for (int i = -4; i <= 4; i++)
	{
		for (int j = -4; j <= 4; j++)
		{
			if (tileOnMap(mapState, tileX + j, tileZ + i))
			{
				/* Get a pointer to the tile at this location */
				MAPTILE *psTile = mapTile(mapState, tileX + j, tileZ + i);

				result += std::max(psTile->height, psTile->waterLevel);
				numTilesAveraged++;
			}
		}
	}
	if (numTilesAveraged == 0) // might be if off map
	{
		return ELEVATION_SCALE * DIST_TILE;
	}

	/**
	 * Work out the average height.
	 * We use this information to keep the player camera above the terrain.
	 */
	MAPTILE *psTile = mapTile(mapState, tileX, tileZ);

	result /= numTilesAveraged;
	if (result < psTile->height)
	{
		result = psTile->height;
	}

	return result;
}

/// The 9x9-tile average of calcAverageTerrainHeight() at each tile's center,
/// interpolated bilinearly to the world position
static float calcInterpolatedAverageTerrainHeight(WorldMapState& mapState, int worldX, int worldZ)
{
	const float u = (static_cast<float>(worldX) - TILE_UNITS / 2) / TILE_UNITS;
	const float v = (static_cast<float>(worldZ) - TILE_UNITS / 2) / TILE_UNITS;
	const int tileX = static_cast<int>(std::floor(u));
	const int tileZ = static_cast<int>(std::floor(v));
	const float fx = u - static_cast<float>(tileX);
	const float fz = v - static_cast<float>(tileZ);

	const float h00 = static_cast<float>(calcAverageTerrainHeight(mapState, tileX, tileZ));
	const float h10 = static_cast<float>(calcAverageTerrainHeight(mapState, tileX + 1, tileZ));
	const float h01 = static_cast<float>(calcAverageTerrainHeight(mapState, tileX, tileZ + 1));
	const float h11 = static_cast<float>(calcAverageTerrainHeight(mapState, tileX + 1, tileZ + 1));
	return (h00 * (1.f - fx) + h10 * fx) * (1.f - fz) + (h01 * (1.f - fx) + h11 * fx) * fz;
}

static void updatePlayerAverageCentreTerrainHeight()
{
	averageCentreTerrainHeight = calcInterpolatedAverageTerrainHeight(gameWorld.map, playerPos.p.x, playerPos.p.z);
}

inline bool quadIntersectsWithScreen(const QUAD & quad)
{
	const int width = pie_GetVideoBufferWidth();
	const int height = pie_GetVideoBufferHeight();
	for (int iCoordIdx = 0; iCoordIdx < 4; ++iCoordIdx)
	{
		if ((quad.coords[iCoordIdx].x < 0) || (quad.coords[iCoordIdx].x > width))
		{
			continue;
		}
		if ((quad.coords[iCoordIdx].y < 0) || (quad.coords[iCoordIdx].y > height))
		{
			continue;
		}

		return true; // corner (x,y) is within the screen bounds
	}

	return false;
}

glm::mat4 getBiasedShadowMapMVPMatrix(glm::mat4 lightOrthoMatrix, const glm::mat4& lightViewMatrix)
{
	glm::mat4 biasMatrix(
		0.5, 0.0, 0.0, 0.0,
		0.0, 0.5, 0.0, 0.0,
		0.0, 0.0, 0.5, 0.0,
		0.5, 0.5, 0.5, 1.0
	);

	glm::mat4 shadowMatrix = lightOrthoMatrix * lightViewMatrix;
	return biasMatrix * shadowMatrix;
}

/// Draw the terrain and all droids, missiles and other objects on it
static void drawTiles(iView *player, LightingData& lightData, LightMap& lightmap, ILightingManager& lightManager)
{
	WZ_PROFILE_SCOPE(drawTiles);
	// draw terrain

	auto currShadowMode = pie_getShadowMode();

	/* ---------------------------------------------------------------- */
	/* Do boundary and extent checking                                  */
	/* ---------------------------------------------------------------- */

	/* Find our position in tile coordinates */
	playerXTile = map_coord(player->p.x);
	playerZTile = map_coord(player->p.z);

	camtrace::markDraw(player->p.x, player->p.y, player->p.z, player->r.x, player->r.y, distance, getWarCamStatus());

	/* ---------------------------------------------------------------- */
	/* Set up the geometry                                              */
	/* ---------------------------------------------------------------- */

	/* ---------------------------------------------------------------- */

	const glm::mat4 baseViewMatrix =
		glm::translate(glm::vec3(0.f, 0.f, distance)) *
		glm::scale(glm::vec3(pie_GetResScalingFactor() / 100.f)) *
		glm::rotate(UNDEG(player->r.z), glm::vec3(0.f, 0.f, 1.f)) *
		glm::rotate(UNDEG(player->r.x), glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(player->r.y), glm::vec3(0.f, 1.f, 0.f)) *
		glm::translate(glm::vec3(0, -player->p.y, 0));

	// Calculate shadow mapping cascades
	glm::vec3 lightInvDir = getTheSun();
	size_t numShadowCascades = std::min<size_t>(gfx_api::context::get().numDepthPasses(), WZ_MAX_SHADOW_CASCADES);
	std::vector<Cascade> shadowCascades;
	if (currShadowMode == ShadowMode::Shadow_Mapping)
	{
		calculateShadowCascades(player, distance, baseViewMatrix, lightInvDir, numShadowCascades, shadowCascades);
	}

	// Incorporate all the view transforms into viewMatrix
	const glm::mat4 viewMatrix = baseViewMatrix * glm::translate(glm::vec3(-player->p.x, 0, player->p.z));

	const glm::mat4 perspectiveMatrix = pie_PerspectiveGet();
	const glm::mat4 perspectiveViewMatrix = perspectiveMatrix * viewMatrix;

	const glm::vec3 cameraPos = (glm::inverse(viewMatrix) * glm::vec4(0,0,0,1)).xyz(); // `actualCameraPosition` is not accurate enough due to int calc

	actualCameraPosition = Vector3i(0, 0, 0);

	/* Set the camera position */
	actualCameraPosition.z -= static_cast<int>(distance);

	// Now, scale the world according to what resolution we're running in
	actualCameraPosition.z /= std::max<int>(static_cast<int>(pie_GetResScalingFactor() / 100.f), 1);

	/* Rotate for the player */
	rotateSomething(actualCameraPosition.x, actualCameraPosition.y, -player->r.z);
	rotateSomething(actualCameraPosition.y, actualCameraPosition.z, -player->r.x);
	rotateSomething(actualCameraPosition.z, actualCameraPosition.x, -player->r.y);

	/* Translate */
	actualCameraPosition.y -= -player->p.y;

	// this also determines the length of the shadows
	// NOTE: the sun in world space
	pie_BeginLighting(getTheSun());

	// Reset all lighting data
	lightManager.SetFrameStart();

	// update the fog of war... FIXME: Remove this
	const glm::mat4 tileCalcPerspectiveViewMatrix = perspectiveMatrix * baseViewMatrix;
	{
		WZ_PROFILE_SCOPE(init_lightmap);
		for (int i = -visibleTiles.y / 2, idx = 0; i <= visibleTiles.y / 2; i++, ++idx)
		{
			/* Go through the x's */
			for (int j = -visibleTiles.x / 2, jdx = 0; j <= visibleTiles.x / 2; j++, ++jdx)
			{
				Vector2i screen(0, 0);
				Position pos;

				pos.x = world_coord(j) - (playerPos.p.x % TILE_WIDTH);
				pos.z = -world_coord(i) + (playerPos.p.z % TILE_HEIGHT);
				pos.y = 0;

				if (tileOnMap(gameWorld.map, playerXTile + j, playerZTile + i))
				{
					pos.y = map_TileHeightSurface(gameWorld.map, playerXTile + j, playerZTile + i);
					auto color = pal_SetBrightness(0);
					lightmap(playerXTile + j, playerZTile + i) = color;
				}
				tileScreenInfo[idx][jdx].z = pie_RotateProjectWithPerspective(&pos, tileCalcPerspectiveViewMatrix, &screen);
				tileScreenInfo[idx][jdx].x = screen.x;
				tileScreenInfo[idx][jdx].y = screen.y;
			}
		}
	}

	// Determine whether each tile in the drawable range is actually visible on-screen
	// (used for more accurate clipping elsewhere)
	{
		WZ_PROFILE_SCOPE(tile_Culling);
		for (int idx = 0; idx < visibleTiles.y; ++idx)
		{
			for (int jdx = 0; jdx < visibleTiles.x; ++jdx)
			{
				QUAD quad;

				quad.coords[0].x = tileScreenInfo[idx + 0][jdx + 0].x;
				quad.coords[0].y = tileScreenInfo[idx + 0][jdx + 0].y;

				quad.coords[1].x = tileScreenInfo[idx + 0][jdx + 1].x;
				quad.coords[1].y = tileScreenInfo[idx + 0][jdx + 1].y;

				quad.coords[2].x = tileScreenInfo[idx + 1][jdx + 1].x;
				quad.coords[2].y = tileScreenInfo[idx + 1][jdx + 1].y;

				quad.coords[3].x = tileScreenInfo[idx + 1][jdx + 0].x;
				quad.coords[3].y = tileScreenInfo[idx + 1][jdx + 0].y;

				tileScreenVisible[idx][jdx] = quadIntersectsWithScreen(quad);
			}
		}
	}

	wzPerfEnd(PERF_START_FRAME);

	pie_StartMeshes();

	/* This is done here as effects can light the terrain - pause mode problems though */
	wzPerfBegin(PERF_EFFECTS, "3D scene - effects");
	processEffects(perspectiveViewMatrix, lightData);
	modweather::tick(); // MOD: Wetter (Gewitter, Sandsturm, Schneesturm, Nacht) - nur Anzeige
	atmosUpdateSystem(gameWorld.map);
	avUpdateTiles(gameWorld.map);
	emitProjectileLights(lightData);
	wzPerfEnd(PERF_EFFECTS);

	// The lightmap need to be ready at this point
	{
		WZ_PROFILE_SCOPE(LightingManager_ComputeFrameData);
		LightingSceneInfo lightScene;
		lightScene.cameraPosition = cameraPos;
		lightScene.groundHeightAt = [](int32_t worldX, int32_t worldY) {
			return map_Height(gameWorld.map, worldX, worldY);
		};
		lightManager.ComputeFrameData(lightData, lightmap, perspectiveViewMatrix, lightScene);

		// Cleared here rather than at the top of the frame, so we can handle lights emitted while
		// drawing (currently: muzzle flashes) which arrive after this point and would otherwise
		// be dropped.
		// Instead, they are carried into the next frame, one frame behind whatever cast them.
		lightData.lights.clear();
	}

	// Every pass that lights reads the same point lights, so upload them once here and hand the reference to the consumers
	const auto& bucketLight = lightManager.getPointLightBuckets();
	// Only the uniform block transport still has a point light block to send.
	const bool lightArraysInBlock = gfx_api::context::get().lightDataTransport() == gfx_api::data_buffer_transport::uniform_block;

	gfx_api::frame_uniform_block_ref<gfx_api::PointLightsUniforms> pointLightsRef;
	if (lightArraysInBlock)
	{
		gfx_api::PointLightsUniforms pointLightUniforms = {};
		// Since capacity is sized to match the uniform block max in this case, we copy the whole thing.
		ASSERT(bucketLight.positions.size() == gfx_api::max_lights
			&& bucketLight.light_index.size() == gfx_api::max_indexed_lights,
			"Light capacity (%zu, %zu) does not match the block on the uniform block transport",
			bucketLight.positions.size(), bucketLight.light_index.size());
		std::copy_n(bucketLight.positions.begin(), gfx_api::max_lights, pointLightUniforms.PointLightsPosition.begin());
		std::copy_n(bucketLight.colorAndEnergy.begin(), gfx_api::max_lights, pointLightUniforms.PointLightsColorAndEnergy.begin());
		std::copy_n(bucketLight.directionAndCos.begin(), gfx_api::max_lights, pointLightUniforms.PointLightsDirectionAndCos.begin());
		std::copy_n(bucketLight.light_index.begin(), gfx_api::max_indexed_lights, pointLightUniforms.indexed_lights.begin());
		pointLightsRef = gfx_api::context::get().upload_frame_uniform(pointLightUniforms);
	}
	else
	{
		// The arrays travel separately and the bucket table rides in each pipeline's globals,
		// so the pointLightsRef block has nothing left in it and is not declared at all.

		const auto& flatLights = lightManager.getFlatPointLightData();
		gfx_api::context::get().upload_light_data(
			flatLights.lights.data(), flatLights.lights.size() * sizeof(glm::vec4),
			flatLights.indices.data(), flatLights.indices.size() * sizeof(int32_t));
	}

	// prepare terrain for drawing
	perFrameTerrainUpdates(gameWorld.map, lightmap);

	// and prepare for rendering the models
	wzPerfBegin(PERF_MODEL_INIT, "Draw 3D scene - model init");

	/* ---------------------------------------------------------------- */
	/* Calculate & batch all mesh / object instances to be drawn        */
	/* ---------------------------------------------------------------- */
	displayStaticObjects(viewMatrix, perspectiveViewMatrix); // may be bucket render implemented
	displayFeatures(viewMatrix, perspectiveViewMatrix);
	displayOrbitAsteroids(viewMatrix);   // MOD: echte MF-Asteroiden unter den Orbit-Inseln
	displayCraterPiles(viewMatrix);      // MOD (17.11. 14:14): Erdhaufen am Kraterrand
	displayDynamicObjects(viewMatrix, perspectiveViewMatrix); // may be bucket render implemented
	if (doWeDrawProximitys())
	{
		displayProximityMsgs(viewMatrix, perspectiveViewMatrix);
	}
	displayDelivPoints(viewMatrix, perspectiveViewMatrix);
	modferry::draw3D(viewMatrix);   // MOD (17.11. 18:18): Landezonen-Lichter rot / blau / gruen
	modnightlights::draw3D(viewMatrix);
	modheadlights::draw3D(viewMatrix);   // MOD (18.11. 17:09): Scheinwerfer an allen Bodenfahrzeugen bei Nacht
	modsat::draw3D(viewMatrix);      // MOD (17.11. 21:34): Satelliten-Start von der Orbit-Insel, Satelliten am Orbit-Himmel
	modnuke::draw3D(viewMatrix);     // MOD (17.11. 21:13): Dampf ueber den Kuehltuermen laufender Atomkraftwerke
	modrailbeam::draw3D(viewMatrix); // MOD (19.11.): blaue Railgun-Strahlen (Zone Trooper, Kommando)
	modicbm::draw3D(viewMatrix);     // MOD (18.11.): Atomrakete im Flug, Atompilz
	modshield::draw3D(viewMatrix);   // MOD (18.11.): Schildkuppeln, Einschlag-Aufblitzen
	modshockwave::draw3D(viewMatrix);   // MOD (22.11. 21:19): Druckwelle grosser Oel-/Munitionsexplosionen
	modtracks::draw3D(viewMatrix);   // MOD (18.11.): Fahrspuren
	modrocket::draw3D(viewMatrix);   // MOD (17.11. 20:56): Raketen im Silo, Countdown-Dampf, Start   // MOD (17.11. 20:44): warmweisse Nachtlichter an Masten, Kraenen, Tuermen, Gebaeuden
	display3DProjectiles(viewMatrix, perspectiveViewMatrix); // may be bucket render implemented
	wzPerfEnd(PERF_MODEL_INIT);

	wzPerfBegin(PERF_PARTICLES, "3D scene - particles");
	atmosDrawParticles(viewMatrix, perspectiveViewMatrix);
	wzPerfEnd(PERF_PARTICLES);

	bucketRenderCurrentList(viewMatrix, perspectiveViewMatrix);

	gfx_api::context::get().debugStringMarker("Draw 3D scene - blueprints");
	displayBlueprints(viewMatrix, perspectiveViewMatrix);

	/* ---------------------------------------------------------------- */
	/* Actually render / draw everything                                */
	/* ---------------------------------------------------------------- */

	pie_UpdateLightmap(getTerrainLightmapTexture(), getModelUVLightmapMatrix());
	pie_FinalizeMeshes(currentGameFrame);


	ShadowCascadesInfo shadowCascadesInfo;
	shadowCascadesInfo.shadowMapSize = gfx_api::context::get().getDepthPassDimensions(0);
	for (size_t i = 0; i < std::min<size_t>(shadowCascades.size(), WZ_MAX_SHADOW_CASCADES); ++i)
	{
		shadowCascadesInfo.shadowMVPMatrix[i] = getBiasedShadowMapMVPMatrix(shadowCascades[i].projectionMatrix, shadowCascades[i].viewMatrix);
		shadowCascadesInfo.shadowCascadeSplit[i] = shadowCascades[i].splitDepth;
	}

	InGame3DFrameContext& ctx = pie_GetInGame3DFrameContext();
	ctx.perspectiveViewMatrix = perspectiveViewMatrix;
	ctx.viewMatrix = viewMatrix;
	ctx.baseViewMatrix = baseViewMatrix;
	ctx.perspectiveMatrix = perspectiveMatrix;
	ctx.cameraPos = cameraPos;
	ctx.currentGameFrame = currentGameFrame;
	ctx.pointLights = pointLightsRef;
	ctx.shadowCascadesInfo = shadowCascadesInfo;
	for (size_t i = 0; i < shadowCascades.size() && i < WZ_MAX_SHADOW_CASCADES; ++i)
	{
		ctx.cascadeProj[i] = shadowCascades[i].projectionMatrix;
		ctx.cascadeView[i] = shadowCascades[i].viewMatrix;
	}
	pie_BindInGame3DFrameContext(&ctx);
}

/// Initialise the fog, skybox and some other stuff
bool init3DView()
{
	setTheSun(getDefaultSunPosition());

	/* There are no drag boxes */
	dragBox3D.status = DRAG_INACTIVE;

	/* Get all the init stuff out of here? */
	initWarCam();
	trackHeightStartAtTarget = true;

	/* Init the game messaging system */
	initConsoleMessages();

	atmosInitSystem();

	// default skybox, will override in script if not satisfactory
	setSkyBox("texpages/page-25-sky-arizona.png", 0.0f, 10000.0f);

	// distance is not saved, so initialise it now
	distance = war_GetMapZoom(); // distance

	if(pie_GetFogEnabled()){
		pie_SetFogStatus(true);
	}

	// Set the initial fog distance
	updateFogDistance(distance);

	setDefaultFogColour();

	// Allocate / free scene-effect pipeline surfaces to match persisted config.
	applySceneEffectSurfaces();

	playerPos.r.z = 0; // roll
	playerPos.r.y = 0; // rotation
	playerPos.r.x = DEG(360 + INITIAL_STARTING_PITCH); // angle

	if (!initTerrain(gameWorld.map))
	{
		return false;
	}

	txtLevelName.setText(WzString::fromUtf8(mapNameWithoutTechlevel(getLevelName())), font_small);
	txtDebugStatus.setText("DEBUG ", font_small);

	batchedObjectStatusRenderer.initialize();


	if (!pScreenTriangleVBO)
	{
		// vertex attributes for a single triangle that covers the screen
		gfx_api::gfxFloat screenTriangleVertices[] = {
			-1.f, -1.f,
			3.f, -1.f,
			-1.f, 3.f
		};
		pScreenTriangleVBO = gfx_api::context::get().create_buffer_object(gfx_api::buffer::usage::vertex_buffer, gfx_api::context::buffer_storage_hint::static_draw, "screenTriangleVertices");
		pScreenTriangleVBO->upload(sizeof(screenTriangleVertices), screenTriangleVertices);
	}

	ssao::init();
	ssr::init();
	if (!range_rings::init())
	{
		debug(LOG_ERROR, "Failed to initialize range-ring buffers");
	}

	clearFadingProjectileLights();

	return true;
}

void shutdown3DView()
{
	txtLevelName = WzText();
	txtDebugStatus = WzText();
	txtCurrentTime = WzText();
	txtShowFPS = WzText();
	txtUnits = WzText();
	// show Samples text
	txtShowSamples_Que = WzText();
	txtShowSamples_Lst = WzText();
	txtShowSamples_Act = WzText();
	// show Orders text
	txtShowOrders = WzText();
	// show Droid visible/draw counts text
	droidText = WzText();
	modTxtCmdLabel = WzText();   // MOD: sonst Absturzmeldung beim Beenden (Textur nicht freigegeben)
	txtModTime = WzText();       // MOD (23.11.)

	batchedObjectStatusRenderer.clear(); // NOTE: *NOT* reset() - see shutdown3DView_FullReset below for why

	// Drop the mesh batches the instanced renderer keeps across frames. Safe here (unlike the GPU
	// buffers noted below) because it is plain CPU-side storage that just regrows on the next match.
	pie_ReleaseMeshBatchMemory();
}

void shutdown3DView_FullReset()
{
	// Because of when levReleaseMissionData can be called (especially when using certain cheats in campaign like "ascend" or "let me win")
	// Only call "reset" (which also resets BatchedMultiRectRenderer and actually frees GPU buffers)
	// in this function, which is called from stageTwoShutdown (as opposed to stageThreeShutdown, which is called from levReleaseMissionData)...
	batchedObjectStatusRenderer.reset();

	ssao::shutdown();
	ssr::shutdown();
	range_rings::shutdown();
	modshield::gpuShutdown();   // MOD (18.11.)

	setLightingManager(nullptr);
	perPixelLightingManager.reset();
	lightmapLightingManager.reset();

	delete pScreenTriangleVBO;
	pScreenTriangleVBO = nullptr;
	smaaFreeLutTextures();
}

/// set the view position from save game
void disp3d_setView(iView *newView)
{
	playerPos = *newView;
}

/// reset the camera rotation (used for save games <= 10)
void disp3d_oldView()
{
	playerPos.r.y = OLD_INITIAL_ROTATION; // rotation
	playerPos.p.y = OLD_START_HEIGHT; // height
}

/// get the view position for save game
void disp3d_getView(iView *newView)
{
	*newView = playerPos;
}

/// Are the current world coordinates within the processed range of tiles on the screen?
/// (Warzone has a maximum range of tiles around the current player camera position that it will display.)
bool quickClipXYToMaximumTilesFromCurrentPosition(SDWORD x, SDWORD y)
{
	// +2 for edge of visibility fading (see terrain.cpp)
	if (std::abs(x - playerPos.p.x) < world_coord(visibleTiles.x / 2 + 2) &&
		std::abs(y - playerPos.p.z) < world_coord(visibleTiles.y / 2 + 2))
	{
		return (true);
	}
	else
	{
		return (false);
	}
}

/// Are the current tile coordinates visible on screen?
bool clipXY(SDWORD x, SDWORD y)
{
	// +2 for edge of visibility fading (see terrain.cpp)
	if (std::abs(x - playerPos.p.x) < world_coord(visibleTiles.x / 2 + 2) &&
	    std::abs(y - playerPos.p.z) < world_coord(visibleTiles.y / 2 + 2))
	{
		// additional check using the tileScreenVisible matrix
		int mapX = map_coord(x - playerPos.p.x) + visibleTiles.x / 2;
		int mapY = map_coord(y - playerPos.p.z) + visibleTiles.y / 2;
		if (mapX < 0 || mapY < 0)
		{
			return false;
		}
		if (mapX > visibleTiles.x || mapY > visibleTiles.y)
		{
			return false;
		}
		return tileScreenVisible[mapY][mapX];
	}
	else
	{
		return (false);
	}
}

bool clipXYZNormalized(const Vector3i &normalizedPosition, const glm::mat4 &perspectiveViewMatrix)
{
	Vector2i pixel(0, 0);
	pie_RotateProjectWithPerspective(&normalizedPosition, perspectiveViewMatrix, &pixel);
	return pixel.x >= 0 && pixel.y >= 0 && pixel.x < pie_GetVideoBufferWidth() && pixel.y < pie_GetVideoBufferHeight();
}

/// Are the current 3d game-world coordinates visible on screen?
/// (Does not take into account occlusion)
bool clipXYZ(int x, int y, int z, const glm::mat4 &perspectiveViewMatrix)
{
	Vector3i position;
	position.x = x;
	position.z = -(y);
	position.y = z;

	return clipXYZNormalized(position, perspectiveViewMatrix);
}

bool clipShapeOnScreen(const iIMDShape *pIMD, const glm::mat4 &perspectiveViewModelMatrix, int overdrawScreenPoints /*= 10*/)
{
	/* Get its absolute dimensions */
	Vector3i origin;
	Vector2i center(0, 0);
	int wsRadius = 22; // World space radius, 22 = magic minimum
	float radius;

	if (pIMD)
	{
		wsRadius = MAX(wsRadius, pIMD->radius);
	}

	origin = Vector3i(0, wsRadius, 0); // take the center of the object

	/* get the screen coordinates */
	const float cZ = pie_RotateProjectWithPerspective(&origin, perspectiveViewModelMatrix, &center) * 0.1f;

	// avoid division by zero
	if (cZ > 0)
	{
		radius = wsRadius / cZ * pie_GetResScalingFactor();
	}
	else
	{
		radius = 1; // 1 just in case some other code assumes radius != 0
	}

	const int screenMinX = -overdrawScreenPoints;
	const int screenMinY = -overdrawScreenPoints;

	return (center.x + radius >  screenMinX &&
			center.x - radius < (pie_GetVideoBufferWidth() + overdrawScreenPoints) &&
			center.y + radius >  screenMinY &&
			center.y - radius < (pie_GetVideoBufferHeight() + overdrawScreenPoints));
}

// Use overdrawScreenPoints as a workaround for casting shadows when the main unit is off-screen (but right at the edge)
bool clipDroidOnScreen(DROID *psDroid, const glm::mat4 &perspectiveViewModelMatrix, int overdrawScreenPoints /*= 25*/)
{
	/* Get its absolute dimensions */
	// NOTE: This only takes into account body, but is "good enough"
	const BODY_STATS *psBStats = psDroid->getBodyStats();
	const iIMDShape * pIMD = (psBStats != nullptr && psBStats->pIMD != nullptr) ? psBStats->pIMD->displayModel() : nullptr;

	if (psDroid->heightAboveMap > 0)
	{
		// clipShapeOnScreen is noticeably insufficient if it's a flying unit - we have to care about both shadows and the unit itself...
		// HACK: boost the overdrawScreenPoints by a factor based on the heightAboveMap
		overdrawScreenPoints *= std::max<int>(1, (psDroid->heightAboveMap / 20));
	}
	return clipShapeOnScreen(pIMD, perspectiveViewModelMatrix, overdrawScreenPoints);
}

bool clipStructureOnScreen(STRUCTURE *psStructure)
{
	StructureBounds b = getStructureBounds(psStructure);
	assert(b.size.x != 0 && b.size.y != 0);
	// +4 to make room for shadows on the terrain
	int start = -2;
	int endB = (b.size.y + 2) - 1;
	int endW = (b.size.x + 2) - 1;
	for (int breadth = start; breadth <= endB; ++breadth)
	{
		for (int width = start; width <= endW; ++width)
		{
			if (width != start && width != endW && breadth != start && breadth != endB)
			{
				continue; // skip interior
			}
			if (clipXY(world_coord(b.map.x + width), world_coord(b.map.y + breadth)))
			{
				return true;
			}
		}
	}

	return false;
}

/** Get the screen coordinates for the current transform matrix.
 * This function is used to determine the area the user can click for the
 * intelligence screen buttons. The radius parameter is always set to the same value.
 */
static void	calcFlagPosScreenCoords(SDWORD *pX, SDWORD *pY, SDWORD *pR, const glm::mat4 &perspectiveViewModelMatrix)
{
	/* Get it's absolute dimensions */
	Vector3i center3d(0, 0, 0);
	Vector2i center2d(0, 0);
	/* How big a box do we want - will ultimately be calculated using xmax, ymax, zmax etc */
	UDWORD	radius = 22;

	/* Pop matrices and get the screen coordinates for last point*/
	pie_RotateProjectWithPerspective(&center3d, perspectiveViewModelMatrix, &center2d);

	/*store the coords*/
	*pX = center2d.x;
	*pY = center2d.y;
	*pR = radius;
}

/// A projectile is drawn (and lights the world) only while it is spawned, not yet impacted, and visible to the viewer.
static bool projectileIsShowing(const PROJECTILE *psObj)
{
	return graphicsTime >= psObj->prevSpacetime.time && graphicsTime <= psObj->time && gfxVisible(psObj);
}

/// Decide whether to render a projectile, and make sure it will be drawn
static void display3DProjectiles(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	WZ_PROFILE_SCOPE(display3DProjectiles);
	WZ_PERF_SCOPE(T_display3DProjectiles);
	PROJECTILE *psObj = proj_GetFirst();
	while (psObj != nullptr)
	{
		if (projectileIsShowing(psObj))
		{
			/* Draw a bullet at psObj->pos.x for X coord
			   psObj->pos.y for Z coord
			   whatever for Y (height) coord - arcing ?
			*/
			/* these guys get drawn last */
			if (psObj->psWStats->weaponSubClass == WSC_ROCKET ||
			    psObj->psWStats->weaponSubClass == WSC_MISSILE ||
			    psObj->psWStats->weaponSubClass == WSC_COMMAND ||
			    psObj->psWStats->weaponSubClass == WSC_SLOWMISSILE ||
			    psObj->psWStats->weaponSubClass == WSC_SLOWROCKET ||
			    psObj->psWStats->weaponSubClass == WSC_ENERGY ||
			    psObj->psWStats->weaponSubClass == WSC_EMP)
			{
				bucketAddTypeToList(RENDER_PROJECTILE, psObj, perspectiveViewMatrix);
			}
			else
			{
				renderProjectile(psObj, viewMatrix, perspectiveViewMatrix);
			}
		}

		psObj = proj_GetNext();
	}
}	/* end of function display3DProjectiles */

/// Whether projectile lighting is switched on and the renderer can carry per-pixel point lights.
static bool projectileLightingActive()
{
	return war_getProjectileLighting() && war_getPointLightPerPixelLighting()
		&& getTerrainShaderQuality() == TerrainShaderQuality::NORMAL_MAPPING;
}

/// The subclasses renderProjectile skips, because an effect is drawn in place of the projectile graphic.
static bool projectileDrawnAsEffect(const WEAPON_STATS *psStats)
{
	return psStats->weaponSubClass == WSC_FLAME
		|| psStats->weaponSubClass == WSC_COMMAND
		|| psStats->weaponSubClass == WSC_ELECTRONIC
		|| psStats->weaponSubClass == WSC_EMP
		|| psStats->weaponSubClass == WSC_LAS_SAT;   // MOD (21:34): immer als Effekt (Laser vom Himmel)
}

static void pushProjectileLight(LightingData &lightData, const Vector3i &lightPosition, const ProjectileLight &projectileLight)
{
	LIGHT light;
	light.position = lightPosition;
	light.range = projectileLight.range;
	light.colour = projectileLight.color;
	light.intensity = projectileLight.intensity;
	lightData.lights.push_back(light);
}

/// Emit the light every showing in-flight projectile throws, in the same frame the light manager reads them.
static void emitProjectileLights(LightingData &lightData)
{
	if (!projectileLightingActive())
	{
		clearFadingProjectileLights();
		return;
	}
	beginProjectileLightFrame();
	for (PROJECTILE *psObj = proj_GetFirst(); psObj != nullptr; psObj = proj_GetNext())
	{
		if (!projectileIsShowing(psObj) || projectileDrawnAsEffect(psObj->psWStats))
		{
			continue;
		}
		const size_t weaponIndex = psObj->psWStats->index;
		const ProjectileLight *projectileLight = cachedInFlightProjectileLight(weaponIndex);
		if (projectileLight == nullptr)
		{
			continue;
		}
		const Spacetime st = interpolateObjectSpacetime(psObj, graphicsTime);
		// A light is positioned by game coordinates, where the height is the middle component
		const Vector3i position(st.pos.x, st.pos.z, st.pos.y);
		pushProjectileLight(lightData, position, *projectileLight);
		recordProjectileLight(psObj->id, position, weaponIndex);
	}
	emitProjectileFades(lightData);
}

/// Draw a projectile to the screen
void	renderProjectile(PROJECTILE *psCurr, const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	WEAPON_STATS	*psStats;
	Vector3i			dv;
	const iIMDShape	*pIMD;
	Spacetime       st;

	psStats = psCurr->psWStats;
	/* Reject flame or command since they have interim drawn fx */
	if (psStats->weaponSubClass == WSC_FLAME ||
	    psStats->weaponSubClass == WSC_COMMAND || // || psStats->weaponSubClass == WSC_ENERGY)
	    psStats->weaponSubClass == WSC_ELECTRONIC ||
	    psStats->weaponSubClass == WSC_EMP ||
	    psStats->weaponSubClass == WSC_LAS_SAT)   // MOD (21:34)
	{
		/* We don't do projectiles from these guys, cos there's an effect instead */
		return;
	}

	st = interpolateObjectSpacetime(psCurr, graphicsTime);

	//the weapon stats holds the reference to which graphic to use
	/*Need to draw the graphic depending on what the projectile is doing - hitting target,
	missing target, in flight etc - JUST DO IN FLIGHT FOR NOW! */
	pIMD = (psStats->pInFlightGraphic) ? psStats->pInFlightGraphic->displayModel() : nullptr;

	if (!clipXYZ(st.pos.x, st.pos.y, st.pos.z, perspectiveViewMatrix))
	{
		return; // Projectile is not on the screen (Note: This uses the position point of the projectile, not a full shape clipping check, for speed.)
	}

	/* Get bullet's x coord */
	dv.x = st.pos.x;

	/* Get it's y coord (z coord in the 3d world */
	dv.z = -(st.pos.y);

	/* What's the present height of the bullet? */
	dv.y = st.pos.z;

	/* Set up the matrix */
	Vector3i camera_base = actualCameraPosition;

	/* Translate to the correct position */
	Vector3i camera_mod;
	camera_mod.x = st.pos.x - playerPos.p.x;
	camera_mod.z = -(st.pos.y - playerPos.p.z);
	camera_mod.y = st.pos.z;
	camera_base -= camera_mod;

	/* Rotate it to the direction it's facing */
	rotateSomething(camera_base.z, camera_base.x, -(-st.rot.direction));

	/* pitch it */
	rotateSomething(camera_base.y, camera_base.z, -st.rot.pitch);

	const glm::mat4 modelMatrix_base =
		glm::translate(glm::vec3(dv)) *
		glm::rotate(UNDEG(-st.rot.direction), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(st.rot.pitch), glm::vec3(1.f, 0.f, 0.f));

#ifdef DEBUG
	bool anyPartGlowed = false;
	const iIMDShape *pFirstIMD = pIMD;
#endif
	for (; pIMD != nullptr; pIMD = pIMD->next.get())
	{
		bool rollToCamera = false;
		bool pitchToCamera = false;
		bool premultiplied = false;
		bool additive = psStats->weaponSubClass == WSC_ROCKET || psStats->weaponSubClass == WSC_MISSILE || psStats->weaponSubClass == WSC_SLOWROCKET || psStats->weaponSubClass == WSC_SLOWMISSILE;

		if (pIMD->flags & iV_IMD_ROLL_TO_CAMERA)
		{
			rollToCamera = true;
		}
		if (pIMD->flags & iV_IMD_PITCH_TO_CAMERA)
		{
			rollToCamera = true;
			pitchToCamera = true;
		}
		if (pIMD->flags & iV_IMD_NO_ADDITIVE)
		{
			additive = false;
		}
		if (pIMD->flags & iV_IMD_ADDITIVE)
		{
			additive = true;
		}
		if (pIMD->flags & iV_IMD_PREMULTIPLIED)
		{
			additive = false;
			premultiplied = true;
		}

#ifdef DEBUG
		anyPartGlowed = anyPartGlowed || additive || premultiplied;
#endif

		Vector3i camera = camera_base;
		glm::mat4 modelMatrix = modelMatrix_base;

		if ((pitchToCamera || rollToCamera) && !pIMD->connectors.empty())
		{
			// Centre on projectile (relevant for twin projectiles).
			camera -= Vector3i(pIMD->connectors[0].x, pIMD->connectors[0].y, pIMD->connectors[0].z);
			modelMatrix *= glm::translate(glm::vec3(pIMD->connectors[0]));
		}

		if (pitchToCamera)
		{
			int x = iAtan2(camera.z, camera.y);
			rotateSomething(camera.y, camera.z, -x);
			modelMatrix *= glm::rotate(UNDEG(x), glm::vec3(1.f, 0.f, 0.f));
		}

		if (rollToCamera)
		{
			int z = -iAtan2(camera.x, camera.y);
			rotateSomething(camera.x, camera.y, -z);
			modelMatrix *= glm::rotate(UNDEG(z), glm::vec3(0.f, 0.f, 1.f));
		}

		if ((pitchToCamera || rollToCamera) && !pIMD->connectors.empty())
		{
			camera -= Vector3i(-pIMD->connectors[0].x, -pIMD->connectors[0].y, -pIMD->connectors[0].z);
			// Undo centre on projectile (relevant for twin projectiles).
			modelMatrix *= glm::translate(glm::vec3(-pIMD->connectors[0].x, -pIMD->connectors[0].y, -pIMD->connectors[0].z));
		}

		if (premultiplied)
		{
			pie_Draw3DShape(pIMD, 0, 0, WZCOL_WHITE, pie_PREMULTIPLIED, 0, modelMatrix, viewMatrix, 0.f, true);
		}
		else if (additive)
		{
			pie_Draw3DShape(pIMD, 0, 0, WZCOL_WHITE, pie_ADDITIVE, 164, modelMatrix, viewMatrix, 0.f, true);
		}
		else
		{
			pie_Draw3DShape(pIMD, 0, 0, WZCOL_WHITE, 0, 0, modelMatrix, viewMatrix, 0.f, true);
		}
	}

#ifdef DEBUG
	ASSERT(anyPartGlowed == projectileGraphicGlows(psStats, pFirstIMD),
	       "Projectile glow decision disagrees with the one lighting uses for %s", getStatsName(psStats));
#endif
}

/// Draw the buildings
static void displayStaticObjects(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	WZ_PROFILE_SCOPE(displayStaticObjects);
	// to solve the flickering edges of baseplates
//	pie_SetDepthOffset(-1.0f);

	/* Go through all the players */
	for (unsigned aPlayer = 0; aPlayer < MAX_PLAYERS; ++aPlayer)
	{
		/* Now go all buildings for that player */
		for (BASE_OBJECT* obj : gameWorld.objects.structures[aPlayer])
		{
			/* Worth rendering the structure? */
			if (obj->type != OBJ_STRUCTURE || (obj->died != 0 && obj->died < graphicsTime)
				|| static_cast<STRUCTURE *>(obj)->isMobileCore()   // MOD (18.11.): Fabrik-Kern der fahrenden Fabrik ist unsichtbar
				|| !quickClipXYToMaximumTilesFromCurrentPosition(obj->pos.x, obj->pos.y))
			{
				continue;
			}
			STRUCTURE *psStructure = castStructure(obj);

			if (!clipStructureOnScreen(psStructure))
			{
				continue;
			}

			renderStructure(psStructure, viewMatrix, perspectiveViewMatrix);
		}
	}

	// Walk through destroyed objects.

	/* Now go all buildings for that player */
	for (BASE_OBJECT* obj : psDestroyedObj)
	{
		/* Worth rendering the structure? */
		if (obj->type != OBJ_STRUCTURE || (obj->died != 0 && obj->died < graphicsTime)
			|| static_cast<STRUCTURE *>(obj)->isMobileCore()   // MOD (18.11.): Fabrik-Kern unsichtbar
			|| !quickClipXYToMaximumTilesFromCurrentPosition(obj->pos.x, obj->pos.y))
		{
			continue;
		}
		STRUCTURE* psStructure = castStructure(obj);

		if (!clipStructureOnScreen(psStructure))
		{
			continue;
		}

		renderStructure(psStructure, viewMatrix, perspectiveViewMatrix);
	}

//	pie_SetDepthOffset(0.0f);
}

static bool tileHasIncompatibleStructure(MAPTILE const *tile, STRUCTURE_STATS const *stats, int moduleIndex)
{
	STRUCTURE *psStruct = castStructure(tile->psObject);
	if (psStruct == nullptr)
	{
		return false;
	}
	if (psStruct->status == SS_BEING_BUILT && nextModuleToBuild(psStruct, -1) >= moduleIndex)
	{
		return true;
	}
	if (isWall(psStruct->pStructureType->type) && isBuildableOnWalls(stats->type))
	{
		return false;
	}
	if (IsStatExpansionModule(stats))
	{
		return false;
	}
	return true;
}

static void drawLineBuild(uint8_t player, STRUCTURE_STATS const *psStats, Vector2i pos, Vector2i pos2, uint16_t direction, STRUCT_STATES state, bool perTile = false)
{
	auto lb = calcLineBuild(psStats, direction, pos, pos2);

	for (int i = 0; i < lb.count; ++i)
	{
		Vector2i cur = lb[i];
		if (tileHasIncompatibleStructure(worldTile(gameWorld.map, cur), psStats, 0))
		{
			continue;  // construction has started
		}

		StructureBounds b = getStructureBounds(psStats, cur, direction);
		int z = 0;
		for (int j = 0; j <= b.size.y; ++j)
			for (int k = 0; k <= b.size.x; ++k)
			{
				z = std::max(z, map_TileHeight(gameWorld.map, b.map.x + k, b.map.y + j));
			}
		// MOD (23.11. 18:38): beim Platzieren jeden Platz einzeln pruefen - nur blockierte Plaetze rot, die anderen gruen (werden gebaut)
		STRUCT_STATES st = state;
		if (perTile && state == SS_BLUEPRINT_VALID && !validLocation(gameWorld, const_cast<STRUCTURE_STATS *>(psStats), cur, direction, player, true)) { st = SS_BLUEPRINT_INVALID; }
		Blueprint blueprint(psStats, Vector3i(cur, z), snapDirection(direction), 0, st, player); // snapDirection may be unnecessary here
		blueprints.push_back(blueprint);
	}
}

static void renderBuildOrder(uint8_t droidPlayer, DroidOrder const &order, STRUCT_STATES state)
{
	STRUCTURE_STATS const *stats;
	Vector2i pos = order.pos;
	if (order.type == DORDER_BUILDMODULE)
	{
		STRUCTURE const *structure = castStructure(order.psObj);
		if (structure == nullptr)
		{
			return;
		}
		stats = getModuleStat(structure);
		pos = structure->pos.xy();
	}
	else
	{
		stats = order.psStats;
	}

	if (stats == nullptr)
	{
		return;
	}

	//draw the current build site if its a line of structures
	if (order.type == DORDER_LINEBUILD)
	{
		drawLineBuild(droidPlayer, stats, pos, order.pos2, order.direction, state);
	}
	if ((order.type == DORDER_BUILD || order.type == DORDER_BUILDMODULE) && !tileHasIncompatibleStructure(mapTile(gameWorld.map, map_coord(pos)), stats, order.index))
	{
		StructureBounds b = getStructureBounds(stats, pos, order.direction);
		int z = 0;
		for (int j = 0; j <= b.size.y; ++j)
			for (int i = 0; i <= b.size.x; ++i)
			{
				z = std::max(z, map_TileHeight(gameWorld.map, b.map.x + i, b.map.y + j));
			}
		Blueprint blueprint(stats, Vector3i(pos, z), snapDirection(order.direction), order.index, state, droidPlayer);
		blueprints.push_back(blueprint);
	}
}

std::unique_ptr<Blueprint> playerBlueprint = std::make_unique<Blueprint>();
std::unique_ptr<ValueTracker> playerBlueprintX = std::make_unique<ValueTracker>();
std::unique_ptr<ValueTracker> playerBlueprintY = std::make_unique<ValueTracker>();
std::unique_ptr<ValueTracker> playerBlueprintZ = std::make_unique<ValueTracker>();
std::unique_ptr<ValueTracker> playerBlueprintDirection = std::make_unique<ValueTracker>();

void displayBlueprints(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	blueprints.clear();  // Delete old blueprints and draw new ones.

	if ((buildState == BUILD3D_VALID || buildState == BUILD3D_POS) &&
	    sBuildDetails.x > 0 && sBuildDetails.x < (int)gameWorld.map.width &&
	    sBuildDetails.y > 0 && sBuildDetails.y < (int)gameWorld.map.height)
	{
		STRUCT_STATES state;
		if (buildState == BUILD3D_VALID)
		{
			state = SS_BLUEPRINT_VALID;
		}
		else
		{
			state = SS_BLUEPRINT_INVALID;
		}
		// we are placing a building or a delivery point
		if (STRUCTURE_STATS *stats = castStructureStats(sBuildDetails.psStats))
		{
			// it's a building
			uint16_t direction = getBuildingDirection();
			if (wallDrag.status == DRAG_PLACING || wallDrag.status == DRAG_DRAGGING)
			{
				if (modFieldBuild())
				{
					// MOD (20.11. 19:23): Feldbau-Vorschau - alle Zeilen des Rechtecks
					for (const auto &row : calcFieldRows(stats, direction, wallDrag.pos, wallDrag.pos2))
					{
						drawLineBuild(selectedPlayer, stats, row.first, row.second, direction, state, true);
					}
				}
				else
				{
					drawLineBuild(selectedPlayer, stats, wallDrag.pos, wallDrag.pos2, direction, state, true);
				}
			}
			else
			{
				unsigned width, height;
				if ((direction & 0x4000) == 0)
				{
					width  = sBuildDetails.width;
					height = sBuildDetails.height;
				}
				else
				{
					// Rotated 90°, swap width and height
					width  = sBuildDetails.height;
					height = sBuildDetails.width;
				}
				// a single building
				Vector2i pos(world_coord(sBuildDetails.x) + world_coord(width) / 2, world_coord(sBuildDetails.y) + world_coord(height) / 2);

				StructureBounds b = getStructureBounds(stats, pos, direction);
				int z = 0;
				for (int j = 0; j <= b.size.y; ++j)
					for (int i = 0; i <= b.size.x; ++i)
					{
						z = std::max(z, map_TileHeight(gameWorld.map, b.map.x + i, b.map.y + j));
					}

				if(!playerBlueprintX->isTracking()){
					playerBlueprintX->startTracking(pos.x)->setSpeed(BlueprintTrackAnimationSpeed);
					playerBlueprintY->startTracking(pos.y)->setSpeed(BlueprintTrackAnimationSpeed);
					playerBlueprintZ->startTracking(z)->setSpeed(BlueprintTrackAnimationSpeed);
					playerBlueprintDirection->startTracking(direction)->setSpeed(BlueprintTrackAnimationSpeed+30);
				}

				playerBlueprintX->setTarget(pos.x)->update();
				playerBlueprintY->setTarget(pos.y)->update();
				playerBlueprintZ->setTarget(z)->update();

				if(playerBlueprintDirection->reachedTarget()){
					playerBlueprintDirection->startTracking(playerBlueprintDirection->getTarget());
					playerBlueprintDirection->setTargetDelta((SWORD)(direction - playerBlueprintDirection->getTarget()));
				}

				playerBlueprintDirection->update();

				playerBlueprint->stats = stats;
				playerBlueprint->pos = { playerBlueprintX->getCurrent(), playerBlueprintY->getCurrent(), playerBlueprintZ->getCurrent() };
				playerBlueprint->dir = playerBlueprintDirection->getCurrent();
				playerBlueprint->index = 0;
				playerBlueprint->state = state;
				playerBlueprint->player = selectedPlayer;

				blueprints.push_back(*playerBlueprint);
			}
		}
	} else {
		playerBlueprintX->stopTracking();
		playerBlueprintY->stopTracking();
		playerBlueprintZ->stopTracking();
		playerBlueprintDirection->stopTracking();
	}

	// MOD (16.11. 19:54): Befestigungsbau - der ganze Bauplan erscheint gruen schon beim Drueberfahren (vor dem Klick)
	if (const std::vector<modbuild::Placement> *plan = modbuild::previewPlan())
	{
		for (const modbuild::Placement &p : *plan)
		{
			if (p.stats == nullptr) { continue; }
			const Vector2i w1 = modbuild::planTileWorld(p.tile);
			const Vector2i w2 = modbuild::planTileWorld(p.tile2);
			if (p.line)
			{
				drawLineBuild(selectedPlayer, p.stats, w1, w2, static_cast<uint16_t>(p.direction), SS_BLUEPRINT_VALID);
				continue;
			}
			StructureBounds b = getStructureBounds(p.stats, w1, static_cast<uint16_t>(p.direction));
			int z = 0;
			for (int j = 0; j <= b.size.y; ++j)
			{
				for (int i = 0; i <= b.size.x; ++i) { z = std::max(z, map_TileHeight(gameWorld.map, b.map.x + i, b.map.y + j)); }
			}
			blueprints.push_back(Blueprint(p.stats, Vector3i(w1, z), snapDirection(static_cast<uint16_t>(p.direction)), 0, SS_BLUEPRINT_VALID, selectedPlayer));
		}
	}

	// now we draw the blueprints for all ordered buildings
	for (unsigned player = 0; player < MAX_PLAYERS; ++player)
	{
		if (!hasSharedVision(selectedPlayer, player) && !NetPlay.players[selectedPlayer].isSpectator)
		{
			continue;
		}
		STRUCT_STATES state = player == selectedPlayer ? SS_BLUEPRINT_PLANNED : SS_BLUEPRINT_PLANNED_BY_ALLY;
		for (const DROID *psDroid : gameWorld.objects.droids[player])
		{
			if (psDroid->droidType == DROID_CONSTRUCT || psDroid->droidType == DROID_CYBORG_CONSTRUCT)
			{
				renderBuildOrder(psDroid->player, psDroid->order, state);
				//now look thru' the list of orders to see if more building sites
				for (int order = psDroid->listPendingBegin; order < (int)psDroid->asOrderList.size(); order++)
				{
					renderBuildOrder(psDroid->player, psDroid->asOrderList[order], state);
				}
			}
		}
	}

	// Erase duplicate blueprints.
	std::sort(blueprints.begin(), blueprints.end());
	blueprints.erase(std::unique(blueprints.begin(), blueprints.end()), blueprints.end());

	// MOD (23.11. 18:38): Stellungs-Vorschauen verbinden sich mit den Nachbar-Bauplaenen (wie das fertige Netz)
	{
		std::vector<std::pair<Vector2i, const STRUCTURE_STATS *>> fortBps;
		for (const auto &bp : blueprints) { if (bp.player == selectedPlayer && bp.stats != nullptr) { fortBps.emplace_back(bp.pos.xy(), bp.stats); } }
		modfort::setBlueprintNeighbours(fortBps, selectedPlayer);
	}
	// Actually render everything.
	for (auto &blueprint : blueprints)
	{
		blueprint.renderBlueprint(viewMatrix, perspectiveViewMatrix);
	}

	// MOD (Wunsch 16.11. 21:01): Wegpunktmarker der ausgewaehlten Einheiten erscheinen wieder, solange Shift (oder Strg) gehalten
	// wird - auch nach dem Ab- und wieder Anwaehlen (vorher nur einmal beim Setzen des Wegpunkts). Nur Anzeige.
	if (selectedPlayer < MAX_PLAYERS && pProximityMsgIMD != nullptr && pProximityMsgIMD->displayModel() != nullptr
	    && (keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT) || keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL)))
	{
		std::vector<Vector3i> marks;
		auto addMark = [&](const DroidOrder &o) {
			if (o.type == DORDER_NONE || o.type == DORDER_BUILD || o.type == DORDER_LINEBUILD) { return; }
			Vector3i p(0, 0, 0);
			if (o.psObj != nullptr && !o.psObj->died)
			{
				p = o.psObj->pos.xzy();
			}
			else if (o.pos.x > 0 || o.pos.y > 0)
			{
				p.x = o.pos.x;
				p.z = o.pos.y;
			}
			else
			{
				return;
			}
			if (!worldOnMap(gameWorld.map, p.x, p.z)) { return; }
			p.y = map_Height(gameWorld.map, p.x, p.z) + 32;
			if (o.psObj != nullptr && !o.psObj->died && o.psObj->sDisplay.imd != nullptr) { p.y += o.psObj->sDisplay.imd->max.y; }
			for (const Vector3i &m : marks) { if (std::abs(m.x - p.x) < 24 && std::abs(m.z - p.z) < 24) { return; } }
			marks.push_back(p);
		};
		for (const DROID *psDroid : gameWorld.objects.droids[selectedPlayer])
		{
			if (!psDroid->selected || psDroid->died) { continue; }
			addMark(psDroid->order);
			for (unsigned n = psDroid->listPendingBegin; n < psDroid->asOrderList.size(); ++n) { addMark(psDroid->asOrderList[n]); }
			if (marks.size() > 200) { break; }
		}
		for (const Vector3i &m : marks)
		{
			const glm::mat4 modelMatrix = glm::translate(glm::vec3(m.x, m.y, -m.z));
			pie_Draw3DShape(pProximityMsgIMD->displayModel(), 0, 0, WZCOL_WHITE, 0, 0, modelMatrix, viewMatrix);
		}
	}

	renderDeliveryRepos(viewMatrix, perspectiveViewMatrix);
}

/// Draw Factory Delivery Points
static void displayDelivPoints(const glm::mat4& viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	WZ_PROFILE_SCOPE(displayDelivPoints);
	if (selectedPlayer >= MAX_PLAYERS) { return; /* no-op */ }
	for (const auto& psDelivPoint : gameWorld.objects.flags[selectedPlayer])
	{
		if (clipXY(psDelivPoint->coords.x, psDelivPoint->coords.y))
		{
			renderDeliveryPoint(psDelivPoint, false, viewMatrix, perspectiveViewMatrix);
		}
	}
}

bool clipFeatureOnScreen(FEATURE *psFeature)
{
	StructureBounds b = getStructureBounds(psFeature);
	assert(b.size.x != 0 && b.size.y != 0);
	int overdrawTiles = (b.size.x < 2 && b.size.y < 2) ? 1 : 2; // make room for shadows on the terrain
	int start = -overdrawTiles;
	int endB = (b.size.y + overdrawTiles) - 1;
	int endW = (b.size.x + overdrawTiles) - 1;
	for (int breadth = start; breadth <= endB; ++breadth)
	{
		for (int width = start; width <= endW; ++width)
		{
			if (width != start && width != endW && breadth != start && breadth != endB)
			{
				continue; // skip interior
			}
			if (clipXY(world_coord(b.map.x + width), world_coord(b.map.y + breadth)))
			{
				return true;
			}
		}
	}

	return false;
}

/// Draw the features
// MOD (Wunsch 16.11. 22:58 "nutz doch die MF-Modelle!"): Im Orbit haengt unter jeder Insel ein echter MF-Asteroid (misc.tbd, drei
// Groessen): flache Plattform oben (liegt knapp ueber den Inselfeldern), Felskoerper nach unten spitz zulaufend. Die Leere-Felder
// schreiben keine Tiefe (terrain.cpp), daher sieht man die Asteroiden bis unten; darunter der verschwommene Planetenboden.
static void displayOrbitAsteroids(const glm::mat4 &viewMatrix)
{
	if (modlayermap::viewLayer() != modlayer::ORBIT) { return; }
	static std::map<std::string, const iIMDShape *> cache;
	static unsigned cacheGen = 0;
	if (modelCacheStale(cacheGen)) { cache.clear(); }   // MOD (18.11.): Modelle wurden neu geladen
	for (const modlayermap::IslandModel &m : modlayermap::orbitIslandModels())
	{
		// (17.11.) kein Entfernungs-Abschnitt: die Plattform-Felder der Insel zeichnet das Gelaende auch in der Ferne - ohne den
		// Asteroiden darunter sahen ferne Inseln wie flache Flecken aus (es sind nur wenige Modelle)
		auto it = cache.find(m.model);
		if (it == cache.end())
		{
			const iIMDBaseShape *b = modelGet(WzString::fromUtf8(m.model));
			it = cache.emplace(m.model, b ? b->displayModel() : nullptr).first;
		}
		if (it->second == nullptr) { continue; }
		const int h = map_Height(gameWorld.map, m.worldX, m.worldY);
		const glm::mat4 modelMatrix = glm::translate(glm::vec3(static_cast<float>(m.worldX), static_cast<float>(h) + 2.f, -static_cast<float>(m.worldY)))
			* glm::rotate(UNDEG(DEG(90 * m.rot)), glm::vec3(0.f, 1.f, 0.f))
			* glm::scale(glm::vec3(m.scale));
		pie_Draw3DShape(it->second, 0, 0, WZCOL_WHITE, pie_SHADOW, 0, modelMatrix, viewMatrix);
	}
}

// MOD (17.11. 14:14) "Beim Krater sieht man einen kegelfoermigen Erdhaufen. Den schuettet der Bagger dann hinein.": je Krater ein
// Erdkegel (effects/craterpile.pie, Mod-Daten craterpile.py) am Rand, Groesse nach Resttiefe - beim Zuschaufeln schrumpft er.
static void displayCraterPiles(const glm::mat4 &viewMatrix)
{
	static const iIMDShape *pile = nullptr;
	static unsigned tried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
	if (modelCacheStale(tried))
	{
		const iIMDBaseShape *b = modelGet("craterpile.pie");
		pile = b != nullptr ? b->displayModel() : nullptr;
	}
	if (selectedPlayer >= MAX_PLAYERS) { return; }
	// (Wunsch 18.11. 13:04) Kraterfleck: ein Viertel Kachel in der Kachelmitte, weicher Rand, folgt der Trichter-Mulde (Mitte tiefer)
	{
		static const iIMDShape *decal = nullptr;
		static unsigned dTried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
		if (modelCacheStale(dTried))
		{
			const iIMDBaseShape *b = modelGet("modcraterdecal.pie");
			decal = b != nullptr ? b->displayModel() : nullptr;
		}
		static std::vector<modterrain::CraterDecal> decals;
		if (decal != nullptr)
		{
			modterrain::craterDecals(decals);
			for (const modterrain::CraterDecal &d : decals)
			{
				if (!quickClipXYToMaximumTilesFromCurrentPosition(d.worldX, d.worldY)) { continue; }
				if (!tileOnMap(gameWorld.map, d.tileX, d.tileY) || !tileOnMap(gameWorld.map, d.tileX + 1, d.tileY + 1)) { continue; }
				MAPTILE *t = mapTile(gameWorld.map, d.tileX, d.tileY);
				if (t == nullptr || !TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(t)) { continue; }
				// Kachelecken (ohne Krater) -> Grundhoehe und Neigung; Mitte = Trichterboden
				const float h00 = static_cast<float>(map_TileHeight(gameWorld.map, d.tileX, d.tileY));
				const float h10 = static_cast<float>(map_TileHeight(gameWorld.map, d.tileX + 1, d.tileY));
				const float h01 = static_cast<float>(map_TileHeight(gameWorld.map, d.tileX, d.tileY + 1));
				const float h11 = static_cast<float>(map_TileHeight(gameWorld.map, d.tileX + 1, d.tileY + 1));
				constexpr int NB = DIST_TILE / TILE_UNITS;
				// (Wunsch 23.11. 04:23 "Man kann oft unter die Krater schauen ... das Terrain wie die Krater geformt nach unten, in derselben
				// Groesse"): der Fleck hat die Trichterform des Gelaendes (Mitte tief, zu den Kanten des 128er-Feldes hin flach). Er passt nur,
				// wenn er weder gedreht (ausser in 90-Grad-Schritten) noch skaliert wird - vorher lag er schief/zu gross ueber der Mulde und
				// schwebte stellenweise. Feinraster: das Gelaende senkt jetzt den gemeinsamen Mittel-Eckpunkt (modterrain::sinkAtCorner) -
				// dieselbe Trichterform ueber dem 2 x 2-Kraterblock.
				float base = (h00 + h10 + h01 + h11) * 0.25f;
				float sink = static_cast<float>(modterrain::centerSink(gameWorld.map, d.tileX, d.tileY));
				float gx = ((h10 + h11) - (h00 + h01)) / (2.f * TILE_UNITS);
				float gyW = ((h01 + h11) - (h00 + h10)) / (2.f * TILE_UNITS);
				if (NB > 1 && tileOnMap(gameWorld.map, d.tileX + NB, d.tileY + NB))
				{
					const float c00 = static_cast<float>(map_TileHeight(gameWorld.map, d.tileX, d.tileY));
					const float c20 = static_cast<float>(map_TileHeight(gameWorld.map, d.tileX + NB, d.tileY));
					const float c02 = static_cast<float>(map_TileHeight(gameWorld.map, d.tileX, d.tileY + NB));
					const float c22 = static_cast<float>(map_TileHeight(gameWorld.map, d.tileX + NB, d.tileY + NB));
					base = h11;   // Mittel-Eckpunkt des Blocks (ohne Absenkung)
					sink = static_cast<float>(modterrain::sinkAtCorner(gameWorld.map, d.tileX + 1, d.tileY + 1));
					gx = ((c20 + c22) - (c00 + c02)) / (2.f * DIST_TILE);
					gyW = ((c02 + c22) - (c00 + c20)) / (2.f * DIST_TILE);
				}
				glm::mat4 shear(1.f);
				shear[0][1] = gx;      // Hoehe nimmt mit x zu
				shear[2][1] = -gyW;    // Modell-z = -Karten-y
				const float ys = std::clamp(sink / 26.f, 0.f, 1.5f);
				const int rot90 = (d.rotDeg / 90) * 90;   // nur Vierteldrehungen: die Trichterform bleibt deckungsgleich
				const glm::mat4 modelMatrix = glm::translate(glm::vec3(static_cast<float>(d.worldX), base - sink + 1.5f, -static_cast<float>(d.worldY)))
					* shear * glm::scale(glm::vec3(1.f, ys, 1.f)) * glm::rotate(UNDEG(DEG(rot90)), glm::vec3(0.f, 1.f, 0.f));
				const int alpha = std::clamp(static_cast<int>(d.alpha * 255.f), 0, 255);
				if (alpha > 0)
				{
					pie_Draw3DShape(decal, 0, 0, WZCOL_WHITE, pie_TRANSLUCENT, alpha, modelMatrix, viewMatrix);
				}
				// MOD (Wunsch 22.11. 23:41 "in jeden Krater ein Debris Wreck0 - Wreck5, plus die Erdhaufen und den kleinen Erdrand, dann sieht
				// es endlich wie ein richtiger Krater aus"): Truemmer (Vanilla-Merkmal-Modelle miwreck/miwrek1-5) auf dem Kraterboden, ein
				// niedriger Erdwall rundherum (effects/modcraterrim.pie) und zwei kleine Erdhaufen am Rand - alles schrumpft beim Zuschaufeln
				if (d.alpha >= 0.6f)
				{
					static const iIMDShape *wrecks[6] = {};
					static const iIMDShape *rim = nullptr;
					static unsigned wTried = 0;
					if (modelCacheStale(wTried))
					{
						static const char *names[6] = { "miwreck.pie", "miwrek1.pie", "miwrek2.pie", "miwrek3.pie", "miwrek4.pie", "miwrek5.pie" };
						for (int k = 0; k < 6; ++k) { const iIMDBaseShape *b = modelGet(names[k]); wrecks[k] = b != nullptr ? b->displayModel() : nullptr; }
						const iIMDBaseShape *r = modelGet("modcraterrim.pie");
						rim = r != nullptr ? r->displayModel() : nullptr;
					}
					const unsigned hh = static_cast<unsigned>(d.tileX) * 73856093u ^ static_cast<unsigned>(d.tileY) * 19349663u;
					const float dep = std::clamp(d.depth, 0.f, 1.f);
					const float floorY = base - sink;
					if (rim != nullptr)
					{
						// (23.11.) der Erdwall folgt innen der Trichterflanke (craterrim.py) - ebenfalls nur Vierteldrehungen, nicht skaliert;
						// Hoehe nach der Trichtertiefe (sonst schwebte der innere Fuss ueber der Mulde)
						const float rimY = std::max(0.2f, std::clamp(sink / 26.f, 0.f, 1.5f));
						const glm::mat4 rm = glm::translate(glm::vec3(static_cast<float>(d.worldX), base - 1.f, -static_cast<float>(d.worldY)))
							* shear * glm::scale(glm::vec3(1.f, rimY, 1.f)) * glm::rotate(UNDEG(DEG(rot90 + 90)), glm::vec3(0.f, 1.f, 0.f));
						pie_Draw3DShape(rim, 0, 0, WZCOL_WHITE, pie_SHADOW, 0, rm, viewMatrix);
					}
					const iIMDShape *w = wrecks[hh % 6u];
					if (w != nullptr)
					{
						const float ws = 0.62f * d.scale * (0.55f + 0.45f * dep);
						const glm::mat4 wm = glm::translate(glm::vec3(static_cast<float>(d.worldX), floorY + 0.5f, -static_cast<float>(d.worldY)))
							* shear * glm::rotate(UNDEG(DEG(static_cast<int>((hh >> 7) % 360u))), glm::vec3(0.f, 1.f, 0.f)) * glm::scale(glm::vec3(ws, ws * 0.7f, ws));
						pie_Draw3DShape(w, 0, 0, WZCOL_WHITE, pie_SHADOW, 0, wm, viewMatrix);
					}
					if (pile != nullptr)
					{
						for (int k = 0; k < 2; ++k)
						{
							const float a = glm::radians(static_cast<float>(d.rotDeg + 110 + k * 130));
							const float rr = 58.f * d.scale;
							const int px = d.worldX + static_cast<int>(std::cos(a) * rr), py = d.worldY + static_cast<int>(std::sin(a) * rr);
							if (!worldOnMap(gameWorld.map, px, py)) { continue; }
							const float ps = (0.42f - 0.1f * static_cast<float>(k)) * std::max(0.3f, dep);
							// (23.11.) auf der Trichterflanke: sichtbares Gelaende = Grundhoehe (ohne die harte Spiellogik-Absenkung) minus Trichteranteil
							const float edge = static_cast<float>(DIST_TILE) * 0.5f;
							const float fr = std::max(0.f, 1.f - std::max(std::fabs(static_cast<float>(px - d.worldX)), std::fabs(static_cast<float>(py - d.worldY))) / edge);
							const glm::mat4 pm = glm::translate(glm::vec3(static_cast<float>(px), static_cast<float>(map_Height(gameWorld.map, px, py) + modterrain::sinkAt(gameWorld.map, px, py)) - 1.f - sink * fr, -static_cast<float>(py)))
								* glm::rotate(UNDEG(DEG(static_cast<int>((hh >> (3 + k * 5)) % 360u))), glm::vec3(0.f, 1.f, 0.f)) * glm::scale(glm::vec3(ps, ps * 0.8f, ps));
							pie_Draw3DShape(pile, 0, 0, WZCOL_WHITE, pie_SHADOW, 0, pm, viewMatrix);
						}
					}
				}
			}
		}
	}
	if (pile == nullptr) { return; }
	static std::vector<modterrain::CraterPile> piles;
	modterrain::craterPiles(piles);
	for (const modterrain::CraterPile &p : piles)
	{
		if (!quickClipXYToMaximumTilesFromCurrentPosition(p.worldX, p.worldY)) { continue; }
		if (!worldOnMap(gameWorld.map, p.worldX, p.worldY)) { continue; }
		MAPTILE *t = worldTile(gameWorld.map, p.worldX, p.worldY);
		if (t == nullptr || !TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(t)) { continue; }
		// Boden etwas weiter draussen (ausserhalb der Mulde) als Fusshoehe
		const int gx = std::clamp(p.worldX + (p.worldX % 7) - 3, 0, world_coord(gameWorld.map.width) - 1);
		// (23.11.) sichtbares Gelaende: Grundhoehe (ohne die harte Spiellogik-Absenkung der Kraterfelder) minus Trichteranteil
		const int raw = std::max(map_Height(gameWorld.map, p.worldX, p.worldY) + modterrain::sinkAt(gameWorld.map, p.worldX, p.worldY),
		                         map_Height(gameWorld.map, gx, p.worldY) + modterrain::sinkAt(gameWorld.map, gx, p.worldY));
		const int h = raw - 2 - static_cast<int>(p.funnel);
		const glm::mat4 modelMatrix = glm::translate(glm::vec3(static_cast<float>(p.worldX), static_cast<float>(h), -static_cast<float>(p.worldY)))
			* glm::rotate(UNDEG(DEG(p.rotDeg)), glm::vec3(0.f, 1.f, 0.f)) * glm::scale(glm::vec3(p.scale, std::max(0.35f, p.scale), p.scale));
		pie_Draw3DShape(pile, 0, 0, WZCOL_WHITE, pie_SHADOW, 0, modelMatrix, viewMatrix);
	}
}

static void displayFeatures(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	WZ_PROFILE_SCOPE(displayFeatures);
	// player can only be 0 for the features.

	/* Go through all the features */
	for (BASE_OBJECT* obj : gameWorld.objects.features[0])
	{
		if (obj->type == OBJ_FEATURE
			&& (obj->died == 0 || obj->died > graphicsTime)
			&& quickClipXYToMaximumTilesFromCurrentPosition(obj->pos.x, obj->pos.y))
		{
			FEATURE* psFeature = castFeature(obj);
			if (clipFeatureOnScreen(psFeature))
			{
				renderFeature(psFeature, viewMatrix, perspectiveViewMatrix);
			}
		}
	}

	// Walk through destroyed objects.

	/* Go through all the features */
	for (BASE_OBJECT* obj : psDestroyedObj)
	{
		if (obj->type == OBJ_FEATURE
			&& (obj->died == 0 || obj->died > graphicsTime)
			&& clipXY(obj->pos.x, obj->pos.y))
		{
			FEATURE* psFeature = castFeature(obj);
			renderFeature(psFeature, viewMatrix, perspectiveViewMatrix);
		}
	}
}

/// Draw the Proximity messages for the *SELECTED PLAYER ONLY*
static void displayProximityMsgs(const glm::mat4& viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	if (selectedPlayer >= MAX_PLAYERS) { return; /* no-op */ }

	/* Go through all the proximity Displays*/
	for (PROXIMITY_DISPLAY* psProxDisp : apsProxDisp[selectedPlayer])
	{
		if (!(psProxDisp->psMessage->read))
		{
			unsigned x, y;
			if (psProxDisp->type == POS_PROXDATA)
			{
				if (!psProxDisp->psMessage->pViewData)
				{
					continue;
				}
				VIEW_PROXIMITY *pViewProximity = (VIEW_PROXIMITY *)psProxDisp->psMessage->pViewData->pData;
				if (!pViewProximity)
				{
					continue;
				}
				x = pViewProximity->x;
				y = pViewProximity->y;
			}
			else
			{
				if (!psProxDisp->psMessage->psObj)
				{
					continue;    // sanity check
				}
				x = psProxDisp->psMessage->psObj->pos.x;
				y = psProxDisp->psMessage->psObj->pos.y;
			}
			/* Is the Message worth rendering? */
			if (clipXY(x, y))
			{
				renderProximityMsg(psProxDisp, viewMatrix, perspectiveViewMatrix);
			}
		}
	}
}

/// Draw the droids
static void displayDynamicObjects(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	WZ_PROFILE_SCOPE(displayDynamicObjects);
	WZ_PERF_SCOPE(T_displayDynamicObjects);
	/* Need to go through all the droid lists */
	for (unsigned player = 0; player < MAX_PLAYERS; ++player)
	{
		for (DROID* psDroid : gameWorld.objects.droids[player])
		{
			if (!psDroid || (psDroid->died != 0 && psDroid->died < graphicsTime)
			    || !quickClipXYToMaximumTilesFromCurrentPosition(psDroid->pos.x, psDroid->pos.y))
			{
				continue;
			}

			/* No point in adding it if you can't see it? */
			if (psDroid->visibleForLocalDisplay())
			{
				displayComponentObject(psDroid, viewMatrix, perspectiveViewMatrix);
			}
		}
	}

	// Walk through destroyed objects.
	for (const auto& obj : psDestroyedObj)
	{
		DROID* psDroid = castDroid(obj);
		if (!psDroid || (obj->died != 0 && obj->died < graphicsTime)
			|| !quickClipXYToMaximumTilesFromCurrentPosition(obj->pos.x, obj->pos.y))
		{
			continue;
		}

		/* No point in adding it if you can't see it? */
		if (psDroid->visibleForLocalDisplay())
		{
			displayComponentObject(psDroid, viewMatrix, perspectiveViewMatrix);
		}
	}
}

/// Sets the player's position and view angle - defaults player rotations as well
void setViewPos(UDWORD x, UDWORD y, WZ_DECL_UNUSED bool Pan)
{
	playerPos.p.x = world_coord(x);
	playerPos.p.z = world_coord(y);
	playerPos.r.z = 0;

	updatePlayerAverageCentreTerrainHeight();

	if(playerPos.p.y < averageCentreTerrainHeight)
	{
		playerPos.p.y = static_cast<int>(std::lround(calculateCameraHeight(averageCentreTerrainHeight)));
	}

	if (getWarCamStatus())
	{
		camToggleStatus();
	}
}

/// Get the player position
Vector2i getPlayerPos()
{
	return playerPos.p.xz();
}

/// Set the player position
void setPlayerPos(const WorldMapState& mapState, SDWORD x, SDWORD y)
{
	ASSERT(x >= 0 && x < world_coord(mapState.width) && y >= 0 && y < world_coord(mapState.height), "Position off map");
	playerPos.p.x = x;
	playerPos.p.z = y;
	playerPos.r.z = 0;
}

/// Get the distance at which the player views the world
float getViewDistance()
{
	return distance;
}

bool getIsCloseDistance()
{
	return bIsCloseDistance;
}

/// Set the distance at which the player views the world
void setViewDistance(float dist)
{
	distance = dist;
	bIsCloseDistance = (distance < 750.f);
	debug(LOG_WZ, _("Setting zoom to %.0f"), distance);
}

/// Draw a feature (tree/rock/etc.)
void	renderFeature(FEATURE *psFeature, const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	PIELIGHT brightness = pal_SetBrightness(200);
	// MOD (Wunsch 18.11. 12:51 "Man sieht Objekte und Gebaeude von der Oberflaeche" im Orbit): auch "von Anfang an sichtbare"
	// Merkmale (Bunker-Ruinen, Baeume ...) nur in ihrer eigenen Ebene zeichnen
	bool bForceDraw = (getRevealStatus() && psFeature->psStats->visibleAtStart && !modlayer::hiddenInView(psFeature));
	int pieFlags = 0;

	if (modgroundfire::isMarkerStats(psFeature->psStats))   // MOD: Bodenziel-Punkt wird nie gezeichnet und nie angeklickt
	{
		return;
	}

	if (!psFeature->visibleForLocalDisplay() && !bForceDraw)
	{
		return;
	}

	/* Mark it as having been drawn */
	psFeature->sDisplay.frameNumber = currentGameFrame;

	// MOD: Wracks liegen frei (ohne Kachelbindung) und werden waehrend ihrer Todesanimation gesondert bewegt
	const bool isWreck = modwreck::isWreckStats(psFeature->psStats);
	modwreck::Placement wreckPose{};
	const bool wreckAnimated = isWreck && modwreck::pose(psFeature, graphicsTime, wreckPose);

	/* Daft hack to get around the oil derrick issue */
	if (!isWreck && !TileHasFeature(mapTile(gameWorld.map, map_coord(psFeature->pos.xy()))))
	{
		return;
	}

	Vector3i dv = Vector3i(
	         psFeature->pos.x,
	         psFeature->pos.z, // features sits at the height of the tile it's centre is on
	         -(psFeature->pos.y)
	     );

	Vector3i rotation;
	/* Get all the pitch,roll,yaw info */
	rotation.y = -psFeature->rot.direction;
	rotation.x = psFeature->rot.pitch;
	rotation.z = psFeature->rot.roll;
	if (wreckAnimated)
	{
		dv = Vector3i(wreckPose.x, wreckPose.z, -wreckPose.y);
		rotation.x = wreckPose.pitch;
		rotation.z = wreckPose.roll;
	}

	glm::mat4 modelMatrix = glm::translate(glm::vec3(dv)) *
		glm::rotate(UNDEG(rotation.y), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(rotation.x), glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(rotation.z), glm::vec3(0.f, 0.f, 1.f));

	if (psFeature->psStats->subType == FEAT_SKYSCRAPER)
	{
		modelMatrix *= objectShimmy((BASE_OBJECT *)psFeature);
	}

	if (!getRevealStatus())
	{
		brightness = pal_SetBrightness(avGetObjLightLevel((BASE_OBJECT *)psFeature, brightness.byte.r));
	}
	if (!hasSensorOnTile(mapTile(gameWorld.map, map_coord(psFeature->pos.x), map_coord(psFeature->pos.y)), selectedPlayer))
	{
		brightness.byte.r /= 2;
		brightness.byte.g /= 2;
		brightness.byte.b /= 2;
	}

	if (psFeature->psStats->subType == FEAT_BUILDING
	    || psFeature->psStats->subType == FEAT_SKYSCRAPER
	    || psFeature->psStats->subType == FEAT_GEN_ARTE
	    || psFeature->psStats->subType == FEAT_BOULDER
	    || psFeature->psStats->subType == FEAT_VEHICLE
	    || psFeature->psStats->subType == FEAT_TREE
	    || psFeature->psStats->subType == FEAT_OIL_DRUM)
	{
		/* these cast a shadow */
		pieFlags = pie_SHADOW;
	}

	iIMDBaseShape *imd = psFeature->sDisplay.imd;
	if (imd && isWreck && modwreck::charred(psFeature))
	{
		// MOD (23.11. 03:05): hochgeschleuderter Rumpf - verkohlt (dunkler), Drehpunkt in der Modellmitte
		const iIMDShape *hm = imd->displayModel();
		modelMatrix *= glm::translate(glm::vec3(-(hm->min.x + hm->max.x) / 2.f, -(hm->min.y + hm->max.y) / 2.f, -(hm->min.z + hm->max.z) / 2.f));
		brightness.byte.r = static_cast<uint8_t>(brightness.byte.r * 45 / 100);
		brightness.byte.g = static_cast<uint8_t>(brightness.byte.g * 42 / 100);
		brightness.byte.b = static_cast<uint8_t>(brightness.byte.b * 40 / 100);
		pieFlags = pie_SHADOW;
	}
	if (imd)
	{
		const iIMDShape *strImd = imd->displayModel();

		float stretchDepth = 0.f;
		if (!(strImd->flags & iV_IMD_NOSTRETCH))
		{
			if (psFeature->psStats->subType == FEAT_TREE) // for now, only do this for trees
			{
				stretchDepth = psFeature->pos.z - psFeature->foundationDepth;
			}
		}

		// MOD (16.11. Punkt 10): abgeschossener Turm am Boden - die echte Waffe (Halterung + Rohr) liegt schraeg im Dreck
		const unsigned modPartWeapon = modrecover::partWeapon(psFeature);
		if (modPartWeapon > 0 && modPartWeapon < asWeaponStats.size() && asWeaponStats[modPartWeapon].pIMD != nullptr)
		{
			const WEAPON_STATS &ws = asWeaponStats[modPartWeapon];
			const glm::mat4 lying = modelMatrix * glm::translate(glm::vec3(0.f, 6.f, 0.f)) * glm::rotate(UNDEG(DEG(-62)), glm::vec3(0.f, 0.f, 1.f));
			if (ws.pMountGraphic != nullptr)
			{
				pie_Draw3DShape(ws.pMountGraphic->displayModel(), 0, 0, brightness, pieFlags, 0, lying, viewMatrix);
			}
			pie_Draw3DShape(ws.pIMD->displayModel(), 0, 0, brightness, pieFlags, 0, lying, viewMatrix);
		}
		else
		{
		/* Translate the feature  - N.B. We can also do rotations here should we require
		buildings to face different ways - Don't know if this is necessary - should be IMO */
		pie_Draw3DShape(strImd, 0, 0, brightness, pieFlags, 0, modelMatrix, viewMatrix, stretchDepth);
		}
	}

	setScreenDispWithPerspective(&psFeature->sDisplay, perspectiveViewMatrix * modelMatrix);
}

void renderProximityMsg(PROXIMITY_DISPLAY *psProxDisp, const glm::mat4& viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	UDWORD			msgX = 0, msgY = 0;
	Vector3i                dv(0, 0, 0);
	VIEW_PROXIMITY	*pViewProximity = nullptr;
	SDWORD			x, y, r;
	const iIMDShape	*proxImd = nullptr;

	//store the frame number for when deciding what has been clicked on
	psProxDisp->frameNumber = currentGameFrame;

	/* Get it's x and y coordinates so we don't have to deref. struct later */
	if (psProxDisp->type == POS_PROXDATA)
	{
		if (psProxDisp->psMessage->pViewData == nullptr)
		{
			return; // if no data - ignore message
		}
		pViewProximity = (VIEW_PROXIMITY *)psProxDisp->psMessage->pViewData->pData;
		if (pViewProximity)
		{
			msgX = pViewProximity->x;
			msgY = pViewProximity->y;
			/* message sits at the height specified at input*/
			dv.y = pViewProximity->z + 64;

			/* in case of a beacon message put above objects */
			if (psProxDisp->psMessage->pViewData->type == VIEW_BEACON)
			{
				if (TileIsOccupied(mapTile(gameWorld.map, msgX / TILE_UNITS, msgY / TILE_UNITS)))
				{
					dv.y = pViewProximity->z + 150;
				}
			}

		}
	}
	else if (psProxDisp->type == POS_PROXOBJ)
	{
		msgX = psProxDisp->psMessage->psObj->pos.x;
		msgY = psProxDisp->psMessage->psObj->pos.y;
		/* message sits at the height specified at input*/
		dv.y = psProxDisp->psMessage->psObj->pos.z + 64;
	}
	else
	{
		ASSERT(!"unknown proximity display message type", "Buggered proximity message type");
		return;
	}

	dv.x = msgX;
#if defined( _MSC_VER )
	#pragma warning( push )
	#pragma warning( disable : 4146 ) // warning C4146: unary minus operator applied to unsigned type, result still unsigned
#endif
	dv.z = -(static_cast<int>(msgY));
#if defined( _MSC_VER )
	#pragma warning( pop )
#endif

	/* Translate the message */
	glm::mat4 modelMatrix = glm::translate(glm::vec3(dv));

	//get the appropriate IMD
	if (pViewProximity)
	{
		switch (pViewProximity->proxType)
		{
		case PROX_ENEMY:
			proxImd = getDisplayImdFromIndex(MI_BLIP_ENEMY);
			break;
		case PROX_RESOURCE:
			proxImd = getDisplayImdFromIndex(MI_BLIP_RESOURCE);
			break;
		case PROX_ARTEFACT:
			proxImd = getDisplayImdFromIndex(MI_BLIP_ARTEFACT);
			break;
		default:
			ASSERT(!"unknown proximity display message type", "Buggered proximity message type");
			break;
		}
	}
	else
	{
		//object Proximity displays are for oil resources and artefacts
		ASSERT_OR_RETURN(, psProxDisp->psMessage->psObj->type == OBJ_FEATURE, "Invalid object type for proximity display");

		if (castFeature(psProxDisp->psMessage->psObj)->psStats->subType == FEAT_OIL_RESOURCE)
		{
			//resource
			proxImd = getDisplayImdFromIndex(MI_BLIP_RESOURCE);
		}
		else
		{
			//artefact
			proxImd = getDisplayImdFromIndex(MI_BLIP_ARTEFACT);
		}
	}

	modelMatrix *= glm::rotate(UNDEG(-playerPos.r.y), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(-playerPos.r.x), glm::vec3(1.f, 0.f, 0.f));

	if (proxImd)
	{
		pie_Draw3DShape(proxImd, getModularScaledGraphicsTime(proxImd->animInterval, proxImd->numFrames), 0, WZCOL_WHITE, pie_ADDITIVE, 192, modelMatrix, viewMatrix);
	}
	//get the screen coords for determining when clicked on
	calcFlagPosScreenCoords(&x, &y, &r, perspectiveViewMatrix * modelMatrix);
	psProxDisp->screenX = x;
	psProxDisp->screenY = y;
	psProxDisp->screenR = r;
}

static PIELIGHT getBlueprintColour(STRUCT_STATES state)
{
	switch (state)
	{
	case SS_BLUEPRINT_VALID:
		return WZCOL_LGREEN;
	case SS_BLUEPRINT_INVALID:
		return WZCOL_LRED;
	case SS_BLUEPRINT_PLANNED:
		return WZCOL_BLUEPRINT_PLANNED;
	case SS_BLUEPRINT_PLANNED_BY_ALLY:
		return WZCOL_BLUEPRINT_PLANNED_BY_ALLY;
	default:
		debug(LOG_ERROR, "this is not a blueprint");
		return WZCOL_WHITE;
	}
}

static void renderStructureTurrets(STRUCTURE *psStructure, const iIMDShape *strImd, PIELIGHT buildingBrightness, int pieFlag, int pieFlagData, int ecmFlag,
	const glm::mat4 &modelMatrix, const glm::mat4 &viewMatrix)
{
	iIMDBaseShape *mountImd[MAX_WEAPONS] = { nullptr };
	iIMDBaseShape *weaponImd[MAX_WEAPONS] = { nullptr };
	iIMDBaseShape *flashImd[MAX_WEAPONS] = { nullptr };

	int colour = getPlayerColour(psStructure->player);

	//get an imd to draw on the connector priority is weapon, ECM, sensor
	//check for weapon
	for (int i = 0; i < MAX(1, psStructure->numWeaps); i++)
	{
		if (psStructure->asWeaps[i].nStat > 0)
		{
			const int nWeaponStat = psStructure->asWeaps[i].nStat;

			weaponImd[i] = asWeaponStats[nWeaponStat].pIMD;
			mountImd[i] = asWeaponStats[nWeaponStat].pMountGraphic;
			flashImd[i] = asWeaponStats[nWeaponStat].pMuzzleGraphic;
		}
	}

	// check for ECM
	if (weaponImd[0] == nullptr && psStructure->pStructureType->pECM != nullptr)
	{
		weaponImd[0] = psStructure->pStructureType->pECM->pIMD;
		mountImd[0] = psStructure->pStructureType->pECM->pMountGraphic;
		flashImd[0] = nullptr;
	}
	// check for sensor (or repair center)
	bool noRecoil = false;
	// MOD (16.11. 21:25): der Platzhalter-Sensor ZNULLSENSOR wird nicht gezeichnet (Strommasten/Raffinerie haben Anschlusspunkte fuer
	// Kabel/Fackel - dort erschien sonst eine sich drehende Sensorschuessel)
	const bool modNullSensor = psStructure->pStructureType->pSensor != nullptr && psStructure->pStructureType->pSensor->id == "ZNULLSENSOR";
	if (weaponImd[0] == nullptr && psStructure->pStructureType->pSensor != nullptr && !modNullSensor)
	{
		weaponImd[0] =  psStructure->pStructureType->pSensor->pIMD;
		/* No recoil for sensors */
		noRecoil = true;
		mountImd[0]  =  psStructure->pStructureType->pSensor->pMountGraphic;
		flashImd[0] = nullptr;
	}

	// flags for drawing weapons
	if (psStructure->isBlueprint())
	{
		pieFlag = pie_TRANSLUCENT;
		pieFlagData = BLUEPRINT_OPACITY;
	}
	else
	{
		pieFlag = pie_SHADOW | ecmFlag;
		pieFlagData = 0;
	}

	// draw Weapon / ECM / Sensor for structure
	for (int i = 0; i < psStructure->numWeaps || i == 0; i++)
	{
		Rotation rot = structureGetInterpolatedWeaponRotation(psStructure, i, graphicsTime);
		// MOD (16.11. Punkt 10): Reparaturarm bewegt sich leicht, blaue Funken und elektrisches Zischen beim Reparieren
		if (i == 0 && psStructure->pStructureType->type == REF_REPAIR_FACILITY && psStructure->status == SS_BUILT && psStructure->pFunctionality != nullptr)
		{
			const REPAIR_FACILITY *modRf = &psStructure->pFunctionality->repairFacility;
			if (modRf->psObj != nullptr && modRf->psObj->type == OBJ_DROID)
			{
				int modPw = 0, modDw = 0;
				modrecover::repairEffects(psStructure, static_cast<const DROID *>(modRf->psObj), modPw, modDw);
				rot.pitch = static_cast<uint16_t>(rot.pitch + modPw);
				rot.direction = static_cast<uint16_t>(rot.direction + modDw);
			}
		}

		if (weaponImd[i] != nullptr && static_cast<size_t>(i) < strImd->connectors.size())
		{
			glm::mat4 matrix = glm::translate(glm::vec3(strImd->connectors[i].xzy())) * glm::rotate(UNDEG(-rot.direction), glm::vec3(0.f, 1.f, 0.f));
			float heightAboveTerrain = strImd->connectors[i].z;
			int recoilValue = noRecoil ? 0 : getRecoil(psStructure->asWeaps[i]);
			if (mountImd[i] != nullptr)
			{
				const iIMDShape *pMountDisplayIMD = mountImd[i]->displayModel();
				matrix *= glm::translate(glm::vec3(0.f, 0.f, recoilValue / 3.f));
				int animFrame = 0;
				if (pMountDisplayIMD->numFrames > 0)	// Calculate an animation frame
				{
					animFrame = getModularScaledGraphicsTime(pMountDisplayIMD->animInterval, pMountDisplayIMD->numFrames);
				}
				pie_Draw3DShape(pMountDisplayIMD, animFrame, colour, buildingBrightness, pieFlag, pieFlagData, modelMatrix * matrix, viewMatrix, -heightAboveTerrain);
				if (!pMountDisplayIMD->connectors.empty())
				{
					matrix *= glm::translate(glm::vec3(pMountDisplayIMD->connectors[0].xzy()));
				}
			}
			matrix *= glm::rotate(UNDEG(rot.pitch), glm::vec3(1.f, 0.f, 0.f));
			matrix *= glm::translate(glm::vec3(0, 0, recoilValue));

			const iIMDShape *pWeaponDisplayIMD = weaponImd[i]->displayModel();
			pie_Draw3DShape(pWeaponDisplayIMD, 0, colour, buildingBrightness, pieFlag, pieFlagData, modelMatrix * matrix, viewMatrix, -heightAboveTerrain);
			if (psStructure->status == SS_BUILT && psStructure->visibleForLocalDisplay() > (UBYTE_MAX / 2))
			{
				if (psStructure->pStructureType->type == REF_REPAIR_FACILITY)
				{
					REPAIR_FACILITY *psRepairFac = &psStructure->pFunctionality->repairFacility;
					// draw repair flash if the Repair Facility has a target which it has started work on
					if (!pWeaponDisplayIMD->connectors.empty() && psRepairFac->psObj != nullptr
					    && psRepairFac->psObj->type == OBJ_DROID)
					{
						DROID *psDroid = (DROID *)psRepairFac->psObj;
						SDWORD xdiff, ydiff;
						xdiff = (SDWORD)psDroid->pos.x - (SDWORD)psStructure->pos.x;
						ydiff = (SDWORD)psDroid->pos.y - (SDWORD)psStructure->pos.y;
						if (xdiff * xdiff + ydiff * ydiff <= (DIST_TILE * 5 / 2) * (DIST_TILE * 5 / 2))
						{
							const iIMDShape	*pRepImd = getDisplayImdFromIndex(MI_FLAME);

							matrix *= glm::translate(glm::vec3(pWeaponDisplayIMD->connectors[0].x, pWeaponDisplayIMD->connectors[0].z - 12, pWeaponDisplayIMD->connectors[0].y)) *
								glm::rotate(UNDEG(rot.direction), glm::vec3(0.f, 1.f, 0.f)) *
								glm::rotate(UNDEG(-playerPos.r.y), glm::vec3(0.f, 1.f, 0.f)) *
								glm::rotate(UNDEG(-playerPos.r.x), glm::vec3(1.f, 0.f, 0.f));
							pie_Draw3DShape(pRepImd, getModularScaledGraphicsTime(pRepImd->animInterval, pRepImd->numFrames), colour, buildingBrightness, pie_ADDITIVE, 192, modelMatrix * matrix, viewMatrix, -heightAboveTerrain);
						}
					}
				}
				else // we have a weapon so we draw a muzzle flash
				{
					const iIMDShape *pFlashDisplayIMD = (flashImd[i]) ? flashImd[i]->displayModel() : nullptr;
					drawMuzzleFlash(psStructure->asWeaps[i], pWeaponDisplayIMD, pFlashDisplayIMD, buildingBrightness, pieFlag, pieFlagData, modelMatrix * matrix, viewMatrix, heightAboveTerrain, colour);
				}
			}
		}
		// no IMD, its a baba machine gun, bunker, etc.
		else if (psStructure->asWeaps[i].nStat > 0)
		{
			if (psStructure->status == SS_BUILT)
			{
				const int nWeaponStat = psStructure->asWeaps[i].nStat;

				// get an imd to draw on the connector priority is weapon, ECM, sensor
				// check for weapon
				flashImd[i] =  asWeaponStats[nWeaponStat].pMuzzleGraphic;
				// draw Weapon/ECM/Sensor for structure
				if (flashImd[i] != nullptr)
				{
					rot = psStructure->asWeaps[i].rot; // Snap rotation so the muzzle graphic appears where it should, see aiUpdateStructure().
					const iIMDShape *pFlashDisplayIMD = flashImd[i]->displayModel();
					glm::mat4 matrix(1.f);
					// horrendous hack
					if (strImd->max.y > 80) // babatower
					{
						matrix *= glm::translate(glm::vec3(0.f, 80.f, 0.f)) * glm::rotate(UNDEG(-rot.direction), glm::vec3(0.f, 1.f, 0.f)) * glm::translate(glm::vec3(0.f, 0.f, -20.f));
					}
					else//baba bunker
					{
						matrix *= glm::translate(glm::vec3(0.f, 10.f, 0.f)) * glm::rotate(UNDEG(-rot.direction), glm::vec3(0.f, 1.f, 0.f)) * glm::translate(glm::vec3(0.f, 0.f, -40.f));
					}
					matrix *= glm::rotate(UNDEG(rot.pitch), glm::vec3(1.f, 0.f, 0.f));
					// draw the muzzle flash?
					if (psStructure->visibleForLocalDisplay() > UBYTE_MAX / 2)
					{
						// animate for the duration of the flash only
						// assume no clan colours for muzzle effects
						if (pFlashDisplayIMD->numFrames == 0 || pFlashDisplayIMD->animInterval <= 0)
						{
							// no anim so display one frame for a fixed time
							if (graphicsTime >= psStructure->asWeaps[i].lastFired && graphicsTime < psStructure->asWeaps[i].lastFired + BASE_MUZZLE_FLASH_DURATION)
							{
								pie_Draw3DShape(pFlashDisplayIMD, 0, colour, buildingBrightness, 0, 0, modelMatrix * matrix, viewMatrix); //muzzle flash
							}
						}
						else
						{
							const int frame = (graphicsTime - psStructure->asWeaps[i].lastFired) / pFlashDisplayIMD->animInterval;
							if (frame < pFlashDisplayIMD->numFrames && frame >= 0)
							{
								pie_Draw3DShape(pFlashDisplayIMD, 0, colour, buildingBrightness, 0, 0, modelMatrix * matrix, viewMatrix); //muzzle flash
							}
						}
					}
				}
			}
		}
		// if there is an unused connector, but not the first connector, add a light to it
		else if (psStructure->sDisplay.imd->displayModel()->connectors.size() > 1)
		{
			switch (psStructure->pStructureType->type)
			{
			case REF_FACTORY:
			case REF_FACTORY_MODULE:
			case REF_CYBORG_FACTORY:
			case REF_VTOL_FACTORY:
				// don't do this
				break;
			default:
				for (size_t cIdx = 1; cIdx < psStructure->sDisplay.imd->displayModel()->connectors.size(); cIdx++)
				{
					const iIMDShape *lImd = getDisplayImdFromIndex(MI_LANDING);
					pie_Draw3DShape(lImd, getModularScaledGraphicsTime(lImd->animInterval, lImd->numFrames), colour, buildingBrightness, 0, 0,
									modelMatrix * glm::translate(glm::vec3(psStructure->sDisplay.imd->displayModel()->connectors[cIdx].xzy())), viewMatrix);
				}
				break;
			}
		}
	}
}

/// Draw the structures
// MOD (16.11. Punkt 4): an jedem Gebaeude im Bau steht ein Kran (Original-WZ-Modell micrane, zerlegt: Mast + drehender Ausleger) an
// einer Ecke und schwenkt hin und her. Reine Anzeige; verschwindet, sobald das Gebaeude fertig ist. Nicht fuer Mauern und kleine
// 1x1-Verteidigungsanlagen (Graeben, Sandsaecke ...).
static void modDrawSiteCrane(const STRUCTURE *psStructure, int colour, PIELIGHT brightness, const glm::mat4 &viewMatrix)
{
	const STRUCTURE_STATS *st = psStructure->pStructureType;
	if (st == nullptr || psStructure->isBlueprint()) { return; }
	const int w = std::max(1, static_cast<int>(st->baseWidth)), b = std::max(1, static_cast<int>(st->baseBreadth));
	if (st->type == REF_WALL || st->type == REF_WALLCORNER || st->type == REF_GATE || (st->type == REF_DEFENSE && w * b <= 1)) { return; }
	static iIMDBaseShape *s_mast = nullptr, *s_jib = nullptr;
	static unsigned s_tried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
	if (modelCacheStale(s_tried))
	{
		s_mast = modelGet("scavcrane.pie");
		s_jib = modelGet("scavcranejib.pie");
	}
	if (s_mast == nullptr || s_jib == nullptr) { return; }
	// Ecke der Baustelle (im Bauwerk-Koordinatensystem), etwas ausserhalb der Grundflaeche
	const float ox = static_cast<float>(w * TILE_UNITS / 2 + 34);
	const float oz = static_cast<float>(b * TILE_UNITS / 2 + 34);
	const float dir = UNDEG(-psStructure->rot.direction);
	const float cs = std::cos(dir), sn = std::sin(dir);
	const float wx = static_cast<float>(psStructure->pos.x) + ox * cs + oz * sn;
	const float wz = static_cast<float>(psStructure->pos.y) - (-ox * sn + oz * cs);
	if (!worldOnMap(gameWorld.map, static_cast<int>(wx), static_cast<int>(wz))) { return; }
	const float gy = static_cast<float>(map_Height(gameWorld.map, static_cast<int>(wx), static_cast<int>(wz)));
	// kleine Gebaeude: kleinerer Kran
	const float scale = (w * b >= 4) ? 1.0f : 0.75f;
	const glm::mat4 base = glm::translate(glm::vec3(wx, gy, -wz)) * glm::rotate(dir, glm::vec3(0.f, 1.f, 0.f)) * glm::scale(glm::vec3(scale));
	pie_Draw3DShape(s_mast->displayModel(), 0, colour, brightness, pie_SHADOW, 0, base, viewMatrix);
	// Ausleger schwenkt langsam hin und her (je Baustelle versetzt), ab und zu eine kleine Pause
	const float t = static_cast<float>(graphicsTime % 600000u) / 1000.f + static_cast<float>(psStructure->id % 97u);
	const float swing = 1.05f * std::sin(t * 0.23f) + 0.25f * std::sin(t * 0.61f + 1.3f);
	const float yaw = 2.3f + swing + static_cast<float>(psStructure->id % 8u) * 0.4f;
	const glm::mat4 jibM = base * glm::translate(glm::vec3(0.f, 95.f, 0.f)) * glm::rotate(yaw, glm::vec3(0.f, 1.f, 0.f));
	pie_Draw3DShape(s_jib->displayModel(), 0, colour, brightness, pie_SHADOW, 0, jibM, viewMatrix);
}

// MOD (16.11. 19:28): an der Tankstelle stehen so viele Oelfaesser (WZ-Fass mioil.pie), wie sie gerade auf Vorrat hat -
// zwei Reihen zu je fuenf vor der Vorderseite. Reine Anzeige.
static void modDrawStationBarrels(const STRUCTURE *psStructure, int colour, PIELIGHT brightness, const glm::mat4 &viewMatrix)
{
	if (psStructure->isBlueprint() || !modfuel::isStation(psStructure)) { return; }
	static iIMDBaseShape *s_drum = nullptr;
	static unsigned s_tried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
	if (modelCacheStale(s_tried)) { s_drum = modelGet("mioil.pie"); }
	if (s_drum == nullptr) { return; }
	const int n = std::clamp(modoil::stationBarrels(psStructure), 0, 10);
	if (n <= 0) { return; }
	const STRUCTURE_STATS *st = psStructure->pStructureType;
	const int b = std::max(1, static_cast<int>(st->baseBreadth));
	const float dir = UNDEG(-psStructure->rot.direction);
	const float cs = std::cos(dir), sn = std::sin(dir);
	for (int i = 0; i < n; ++i)
	{
		const float ox = -44.f + static_cast<float>(i % 5) * 22.f;
		const float oz = static_cast<float>(b * TILE_UNITS / 2 + 14 + (i / 5) * 20);
		const float wx = static_cast<float>(psStructure->pos.x) + ox * cs + oz * sn;
		const float wz = static_cast<float>(psStructure->pos.y) - (-ox * sn + oz * cs);
		if (!worldOnMap(gameWorld.map, static_cast<int>(wx), static_cast<int>(wz))) { continue; }
		const float gy = static_cast<float>(map_Height(gameWorld.map, static_cast<int>(wx), static_cast<int>(wz)));
		const float yaw = dir + static_cast<float>((psStructure->id + static_cast<unsigned>(i) * 37u) % 360u) * 0.01745f;
		const glm::mat4 m = glm::translate(glm::vec3(wx, gy, -wz)) * glm::rotate(yaw, glm::vec3(0.f, 1.f, 0.f)) * glm::scale(glm::vec3(0.6f));
		pie_Draw3DShape(s_drum->displayModel(), 0, colour, brightness, pie_SHADOW, 0, m, viewMatrix);
	}
}

void renderStructure(STRUCTURE *psStructure, const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	int colour, pieFlagData, ecmFlag = 0, pieFlag = 0;
	PIELIGHT buildingBrightness;
	const Vector3i dv = Vector3i(psStructure->pos.x, psStructure->pos.z, -(psStructure->pos.y));
	bool bHitByElectronic = false;
	bool defensive = false;
	const FACTION *faction = getPlayerFaction(psStructure->player);
	const iIMDShape *strImd = getFactionDisplayIMD(faction, psStructure->sDisplay.imd->displayModel());
	MAPTILE *psTile = worldTile(gameWorld.map, psStructure->pos.x, psStructure->pos.y);

	glm::mat4 modelMatrix = glm::translate(glm::vec3(dv)) * glm::rotate(UNDEG(-psStructure->rot.direction), glm::vec3(0.f, 1.f, 0.f));

	if (psStructure->pStructureType->type == REF_WALL || psStructure->pStructureType->type == REF_WALLCORNER
	    || psStructure->pStructureType->type == REF_GATE)
	{
		renderWallSection(psStructure, viewMatrix, perspectiveViewMatrix);
		return;
	}
	// MOD (17.11.): Gruben im Stellungsnetz - Modell mit Gaengen zu den offenen Seiten (ohne Drehung, siehe modfort.h)
	// (17.11. 16:10) bewaffnete Gruben: die Waffe sitzt am Anschluss des eigenen Modells (gedreht wie das Bauwerk)
	const iIMDShape *modFortConnImd = nullptr;
	if (const iIMDShape *fortImd = modfort::displayModel(psStructure))
	{
		modFortConnImd = strImd;
		strImd = getFactionDisplayIMD(faction, fortImd);
		modelMatrix = glm::translate(glm::vec3(dv.x, modfort::displayHeight(psStructure, dv.y), dv.z));   // tief: auf Originalboden
	}
	// If the structure is not truly visible, but we know there is something there, we will instead draw a blip
	UBYTE visibilityAmount = psStructure->visibleForLocalDisplay();
	if (visibilityAmount < UBYTE_MAX && visibilityAmount > 0)
	{
		int frame = graphicsTime / BLIP_ANIM_DURATION + psStructure->id % 8192;  // de-sync the blip effect, but don't overflow the int
		pie_Draw3DShape(getFactionDisplayIMD(faction, getDisplayImdFromIndex(MI_BLIP)), frame, 0, WZCOL_WHITE, pie_ADDITIVE, visibilityAmount / 2,
			glm::translate(glm::vec3(dv)), viewMatrix);
		return;
	}
	else if (!visibilityAmount)
	{
		return;
	}
	else if (psStructure->pStructureType->type == REF_DEFENSE)
	{
		defensive = true;
	}

	if (psTile->jammerBits & alliancebits[psStructure->player])
	{
		ecmFlag = pie_ECM;
	}

	colour = getPlayerColour(psStructure->player);

	// -------------------------------------------------------------------------------

	/* Mark it as having been drawn */
	psStructure->sDisplay.frameNumber = currentGameFrame;

	if (!defensive
	    && psStructure->timeLastHit - graphicsTime < ELEC_DAMAGE_DURATION
	    && psStructure->lastHitWeapon == WSC_ELECTRONIC)
	{
		bHitByElectronic = true;
	}

	buildingBrightness = structureBrightness(psStructure);

	// MOD (22.11.): Strassenbruecke - Platte, Pfeiler, Gelaender (Eisenbahnbruecken zeichnet modrail mit Schienen)
	if (modbridge::isBridge(psStructure) && !modrail::isRail(psStructure) && modbridge::drawBridge(psStructure, buildingBrightness, viewMatrix))
	{
		setScreenDispWithPerspective(&psStructure->sDisplay, perspectiveViewMatrix * modelMatrix);
		return;
	}
	// MOD (22.11. 21:19): fertige Strasse - die Kachel zeigt sie (modroad); das Bauwerk selbst unsichtbar, bleibt anklickbar (Abriss)
	if (modroad::isRoad(psStructure) && psStructure->status == SS_BUILT)
	{
		setScreenDispWithPerspective(&psStructure->sDisplay, perspectiveViewMatrix * modelMatrix);
		return;
	}
	// MOD (21.11.): Gleise - die Engine zeichnet die Gleisstuecke entlang der Verbindungen zu den Nachbarfeldern (modrail)
	if (modrail::isRail(psStructure) && modrail::drawRail(psStructure, buildingBrightness, viewMatrix))
	{
		setScreenDispWithPerspective(&psStructure->sDisplay, perspectiveViewMatrix * modelMatrix);   // anklickbar (Abriss)
		return;
	}

	if (!defensive)
	{
		/* Draw the building's base first */
		if (psStructure->pStructureType->pBaseIMD != nullptr && !modconcrete::replacesBasePlate(psStructure))   // MOD (18.11.): Beton mit weichem Rand statt Platte
		{
			if (psStructure->isBlueprint())
			{
				pieFlagData = BLUEPRINT_OPACITY;
			}
			else
			{
				pieFlag = pie_FORCE_FOG | ecmFlag;
				pieFlagData = 255;
			}
			pie_Draw3DShape(getFactionDisplayIMD(faction, psStructure->pStructureType->pBaseIMD->displayModel()), 0, colour, buildingBrightness, pieFlag | pie_TRANSLUCENT | pie_FORCELIGHT, pieFlagData,
				modelMatrix, viewMatrix);
		}

		// override
		if (bHitByElectronic)
		{
			buildingBrightness = pal_SetBrightness(150);
		}
	}

	if (bHitByElectronic)
	{
		modelMatrix *= objectShimmy((BASE_OBJECT *)psStructure);
	}

	//first check if partially built - ANOTHER HACK!
	if (psStructure->status == SS_BEING_BUILT)
	{
		if (psStructure->prebuiltImd != nullptr)
		{
			// strImd is a module, so render the already-built part at full height.
			const iIMDBaseShape *imd = psStructure->prebuiltImd;
			if (imd)
			{
				pie_Draw3DShape(getFactionDisplayIMD(faction, imd->displayModel()), 0, colour, buildingBrightness, pie_SHADOW, 0,
					modelMatrix, viewMatrix);
			}
		}
		if (strImd)
		{
			pie_Draw3DShape(strImd, 0, colour, buildingBrightness, pie_HEIGHT_SCALED | pie_SHADOW, static_cast<int>(structHeightScale(psStructure) * pie_RAISE_SCALE), modelMatrix, viewMatrix);
		}
		modDrawSiteCrane(psStructure, colour, buildingBrightness, viewMatrix);   // MOD (16.11. Punkt 4): Baustellen-Kran
		setScreenDispWithPerspective(&psStructure->sDisplay, perspectiveViewMatrix * modelMatrix);
		return;
	}

	if (psStructure->isBlueprint())
	{
		pieFlag = pie_TRANSLUCENT;
		pieFlagData = BLUEPRINT_OPACITY;
	}
	else
	{
		// structures can be rotated, so use a dynamic shadow for them
		pieFlag = pie_SHADOW | ecmFlag;
		pieFlagData = 0;
	}

	// check for animation model replacement - if none found, use animation in existing IMD
	if (strImd->objanimpie[psStructure->animationEvent])
	{
		strImd = strImd->objanimpie[psStructure->animationEvent]->displayModel();
	}

	// MOD (19.11.): hat die zweite Ebene eine eigene Animation (Skelett-Weg in drawShape, z. B. das schwenkende Solarpaneel), zeichnet
	// drawShape mit der ersten Ebene bereits ALLE Ebenen - die folgenden Ebenen hier nicht noch einmal zeichnen (nur Anbauten/Tuerme)
	bool allLevelsDrawn = false;
	while (strImd)
	{
		float stretch = 0.f;
		if (defensive && !psStructure->isBlueprint() && !(strImd->flags & iV_IMD_NOSTRETCH))
		{
			stretch = psStructure->pos.z - psStructure->foundationDepth;
		}
		if (!allLevelsDrawn)
		{
			drawShape(strImd, psStructure->timeAnimationStarted, colour, buildingBrightness, pieFlag, pieFlagData, modelMatrix, viewMatrix, stretch);
			allLevelsDrawn = strImd->next && strImd->next->objanimframes > 0;
		}
		if (modFortConnImd != nullptr)   // MOD (17.11. 16:10): Netz-Grube mit Waffe
		{
			if (modFortConnImd->connectors.size() > 0 && psStructure->numWeaps > 0 && !modnestcrew::unmanned(psStructure))
			{
				renderStructureTurrets(psStructure, modFortConnImd, buildingBrightness, pieFlag, pieFlagData, ecmFlag,
				                       modelMatrix * glm::rotate(UNDEG(-psStructure->rot.direction), glm::vec3(0.f, 1.f, 0.f)), viewMatrix);
			}
		}
		else if (strImd->connectors.size() > 0 && !modnestcrew::unmanned(psStructure))   // MOD: MG-Stellung ohne Schuetze
		{
			renderStructureTurrets(psStructure, strImd, buildingBrightness, pieFlag, pieFlagData, ecmFlag, modelMatrix, viewMatrix);
		}
		strImd = strImd->next.get();
	}
	modDrawStationBarrels(psStructure, colour, buildingBrightness, viewMatrix);   // MOD: Oelfaesser an der Tankstelle
	setScreenDispWithPerspective(&psStructure->sDisplay, perspectiveViewMatrix * modelMatrix);
}

/// draw the delivery points
void renderDeliveryPoint(FLAG_POSITION *psPosition, bool blueprint, const glm::mat4& viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	Vector3i	dv;
	SDWORD		x, y, r;
	int pieFlag, pieFlagData;
	PIELIGHT colour;

	//store the frame number for when deciding what has been clicked on
	psPosition->frameNumber = currentGameFrame;

	dv.x = psPosition->coords.x;
	dv.z = -(psPosition->coords.y);
	dv.y = psPosition->coords.z;

	//quick check for invalid data
	ASSERT_OR_RETURN(, psPosition && psPosition->factoryType < NUM_FLAG_TYPES, "Invalid assembly point");

	// MOD (Wunsch 22.11. 16:07 "die Fabrikauslieferungspunkte drehen sich dummerweise mit der Kamera"): die Sammelpunkte sind Bodenmarken
	// mit Nummer - sie liegen jetzt fest auf dem Boden (Ausrichtung wie die Karte), statt sich wie frueher die Fahne zur Kamera zu drehen
	const glm::mat4 modelMatrix = glm::translate(glm::vec3(dv)) * glm::scale(glm::vec3(.5f));

	pieFlag = pie_TRANSLUCENT;
	pieFlagData = BLUEPRINT_OPACITY;

	if (blueprint)
	{
		colour = deliveryReposValid() ? WZCOL_LGREEN : WZCOL_LRED;
	}
	else
	{
		pieFlag |= pie_FORCE_FOG;
		colour = WZCOL_WHITE;
		STRUCTURE *structure = findDeliveryFactory(psPosition);
		if (structure != nullptr && structure->selected)
		{
			colour = selectionBrightness();
		}
	}
	const iIMDBaseShape *psIMD = getAssemblyPointIMD(psPosition->factoryType, psPosition->factoryInc);
	if (psIMD != nullptr)
	{
		pie_Draw3DShape(psIMD->displayModel(), 0, 0, colour, pieFlag, pieFlagData, modelMatrix, viewMatrix);
	}

	// MOD (Wunsch 14.11. Punkt 8 / 17.11. 15:46): Scavengers bekommen am Sammelpunkt eine kleine rot-gelbe Flagge (scavflag.py); die
	// Nummer am Boden bleibt. Die Flagge weht leicht (kleine Pendelbewegung, nur Anzeige).
	if (!blueprint && psPosition->factoryType != REPAIR_FLAG && psPosition->player < MAX_PLAYERS
	    && getPlayerFaction(static_cast<uint8_t>(psPosition->player)) == getFactionByID(FACTION_NORMAL))
	{
		static const iIMDBaseShape *s_flag = nullptr;
		static unsigned s_tried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
		if (modelCacheStale(s_tried)) { s_flag = modelGet(WzString::fromUtf8("scavflag.pie")); }
		if (s_flag != nullptr)
		{
			const float sway = 4.f * std::sin(static_cast<float>(graphicsTime % 3000u) / 3000.f * 6.2831853f);
			const glm::mat4 fm = glm::translate(glm::vec3(dv) + glm::vec3(-22.f, 0.f, 22.f)) * glm::rotate(glm::radians(sway), glm::vec3(0, 1, 0));
			pie_Draw3DShape(s_flag->displayModel(), 0, psPosition->player, colour, pie_FORCE_FOG, 0, fm, viewMatrix);
		}
	}

	// MOD (16.11. 23:30): Reparatur-Sammelpunkt = stark leuchtendes, hellgruen blinkendes Leuchtfeuer - ein einzelner Punkt wie ein
	// Landezonen-Licht (Lichthof zur Kamera gedreht, im Takt hell/dunkel, dazu gruenes Licht auf dem Boden)
	if (!blueprint && psPosition->factoryType == REPAIR_FLAG)
	{
		const iIMDBaseShape *beacon = modelGet(WzString::fromUtf8("modrallybeacon.pie"));
		const unsigned phase = graphicsTime % 1000u;
		const bool on = phase < 520u;
		const float pulse = on ? (1.f - 0.25f * static_cast<float>(phase) / 520.f) : 0.18f;
		if (beacon != nullptr)
		{
			const glm::mat3 camRot(viewMatrix);
			const glm::vec3 p(static_cast<float>(dv.x), static_cast<float>(dv.y) + 24.f, static_cast<float>(dv.z));
			const float sc = on ? 2.6f : 1.5f;
			const glm::mat4 bb = glm::translate(glm::mat4(1.f), p) * glm::mat4(glm::transpose(camRot)) * glm::scale(glm::mat4(1.f), glm::vec3(sc));
			const int alpha = std::clamp(static_cast<int>(255.f * pulse), 0, 255);
			pie_Draw3DShape(beacon->displayModel(), 0, 0, WZCOL_WHITE, pie_ADDITIVE, alpha, bb, viewMatrix, 0.f, true);
			if (on)
			{
				// zweiter, kleinerer Kern - "stark leuchtend"
				const glm::mat4 core = glm::translate(glm::mat4(1.f), p) * glm::mat4(glm::transpose(camRot)) * glm::scale(glm::mat4(1.f), glm::vec3(sc * 0.55f));
				pie_Draw3DShape(beacon->displayModel(), 0, 0, WZCOL_WHITE, pie_ADDITIVE, alpha, core, viewMatrix, 0.f, true);
			}
		}
		if (on)
		{
			LIGHT light;
			light.position = Vector3i(psPosition->coords.x, psPosition->coords.z + 40, psPosition->coords.y);
			light.range = 420;
			light.colour = pal_Colour(150, 255, 140);
			light.intensity = 2.2f * pulse;
			getCurrentLightingData().lights.push_back(light);
		}
	}

	//get the screen coords for the DP
	calcFlagPosScreenCoords(&x, &y, &r, perspectiveViewMatrix * modelMatrix);
	psPosition->screenX = x;
	psPosition->screenY = y;
	psPosition->screenR = r;
}

// MOD (Wunsch 20.11. "Kann man das Runterziehen bei den Hedgehog-Panzersperren verhindern? Das sieht nicht gut aus."):
// Mauern/Sperren sitzen auf der hoechsten Ecke ihres Feldes, die unteren Punkte werden bis zum tiefsten Boden gestreckt - am Hang wurden
// die Balkenenden der Panzerigel dabei zu langen Spiessen. Modelle aus Einzelteilen je Ebene (PIE-Merker iV_IMD_MOD_GROUNDLEVELS) zeichnet
// diese Funktion Teil fuer Teil: jedes steht auf dem Boden unter seiner eigenen Mitte und neigt sich mit dem Hang (hoechstens 30 Grad),
// ohne Streckung. Nur Anzeige - das Spiel rechnet weiter mit pos.z. false = kein solches Modell (normal zeichnen).
static bool modDrawGroundLevels(const iIMDShape *shape, const STRUCTURE *psStructure, int colour, PIELIGHT brightness, int pieFlag, int pieFlagData, const glm::mat4 &viewMatrix)
{
	if (shape == nullptr || !(shape->flags & iV_IMD_MOD_GROUNDLEVELS))
	{
		return false;
	}
	const float rotRad = UNDEG(-psStructure->rot.direction);
	const glm::mat4 rotY = glm::rotate(rotRad, glm::vec3(0.f, 1.f, 0.f));
	auto H = [](float x, float y) { return static_cast<float>(map_Height(gameWorld.map, static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)))); };
	const float maxTilt = glm::radians(30.f);
	for (const iIMDShape *lvl = shape; lvl != nullptr; lvl = lvl->next.get())
	{
		const glm::vec3 c(lvl->modPartCenter.x, lvl->modPartCenter.y, lvl->modPartCenter.z);
		const glm::vec4 off = rotY * glm::vec4(c.x, 0.f, c.z, 0.f);
		const float wx = static_cast<float>(psStructure->pos.x) + off.x;
		const float wy = static_cast<float>(psStructure->pos.y) - off.z;   // Anzeige-z = -Welt-y
		const float r = clip(lvl->modPartRadius, 8.f, 48.f);
		const float hc = H(wx, wy);
		// Hang unter dem Teil (Mittelwert ueber die Standflaeche) -> Neigung
		const float hx = (H(wx + r, wy) - H(wx - r, wy)) / (2.f * r);
		const float hy = (H(wx, wy + r) - H(wx, wy - r)) / (2.f * r);
		glm::vec3 n = glm::normalize(glm::vec3(-hx, 1.f, hy));
		glm::mat4 tilt(1.f);
		const float ang = std::acos(clip(n.y, -1.f, 1.f));
		if (ang > 0.001f)
		{
			const glm::vec3 axis = glm::normalize(glm::cross(glm::vec3(0.f, 1.f, 0.f), n));
			tilt = glm::rotate(std::min(ang, maxTilt), axis);
		}
		// Mulden: liegt der Boden an einer Ecke der Standflaeche tiefer als die geneigte Ebene, das Teil so weit absenken (hoechstens 6),
		// damit kein Balkenende in der Luft haengt; 1 Einheit sinkt es immer ein (sitzt fest auf dem Boden)
		float sink = 1.f;
		for (int k = 0; k < 4; k++)
		{
			const float dx = (k & 1) ? r : -r, dy = (k & 2) ? r : -r;
			const float gap = (hc + hx * dx + hy * dy) - H(wx + dx, wy + dy);
			sink = std::max(sink, std::min(gap, 6.f));
		}
		const glm::mat4 m = glm::translate(glm::vec3(wx, hc - sink, -wy)) * tilt * rotY * glm::translate(glm::vec3(-c.x, -c.y, -c.z));
		pie_Draw3DShape(lvl, 0, colour, brightness, pieFlag, pieFlagData, m, viewMatrix, 0.f, true);
	}
	return true;
}

// MOD (Wunsch 21.11. 19:35 "Nur maximal so viele Verbindungswege-Stuecke wie absolut notwendig ... in der Kampagne hat ein einfacher rechter
// Winkel ein +-Stueck an der Ecke, was er nicht braucht und bloed aussieht"): Eckmauern (CORNER WALL), deren Modell das Kreuzstueck eines
// Mauersatzes ist, zeigen das Stueck, das zu ihren Nachbarn passt - gerade, Ecke (L), T oder Kreuz (wie die normalen Mauerteile).
// Allein stehend bleibt das Kreuz. Nur Anzeige (Drehung/Modell des Bauwerks bleiben unveraendert).
static bool modWallCombines(const STRUCTURE_STATS *st)
{
	return st->type == REF_WALL || st->type == REF_GATE || st->type == REF_WALLCORNER || st->combinesWithWall;
}

static bool modCornerWallShape(const STRUCTURE *s, iIMDBaseShape *&imdOut, uint16_t &dirOut)
{
	const STRUCTURE_STATS *cs = s->pStructureType;
	if (cs == nullptr || cs->type != REF_WALLCORNER || cs->pIMD.empty() || cs->pIMD[0] == nullptr)
	{
		return false;
	}
	// Zuordnung Eckmauer -> Mauersatz (Index); die Werte werden je Spiel neu geladen -> bei neuem Feld/neuer Anzahl verwerfen,
	// gefundene Eintraege vor Gebrauch pruefen (keine alten Zeiger)
	static std::unordered_map<const STRUCTURE_STATS *, UDWORD> s_wallOf;
	static const STRUCTURE_STATS *s_statsBase = nullptr;
	static UDWORD s_statsNum = 0;
	if (s_statsBase != asStructureStats || s_statsNum != numStructureStats)
	{
		s_wallOf.clear();
		s_statsBase = asStructureStats;
		s_statsNum = numStructureStats;
	}
	const auto isSet = [cs](UDWORD i) {
		if (i >= numStructureStats) { return false; }
		const STRUCTURE_STATS &st = asStructureStats[i];
		return st.type == REF_WALL && st.pIMD.size() >= 4 && st.pIMD[1] == cs->pIMD[0];
	};
	constexpr UDWORD NONE = 0xFFFFFFFFu;
	UDWORD wi = NONE;
	const auto it = s_wallOf.find(cs);
	if (it != s_wallOf.end() && (it->second == NONE || isSet(it->second)))
	{
		wi = it->second;
	}
	else
	{
		for (UDWORD i = 0; i < numStructureStats; ++i)
		{
			if (isSet(i)) { wi = i; break; }
		}
		s_wallOf[cs] = wi;
	}
	if (wi == NONE)
	{
		return false;
	}
	const STRUCTURE_STATS *wall = &asStructureStats[wi];
	const int mx = map_coord(s->pos.x), my = map_coord(s->pos.y);
	constexpr int step = DIST_TILE / TILE_UNITS;     // Nachbarmauer eine Original-Kachel weiter (Zweig viertel: 2 Feinfelder)
	auto has = [&](int dx, int dy) {
		const int x = mx + dx * step, y = my + dy * step;
		if (x < 0 || y < 0 || x >= gameWorld.map.width || y >= gameWorld.map.height) { return false; }
		const MAPTILE *t = mapTile(gameWorld.map, x, y);
		if (!TileHasStructure(t)) { return false; }
		const STRUCTURE *o = castStructure(t->psObject);
		return o != nullptr && !o->died && modWallCombines(o->pStructureType) && (o->player == s->player || aiCheckAlliancesInRange(static_cast<int>(o->player), static_cast<int>(s->player)));
	};
	const int orient = (has(-1, 0) ? 1 : 0) | (has(1, 0) ? 2 : 0) | (has(0, -1) ? 4 : 0) | (has(0, 1) ? 8 : 0);
	if (orient == 0)
	{
		return false;
	}
	// wie wallType()/wallDir() in structure.cpp: Modell 0 = gerade, 1 = Kreuz, 2 = T, 3 = Ecke
	static const int types[16] = {0, 0, 0, 0, 0, 3, 3, 2, 0, 3, 3, 2, 0, 2, 2, 1};
	const uint16_t d0 = DEG(0), d1 = DEG(90), d2 = DEG(180), d3 = DEG(270);
	const uint16_t dirs[16] = {d0, d0, d2, d0, d3, d0, d3, d0, d1, d1, d2, d2, d3, d1, d3, d0};
	iIMDBaseShape *imd = wall->pIMD[static_cast<size_t>(types[orient])];
	if (imd == nullptr)
	{
		return false;
	}
	imdOut = imd;
	dirOut = dirs[orient];
	return true;
}

static bool renderWallSection(STRUCTURE *psStructure, const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	int ecmFlag = 0;
	PIELIGHT		brightness;
	Vector3i			dv;
	int				pieFlag, pieFlagData;
	MAPTILE			*psTile = worldTile(gameWorld.map, psStructure->pos.x, psStructure->pos.y);
	const FACTION *faction = getPlayerFaction(psStructure->player);

	if (!psStructure->visibleForLocalDisplay())
	{
		return false;
	}

	if (psTile->jammerBits & alliancebits[psStructure->player])
	{
		ecmFlag = pie_ECM;
	}

	psStructure->sDisplay.frameNumber = currentGameFrame;

	brightness = structureBrightness(psStructure);
	float stretch = psStructure->pos.z - psStructure->foundationDepth;

	/* Establish where it is in the world */
	dv.x = psStructure->pos.x;
	dv.z = -(psStructure->pos.y);
	dv.y = psStructure->pos.z;

	dv.y -= gateCurrentOpenHeight(psStructure, graphicsTime, 1);  // Make gate stick out by 1 unit, so that the tops of ┼ gates can safely have heights differing by 1 unit.

	// MOD (17.11.): Graeben im Stellungsnetz - Modell mit Gaengen zu den offenen Seiten (ohne Drehung, siehe modfort.h)
	const iIMDShape *fortImd = modfort::displayModel(psStructure);
	if (fortImd != nullptr) { dv.y = modfort::displayHeight(psStructure, dv.y); }   // (17.11. 14:14) tief: Modell auf Originalboden
	iIMDBaseShape *modCornerImd = nullptr;   // MOD (21.11.): passendes Mauerstueck fuer Eckmauern
	uint16_t modDrawDir = psStructure->rot.direction;
	if (fortImd == nullptr && !modCornerWallShape(psStructure, modCornerImd, modDrawDir)) { modCornerImd = nullptr; modDrawDir = psStructure->rot.direction; }
	const glm::mat4 modelMatrix = fortImd != nullptr ? glm::translate(glm::vec3(dv))
	                              : glm::translate(glm::vec3(dv)) * glm::rotate(UNDEG(-modDrawDir), glm::vec3(0.f, 1.f, 0.f));

	/* Actually render it */
	if (psStructure->status == SS_BEING_BUILT)
	{
		if (fortImd != nullptr)
		{
			pie_Draw3DShape(getFactionDisplayIMD(faction, fortImd), 0, getPlayerColour(psStructure->player),
							brightness, pie_HEIGHT_SCALED | pie_SHADOW | ecmFlag, static_cast<int>(structHeightScale(psStructure) * pie_RAISE_SCALE), modelMatrix, viewMatrix, stretch);
		}
		else if (psStructure->sDisplay.imd)
		{
			const iIMDShape *shape = getFactionDisplayIMD(faction, (modCornerImd != nullptr ? modCornerImd : psStructure->sDisplay.imd)->displayModel());
			const int scaleData = static_cast<int>(structHeightScale(psStructure) * pie_RAISE_SCALE);
			if (!modDrawGroundLevels(shape, psStructure, getPlayerColour(psStructure->player), brightness, pie_HEIGHT_SCALED | pie_SHADOW | ecmFlag, scaleData, viewMatrix))
			{
				pie_Draw3DShape(shape, 0, getPlayerColour(psStructure->player), brightness, pie_HEIGHT_SCALED | pie_SHADOW | ecmFlag, scaleData, modelMatrix, viewMatrix, (shape->flags & iV_IMD_NOSTRETCH) ? 0.f : stretch);
			}
		}
	}
	else
	{
		if (psStructure->isBlueprint())
		{
			pieFlag = pie_TRANSLUCENT;
			pieFlagData = BLUEPRINT_OPACITY;
		}
		else
		{
			// Use a dynamic shadow
			pieFlag = pie_SHADOW;
			pieFlagData = 0;
		}
		iIMDBaseShape *imd = modCornerImd != nullptr ? modCornerImd : psStructure->sDisplay.imd;
		if (fortImd != nullptr)
		{
			pie_Draw3DShape(getFactionDisplayIMD(faction, fortImd), 0, getPlayerColour(psStructure->player), brightness, pieFlag | ecmFlag, pieFlagData, modelMatrix, viewMatrix, stretch);
		}
		else if (imd)
		{
			const iIMDShape *shape = getFactionDisplayIMD(faction, imd->displayModel());
			if (!modDrawGroundLevels(shape, psStructure, getPlayerColour(psStructure->player), brightness, pieFlag | ecmFlag, pieFlagData, viewMatrix))
			{
				// MOD (Wunsch 23.11. 18:38 Container: "das Ziehen an den Boden wegmachen"): Mauer-Modelle mit NOSTRETCH (TYPE 0x1000) werden
				// nicht mehr bis zum Boden gezogen
				pie_Draw3DShape(shape, 0, getPlayerColour(psStructure->player), brightness, pieFlag | ecmFlag, pieFlagData, modelMatrix, viewMatrix, (shape->flags & iV_IMD_NOSTRETCH) ? 0.f : stretch);
			}
		}
	}
	setScreenDispWithPerspective(&psStructure->sDisplay, perspectiveViewMatrix * modelMatrix);
	return true;
}

/// SHURCOOL: Draws the strobing 3D drag box that is used for multiple selection
static void	drawDragBox()
{
	if (dragBox3D.status != DRAG_DRAGGING || buildState != BUILD3D_NONE)
	{
		return;
	}

	int X1 = MIN(dragBox3D.x1, mouseX());
	int X2 = MAX(dragBox3D.x1, mouseX());
	int Y1 = MIN(dragBox3D.y1, mouseY());
	int Y2 = MAX(dragBox3D.y1, mouseY());

	// draw static box

	iV_Box(X1, Y1, X2, Y2, WZCOL_UNIT_SELECT_BORDER);
	pie_UniTransBoxFill(X1, Y1, X2, Y2, WZCOL_UNIT_SELECT_BOX);

	// draw pulse effect

	dragBox3D.pulse += (BOX_PULSE_SIZE - dragBox3D.pulse) * realTimeAdjustedIncrement(5);

	if(dragBox3D.pulse > BOX_PULSE_SIZE - 0.1f) {
		dragBox3D.pulse = 0;
	}

	PIELIGHT color = WZCOL_UNIT_SELECT_BOX;

	color.byte.a = static_cast<uint8_t>((float)color.byte.a * (1 - (dragBox3D.pulse / BOX_PULSE_SIZE))); // alpha relative to max pulse size

	pie_UniTransBoxFill(X2, Y1, X2 + dragBox3D.pulse, Y2 + dragBox3D.pulse, color); // east side + south-east corner
	pie_UniTransBoxFill(X1 - dragBox3D.pulse, Y2, X2, Y2 + dragBox3D.pulse, color); // south side + south-west corner
	pie_UniTransBoxFill(X1 - dragBox3D.pulse, Y1 - dragBox3D.pulse, X1, Y2, color); // west side + north-west corner
	pie_UniTransBoxFill(X1, Y1 - dragBox3D.pulse, X2 + dragBox3D.pulse, Y1, color); // north side + north-east corner
}


/// Display reload bars for structures and droids
static void queueWeaponReloadBarRects(BASE_OBJECT *psObj, WEAPON *psWeap, int weapon_slot, BatchedMultiRectRenderer& batchedMultiRectRenderer, size_t rectGroup)
{
	SDWORD			scrX, scrY, scrR, scale;
	STRUCTURE		*psStruct;
	float			mulH;	// display unit resistance instead of reload!
	DROID			*psDroid;
	int			armed, firingStage;

	if (ctrlShiftDown() && (psObj->type == OBJ_DROID))
	{
		psDroid = (DROID *)psObj;
		scrX = psObj->sDisplay.screenX;
		scrY = psObj->sDisplay.screenY;
		scrR = psObj->sDisplay.screenR;
		scrY += scrR + 2;

		if (weapon_slot != 0) // only rendering resistance in the first slot
		{
			return;
		}
		if (psDroid->resistance)
		{
			mulH = (float)PERCENT(psDroid->resistance, psDroid->droidResistance());
		}
		else
		{
			mulH = 100.f;
		}
		firingStage = static_cast<int>(mulH);
		firingStage = ((((2 * scrR) * 10000) / 100) * firingStage) / 10000;
		if (firingStage >= (UDWORD)(2 * scrR))
		{
			firingStage = (2 * scrR);
		}
		batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(scrX - scrR - 1, 3 + scrY + 0 + (weapon_slot * 2), scrX - scrR + (2 * scrR) + 1,    3 + scrY + 3 + (weapon_slot * 2), WZCOL_RELOAD_BACKGROUND));
		batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(scrX - scrR,   3 + scrY + 1 + (weapon_slot * 2), scrX - scrR + firingStage, 3 + scrY + 2 + (weapon_slot * 2), WZCOL_HEALTH_RESISTANCE));
		return;
	}

	armed = droidReloadBar(psObj, psWeap, weapon_slot);
	if (armed >= 0 && armed < 100) // no need to draw if full
	{
		scrX = psObj->sDisplay.screenX;
		scrY = psObj->sDisplay.screenY;
		scrR = psObj->sDisplay.screenR;
		switch (psObj->type)
		{
		case OBJ_DROID:
			scrY += scrR + 2;
			break;
		case OBJ_STRUCTURE:
			psStruct = (STRUCTURE *)psObj;
			scale = MAX(psStruct->pStructureType->baseWidth, psStruct->pStructureType->baseBreadth);
			scrY += scale * 10;
			scrR = scale * 20;
			break;
		default:
			break;
		}
		/* Scale it into an appropriate range */
		firingStage = ((((2 * scrR) * 10000) / 100) * armed) / 10000;
		if (firingStage >= 2 * scrR)
		{
			firingStage = (2 * scrR);
		}
		/* Power bars */
		batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(scrX - scrR - 1, 3 + scrY + 0 + (weapon_slot * 2), scrX - scrR + (2 * scrR) + 1,  3 + scrY + 3 + (weapon_slot * 2), WZCOL_RELOAD_BACKGROUND));
		batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(scrX - scrR,   3 + scrY + 1 + (weapon_slot * 2), scrX - scrR + firingStage, 3 + scrY + 2 + (weapon_slot * 2), WZCOL_RELOAD_BAR));
	}
}

/// draw target origin icon for the specified structure
static void drawStructureTargetOriginIcon(STRUCTURE *psStruct, int weapon_slot)
{
	SDWORD		scrX, scrY, scrR;
	UDWORD		scale;

	// Process main weapon only for now
	if (!tuiTargetOrigin || weapon_slot || !((psStruct->asWeaps[weapon_slot]).nStat))
	{
		return;
	}

	scale = MAX(psStruct->pStructureType->baseWidth, psStruct->pStructureType->baseBreadth);
	scrX = psStruct->sDisplay.screenX;
	scrY = psStruct->sDisplay.screenY + (scale * 10);
	scrR = scale * 20;

	/* Render target origin graphics */
	switch (psStruct->asWeaps[weapon_slot].origin)
	{
	case ORIGIN_VISUAL:
		iV_DrawImage(IntImages, IMAGE_ORIGIN_VISUAL, scrX + scrR + 5, scrY - 1);
		break;
	case ORIGIN_COMMANDER:
		iV_DrawImage(IntImages, IMAGE_ORIGIN_COMMANDER, scrX + scrR + 5, scrY - 1);
		break;
	case ORIGIN_SENSOR:
		iV_DrawImage(IntImages, IMAGE_ORIGIN_SENSOR_STANDARD, scrX + scrR + 5, scrY - 1);
		break;
	case ORIGIN_CB_SENSOR:
		iV_DrawImage(IntImages, IMAGE_ORIGIN_SENSOR_CB, scrX + scrR + 5, scrY - 1);
		break;
	case ORIGIN_AIRDEF_SENSOR:
		iV_DrawImage(IntImages, IMAGE_ORIGIN_SENSOR_AIRDEF, scrX + scrR + 5, scrY - 1);
		break;
	case ORIGIN_RADAR_DETECTOR:
		iV_DrawImage(IntImages, IMAGE_ORIGIN_RADAR_DETECTOR, scrX + scrR + 5, scrY - 1);
		break;
	case ORIGIN_UNKNOWN:
		// Do nothing
		break;
	default:
		debug(LOG_WARNING, "Unexpected target origin in structure(%d)!", psStruct->id);
	}
}

/// draw the health bar for the specified structure
static void queueStructureHealth(STRUCTURE *psStruct, BatchedMultiRectRenderer& batchedMultiRectRenderer, size_t rectGroup)
{
	int32_t		scrX, scrY, scrR;
	PIELIGHT	powerCol = WZCOL_BLACK, powerColShadow = WZCOL_BLACK;
	int32_t		health, width;

	int32_t scale = static_cast<int32_t>(MAX(psStruct->pStructureType->baseWidth, psStruct->pStructureType->baseBreadth));
	width = scale * 20;
	scrX = psStruct->sDisplay.screenX;
	scrY = static_cast<int32_t>(psStruct->sDisplay.screenY) + (scale * 10);
	scrR = width;
	//health = PERCENT(psStruct->body, psStruct->baseBodyPoints);
	if (ctrlShiftDown())
	{
		//show resistance values if CTRL/SHIFT depressed
		UDWORD  resistance = structureResistance(
		                         psStruct->pStructureType, psStruct->player);
		if (resistance)
		{
			health = static_cast<int32_t>(PERCENT(MAX(0, psStruct->resistance), resistance));
		}
		else
		{
			health = 100;
		}
	}
	else
	{
		//show body points
		health = static_cast<int32_t>((1. - getStructureDamage(psStruct) / 65536.f) * 100);

		// If structure is incomplete, make bar correspondingly thinner.
		int maxBody = psStruct->structureBody();
		int maxBodyBuilt = structureBodyBuilt(psStruct);
		width = (uint64_t)width * maxBodyBuilt / maxBody;
	}
	if (health > REPAIRLEV_HIGH)
	{
		powerCol = WZCOL_HEALTH_HIGH;
		powerColShadow = WZCOL_HEALTH_HIGH_SHADOW;
	}
	else if (health > REPAIRLEV_LOW)
	{
		powerCol = WZCOL_HEALTH_MEDIUM;
		powerColShadow = WZCOL_HEALTH_MEDIUM_SHADOW;
	}
	else
	{
		powerCol = WZCOL_HEALTH_LOW;
		powerColShadow = WZCOL_HEALTH_LOW_SHADOW;
	}
	modBarColors(psStruct->player, powerCol, powerColShadow);   // MOD: Scavenger gelb
	health = (((width * 10000) / 100) * health) / 10000;
	health *= 2;
	batchedMultiRectRenderer.addRectF(PIERECT_DrawRequest_f(scrX - scrR - 1, scrY - 1, scrX - scrR + 2 * width + 1, scrY + 3, WZCOL_RELOAD_BACKGROUND), rectGroup);
	batchedMultiRectRenderer.addRectF(PIERECT_DrawRequest_f(scrX - scrR, scrY, scrX - scrR + health, scrY + 1, powerCol), rectGroup);
	batchedMultiRectRenderer.addRectF(PIERECT_DrawRequest_f(scrX - scrR, scrY + 1, scrX - scrR + health, scrY + 2, powerColShadow), rectGroup);
	// MOD (Wunsch 18.11. 12:45 "es gibt keinen Ladebalken am Oellagerturm"): Fuellstand von Oellager (Faesser, blau) und Munitionsdepot /
	// Kiste / Kistenhaufen (Kisten, gelb) als Balken unter dem Lebensbalken, ein Abschnitt je Platz (bei vielen Plaetzen je 5)
	{
		int cap = 0, have = -1;
		PIELIGHT col = pal_RGBA(70, 150, 255, 255), shade = pal_RGBA(30, 80, 175, 255);
		have = modoil::storeBarrels(psStruct, cap);
		if (have < 0 && psStruct->player == selectedPlayer)
		{
			cap = modcrates::crateCapAt(psStruct);
			if (cap > 0)
			{
				have = modcrates::cratesAt(psStruct);
				col = pal_RGBA(255, 214, 40, 255); shade = pal_RGBA(170, 130, 10, 255);
			}
		}
		if (have >= 0 && cap > 0)
		{
			const int full = 2 * width;
			const int fill = full * std::clamp(have, 0, cap) / cap;
			const int x0 = scrX - scrR, y0 = scrY + 5;
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0 - 1, y0 - 1, x0 + full + 1, y0 + 3, WZCOL_RELOAD_BACKGROUND), rectGroup);
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0, x0 + fill, y0 + 1, col), rectGroup);
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0 + 1, x0 + fill, y0 + 2, shade), rectGroup);
			const int segs = cap > 12 ? cap / 5 : cap;
			for (int k = 1; k < segs; ++k)
			{
				const int sx = x0 + full * k / segs;
				batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(sx, y0, sx + 1, y0 + 2, WZCOL_RELOAD_BACKGROUND), rectGroup);
			}
		}
	}
	// MOD (Wunsch 24.11. 17:46 "Lager-Ladebalken beim Kraftwerk - machen"): Faesser im Kraftwerk (Reserve bis zu einigen hundert, orange,
	// 10 Abschnitte); rot, solange weniger als 4 Faesser (12 Minuten) da sind
	{
		int gcap = 0;
		const int gs = (psStruct->player == selectedPlayer) ? modoil::generatorStock(psStruct, gcap) : -1;
		if (gs >= 0 && gcap > 0)
		{
			const int full = 2 * width;
			const int fill = std::max(gs > 0 ? 1 : 0, full * std::clamp(gs, 0, gcap) / gcap);
			const int x0 = scrX - scrR, y0 = scrY + 5;
			const bool low = gs < 4;
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0 - 1, y0 - 1, x0 + full + 1, y0 + 3, WZCOL_RELOAD_BACKGROUND), rectGroup);
			if (fill > 0)
			{
				batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0, x0 + fill, y0 + 1, low ? pal_RGBA(255, 70, 50, 255) : pal_RGBA(255, 150, 40, 255)), rectGroup);
				batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0 + 1, x0 + fill, y0 + 2, low ? pal_RGBA(170, 40, 30, 255) : pal_RGBA(170, 90, 20, 255)), rectGroup);
			}
			for (int k = 1; k < 10; ++k)
			{
				const int sx = x0 + full * k / 10;
				batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(sx, y0, sx + 1, y0 + 2, WZCOL_RELOAD_BACKGROUND), rectGroup);
			}
		}
	}
	// MOD (Wunsch 20.11. 19:06 "Man soll auch optisch sehen, dass die Raffinerie arbeitet - mit Ladebalken"): Fortschritt des laufenden Fasses
	{
		const int prog = modoil::refineryProgress(psStruct);
		if (prog >= 0)
		{
			const int full = 2 * width;
			const int fill = full * prog / 1000;
			const int x0 = scrX - scrR, y0 = scrY + 5;
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0 - 1, y0 - 1, x0 + full + 1, y0 + 3, WZCOL_RELOAD_BACKGROUND), rectGroup);
			if (fill > 0)
			{
				batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0, x0 + fill, y0 + 1, pal_RGBA(255, 150, 40, 255)), rectGroup);
				batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0 + 1, x0 + fill, y0 + 2, pal_RGBA(170, 90, 20, 255)), rectGroup);
			}
		}
	}
	// MOD (18.11.): Schildgenerator - Schildbalken (hellblau, 10 Abschnitte) ueber dem Lebensbalken
	{
		const int sp = modshield::shieldPercent(psStruct);
		if (sp >= 0)
		{
			const int full = 2 * width;
			const int fill = full * sp / 100;
			const int x0 = scrX - scrR, y0 = scrY - 5;
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0 - 1, y0 - 1, x0 + full + 1, y0 + 3, WZCOL_RELOAD_BACKGROUND), rectGroup);
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0, x0 + fill, y0 + 1, pal_RGBA(140, 215, 255, 255)), rectGroup);
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0 + 1, x0 + fill, y0 + 2, pal_RGBA(60, 140, 220, 255)), rectGroup);
			for (int k = 1; k < 10; ++k)
			{
				const int sx = x0 + full * k / 10;
				batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(sx, y0, sx + 1, y0 + 2, WZCOL_RELOAD_BACKGROUND), rectGroup);
			}
		}
	}
	// MOD (16.11.): Tankstelle - Oelbalken (blau) darunter, ein Abschnitt je Fass
	if (modfuel::isStation(psStruct))
	{
		const int full = 2 * width;
		const int capM = std::max(1, modoil::stationCapMilli());
		const int milli = std::clamp(modoil::stationMilli(psStruct), 0, capM);
		const int fill = static_cast<int>(static_cast<int64_t>(full) * milli / capM);
		const int x0 = scrX - scrR, y0 = scrY + 5;
		batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0 - 1, y0 - 1, x0 + full + 1, y0 + 3, WZCOL_RELOAD_BACKGROUND), rectGroup);
		batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0, x0 + fill, y0 + 1, pal_RGBA(70, 150, 255, 255)), rectGroup);
		batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0 + 1, x0 + fill, y0 + 2, pal_RGBA(30, 80, 175, 255)), rectGroup);
		const int segs = std::max(1, capM / 1000);
		for (int k = 1; k < segs; ++k)
		{
			const int sx = x0 + full * k / segs;
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(sx, y0, sx + 1, y0 + 2, WZCOL_RELOAD_BACKGROUND), rectGroup);
		}
	}
}

/// draw the construction bar for the specified structure
static void queueStructureBuildProgress(STRUCTURE *psStruct, BatchedMultiRectRenderer& batchedMultiRectRenderer, size_t rectGroup)
{
	int32_t scale = static_cast<int32_t>(MAX(psStruct->pStructureType->baseWidth, psStruct->pStructureType->baseBreadth));
	int32_t scrX = static_cast<int32_t>(psStruct->sDisplay.screenX);
	int32_t scrY = static_cast<int32_t>(psStruct->sDisplay.screenY) + (scale * 10);
	int32_t scrR = scale * 20;
	auto progress = scale * 40 * structureCompletionProgress(*psStruct);
	batchedMultiRectRenderer.addRectF(PIERECT_DrawRequest_f(scrX - scrR - 1, scrY - 1 + 5, scrX + scrR + 1, scrY + 3 + 5, WZCOL_RELOAD_BACKGROUND), rectGroup);
	batchedMultiRectRenderer.addRectF(PIERECT_DrawRequest_f(scrX - scrR, scrY + 5, scrX - scrR + progress, scrY + 1 + 5, WZCOL_HEALTH_MEDIUM_SHADOW), rectGroup);
	batchedMultiRectRenderer.addRectF(PIERECT_DrawRequest_f(scrX - scrR, scrY + 1 + 5, scrX - scrR + progress, scrY + 2 + 5, WZCOL_HEALTH_MEDIUM), rectGroup);
}

/// Draw the health of structures and show enemy structures being targetted
static void	drawStructureSelections()
{
	STRUCTURE	*psStruct;
	BASE_OBJECT	*psClickedOn;
	bool		bMouseOverStructure = false;
	bool		bMouseOverOwnStructure = false;

	psClickedOn = mouseTarget();
	if (psClickedOn != nullptr && psClickedOn->type == OBJ_STRUCTURE)
	{
		bMouseOverStructure = true;
		if (psClickedOn->player == selectedPlayer)
		{
			bMouseOverOwnStructure = true;
		}
	}
	pie_SetFogStatus(false);
	const bool isSpectator = bMultiPlayer && NetPlay.players[selectedPlayer].isSpectator;
	if (isSpectator)
	{
		if (barMode == BAR_DROIDS_AND_STRUCTURES)
		{
			for (int player = 0; player < MAX_PLAYERS; player++)
			{
				batchedObjectStatusRenderer.addStructureSelectionsToRender(player, psClickedOn, bMouseOverOwnStructure);
			}
		}
		return;
	}
	if (selectedPlayer >= MAX_PLAYERS) { return; /* no-op */ }

	batchedObjectStatusRenderer.addStructureSelectionsToRender(selectedPlayer, psClickedOn, bMouseOverOwnStructure);

	batchedObjectStatusRenderer.addStructureTargetingGfxToRender();

	if (bMouseOverStructure && !bMouseOverOwnStructure)
	{
		if (mouseDown(getRightClickOrders() ? MOUSE_LMB : MOUSE_RMB))
		{
			psStruct = (STRUCTURE *)psClickedOn;
			batchedObjectStatusRenderer.addEnemyStructureHealthToRender(psStruct);
		}
	}

}

static UDWORD	getTargettingGfx()
{
	const unsigned index = getModularScaledRealTime(1000, 10);
	switch (index)
	{
	case	0:
	case	1:
	case	2:
		return (IMAGE_TARGET1 + index);
		break;
	default:
		if (index & 0x01)
		{
			return (IMAGE_TARGET4);
		}
		else
		{
			return (IMAGE_TARGET5);
		}
		break;
	}
}

/// Is the droid, its commander or its sensor tower selected?
bool	eitherSelected(DROID *psDroid)
{
	if (psDroid->selected)
	{
		return true;
	}

	if (psDroid->psGroup)
	{
		if (psDroid->psGroup->psCommander)
		{
			if (psDroid->psGroup->psCommander->selected)
			{
				return true;
			}
		}
	}

	BASE_OBJECT *psObj = orderStateObj(psDroid, DORDER_FIRESUPPORT);
	if (psObj && psObj->selected)
	{
		return true;
	}

	return false;
}

// MOD: Balken in Abschnitte teilen (wie der StarCraft-1-Marine): duenne schwarze Trennstriche
static constexpr int MOD_BAR_SEGMENTS = 10;   // (16.11. 19:28) Abschnitte halb so lang wie vorher (5)
static void modQueueBarSegments(BatchedMultiRectRenderer& renderer, size_t rectGroup, int xLeft, int width, int yTop, int yBottom)
{
	for (int seg = 1; seg < MOD_BAR_SEGMENTS; ++seg)
	{
		const int sx = xLeft + (width * seg) / MOD_BAR_SEGMENTS;
		renderer.addRect(PIERECT_DrawRequest(sx, yTop, sx + 1, yBottom, WZCOL_BLACK), rectGroup);
	}
}

// MOD: Lebensbalken im StarCraft-1-Stil: schmal (mind. 14 Pixel je Seite), nur 5 Pixel hoch, in ganzen Abschnitten
static int modBarHalf(UDWORD screenR)
{
	return std::max(static_cast<int>(screenR), 14);
}

/// Scavenger-Einheiten und -Bauwerke bekommen gelbe Balken (WZ-Gruen bleibt fuer alle anderen)
static void modBarColors(int player, PIELIGHT &col, PIELIGHT &colShadow)
{
	if (player >= 0 && player == scavengerPlayer())
	{
		col = WZCOL_YELLOW;
		colShadow = pal_RGBA(150, 120, 10, 255);
	}
}

static void modQueueHealthBar(BatchedMultiRectRenderer& renderer, size_t rectGroup, int cx, int cy, int half, int fillPx, PIELIGHT col, PIELIGHT colShadow)
{
	const int x0 = cx - half;
	const int y0 = cy + 2;
	const int full = 2 * half;
	// ganze Abschnitte: ein angefangener Abschnitt bleibt voll sichtbar (wie bei StarCraft)
	int segs = (std::max(0, fillPx) * MOD_BAR_SEGMENTS + full - 1) / full;
	segs = std::min(segs, MOD_BAR_SEGMENTS);
	if (fillPx <= 0) { segs = 0; }
	const int fill = (segs * full) / MOD_BAR_SEGMENTS;
	renderer.addRect(PIERECT_DrawRequest(x0 - 1, y0, cx + half + 1, y0 + 5, WZCOL_RELOAD_BACKGROUND), rectGroup);
	renderer.addRect(PIERECT_DrawRequest(x0, y0 + 1, x0 + fill, y0 + 3, col), rectGroup);
	renderer.addRect(PIERECT_DrawRequest(x0, y0 + 3, x0 + fill, y0 + 4, colShadow), rectGroup);
	modQueueBarSegments(renderer, rectGroup, x0, full, y0 + 1, y0 + 4);
}

// MOD (17.11.): Ring aus kleinen Punkten als echter Bodenkreis um die Fuesse (projiziert: Mitte + zwei Halbachsen, siehe calcScreenCoords)
static void modQueueGroundRing(BatchedMultiRectRenderer& renderer, size_t rectGroup, const SCREEN_DISP_DATA &d)
{
	const float per = 3.3f * (std::hypot(d.modAx, d.modAy) + std::hypot(d.modBx, d.modBy));
	const int N = std::max(24, std::min(96, static_cast<int>(per / 5.f)));
	const PIELIGHT core = pal_RGBA(235, 255, 215, 255);
	const PIELIGHT halo = pal_RGBA(90, 190, 90, 150);
	for (int i = 0; i < N; ++i)
	{
		const float a = (6.2831853f * static_cast<float>(i)) / static_cast<float>(N);
		const float c = std::cos(a), sn = std::sin(a);
		const int x = d.modFootX + static_cast<int>(d.modAx * c + d.modBx * sn);
		const int y = d.modFootY + static_cast<int>(d.modAy * c + d.modBy * sn);
		renderer.addRect(PIERECT_DrawRequest(x - 1, y - 1, x + 2, y + 2, halo), rectGroup);
		renderer.addRect(PIERECT_DrawRequest(x, y, x + 1, y + 1, core), rectGroup);
	}
}

// MOD: Ring aus kleinen Punkten (Ellipse auf dem Boden) um die Fuesse der gewaehlten Einheit; heller Kern + schwacher Hof
[[maybe_unused]] static void modQueueSelectionRing(BatchedMultiRectRenderer& renderer, size_t rectGroup, int cx, int cy, int screenR)
{
	const int rx = std::max(screenR + screenR / 6, 13);
	const int ry = std::max(rx * 9 / 20, 5);
	const int fy = cy + (screenR * 3) / 4;
	constexpr int N = 36;
	const PIELIGHT core = pal_RGBA(235, 255, 215, 255);
	const PIELIGHT halo = pal_RGBA(90, 190, 90, 150);
	for (int i = 0; i < N; ++i)
	{
		const float a = (6.2831853f * static_cast<float>(i)) / static_cast<float>(N);
		const int ox = static_cast<int>(std::cos(a) * static_cast<float>(rx));
		const int oy = static_cast<int>(std::sin(a) * static_cast<float>(ry));
		renderer.addRect(PIERECT_DrawRequest(cx + ox - 1, fy + oy - 1, cx + ox + 2, fy + oy + 2, halo), rectGroup);
		renderer.addRect(PIERECT_DrawRequest(cx + ox, fy + oy, cx + ox + 1, fy + oy + 1, core), rectGroup);
	}
}

// MOD: Munitionsbalken (gelb, 5 Abschnitte) unter dem Lebensbalken; ein Abschnitt verschwindet immer ganz
static void modQueueAmmoBar(BatchedMultiRectRenderer& renderer, size_t rectGroup, int cx, int cy, int half, int segs)
{
	const int x0 = cx - half;
	const int y0 = cy + 8;
	const int full = 2 * half;
	const PIELIGHT ammoCol = pal_RGBA(236, 196, 40, 255);
	const PIELIGHT ammoShadow = pal_RGBA(150, 112, 14, 255);
	renderer.addRect(PIERECT_DrawRequest(x0 - 1, y0, cx + half + 1, y0 + 5, WZCOL_RELOAD_BACKGROUND), rectGroup);
	const int fill = (std::clamp(segs, 0, MOD_BAR_SEGMENTS) * full) / MOD_BAR_SEGMENTS;
	renderer.addRect(PIERECT_DrawRequest(x0, y0 + 1, x0 + fill, y0 + 3, ammoCol), rectGroup);
	renderer.addRect(PIERECT_DrawRequest(x0, y0 + 3, x0 + fill, y0 + 4, ammoShadow), rectGroup);
	modQueueBarSegments(renderer, rectGroup, x0, full, y0 + 1, y0 + 4);
}

// MOD (16.11.): dritter Balken ganz unten - Treibstoff (blau)
static void modQueueFuelBar(BatchedMultiRectRenderer& renderer, size_t rectGroup, int cx, int cy, int half, int permille, int row)
{
	const int x0 = cx - half;
	const int y0 = cy + 2 + 6 * row;
	const int full = 2 * half;
	const bool empty = permille <= 0;
	const PIELIGHT fuelCol = empty ? pal_RGBA(255, 60, 50, 255) : pal_RGBA(70, 150, 255, 255);
	const PIELIGHT fuelShadow = empty ? pal_RGBA(150, 30, 25, 255) : pal_RGBA(30, 80, 175, 255);
	renderer.addRect(PIERECT_DrawRequest(x0 - 1, y0, cx + half + 1, y0 + 5, WZCOL_RELOAD_BACKGROUND), rectGroup);
	const int fill = empty ? full : (std::clamp(permille, 0, 1000) * full + 999) / 1000;
	renderer.addRect(PIERECT_DrawRequest(x0, y0 + 1, x0 + fill, y0 + 3, fuelCol), rectGroup);
	renderer.addRect(PIERECT_DrawRequest(x0, y0 + 3, x0 + fill, y0 + 4, fuelShadow), rectGroup);
	modQueueBarSegments(renderer, rectGroup, x0, full, y0 + 1, y0 + 4);
}

// MOD (18.11.): fahrender Schildgenerator - Schildbalken (hellblau, 10 Abschnitte) knapp ueber dem Lebensbalken, wie bei den Schild-Bauwerken
static void modQueueDroidShieldBar(const DROID *psDroid, BatchedMultiRectRenderer& batchedMultiRectRenderer, size_t rectGroup, int cx, int barY, int barHalf)
{
	const int sp = modshield::shieldPercent(psDroid);
	if (sp < 0) { return; }
	const int full = 2 * barHalf;
	const int fill = full * sp / 100;
	const int x0 = cx - barHalf, y0 = barY - 6;
	batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0 - 1, y0 - 1, x0 + full + 1, y0 + 3, WZCOL_RELOAD_BACKGROUND), rectGroup);
	batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0, x0 + fill, y0 + 1, pal_RGBA(140, 215, 255, 255)), rectGroup);
	batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0 + 1, x0 + fill, y0 + 2, pal_RGBA(60, 140, 220, 255)), rectGroup);
	for (int k = 1; k < 10; ++k)
	{
		const int sx = x0 + full * k / 10;
		batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(sx, y0, sx + 1, y0 + 2, WZCOL_RELOAD_BACKGROUND), rectGroup);
	}
}

static void queueDroidPowerBarsRects(DROID *psDroid, bool drawBox, BatchedMultiRectRenderer& batchedMultiRectRenderer, size_t rectGroup)
{
	UDWORD damage = PERCENT(psDroid->body, psDroid->originalBody);
	UDWORD shields = 0;

	if (psDroid->shieldPoints >= 0)
	{
		int maxShieldPoints = droidGetMaxShieldPoints(psDroid);

		if (maxShieldPoints > 0)
		{
			shields = PERCENT(psDroid->shieldPoints, maxShieldPoints);
			shields = static_cast<UDWORD>((float)psDroid->shieldPoints / (float)maxShieldPoints * (float)psDroid->sDisplay.screenR);
		}
	}

	PIELIGHT powerCol = WZCOL_BLACK;
	PIELIGHT powerColShadow = WZCOL_BLACK;

	if (damage > REPAIRLEV_HIGH)
	{
		powerCol = WZCOL_HEALTH_HIGH;
		powerColShadow = WZCOL_HEALTH_HIGH_SHADOW;
	}
	else if (damage > REPAIRLEV_LOW)
	{
		powerCol = WZCOL_HEALTH_MEDIUM;
		powerColShadow = WZCOL_HEALTH_MEDIUM_SHADOW;
	}
	else
	{
		powerCol = WZCOL_HEALTH_LOW;
		powerColShadow = WZCOL_HEALTH_LOW_SHADOW;
	}

	modBarColors(psDroid->player, powerCol, powerColShadow);
	const int barHalf = modBarHalf(psDroid->sDisplay.screenR);
	damage = static_cast<UDWORD>((float)psDroid->body / (float)psDroid->originalBody * (float)barHalf);
	if (damage > static_cast<UDWORD>(barHalf))
	{
		damage = static_cast<UDWORD>(barHalf);
	}

	damage *= 2;

	if (drawBox)
	{
		// MOD: Auswahl-Hervorhebung wie in Rome Total War: Leuchtring um die Fuesse der Einheit (statt der vier Eckwinkel)
		if (s_ring3DActive)
		{
			// MOD (18.11.): Ring liegt als 3D-Ring am Boden (modDrawSelectionRings3D)
		}
		else if (psDroid->sDisplay.modFootValid)
		{
			modQueueGroundRing(batchedMultiRectRenderer, rectGroup, psDroid->sDisplay);
		}
		else
		{
			modQueueSelectionRing(batchedMultiRectRenderer, rectGroup, static_cast<int>(psDroid->sDisplay.screenX), static_cast<int>(psDroid->sDisplay.screenY), static_cast<int>(psDroid->sDisplay.screenR));
		}
	}
	// MOD (17.11.): Balken knapp ueber der Oberkante des Modells (nicht hoch ueber dem gerechneten Mittelpunkt)
	const int barY = psDroid->sDisplay.modFootValid ? std::min(static_cast<int>(psDroid->sDisplay.screenY) + static_cast<int>(psDroid->sDisplay.screenR), psDroid->sDisplay.modTopY - 6)
	                                               : static_cast<int>(psDroid->sDisplay.screenY) + static_cast<int>(psDroid->sDisplay.screenR);

	/* Power bars */
	modQueueHealthBar(batchedMultiRectRenderer, rectGroup, static_cast<int>(psDroid->sDisplay.screenX), barY, barHalf, static_cast<int>(damage), powerCol, powerColShadow);
	modQueueDroidShieldBar(psDroid, batchedMultiRectRenderer, rectGroup, static_cast<int>(psDroid->sDisplay.screenX), barY, barHalf);
	{
		const int ammoSegs = modammo::segments(psDroid);
		if (ammoSegs >= 0)
		{
			modQueueAmmoBar(batchedMultiRectRenderer, rectGroup, static_cast<int>(psDroid->sDisplay.screenX), barY, barHalf, ammoSegs);
		}
		const int fuelPm = modfuel::permille(psDroid);
		if (fuelPm >= 0)
		{
			modQueueFuelBar(batchedMultiRectRenderer, rectGroup, static_cast<int>(psDroid->sDisplay.screenX), barY, barHalf, fuelPm, ammoSegs >= 0 ? 2 : 1);
		}
		// MOD (20.11.): Ladung der Fracht-Lkw / Oeltanker (orange, ein Abschnitt je Fass) unter dem Tankbalken - die Ladung startet leer
		int cargoCap = 0;
		const int cargoN = modoil::cargoBarrels(psDroid, cargoCap);
		if (cargoCap > 0)
		{
			const int row = (ammoSegs >= 0 ? 2 : 1) + (fuelPm >= 0 ? 1 : 0);
			const int x0 = static_cast<int>(psDroid->sDisplay.screenX) - barHalf, y0 = barY + 2 + 6 * row, full = 2 * barHalf;
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0 - 1, y0, x0 + full + 1, y0 + 5, WZCOL_RELOAD_BACKGROUND), rectGroup);
			const int fill = full * std::clamp(cargoN, 0, cargoCap) / cargoCap;
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0 + 1, x0 + fill, y0 + 3, pal_RGBA(255, 150, 40, 255)), rectGroup);
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0 + 3, x0 + fill, y0 + 4, pal_RGBA(170, 90, 20, 255)), rectGroup);
			for (int k = 1; k < cargoCap; ++k)
			{
				const int sx = x0 + full * k / cargoCap;
				batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(sx, y0 + 1, sx + 1, y0 + 4, WZCOL_RELOAD_BACKGROUND), rectGroup);
			}
		}
		// MOD (Wunsch 23.11. 00:16 "wie alle Transporter einen Ladebalken mit Sektionen (in weiss)"): Insassen der Schuetzenpanzer, Truppenwaggons
		// und Transporter - weisser Balken, ein Abschnitt je Platz, darunter
		int paxCap = 0, paxN = 0;
		if (modapc::isApc(psDroid)) { paxCap = modapc::capacity(psDroid); paxN = modapc::load(psDroid); }
		else if (psDroid->isTransporter()) { paxCap = TRANSPORTER_CAPACITY; paxN = TRANSPORTER_CAPACITY - calcRemainingCapacity(psDroid); }
		if (paxCap > 0)
		{
			const int row = (ammoSegs >= 0 ? 2 : 1) + (fuelPm >= 0 ? 1 : 0) + (cargoCap > 0 ? 1 : 0);
			const int x0 = static_cast<int>(psDroid->sDisplay.screenX) - barHalf, y0 = barY + 2 + 6 * row, full = 2 * barHalf;
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0 - 1, y0, x0 + full + 1, y0 + 5, WZCOL_RELOAD_BACKGROUND), rectGroup);
			const int fill = full * std::clamp(paxN, 0, paxCap) / paxCap;
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0 + 1, x0 + fill, y0 + 3, pal_RGBA(245, 245, 245, 255)), rectGroup);
			batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(x0, y0 + 3, x0 + fill, y0 + 4, pal_RGBA(165, 165, 170, 255)), rectGroup);
			for (int k = 1; k < paxCap; ++k)
			{
				const int sx = x0 + full * k / paxCap;
				batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(sx, y0 + 1, sx + 1, y0 + 4, WZCOL_RELOAD_BACKGROUND), rectGroup);
			}
		}
	}
	batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(
		psDroid->sDisplay.screenX - psDroid->sDisplay.screenR - 3,
		barY,
		psDroid->sDisplay.screenX - psDroid->sDisplay.screenR - 1,
		barY - shields, WZCOL_LBLUE),
	rectGroup);
	batchedMultiRectRenderer.addRect(PIERECT_DrawRequest(
		psDroid->sDisplay.screenX - psDroid->sDisplay.screenR - 2,
		barY,
		psDroid->sDisplay.screenX - psDroid->sDisplay.screenR,
		barY - shields, WZCOL_BLACK),
	rectGroup);
}

static void queueDroidEnemyHealthBarsRects(DROID *psDroid, BatchedMultiRectRenderer& batchedMultiRectRenderer, size_t rectGroup)
{
	PIELIGHT powerCol = WZCOL_BLACK;
	PIELIGHT powerColShadow = WZCOL_BLACK;
	PIELIGHT boxCol;
	UDWORD damage;
	float mulH;

	//show resistance values if CTRL/SHIFT depressed
	if (ctrlShiftDown())
	{
		if (psDroid->resistance)
		{
			damage = PERCENT(psDroid->resistance, psDroid->droidResistance());
		}
		else
		{
			damage = 100;
		}
	}
	else
	{
		damage = PERCENT(psDroid->body, psDroid->originalBody);
	}

	if (damage > REPAIRLEV_HIGH)
	{
		powerCol = WZCOL_HEALTH_HIGH;
		powerColShadow = WZCOL_HEALTH_HIGH_SHADOW;
	}
	else if (damage > REPAIRLEV_LOW)
	{
		powerCol = WZCOL_HEALTH_MEDIUM;
		powerColShadow = WZCOL_HEALTH_MEDIUM_SHADOW;
	}
	else
	{
		powerCol = WZCOL_HEALTH_LOW;
		powerColShadow = WZCOL_HEALTH_LOW_SHADOW;
	}

	//show resistance values if CTRL/SHIFT depressed
	if (ctrlShiftDown())
	{
		if (psDroid->resistance)
		{
			mulH = (float)psDroid->resistance / (float)psDroid->droidResistance();
		}
		else
		{
			mulH = 100.f;
		}
	}
	else
	{
		mulH = (float)psDroid->body / (float)psDroid->originalBody;
	}
	modBarColors(psDroid->player, powerCol, powerColShadow);
	const int barHalf = modBarHalf(psDroid->sDisplay.screenR);
	damage = static_cast<UDWORD>(mulH * (float)barHalf);
	if (damage > static_cast<UDWORD>(barHalf))
	{
		damage = static_cast<UDWORD>(barHalf);
	}
	damage *= 2;
	auto scrX = psDroid->sDisplay.screenX;
	auto scrY = psDroid->sDisplay.screenY;
	auto scrR = psDroid->sDisplay.screenR;

	/* Three DFX clips properly right now - not sure if software does */
	if ((scrX + scrR) > 0
		&&	(scrX - scrR) < pie_GetVideoBufferWidth()
		&&	(scrY + scrR) > 0
		&&	(scrY - scrR) < pie_GetVideoBufferHeight())
	{
		boxCol = WZCOL_WHITE;

		/* Power bars */
		{
			int eBarY = static_cast<int>(scrY) + static_cast<int>(scrR);
			if (psDroid->sDisplay.modFootValid) { eBarY = std::min(eBarY, psDroid->sDisplay.modTopY - 6); }   // MOD (17.11.): ueber dem Modell
			modQueueHealthBar(batchedMultiRectRenderer, rectGroup, static_cast<int>(scrX), eBarY, barHalf, static_cast<int>(damage), powerCol, powerColShadow);
			modQueueDroidShieldBar(psDroid, batchedMultiRectRenderer, rectGroup, static_cast<int>(scrX), eBarY, barHalf);
		}
	}
}

/// Draw the selection graphics for selected droids
static void	drawDroidSelections()
{
	BASE_OBJECT		*psClickedOn;
	bool			bMouseOverDroid = false;
	bool			bMouseOverOwnDroid = false;
	UDWORD			index;

	psClickedOn = mouseTarget();
	if (psClickedOn != nullptr && psClickedOn->type == OBJ_DROID)
	{
		bMouseOverDroid = true;
		if (psClickedOn->player == selectedPlayer && !psClickedOn->selected)
		{
			bMouseOverOwnDroid = true;
		}
	}
	const bool isSpectator = bMultiPlayer && NetPlay.players[selectedPlayer].isSpectator;

	if (isSpectator)
	{
		if (barMode == BAR_DROIDS || barMode == BAR_DROIDS_AND_STRUCTURES)
		{
			for (int player = 0; player < MAX_PLAYERS; player++)
			{
				batchedObjectStatusRenderer.addDroidSelectionsToRender(player, psClickedOn, bMouseOverDroid);
			}
		}

		return;
	}

	if (selectedPlayer >= MAX_PLAYERS) { return; /* no-op */ }
	pie_SetFogStatus(false);

	batchedObjectStatusRenderer.addDroidSelectionsToRender(selectedPlayer, psClickedOn, bMouseOverDroid);

	/* Are we over an enemy droid */
	if (bMouseOverDroid && !bMouseOverOwnDroid)
	{
		if (mouseDown(getRightClickOrders() ? MOUSE_LMB : MOUSE_RMB))
		{
			if (psClickedOn->player != selectedPlayer && psClickedOn->sDisplay.frameNumber == currentGameFrame)
			{
				DROID *psDroid = (DROID *)psClickedOn;
				batchedObjectStatusRenderer.addEnemyDroidHealthToRender(psDroid);
			}
		}
	}

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		/* Go thru' all the droids */
		for (const DROID *psDroid : gameWorld.objects.droids[i])
		{
			if (showORDERS)
			{
				drawDroidOrder(psDroid);
			}
			if (!psDroid->died && psDroid->sDisplay.frameNumber == currentGameFrame)
			{
				/* If it's selected */
				if (psDroid->flags.test(OBJECT_FLAG_TARGETED) && psDroid->visibleForLocalDisplay() == UBYTE_MAX)
				{
					index = IMAGE_BLUE1 + getModularScaledRealTime(1020, 5);
					iV_DrawImage(IntImages, index, psDroid->sDisplay.screenX, psDroid->sDisplay.screenY);
				}
			}
		}
	}

	for (const FEATURE *psFeature : gameWorld.objects.features[0])
	{
		if (!psFeature->died && psFeature->sDisplay.frameNumber == currentGameFrame)
		{
			if (psFeature->flags.test(OBJECT_FLAG_TARGETED))
			{
				iV_DrawImage(IntImages, getTargettingGfx(), psFeature->sDisplay.screenX, psFeature->sDisplay.screenY);
			}
		}
	}
}

static void drawDroidAndStructureSelections()
{
	modweather::drawOverlay(); // MOD: Vollbildfaerbung/Blitz des Wetters
	drawDroidSelections();
	drawStructureSelections();

	batchedObjectStatusRenderer.drawAllSelections();
	batchedObjectStatusRenderer.clear();

	const char *modLabel = modgroundfire::activeLabel();
	if (modLabel == nullptr) { modLabel = modbuild::activeLabel(); }
	if (modLabel == nullptr) { modLabel = modpaint::activeLabel(); }   // MOD: Schlachtplan
	if (modLabel != nullptr)
	{
		modTxtCmdLabel.setText(modLabel, font_regular);
		modTxtCmdLabel.render(mouseX() + 20, mouseY() + 24, WZCOL_TEXT_BRIGHT);
	}
}

/* ---------------------------------------------------------------------------- */
/// X/Y offset to display the group number at
#define GN_X_OFFSET	(8)
#define GN_Y_OFFSET	(3)
enum GROUPNUMBER_TYPE
{
	GN_NORMAL,
	GN_DAMAGED,
	GN_FACTORY
};
/// rendering of the object's group next to the object itself,
/// or the group that will be assigned to the object after production in the factory
static void	drawGroupNumber(BASE_OBJECT *psObject)
{
	UWORD id = UWORD_MAX;
	UBYTE groupNumber = UBYTE_MAX;
	int32_t x = 0, y = 0;
	GROUPNUMBER_TYPE groupNumberType = GN_NORMAL;

	if (auto *psDroid = castDroid(psObject))
	{
		int32_t xShift = psDroid->sDisplay.screenR + GN_X_OFFSET;
		int32_t yShift = psDroid->sDisplay.screenR;

		x = psDroid->sDisplay.screenX - xShift;
		y = psDroid->sDisplay.screenY + yShift;

		if (psDroid->repairGroup != UBYTE_MAX)
		{
			groupNumber = psDroid->repairGroup;
			groupNumberType = GN_DAMAGED;
		}
		else
		{
			groupNumber = psDroid->group;
		}
	}
	else if (auto *psStruct = castStructure(psObject))
	{
		// same as in queueStructureHealth
		int32_t scale = static_cast<int32_t>(MAX(psStruct->pStructureType->baseWidth, psStruct->pStructureType->baseBreadth));
		int32_t width = scale * 20;
		int32_t scrX = psStruct->sDisplay.screenX;
		int32_t scrY = static_cast<int32_t>(psStruct->sDisplay.screenY) + (scale * 10);
		int32_t scrR = width;

		x = scrX - scrR - GN_X_OFFSET;
		y = scrY - GN_Y_OFFSET;

		groupNumber = psStruct->productToGroup;
		groupNumberType = GN_FACTORY;
	}

	switch (groupNumber)
	{
	case 0:
		id = IMAGE_GN_0;
		break;
	case 1:
		id = IMAGE_GN_1;
		break;
	case 2:
		id = IMAGE_GN_2;
		break;
	case 3:
		id = IMAGE_GN_3;
		break;
	case 4:
		id = IMAGE_GN_4;
		break;
	case 5:
		id = IMAGE_GN_5;
		break;
	case 6:
		id = IMAGE_GN_6;
		break;
	case 7:
		id = IMAGE_GN_7;
		break;
	case 8:
		id = IMAGE_GN_8;
		break;
	case 9:
		id = IMAGE_GN_9;
		break;
	default:
		break;
	}

	if (id != UWORD_MAX)
	{
		switch (groupNumberType)
		{
		case GN_NORMAL:
			iV_DrawImage(IntImages, id, x, y);
			break;
		case GN_DAMAGED:
			iV_DrawImageTint(IntImages, id, x, y, pal_RGBA(255, 0, 0, 255) /* red */);
			break;
		case GN_FACTORY:
			iV_DrawImageTint(IntImages, id, x, y, pal_RGBA(255, 220, 115, 255) /* gold */);
			break;
		default:
			break;
		}
	}
}

/// X offset to display the group number at
#define CMND_STAR_X_OFFSET	(6)
#define CMND_GN_Y_OFFSET	(8)

static void drawDroidOrder(const DROID *psDroid)
{
	const int xShift = psDroid->sDisplay.screenR + GN_X_OFFSET;
	const int yShift = psDroid->sDisplay.screenR - CMND_GN_Y_OFFSET;
	const char *letter = getDroidOrderKey(psDroid->order.type);
	iV_SetTextColour(WZCOL_TEXT_BRIGHT);
	iV_DrawText(letter, psDroid->sDisplay.screenX - xShift - CMND_STAR_X_OFFSET,  psDroid->sDisplay.screenY + yShift, font_regular);
}

/// Draw the number of the commander the droid is assigned to
static void	drawDroidCmndNo(DROID *psDroid)
{
	SDWORD	xShift, yShift, index;
	UDWORD	id2;
	UWORD	id;
	bool	bDraw = true;

	id = UWORD_MAX;

	id2 = IMAGE_GN_STAR;
	index = SDWORD_MAX;
	if (psDroid->droidType == DROID_COMMAND)
	{
		index = cmdDroidGetIndex(psDroid);
	}
	else if (hasCommander(psDroid))
	{
		index = cmdDroidGetIndex(psDroid->psGroup->psCommander);
	}
	switch (index)
	{
	case 1:
		id = IMAGE_GN_1;
		break;
	case 2:
		id = IMAGE_GN_2;
		break;
	case 3:
		id = IMAGE_GN_3;
		break;
	case 4:
		id = IMAGE_GN_4;
		break;
	case 5:
		id = IMAGE_GN_5;
		break;
	case 6:
		id = IMAGE_GN_6;
		break;
	case 7:
		id = IMAGE_GN_7;
		break;
	case 8:
		id = IMAGE_GN_8;
		break;
	case 9:
		id = IMAGE_GN_9;
		break;
	default:
		bDraw = false;
		break;
	}

	if (bDraw)
	{
		xShift = psDroid->sDisplay.screenR + GN_X_OFFSET;
		yShift = psDroid->sDisplay.screenR - CMND_GN_Y_OFFSET;
		iV_DrawImage(IntImages, id2, psDroid->sDisplay.screenX - xShift - CMND_STAR_X_OFFSET, psDroid->sDisplay.screenY + yShift);
		iV_DrawImage(IntImages, id, psDroid->sDisplay.screenX - xShift, psDroid->sDisplay.screenY + yShift);
	}
}
/* ---------------------------------------------------------------------------- */


/**	Get the onscreen coordinates of a droid so we can draw a bounding box
 * This need to be severely speeded up and the accuracy increased to allow variable size bouding boxes
 * @todo Remove all magic numbers and hacks
 */
void calcScreenCoords(DROID *psDroid, const glm::mat4 &perspectiveViewMatrix)
{
	/* Get it's absolute dimensions */
	const BODY_STATS *psBStats = psDroid->getBodyStats();
	Vector3i origin;
	Vector2i center(0, 0);
	int wsRadius = 22; // World space radius, 22 = magic minimum
	float radius;

	// NOTE: This only takes into account body, but seems "good enough"
	if (psBStats && psBStats->pIMD)
	{
		wsRadius = MAX(wsRadius, psBStats->pIMD->radius);
	}

	origin = Vector3i(0, wsRadius, 0); // take the center of the object

	/* get the screen coordinates */
	const float cZ = pie_RotateProjectWithPerspective(&origin, perspectiveViewMatrix, &center) * 0.1f;

	// avoid division by zero
	if (cZ > 0)
	{
		radius = wsRadius / cZ * pie_GetResScalingFactor();
	}
	else
	{
		radius = 1; // 1 just in case some other code assumes radius != 0
	}

	/* Deselect all the droids if we've released the drag box */
	if (dragBox3D.status == DRAG_RELEASED && wallDrag.status == DRAG_INACTIVE)
	{
		if (inQuad(&center, &dragQuad) && psDroid->player == selectedPlayer)
		{
			//don't allow Transporter Droids to be selected here
			//unless we're in multiPlayer mode!!!!
			if (!psDroid->isTransporter() || bMultiPlayer)
			{
				dealWithDroidSelect(psDroid, true);
			}
		}
	}

	/* Store away the screen coordinates so we can select the droids without doing a transform */
	psDroid->sDisplay.screenX = center.x;
	psDroid->sDisplay.screenY = center.y;
	// MOD (Wunsch 17.11. "Auswahlmarker bei Seitenansicht hoch"): der Auswahlring liegt als Ellipse auf dem Boden um die Fuesse
	// (projizierter Kreis), die Balken sitzen knapp ueber der Oberkante des Modells - nicht mehr ueber dem hochgerechneten Mittelpunkt.
	{
		psDroid->sDisplay.modFootValid = false;
		const int R = std::max(48, (wsRadius * 4) / 5);
		int topH = 40;
		if (psBStats && psBStats->pIMD && psBStats->pIMD->displayModel())
		{
			topH = std::max(24, psBStats->pIMD->displayModel()->max.y + 22);
		}
		Vector3i pts[6] = {Vector3i(0, 2, 0), Vector3i(R, 2, 0), Vector3i(0, 2, R), Vector3i(-R, 2, 0), Vector3i(0, 2, -R), Vector3i(0, topH, 0)};
		Vector2i sp[6];
		bool ok = true;
		for (int k = 0; k < 6; ++k)
		{
			sp[k] = Vector2i(0, 0);
			if (pie_RotateProjectWithPerspective(&pts[k], perspectiveViewMatrix, &sp[k]) <= 0) { ok = false; }
		}
		if (ok)
		{
			psDroid->sDisplay.modFootX = (sp[1].x + sp[2].x + sp[3].x + sp[4].x) / 4;
			psDroid->sDisplay.modFootY = (sp[1].y + sp[2].y + sp[3].y + sp[4].y) / 4;
			psDroid->sDisplay.modAx = static_cast<float>(sp[1].x - sp[3].x) * 0.5f;
			psDroid->sDisplay.modAy = static_cast<float>(sp[1].y - sp[3].y) * 0.5f;
			psDroid->sDisplay.modBx = static_cast<float>(sp[2].x - sp[4].x) * 0.5f;
			psDroid->sDisplay.modBy = static_cast<float>(sp[2].y - sp[4].y) * 0.5f;
			psDroid->sDisplay.modTopY = sp[5].y;
			// unsinnig grosse Ellipsen (Kamera fast in der Einheit) nicht zeichnen
			const float big = std::max(std::abs(psDroid->sDisplay.modAx) + std::abs(psDroid->sDisplay.modBx), std::abs(psDroid->sDisplay.modAy) + std::abs(psDroid->sDisplay.modBy));
			psDroid->sDisplay.modFootValid = big < static_cast<float>(pie_GetVideoBufferWidth());
		}
	}
	// MOD: einheitliche Balkenlaenge/Auswahlgroesse, unabhaengig von Modellgroesse und Zoom (leicht anklickbar, Balken immer kurz)
	psDroid->sDisplay.screenR = static_cast<UDWORD>(std::min(16.0f, std::max(9.0f, static_cast<float>(radius))));
	// MOD (Wunsch 17.11.): grosse Einheiten (Anhaenger, Feuerwehr, schwere Panzer) mit ihrer echten Bildgroesse anklickbar
	psDroid->sDisplay.modPickR = static_cast<int>(std::min(180.0f, std::max(9.0f, static_cast<float>(radius))));
}

// MOD (Wunsch 17.11. "select hitbox bei groesseren Einheiten sehr klein"): Trefferflaeche = Kasten um den Mittelpunkt mit der echten
// Bildgroesse, erweitert bis zur Modell-Oberkante und ueber den Bodenring (auch in der Seitenansicht gut zu treffen)
bool modDroidHit(const DROID *psDroid, int mx, int my)
{
	const SCREEN_DISP_DATA &s = psDroid->sDisplay;
	const int r = std::max(static_cast<int>(s.screenR), s.modPickR);
	const int cx = static_cast<int>(s.screenX), cy = static_cast<int>(s.screenY);
	int x0 = cx - r, x1 = cx + r, y0 = cy - r, y1 = cy + r;
	if (s.modFootValid)
	{
		const int ex = static_cast<int>((std::abs(s.modAx) + std::abs(s.modBx)) * 0.7f);
		const int ey = static_cast<int>((std::abs(s.modAy) + std::abs(s.modBy)) * 0.7f);
		x0 = std::min(x0, s.modFootX - ex);
		x1 = std::max(x1, s.modFootX + ex);
		y0 = std::min({y0, s.modTopY, s.modFootY - ey});
		y1 = std::max(y1, s.modFootY + ey);
	}
	return mx >= x0 && mx <= x1 && my >= y0 && my <= y1;
}

// MOD (Wunsch 20.11. 16:52 "Select Hitbox fuer SAEMTLICHE Einheiten deutlich praeziser"): die Einheit unter der Maus. Getroffen wird nur
// das echte Modell (Dreiecke im Bild, 2 Bildpunkte Rand); liegen mehrere uebereinander, gewinnt die vorderste sichtbare Oberflaeche.
// Reihenfolge: echte Treffer (vorderste) > Randtreffer (naechster Abstand) > Radar-Echos ohne Modell (alte Kastenpruefung).
DROID *modDroidUnderMouse(int mx, int my)
{
	DROID *psExact = nullptr, *psNear = nullptr, *psBox = nullptr;
	float exactDepth = 1e30f, nearDist = 1e30f, nearDepth = 1e30f;
	int boxD2 = INT_MAX;
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		for (DROID *psDroid : gameWorld.objects.droids[i])
		{
			if (!psDroid->visibleForLocalDisplay() || !DrawnInLastFrame(psDroid->sDisplay.frameNumber))
			{
				continue;
			}
			// schneller Vorfilter: weit weg vom Bildmittelpunkt (mehr als zweieinhalb Rumpf-Bildgroessen - lange Rohre, Anhaenger) kann das Modell nicht liegen
			if (psDroid->sDisplay.modPickR > 0 && psDroid->sDisplay.modPickR < 180)
			{
				const int lim = (psDroid->sDisplay.modPickR * 5) / 2 + 24;
				const int dx = static_cast<int>(psDroid->sDisplay.screenX) - mx, dy = static_cast<int>(psDroid->sDisplay.screenY) - my;
				if (dx * dx + dy * dy > lim * lim)
				{
					continue;
				}
			}
			float depth = 0.f, dist = 0.f;
			switch (modpick::test(psDroid, mx, my, depth, dist))
			{
			case modpick::PICK_EXACT:
				if (depth < exactDepth) { exactDepth = depth; psExact = psDroid; }
				break;
			case modpick::PICK_NEAR:
				if (dist < nearDist - 0.01f || (dist < nearDist + 0.01f && depth < nearDepth)) { nearDist = dist; nearDepth = depth; psNear = psDroid; }
				break;
			case modpick::PICK_NO_DATA:
				if (modDroidHit(psDroid, mx, my))
				{
					const int dx = static_cast<int>(psDroid->sDisplay.screenX) - mx, dy = static_cast<int>(psDroid->sDisplay.screenY) - my;
					if (dx * dx + dy * dy < boxD2) { boxD2 = dx * dx + dy * dy; psBox = psDroid; }
				}
				break;
			case modpick::PICK_MISS:
				break;
			}
		}
	}
	return psExact ? psExact : (psNear ? psNear : psBox);
}

void screenCoordToWorld(Vector2i screenCoord, Vector2i &worldCoord, SDWORD &tileX, SDWORD &tileY)
{
	int nearestZ = INT_MAX;
	Vector2i outMousePos(0, 0);
	// MOD: erst nur sauber projizierte Kacheln; trifft keine, wie frueher alle (sonst landete die Maus bei 0,0)
	for (int pass = 0; pass < 2 && nearestZ == INT_MAX; ++pass)
	{
	// Intentionally not the same range as in drawTiles()
	for (int i = -visibleTiles.y / 2, idx = 0; i < visibleTiles.y / 2; i++, ++idx)
	{
		for (int j = -visibleTiles.x / 2, jdx = 0; j < visibleTiles.x / 2; j++, ++jdx)
		{
			// MOD (Wunsch 19.11. 20:53 "die Auswahl ist manchmal nicht richtig bei Hoehenunterschieden und schlechtem Kamerawinkel"):
			// Kacheln mit einer Ecke hinter/zu nah an der Kamera ergeben riesige Bildschirm-Vierecke (Ecke "weit weg"), die die Maus
			// enthielten und als "naechste" gewaehlt wurden -> ueberspringen; Tiefe = Mittel der vier Ecken statt nur einer Ecke
			// (an Haengen und Kuppen war eine einzelne Ecke oft naeher als die wirklich sichtbare Kachel)
			const Vector3i &c00 = tileScreenInfo[idx][jdx], &c01 = tileScreenInfo[idx][jdx + 1], &c11 = tileScreenInfo[idx + 1][jdx + 1], &c10 = tileScreenInfo[idx + 1][jdx];
			if (pass == 0 && (c00.z <= 0 || c01.z <= 0 || c11.z <= 0 || c10.z <= 0
			    || c00.x >= LONG_WAY / 2 || c01.x >= LONG_WAY / 2 || c11.x >= LONG_WAY / 2 || c10.x >= LONG_WAY / 2))
			{
				continue;
			}
			const int tileZ = pass == 0 ? (c00.z + c01.z + c11.z + c10.z) / 4 : c00.z;

			if (tileZ <= nearestZ)
			{
				QUAD quad;

				quad.coords[0].x = tileScreenInfo[idx + 0][jdx + 0].x;
				quad.coords[0].y = tileScreenInfo[idx + 0][jdx + 0].y;

				quad.coords[1].x = tileScreenInfo[idx + 0][jdx + 1].x;
				quad.coords[1].y = tileScreenInfo[idx + 0][jdx + 1].y;

				quad.coords[2].x = tileScreenInfo[idx + 1][jdx + 1].x;
				quad.coords[2].y = tileScreenInfo[idx + 1][jdx + 1].y;

				quad.coords[3].x = tileScreenInfo[idx + 1][jdx + 0].x;
				quad.coords[3].y = tileScreenInfo[idx + 1][jdx + 0].y;

				/* We've got a match for our mouse coords */
				if (inQuad(&screenCoord, &quad))
				{
					outMousePos.x = playerPos.p.x + (world_coord(j) - (playerPos.p.x % TILE_WIDTH));
					outMousePos.y = playerPos.p.z + (world_coord(i) - (playerPos.p.z % TILE_HEIGHT));
					outMousePos += positionInQuad(screenCoord, quad);
					if (outMousePos.x < 0)
					{
						outMousePos.x = 0;
					}
					else if (outMousePos.x > world_coord(gameWorld.map.width - 1))
					{
						outMousePos.x = world_coord(gameWorld.map.width - 1);
					}
					if (outMousePos.y < 0)
					{
						outMousePos.y = 0;
					}
					else if (outMousePos.y > world_coord(gameWorld.map.height - 1))
					{
						outMousePos.y = world_coord(gameWorld.map.height - 1);
					}
					tileX = map_coord(outMousePos.x);
					tileY = map_coord(outMousePos.y);
					/* Store away z value */
					nearestZ = tileZ;
				}
			}
		}
	}
	}
	worldCoord = outMousePos;
}

/**
 * Find the tile the mouse is currently over
 * \todo This is slow - speed it up
 */
static void locateMouse()
{
	const Vector2i pt(mouseX(), mouseY());
	screenCoordToWorld(pt, mousePos, mouseTileX, mouseTileY);
}

/// Render the sky and surroundings
static void renderSurroundings(const glm::mat4& projectionMatrix, const glm::mat4 &skyboxViewMatrix)
{
	WZ_PROFILE_SCOPE(renderSurroundings);
	// Render skybox relative to ground (i.e. undo player y translation)
	// then move it somewhat below ground level for the blending effect
	// rotate it

	if (!gamePaused())
	{
		wind = std::remainder(wind + graphicsTimeAdjustedIncrement(windSpeed), 360.0f);
	}

	// MOD (16.11. 18:20): im Untergrund gibt es keinen Himmel - dort bleibt es oben dunkel (Hoehlendecke)
	if (modlayermap::viewLayer() == modlayer::UNDERGROUND)
	{
		return;
	}
	// MOD (16.11.): Orbit - Himmel mit Dunst und tiefer Planetenboden rundum (keine schwarzen Raender, weicher Uebergang)
	if (modlayermap::viewLayer() == modlayer::ORBIT)
	{
		const glm::vec4 amb = pie_GetSceneLighting0(LIGHT_AMBIENT);
		const glm::vec4 dif = pie_GetSceneLighting0(LIGHT_DIFFUSE);
		const glm::vec3 lightC = glm::clamp(glm::vec3(amb) * 0.55f + glm::vec3(dif) * 0.55f, glm::vec3(0.3f), glm::vec3(1.2f));
		const glm::vec3 hazeTint(0.62f, 0.66f, 0.72f);   // wie MODV-Dunst im Gelaende-Shader
		glm::vec3 haze = glm::min(hazeTint * lightC, glm::vec3(1.f));
		if (gfx_api::context::get().linearLightingEnabled())
		{
			haze = glm::pow(haze, glm::vec3(1.f / 2.2f));
		}
		const glm::vec4 hazeColour(haze, 1.f);
		pie_DrawSkyboxHaze(skybox_scale, projectionMatrix, skyboxViewMatrix * glm::translate(glm::vec3(0.f, -500, 0.f)) * glm::rotate(RADIANS(wind), glm::vec3(0.f, 1.f, 0.f)), hazeColour);

		const int W = gameWorld.map.width, H = gameWorld.map.height;
		if (W > 0 && H > 0)
		{
			const char *variant = (currentMapTileset == MAP_TILESET::URBAN) ? "urban" : ((currentMapTileset == MAP_TILESET::ROCKIES) ? "rockies" : nullptr);
			std::string tex = "texpages/modisland-128/tile-256.png";
			if (variant != nullptr)
			{
				const std::string alt = astringf("texpages/modisland-128/tile-256-%s.png", variant);
				if (PHYSFS_exists(alt.c_str())) { tex = alt; }
			}
			const float cx = static_cast<float>(world_coord(W)) * 0.5f;
			const float cz = -static_cast<float>(world_coord(H)) * 0.5f;
			const float halfDiag = std::hypot(cx, cz);
			const float islandY = static_cast<float>(map_Height(gameWorld.map, world_coord(W) / 2, world_coord(H) / 2));
			constexpr float MODV_DEPTH = 4600.f;   // wie im Gelaende-Shader
			const glm::mat4 worldView = skyboxViewMatrix * glm::translate(glm::vec3(-playerPos.p.x, 0, playerPos.p.z));
			// (Wunsch 18.11. 01:14) "die verwaschene Planetenoberflaeche weit ueber die Kartengrenze hinaus - sonst sieht man die Kante und es geht
			// ploetzlich runter": der Boden beginnt schon innerhalb der Karte (darunter, das Gelaende zeichnet darueber) statt erst ausserhalb
			// der Kartenecken (dazwischen klaffte eine Luecke) und reicht bis weit an die Sichtgrenze
			const float innerR = std::max(static_cast<float>(DIST_TILE), std::min(cx, -cz) * 0.85f);
			// (18.11.) Bild: die verwaschene echte Oberflaeche (wie unter den Leere-Feldern) als 2 x 2 gespiegelter Block - wiederholt ergibt das
			// eine endlose Spiegelung ohne Naht, die am Kartenrand nahtlos an das Gelaende anschliesst
			static iV_Image s_floorImg;
			static int s_floorW = 0, s_floorH = 0, s_floorGen = 0;
			static UDWORD s_floorAt = 0;
			bool haveFloor = false;
			if (W <= 512 && H <= 512)
			{
				uint8_t probe[3];
				if (modlayermap::orbitFloorColour(0, 0, probe) && (s_floorW != W || s_floorH != H || realTime - s_floorAt > 60000))
				{
					const int IW = 2 * W, IH = 2 * H;
					std::vector<float> buf(static_cast<size_t>(IW) * IH * 3);
					for (int y = 0; y < H; ++y)
					{
						for (int x = 0; x < W; ++x)
						{
							uint8_t c[3];
							modlayermap::orbitFloorColour(x, y, c);
							const int xs[2] = {x, IW - 1 - x}, ys[2] = {y, IH - 1 - y};
							for (int a = 0; a < 2; ++a) { for (int b = 0; b < 2; ++b) { for (int k = 0; k < 3; ++k) { buf[(static_cast<size_t>(xs[a]) + static_cast<size_t>(ys[b]) * IW) * 3 + k] = c[k]; } } }
						}
					}
					// weichzeichnen (Kastenfilter, je 2 Durchgaenge waagrecht/senkrecht, wrap = nahtlos)
					std::vector<float> tmp(buf.size());
					for (int pass = 0; pass < 2; ++pass)
					{
						for (int y = 0; y < IH; ++y) { for (int x = 0; x < IW; ++x) { for (int k = 0; k < 3; ++k) {
							float s = 0.f; for (int d = -2; d <= 2; ++d) { s += buf[(static_cast<size_t>((x + d + IW) % IW) + static_cast<size_t>(y) * IW) * 3 + k]; }
							tmp[(static_cast<size_t>(x) + static_cast<size_t>(y) * IW) * 3 + k] = s / 5.f; } } }
						for (int y = 0; y < IH; ++y) { for (int x = 0; x < IW; ++x) { for (int k = 0; k < 3; ++k) {
							float s = 0.f; for (int d = -2; d <= 2; ++d) { s += tmp[(static_cast<size_t>(x) + static_cast<size_t>((y + d + IH) % IH) * IW) * 3 + k]; }
							buf[(static_cast<size_t>(x) + static_cast<size_t>(y) * IW) * 3 + k] = s / 5.f; } } }
					}
					if (s_floorImg.allocate(static_cast<unsigned>(IW), static_cast<unsigned>(IH), 4, false))
					{
						unsigned char *px = s_floorImg.bmp_w();
						for (size_t i = 0; i < static_cast<size_t>(IW) * IH; ++i)
						{
							for (int k = 0; k < 3; ++k)
							{
								const float v = buf[i * 3 + k] / 255.f * 0.78f + hazeTint[k] * 0.22f;   // Dunst wie im Gelaende-Shader
								px[i * 4 + k] = static_cast<unsigned char>(std::clamp(v * 255.f + 0.5f, 0.f, 255.f));
							}
							px[i * 4 + 3] = 255;
						}
						s_floorW = W; s_floorH = H; s_floorAt = realTime; ++s_floorGen;
					}
				}
				haveFloor = s_floorW == W && s_floorH == H && s_floorImg.width() > 0;
			}
			if (haveFloor)
			{
				const std::string key = astringf("orbitfloor:%d", s_floorGen);
				pie_DrawOrbitGround(key.c_str(), hazeTint, 0.f, cx, cz, innerR, halfDiag + 90000.f, islandY - MODV_DEPTH,
				                    static_cast<float>(DIST_TILE) * 4.f, projectionMatrix, worldView, glm::vec4(lightC, 1.f), hazeColour,
				                    &s_floorImg, 2.f * static_cast<float>(world_coord(W)), 2.f * static_cast<float>(world_coord(H)));
			}
			else
			{
				pie_DrawOrbitGround(tex.c_str(), hazeTint, 0.22f, cx, cz, innerR, halfDiag + 90000.f, islandY - MODV_DEPTH,
				                    static_cast<float>(DIST_TILE) * 4.f, projectionMatrix, worldView, glm::vec4(lightC, 1.f), hazeColour);
			}
		}
		return;
	}

	// TODO: skybox needs to be just below lowest point on map (because we have a bottom cap now). Hardcoding for now.
	pie_DrawSkybox(skybox_scale, projectionMatrix, skyboxViewMatrix * glm::translate(glm::vec3(0.f, -500, 0.f)) * glm::rotate(RADIANS(wind), glm::vec3(0.f, 1.f, 0.f)));
}

/// MOD (16.11.): In der Orbit-Ansicht wird der Hintergrund (Himmel + Planetenboden) VOR dem Gelaende gezeichnet (Maler-Reihenfolge),
/// damit der weite Boden ausserhalb der Karte nie Inseln oder Leere ueberdeckt.
bool display3d_surroundingsFirst()
{
	return modlayermap::viewLayer() == modlayer::ORBIT;
}

/// Camera height for a given averaged terrain height
/// (raised by half of HEIGHT_TRACK_INCREMENTS to keep the established framing)
static float calculateCameraHeight(float averageTerrainHeight)
{
	// MOD: Der Drehpunkt der Kamera sinkt beim Hineinzoomen auf Bodenhoehe, so sieht man kleine Einheiten
	// nahe am Boden, ohne dass die Kamera ins Gelaende rutscht. Ab ~2000 Distanz ist alles wie im Original.
	float k = std::min(1.0f, std::max(0.0f, distance / 2000.0f));
	k = k * k * (3.0f - 2.0f * k);   // glatte Kurve
	float lift = static_cast<float>(HEIGHT_TRACK_INCREMENTS / 2) + static_cast<float>(CAMERA_PIVOT_HEIGHT) * (0.04f + 0.96f * k);
	// MOD (Wunsch 20.11. 02:13 "die Kamera soll wirklich bis an den Boden sinken duerfen"): ganz hineingezoomt sinkt auch der Drehpunkt
	// (vorher blieb er ~45 Einheiten ueber dem Boden) - unter 600 Distanz gleitet er weich bis 3 Einheiten ueber den Boden
	float n = std::min(1.0f, std::max(0.0f, distance / 600.0f));
	n = n * n * (3.0f - 2.0f * n);
	lift = 3.0f + (lift - 3.0f) * n;
	return averageTerrainHeight + lift;
}

int calculateCameraHeightAt(WorldMapState& mapState, int worldX, int worldZ)
{
	return static_cast<int>(std::lround(calculateCameraHeight(calcInterpolatedAverageTerrainHeight(mapState, worldX, worldZ))));
}

/// MOD (17.11.): Die Kamera taucht nicht mehr in Haeuser/Hochhaeuser ein (dort wurden sie abgeschnitten und verschwanden). Liegt das
/// Kamera-Auge fuer die gewuenschte Hoehe innerhalb eines Gebaeudes oder Features, wird die Hoehe so weit angehoben, dass das Auge knapp
/// ueber dessen Dach bleibt (weich ueber trackHeight). Nur Anzeige.
static glm::vec3 modCameraEye(float pivotY)
{
	const glm::mat4 view =
		glm::translate(glm::vec3(0.f, 0.f, distance)) *
		glm::scale(glm::vec3(pie_GetResScalingFactor() / 100.f)) *
		glm::rotate(UNDEG(playerPos.r.z), glm::vec3(0.f, 0.f, 1.f)) *
		glm::rotate(UNDEG(playerPos.r.x), glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(playerPos.r.y), glm::vec3(0.f, 1.f, 0.f)) *
		glm::translate(glm::vec3(-static_cast<float>(playerPos.p.x), -pivotY, static_cast<float>(playerPos.p.z)));
	return (glm::inverse(view) * glm::vec4(0.f, 0.f, 0.f, 1.f)).xyz();
}

/// MOD (Wunsch 20.11. 01:46 "es ist immer noch moeglich durch den Boden zu sinken"): hoechster Gelaendepunkt unter dem Kamera-Auge und
/// rundherum (Radius radius) - der Drehpunkt folgte nur dem Boden UNTER DEM BILDMITTELPUNKT; stand das Auge ueber einem Hang/Huegel
/// hinter dem Drehpunkt, lag es im Gelaende
static constexpr float MOD_EYE_GROUND_CLEAR = 4.f;   // MOD (20.11.): kleinster Abstand Kamera-Auge - Gelaende

static float modTerrainUnderEye(const glm::vec3 &eye, int radius = 24)
{
	float top = -1e9f;
	const int ex = static_cast<int>(eye.x), ey = static_cast<int>(-eye.z);
	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			const int x = ex + dx * radius, y = ey + dy * radius;
			if (!worldOnMap(gameWorld.map, x, y)) { continue; }
			top = std::max(top, static_cast<float>(map_Height(gameWorld.map, x, y)));
		}
	}
	return top;
}

static float modCameraClearance(float pivotY)
{
	const glm::vec3 eye = modCameraEye(pivotY);
	const int ex = static_cast<int>(eye.x), ey = static_cast<int>(-eye.z);
	if (!worldOnMap(gameWorld.map, ex, ey)) { return 0.f; }
	constexpr float MARGIN = 40.f;
	float need = 0.f;
	// (20.11. 02:13 "mit Kollision mit der Bodenoberflaeche auf 0") Gelaende unter dem Auge: das Auge bleibt knapp darueber (die
	// Nahebene folgt mit, siehe updateCameraForFrame)
	const float ground = modTerrainUnderEye(eye);
	if (ground > -1e8f) { need = std::max(need, ground + MOD_EYE_GROUND_CLEAR - eye.y); }
	for (BASE_OBJECT *o : gridStartIterate(ex, ey, DIST_TILE * 4))
	{
		const iIMDShape *imd = nullptr;
		if (o->type == OBJ_FEATURE)
		{
			const FEATURE *f = static_cast<const FEATURE *>(o);
			if (f->psStats != nullptr && f->psStats->psImd != nullptr) { imd = f->psStats->psImd->displayModel(); }
		}
		else if (o->type == OBJ_STRUCTURE)
		{
			const STRUCTURE *st = static_cast<const STRUCTURE *>(o);
			if (st->sDisplay.imd != nullptr) { imd = st->sDisplay.imd->displayModel(); }
		}
		if (imd == nullptr || o->died) { continue; }
		const float top = static_cast<float>(o->pos.z) + static_cast<float>(imd->max.y);
		if (top + MARGIN <= eye.y) { continue; }
		// Auge in den Modellraum des Objekts drehen und gegen den Grundriss pruefen
		const float dx = eye.x - static_cast<float>(o->pos.x);
		const float dz = eye.z + static_cast<float>(o->pos.y);
		const float a = UNDEG(o->rot.direction);
		// Modellmatrix dreht um -Richtung (renderStructure/displayFeature) -> zurueck um +Richtung
		const float lx = dx * std::cos(a) + dz * std::sin(a);
		const float lz = -dx * std::sin(a) + dz * std::cos(a);
		if (lx < static_cast<float>(imd->min.x) - MARGIN || lx > static_cast<float>(imd->max.x) + MARGIN) { continue; }
		if (lz < static_cast<float>(imd->min.z) - MARGIN || lz > static_cast<float>(imd->max.z) + MARGIN) { continue; }
		need = std::max(need, top + MARGIN - eye.y);
	}
	return need;
}

/// The last step of moving the camera each frame, after scrolling, panning and rotating.
/// Follows the terrain height, or lets the war camera move the camera. Runs before the scene is drawn.
static void updateCameraForFrame()
{
	while (playerPos.r.y > DEG(360))
	{
		playerPos.r.y -= DEG(360);
	}

	/* If we don't have an active camera track, then track terrain height! */
	if (!getWarCamStatus())
	{
		/* Move the autonomous camera if necessary */
		updatePlayerAverageCentreTerrainHeight();
		float desiredCameraHeight = calculateCameraHeight(averageCentreTerrainHeight);
		desiredCameraHeight += modCameraClearance(desiredCameraHeight);   // MOD: nicht in Gebaeude eintauchen
		camtrace::markHeightTarget(static_cast<int>(std::lround(averageCentreTerrainHeight)), static_cast<int>(std::lround(desiredCameraHeight)));
		trackHeight(desiredCameraHeight);
	}
	else
	{
		processWarCam();
		// MOD (20.11.): auch die Verfolgerkamera taucht nicht ins Gelaende (sie kommt jetzt nah hinter das Fahrzeug)
		const glm::vec3 eye = modCameraEye(static_cast<float>(playerPos.p.y));
		const float ground = modTerrainUnderEye(eye);
		if (ground > -1e8f && eye.y < ground + MOD_EYE_GROUND_CLEAR)
		{
			playerPos.p.y += static_cast<int>(std::ceil(ground + MOD_EYE_GROUND_CLEAR - eye.y));
		}
	}
	// MOD (Wunsch 20.11. 01:46 "der untere schwarze Rand ... der Boden soll nicht mehr wegclippen"): die Nahebene lag fest bei 330
	// Einheiten - bei tiefer, flacher Kamera lag der Boden vor den Fuessen naeher und wurde abgeschnitten (schwarzer Streifen unten).
	// Jetzt: etwa halb so weit wie das Auge ueber dem Gelaende steht (3..330, Bildwinkel bleibt gleich, siehe piematrix)
	{
		const glm::vec3 eye = modCameraEye(static_cast<float>(playerPos.p.y));
		const float ground = modTerrainUnderEye(eye, TILE_UNITS * 3 / 4);   // Umgebung: auch ein Hang direkt vor dem Auge zaehlt
		const float above = ground > -1e8f ? std::max(0.f, eye.y - ground) : 1000.f;
		const float s = pie_GetResScalingFactor() / 100.f;
		pie_setPerspectiveZClose(std::max(3.f, std::min(330.f, above * 0.5f * s)));
	}
}

/// Spring constants of the height follower, y'' = -stiffness * (y - target) - damping * y'.
/// Critically damped (damping = 2 sqrt(stiffness)),
/// and separate from the war camera's ACCEL_CONSTANT / VELOCITY_CONSTANT.
static constexpr double HEIGHT_FOLLOW_STIFFNESS = 16.0;
static constexpr double HEIGHT_FOLLOW_DAMPING = 8.0;

/// Smoothly adjust player height to match the desired height
static void trackHeight(float desiredHeight)
{
	// Follower state is kept in floating point and only rounded when committed to the camera position.
	// The solver is exact for any dt and runs once per frame on the frame clock.
	static float heightSpeed = 0.0f;
	static float trackedHeight = 0.0f;   // follower position, which playerPos.p.y is the rounding of
	static int lastCommittedHeight = 0;
	static bool haveTrackedHeight = false;
	static UDWORD lastTrackFrame = UDWORD_MAX;

	double dt = frameTimeDeltaSeconds();
	const UDWORD frame = frameGetFrameNumber();
	if (frame == lastTrackFrame)
	{
		dt = 0.0;
	}
	lastTrackFrame = frame;

	if (trackHeightStartAtTarget)
	{
		// The first update of a game starts at the target
		trackedHeight = desiredHeight;
		heightSpeed = 0.0f;
		haveTrackedHeight = true;
		trackHeightStartAtTarget = false;
	}
	else if (!haveTrackedHeight || playerPos.p.y != lastCommittedHeight)
	{
		// Something else set the camera height (ex. war camera, radar jump, loading, setViewPos),
		// so carry on from there at rest
		trackedHeight = static_cast<float>(playerPos.p.y);
		heightSpeed = 0.0f;
		haveTrackedHeight = true;
	}

	// MOD (Wunsch 18.11. 12:52 "beim Ebenenwechsel springt die Kamera immer von oben nach unten. Sie soll ab jetzt immer starr sein,
	// wenn man sie nicht selbst bewegt. Und ohne JEGLICHE Traegheit oder Verzoegerung"): keine Feder mehr. Die Hoehe folgt dem Gelaende
	// nur, wenn der Spieler die Kamera bewegt (Position, Drehung, Neigung, Zoom) oder die Ebene wechselt (unter dem Schleier) - und
	// dann sofort. Steht die Kamera, bleibt sie, wo sie ist (auch wenn sich darunter etwas aendert).
	{
		static int lx = INT32_MIN, lz = 0, lry = 0, lrx = 0, lview = -1;
		static float ldist = -1.f;
		const int view = static_cast<int>(modlayermap::viewLayer());
		const float dist = getViewDistance();
		const bool moved = playerPos.p.x != lx || playerPos.p.z != lz || playerPos.r.y != lry || playerPos.r.x != lrx
		                   || view != lview || std::fabs(dist - ldist) > 0.5f;
		lx = playerPos.p.x; lz = playerPos.p.z; lry = playerPos.r.y; lrx = playerPos.r.x; lview = view; ldist = dist;
		if (moved) { trackedHeight = desiredHeight; }
		heightSpeed = 0.0f;
		playerPos.p.y = static_cast<int>(std::lround(trackedHeight));
		lastCommittedHeight = playerPos.p.y;
		return;
	}
	float separation = desiredHeight - trackedHeight;	// How far are we from desired height?
	if (std::fabs(separation) < 0.5f && heightSpeed > -5.f && heightSpeed < 5.f)
	{
		// Close enough that the committed height is the target, and slow enough to stop
		trackedHeight = desiredHeight;
		heightSpeed = 0.0f;
		camtrace::markHeightUpdate(dt * 1000.0, 0.f, 0.f, 0.f, true);
	}
	else if (dt > 0.0)
	{
		const float separationBefore = separation;
		// d²/dt² player.p.y = -stiffness * (player.p.y - desiredHeight) - damping * d/dt player.p.y
		double stiffness = HEIGHT_FOLLOW_STIFFNESS, damping = HEIGHT_FOLLOW_DAMPING;
		camtrace::overrideHeightSpring(stiffness, damping); // debug-only: --cameraheightspring
		solveDifferential2ndOrder(&separation, &heightSpeed, stiffness, damping, dt);
		camtrace::markHeightUpdate(dt * 1000.0, separationBefore, separation, heightSpeed);
		trackedHeight = desiredHeight - separation;
	}

	/* Adjust the height accordingly */
	playerPos.p.y = static_cast<int>(std::lround(trackedHeight));
	lastCommittedHeight = playerPos.p.y;
}

/// Select the next energy bar display mode
ENERGY_BAR toggleEnergyBars()
{
	if (++barMode == BAR_LAST)
	{
		barMode = BAR_SELECTED;
	}
	return (ENERGY_BAR)barMode;
}

void display3dVisitUiObjectRefs(const UiObjectRefVisitor &visit)
{
	if (visitUiObjectRef(psSensorObj, visit))
	{
		bSensorTargetting = false;
	}
}

/// Set everything up for when the player assigns the sensor target
void assignSensorTarget(BASE_OBJECT *psObj)
{
	bSensorTargetting = true;
	lastTargetAssignation = realTime;
	psSensorObj = psObj;
}

/// Set everything up for when the player selects the destination
void	assignDestTarget()
{
	bDestTargetting = true;
	lastDestAssignation = realTime;
	destTargetX = mouseX();
	destTargetY = mouseY();
	destTileX = mouseTileX;
	destTileY = mouseTileY;
}

/// Draw a graphical effect after selecting a sensor target
static void processSensorTarget()
{
	if (bSensorTargetting)
	{
		if ((realTime - lastTargetAssignation) < TARGET_TO_SENSOR_TIME)
		{
			if (psSensorObj != nullptr && !psSensorObj->died && psSensorObj->sDisplay.frameNumber == currentGameFrame)
			{
				const int x = /*mouseX();*/(SWORD)psSensorObj->sDisplay.screenX;
				const int y = (SWORD)psSensorObj->sDisplay.screenY;
				int index = IMAGE_BLUE1;
				if (!gamePaused())
				{
					index = IMAGE_BLUE1 + getModularScaledGraphicsTime(1020, 5);
				}
				iV_DrawImage(IntImages, index, x, y);
				const int offset = 12 + ((TARGET_TO_SENSOR_TIME) - (realTime - lastTargetAssignation)) / 2;
				const int x0 = x - offset;
				const int y0 = y - offset;
				const int x1 = x + offset;
				const int y1 = y + offset;
				const std::vector<glm::ivec4> lines = { glm::ivec4(x0, y0, x0 + 8, y0), glm::ivec4(x0, y0, x0, y0 + 8),
				                                        glm::ivec4(x1, y0, x1 - 8, y0), glm::ivec4(x1, y0, x1, y0 + 8),
				                                        glm::ivec4(x1, y1, x1 - 8, y1), glm::ivec4(x1, y1, x1, y1 - 8),
				                                        glm::ivec4(x0, y1, x0 + 8, y1), glm::ivec4(x0, y1, x0, y1 - 8),
				                                        glm::ivec4(x0, y0, x0 + 8, y0), glm::ivec4(x0, y0, x0, y0 + 8) };
				iV_Lines(lines, WZCOL_WHITE);
			}
			else
			{
				bSensorTargetting = false;
			}
		}
		else
		{
			bSensorTargetting = false;
		}
	}

}

/// Draw a graphical effect after selecting a destination
static void processDestinationTarget()
{
	if (bDestTargetting)
	{
		if ((realTime - lastDestAssignation) < DEST_TARGET_TIME)
		{
			const int x = destTargetX;
			const int y = destTargetY;
			const int offset = ((DEST_TARGET_TIME) - (realTime - lastDestAssignation)) / 2;
			const int x0 = x - offset;
			const int y0 = y - offset;
			const int x1 = x + offset;
			const int y1 = y + offset;

			pie_BoxFill(x0, y0, x0 + 2, y0 + 2, WZCOL_WHITE);
			pie_BoxFill(x1 - 2, y0 - 2, x1, y0, WZCOL_WHITE);
			pie_BoxFill(x1 - 2, y1 - 2, x1, y1, WZCOL_WHITE);
			pie_BoxFill(x0, y1, x0 + 2, y1 + 2, WZCOL_WHITE);
		}
		else
		{
			bDestTargetting = false;
		}
	}
}

/// Set what tile is being used to draw the bottom of a body of water
void	setUnderwaterTile(UDWORD num)
{
	underwaterTile = num;
}

/// Set what tile is being used to show rubble
void	setRubbleTile(UDWORD num)
{
	rubbleTile = num;
}
/// Get the tile that is currently being used to draw underwater ground
UDWORD	getWaterTileNum()
{
	return (underwaterTile);
}
/// Get the tile that is being used to show rubble
UDWORD	getRubbleTileNum()
{
	return (rubbleTile);
}

/// Draw the spinning particles for power stations and re-arm pads for the specified player
static void structureEffectsPlayer(UDWORD player)
{
	SDWORD	radius;
	SDWORD	xDif, yDif;
	Vector3i	pos;
	UDWORD	gameDiv;

	const int effectsPerSecond = 12;  // Effects per second. Will add effects up to once time per frame, so won't add as many effects if the framerate is low, but will be consistent, otherwise.
	unsigned effectTime = graphicsTime / (GAME_TICKS_PER_SEC / effectsPerSecond) * (GAME_TICKS_PER_SEC / effectsPerSecond);
	if (effectTime <= graphicsTime - deltaGraphicsTime)
	{
		return;  // Don't add effects this frame.
	}

	for (const STRUCTURE *psStructure : gameWorld.objects.structures[player])
	{
		if (psStructure->status != SS_BUILT)
		{
			continue;
		}
		if (psStructure->pStructureType->type == REF_POWER_GEN && psStructure->visibleForLocalDisplay())
		{
			POWER_GEN *psPowerGen = &psStructure->pFunctionality->powerGenerator;
			unsigned numConnected = 0;
			for (int i = 0; i < NUM_POWER_MODULES; i++)
			{
				if (psPowerGen->apResExtractors[i])
				{
					numConnected++;
				}
			}
			/* No effect if nothing connected */
			if (!numConnected)
			{
				//keep looking for another!
				continue;
			}
			else switch (numConnected)
				{
				case 1:
				case 2:
					gameDiv = 1440;
					break;
				case 3:
				case 4:
				default:
					gameDiv = 1080;   // really fast!!!
					break;
				}

			/* New addition - it shows how many are connected... */
			for (int i = 0 ; i < numConnected; i++)
			{
				radius = 32 - (i * 2);	// around the spire
				xDif = iSinSR(effectTime, gameDiv, radius);
				yDif = iCosSR(effectTime, gameDiv, radius);

				pos.x = psStructure->pos.x + xDif;
				pos.z = psStructure->pos.y + yDif;
				pos.y = map_Height(gameWorld.map, pos.x, pos.z) + 64 + (i * 20);	// 64 up to get to base of spire
				effectGiveAuxVar(50);	// half normal plasma size...
				addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER, false, nullptr, 0);

				pos.x = psStructure->pos.x - xDif;
				pos.z = psStructure->pos.y - yDif;
				effectGiveAuxVar(50);	// half normal plasma size...

				addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER, false, nullptr, 0);
			}
		}
		/* Might be a re-arm pad! */
		else if (psStructure->pStructureType->type == REF_REARM_PAD
		         && psStructure->visibleForLocalDisplay())
		{
			REARM_PAD *psReArmPad = &psStructure->pFunctionality->rearmPad;
			BASE_OBJECT *psChosenObj = psReArmPad->psObj;
			if (psChosenObj != nullptr && (((DROID *)psChosenObj)->visibleForLocalDisplay()))
			{
				unsigned bFXSize = 0;
				DROID *psDroid = (DROID *) psChosenObj;
				if (!psDroid->died && psDroid->action == DACTION_WAITDURINGREARM)
				{
					bFXSize = 30;
				}
				/* Then it's repairing...? */
				const iIMDShape *pDisplayModel = psStructure->sDisplay.imd->displayModel();
				radius = pDisplayModel->radius;
				xDif = iSinSR(effectTime, 720, radius);
				yDif = iCosSR(effectTime, 720, radius);
				pos.x = psStructure->pos.x + xDif;
				pos.z = psStructure->pos.y + yDif;
				pos.y = map_Height(gameWorld.map, pos.x, pos.z) + pDisplayModel->max.y;
				effectGiveAuxVar(30 + bFXSize);	// half normal plasma size...
				addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER, false, nullptr, 0);
				pos.x = psStructure->pos.x - xDif;
				pos.z = psStructure->pos.y - yDif;	// buildings are level!
				effectGiveAuxVar(30 + bFXSize);	// half normal plasma size...
				addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER, false, nullptr, 0);
			}
		}
	}
}

/// Draw the effects for all players and buildings
static void structureEffects()
{
	for (unsigned i = 0; i < MAX_PLAYERS; i++)
	{
		if (!gameWorld.objects.structures[i].empty())
		{
			structureEffectsPlayer(i);
		}
	}
}

/** Turn on drawing a range ring at a position to visualize the radius.
 * \note Pass a non-positive radius to turn this off
 */
void showRangeAtPos(SDWORD centerX, SDWORD centerY, SDWORD radius)
{
	range_rings::setDebugRange(static_cast<float>(centerX), static_cast<float>(centerY), static_cast<float>(radius));
}

UDWORD  getDroidRankGraphicFromLevel(unsigned int level)
{
	UDWORD gfxId = UDWORD_MAX;
	switch (level)
		{
		case 0:
			break;
		case 1:
			gfxId = IMAGE_LEV_0;
			break;
		case 2:
			gfxId = IMAGE_LEV_1;
			break;
		case 3:
			gfxId = IMAGE_LEV_2;
			break;
		case 4:
			gfxId = IMAGE_LEV_3;
			break;
		case 5:
			gfxId = IMAGE_LEV_4;
			break;
		case 6:
			gfxId = IMAGE_LEV_5;
			break;
		case 7:
			gfxId = IMAGE_LEV_6;
			break;
		case 8:
			gfxId = IMAGE_LEV_7;
			break;
		default:
			ASSERT(!"out of range droid rank", "Weird droid level in drawDroidRank");
			break;
		}

	return gfxId;
}
/// Get the graphic ID for a droid rank
UDWORD  getDroidRankGraphic(const DROID *psDroid)
{
	/* Establish the numerical value of the droid's rank */
	return getDroidRankGraphicFromLevel(getDroidLevel(psDroid));
}

/**	Will render a graphic depiction of the droid's present rank.
 * \note Assumes matrix context set and that z-buffer write is force enabled (Always).
 */
static void	drawDroidRank(DROID *psDroid)
{
	UDWORD	gfxId = getDroidRankGraphic(psDroid);

	/* Did we get one? - We should have... */
	if (gfxId != UDWORD_MAX)
	{
		/* Render the rank graphic at the correct location */ // remove hardcoded numbers?!
		iV_DrawImage(IntImages, (UWORD)gfxId,
		             psDroid->sDisplay.screenX + psDroid->sDisplay.screenR + 8,
		             psDroid->sDisplay.screenY + psDroid->sDisplay.screenR);
	}
}

/**	Will render a sensor graphic for a droid locked to a sensor droid/structure
 * \note Assumes matrix context set and that z-buffer write is force enabled (Always).
 */
static void	drawDroidSensorLock(DROID *psDroid)
{
	//if on fire support duty - must be locked to a Sensor Droid/Structure
	if (orderState(psDroid, DORDER_FIRESUPPORT))
	{
		/* MOD: das Sternchen der zugewiesenen Artillerie ist deutlich groesser, weiss und hat einen dunklen Rand */
		const float sz = 24.f;
		const float sx = static_cast<float>(psDroid->sDisplay.screenX) - 8.f, sy = static_cast<float>(psDroid->sDisplay.screenY) - 8.f;
		const Vector2f starSize(sz, sz);
		for (int dy = -1; dy <= 1; dy += 2)
		{
			for (int dx = -1; dx <= 1; dx += 2)
			{
				iV_DrawImageTint(IntImages, IMAGE_GN_STAR, sx + static_cast<float>(dx), sy + static_cast<float>(dy), pal_RGBA(0, 0, 0, 220), starSize);
			}
		}
		iV_DrawImageTint(IntImages, IMAGE_GN_STAR, sx, sy, pal_RGBA(255, 255, 255, 255), starSize);
	}
}

/// Draw the construction lines for all construction droids
static void doConstructionLines(const glm::mat4 &viewMatrix)
{
	WZ_PROFILE_SCOPE(doConstructionLines);
	for (unsigned i = 0; i < MAX_PLAYERS; i++)
	{
		for (DROID *psDroid : gameWorld.objects.droids[i])
		{
			if (clipXY(psDroid->pos.x, psDroid->pos.y)
			    && psDroid->visibleForLocalDisplay() == UBYTE_MAX
			    && psDroid->sMove.Status != MOVESHUFFLE)
			{
				if (psDroid->action == DACTION_BUILD)
				{
					if (psDroid->order.psObj)
					{
						if (psDroid->order.psObj->type == OBJ_STRUCTURE)
						{
							addConstructionLine(psDroid, (STRUCTURE *)psDroid->order.psObj, viewMatrix);
						}
					}
				}
				else if ((psDroid->action == DACTION_DEMOLISH) ||
				         (psDroid->action == DACTION_REPAIR) ||
				         (psDroid->action == DACTION_RESTORE))
				{
					if (psDroid->psActionTarget[0]
					    && psDroid->psActionTarget[0]->type == OBJ_STRUCTURE)
					{
						addConstructionLine(psDroid, (STRUCTURE *)psDroid->psActionTarget[0], viewMatrix);
					}
				}
			}
		}
	}
}

static uint32_t randHash(std::initializer_list<uint32_t> data) {
	uint32_t v = 0x12345678;
	auto shuffle = [&v](uint32_t d, uint32_t x){
		v += d;
		v *= x;
		v ^= v>>15;
		v *= 0x987decaf;
		v ^= v>>17;
	};
	for (int i : data) {
		shuffle(i, 0x7ea99999);
	}
	for (int i : data) {
		shuffle(i, 0xc0ffee77);
	}
	return v;
}

/// Draw the construction or demolish lines for one droid
static void addConstructionLine(DROID *psDroid, STRUCTURE *psStructure, const glm::mat4 &viewMatrix)
{
	if (!psStructure->sDisplay.imd)
	{
		return;
	}
	// MOD (16.11. 18:41): Scavenger-Infanterie baut ohne Baustrahl - sie geht direkt an die Baustelle (vorerst keine Animation)
	if (psDroid->droidType == DROID_PERSON)
	{
		return;
	}
	const iIMDShape *pDisplayModel = psStructure->sDisplay.imd->displayModel();

	auto deltaPlayer = Vector3f(0,0,0);
	auto pt0 = Vector3f(psDroid->pos.x, psDroid->pos.z + 24, -psDroid->pos.y) + deltaPlayer;

	int constructPoints = constructorPoints(*psDroid->getConstructStats(), psDroid->player);
	int amount = 800 * constructPoints * (graphicsTime - psDroid->actionStarted) / GAME_TICKS_PER_SEC;

	Vector3i each;
	auto getPoint = [&](uint32_t c) {
		uint32_t t = (amount + c)/1000;
		float s = (amount + c)%1000*.001f;
		unsigned pointIndexA = randHash({psDroid->id, psStructure->id, psDroid->actionStarted, t, c}) % pDisplayModel->points.size();
		unsigned pointIndexB = randHash({psDroid->id, psStructure->id, psDroid->actionStarted, t + 1, c}) % pDisplayModel->points.size();
		auto &pointA = pDisplayModel->points[pointIndexA];
		auto &pointB = pDisplayModel->points[pointIndexB];
		auto point = mix(pointA, pointB, s);

		each = Vector3f(psStructure->pos.x, psStructure->pos.z, psStructure->pos.y)
			+ Vector3f(point.x, structHeightScale(psStructure) * point.y, -point.z);
		return Vector3f(each.x, each.y, -each.z) + deltaPlayer;
	};

	auto pt1 = getPoint(250);
	auto pt2 = getPoint(750);

	if (psStructure->currentBuildPts < 10) {
		auto pointC = Vector3f(psStructure->pos.x, psStructure->pos.z + 10, -psStructure->pos.y) + deltaPlayer;
		auto cross = Vector3f(psStructure->pos.y - psDroid->pos.y, 0, psStructure->pos.x - psDroid->pos.x);
		auto shift = 40.f*normalize(cross);
		pt1 = mix(pointC - shift, pt1, psStructure->currentBuildPts*.1f);
		pt2 = mix(pointC + shift, pt1, psStructure->currentBuildPts*.1f);
	}

	if (rand() % 250u < deltaGraphicsTime)
	{
		effectSetSize(30);
		addEffect(&each, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED, true, getDisplayImdFromIndex(MI_PLASMA), 0);
	}

	PIELIGHT colour = psDroid->action == DACTION_DEMOLISH? WZCOL_DEMOLISH_BEAM : WZCOL_CONSTRUCTOR_BEAM;
	pie_TransColouredTriangle({pt0, pt1, pt2}, colour, viewMatrix);
	pie_TransColouredTriangle({pt0, pt2, pt1}, colour, viewMatrix);
}

bool	getDrawTerrainShadows()
{
	return (bDrawTerrainShadows);
}

void	setDrawTerrainShadows(bool val)
{
	bDrawTerrainShadows = val;
}

static void drawWorldToScreenBlit(gfx_api::abstract_texture* sourceTexture)
{
	// the rendered area may be a sub-rect of the source under dynamic resolution
	const auto renderedDims = gfx_api::context::get().getSceneRenderTargetDimensions();
	const auto textureDims = gfx_api::context::get().getRenderTargetDimensions(sourceTexture).value_or(renderedDims);
	const float texW = static_cast<float>(std::max<uint32_t>(textureDims.first, 1));
	const float texH = static_cast<float>(std::max<uint32_t>(textureDims.second, 1));

	gfx_api::constant_buffer_type<SHADER_WORLD_TO_SCREEN> cbuf;
	cbuf.uvScaleClamp = glm::vec4(
		renderedDims.first / texW, renderedDims.second / texH,
		(renderedDims.first - 0.5f) / texW, (renderedDims.second - 0.5f) / texH);

	display3d_drawFullscreenTriangle<gfx_api::WorldToScreenPSO>(cbuf, sourceTexture);
}

// RCAS sharpness in stops (0 is sharpest, 2 is the least sharp)
static float upscalingSharpnessStops = 0.25f;

void display3d_setUpscalingSharpness(float stops)
{
	upscalingSharpnessStops = glm::clamp(stops, 0.f, 2.f);
}

float display3d_getUpscalingSharpness()
{
	return upscalingSharpnessStops;
}

static void drawFsr1Easu(gfx_api::abstract_texture* sourceTexture)
{
	const auto inputDims = gfx_api::context::get().getRenderTargetDimensions(sourceTexture);
	const auto outputDims = gfx_api::context::get().getDrawableDimensions();
	ASSERT_OR_RETURN(, inputDims.has_value(), "Unknown upscale input dimensions");
	ASSERT_OR_RETURN(, outputDims.first > 0 && outputDims.second > 0, "Invalid drawable dimensions");
	// the rendered viewport may be a sub-rect of the input texture under dynamic resolution
	const auto renderedDims = gfx_api::context::get().getSceneRenderTargetDimensions();
	const float vpW = static_cast<float>(std::min(renderedDims.first, inputDims->first));
	const float vpH = static_cast<float>(std::min(renderedDims.second, inputDims->second));
	const float inW = static_cast<float>(inputDims->first);
	const float inH = static_cast<float>(inputDims->second);
	const float outW = static_cast<float>(outputDims.first);
	const float outH = static_cast<float>(outputDims.second);

	// FsrEasuCon
	gfx_api::constant_buffer_type<SHADER_FSR1_EASU> cbuf;
	cbuf.con0 = glm::vec4(vpW / outW, vpH / outH, 0.5f * vpW / outW - 0.5f, 0.5f * vpH / outH - 0.5f);
	cbuf.con1 = glm::vec4(1.f / inW, 1.f / inH, 1.f / inW, -1.f / inH);
	cbuf.con2 = glm::vec4(-1.f / inW, 2.f / inH, 1.f / inW, 2.f / inH);
	cbuf.con3 = glm::vec4(0.f, 4.f / inH, 0.f, 0.f);
	cbuf.con4 = glm::vec4((vpW - 0.5f) / inW, (vpH - 0.5f) / inH, (vpW - 1.5f) / inW, (vpH - 1.5f) / inH);

	display3d_drawFullscreenTriangle<gfx_api::Fsr1EasuPSO>(cbuf, sourceTexture);
}

static void drawFsr1Rcas(gfx_api::abstract_texture* sourceTexture)
{
	const auto inputDims = gfx_api::context::get().getRenderTargetDimensions(sourceTexture);
	ASSERT_OR_RETURN(, inputDims.has_value(), "Unknown sharpen input dimensions");

	// FsrRcasCon
	gfx_api::constant_buffer_type<SHADER_FSR1_RCAS> cbuf;
	cbuf.con0 = glm::vec4(exp2f(-upscalingSharpnessStops), 0.f, 0.f, 0.f);
	cbuf.con1 = glm::vec4(1.f / static_cast<float>(inputDims->first), 1.f / static_cast<float>(inputDims->second), 0.f, 0.f);

	display3d_drawFullscreenTriangle<gfx_api::Fsr1RcasPSO>(cbuf, sourceTexture);
}

// SMAA quality parameters, defaults matching the reference high preset
static float smaaThreshold = 0.1f;
static float smaaMaxSearchSteps = 16.f;
static float smaaMaxSearchStepsDiag = 8.f;
static float smaaCornerRounding = 0.25f;

void display3d_setSmaaParameters(float threshold, float maxSearchSteps, float maxSearchStepsDiag, float cornerRounding)
{
	smaaThreshold = glm::clamp(threshold, 0.f, 0.5f);
	smaaMaxSearchSteps = glm::clamp(maxSearchSteps, 1.f, 112.f);
	smaaMaxSearchStepsDiag = glm::clamp(maxSearchStepsDiag, 0.f, 20.f);
	smaaCornerRounding = glm::clamp(cornerRounding, 0.f, 1.f);
}

bool display3d_setSmaaMode(SMAA_MODE mode)
{
	// the reference quality presets, with diagonal and corner processing
	// expressed through the same parameters (0 diagonal steps and a corner
	// rounding of 1 disable them)
	switch (mode)
	{
	case SMAA_MODE::OFF:
		return gfx_api::context::get().setSmaaEnabled(false);
	case SMAA_MODE::LOW:
		display3d_setSmaaParameters(0.15f, 4.f, 0.f, 1.f);
		break;
	case SMAA_MODE::MEDIUM:
		display3d_setSmaaParameters(0.1f, 8.f, 0.f, 1.f);
		break;
	case SMAA_MODE::HIGH:
		display3d_setSmaaParameters(0.1f, 16.f, 8.f, 0.25f);
		break;
	case SMAA_MODE::ULTRA:
		display3d_setSmaaParameters(0.05f, 32.f, 16.f, 0.25f);
		break;
	}
	return gfx_api::context::get().setSmaaEnabled(true);
}

// rtMetrics carries the physical input size, uvScaleClamp maps viewport
// spanning texcoords onto the rendered sub-rect and bounds every tap inside it
static void smaaCommonConstants(gfx_api::abstract_texture* sourceTexture, glm::vec4& rtMetrics, glm::vec4& uvScaleClamp)
{
	const auto renderedDims = gfx_api::context::get().getSceneRenderTargetDimensions();
	const auto textureDims = gfx_api::context::get().getRenderTargetDimensions(sourceTexture).value_or(renderedDims);
	const float texW = static_cast<float>(std::max<uint32_t>(textureDims.first, 1));
	const float texH = static_cast<float>(std::max<uint32_t>(textureDims.second, 1));
	rtMetrics = glm::vec4(1.f / texW, 1.f / texH, texW, texH);
	uvScaleClamp = glm::vec4(
		renderedDims.first / texW, renderedDims.second / texH,
		(renderedDims.first - 0.5f) / texW, (renderedDims.second - 0.5f) / texH);
}

static void drawSmaaEdges(gfx_api::abstract_texture* sourceTexture)
{
	gfx_api::constant_buffer_type<SHADER_SMAA_EDGES> cbuf;
	smaaCommonConstants(sourceTexture, cbuf.rtMetrics, cbuf.uvScaleClamp);
	cbuf.params = glm::vec4(smaaThreshold, 0.f, 0.f, 0.f);

	display3d_drawFullscreenTriangle<gfx_api::SmaaEdgesPSO>(cbuf, sourceTexture);
}

static void drawSmaaWeights(gfx_api::abstract_texture* edgesTexture)
{
	gfx_api::texture* areaTexture = smaaGetAreaTexture();
	gfx_api::texture* searchTexture = smaaGetSearchTexture();
	ASSERT_OR_RETURN(, areaTexture != nullptr && searchTexture != nullptr, "Failed to create SMAA lookup textures");

	gfx_api::constant_buffer_type<SHADER_SMAA_WEIGHTS> cbuf;
	smaaCommonConstants(edgesTexture, cbuf.rtMetrics, cbuf.uvScaleClamp);
	cbuf.params = glm::vec4(smaaMaxSearchSteps, smaaMaxSearchStepsDiag, smaaCornerRounding, 0.f);

	display3d_drawFullscreenTriangle<gfx_api::SmaaWeightsPSO>(cbuf, edgesTexture, areaTexture, searchTexture);
}

static void drawSmaaBlend(gfx_api::abstract_texture* colorTexture, gfx_api::abstract_texture* weightsTexture)
{
	gfx_api::constant_buffer_type<SHADER_SMAA_BLEND> cbuf;
	smaaCommonConstants(colorTexture, cbuf.rtMetrics, cbuf.uvScaleClamp);

	display3d_drawFullscreenTriangle<gfx_api::SmaaBlendPSO>(cbuf, colorTexture, weightsTexture);
}

void display3d_renderSurroundings(const glm::mat4& projectionMatrix, const glm::mat4& skyboxViewMatrix)
{
	renderSurroundings(projectionMatrix, skyboxViewMatrix);
}

void display3d_doConstructionLines(const glm::mat4& viewMatrix)
{
	doConstructionLines(viewMatrix);
}

// MOD (Wunsch 18.11. 12:46 "Die Intersektionen [des Auswahlrings] sind immer unregelmaessig je nach Kamerawinkel und Entfernung"):
// der Ring wurde als Punkte UEBER das fertige Bild gemalt (immer vor dem Modell, auch der Teil dahinter) und war nur so gross wie
// der Rumpf. Jetzt liegt er als gestrichelter Leuchtring wirklich am Boden (Gelaendehoehe je Punkt), mit Tiefentest - das Fahrzeug
// verdeckt den hinteren Teil - und umschliesst die ganze Grundflaeche (Rumpf, Antrieb, Aufbau).
static void modDrawSelectionRings3D(const glm::mat4 &viewMatrix)
{
	s_ring3DActive = true;
	if (selectedPlayer >= MAX_PLAYERS) { return; }
	static std::vector<Vector3f> core, halo;
	core.clear(); halo.clear();
	auto groundAt = [](const DROID *d, float x, float z) -> float {
		const int ix = std::clamp(static_cast<int>(x), 0, gameWorld.map.width * TILE_UNITS - 1);
		const int iz = std::clamp(static_cast<int>(z), 0, gameWorld.map.height * TILE_UNITS - 1);
		float h = static_cast<float>(map_Height(gameWorld.map, ix, iz));
		const MAPTILE *t = worldTile(gameWorld.map, ix, iz);
		if (t != nullptr && terrainType(t) == TER_WATER) { h = std::max(h, static_cast<float>(d->pos.z)); }
		return h + 4.f;
	};
	// ein Strich = Viereck (innen/aussen) als eigener Streifen; Streifen durch doppelte Punkte getrennt (gerade Anzahl -> gleiche Drehrichtung)
	auto addDash = [&](std::vector<Vector3f> &v, const DROID *d, float r, float w, float a0, float a1) {
		const float cx = static_cast<float>(d->pos.x), cz = static_cast<float>(d->pos.y);
		const Vector3f p[4] = {
			Vector3f(cx + (r - w) * std::cos(a0), 0.f, cz + (r - w) * std::sin(a0)),
			Vector3f(cx + (r + w) * std::cos(a0), 0.f, cz + (r + w) * std::sin(a0)),
			Vector3f(cx + (r - w) * std::cos(a1), 0.f, cz + (r - w) * std::sin(a1)),
			Vector3f(cx + (r + w) * std::cos(a1), 0.f, cz + (r + w) * std::sin(a1))};
		Vector3f q[4];
		for (int k = 0; k < 4; ++k) { q[k] = Vector3f(p[k].x, groundAt(d, p[k].x, p[k].z), -p[k].z); }
		if (!v.empty()) { v.push_back(v.back()); v.push_back(q[0]); }
		for (int k = 0; k < 4; ++k) { v.push_back(q[k]); }
	};
	for (const DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d == nullptr || d->died || !d->selected || d->sDisplay.frameNumber != currentGameFrame) { continue; }
		const float R = static_cast<float>(std::max(40, modDroidFootprintRadius(d) + 8));
		const int n = std::clamp(static_cast<int>(R * 6.2831853f / 15.f), 20, 80);
		const float step = 6.2831853f / static_cast<float>(n);
		for (int i = 0; i < n; ++i)
		{
			const float a0 = step * static_cast<float>(i), a1 = a0 + step * 0.58f;
			addDash(halo, d, R, 3.6f, a0 - step * 0.06f, a1 + step * 0.06f);
			addDash(core, d, R, 1.3f, a0, a1);
		}
	}
	if (core.empty()) { return; }
	// beide Umlaufrichtungen (eine davon faellt der Rueckseiten-Unterdrueckung zum Opfer, je nach Blickrichtung)
	auto drawBoth = [&](std::vector<Vector3f> &v, PIELIGHT c) {
		pie_TransColouredTriangleStrip(v.data(), v.size(), c, viewMatrix);
		for (size_t k = 0; k + 1 < v.size(); k += 2) { std::swap(v[k], v[k + 1]); }
		pie_TransColouredTriangleStrip(v.data(), v.size(), c, viewMatrix);
	};
	drawBoth(halo, pal_RGBA(40, 120, 40, 150));
	drawBoth(core, pal_RGBA(170, 255, 150, 255));
}

void display3d_drawSelectionRings(const glm::mat4& viewMatrix)
{
	modDrawSelectionRings3D(viewMatrix);
}

void display3d_drawShieldDomes(const glm::mat4& perspectiveMatrix, const glm::mat4& viewMatrix)
{
	modshield::drawDomes(perspectiveMatrix, viewMatrix);
}

void display3d_locateMouse()
{
	locateMouse();
}

void display3d_drawWorldToScreenBlit(gfx_api::abstract_texture* sourceTexture)
{
	drawWorldToScreenBlit(sourceTexture);
}

gfx_api::buffer* display3d_getScreenTriangleVBO()
{
	return pScreenTriangleVBO;
}

static void fillUvScaleClamp(uint32_t usedW, uint32_t usedH, gfx_api::abstract_texture* sourceTex, glm::vec4& uvScaleClamp)
{
	const auto textureDims = gfx_api::context::get().getRenderTargetDimensions(sourceTex)
		.value_or(std::pair<uint32_t, uint32_t>{usedW, usedH});
	const float texW = static_cast<float>(std::max<uint32_t>(textureDims.first, 1));
	const float texH = static_cast<float>(std::max<uint32_t>(textureDims.second, 1));
	uvScaleClamp = glm::vec4(
		usedW / texW, usedH / texH,
		(usedW - 0.5f) / texW, (usedH - 0.5f) / texH);
}

static void fillSurfaceUvScaleClamp(gfx_api::PipelineSurfaceId id, glm::vec4& uvScaleClamp)
{
	auto& ctx = gfx_api::context::get();
	const auto used = ctx.usedPipelineSurfaceExtent(id);
	fillUvScaleClamp(used.first, used.second, ctx.getPipelineSurface(id), uvScaleClamp);
}

void display3d_fillPassReadUvScaleClamp(const gfx_api::RenderPassContext& passCtx, size_t readIndex, glm::vec4& uvScaleClamp)
{
	const auto& read = passCtx.resolvedRead(readIndex);
	ASSERT(read.pipelineSurfaceId.has_value(), "read %zu has no pipeline surface", readIndex);
	fillSurfaceUvScaleClamp(*read.pipelineSurfaceId, uvScaleClamp);
}

void display3d_drawFsr1Easu(gfx_api::abstract_texture* sourceTexture)
{
	drawFsr1Easu(sourceTexture);
}

void display3d_drawFsr1Rcas(gfx_api::abstract_texture* sourceTexture)
{
	drawFsr1Rcas(sourceTexture);
}

void display3d_drawSmaaEdges(gfx_api::abstract_texture* sourceTexture)
{
	drawSmaaEdges(sourceTexture);
}

void display3d_drawSmaaWeights(gfx_api::abstract_texture* edgesTexture)
{
	drawSmaaWeights(edgesTexture);
}

void display3d_drawSmaaBlend(gfx_api::abstract_texture* colorTexture, gfx_api::abstract_texture* weightsTexture)
{
	drawSmaaBlend(colorTexture, weightsTexture);
}

void display3d_processSensorTarget()
{
	processSensorTarget();
}

void display3d_processDestinationTarget()
{
	processDestinationTarget();
}

void display3d_recordSceneOverlays(const gfx_api::RenderPassContext&)
{
	if (!pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	pie_BeginInterface();

	drawDragBox();
	modfacing::draw();   // MOD (21.11. 20:22): Richtungspfeile beim Ausrichten

	if (dragBox3D.status == DRAG_RELEASED)
	{
		dragBox3D.status = DRAG_INACTIVE;
	}

	modoil::drawOverlay(pie_GetInGame3DFrameContext().perspectiveViewMatrix);   // MOD: Faesser-Anzeige
	modgrid::drawOverlay(pie_GetInGame3DFrameContext().perspectiveViewMatrix);   // MOD: Stromkabel / Stromnetz-Anzeige
	modoilui::drawWorldIcons(pie_GetInGame3DFrameContext().perspectiveViewMatrix);   // MOD (20.11.): PowerDown- und Oel-Warnsymbole
	modpaint::drawOverlay(pie_GetInGame3DFrameContext().perspectiveViewMatrix);  // MOD: Schlachtplan-Zeichnungen
	modferry::drawOverlay(pie_GetInGame3DFrameContext().perspectiveViewMatrix);  // MOD: blinkende Transport-Leuchtfeuer
	modtunnel::drawOverlay(pie_GetInGame3DFrameContext().perspectiveViewMatrix); // MOD (22.11.): eigene Tunnel / Grabungen
	modtactical::drawOverlay(pie_GetInGame3DFrameContext().perspectiveViewMatrix);   // MOD (17.11. 18:48): taktische Symbole statt Modelle
	modmines::drawOverlay(pie_GetInGame3DFrameContext().perspectiveViewMatrix);  // MOD (17.11. 18:48): Shift = gelbe Minen-Zeichen
	drawDroidAndStructureSelections();
	modbigmap::draw();   // MOD (17.11. 18:48): strategische Karte (ganzer Bildschirm oder linke Haelfte)

	pie_SetFogStatus(false);
	iV_SetTextColour(WZCOL_TEXT_BRIGHT);

	if (errorWaiting)
	{
		if (lastErrorTime == 0 || lastErrorTime + (60 * GAME_TICKS_PER_SEC) < realTime)
		{
			char trimMsg[255];
			audio_PlayBuildFailedOnce();
			ssprintf(trimMsg, "Error! (Check your logs!): %.78s", errorWaiting);
			addConsoleMessage(trimMsg, DEFAULT_JUSTIFY, NOTIFY_MESSAGE);
			errorWaiting = nullptr;
			lastErrorTime = realTime;
		}
	}
	else
	{
		errorWaiting = debugLastError();
	}
	if (showSAMPLES)
	{
		unsigned int width, height;
		std::string Qbuf, Lbuf, Abuf;

		Qbuf = astringf("Que: %04u", audio_GetSampleQueueCount());
		Lbuf = astringf("Lst: %04u", audio_GetSampleListCount());
		Abuf = astringf("Act: %04u", sound_GetActiveSamplesCount());
		txtShowSamples_Que.setText(WzString::fromUtf8(Qbuf), font_regular);
		txtShowSamples_Lst.setText(WzString::fromUtf8(Lbuf), font_regular);
		txtShowSamples_Act.setText(WzString::fromUtf8(Abuf), font_regular);

		width = txtShowSamples_Que.width() + 11;
		height = txtShowSamples_Que.height();

		txtShowSamples_Que.render(pie_GetVideoBufferWidth() - width, height + 2, WZCOL_TEXT_BRIGHT);
		txtShowSamples_Lst.render(pie_GetVideoBufferWidth() - width, height + 48, WZCOL_TEXT_BRIGHT);
		txtShowSamples_Act.render(pie_GetVideoBufferWidth() - width, height + 59, WZCOL_TEXT_BRIGHT);
	}
	// MOD (17.11. 14:39): Zeitfaktor oben in der Mitte anzeigen, solange er nicht 100 % ist (Numblock * / -)
	{
		const int tf = modTimeFactorPercent();
		if (tf != 100 && GetGameMode() == GS_NORMAL)
		{
			txtModTime.setText(WzString::fromUtf8(astringf(_("Time factor: %d %%"), tf)), font_large);
			const int tx = (static_cast<int>(pie_GetVideoBufferWidth()) - static_cast<int>(txtModTime.width())) / 2;
			txtModTime.render(tx, 34 + static_cast<int>(txtModTime.lineSize()), tf == 0 ? WZCOL_RED : WZCOL_YELLOW);
		}
	}
	if (showFPS)
	{
		std::string fps = astringf("FPS: %d", frameRate());
		txtShowFPS.setText(WzString::fromUtf8(fps), font_regular);
		const unsigned width = txtShowFPS.width() + 10;
		const unsigned height = 9;
		txtShowFPS.render(pie_GetVideoBufferWidth() - width, pie_GetVideoBufferHeight() - height, WZCOL_TEXT_BRIGHT);
	}
	if (showUNITCOUNT && selectedPlayer < MAX_PLAYERS)
	{
		std::string killdiff = astringf("Units: %u lost / %u built / %u killed", missionData.unitsLost, missionData.unitsBuilt, getSelectedPlayerUnitsKilled());
		txtUnits.setText(WzString::fromUtf8(killdiff), font_regular);
		const unsigned width = txtUnits.width() + 10;
		const unsigned height = 9;
		txtUnits.render(pie_GetVideoBufferWidth() - width - ((showFPS) ? txtShowFPS.width() + 10 : 0), pie_GetVideoBufferHeight() - height, WZCOL_TEXT_BRIGHT);
	}
	if (showORDERS)
	{
		unsigned int height;
		txtShowOrders.setText(DROIDDOING, font_regular);
		height = txtShowOrders.height();
		txtShowOrders.render(0, pie_GetVideoBufferHeight() - height, WZCOL_TEXT_BRIGHT);
	}
	if (showDROIDcounts && selectedPlayer < MAX_PLAYERS)
	{
		int visibleDroids = 0;
		int undrawnDroids = 0;
		for (const DROID *psDroid : gameWorld.objects.droids[selectedPlayer])
		{
			if (psDroid->sDisplay.frameNumber != currentGameFrame)
			{
				++undrawnDroids;
				continue;
			}
			++visibleDroids;
		}
		char droidCounts[255];
		ssprintf(droidCounts, "Droids: %d drawn, %d undrawn", visibleDroids, undrawnDroids);
		droidText.setText(droidCounts, font_regular);
		droidText.render(pie_GetVideoBufferWidth() - droidText.width() - 10, droidText.height() + 2, WZCOL_TEXT_BRIGHT);
	}

	setupConnectionStatusForm();

	if (getWidgetsStatus() && !gamePaused())
	{
		char buildInfo[255];
		bool showMs = (gameTimeGetMod() < Rational(1, 4));
		getAsciiTime(buildInfo, showMs ? graphicsTime : gameTime, showMs);
		txtLevelName.render(RET_X + 134, 410 + E_H, WZCOL_TEXT_MEDIUM);
		const DebugInputManager& dbgInputManager = gInputManager.debugManager();
		if (dbgInputManager.debugMappingsAllowed())
		{
			txtDebugStatus.render(RET_X + 134, 436 + E_H, WZCOL_TEXT_MEDIUM);
		}
		txtCurrentTime.setText(buildInfo, font_small);
		txtCurrentTime.render(RET_X + 134, 422 + E_H, WZCOL_TEXT_MEDIUM);
	}
}

void display3d_recordSceneDebugOverlays(const gfx_api::RenderPassContext&)
{
	if (!pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	if (CauseCrash)
	{
		char *crash = nullptr;
#ifdef DEBUG
		ASSERT(false, "Yes, this is a assert.  This should not happen on release builds! Use --noassert to bypass in debug builds.");
		debug(LOG_WARNING, " *** Warning!  You have compiled in debug mode! ***");
#endif
		writeGameInfo("WZdebuginfo.txt");
		debug(LOG_FATAL, "Forcing a segfault! (crash handler test)");
		if (!crashHandlingProviderTestCrash())
		{
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
			*crash = 0x3;
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic pop
#endif
		}
#if defined(__EMSCRIPTEN__)
		abort();
#endif
		exit(-1);
	}
	if (showPath)
	{
		showDroidPaths(gameWorld);
	}

	pie_DebugDrawTessellationTestPatch(); // dev-only (no-op unless WZ_DEBUG_TESS_TEST=1)

	if (showCorridors)
	{
		showCorridorCenterlines(gameWorld);
	}
}
