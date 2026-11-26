// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Gelaende verformen (Krater, Schuetzenloecher, tiefe Graeben, Zuschaufeln) - siehe modterrain.h
//
// 16.11. 18:24 (Wunsch): Krater nur 1 Kachel gross, mit Kratertextur, zurueck in den Originalzustand, sobald zugeschuettet; Graeben tiefer;
// harte Kante pro Kachel statt weicher Mulde. Umsetzung: statt Eckpunkte abzusenken, bekommt jede betroffene KACHEL eine Absenkung
// ("Sink"). Die Hoehe einer Stelle = Gelaende minus Absenkung, die an den Kachelraendern nur auf einem schmalen Streifen (SINK_EDGE
// Einheiten je Seite) uebergeht - fast senkrechte Waende. Die Originalhoehen werden nie veraendert; nur die Kacheltextur wird
// fuer Krater getauscht und beim Zuschuetten wiederhergestellt.
// 17.11. (Wunsch): "ganz hart ein 1-Feld-Krater", nur so tief wie ein Schuetzenloch, fuer ALLE Fahrzeuge unbefahrbar (Infanterie darf
// hinein, Flieger darueber), verbrannte Kratertextur schwarz / schwarzbraun im Wechsel (Zusatzkacheln, mf/crater.txt). Im Klassik-Netz
// ohne Unterteilung senkt der Krater nur den Mittelpunkt SEINES Feldes ab (Trichter) - die Nachbarfelder bleiben eben.
#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/framework/file.h"

#include <physfs.h>
#include <cstdio>
#include <cstdint>
#include <string>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "modterrain.h"
#include "modroad.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "modgroundfire.h"
#include "modlayer.h"
#include "modlayermap.h"
#include "map.h"
#include "terrain.h"
#include "droid.h"
#include "droiddef.h"
#include "effects.h"
#include "display3d.h"
#include "projectile.h"
#include "statsdef.h"
#include "stats.h"
#include "structure.h"
#include "mapgrid.h"
#include "lib/gamelib/gtime.h"
#include "game_world.h"

