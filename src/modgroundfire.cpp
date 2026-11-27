// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Boden beschiessen (siehe modgroundfire.h)

#include "modicbm.h"
#include "modgroundfire.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/vector.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"

#include "baseobject.h"
#include "droid.h"
#include "console.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "ordersource.h"
#include "projectile.h"
#include "random.h"
#include "range_rings.h"
#include "stats.h"
#include "structure.h"
#include "multiplay.h"
#include "lib/netplay/sync_debug.h"
#include "statsdef.h"
#include "modlayermap.h"
#include "modui.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace modgroundfire
{
namespace
{
constexpr int DIRECT_RADIUS = 96;       // direkte Waffen: knapp ein Feld
constexpr int INDIRECT_RADIUS = 320;    // Artillerie / Moerser: gut zwei Felder
constexpr int REUSE_DIST = 48;          // Befehle auf (fast) denselben Punkt teilen sich einen Zielpunkt
constexpr unsigned CLEAN_INTERVAL = 1000;

FEATURE_STATS *s_stats = nullptr;
size_t s_cachedNum = static_cast<size_t>(-1);
const FEATURE_STATS *s_cachedData = nullptr;
unsigned s_nextClean = 0;
bool s_ringOn = false;
bool s_preview = false;          // (Wunsch 27.11. 22:05) Fadenkreuz am Mauszeiger, solange "Boden beschiessen" bereit ist
Vector2i s_previewPos(0, 0);

/// Splash-Waffe (Flaechenschaden oder Brand im Umkreis)?
bool hasSplash(const WEAPON_STATS &ws, unsigned player)
{
	const auto &u = ws.upgrade[player < MAX_PLAYERS ? player : 0];
	return u.radius > 0 && (u.radiusDamage > 0 || u.periodicalDamage > 0);
}

FEATURE_STATS *markerStats()
{
	if (s_cachedNum == asFeatureStats.size() && s_cachedData == asFeatureStats.data()) { return s_stats; }
	s_cachedNum = asFeatureStats.size();
	s_cachedData = asFeatureStats.data();
	s_stats = nullptr;
	for (FEATURE_STATS &fs : asFeatureStats)
	{
		if (fs.id.toStdString() == "ModGroundTarget") { s_stats = &fs; break; }
	}
	return s_stats;
}

bool referencedByDroid(const DROID *d, const BASE_OBJECT *marker)
{
	if (d->order.psObj == marker) { return true; }
	for (unsigned i = 0; i < MAX_WEAPONS; ++i)
	{
		if (d->psActionTarget[i] == marker) { return true; }
	}
	for (const DroidOrder &o : d->asOrderList)
	{
		if (o.psObj == marker) { return true; }
	}
	return false;
}

struct StructGround
{
	uint32_t structId = 0;
	FEATURE *marker = nullptr;
	uint32_t until = 0;
};
std::vector<StructGround> s_structGround;
constexpr uint32_t STRUCT_GROUND_MS = 40000;   // so lange beschiesst ein Turm den befohlenen Punkt

bool isReferenced(const GameWorld &world, const BASE_OBJECT *marker)
{
	for (const StructGround &g : s_structGround)
	{
		if (g.marker == marker && gameTime < g.until) { return true; }
	}
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const DROID *d : world.objects.droids[p])
		{
			if (d != nullptr && referencedByDroid(d, marker)) { return true; }
		}
	}
	return false;
}
} // namespace

bool isMarkerStats(const FEATURE_STATS *stats)
{
	return stats != nullptr && stats == markerStats();
}

bool isMarker(const BASE_OBJECT *obj)
{
	return obj != nullptr && obj->type == OBJ_FEATURE && isMarkerStats(static_cast<const FEATURE *>(obj)->psStats);
}

