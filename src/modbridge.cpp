// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Bruecken ueber Wasser (siehe modbridge.h)
#include "modbridge.h"

#include <algorithm>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include <cmath>
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piematrix.h"

#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "objmem.h"
#include "structure.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace modbridge
{
namespace
{
std::vector<uint8_t> s_bridged;     // je Spielfeld: fertige Bruecke ueber Wasser
std::vector<int> s_list;            // dieselben Felder als Liste (sortiert)
int s_w = 0, s_h = 0;
constexpr int END_RAMP = 13;       // (22.11. 20:26) Rampe des Vanilla-Brueckenendes in der Mitte des Feldes
constexpr int DECK = DIST_TILE / 3; // Wasserstand liegt 1/3 Feld unter dem urspruenglichen Gelaende (afterMapLoad) - die Fahrbahn liegt dort

bool surfaceResident()
{
	return !modlayermap::worldsExist() || modlayermap::resident() == modlayer::SURFACE;
}

bool waterTile(int tx, int ty)
{
	return tileOnMap(gameWorld.map, tx, ty) && terrainType(mapTile(gameWorld.map, tx, ty)) == TER_WATER;
}

/// Fahrbahnhoehe ueber einem Wasserfeld: Wasserstand + 1/3 Feld je Ecke (= Gelaende vor dem Ausheben des Flussbetts), weich verblendet
int waterDeck(int wx, int wy)
{
	const WorldMapState &m = gameWorld.map;
	wx = std::clamp(wx, 0, world_coord(m.width) - 1);
	wy = std::clamp(wy, 0, world_coord(m.height) - 1);
	const int tx = map_coord(wx), ty = map_coord(wy);
	const int fx = wx - world_coord(tx), fy = wy - world_coord(ty);
	auto corner = [&](int x, int y) { return map_WaterHeight(m, std::min(x, m.width - 1), std::min(y, m.height - 1)) + DECK; };
	const int64_t h00 = corner(tx, ty), h10 = corner(tx + 1, ty), h01 = corner(tx, ty + 1), h11 = corner(tx + 1, ty + 1);
	const int64_t top = h00 * (TILE_UNITS - fx) + h10 * fx, bot = h01 * (TILE_UNITS - fx) + h11 * fx;
	return static_cast<int>((top * (TILE_UNITS - fy) + bot * fy) / (static_cast<int64_t>(TILE_UNITS) * TILE_UNITS));
}

bool bridgeAt(int tx, int ty)
{
	if (!tileOnMap(gameWorld.map, tx, ty)) { return false; }
	const BASE_OBJECT *o = mapTile(gameWorld.map, tx, ty)->psObject;
	return o != nullptr && o->type == OBJ_STRUCTURE && isBridge(static_cast<const STRUCTURE *>(o));
}

/// Laengsrichtung einer Bruecke auf diesem Wasserfeld: true = entlang x (Welt), false = entlang y
bool alongX(int tx, int ty)
{
	if (bridgeAt(tx - 1, ty) || bridgeAt(tx + 1, ty)) { return true; }
	if (bridgeAt(tx, ty - 1) || bridgeAt(tx, ty + 1)) { return false; }
	return !waterTile(tx - 1, ty) || !waterTile(tx + 1, ty);   // ein einzelnes Feld: in Richtung des Ufers
}

/// Brueckenende auf Land: Richtung zum Wasser/zur Bruecke (Nachbar dx, dy); false = kein solcher Nachbar
bool endFacing(int tx, int ty, int &dx, int &dy)
{
	static const int N[4][2] = { {0, -1}, {1, 0}, {0, 1}, {-1, 0} };
	for (const auto &n : N)
	{
		if (waterTile(tx + n[0], ty + n[1])) { dx = n[0]; dy = n[1]; return true; }
	}
	for (const auto &n : N)
	{
		if (bridgeAt(tx + n[0], ty + n[1])) { dx = n[0]; dy = n[1]; return true; }
	}
	return false;
}
}

bool isBridgeStats(const STRUCTURE_STATS *s)
{
	return s != nullptr && (s->id.startsWith("A0Bridge") || s->id.startsWith("A0RailTrackBridge"));
}

bool isBridge(const STRUCTURE *s)
{
	return s != nullptr && isBridgeStats(s->pStructureType);
}

bool bridgedTile(int tx, int ty)
{
	if (tx < 0 || ty < 0 || tx >= s_w || ty >= s_h || s_bridged.empty()) { return false; }
	return s_bridged[static_cast<size_t>(ty * s_w + tx)] != 0;
}

int deckHeight(int wx, int wy)
{
	if (s_bridged.empty() || !surfaceResident()) { return INT_MIN; }
	if (!bridgedTile(map_coord(wx), map_coord(wy))) { return INT_MIN; }
	return waterDeck(wx, wy);
}

int structureDeckHeight(const STRUCTURE *s)
{
	if (s == nullptr) { return 0; }
	// Landfeld: Mitte der Rampe des Vanilla-Brueckenendes (mibridg1: Fahrbahn 7..19 ueber dem Boden)
	return waterTile(map_coord(s->pos.x), map_coord(s->pos.y)) ? waterDeck(s->pos.x, s->pos.y) : map_Height(gameWorld.map, s->pos.x, s->pos.y) + END_RAMP;
}

void tick()
{
	if (!surfaceResident()) { return; }
	WorldMapState &m = gameWorld.map;
	if (m.width <= 0 || m.height <= 0) { return; }
	if (s_w != m.width || s_h != m.height)
	{
		s_w = m.width; s_h = m.height;
		s_bridged.assign(static_cast<size_t>(s_w) * static_cast<size_t>(s_h), 0);
		s_list.clear();
	}
	std::vector<int> cur;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died || s->status != SS_BUILT || !isBridge(s)) { continue; }
			const StructureBounds b = getStructureBounds(s);
			for (int y = b.map.y; y < b.map.y + b.size.y; ++y)
			{
				for (int x = b.map.x; x < b.map.x + b.size.x; ++x)
				{
					if (waterTile(x, y)) { cur.push_back(y * s_w + x); }
				}
			}
		}
	}
	std::sort(cur.begin(), cur.end());
	cur.erase(std::unique(cur.begin(), cur.end()), cur.end());
	bool changed = cur != s_list;
	for (int i : cur)
	{
		if ((blockTile(m, i % s_w, i / s_w, 0) & WATER_BLOCKED) != 0) { changed = true; break; }   // Sperrbits neu aufgebaut (Ebenenwechsel/Laden)
	}
	if (!changed) { return; }
	for (int i : s_list)
	{
		if (std::binary_search(cur.begin(), cur.end(), i)) { continue; }
		s_bridged[static_cast<size_t>(i)] = 0;
		if (waterTile(i % s_w, i / s_w)) { auxSetBlocking(m, i % s_w, i / s_w, WATER_BLOCKED); }
	}
	for (int i : cur)
	{
		s_bridged[static_cast<size_t>(i)] = 1;
		auxClearBlocking(m, i % s_w, i / s_w, WATER_BLOCKED);
	}
	s_list = cur;
	mapFloodFillContinents(m);
}