namespace modterrain
{
namespace
{
constexpr int SINK_EDGE = 14;          // Uebergangsbreite je Kachelseite (Weltmasseinheiten; Kachel = 128) -> fast senkrechte Kante
constexpr int MAX_CRATER_SINK = 64;    // tiefer werden Krater auch bei vielen Treffern nicht
constexpr int MAX_SINK = 120;          // absolute Grenze (Graeben)
constexpr int COVER_DEPTH = 10;        // ab dieser Tiefe gibt die Mulde Deckung
constexpr int CRATER_DEPTH = 26;       // (17.11.) ein Krater = ein Schuetzenloch tief; weitere Treffer vertiefen nicht
constexpr int VEHICLE_BLOCK_DEPTH = 8;  // ab dieser Tiefe kommt kein Fahrzeug mehr durch

struct Cell
{
	int16_t dyn = 0;            // Krater / Schuetzenloch (zuschuettbar)
	int16_t fixed = 0;          // tiefer Graben (Bauwerk) - verschwindet mit dem Bauwerk
	uint16_t origTexture = 0;   // Kacheltextur vor dem Krater
	bool texSwapped = false;
	bool origDecal = false;
	uint32_t born = 0;          // (18.11.) Entstehungszeit des Kraters (Einblenden des Kraterflecks)
	int depth() const { return std::max<int>(dyn, fixed); }
};

std::unordered_map<uint64_t, Cell> s_cells;

// MOD (24.11.): Graben-Absenkungen als Rinnen/Becken (siehe TrenchSink in modterrain.h)
std::vector<TrenchSink> s_trench;
int s_trenchLayer = -1;
std::unordered_map<uint64_t, std::vector<uint16_t>> s_trenchTiles;   // Feld -> Graben, deren Feld (128 x 128) es beruehrt
constexpr int TRENCH_HALF = DIST_TILE / 2;   // halbe Feldbreite (Original-Kachel, auch im Feinraster)
constexpr int TRENCH_RIM = 22;               // halbe Breite der Rinne in voller Tiefe (Graben 40 breit)
constexpr int PIT_RIM = 40;                  // Becken der Grube (lichte Weite 76)
constexpr int RAMP_LEN = 30;                 // Anstieg zur Feldkante, wo kein absenkender Nachbar ist

/// Anteil der vollen Tiefe (0..1024) im Abstand perp von der Rinnenmitte: bis rim voll, bis zur Feldkante linear auf 0
int rimShareI(int perp, int rim)
{
	if (perp <= rim) { return 1024; }
	if (perp >= TRENCH_HALF) { return 0; }
	return (TRENCH_HALF - perp) * 1024 / (TRENCH_HALF - rim);
}
float rimShareF(float perp, float rim)
{
	if (perp <= rim) { return 1.f; }
	if (perp >= static_cast<float>(TRENCH_HALF)) { return 0.f; }
	return (static_cast<float>(TRENCH_HALF) - perp) / (static_cast<float>(TRENCH_HALF) - rim);
}

const int ARM_DX[4] = {0, 1, 0, -1};
const int ARM_DY[4] = {-1, 0, 1, 0};

/// Anteil (0..1024) eines Grabens am Weltpunkt (ganzzahlig, deterministisch)
int trenchShareI(const TrenchSink &t, int px, int py)
{
	const int dx = px - t.x, dy = py - t.y;
	if (std::abs(dx) > TRENCH_HALF || std::abs(dy) > TRENCH_HALF) { return 0; }
	const int rim = t.pit ? PIT_RIM : TRENCH_RIM;
	int best = rimShareI(t.pit ? std::max(std::abs(dx), std::abs(dy)) : iHypot(dx, dy), rim);
	for (int k = 0; k < 4; ++k)
	{
		if ((t.arms & (1 << k)) == 0) { continue; }
		const int along = dx * ARM_DX[k] + dy * ARM_DY[k];
		if (along <= 0) { continue; }
		const int perp = std::abs(dx * ARM_DY[k] - dy * ARM_DX[k]);
		int v = rimShareI(perp, TRENCH_RIM);
		if ((t.ramps & (1 << k)) != 0) { v = v * std::clamp(TRENCH_HALF - along, 0, RAMP_LEN) / RAMP_LEN; }
		best = std::max(best, v);
	}
	return best;
}
float trenchShareF(const TrenchSink &t, float px, float py)
{
	const float dx = px - static_cast<float>(t.x), dy = py - static_cast<float>(t.y);
	const float H = static_cast<float>(TRENCH_HALF);
	if (std::fabs(dx) > H || std::fabs(dy) > H) { return 0.f; }
	const float rim = static_cast<float>(t.pit ? PIT_RIM : TRENCH_RIM);
	float best = rimShareF(t.pit ? std::max(std::fabs(dx), std::fabs(dy)) : std::sqrt(dx * dx + dy * dy), rim);
	for (int k = 0; k < 4; ++k)
	{
		if ((t.arms & (1 << k)) == 0) { continue; }
		const float along = dx * static_cast<float>(ARM_DX[k]) + dy * static_cast<float>(ARM_DY[k]);
		if (along <= 0.f) { continue; }
		const float perp = std::fabs(dx * static_cast<float>(ARM_DY[k]) - dy * static_cast<float>(ARM_DX[k]));
		float v = rimShareF(perp, static_cast<float>(TRENCH_RIM));
		if ((t.ramps & (1 << k)) != 0) { v *= std::clamp((H - along) / static_cast<float>(RAMP_LEN), 0.f, 1.f); }
		best = std::max(best, v);
	}
	return best;
}

const std::vector<uint16_t> *trenchesAt(int layer, int tileX, int tileY)
{
	if (s_trench.empty() || layer != s_trenchLayer) { return nullptr; }
	const auto it = s_trenchTiles.find((static_cast<uint64_t>(static_cast<uint32_t>(tileX)) << 32) | static_cast<uint32_t>(tileY));
	return it == s_trenchTiles.end() ? nullptr : &it->second;
}

/// Standhoehe (Spiellogik): in der Rinne die Sohle, zum Rand hin flacher
int trenchStandI(int layer, int px, int py)
{
	const std::vector<uint16_t> *l = trenchesAt(layer, px >> TILE_SHIFT, py >> TILE_SHIFT);
	if (l == nullptr) { return 0; }
	int best = 0;
	for (uint16_t i : *l)
	{
		const TrenchSink &t = s_trench[i];
		const int sh = trenchShareI(t, px, py);
		if (sh > 0) { best = std::max(best, std::min(t.floor, (t.vis * sh + 512) / 1024)); }
	}
	return best;
}
float trenchDepthF(int layer, float px, float py, bool stand)
{
	const std::vector<uint16_t> *l = trenchesAt(layer, static_cast<int>(std::floor(px / TILE_UNITS)), static_cast<int>(std::floor(py / TILE_UNITS)));
	if (l == nullptr) { return 0.f; }
	float best = 0.f;
	for (uint16_t i : *l)
	{
		const TrenchSink &t = s_trench[i];
		const float sh = trenchShareF(t, px, py);
		if (sh <= 0.f) { continue; }
		const float d = static_cast<float>(t.vis) * sh;
		best = std::max(best, stand ? std::min(static_cast<float>(t.floor), d) : d);
	}
	return best;
}
int trenchVisI(int layer, int px, int py)
{
	const std::vector<uint16_t> *l = trenchesAt(layer, px >> TILE_SHIFT, py >> TILE_SHIFT);
	if (l == nullptr) { return 0; }
	int best = 0;
	for (uint16_t i : *l) { best = std::max(best, (s_trench[i].vis * trenchShareI(s_trench[i], px, py) + 512) / 1024); }
	return best;
}

int layerTag()
{
	return modlayermap::worldsExist() ? static_cast<int>(modlayermap::resident()) : static_cast<int>(modlayer::SURFACE);
}

uint64_t keyOf(int layer, int x, int y)
{
	return (static_cast<uint64_t>(layer) << 48) | (static_cast<uint64_t>(static_cast<uint32_t>(x) & 0xffffff) << 24) | static_cast<uint64_t>(static_cast<uint32_t>(y) & 0xffffff);
}

unsigned s_version = 1;   // jede Aenderung an s_cells zaehlt hoch (Sperrkarte fuer Fahrzeuge neu aufbauen)
struct BlockCache
{
	unsigned version = 0;
	int w = 0, h = 0;
	std::vector<uint8_t> grid;
};
std::unordered_map<int, BlockCache> s_blockCache;

int cellDyn(int layer, int x, int y);
int cellFixed(int layer, int x, int y);
int visFixed(int fixed);

// Zusatzkacheln fuer die Kratertextur (mf/crater.txt: erste Nummer, Anzahl) - sonst die alte Schutt-Kachel
int s_craterFirst = -2, s_craterCount = 0;
void loadCraterTiles()
{
	if (s_craterFirst != -2) { return; }
	s_craterFirst = -1;
	s_craterCount = 0;
	char *buf = nullptr;
	UDWORD size = 0;
	if (!PHYSFS_exists("mf/crater.txt") || !loadFile("mf/crater.txt", &buf, &size) || buf == nullptr) { return; }
	const std::string txt(buf, size);
	free(buf);
	int first = 0, count = 0;
	if (sscanf(txt.c_str(), "%d %d", &first, &count) == 2 && first >= 256 && count > 0 && first + count - 1 <= static_cast<int>(TILE_NUMMASK))
	{
		s_craterFirst = first;
		s_craterCount = count;
	}
}

int cellDepth(int layer, int x, int y)
{
	const auto it = s_cells.find(keyOf(layer, x, y));
	return it == s_cells.end() ? 0 : it->second.depth();
}

int cellDyn(int layer, int x, int y)
{
	const auto it = s_cells.find(keyOf(layer, x, y));
	return it == s_cells.end() ? 0 : std::max<int>(0, it->second.dyn);
}

int cellFixed(int layer, int x, int y)
{
	const auto it = s_cells.find(keyOf(layer, x, y));
	return it == s_cells.end() ? 0 : std::max<int>(0, it->second.fixed);
}

/// (17.11. 14:14) Gelaendenetz unter tiefen Stellungen: tiefer als die Sohle des Modells (Faktor 5/2), damit das grobe Netz nirgends
/// durch den Grabenboden stoesst; Spiellogik und Standhoehe der Einheiten bleiben bei der Sohlentiefe (fixed)
int visFixed(int fixed)
{
	return fixed * 5 / 2;
}

int cellVisDepth(int layer, int x, int y)
{
	const auto it = s_cells.find(keyOf(layer, x, y));
	return it == s_cells.end() ? 0 : std::max<int>({0, it->second.dyn, visFixed(it->second.fixed)});
}

bool tileOk(const WorldMapState &m, int x, int y)
{
	if (x < 2 || y < 2 || x >= m.width - 2 || y >= m.height - 2)
	{
		return false;
	}
	const MAPTILE *t = &m.tiles[x + y * m.width];
	if (TileHasStructure(t) || TileHasFeature(t))
	{
		return false;
	}
	const uint8_t tt = terrainType(t);
	return tt != TER_WATER && tt != TER_CLIFFFACE;
}

void dirtyAround(int x, int y)
{
	++s_version;
	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			if (tileOnMap(gameWorld.map, x + dx, y + dy)) { markTileDirty(x + dx, y + dy); }
		}
	}
}

