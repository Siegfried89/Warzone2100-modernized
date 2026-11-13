// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Eigene Karte je Ebene, siehe modlayermap.h

#include "modlayermap.h"
#include "modcombot.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"
#include "lib/ivis_opengl/pielighting.h"

#include "lib/framework/file.h"
#include "texture.h"
#include "advvis.h"
#include "ai.h"
#include "game_world.h"
#include "lighting.h"
#include "map.h"
#include "multiplay.h"
#include "terrain.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace modlayermap
{

namespace
{

constexpr int NL = 3;

/// Was zu einer Karte gehoert und beim Umschalten den Platz wechselt. Breite/Hoehe sind in allen Ebenen gleich.
struct Slot
{
	std::unique_ptr<MAPTILE[]> tiles;
	std::array<std::unique_ptr<uint8_t[]>, AUX_MAX> blockMap;
	std::array<std::unique_ptr<uint8_t[]>, MAX_PLAYERS + AUX_MAX> auxMap;
	WorldScrollLimits scroll;
};

Slot s_slot[NL];            // Zwischenspeicher; der Eintrag der aktiven Ebene ist leer
Layer s_resident = modlayer::SURFACE;
Layer s_view = modlayer::SURFACE;
bool s_built = false;
bool s_failed = false;      // Erzeugen nicht moeglich (z.B. keine Felswand-Kachel im Tileset): nicht jedes Mal neu versuchen
bool s_parked = false;      // Gefahrenkarten-Thread angehalten (solange die Oberflaeche nicht aktiv ist)
bool s_dirtyView = false;   // Gelaende der angezeigten Ebene neu aufbauen
int s_scopeDepth = 0;
int s_w = 0;
int s_h = 0;
std::string s_mapName;
uint32_t s_seed = 0;
bool s_deferRefresh = false;   // beim Start mehrere Kammern nacheinander freilegen, Karte erst am Ende neu berechnen
bool s_pendingRefresh = false;
bool s_homesDone = false;

// ---------------------------------------------------------------------------------------------------------------------
// Karten hin- und herschieben
// ---------------------------------------------------------------------------------------------------------------------

void takeFromWorld(Slot &dst)
{
	WorldMapState &m = gameWorld.map;
	dst.tiles = std::move(m.tiles);
	for (size_t i = 0; i < dst.blockMap.size(); ++i) { dst.blockMap[i] = std::move(m.blockMap[i]); }
	for (size_t i = 0; i < dst.auxMap.size(); ++i) { dst.auxMap[i] = std::move(m.auxMap[i]); }
	dst.scroll = m.scroll;
}

void putIntoWorld(Slot &src)
{
	WorldMapState &m = gameWorld.map;
	m.tiles = std::move(src.tiles);
	for (size_t i = 0; i < src.blockMap.size(); ++i) { m.blockMap[i] = std::move(src.blockMap[i]); }
	for (size_t i = 0; i < src.auxMap.size(); ++i) { m.auxMap[i] = std::move(src.auxMap[i]); }
	m.scroll = src.scroll;
}

void rebuildDisplay()
{
	s_dirtyView = false;
	if (gameWorld.map.tiles == nullptr) { return; }
	getCurrentLightmapData().reset(static_cast<size_t>(gameWorld.map.width), static_cast<size_t>(gameWorld.map.height));
	if (!initTerrain(gameWorld.map))
	{
		debug(LOG_ERROR, "modlayermap: Gelaende der Ebene %d konnte nicht aufgebaut werden", static_cast<int>(s_resident));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Zufall (nur Ganzzahlen, damit alle Rechner exakt dasselbe erzeugen)
// ---------------------------------------------------------------------------------------------------------------------

struct Rng
{
	uint32_t s;
	explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) { next(); next(); }
	uint32_t next()
	{
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}
	int range(int n) { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

uint32_t hashStr(const std::string &str)
{
	uint32_t h = 2166136261u;
	for (const char c : str) { h ^= static_cast<uint8_t>(c); h *= 16777619u; }
	return h;
}

uint32_t hash2(int x, int y, uint32_t salt)
{
	uint32_t h = salt * 747796405u + 2891336453u;
	h ^= static_cast<uint32_t>(x) * 0x27D4EB2Du;
	h = (h << 13) | (h >> 19);
	h ^= static_cast<uint32_t>(y) * 0x165667B1u;
	h *= 0x85EBCA6Bu;
	h ^= h >> 15;
	h *= 0xC2B2AE35u;
	h ^= h >> 13;
	return h;
}

// ---------------------------------------------------------------------------------------------------------------------
// Kacheln aussuchen (Fels = Felswand-Kachel, blockiert; Boden = brauner Boden)
// ---------------------------------------------------------------------------------------------------------------------

struct Palette
{
	std::vector<uint16_t> floor;
	std::vector<uint16_t> rock;
};

bool pickPalette(Palette &p)
{
	const int maxId = std::min<int>(MAX_TILE_TEXTURES, 0x200);
	for (int id = 0; id < maxId; ++id)
	{
		if (terrainTypes[id] == TER_CLIFFFACE) { p.rock.push_back(static_cast<uint16_t>(id)); }
	}
	const unsigned order[] = { TER_BAKEDEARTH, TER_SANDYBRUSH, TER_REDBRUSH, TER_PINKROCK, TER_GREENMUD };
	for (const unsigned want : order)
	{
		for (int id = 0; id < maxId; ++id)
		{
			if (terrainTypes[id] == want) { p.floor.push_back(static_cast<uint16_t>(id)); }
		}
		if (p.floor.size() >= 3) { break; }
	}
	if (p.floor.empty() || p.rock.empty())
	{
		// Notloesung: Kacheln, die die Oberflaechenkarte selbst verwendet
		const WorldMapState &m = gameWorld.map;
		const size_t n = static_cast<size_t>(m.width) * static_cast<size_t>(m.height);
		for (size_t i = 0; i < n; i += 7)
		{
			const MAPTILE &t = m.tiles[i];
			const uint16_t id = static_cast<uint16_t>(TileNumber_tile(t.texture));
			const unsigned ty = terrainType(&t);
			if (ty == TER_CLIFFFACE) { if (std::find(p.rock.begin(), p.rock.end(), id) == p.rock.end()) { p.rock.push_back(id); } }
			else if (ty != TER_WATER) { if (std::find(p.floor.begin(), p.floor.end(), id) == p.floor.end()) { p.floor.push_back(id); } }
		}
	}
	return !p.floor.empty() && !p.rock.empty();
}

// ---------------------------------------------------------------------------------------------------------------------
// Erzeugen
// ---------------------------------------------------------------------------------------------------------------------

struct Pt { int x; int y; };

struct Grid
{
	int W;
	int H;
	std::vector<uint8_t> cells;
	std::vector<int16_t> tid;   // nur Orbit mit MF-Inseln: Nummer der Inselkachel je Feld (-1 = Leere); leer = nicht benutzt
	Grid(int w, int h) : W(w), H(h), cells(static_cast<size_t>(w) * static_cast<size_t>(h), 0) {}
	bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < W && y < H; }
	bool at(int x, int y) const { return inside(x, y) && cells[static_cast<size_t>(x) + static_cast<size_t>(y) * W] != 0; }
	void set(int x, int y) { if (inside(x, y)) { cells[static_cast<size_t>(x) + static_cast<size_t>(y) * W] = 1; } }
	void clear(int x, int y) { if (inside(x, y)) { cells[static_cast<size_t>(x) + static_cast<size_t>(y) * W] = 0; } }
};

constexpr int MARGIN = 3;

void carveDisc(Grid &g, int cx, int cy, int rad)
{
	for (int dy = -rad; dy <= rad; ++dy)
	{
		for (int dx = -rad; dx <= rad; ++dx)
		{
			if (dx * dx + dy * dy > rad * rad + rad / 2) { continue; }
			const int x = cx + dx, y = cy + dy;
			if (x < MARGIN || y < MARGIN || x >= g.W - MARGIN || y >= g.H - MARGIN) { continue; }
			g.set(x, y);
		}
	}
}

int sgn(int v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }

// Gang zwischen zwei Punkten: ueberwiegend geradeaus, ab und zu ein Schlenker; Breite 3 Felder
void carveTunnel(Grid &g, Rng &r, Pt a, Pt b, int rad)
{
	int x = a.x, y = a.y;
	int guard = 6 * (g.W + g.H);
	while ((x != b.x || y != b.y) && guard-- > 0)
	{
		carveDisc(g, x, y, rad);
		const int dx = b.x - x, dy = b.y - y;
		bool horiz;
		if (dx == 0) { horiz = false; }
		else if (dy == 0) { horiz = true; }
		else if (std::abs(dx) > std::abs(dy)) { horiz = r.range(10) < 8; }
		else { horiz = r.range(10) < 2; }
		if (horiz) { x += sgn(dx); } else { y += sgn(dy); }
		if (r.range(9) == 0)   // Schlenker
		{
			const int s = r.range(2) ? 1 : -1;
			if (horiz) { y = std::clamp(y + s, MARGIN, g.H - MARGIN - 1); } else { x = std::clamp(x + s, MARGIN, g.W - MARGIN - 1); }
		}
	}
	carveDisc(g, b.x, b.y, rad);
}

// MF-treu: der Untergrund beginnt als massiver Fels. Gaenge entstehen nur durch Bohren; eine kleine Kammer legt der Schacht frei
// (openSite). Der fruehere Zufalls-Hoehlengenerator ist entfernt (Nutzer 13.11.: "es soll MF treu sein").
void genUndergroundGrid(Grid &, Rng &)
{
}

// ---------------------------------------------------------------------------------------------------------------------
// Originale MF-Inseln (mf/islands.txt, Texturen texpages/modisland-*/): Form + Kachelnummer je Feld
// ---------------------------------------------------------------------------------------------------------------------

struct IslandShape
{
	int w = 0;
	int h = 0;
	std::vector<int16_t> t;   // w*h, -1 = Leere
};

const std::vector<IslandShape> &islandLibrary()
{
	static std::vector<IslandShape> lib;
	static bool tried = false;
	if (tried) { return lib; }
	tried = true;
	char *buf = nullptr;
	UDWORD size = 0;
	if (!loadFile("mf/islands.txt", &buf, &size, false) || buf == nullptr) { return lib; }
	const char *p = buf;
	const char *end = buf + size;
	auto readInt = [&](int &v) -> bool {
		while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) { ++p; }
		if (p >= end) { return false; }
		char *e = nullptr;
		const long x = strtol(p, &e, 10);
		if (e == p) { return false; }
		v = static_cast<int>(x);
		p = e;
		return true;
	};
	int n = 0;
	if (readInt(n) && n > 0 && n < 5000)
	{
		for (int i = 0; i < n; ++i)
		{
			IslandShape s;
			if (!readInt(s.w) || !readInt(s.h) || s.w <= 0 || s.h <= 0 || s.w > 512 || s.h > 512) { lib.clear(); break; }
			s.t.resize(static_cast<size_t>(s.w) * static_cast<size_t>(s.h));
			bool ok = true;
			for (size_t k = 0; k < s.t.size() && ok; ++k)
			{
				int v = 0;
				ok = readInt(v);
				s.t[k] = static_cast<int16_t>(v);
			}
			if (!ok) { lib.clear(); break; }
			lib.push_back(std::move(s));
		}
	}
	free(buf);
	return lib;
}

void genOrbitGridRandom(Grid &g, Rng &r);

// Setzt zufaellig ausgewaehlte Original-Inseln auf die Karte (ohne Ueberlappung, mit Abstand). false = Bibliothek fehlt/passt nicht.
bool genOrbitGridMF(Grid &g, Rng &r)
{
	const std::vector<IslandShape> &lib = islandLibrary();
	if (lib.empty() || modExtTileCount < 2) { return false; }
	const int W = g.W, H = g.H;
	const int maxW = W - 2 * MARGIN - 2, maxH = H - 2 * MARGIN - 2;
	const long mapArea = static_cast<long>(W) * H;
	std::vector<int> fit;
	for (size_t i = 0; i < lib.size(); ++i)
	{
		if (lib[i].w <= maxW && lib[i].h <= maxH && static_cast<long>(lib[i].w) * lib[i].h * 4 <= mapArea * 3) { fit.push_back(static_cast<int>(i)); }
	}
	if (fit.empty()) { return false; }
	g.tid.assign(static_cast<size_t>(W) * static_cast<size_t>(H), -1);
	std::vector<uint8_t> occ(static_cast<size_t>(W) * static_cast<size_t>(H), 0);
	const long wanted = mapArea * 22 / 100;   // etwa ein Fuenftel der Karte soll Inselflaeche sein
	long placed = 0;
	int count = 0;
	for (int tries = 0; tries < 600 && (placed < wanted || count < 3); ++tries)
	{
		const IslandShape &s = lib[static_cast<size_t>(fit[static_cast<size_t>(r.range(static_cast<int>(fit.size())))])];
		const int x0 = MARGIN + 1 + r.range(std::max(1, maxW - s.w + 1));
		const int y0 = MARGIN + 1 + r.range(std::max(1, maxH - s.h + 1));
		bool free = true;
		for (int y = std::max(0, y0 - 2); y < std::min(H, y0 + s.h + 2) && free; ++y)
		{
			for (int x = std::max(0, x0 - 2); x < std::min(W, x0 + s.w + 2); ++x)
			{
				if (occ[static_cast<size_t>(x) + static_cast<size_t>(y) * W]) { free = false; break; }
			}
		}
		if (!free) { continue; }
		for (int y = 0; y < s.h; ++y)
		{
			for (int x = 0; x < s.w; ++x)
			{
				occ[static_cast<size_t>(x0 + x) + static_cast<size_t>(y0 + y) * W] = 1;
				const int16_t v = s.t[static_cast<size_t>(x) + static_cast<size_t>(y) * s.w];
				if (v < 0) { continue; }
				g.set(x0 + x, y0 + y);
				g.tid[static_cast<size_t>(x0 + x) + static_cast<size_t>(y0 + y) * W] = v;
				++placed;
			}
		}
		++count;
	}
	return count > 0;
}

void genOrbitGrid(Grid &g, Rng &r)
{
	if (!genOrbitGridMF(g, r)) { genOrbitGridRandom(g, r); }
}

// Notloesung ohne Bibliothek: Zufallswachstum ab einem Mittelpunkt (nur Ganzzahlen)
void genOrbitGridRandom(Grid &g, Rng &r)
{
	const int W = g.W, H = g.H;
	const int nIs = std::max(3, (W * H) / 1800);
	for (int i = 0; i < nIs; ++i)
	{
		const int rad = 5 + r.range(6) + ((W * H > 40000) ? 3 : 0);
		const int cx = MARGIN + rad + 1 + r.range(std::max(1, W - 2 * (MARGIN + rad + 1)));
		const int cy = MARGIN + rad + 1 + r.range(std::max(1, H - 2 * (MARGIN + rad + 1)));
		const int target = (rad * rad * 3);
		std::vector<Pt> frontier;
		frontier.push_back({ cx, cy });
		g.set(cx, cy);
		int area = 1;
		int guard = target * 40;
		while (area < target && !frontier.empty() && guard-- > 0)
		{
			const size_t idx = static_cast<size_t>(r.range(static_cast<int>(frontier.size())));
			const Pt p = frontier[idx];
			static const int DX[4] = { 1, -1, 0, 0 };
			static const int DY[4] = { 0, 0, 1, -1 };
			const int d = r.range(4);
			const int nx = p.x + DX[d], ny = p.y + DY[d];
			if (nx < MARGIN || ny < MARGIN || nx >= W - MARGIN || ny >= H - MARGIN) { continue; }
			if (std::abs(nx - cx) > rad + 3 || std::abs(ny - cy) > rad + 3) { continue; }
			if (g.at(nx, ny)) { continue; }
			g.set(nx, ny);
			frontier.push_back({ nx, ny });
			++area;
		}
	}
}

// Kacheln fuer die Ebene fuellen
std::unique_ptr<MAPTILE[]> makeTiles(Layer layer, const Grid &g, const Palette &pal, uint32_t salt)
{
	const int W = g.W, H = g.H;
	auto tiles = std::make_unique<MAPTILE[]>(static_cast<size_t>(W) * static_cast<size_t>(H));
	for (int y = 0; y < H; ++y)
	{
		for (int x = 0; x < W; ++x)
		{
			MAPTILE &t = tiles[static_cast<size_t>(x) + static_cast<size_t>(y) * W];
			const bool isOpenTile = g.at(x, y);
			// Eckpunkt liegt auf Bodenhoehe, wenn irgendeines der vier angrenzenden Felder offen ist; so bleiben offene Felder eben
			// und die Neigung liegt ganz auf den Felswand-Kacheln.
			const bool low = g.at(x, y) || g.at(x - 1, y) || g.at(x, y - 1) || g.at(x - 1, y - 1);
			const uint32_t hv = hash2(x, y, salt);
			if (layer == modlayer::UNDERGROUND)
			{
				t.height = low ? static_cast<int32_t>(hv % 24u) : static_cast<int32_t>(330u + (hv >> 8) % 110u);
			}
			else
			{
				t.height = low ? static_cast<int32_t>(1040u + hv % 24u) : 0;
			}
			const std::vector<uint16_t> &src = isOpenTile ? pal.floor : pal.rock;
			t.texture = src[(hv >> 16) % src.size()];
			if (layer == modlayer::ORBIT && !g.tid.empty())
			{
				const int16_t v = g.tid[static_cast<size_t>(x) + static_cast<size_t>(y) * W];
				t.texture = static_cast<uint16_t>(v >= 0 ? MOD_TILE_ISLAND0 + v : MOD_TILE_VOID);
			}
			t.psObject = nullptr;
			if (layer == modlayer::ORBIT)
			{
				t.tileExploredBits = static_cast<PlayerMask>(~static_cast<PlayerMask>(0));   // kein Nebel im Orbit
				t.sensorBits = static_cast<PlayerMask>(~static_cast<PlayerMask>(0));
			}
		}
	}
	return tiles;
}

bool buildLayer(Layer layer, const Palette &pal, uint32_t seed)
{
	const int W = s_w, H = s_h;
	Rng r(seed ^ (layer == modlayer::UNDERGROUND ? 0xA5A5A5A5u : 0x5A5A5A5Au));
	Grid g(W, H);
	if (layer == modlayer::UNDERGROUND) { genUndergroundGrid(g, r); } else { genOrbitGrid(g, r); }

	WorldMapState tmp;
	tmp.width = W;
	tmp.height = H;
	tmp.tiles = makeTiles(layer, g, pal, seed ^ (static_cast<uint32_t>(layer) * 977u));
	if (!mapSetupLayerWorld(tmp))
	{
		return false;
	}
	Slot &s = s_slot[layer];
	s.tiles = std::move(tmp.tiles);
	for (size_t i = 0; i < s.blockMap.size(); ++i) { s.blockMap[i] = std::move(tmp.blockMap[i]); }
	for (size_t i = 0; i < s.auxMap.size(); ++i) { s.auxMap[i] = std::move(tmp.auxMap[i]); }
	s.scroll = tmp.scroll;
	return true;
}

void freeSlots()
{
	for (int i = 0; i < NL; ++i) { s_slot[i] = Slot(); }
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Oeffentliche Schnittstelle
// ---------------------------------------------------------------------------------------------------------------------

bool worldsExist()
{
	return s_built;
}

Layer resident()
{
	return s_resident;
}

Layer viewLayer()
{
	return s_view;
}

void bring(Layer l)
{
	if (!s_built || l == s_resident) { return; }
	if (s_resident == modlayer::SURFACE)
	{
		// Der Gefahrenkarten-Thread liest gameWorld.map.auxMap: vor dem Wegschalten der Oberflaeche anhalten
		s_parked = mapDangerSerializeBegin();
	}
	Slot tmp;
	takeFromWorld(tmp);
	putIntoWorld(s_slot[l]);
	s_slot[s_resident] = std::move(tmp);
	s_resident = l;
	if (l == modlayer::SURFACE && s_parked)
	{
		mapDangerSerializeEnd(true);
		s_parked = false;
	}
}

Scope::Scope(Layer l) : prev_(s_resident), active_(false)
{
	if (!s_built || l == s_resident) { return; }
	bring(l);
	active_ = true;
	++s_scopeDepth;
}

Scope::~Scope()
{
	if (!active_) { return; }
	--s_scopeDepth;
	bring(prev_);
}


void placeHomes();

bool ensureWorlds()
{
	if (s_built)
	{
		if (!s_homesDone) { placeHomes(); }
		return true;
	}
	if (s_failed || !modcombot::ready()) { return false; }
	if (gameWorld.map.tiles == nullptr || gameWorld.map.width < 24 || gameWorld.map.height < 24) { return false; }
	if (s_resident != modlayer::SURFACE) { return false; }

	s_w = gameWorld.map.width;
	s_h = gameWorld.map.height;
	s_mapName = game.map;

	Palette pal;
	if (!pickPalette(pal))
	{
		debug(LOG_ERROR, "modlayermap: keine Kacheln fuer Fels/Boden im Tileset gefunden, Ebenen ohne eigene Karte");
		s_failed = true;
		return false;
	}
	// gleicher Kartenname + Groesse -> gleiche Hoehlen auf jedem Rechner und bei jedem Laden
	const uint32_t seed = hashStr(s_mapName) ^ (static_cast<uint32_t>(s_w) * 7919u) ^ (static_cast<uint32_t>(s_h) * 104729u);
	s_seed = seed;

	if (!buildLayer(modlayer::UNDERGROUND, pal, seed) || !buildLayer(modlayer::ORBIT, pal, seed))
	{
		debug(LOG_ERROR, "modlayermap: Zusatzkarten konnten nicht erzeugt werden");
		freeSlots();
		s_failed = true;
		return false;
	}
	s_built = true;

	// Beleuchtung der neuen Karten (liest die aktive Karte, darum kurz hineinschalten)
	for (const Layer l : { modlayer::UNDERGROUND, modlayer::ORBIT })
	{
		Scope sc(l);
		initLighting(gameWorld.map, 0, 0, gameWorld.map.width, gameWorld.map.height);
		preProcessVisibility(gameWorld.map);
	}
	debug(LOG_INFO, "modlayermap: Untergrund- und Orbit-Karte erzeugt (%d x %d, Startwert %08X)", s_w, s_h, seed);
	return true;
}

// Unter der Basis jedes Spielers liegt eine Kammer (erkundet), damit die Untergrund-Ansicht nicht schwarz beginnt.
// Erst wenn die Startbauwerke stehen (Skripte setzen sie in den ersten Spielschritten).
void placeHomes()
{
	if (s_homesDone || gameTime < 600) { return; }
	s_homesDone = true;
	s_deferRefresh = true;
	s_pendingRefresh = false;
	for (int p = 0; p < MAX_PLAYERS; ++p)
	{
		Vector2i at(-1, -1);
		if (!gameWorld.objects.structures[p].empty()) { at = Vector2i(map_coord(gameWorld.objects.structures[p].front()->pos.x), map_coord(gameWorld.objects.structures[p].front()->pos.y)); }
		else if (!gameWorld.objects.droids[p].empty()) { at = Vector2i(map_coord(gameWorld.objects.droids[p].front()->pos.x), map_coord(gameWorld.objects.droids[p].front()->pos.y)); }
		(void)at;   // MF: unter der Basis gibt es keine Kammer; nur der Schacht legt eine frei
	}
	s_deferRefresh = false;
	if (s_pendingRefresh)
	{
		s_pendingRefresh = false;
		Scope sc(modlayer::UNDERGROUND);
		mapSetupLayerWorld(gameWorld.map);
		initLighting(gameWorld.map, 0, 0, gameWorld.map.width, gameWorld.map.height);
	}
}

void setView(Layer l)
{
	if (!s_built)
	{
		s_view = modlayer::SURFACE;
		return;
	}
	if (s_scopeDepth > 0) { return; }   // nicht innerhalb der Spielschleife umbauen
	if (l == s_view && l == s_resident) { return; }
	s_view = l;
	bring(l);
	rebuildDisplay();
}

void tick()
{
	if (s_built && s_dirtyView && s_scopeDepth == 0) { rebuildDisplay(); }
}

int32_t heightAt(Layer l, int worldX, int worldY)
{
	if (!s_built || l == s_resident) { return map_Height(gameWorld.map, worldX, worldY); }
	// fremde Karte kurz hineinschalten
	Scope sc(l);
	return map_Height(gameWorld.map, worldX, worldY);
}

bool isSolid(Layer l, int tileX, int tileY)
{
	if (!s_built) { return false; }
	Scope sc(l);
	if (!tileOnMap(gameWorld.map, tileX, tileY)) { return true; }
	return terrainType(mapTile(gameWorld.map, tileX, tileY)) == TER_CLIFFFACE;
}

void openSite(int tileX, int tileY, int player, int rad, int revealRad)
{
	if (!s_built) { return; }
	Scope sc(modlayer::UNDERGROUND);
	WorldMapState &m = gameWorld.map;
	const int W = m.width, H = m.height;
	if (tileX < MARGIN + 2 || tileY < MARGIN + 2 || tileX >= W - MARGIN - 2 || tileY >= H - MARGIN - 2) { return; }

	Palette pal;
	if (!pickPalette(pal)) { return; }

	auto isOpen = [&](int x, int y) {
		return tileOnMap(m, x, y) && terrainType(mapTile(m, x, y)) != TER_CLIFFFACE;
	};

	// Kammer rund um den Punkt
	std::vector<Pt> carved;
	for (int dy = -rad; dy <= rad; ++dy)
	{
		for (int dx = -rad; dx <= rad; ++dx)
		{
			// MF: die Aufzugskammer ist quadratisch (5x5 Bloecke zu je 2x2 Whixel um den Aufzug); kleine Kammern daher als volles Quadrat
			if (rad > 1 && dx * dx + dy * dy > rad * rad + rad / 2) { continue; }
			carved.push_back({ tileX + dx, tileY + dy });
		}
	}
	// naechstes schon offenes Feld ausserhalb der Kammer suchen und per Gang anbinden
	Pt best{ -1, -1 };
	long bestD = -1;
	bool alreadyConnected = false;
	for (int y = MARGIN; y < H - MARGIN; ++y)
	{
		for (int x = MARGIN; x < W - MARGIN; ++x)
		{
			if (!isOpen(x, y)) { continue; }
			const long dx = x - tileX, dy = y - tileY;
			const long d = dx * dx + dy * dy;
			if (d <= static_cast<long>(rad * rad + rad / 2)) { alreadyConnected = true; continue; }
			if (bestD < 0 || d < bestD) { bestD = d; best = { x, y }; }
		}
	}

	Grid g(W, H);
	for (int y = 0; y < H; ++y) { for (int x = 0; x < W; ++x) { if (isOpen(x, y)) { g.set(x, y); } } }
	for (const Pt &p : carved) { if (p.x >= MARGIN && p.y >= MARGIN && p.x < W - MARGIN && p.y < H - MARGIN) { g.set(p.x, p.y); } }
	// MF-treu: Gaenge entstehen nur durch Bohren (Bohrfahrzeug), nie von selbst. Die fruehere automatische Anbindung bleibt abschaltbar erhalten.
	constexpr bool kAutoConnect = false;
	if (kAutoConnect && !alreadyConnected && best.x >= 0)
	{
		Rng r(s_seed ^ static_cast<uint32_t>(tileX * 31 + tileY * 17));
		carveTunnel(g, r, { tileX, tileY }, best, 1);
	}

	// Kacheln neu setzen (Hoehen nach der Eckpunkt-Regel)
	const uint32_t salt = s_seed ^ (static_cast<uint32_t>(modlayer::UNDERGROUND) * 977u);
	auto fresh = makeTiles(modlayer::UNDERGROUND, g, pal, salt);
	// nur die geaenderten Bereiche uebernehmen (Erkundungsstand u.a. bleibt)
	for (int y = 0; y < H; ++y)
	{
		for (int x = 0; x < W; ++x)
		{
			MAPTILE &dst = m.tiles[static_cast<size_t>(x) + static_cast<size_t>(y) * W];
			const MAPTILE &src = fresh[static_cast<size_t>(x) + static_cast<size_t>(y) * W];
			if (dst.texture != src.texture || dst.height != src.height)
			{
				dst.texture = src.texture;
				dst.height = src.height;
			}
		}
	}
	// Bodentypen, Sperrkarten und Kontinente neu berechnen (Wegfindung braucht die Kontinente). Der Gefahrenkarten-Thread ist angehalten.
	if (!s_deferRefresh) { mapSetupLayerWorld(m); } else { s_pendingRefresh = true; }
	// Schleier lichten: das Gelaende rund um den Punkt gilt fuer den Spieler (und Verbuendete) als erkundet
	if (player >= 0 && player < MAX_PLAYER_SLOTS && revealRad > 0)
	{
		for (int dy = -revealRad; dy <= revealRad; ++dy)
		{
			for (int dx = -revealRad; dx <= revealRad; ++dx)
			{
				if (dx * dx + dy * dy > revealRad * revealRad) { continue; }
				const int x = tileX + dx, y = tileY + dy;
				if (!tileOnMap(m, x, y)) { continue; }
				SET_TILE_VISIBLE(player, mapTile(m, x, y));
			}
		}
	}
	if (!s_deferRefresh) { initLighting(m, 0, 0, static_cast<UDWORD>(W), static_cast<UDWORD>(H)); }
	if (s_view == modlayer::UNDERGROUND) { s_dirtyView = true; }
}

bool drillTile(int tileX, int tileY, int player, int revealRad)
{
	if (!s_built) { return false; }
	Scope sc(modlayer::UNDERGROUND);
	WorldMapState &m = gameWorld.map;
	const int W = m.width, H = m.height;
	if (tileX < MARGIN || tileY < MARGIN || tileX >= W - MARGIN || tileY >= H - MARGIN) { return false; }
	auto isOpen = [&](int x, int y) {
		return tileOnMap(m, x, y) && terrainType(mapTile(m, x, y)) != TER_CLIFFFACE;
	};
	if (isOpen(tileX, tileY)) { return false; }
	Palette pal;
	if (!pickPalette(pal)) { return false; }
	const uint32_t salt = s_seed ^ (static_cast<uint32_t>(modlayer::UNDERGROUND) * 977u);
	// das Feld selbst wird Boden
	{
		MAPTILE &t = m.tiles[static_cast<size_t>(tileX) + static_cast<size_t>(tileY) * W];
		const uint32_t hv = hash2(tileX, tileY, salt);
		t.texture = pal.floor[(hv >> 16) % pal.floor.size()];
	}
	// Eckpunkte: tief, wenn irgendeines der vier angrenzenden Felder offen ist (gleiche Regel wie makeTiles)
	for (int cy = tileY; cy <= tileY + 1; ++cy)
	{
		for (int cx = tileX; cx <= tileX + 1; ++cx)
		{
			if (!tileOnMap(m, cx, cy)) { continue; }
			const bool low = isOpen(cx, cy) || isOpen(cx - 1, cy) || isOpen(cx, cy - 1) || isOpen(cx - 1, cy - 1);
			const uint32_t hv = hash2(cx, cy, salt);
			mapTile(m, cx, cy)->height = low ? static_cast<int32_t>(hv % 24u) : static_cast<int32_t>(330u + (hv >> 8) % 110u);
		}
	}
	mapSetupLayerWorld(m);
	if (player >= 0 && player < MAX_PLAYER_SLOTS && revealRad > 0)
	{
		for (int dy = -revealRad; dy <= revealRad; ++dy)
		{
			for (int dx = -revealRad; dx <= revealRad; ++dx)
			{
				if (dx * dx + dy * dy > revealRad * revealRad) { continue; }
				const int x = tileX + dx, y = tileY + dy;
				if (tileOnMap(m, x, y)) { SET_TILE_VISIBLE(player, mapTile(m, x, y)); }
			}
		}
	}
	initLighting(m, static_cast<UDWORD>(std::max(0, tileX - 3)), static_cast<UDWORD>(std::max(0, tileY - 3)),
	             static_cast<UDWORD>(std::min(W, tileX + 5)), static_cast<UDWORD>(std::min(H, tileY + 5)));
	if (s_view == modlayer::UNDERGROUND) { s_dirtyView = true; }
	return true;
}

void reset()
{
	if (s_built && s_resident != modlayer::SURFACE)
	{
		s_scopeDepth = 0;
		bring(modlayer::SURFACE);
	}
	if (s_parked)
	{
		mapDangerSerializeEnd(true);
		s_parked = false;
	}
	freeSlots();
	s_built = false;
	s_failed = false;
	s_resident = modlayer::SURFACE;
	s_view = modlayer::SURFACE;
	s_dirtyView = false;
	s_scopeDepth = 0;
	s_w = s_h = 0;
	s_mapName.clear();
	s_seed = 0;
	s_deferRefresh = false;
	s_pendingRefresh = false;
	s_homesDone = false;
}

}