FEATURE *markerAt(const Vector2i &pos, unsigned player, int layer)
{
	FEATURE_STATS *st = markerStats();
	if (st == nullptr || player >= MAX_PLAYERS || !worldOnMap(gameWorld.map, pos)) { return nullptr; }
	for (FEATURE *f : gameWorld.objects.features[0])
	{
		if (f != nullptr && !f->died && f->psStats == st && static_cast<int>(modlayer::layerOf(f)) == layer
		    && std::abs(f->pos.x - pos.x) <= REUSE_DIST && std::abs(f->pos.y - pos.y) <= REUSE_DIST)
		{
			f->visible[player] = UBYTE_MAX;
			return f;
		}
	}
	FEATURE *f = buildFeature(gameWorld, st, static_cast<UDWORD>(pos.x), static_cast<UDWORD>(pos.y), false);
	if (f == nullptr) { return nullptr; }
	f->pos.z = map_Height(gameWorld.map, pos.x, pos.y);
	f->visible[player] = UBYTE_MAX;
	if (layer != modlayer::SURFACE) { modlayer::assignLayer(f, static_cast<modlayer::Layer>(layer)); }   // Zielpunkt in der Ebene der Einheit (Marker sind nicht an Kacheln gebunden)
	return f;
}

bool canFireOnGround(const DROID *psDroid)
{
	if (psDroid == nullptr || psDroid->died || psDroid->numWeaps == 0 || psDroid->asWeaps[0].nStat == 0 || psDroid->isTransporter())
	{
		return false;
	}
	const WEAPON_STATS *ws = psDroid->getWeaponStats(0);
	if (ws == nullptr || ws->weaponSubClass == WSC_ELECTRONIC) { return false; }
	return (ws->surfaceToAir & SHOOT_ON_GROUND) != 0;
}

int scatterRadius(const WEAPON_STATS &stats)
{
	return proj_Direct(&stats) ? DIRECT_RADIUS : INDIRECT_RADIUS;
}

Vector2i scatterPoint(const Vector2i &center, int radius)
{
	if (radius <= 0) { return center; }
	int dx = 0, dy = 0;
	// gleichmaessig im Kreis: Wuerfeln im Quadrat, bis es im Kreis liegt (hoechstens 4 Versuche, danach nach innen ziehen)
	for (int i = 0; i < 4; ++i)
	{
		dx = static_cast<int>(gameRand(static_cast<uint32_t>(2 * radius + 1))) - radius;
		dy = static_cast<int>(gameRand(static_cast<uint32_t>(2 * radius + 1))) - radius;
		if (dx * dx + dy * dy <= radius * radius) { break; }
		if (i == 3) { dx /= 2; dy /= 2; }
	}
	return Vector2i(center.x + dx, center.y + dy);
}

bool canStructureFireOnGround(const STRUCTURE *psStruct)
{
	if (psStruct == nullptr || psStruct->died || psStruct->status != SS_BUILT || psStruct->numWeaps == 0 || psStruct->asWeaps[0].nStat == 0) { return false; }
	const WEAPON_STATS *ws = psStruct->getWeaponStats(0);
	if (ws == nullptr || ws->weaponSubClass == WSC_ELECTRONIC || ws->weaponSubClass == WSC_LAS_SAT) { return false; }
	return (ws->surfaceToAir & SHOOT_ON_GROUND) != 0;
}

void structureFireAt(STRUCTURE *psStruct, const Vector2i &pos)
{
	if (!canStructureFireOnGround(psStruct)) { return; }
	FEATURE *m = markerAt(pos, psStruct->player, static_cast<int>(modlayer::layerOf(psStruct)));
	if (m == nullptr) { return; }
	s_structGround.erase(std::remove_if(s_structGround.begin(), s_structGround.end(), [psStruct](const StructGround &g) { return g.structId == psStruct->id; }), s_structGround.end());
	StructGround g;
	g.structId = psStruct->id;
	g.marker = m;
	g.until = gameTime + STRUCT_GROUND_MS;
	s_structGround.push_back(g);
	syncDebug("structGround s%u (%d,%d)", psStruct->id, pos.x, pos.y);
}