/// Kratertextur setzen (Original merken) bzw. wiederherstellen
void swapTexture(Cell &c, int x, int y, bool crater)
{
	MAPTILE *t = mapTile(gameWorld.map, x, y);
	if (crater && !c.texSwapped)
	{
		c.origTexture = t->texture;
		c.origDecal = TILE_HAS_DECAL(t) != 0;
		c.texSwapped = true;
		// Drehung/Spiegelung aus der Lage ableiten (deterministisch) - nicht jeder Krater sieht gleich aus
		// (Kachel-Viertelung: gleiche Drehung fuer alle Feinfelder einer Original-Kachel, sonst passen die Viertel nicht zusammen)
		const int cx = x / (DIST_TILE / TILE_UNITS), cy = y / (DIST_TILE / TILE_UNITS);
		const unsigned rot = static_cast<unsigned>(cx * 7 + cy * 13) & 3u;
		loadCraterTiles();
		// (17.11.) verbrannte Kratertextur: schwarz und schwarzbraun im Wechsel (Schachbrett nach Original-Kachel)
		const int tileNo = (s_craterFirst >= 0) ? s_craterFirst + ((cx + cy) & 1) % s_craterCount : static_cast<int>(RUBBLE_TILE);
		t->texture = static_cast<uint16_t>((static_cast<unsigned>(tileNo) & TILE_NUMMASK) | (rot << TILE_ROTSHIFT));
		SET_TILE_DECAL(t);
	}
	else if (!crater && c.texSwapped)
	{
		t->texture = c.origTexture;
		if (c.origDecal) { SET_TILE_DECAL(t); } else { CLEAR_TILE_DECAL(t); }
		c.texSwapped = false;
	}
}

/// Kachel loeschen, wenn nichts mehr abgesenkt ist (Textur zurueck)
void tidy(uint64_t k, int x, int y)
{
	const auto it = s_cells.find(k);
	if (it == s_cells.end()) { return; }
	if (it->second.dyn <= 0) { swapTexture(it->second, x, y, false); }
	if (it->second.dyn <= 0 && it->second.fixed <= 0) { s_cells.erase(it); }
}

void dust(int wx, int wy, int size)
{
	if (!clipXY(wx, wy))
	{
		return;
	}
	Vector3i pos(wx, map_Height(gameWorld.map, wx, wy) + 12, wy);
	// (18.11. 13:04 "etwas weniger Rauch, damit man es auch sehen kann") kleine Staubwolke ueber der Kachelmitte - verdeckt das kurze
	// Einblenden des Kraterflecks; dazu fliegt Erde senkrecht nach oben und faellt zurueck
	const int n = 1 + size;
	const int cxw = (wx / TILE_UNITS) * TILE_UNITS + TILE_UNITS / 2, cyw = (wy / TILE_UNITS) * TILE_UNITS + TILE_UNITS / 2;
	for (int i = 0; i < n; i++)
	{
		Vector3i p(cxw, pos.y, cyw);
		p.x += ((i * 53) % 41 - 20) * size / 2;
		p.z += ((i * 37) % 39 - 19) * size / 2;
		p.y += (i % 2) * 8;
		addEffect(&p, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING, false, nullptr, 0, gameTime);
	}
	static iIMDBaseShape *s_clod = nullptr;
	static unsigned s_clodTried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
	if (modelCacheStale(s_clodTried)) { s_clod = modelGet("moddirtclod.pie"); }
	if (s_clod != nullptr && s_clod->displayModel() != nullptr)
	{
		const int clods = 5 + size * 4;
		for (int i = 0; i < clods; ++i)
		{
			Vector3i p(cxw + rand() % 21 - 10, pos.y - 4, cyw + rand() % 21 - 10);
			Vector3f vel(static_cast<float>(rand() % 90 - 45), static_cast<float>(420 + rand() % 260 + size * 60), static_cast<float>(rand() % 90 - 45));
			addEffect(&p, EFFECT_GRAVITON, GRAVITON_TYPE_DIRT, true, s_clod->displayModel(), 0, graphicsTime, nullptr, &vel);
		}
	}
}

struct PendingCrater
{
	int x, y, size;
	unsigned at;
};
std::vector<PendingCrater> s_pending;   // Reihenfolge = Einschlagsreihenfolge (auf allen Rechnern gleich)

[[maybe_unused]] int craterDepth(int size)
{
	switch (size)
	{
	case 1: return 16;
	case 2: return 26;
	default: return 36;
	}
}

bool isLegged(const DROID *d)
{
	if (d == nullptr)
	{
		return false;
	}
	const PROPULSION_STATS *p = d->getPropulsionStats();
	return p != nullptr && p->propulsionType == PROPULSION_TYPE_LEGGED;
}