void reset()
{
	s_bridged.clear();
	s_list.clear();
	s_w = s_h = 0;
}

bool drawBridge(const STRUCTURE *s, PIELIGHT brightness, const glm::mat4 &viewMatrix)
{
	// (Wunsch 22.11. 20:26) Aussehen von Vanilla: Brueckenmitte mit Pfeiler (mibridge, Fahrbahn 97 ueber dem Modell-Nullpunkt) ueber Wasser,
	// Brueckenende (mibridg1, Rampe steigt zur +z-Seite an) am Ufer. Eisenbahnbruecken gleich - modrail legt das Gleis auf die Fahrbahn.
	enum { MIDDLE = 0, END, NSHAPE };
	static const char *const names[NSHAPE] = { "mibridge.pie", "mibridg1.pie" };
	static const iIMDShape *shapes[NSHAPE] = {};
	static unsigned s_gen = 0;
	constexpr float MID_DECK = 97.f;
	if (modelCacheStale(s_gen))
	{
		for (int k = 0; k < NSHAPE; ++k)
		{
			const iIMDBaseShape *b = modelGet(WzString::fromUtf8(names[k]));
			shapes[k] = b ? b->displayModel() : nullptr;
		}
	}
	if (shapes[MIDDLE] == nullptr || shapes[END] == nullptr) { return false; }
	const bool built = s->status == SS_BUILT;
	const int flags = built ? 0 : pie_TRANSLUCENT;
	const int flagData = built ? 0 : 110;
	const StructureBounds b = getStructureBounds(s);
	for (int ty = b.map.y; ty < b.map.y + b.size.y; ++ty)
	{
		for (int tx = b.map.x; tx < b.map.x + b.size.x; ++tx)
		{
			const int cx = world_coord(tx) + TILE_UNITS / 2, cy = world_coord(ty) + TILE_UNITS / 2;
			const glm::vec3 at(static_cast<float>(cx), 0.f, static_cast<float>(-cy));
			if (waterTile(tx, ty))
			{
				const float h = static_cast<float>(waterDeck(cx, cy)) - MID_DECK;
				const float yaw = alongX(tx, ty) ? 1.5707963f : 0.f;   // Modell laeuft entlang z = Welt y
				const glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.f), at + glm::vec3(0.f, h, 0.f)), yaw, glm::vec3(0.f, 1.f, 0.f));
				pie_Draw3DShape(shapes[MIDDLE], 0, 0, brightness, flags, flagData, m, viewMatrix);
				continue;
			}
			int dx = 0, dy = -1;
			endFacing(tx, ty, dx, dy);
			// Modell +z (hohe Seite der Rampe) zeigt zum Nachbarn: Bild-Richtung (dx, -dy) = (sin a, cos a)
			const float yaw = std::atan2(static_cast<float>(dx), static_cast<float>(-dy));
			const float h = static_cast<float>(map_Height(gameWorld.map, cx, cy));
			const glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.f), at + glm::vec3(0.f, h, 0.f)), yaw, glm::vec3(0.f, 1.f, 0.f));
			pie_Draw3DShape(shapes[END], 0, 0, brightness, flags, flagData, m, viewMatrix);
		}
	}
	return true;
}