BASE_OBJECT *structureGroundTarget(const STRUCTURE *psStruct)
{
	if (s_structGround.empty() || psStruct == nullptr) { return nullptr; }
	for (const StructGround &g : s_structGround)
	{
		if (g.structId == psStruct->id && gameTime < g.until && g.marker != nullptr && !g.marker->died) { return g.marker; }
	}
	return nullptr;
}

bool orderSelected(unsigned player, int worldX, int worldY)
{
	if (player >= MAX_PLAYERS || markerStats() == nullptr) { return false; }
	const bool surfaceView = modlayermap::viewLayer() == modlayer::SURFACE;
	if (!worldOnMap(gameWorld.map, worldX, worldY)) { return false; }
	bool any = false;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (d == nullptr || !d->selected || (!canFireOnGround(d) && !modicbm::isTelDroid(d))) { continue; }   // (19.11.) Atomraketen-Werfer immer
		// Ebenen: ausserhalb der Oberflaeche nur Ebenen-Sonderwaffen (Orbitalbombe, Tektonik-Torpedo); diese wirken nur in eine ANDERE Ebene
		// als die eigene (Orbitalbombe aus dem Orbit auf die Oberflaeche, Torpedo von der Oberflaeche in den Untergrund)
		const WEAPON_STATS *ws = d->getWeaponStats(0);
		const bool special = ws != nullptr && ws->crossLayer >= 0;
		if (special && static_cast<int>(modlayer::layerOf(d)) == ws->crossLayer) { continue; }
		if (!surfaceView && !special) { continue; }
		DroidOrder sOrder(DORDER_ATTACKGROUND, Vector2i(worldX, worldY));
		sendDroidInfo(d, sOrder, false, currentOrderSource());
		any = true;
	}
	// MOD: gewaehlte Tuerme/Stellungen feuern ebenfalls auf den Punkt (nur an der Oberflaeche)
	if (surfaceView)
	{
		bool tooFar = false;
		int farTiles10 = 0, rangeTiles10 = 0;
		for (STRUCTURE *s : gameWorld.objects.structures[player])
		{
			if (s == nullptr || !s->selected || !canStructureFireOnGround(s)) { continue; }
			// (18.11. 13:37 "MG-Posten dreht sich, schiesst aber nicht" - Strg + Rechtsklick auf den Boden): Tuerme koennen nicht
			// naeher heranfahren. Liegt der Punkt hinter der Reichweite, dreht der Turm nur hin und schweigt. Jetzt: er beschiesst den
			// Boden in dieser Richtung so weit er reicht, und eine Meldung sagt, dass das Ziel zu weit weg ist.
			int tx = worldX, ty = worldY;
			const int range = proj_GetLongRange(*s->getWeaponStats(0), s->player);
			const int dx = worldX - s->pos.x, dy = worldY - s->pos.y;
			const int dist = iHypot(dx, dy);
			if (range > 0 && dist > range - 8)
			{
				const int r = std::max(32, range - 24);
				tx = s->pos.x + static_cast<int>(int64_t(dx) * r / std::max(1, dist));
				ty = s->pos.y + static_cast<int>(int64_t(dy) * r / std::max(1, dist));
				tooFar = true;
				farTiles10 = std::max(farTiles10, dist * 10 / TILE_UNITS);
				rangeTiles10 = std::max(rangeTiles10, range * 10 / TILE_UNITS);
			}
			sendStructureGroundFire(s, tx, ty, currentOrderSource());
			any = true;
		}
		if (tooFar && player == selectedPlayer)
		{
			char msg[200];
			snprintf(msg, sizeof(msg), "Ziel zu weit weg (%d,%d Felder) - der Turm reicht nur %d,%d Felder weit und beschiesst den Boden so weit er kommt.",
			         farTiles10 / 10, farTiles10 % 10, rangeTiles10 / 10, rangeTiles10 % 10);
			addConsoleMessage(msg, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		}
	}
	return any;
}

bool keysForGroundFire()
{
	const bool ctrl = keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL);
	const bool shift = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	return ctrl && !shift;
}