/// Anteil (in 1/(2E)) der Strecke [c-E, c+E] in der Kachel lo..lo+TILE_UNITS (ganzzahlig)
inline int overlapI(int c, int lo)
{
	const int a = std::max(c - SINK_EDGE, lo), b = std::min(c + SINK_EDGE, lo + TILE_UNITS);
	return std::max(0, b - a);
}
inline float overlapF(float c, float lo)
{
	const float e = static_cast<float>(SINK_EDGE);
	const float a = std::max(c - e, lo), b = std::min(c + e, lo + static_cast<float>(TILE_UNITS));
	return std::max(0.f, b - a);
}
} // namespace

int sinkAt(const WorldMapState &mapState, int worldX, int worldY)
{
	if ((s_cells.empty() && s_trench.empty()) || &mapState != &gameWorld.map)
	{
		return 0;
	}
	const int layer = layerTag();
	const int trench = trenchStandI(layer, worldX, worldY);   // (24.11.) Graben-Rinne: Sohle
	const int tx = worldX >> TILE_SHIFT, ty = worldY >> TILE_SHIFT;
	int sum = 0;
	for (int dy = -1; dy <= 1; ++dy)
	{
		const int wy = overlapI(worldY, (ty + dy) << TILE_SHIFT);
		if (wy == 0) { continue; }
		for (int dx = -1; dx <= 1; ++dx)
		{
			const int wx = overlapI(worldX, (tx + dx) << TILE_SHIFT);
			if (wx == 0) { continue; }
			const int d = cellDepth(layer, tx + dx, ty + dy);
			if (d != 0) { sum += d * wx * wy; }
		}
	}
	constexpr int norm = 4 * SINK_EDGE * SINK_EDGE;
	return std::max(trench, (sum + norm / 2) / norm);
}

namespace
{
float sinkWeighted(float worldX, float worldY, bool visual)
{
	const int layer = layerTag();
	const int tx = static_cast<int>(std::floor(worldX / TILE_UNITS)), ty = static_cast<int>(std::floor(worldY / TILE_UNITS));
	float sum = 0.f;
	for (int dy = -1; dy <= 1; ++dy)
	{
		const float wy = overlapF(worldY, static_cast<float>((ty + dy) * TILE_UNITS));
		if (wy <= 0.f) { continue; }
		for (int dx = -1; dx <= 1; ++dx)
		{
			const float wx = overlapF(worldX, static_cast<float>((tx + dx) * TILE_UNITS));
			if (wx <= 0.f) { continue; }
			const int d = visual ? cellVisDepth(layer, tx + dx, ty + dy) : cellDepth(layer, tx + dx, ty + dy);
			if (d != 0) { sum += static_cast<float>(d) * wx * wy; }
		}
	}
	return std::max(trenchDepthF(layer, worldX, worldY, !visual), sum / static_cast<float>(4 * SINK_EDGE * SINK_EDGE));
}
} // namespace

float sinkAtF(const WorldMapState &mapState, float worldX, float worldY)
{
	if ((s_cells.empty() && s_trench.empty()) || &mapState != &gameWorld.map)
	{
		return 0.f;
	}
	return sinkWeighted(worldX, worldY, true);
}

float sinkStandF(const WorldMapState &mapState, float worldX, float worldY)
{
	if ((s_cells.empty() && s_trench.empty()) || &mapState != &gameWorld.map)
	{
		return 0.f;
	}
	// (Wunsch 26.11. "Infanterie in Kratern ... sinkt durch den Boden"): das Gelaendenetz (Unterteilung 2-4 je Feld) zeigt einen Krater als
	// Gerade zwischen seinen Netzpunkten - die Standhoehe folgte aber der exakten, fast senkrechten Kraterkante und lag am Kraterrand bis zu
	// einem Drittel der Kratertiefe UNTER dem sichtbaren Boden. In reinen Krater-Mulden (kein Graben, keine Stellung) steht die Einheit
	// jetzt genau auf dem gezeichneten Netz (gleiche Netzpunkte, bilinear).
	const int sub = getTerrainMeshSubdivision();
	if (sub > 1)
	{
		const int layer = layerTag();
		const int tx = static_cast<int>(std::floor(worldX / TILE_UNITS)), ty = static_cast<int>(std::floor(worldY / TILE_UNITS));
		bool crater = false, fixedNear = false;
		for (int dy = -1; dy <= 1 && !fixedNear; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (cellFixed(layer, tx + dx, ty + dy) > 0) { fixedNear = true; break; }
				if (cellDyn(layer, tx + dx, ty + dy) > 0) { crater = true; }
			}
		}
		if (crater && !fixedNear && trenchDepthF(layer, worldX, worldY, false) <= 0.f)
		{
			const float step = static_cast<float>(TILE_UNITS) / static_cast<float>(sub);
			const float gx = std::floor(worldX / step) * step, gy = std::floor(worldY / step) * step;
			const float fx = (worldX - gx) / step, fy = (worldY - gy) / step;
			const float s00 = sinkWeighted(gx, gy, true), s10 = sinkWeighted(gx + step, gy, true);
			const float s01 = sinkWeighted(gx, gy + step, true), s11 = sinkWeighted(gx + step, gy + step, true);
			return (s00 * (1.f - fx) + s10 * fx) * (1.f - fy) + (s01 * (1.f - fx) + s11 * fx) * fy;
		}
	}
	return sinkWeighted(worldX, worldY, false);
}