void onDestroyed(const STRUCTURE *s, GameWorld &world)
{
	// (Wunsch 22.11. 20:26 "Du hast sogar eine schon zerstoerte Variante fuer das Brueckenwrack-Modell"): zerstoerte Bruecke hinterlaesst
	// je Feld das Vanilla-Wrack (Merkmal "A0BridgeWreck", Modell mibrdfuk) in Laengsrichtung; abgerissene Bruecken nicht.
	if (s == nullptr || !isBridge(s) || s->status != SS_BUILT) { return; }
	const int idx = getFeatureStatFromName(WzString::fromUtf8("A0BridgeWreck"));
	if (idx < 0) { return; }
	const StructureBounds b = getStructureBounds(s);
	for (int ty = b.map.y; ty < b.map.y + b.size.y; ++ty)
	{
		for (int tx = b.map.x; tx < b.map.x + b.size.x; ++tx)
		{
			if (!tileOnMap(world.map, tx, ty) || mapTile(world.map, tx, ty)->psObject != nullptr) { continue; }
			FEATURE *f = buildFeature(world, &asFeatureStats[idx], world_coord(tx) + TILE_UNITS / 2, world_coord(ty) + TILE_UNITS / 2, false);
			if (f != nullptr && waterTile(tx, ty) && alongX(tx, ty)) { f->rot.direction = DEG(90); }
		}
	}
}
}