void updatePreview(bool wanted, const Vector2i &worldPos)
{
	// (Wunsch 27.11. 22:05) rotes Fadenkreuz genau am Zielpunkt fuer alle, die Boden beschiessen koennen; der rote Streukreis dazu
	// nur bei Panzern/Fahrzeugen (nicht Infanterie/Cyborgs) und bei allen Splash-Waffen (Flaechenschaden/Brand)
	int radius = 0;
	bool any = false;
	if (wanted && selectedPlayer < MAX_PLAYERS && markerStats() != nullptr)
	{
		for (const DROID *d : gameWorld.objects.droids[selectedPlayer])
		{
			if (d == nullptr || !d->selected) { continue; }
			const bool tel = modicbm::isTelDroid(d);
			if (!canFireOnGround(d) && !tel) { continue; }
			any = true;
			const WEAPON_STATS *ws = d->getWeaponStats(0);
			if (ws == nullptr) { continue; }
			const bool vehicle = d->droidType != DROID_PERSON && !d->isCyborg();
			if (vehicle || tel || hasSplash(*ws, d->player))
			{
				radius = std::max(radius, scatterRadius(*ws));
			}
		}
		for (const STRUCTURE *s : gameWorld.objects.structures[selectedPlayer])
		{
			if (s != nullptr && s->selected && canStructureFireOnGround(s))
			{
				any = true;
				if (hasSplash(*s->getWeaponStats(0), s->player))
				{
					radius = std::max(radius, scatterRadius(*s->getWeaponStats(0)));
				}
			}
		}
	}
	s_preview = any;
	s_previewPos = worldPos;
	if (radius > 0)
	{
		range_rings::setDebugRange(static_cast<float>(worldPos.x), static_cast<float>(worldPos.y), static_cast<float>(radius));
		s_ringOn = true;
	}
	else if (s_ringOn)
	{
		range_rings::setDebugRange(0.f, 0.f, 0.f);
		s_ringOn = false;
	}
}

namespace
{
/// Balken zwischen zwei Bildschirmpunkten
void bar(float x0, float y0, float x1, float y1, float thick, PIELIGHT c)
{
	const float dx = x1 - x0, dy = y1 - y0;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len < 0.5f) { return; }
	pie_DrawSoftRoundedBoxRotated((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, len + thick, thick, std::atan2(dy, dx), c, thick * 0.5f, 1.f);
}

bool project(int wx, int wy, const glm::mat4 &m, float &sx, float &sy)
{
	const int wz = (wx >= 0 && wy >= 0 && worldOnMap(gameWorld.map, wx, wy)) ? map_Height(gameWorld.map, wx, wy) : 0;
	const Vector3i pos(wx, wz + 4, -wy);
	Vector2i sp(0, 0);
	if (pie_RotateProjectWithPerspective(&pos, m, &sp) <= 0) { return false; }
	sx = static_cast<float>(sp.x); sy = static_cast<float>(sp.y);
	return true;
}

/// rotes Fadenkreuz flach auf dem Gelaende bei (wx, wy); bright = Vorschau am Mauszeiger, sonst schon befohlenes Ziel
void crosshair(int wx, int wy, const glm::mat4 &m, bool bright)
{
	float cx = 0.f, cy = 0.f;
	if (!project(wx, wy, m, cx, cy)) { return; }
	const float W = static_cast<float>(pie_GetVideoBufferWidth()), H = static_cast<float>(pie_GetVideoBufferHeight());
	if (cx < -80.f || cy < -80.f || cx > W + 80.f || cy > H + 80.f) { return; }
	const float sc = modui::uiScale();
	// Arme in Weltmass (liegen auf dem Boden); weit herausgezoomt mindestens gut lesbare Pixelgroesse
	float R = 44.f;
	float px = 0.f, py = 0.f;
	if (project(wx + static_cast<int>(R), wy, m, px, py))
	{
		const float d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
		const float minPx = (bright ? 16.f : 12.f) * sc;
		if (d > 0.5f && d < minPx) { R *= std::min(6.f, minPx / d); }
	}
	const float gap = R * 0.3f;
	const int a = bright ? 255 : 200;
	const PIELIGHT red = pal_RGBA(255, 36, 24, static_cast<UBYTE>(a));
	const PIELIGHT dark = pal_RGBA(20, 0, 0, static_cast<UBYTE>(a * 3 / 4));
	const float thick = std::max(2.f, (bright ? 2.6f : 2.f) * sc);
	static const int DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
	float ends[4][4];
	bool ok[4];
	for (int i = 0; i < 4; ++i)
	{
		ok[i] = project(wx + static_cast<int>(DIRS[i][0] * gap), wy + static_cast<int>(DIRS[i][1] * gap), m, ends[i][0], ends[i][1])
		        && project(wx + static_cast<int>(DIRS[i][0] * R), wy + static_cast<int>(DIRS[i][1] * R), m, ends[i][2], ends[i][3]);
	}
	for (int pass = 0; pass < 2; ++pass)   // erst dunkler Rand, dann Rot
	{
		const float t = pass == 0 ? thick + 2.f : thick;
		const PIELIGHT c = pass == 0 ? dark : red;
		for (int i = 0; i < 4; ++i)
		{
			if (ok[i]) { bar(ends[i][0], ends[i][1], ends[i][2], ends[i][3], t, c); }
		}
		const float r = (pass == 0 ? 2.6f : 1.7f) * sc;
		pie_DrawSoftRoundedBox(cx - r, cy - r, cx + r, cy + r, c, r, 1.f);
	}
}
} // namespace