namespace
{
/// (Wunsch 23.11. 04:23 "Man kann oft unter die Krater schauen. Kann das Terrain nicht eher wie die Krater geformt nach unten gehen? Und
/// in derselben Groesse?") Feinraster (Zweig viertel): ein Krater belegt 2 x 2 Feinfelder. Statt vier kleiner Trichter (je Feinfeld der
/// Mittelpunkt) senkt er den gemeinsamen Mittel-Eckpunkt um die volle Tiefe und die vier Feld-Mittelpunkte um die halbe: genau die
/// Trichterform des Kraterflecks ueber 128 x 128 (wie ein Krater im Klassik-Netz). Liegt die Ecke zwischen vier Kraterfeldern, ist sie
/// Mulde - so verbinden sich mehrere Einschlaege zu einer groesseren Mulde.
int craterCorner(int layer, int x, int y)
{
	constexpr int NB = DIST_TILE / TILE_UNITS;
	if (NB <= 1) { return 0; }
	int m = INT32_MAX;
	const int tx[4] = {x - 1, x, x - 1, x}, ty[4] = {y - 1, y - 1, y, y};
	for (int k = 0; k < 4; ++k)
	{
		const int d = cellDyn(layer, tx[k], ty[k]);
		if (d <= 0) { return 0; }
		m = std::min(m, d);
	}
	return m;
}

int cornerTotal(int layer, int x, int y)
{
	const int fixedPart = visFixed(std::max({cellFixed(layer, x - 1, y - 1), cellFixed(layer, x, y - 1), cellFixed(layer, x - 1, y), cellFixed(layer, x, y)}));
	return std::max(fixedPart, craterCorner(layer, x, y));
}
} // namespace

int sinkAtCorner(const WorldMapState &mapState, int x, int y)
{
	if ((s_cells.empty() && s_trench.empty()) || &mapState != &gameWorld.map)
	{
		return 0;
	}
	// (17.11.) tiefe Graeben (Bauwerke) senken die Ecken; Krater/Schuetzenloecher den Feldmittelpunkt (centerSink) - (23.11.) im
	// Feinraster zusaetzlich den gemeinsamen Mittel-Eckpunkt eines 2 x 2-Kraters
	const int layer = layerTag();
	return std::max(cornerTotal(layer, x, y), trenchVisI(layer, world_coord(x), world_coord(y)));   // (24.11.) Graben-Rinne
}

Vector2i craterCenter(int tileX, int tileY)
{
	const Vector2i mid(world_coord(tileX) + TILE_UNITS / 2, world_coord(tileY) + TILE_UNITS / 2);
	constexpr int NB = DIST_TILE / TILE_UNITS;
	if (NB <= 1) { return mid; }
	// Feinraster: ein Krater belegt 2 x 2 Feinfelder, seine Mitte ist die gemeinsame Ecke
	const int layer = layerTag();
	for (int cy = tileY; cy <= tileY + 1; ++cy)
	{
		for (int cx = tileX; cx <= tileX + 1; ++cx)
		{
			if (craterCorner(layer, cx, cy) > 0) { return Vector2i(world_coord(cx), world_coord(cy)); }
		}
	}
	return mid;
}

int centerSink(const WorldMapState &mapState, int x, int y)
{
	if ((s_cells.empty() && s_trench.empty()) || &mapState != &gameWorld.map)
	{
		return 0;
	}
	const int layer = layerTag();
	const int d = cellDyn(layer, x, y), f = visFixed(cellFixed(layer, x, y));
	constexpr int NB = DIST_TILE / TILE_UNITS;
	if (NB > 1 && d > 0)
	{
		const int c0 = craterCorner(layer, x, y), c1 = craterCorner(layer, x + 1, y), c2 = craterCorner(layer, x, y + 1), c3 = craterCorner(layer, x + 1, y + 1);
		if (c0 + c1 + c2 + c3 > 0)
		{
			// Mittelpunkt = Mittel der (abgesenkten) Ecken minus diese Zusatz-Absenkung: Ziel halbe Tiefe (Trichterflanke) bzw. so tief
			// wie die Ecken, wenn mehrere Krater ineinander liegen
			const int avg = (cornerTotal(layer, x, y) + cornerTotal(layer, x + 1, y) + cornerTotal(layer, x, y + 1) + cornerTotal(layer, x + 1, y + 1) + 2) / 4;
			const int want = std::max(avg, d / 2);
			return std::max(0, want - avg);
		}
	}
	// (24.11.) Graben-Rinne/Becken: Mittelpunkt so tief wie die Rinne dort, abzueglich der schon abgesenkten Ecken
	const int tr = trenchVisI(layer, world_coord(x) + TILE_UNITS / 2, world_coord(y) + TILE_UNITS / 2);
	if (tr > 0)
	{
		const int avg = (sinkAtCorner(mapState, x, y) + sinkAtCorner(mapState, x + 1, y) + sinkAtCorner(mapState, x, y + 1) + sinkAtCorner(mapState, x + 1, y + 1) + 2) / 4;
		return std::max({0, std::max(d, tr) - avg, d > f ? d - f : 0});
	}
	return d > f ? d - f : 0;   // die Ecken sind um die Grabentiefe schon abgesenkt
}

bool blocksVehicles(const WorldMapState &mapState, int x, int y)
{
	if (s_cells.empty() || &mapState != &gameWorld.map || x < 0 || y < 0 || x >= mapState.width || y >= mapState.height)
	{
		return false;
	}
	const int layer = layerTag();
	BlockCache &bc = s_blockCache[layer];
	if (bc.version != s_version || bc.w != mapState.width || bc.h != mapState.height)
	{
		bc.version = s_version;
		bc.w = mapState.width;
		bc.h = mapState.height;
		bc.grid.assign(static_cast<size_t>(bc.w) * static_cast<size_t>(bc.h), 0);
		for (const auto &kv : s_cells)
		{
			if (static_cast<int>(kv.first >> 48) != layer || kv.second.dyn < VEHICLE_BLOCK_DEPTH) { continue; }
			const int cx = static_cast<int>((kv.first >> 24) & 0xffffff), cy = static_cast<int>(kv.first & 0xffffff);
			if (cx >= 0 && cy >= 0 && cx < bc.w && cy < bc.h) { bc.grid[static_cast<size_t>(cx) + static_cast<size_t>(cy) * bc.w] = 1; }
		}
	}
	return bc.grid[static_cast<size_t>(x) + static_cast<size_t>(y) * bc.w] != 0;
}

