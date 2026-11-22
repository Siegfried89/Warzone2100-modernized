// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Tunnel wie die Nydus-Kanaele (siehe modtunnel.h)

#include "modtunnel.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"

#include "action.h"
#include "ai.h"
#include "console.h"
#include "droid.h"
#include "effects.h"
#include "game_world.h"
#include "map.h"
#include "modlayer.h"
#include "modlayerdig.h"
#include "modlayermap.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "orderdef.h"
#include "power.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace modtunnel
{
namespace
{
constexpr unsigned DIG_MS_PER_TILE = 600;       // Bohrgeschwindigkeit (je Plan-Kachel = Original-Feld)
constexpr unsigned TRANSIT_MS_PER_TILE = 230;   // so schnell rasen Einheiten durch den fertigen Gang
constexpr int MIN_LEN = DIST_TILE * 4;          // kuerzer lohnt kein Tunnel
constexpr int ORDER_R = DIST_TILE * 13 / 10;    // Bewegungsziel so nah am Mund -> Einheit will hinein
constexpr int REACH = DIST_TILE * 3 / 2;        // so nah muss sie am Mund sein
constexpr unsigned EXIT_COOL_MS = 2500;

struct Tunnel
{
	unsigned player = 0;
	uint32_t a = 0, b = 0;           // Muender (Bauwerks-Kennungen); a = Eingang (dort begann die Grabung)
	Vector2i pa{0, 0}, pb{0, 0};     // Lage der Muender (Welt)
	unsigned emerged = 0;            // Zaehler fuer die Austrittsplaetze (verteilt die Einheiten um den Mund)
};
struct Dig
{
	unsigned player = 0;
	uint32_t entrance = 0;
	Vector2i from{0, 0}, to{0, 0};   // Welt
	unsigned start = 0;
	Vector2i lastTile{-1, -1};
	unsigned nextDust = 0;
	int cost = 0;
};
struct Transit
{
	uint32_t tunnel = 0;
	bool aToB = true;
	unsigned start = 0;
	std::vector<DroidOrder> pending;   // Befehle, die unterwegs kamen (nach dem Austritt ausgefuehrt)
};

std::map<uint32_t, Tunnel> s_tunnels;    // Schluessel = Kennung des Eingangs
std::map<uint32_t, Dig> s_digs;          // Schluessel = Bohrfahrzeug
std::map<uint32_t, Transit> s_transit;   // Schluessel = Einheit
std::map<uint32_t, unsigned> s_cool;     // Einheit -> frueheste Zeit fuer den naechsten Eintritt

int64_t d2(const Vector2i &a, const Vector2i &b)
{
	const int64_t dx = a.x - b.x, dy = a.y - b.y;
	return dx * dx + dy * dy;
}

STRUCTURE_STATS *mouthStats()
{
	for (unsigned i = 0; i < numStructureStats; ++i)
	{
		if (asStructureStats[i].id == "A0TunnelMouth") { return &asStructureStats[i]; }
	}
	return nullptr;
}

bool freeTile(int tx, int ty)
{
	if (tx < 2 || ty < 2 || tx >= gameWorld.map.width - 2 || ty >= gameWorld.map.height - 2) { return false; }
	const MAPTILE *t = mapTile(gameWorld.map, tx, ty);
	return t != nullptr && !TileIsOccupied(t) && terrainType(t) != TER_WATER && terrainType(t) != TER_CLIFFFACE;
}

/// naechstes freies Feld (Ringe um das Feld, feste Reihenfolge)
bool findFree(const Vector2i &tile, int maxR, Vector2i &out, int skip = 0)
{
	for (int r = 0; r <= maxR; ++r)
	{
		for (int dy = -r; dy <= r; ++dy)
		{
			for (int dx = -r; dx <= r; ++dx)
			{
				if (std::max(std::abs(dx), std::abs(dy)) != r) { continue; }
				if (!freeTile(tile.x + dx, tile.y + dy)) { continue; }
				if (skip-- > 0) { continue; }
				out = Vector2i(tile.x + dx, tile.y + dy);
				return true;
			}
		}
	}
	return false;
}

Vector2i tileCenter(const Vector2i &t) { return Vector2i(world_coord(t.x) + TILE_UNITS / 2, world_coord(t.y) + TILE_UNITS / 2); }
Vector2i tileOf(const Vector2i &w) { return Vector2i(map_coord(w.x), map_coord(w.y)); }

STRUCTURE *spawnMouth(unsigned player, const Vector2i &tile, uint16_t dir)
{
	STRUCTURE_STATS *st = mouthStats();
	if (st == nullptr) { return nullptr; }
	const Vector2i c = tileCenter(tile);
	STRUCTURE *s = buildStructureDir(gameWorld, st, static_cast<UDWORD>(c.x), static_cast<UDWORD>(c.y), dir, player, false, generateSynchronisedObjectId(), false, true, true);
	if (s == nullptr) { return nullptr; }
	s->status = SS_BUILT;
	buildingComplete(s, gameWorld);
	return s;
}

STRUCTURE *aliveStruct(uint32_t id, unsigned player)
{
	STRUCTURE *s = IdToStruct(id, player);
	return (s != nullptr && !s->died) ? s : nullptr;
}

void dust(const Vector2i &w, int n)
{
	if (!worldOnMap(gameWorld.map, w.x, w.y)) { return; }
	const unsigned it = gameTime - deltaGameTime / 2 + 1;
	for (int i = 0; i < n; ++i)
	{
		Vector3i p(w.x + (i - n / 2) * 22, map_Height(gameWorld.map, w.x, w.y) + 18 + i * 10, w.y + ((i * 37) % 3 - 1) * 18);
		addEffect(&p, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING, false, nullptr, 0, it);
	}
}

/// Einheit im Untergrund an eine Stelle setzen (ohne Wegfindung)
void placeUnder(DROID *d, const Vector2i &w, const Vector2i &dir)
{
	d->pos.x = std::clamp(w.x, TILE_UNITS, world_coord(gameWorld.map.width - 1) - 1);
	d->pos.y = std::clamp(w.y, TILE_UNITS, world_coord(gameWorld.map.height - 1) - 1);
	d->pos.z = modlayermap::worldsExist() ? modlayermap::heightAt(modlayer::UNDERGROUND, d->pos.x, d->pos.y) : map_Height(gameWorld.map, d->pos.x, d->pos.y);
	if (dir.x != 0 || dir.y != 0) { d->rot.direction = iAtan2(dir.x, dir.y); }
}

void say(unsigned player, const char *msg)
{
	if (player == selectedPlayer) { addConsoleMessage(msg, DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
}

bool startDig(DROID *d, const DroidOrder *o)
{
	STRUCTURE_STATS *st = mouthStats();
	if (st == nullptr || !modlayermap::worldsExist()) { say(d->player, _("Tunnel: not possible on this map")); return true; }
	const Vector2i target(o->pos.x, o->pos.y);
	if (!worldOnMap(gameWorld.map, target.x, target.y)) { return true; }
	if (d2(target, d->pos.xy()) < static_cast<int64_t>(MIN_LEN) * MIN_LEN)
	{
		say(d->player, _("Tunnel: the exit must be at least 4 tiles away from the drill rig"));
		return true;
	}
	Vector2i entTile;
	if (!findFree(tileOf(d->pos.xy()), 3, entTile)) { say(d->player, _("Tunnel: no free ground for the entrance here")); return true; }
	const int cost = static_cast<int>(st->powerToBuild);
	if (cost > 0 && getPower(d->player) < cost) { say(d->player, _("Tunnel: not enough power")); return true; }
	if (cost > 0) { usePower(d->player, static_cast<uint32_t>(cost)); }
	const Vector2i dirv = target - d->pos.xy();
	STRUCTURE *ent = spawnMouth(d->player, entTile, static_cast<uint16_t>(iAtan2(dirv.x, dirv.y)));
	if (ent == nullptr) { if (cost > 0) { addPower(d->player, cost); } return true; }
	Dig g;
	g.player = d->player;
	g.entrance = ent->id;
	g.from = ent->pos.xy();
	g.to = target;
	g.start = gameTime;
	g.cost = cost;
	s_digs[d->id] = g;
	// der Bohrer taucht am Eingang ab
	actionDroid(d, DACTION_NONE);
	d->order = DroidOrder(DORDER_HOLD);   // (HOLD: keine automatischen Wach-Befehle, solange er graebt)
	modlayer::moveToLayer(d, modlayer::UNDERGROUND);
	placeUnder(d, g.from, dirv);
	initDroidMovement(d);
	dust(g.from, 4);
	say(d->player, _("Tunnel: the drill rig is digging towards the exit"));
	return true;
}

void endDigAbort(DROID *d, Dig &g, bool refund)
{
	// zurueck an die Oberflaeche beim Eingang (der Eingang verschwindet)
	if (d != nullptr)
	{
		Vector2i t;
		if (findFree(tileOf(g.from), 4, t))
		{
			const Vector2i w = tileCenter(t);
			d->pos.x = w.x; d->pos.y = w.y;
		}
		modlayer::moveToLayer(d, modlayer::SURFACE);
		droidSetPosition(d, gameWorld.map, d->pos.x, d->pos.y);
	}
	if (STRUCTURE *e = aliveStruct(g.entrance, g.player)) { removeStruct(e, true, gameWorld); }
	if (refund && g.cost > 0) { addPower(static_cast<int>(g.player), g.cost); }
}

void digTick(GameWorld &world)
{
	for (auto it = s_digs.begin(); it != s_digs.end();)
	{
		Dig &g = it->second;
		DROID *d = IdToDroid(world.objects, it->first, g.player);
		STRUCTURE *ent = aliveStruct(g.entrance, g.player);
		if (d == nullptr || d->died)
		{
			// Bohrer verloren: der angefangene Gang stuerzt ein
			if (ent != nullptr) { destroyStruct(ent, gameTime, world); }
			say(g.player, _("Tunnel: the drill rig was lost - the tunnel caved in"));
			it = s_digs.erase(it);
			continue;
		}
		if (ent == nullptr)
		{
			// Eingang zerstoert, waehrend gegraben wurde: der Bohrer steckt im Gang und ist verloren
			destroyDroid(d, gameTime, world);
			say(g.player, _("Tunnel: the entrance was destroyed - the tunnel caved in"));
			it = s_digs.erase(it);
			continue;
		}
		const double dx = g.to.x - g.from.x, dy = g.to.y - g.from.y;
		const double L = std::max(1.0, std::sqrt(dx * dx + dy * dy));
		const double s = std::min(L, static_cast<double>(gameTime - g.start) * DIST_TILE / DIG_MS_PER_TILE);
		const Vector2i cur(g.from.x + static_cast<int>(dx / L * s), g.from.y + static_cast<int>(dy / L * s));
		const Vector2i dirv(static_cast<int>(dx), static_cast<int>(dy));
		actionDroid(d, DACTION_NONE);
		placeUnder(d, cur, dirv);
		// der Gang: jedes neue Feld unter dem Bohrer aufbohren (im Untergrund sichtbar)
		const Vector2i t = tileOf(cur);
		if (t != g.lastTile)
		{
			if (tileOnMap(world.map, t.x, t.y)) { modlayermap::drillTile(t.x, t.y, static_cast<int>(g.player), 2); }
			g.lastTile = t;
		}
		// Staub an der Oberflaeche - nur fuer den eigenen Spieler und Verbuendete (Ueberraschung)
		if (gameTime >= g.nextDust)
		{
			g.nextDust = gameTime + 700;
			if (selectedPlayer < MAX_PLAYERS && aiCheckAlliances(g.player, selectedPlayer)) { dust(cur, 2); }
		}
		if (s < L) { ++it; continue; }
		// Durchbruch: Ausgang an der Zielstelle (oder dem naechsten freien Feld)
		Vector2i exitTile;
		if (!findFree(tileOf(g.to), 4, exitTile))
		{
			say(g.player, _("Tunnel: no free ground at the exit - the drill rig returns"));
			endDigAbort(d, g, true);
			it = s_digs.erase(it);
			continue;
		}
		STRUCTURE *ex = spawnMouth(g.player, exitTile, static_cast<uint16_t>(iAtan2(-dirv.x, -dirv.y)));
		if (ex == nullptr) { endDigAbort(d, g, true); it = s_digs.erase(it); continue; }
		Tunnel tn;
		tn.player = g.player;
		tn.a = g.entrance;
		tn.b = ex->id;
		tn.pa = ent->pos.xy();
		tn.pb = ex->pos.xy();
		s_tunnels[tn.a] = tn;
		// der Bohrer kommt neben dem Ausgang heraus
		Vector2i outT;
		if (findFree(exitTile, 3, outT, 1)) { const Vector2i w = tileCenter(outT); d->pos.x = w.x; d->pos.y = w.y; }
		modlayer::moveToLayer(d, modlayer::SURFACE);
		droidSetPosition(d, world.map, d->pos.x, d->pos.y);
		d->order = DroidOrder(DORDER_NONE);
		dust(ex->pos.xy(), 5);
		say(g.player, _("Tunnel complete - send units onto a tunnel mouth to travel through it"));
		it = s_digs.erase(it);
	}
}

/// Einheiten, die auf einen Mund zufahren, steigen ein
void enterTick(GameWorld &world)
{
	if (s_tunnels.empty()) { return; }
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (DROID *d : world.objects.droids[p])
		{
			if (d == nullptr || d->died) { continue; }
			if (d->order.type != DORDER_MOVE && d->order.type != DORDER_SCOUT) { continue; }
			if (s_transit.count(d->id) || s_digs.count(d->id) || !canUse(d)) { continue; }
			const auto cool = s_cool.find(d->id);
			if (cool != s_cool.end()) { if (gameTime < cool->second) { continue; } s_cool.erase(cool); }
			for (auto &kv : s_tunnels)
			{
				Tunnel &tn = kv.second;
				if (!aiCheckAlliances(tn.player, d->player)) { continue; }
				int end = -1;
				if (d2(d->order.pos, tn.pa) <= static_cast<int64_t>(ORDER_R) * ORDER_R && d2(d->pos.xy(), tn.pa) <= static_cast<int64_t>(REACH) * REACH) { end = 0; }
				else if (d2(d->order.pos, tn.pb) <= static_cast<int64_t>(ORDER_R) * ORDER_R && d2(d->pos.xy(), tn.pb) <= static_cast<int64_t>(REACH) * REACH) { end = 1; }
				if (end < 0) { continue; }
				Transit tr;
				tr.tunnel = kv.first;
				tr.aToB = end == 0;
				tr.start = gameTime;
				actionDroid(d, DACTION_NONE);
				d->order = DroidOrder(DORDER_HOLD);   // im Gang: keine automatischen Wach-Befehle (Shift-Befehle werden trotzdem abgeholt)
				const Vector2i from = tr.aToB ? tn.pa : tn.pb, to = tr.aToB ? tn.pb : tn.pa;
				modlayer::moveToLayer(d, modlayer::UNDERGROUND);
				placeUnder(d, from, to - from);
				initDroidMovement(d);
				s_transit[d->id] = tr;
				if (selectedPlayer < MAX_PLAYERS && aiCheckAlliances(tn.player, selectedPlayer)) { dust(from, 2); }
				break;
			}
		}
	}
}

void transitTick(GameWorld &world)
{
	for (auto it = s_transit.begin(); it != s_transit.end();)
	{
		const uint32_t id = it->first;
		DROID *d = nullptr;
		for (unsigned p = 0; p < MAX_PLAYERS && d == nullptr; ++p) { d = IdToDroid(world.objects, id, p); }
		const auto tit = s_tunnels.find(it->second.tunnel);
		if (d == nullptr || d->died || tit == s_tunnels.end()) { it = s_transit.erase(it); continue; }
		Tunnel &tn = tit->second;
		const Vector2i from = it->second.aToB ? tn.pa : tn.pb, to = it->second.aToB ? tn.pb : tn.pa;
		const double dx = to.x - from.x, dy = to.y - from.y;
		const double L = std::max(1.0, std::sqrt(dx * dx + dy * dy));
		const double s = std::min(L, static_cast<double>(gameTime - it->second.start) * DIST_TILE / TRANSIT_MS_PER_TILE);
		if (s < L)
		{
			if (d->action != DACTION_NONE) { actionDroid(d, DACTION_NONE); }
			placeUnder(d, Vector2i(from.x + static_cast<int>(dx / L * s), from.y + static_cast<int>(dy / L * s)), to - from);
			++it;
			continue;
		}
		// Austritt: neben dem Mund an die Oberflaeche (Plaetze reihum verteilt)
		Vector2i outT;
		if (findFree(tileOf(to), 4, outT, static_cast<int>(tn.emerged % 9u)) || findFree(tileOf(to), 5, outT))
		{
			const Vector2i w = tileCenter(outT);
			d->pos.x = w.x; d->pos.y = w.y;
		}
		++tn.emerged;
		std::vector<DroidOrder> pending = std::move(it->second.pending);
		it = s_transit.erase(it);
		modlayer::moveToLayer(d, modlayer::SURFACE);
		droidSetPosition(d, world.map, d->pos.x, d->pos.y);
		d->order = DroidOrder(DORDER_NONE);
		s_cool[d->id] = gameTime + EXIT_COOL_MS;
		if (selectedPlayer < MAX_PLAYERS && aiCheckAlliances(tn.player, selectedPlayer)) { dust(to, 2); }
		if (!pending.empty()) { orderDroidBase(d, &pending.front()); }
	}
}

/// zerstoerter Mund: der Tunnel stuerzt ein, alle Einheiten darin sind verloren, der andere Mund faellt in sich zusammen
void collapseTick(GameWorld &world)
{
	for (auto it = s_tunnels.begin(); it != s_tunnels.end();)
	{
		Tunnel &tn = it->second;
		STRUCTURE *a = aliveStruct(tn.a, tn.player);
		STRUCTURE *b = aliveStruct(tn.b, tn.player);
		if (a != nullptr && b != nullptr) { ++it; continue; }
		unsigned lost = 0;
		for (auto tr = s_transit.begin(); tr != s_transit.end();)
		{
			if (tr->second.tunnel != it->first) { ++tr; continue; }
			DROID *d = nullptr;
			for (unsigned p = 0; p < MAX_PLAYERS && d == nullptr; ++p) { d = IdToDroid(world.objects, tr->first, p); }
			tr = s_transit.erase(tr);
			if (d != nullptr && !d->died) { destroyDroid(d, gameTime, world); ++lost; }
		}
		if (a != nullptr) { destroyStruct(a, gameTime, world); }
		if (b != nullptr) { destroyStruct(b, gameTime, world); }
		char msg[160];
		if (lost > 0) { snprintf(msg, sizeof(msg), _("Tunnel destroyed - it caved in, %u units inside were lost"), lost); }
		else { snprintf(msg, sizeof(msg), "%s", _("Tunnel destroyed - it caved in")); }
		say(tn.player, msg);
		it = s_tunnels.erase(it);
	}
}

// ---------------------------------------------------------------- Anzeige
bool project(const Vector2i &w, const glm::mat4 &m, glm::vec2 &out)
{
	const int wx = std::clamp(w.x, 0, world_coord(gameWorld.map.width) - 1);
	const int wy = std::clamp(w.y, 0, world_coord(gameWorld.map.height) - 1);
	const Vector3i pos(wx, map_Height(gameWorld.map, wx, wy) + 8, -wy);
	Vector2i s(0, 0);
	if (pie_RotateProjectWithPerspective(&pos, m, &s) <= 0) { return false; }
	out = glm::vec2(static_cast<float>(s.x), static_cast<float>(s.y));
	return true;
}

void band(const Vector2i &A, const Vector2i &B, const glm::mat4 &m, bool dashed, int alpha)
{
	const glm::vec2 a(A.x, A.y), b(B.x, B.y);
	const float L = glm::length(b - a);
	if (L < 1.f) { return; }
	const glm::vec2 dir = (b - a) / L, perp(-dir.y, dir.x);
	const float step = DIST_TILE * 0.6f, dash = dashed ? DIST_TILE * 0.34f : step;
	const PIELIGHT glow = pal_RGBA(90, 60, 25, static_cast<UBYTE>(alpha * 0.35f));
	const PIELIGHT core = pal_RGBA(175, 130, 70, static_cast<UBYTE>(alpha));
	for (float s = 0.f; s < L; s += step)
	{
		const float e = std::min(L, s + dash);
		const glm::vec2 p0 = a + dir * s, p1 = a + dir * e, pw = p0 + perp * (DIST_TILE * 0.22f);
		glm::vec2 s0, s1, sw;
		if (!project(Vector2i(static_cast<int>(p0.x), static_cast<int>(p0.y)), m, s0) || !project(Vector2i(static_cast<int>(p1.x), static_cast<int>(p1.y)), m, s1)
		    || !project(Vector2i(static_cast<int>(pw.x), static_cast<int>(pw.y)), m, sw)) { continue; }
		const float w = std::clamp(glm::length(sw - s0) * 2.f, 2.f, 28.f);
		const glm::vec2 c = (s0 + s1) * 0.5f, dd = s1 - s0;
		const float ll = glm::length(dd);
		if (ll < 0.5f) { continue; }
		const float ang = std::atan2(dd.y, dd.x);
		pie_DrawSoftRoundedBoxRotated(c.x, c.y, ll + w * 1.6f, w * 2.2f, ang, glow, w * 1.1f, w * 0.8f);
		pie_DrawSoftRoundedBoxRotated(c.x, c.y, ll + w * 0.4f, w * 0.7f, ang, core, w * 0.35f, std::max(1.2f, w * 0.2f));
	}
}
}

bool isMouthStat(const STRUCTURE_STATS *psStats)
{
	return psStats != nullptr && psStats->id == "A0TunnelMouth";
}

bool usableMouth(const BASE_OBJECT *psObj, unsigned player)
{
	if (psObj == nullptr || psObj->type != OBJ_STRUCTURE || psObj->died) { return false; }
	const STRUCTURE *s = static_cast<const STRUCTURE *>(psObj);
	if (!isMouthStat(s->pStructureType) || s->status != SS_BUILT || player >= MAX_PLAYERS || !aiCheckAlliances(s->player, player)) { return false; }
	for (const auto &kv : s_tunnels) { if (kv.second.a == s->id || kv.second.b == s->id) { return true; } }
	return false;
}

bool canUse(const DROID *d)
{
	if (d == nullptr || d->died || d->isTransporter() || d->isVtol() || modlayer::isFlier(d)) { return false; }
	return modlayer::layerOf(d) == modlayer::SURFACE && !modlayer::inTransition(d);
}

bool onOrder(DROID *psDroid, const DroidOrder *psOrder)
{
	if (psDroid == nullptr) { return false; }
	auto tr = s_transit.find(psDroid->id);
	if (tr != s_transit.end())
	{
		// unterwegs im Gang: der Befehl wird nach dem Austritt ausgefuehrt
		if (psOrder != nullptr && psOrder->type != DORDER_NONE && psOrder->type != DORDER_STOP && psOrder->type != DORDER_HOLD && psOrder->type != DORDER_GUARD)
		{
			tr->second.pending.clear();
			tr->second.pending.push_back(*psOrder);
		}
		return true;
	}
	if (s_digs.count(psDroid->id)) { return true; }   // der Bohrer graebt (laesst sich nicht unterbrechen)
	if (psOrder != nullptr && psOrder->type == DORDER_BUILD && isMouthStat(psOrder->psStats))
	{
		// Tunnelmund: nur Bohrfahrzeuge an der Oberflaeche - sie graben vom Standort aus bis zur Zielstelle
		if (!modlayerdig::isDrill(psDroid) || modlayer::layerOf(psDroid) != modlayer::SURFACE)
		{
			say(psDroid->player, _("Only a drill rig can dig a tunnel"));
			return true;
		}
		return startDig(psDroid, psOrder);
	}
	return false;
}

bool inTransit(const DROID *psDroid) { return psDroid != nullptr && s_transit.count(psDroid->id) != 0; }
bool digging(const DROID *psDroid) { return psDroid != nullptr && s_digs.count(psDroid->id) != 0; }

void tick(GameWorld &world)
{
	if (s_digs.empty() && s_tunnels.empty() && s_transit.empty()) { return; }
	digTick(world);
	collapseTick(world);
	enterTick(world);
	transitTick(world);
}

std::vector<Info> tunnels(unsigned player)
{
	std::vector<Info> out;
	if (player >= MAX_PLAYERS) { return out; }
	for (const auto &kv : s_tunnels)
	{
		if (!aiCheckAlliances(kv.second.player, player)) { continue; }
		Info i;
		i.id = kv.first;
		i.a = kv.second.pa;
		i.b = kv.second.pb;
		out.push_back(i);
	}
	return out;
}

void drawOverlay(const glm::mat4 &m)
{
	if (selectedPlayer >= MAX_PLAYERS || modlayer::current() != modlayer::SURFACE) { return; }
	for (const auto &kv : s_tunnels)
	{
		if (!aiCheckAlliances(kv.second.player, selectedPlayer)) { continue; }
		band(kv.second.pa, kv.second.pb, m, true, 120);
	}
	for (const auto &kv : s_digs)
	{
		const Dig &g = kv.second;
		if (!aiCheckAlliances(g.player, selectedPlayer)) { continue; }
		const DROID *d = IdToDroid(gameWorld.objects, kv.first, g.player);
		if (d == nullptr) { continue; }
		band(g.from, d->pos.xy(), m, false, 170);
		band(d->pos.xy(), g.to, m, true, 60 + static_cast<int>(40.0 * std::sin(static_cast<double>(realTime) / 250.0)));
	}
}

void saveState(std::vector<int64_t> &out)
{
	out.clear();
	out.push_back(1);
	out.push_back(static_cast<int64_t>(s_tunnels.size()));
	for (const auto &kv : s_tunnels)
	{
		const Tunnel &t = kv.second;
		out.insert(out.end(), { static_cast<int64_t>(kv.first), t.player, t.a, t.b, t.pa.x, t.pa.y, t.pb.x, t.pb.y });
	}
	out.push_back(static_cast<int64_t>(s_digs.size()));
	for (const auto &kv : s_digs)
	{
		const Dig &g = kv.second;
		out.insert(out.end(), { static_cast<int64_t>(kv.first), g.player, g.entrance, g.from.x, g.from.y, g.to.x, g.to.y, g.start, g.cost });
	}
	out.push_back(static_cast<int64_t>(s_transit.size()));
	for (const auto &kv : s_transit)
	{
		out.insert(out.end(), { static_cast<int64_t>(kv.first), kv.second.tunnel, kv.second.aToB ? 1 : 0, kv.second.start });
	}
}

void loadState(const std::vector<int64_t> &in)
{
	reset();
	size_t i = 0;
	auto next = [&in, &i](int64_t &v) { if (i >= in.size()) { return false; } v = in[i++]; return true; };
	int64_t ver = 0, n = 0;
	if (!next(ver) || ver != 1) { return; }
	if (!next(n)) { return; }
	for (int64_t k = 0; k < n; ++k)
	{
		int64_t v[8];
		for (int64_t &x : v) { if (!next(x)) { return; } }
		Tunnel t;
		t.player = static_cast<unsigned>(v[1]); t.a = static_cast<uint32_t>(v[2]); t.b = static_cast<uint32_t>(v[3]);
		t.pa = Vector2i(static_cast<int>(v[4]), static_cast<int>(v[5])); t.pb = Vector2i(static_cast<int>(v[6]), static_cast<int>(v[7]));
		if (t.player < MAX_PLAYERS) { s_tunnels[static_cast<uint32_t>(v[0])] = t; }
	}
	if (!next(n)) { return; }
	for (int64_t k = 0; k < n; ++k)
	{
		int64_t v[9];
		for (int64_t &x : v) { if (!next(x)) { return; } }
		Dig g;
		g.player = static_cast<unsigned>(v[1]); g.entrance = static_cast<uint32_t>(v[2]);
		g.from = Vector2i(static_cast<int>(v[3]), static_cast<int>(v[4])); g.to = Vector2i(static_cast<int>(v[5]), static_cast<int>(v[6]));
		g.start = static_cast<unsigned>(v[7]); g.cost = static_cast<int>(v[8]);
		if (g.player < MAX_PLAYERS) { s_digs[static_cast<uint32_t>(v[0])] = g; }
	}
	if (!next(n)) { return; }
	for (int64_t k = 0; k < n; ++k)
	{
		int64_t v[4];
		for (int64_t &x : v) { if (!next(x)) { return; } }
		Transit tr;
		tr.tunnel = static_cast<uint32_t>(v[1]); tr.aToB = v[2] != 0; tr.start = static_cast<unsigned>(v[3]);
		s_transit[static_cast<uint32_t>(v[0])] = tr;
	}
}

void reset()
{
	s_tunnels.clear();
	s_digs.clear();
	s_transit.clear();
	s_cool.clear();
}
}