void drawOverlay(const glm::mat4 &m)
{
	if (selectedPlayer >= MAX_PLAYERS || markerStats() == nullptr || modlayermap::viewLayer() != modlayer::SURFACE) { return; }
	// schon befohlene Bodenziele der gewaehlten Einheiten und Tuerme
	std::vector<const BASE_OBJECT *> done;
	auto mark = [&](const BASE_OBJECT *t)
	{
		if (t == nullptr || t->died || !isMarker(t)) { return; }
		if (std::find(done.begin(), done.end(), t) != done.end()) { return; }
		done.push_back(t);
		crosshair(t->pos.x, t->pos.y, m, false);
	};
	for (const DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d == nullptr || !d->selected || d->died) { continue; }
		mark(d->order.psObj);
		mark(d->psActionTarget[0]);
	}
	for (const STRUCTURE *s : gameWorld.objects.structures[selectedPlayer])
	{
		if (s != nullptr && s->selected && !s->died) { mark(structureGroundTarget(s)); }
	}
	if (s_preview) { crosshair(s_previewPos.x, s_previewPos.y, m, true); }
}

const char *activeLabel()
{
	return (s_ringOn || s_preview) ? "Area Bombardment: Attack Area" : nullptr;
}

void tick(GameWorld &world)
{
	if (gameTime < s_nextClean) { return; }
	s_nextClean = gameTime + CLEAN_INTERVAL;
	FEATURE_STATS *st = markerStats();
	if (st == nullptr) { return; }
	s_structGround.erase(std::remove_if(s_structGround.begin(), s_structGround.end(), [](const StructGround &g) { return gameTime >= g.until || g.marker == nullptr || g.marker->died; }), s_structGround.end());
	std::vector<FEATURE *> doomed;
	for (FEATURE *f : world.objects.features[0])
	{
		if (f != nullptr && !f->died && f->psStats == st && !isReferenced(world, f))
		{
			doomed.push_back(f);
		}
	}
	for (FEATURE *f : doomed)
	{
		modlayer::forget(f->id);   // Ebenen-Eintrag des Zielpunkts freigeben
		removeFeature(f, world);
	}
}

void reset()
{
	s_structGround.clear();
	s_stats = nullptr;
	s_cachedNum = static_cast<size_t>(-1);
	s_cachedData = nullptr;
	s_nextClean = 0;
	s_preview = false;
	if (s_ringOn)
	{
		range_rings::setDebugRange(0.f, 0.f, 0.f);
		s_ringOn = false;
	}
}
}