bool addCrater(int worldX, int worldY, int size)
{
	WorldMapState &m = gameWorld.map;
	size = std::clamp(size, 1, 3);
	(void)size;   // (17.11.) alle Krater gleich gross/tief
	if (!worldOnMap(m, worldX, worldY))
	{
		return false;
	}
	const int tx = map_coord(worldX), ty = map_coord(worldY);
#if defined(WZ_MOD_FINE_GRID)
	// MOD (Kachel-Viertelung): leichte Treffer = ein Feinfeld (Viertel-Kachel), mittlere/schwere = 2x2 Feinfelder (eine Original-Kachel,
	// am Raster der Original-Kacheln ausgerichtet, damit die Kratertextur ganz zu sehen ist)
	// (17.11.) jeder Krater = genau eine Original-Kachel (2x2 Feinfelder), damit die Kratertextur ganz zu sehen ist
	const int n = DIST_TILE / TILE_UNITS;
	const int x0 = n > 1 ? tx - (tx % n) : tx, y0 = n > 1 ? ty - (ty % n) : ty;
#else
	const int n = 1;
	const int x0 = tx, y0 = ty;
#endif
	bool changed = false;
	for (int yy = y0; yy < y0 + n; yy++)
	{
		for (int xx = x0; xx < x0 + n; xx++)
		{
			modroad::onCrater(xx, yy);   // MOD (22.11. 20:50): Krater zerstoert Strassen (das Strassen-Bauwerk weicht, dann entsteht der Krater)
			if (!tileOk(m, xx, yy)) { continue; }
			const uint64_t k = keyOf(layerTag(), xx, yy);
			Cell &c = s_cells[k];
			const int before = c.dyn;
			// (17.11.) jeder Krater ist gleich: so tief wie ein Schuetzenloch; weitere Treffer vertiefen nicht
			c.dyn = static_cast<int16_t>(std::max<int>(c.dyn, CRATER_DEPTH));
			// (18.11. 13:04) keine Kratertextur-Kachel mehr - Kraterfleck in der Mitte (modcraterdecal.pie)
			if (before <= 0) { c.born = gameTime; }
			if (c.dyn != before)
			{
				dirtyAround(xx, yy);
				changed = true;
			}
		}
	}
	return changed;
}

void onProjectileImpact(const PROJECTILE *psProj)
{
	if (psProj == nullptr || psProj->psWStats == nullptr)
	{
		return;
	}
	// nur Treffer auf freien Boden (Fehlschuss, Bodenziel); Treffer auf Einheiten und Bauwerke graben nicht
	if (psProj->psDest != nullptr && !modgroundfire::isMarker(psProj->psDest))
	{
		return;
	}
	const WEAPON_STATS *ws = psProj->psWStats;
	const unsigned dmg = ws->upgrade[psProj->player].damage;
	const unsigned rad = ws->upgrade[psProj->player].radius;
	int size = 0;
	switch (ws->weaponSubClass)
	{
	case WSC_CANNON:
		if (dmg < 20) { return; }
		size = (dmg >= 70 || rad >= 192) ? 3 : (dmg >= 35 || rad >= 96) ? 2 : 1;
		break;
	case WSC_MORTARS:
		if (dmg < 12) { return; }
		size = (dmg >= 70 || rad >= 192) ? 3 : 2;
		break;
	case WSC_HOWITZERS:
	case WSC_BOMB:
		size = 3;
		break;
	default:
		return;
	}
	if (size == 1)
	{
		// leichte Kanonen graben nur jeden dritten Schuss (deterministisch aus Ort und Zeit)
		const unsigned h = static_cast<unsigned>((psProj->pos.x >> 7) * 31 + (psProj->pos.y >> 7) * 17) + gameTime / 97;
		if (h % 3 != 0)
		{
			return;
		}
	}
	// erst die Staubwolke, der Krater sinkt 0,35 s spaeter darunter ein (kein sichtbares "Aufploppen")
	dust(psProj->pos.x, psProj->pos.y, size);
	s_pending.push_back({ psProj->pos.x, psProj->pos.y, size, gameTime + 350 });
}

void tick()
{
	if (s_pending.empty()) { return; }
	size_t keep = 0;
	for (size_t i = 0; i < s_pending.size(); ++i)
	{
		const PendingCrater pc = s_pending[i];
		if (gameTime >= pc.at)
		{
			addCrater(pc.x, pc.y, pc.size);
		}
		else
		{
			s_pending[keep++] = pc;
		}
	}
	s_pending.resize(keep);
}

int depthAtTile(int tileX, int tileY)
{
	if (s_cells.empty())
	{
		return 0;
	}
	return cellDepth(layerTag(), tileX, tileY);
}

bool tileGivesCover(int tileX, int tileY)
{
	return depthAtTile(tileX, tileY) >= COVER_DEPTH;
}

bool droidInCover(const DROID *psDroid)
{
	if (s_cells.empty() || !isLegged(psDroid))
	{
		return false;
	}
	return tileGivesCover(map_coord(psDroid->pos.x), map_coord(psDroid->pos.y));
}

int fillTile(int tileX, int tileY, int amount)
{
	if (s_cells.empty() || amount <= 0)
	{
		return 0;
	}
	const uint64_t k = keyOf(layerTag(), tileX, tileY);
	const auto it = s_cells.find(k);
	if (it == s_cells.end() || it->second.dyn <= 0)
	{
		return depthAtTile(tileX, tileY);
	}
	// Zuschuetten geht schneller als das Graben dauerte: je Aufruf mindestens 2 Einheiten
	int left = it->second.dyn - std::max(2, amount * 2);
	if (left <= 6) { left = 0; }   // Rest sofort eben (die Bagger suchen erst ab 6 Tiefe) -> Originalzustand
	it->second.dyn = static_cast<int16_t>(left);
	dirtyAround(tileX, tileY);
	tidy(k, tileX, tileY);   // ganz zu -> Originaltextur zurueck
	return depthAtTile(tileX, tileY);
}

int craterDepthAt(int tileX, int tileY)
{
	if (s_cells.empty()) { return 0; }
	return cellDyn(layerTag(), tileX, tileY);
}

bool findDent(const Vector2i &worldCenter, int radiusTiles, int minDepth, Vector2i &outTile)
{
	if (s_cells.empty())
	{
		return false;
	}
	const int layer = layerTag();
	const int cx = map_coord(worldCenter.x), cy = map_coord(worldCenter.y);
	int best = minDepth - 1;
	for (int ty = cy - radiusTiles; ty <= cy + radiusTiles; ty++)
	{
		for (int tx = cx - radiusTiles; tx <= cx + radiusTiles; tx++)
		{
			const auto it = s_cells.find(keyOf(layer, tx, ty));
			if (it == s_cells.end()) { continue; }
			const int d = it->second.dyn;   // nur Krater/Loecher, keine Graben-Bauwerke
			if (d > best)
			{
				best = d;
				outTile = Vector2i(tx, ty);
			}
		}
	}
	return best >= minDepth;
}

bool dig(int tileX, int tileY, int depth, int bermHeight, unsigned dirAngle)
{
	(void)bermHeight; (void)dirAngle;   // 16.11.: kein Erdwall mehr - harte Kante, das Loch ist einfach tiefer
	WorldMapState &m = gameWorld.map;
	if (!tileOk(m, tileX, tileY))
	{
		return false;
	}
	Cell &c = s_cells[keyOf(layerTag(), tileX, tileY)];
	const int want = std::clamp(depth * 2, 0, MAX_CRATER_SINK);   // Schuetzenloch: doppelt so tief wie frueher
	if (c.dyn >= want)
	{
		return false;
	}
	c.dyn = static_cast<int16_t>(want);
	dirtyAround(tileX, tileY);
	return true;
}

void craterPiles(std::vector<CraterPile> &out)
{
	out.clear();
	if (s_cells.empty()) { return; }
	const uint64_t layer = static_cast<uint64_t>(layerTag());
	constexpr int N = DIST_TILE / TILE_UNITS;   // ein Krater = N x N Felder (Feinraster 2x2)
	static const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
	static const int DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
	for (const auto &kv : s_cells)
	{
		if ((kv.first >> 48) != layer || kv.second.dyn <= 0) { continue; }
		const int x = static_cast<int>((kv.first >> 24) & 0xffffff), y = static_cast<int>(kv.first & 0xffffff);
		// je Krater nur ein Haufen: das Feld links oben des Blocks (links und oben kein Krater)
		if (N > 1 && (cellDyn(static_cast<int>(layer), x - 1, y) > 0 || cellDyn(static_cast<int>(layer), x, y - 1) > 0)) { continue; }
		const unsigned h = static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(y) * 19349663u;
		const int d = static_cast<int>(h % 8u);
		const float diag = (DX[d] != 0 && DY[d] != 0) ? 0.7071f : 1.f;
		const float cx = static_cast<float>(world_coord(x)) + static_cast<float>(N * TILE_UNITS) * 0.5f;
		const float cy = static_cast<float>(world_coord(y)) + static_cast<float>(N * TILE_UNITS) * 0.5f;
		const float off = (N > 1 ? static_cast<float>(DIST_TILE) * 0.47f : 38.f) * diag;   // (18.11.) direkt am Rand des kleinen Kraterflecks
		CraterPile p;
		p.worldX = static_cast<int>(cx + static_cast<float>(DX[d]) * off);
		p.worldY = static_cast<int>(cy + static_cast<float>(DY[d]) * off);
		// (18.11. 12:18) kein Erdhaufen auf/an einem Bauwerk (Krater neben einem Bauplatz, Haufen ragte in das Gebaeude)
		if (worldOnMap(gameWorld.map, p.worldX, p.worldY))
		{
			const MAPTILE *pt = worldTile(gameWorld.map, p.worldX, p.worldY);
			if (pt != nullptr && TileHasStructure(pt)) { continue; }
		}
		const float depth = static_cast<float>(kv.second.dyn) / static_cast<float>(CRATER_DEPTH);
		p.scale = std::clamp(depth, 0.f, 1.f) * (0.78f + 0.32f * static_cast<float>((h >> 8) % 100u) / 100.f) * 0.6f;
		{
			// sichtbarer Trichter (Mitte des Kraters tief, zu den Kanten des 128er-Feldes hin flach)
			const float cs = static_cast<float>(N > 1 ? cornerTotal(static_cast<int>(layer), x + 1, y + 1) : centerSink(gameWorld.map, x, y));
			const float ex = std::fabs(static_cast<float>(p.worldX) - cx), ey = std::fabs(static_cast<float>(p.worldY) - cy);
			p.funnel = cs * std::max(0.f, 1.f - std::max(ex, ey) / (static_cast<float>(N * TILE_UNITS) * 0.5f));
		}
		p.rotDeg = static_cast<int>((h >> 16) % 360u);
		if (p.scale >= 0.08f) { out.push_back(p); }
	}
}

void craterDecals(std::vector<CraterDecal> &out)
{
	out.clear();
	if (s_cells.empty()) { return; }
	const uint64_t layer = static_cast<uint64_t>(layerTag());
	for (const auto &kv : s_cells)
	{
		if ((kv.first >> 48) != layer || kv.second.dyn <= 0) { continue; }
		const int x = static_cast<int>((kv.first >> 24) & 0xffffff), y = static_cast<int>(kv.first & 0xffffff);
		constexpr int NB = DIST_TILE / TILE_UNITS;   // Feinraster: ein Krater = NB x NB Felder
		if (NB > 1 && (cellDyn(static_cast<int>(layer), x - 1, y) > 0 || cellDyn(static_cast<int>(layer), x, y - 1) > 0)) { continue; }
		const unsigned h = static_cast<unsigned>(x) * 2654435761u ^ static_cast<unsigned>(y) * 40503u;
		CraterDecal d;
		d.tileX = x; d.tileY = y;
		d.worldX = world_coord(x) + NB * TILE_UNITS / 2;
		d.worldY = world_coord(y) + NB * TILE_UNITS / 2;
		d.depth = static_cast<float>(kv.second.dyn) / static_cast<float>(CRATER_DEPTH);
		d.rotDeg = static_cast<int>(h % 360u);
		d.scale = 0.88f + 0.2f * static_cast<float>((h >> 9) % 100u) / 100.f;
		// (18.11.) nicht aufploppen, aber auch nicht langsam: in 0,2 s unter der Staubwolke einblenden
		const uint32_t age = kv.second.born != 0 && graphicsTime >= kv.second.born ? graphicsTime - kv.second.born : 100000u;
		d.alpha = std::clamp(static_cast<float>(age) / 200.f, 0.f, 1.f);
		out.push_back(d);
	}
}

void setStructureSink(int tileX, int tileY, int depth)
{
	const uint64_t k = keyOf(layerTag(), tileX, tileY);
	depth = std::clamp(depth, 0, MAX_SINK);
	auto it = s_cells.find(k);
	if (it == s_cells.end())
	{
		if (depth <= 0) { return; }
		it = s_cells.emplace(k, Cell()).first;
	}
	if (it->second.fixed == depth) { return; }
	it->second.fixed = static_cast<int16_t>(depth);
	dirtyAround(tileX, tileY);
	tidy(k, tileX, tileY);
}

void setTrenchSinks(std::vector<TrenchSink> list)
{
	const int layer = layerTag();
	auto same = [](const TrenchSink &a, const TrenchSink &b) {
		return a.x == b.x && a.y == b.y && a.floor == b.floor && a.vis == b.vis && a.arms == b.arms && a.ramps == b.ramps && a.pit == b.pit;
	};
	std::sort(list.begin(), list.end(), [](const TrenchSink &a, const TrenchSink &b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
	if (layer == s_trenchLayer && list.size() == s_trench.size() && std::equal(list.begin(), list.end(), s_trench.begin(), same)) { return; }
	// geaenderte Felder neu zeichnen (alte und neue Lage)
	auto touch = [](const TrenchSink &t) {
		const int x0 = (t.x - TRENCH_HALF) >> TILE_SHIFT, y0 = (t.y - TRENCH_HALF) >> TILE_SHIFT;
		const int x1 = (t.x + TRENCH_HALF) >> TILE_SHIFT, y1 = (t.y + TRENCH_HALF) >> TILE_SHIFT;
		for (int ty = y0; ty <= y1; ++ty) { for (int tx = x0; tx <= x1; ++tx) { if (tileOnMap(gameWorld.map, tx, ty)) { dirtyAround(tx, ty); } } }
	};
	if (layer == s_trenchLayer)
	{
		// nur die Unterschiede (beide Listen sortiert)
		size_t i = 0, j = 0;
		auto before = [](const TrenchSink &a, const TrenchSink &b) { return a.y != b.y ? a.y < b.y : a.x < b.x; };
		while (i < s_trench.size() || j < list.size())
		{
			if (j >= list.size() || (i < s_trench.size() && before(s_trench[i], list[j]))) { touch(s_trench[i++]); }
			else if (i >= s_trench.size() || before(list[j], s_trench[i])) { touch(list[j++]); }
			else { if (!same(s_trench[i], list[j])) { touch(s_trench[i]); touch(list[j]); } ++i; ++j; }
		}
	}
	else
	{
		for (const TrenchSink &t : s_trench) { touch(t); }
		for (const TrenchSink &t : list) { touch(t); }
	}
	s_trench.swap(list);
	s_trenchLayer = layer;
	s_trenchTiles.clear();
	for (size_t n = 0; n < s_trench.size() && n < 65535; ++n)
	{
		const TrenchSink &t = s_trench[n];
		const int x0 = (t.x - TRENCH_HALF) >> TILE_SHIFT, y0 = (t.y - TRENCH_HALF) >> TILE_SHIFT;
		const int x1 = (t.x + TRENCH_HALF) >> TILE_SHIFT, y1 = (t.y + TRENCH_HALF) >> TILE_SHIFT;
		for (int ty = y0; ty <= y1; ++ty)
		{
			for (int tx = x0; tx <= x1; ++tx)
			{
				s_trenchTiles[(static_cast<uint64_t>(static_cast<uint32_t>(tx)) << 32) | static_cast<uint32_t>(ty)].push_back(static_cast<uint16_t>(n));
			}
		}
	}
	++s_version;
}

void reset()
{
	s_trench.clear();
	s_trenchTiles.clear();
	s_trenchLayer = -1;
	s_cells.clear();
	s_pending.clear();
	s_blockCache.clear();
	++s_version;
	s_craterFirst = -2;
}

void saveState(std::vector<int> &out)
{
	out.clear();
	const int surface = static_cast<int>(modlayer::SURFACE);
	std::vector<std::pair<uint64_t, Cell>> list;
	for (const auto &kv : s_cells)
	{
		if (static_cast<int>(kv.first >> 48) != surface || kv.second.dyn <= 0) { continue; }
		list.emplace_back(kv.first, kv.second);
	}
	std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	for (const auto &e : list)
	{
		out.push_back(static_cast<int>((e.first >> 24) & 0xffffff));
		out.push_back(static_cast<int>(e.first & 0xffffff));
		out.push_back(e.second.dyn);
		out.push_back(e.second.origTexture);
		out.push_back(e.second.texSwapped ? 1 : 0);
		out.push_back(e.second.origDecal ? 1 : 0);
	}
}

void loadState(const std::vector<int> &in)
{
	const int surface = static_cast<int>(modlayer::SURFACE);
	for (size_t i = 0; i + 6 <= in.size(); i += 6)
	{
		const int x = in[i], y = in[i + 1];
		if (!tileOnMap(gameWorld.map, x, y)) { continue; }
		Cell &c = s_cells[keyOf(surface, x, y)];
		c.dyn = static_cast<int16_t>(std::clamp(in[i + 2], 0, static_cast<int>(MAX_CRATER_SINK)));
		c.origTexture = static_cast<uint16_t>(in[i + 3]);
		c.texSwapped = in[i + 4] != 0;
		c.origDecal = in[i + 5] != 0;
		swapTexture(c, x, y, false);   // (18.11.) alte ganze Krater-Kacheln zurueck - der Krater ist jetzt ein Fleck in der Kachelmitte
		dirtyAround(x, y);
	}
}
} // namespace modterrain
