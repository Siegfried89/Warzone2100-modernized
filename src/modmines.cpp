// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Minen (siehe modmines.h)
#include "modmines.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/math_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "ai.h"
#include "display.h"
#include "droid.h"
#include "effects.h"
#include "feature.h"
#include "game_world.h"
#include "map.h"
#include "mapgrid.h"
#include "modlayer.h"
#include "objmem.h"
#include "stats.h"
#include "structure.h"
#include "modui.h"
#include "modoil.h"
#include "console.h"
#include "order.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"

#include <cmath>
#include <map>
#include <vector>

namespace modmines
{
namespace
{
const STRUCTURE_STATS *s_mine = nullptr;
bool s_looked = false;

constexpr int TRIGGER_R = DIST_TILE * 45 / 100;   // Ausloesen: Bodeneinheit fast auf der Mine
constexpr int BLAST_R = DIST_TILE * 9 / 10;       // Wirkung
constexpr int SPOT_R = DIST_TILE * 3 / 2;         // direkt davor sieht man sie
constexpr int SWEEP_R = DIST_TILE * 4;            // Sensor-Fahrzeug: Minensuche
constexpr unsigned BLAST_DMG = 340;
// (Wunsch 17.11. 19:36) Minen raeumen: Infanterie sieht Minen weiter (2,5 Felder) und loest die Panzerminen nicht aus; steht sie
// 3 Sekunden direkt an einer feindlichen Mine, ist die Mine entschaerft. Minenraeumer-Panzer (Turm "MineFlail") schlagen Minen vor
// dem Bug zur Explosion (der Raeumer nimmt kaum Schaden).
constexpr int INF_SPOT_R = DIST_TILE * 5 / 2;
constexpr int DISARM_R = DIST_TILE * 6 / 10;
constexpr unsigned DISARM_MS = 3000;
constexpr int FLAIL_R = DIST_TILE * 17 / 10;
constexpr int FLAIL_NEAR_R = DIST_TILE * 9 / 10;
int s_flail = -2;                                   // ECM-Bauteil "MineFlail" (-1 = gibt es nicht)
struct Disarm { uint32_t droid = 0; unsigned start = 0; };
std::map<uint32_t, Disarm> s_disarm;                // Mine -> wer gerade entschaerft

} // namespace

bool isFlail(const DROID *d)
{
	if (s_flail == -2)
	{
		s_flail = getCompFromID(COMP_ECM, WzString::fromUtf8("MineFlail"));
	}
	return s_flail >= 0 && d != nullptr && static_cast<int>(d->asBits[COMP_ECM]) == s_flail;
}

bool flailing(const DROID *d)
{
	return isFlail(d) && (d->secondaryOrder & DSS_MINECLEAR_MASK) != 0;
}

namespace
{

const STRUCTURE_STATS *mineStats()
{
	if (!s_looked)
	{
		s_looked = true;
		const int idx = getStructStatFromName(WzString::fromUtf8("A0Mine"));
		s_mine = idx >= 0 ? &asStructureStats[idx] : nullptr;
	}
	return s_mine;
}

bool groundUnit(const DROID *d)
{
	return d != nullptr && !d->died && !d->isFlying() && !d->isVtol() && !d->isTransporter() && !modlayer::isFlier(d);
}

void blast(STRUCTURE *mine)
{
	const int x = mine->pos.x, y = mine->pos.y;
	const int z = map_Height(gameWorld.map, x, y);
	const unsigned it = gameTime - deltaGameTime / 2 + 1;
	// Anzeige: Erdfontaene, Feuerball, Rauch
	Vector3i c(x, z + 8, y);
	addEffect(&c, EFFECT_EXPLOSION, EXPLOSION_TYPE_LARGE, false, nullptr, 0, it);
	for (int i = 0; i < 4; ++i)
	{
		Vector3i p(x + ((i & 1) ? 22 : -22), z + 14 + 10 * i, y + ((i & 2) ? 18 : -18));
		addEffect(&p, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM, false, nullptr, 0, it);
	}
	Vector3i sp(x, z + 30, y);
	addEffect(&sp, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH, false, nullptr, 0, it);
	audio_PlayStaticTrack(x, y, ID_SOUND_EXPLOSION);
	// Wirkung: Bodeneinheiten schwer, Bauwerke wenig (andere Minen koennen mitgehen)
	std::vector<BASE_OBJECT *> hit;
	for (BASE_OBJECT *o : gridStartIterate(x, y, static_cast<uint32_t>(BLAST_R)))
	{
		if (o != nullptr && !o->died && o != mine && modlayer::canInteract(mine, o)) { hit.push_back(o); }
	}
	for (BASE_OBJECT *o : hit)
	{
		if (o->died) { continue; }
		const int dist = iHypot(Vector2i(o->pos.x - x, o->pos.y - y));
		if (dist > BLAST_R) { continue; }
		const unsigned dmg = BLAST_DMG * static_cast<unsigned>(100 - 60 * dist / BLAST_R) / 100u;
		if (o->type == OBJ_DROID)
		{
			DROID *d = static_cast<DROID *>(o);
			if (!groundUnit(d)) { continue; }
			droidDamage(gameWorld, d, nullptr, dmg, WC_KINETIC, WSC_BOMB, it, false, 0, false);
		}
		else if (o->type == OBJ_STRUCTURE)
		{
			structureDamage(gameWorld, static_cast<STRUCTURE *>(o), nullptr, dmg / 4u, WC_KINETIC, WSC_BOMB, it, false, 0, false);
		}
	}
	if (!mine->died) { destroyStruct(mine, it, gameWorld); }
}
}

void tick()
{
	const STRUCTURE_STATS *ms = mineStats();
	if (ms == nullptr) { return; }
	std::vector<STRUCTURE *> boom, disarmed;
	std::vector<std::pair<STRUCTURE *, uint32_t>> flailHits;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s == nullptr || s->died || s->pStructureType != ms) { continue; }
			bool spotted[MAX_PLAYERS] = {};
			bool trigger = false;
			const DROID *flail = nullptr;
			const DROID *sapper = nullptr;
			for (BASE_OBJECT *o : gridStartIterate(s->pos.x, s->pos.y, static_cast<uint32_t>(SWEEP_R)))
			{
				if (o == nullptr || o->died || o->type != OBJ_DROID || o->player >= MAX_PLAYERS || aiCheckAlliances(o->player, p)) { continue; }
				const DROID *d = static_cast<const DROID *>(o);
				if (!modlayer::canInteract(s, d)) { continue; }
				const int dist = iHypot(Vector2i(d->pos.x - s->pos.x, d->pos.y - s->pos.y));
				const bool inf = modoil::isInfantry(d);
				const bool fl = isFlail(d);
				const bool flSpin = fl && flailing(d);   // (20:06) nur mit drehender Walze schlaegt der Raeumer Minen ab, sonst loest er sie aus
				if (dist <= SPOT_R || (d->droidType == DROID_SENSOR && dist <= SWEEP_R) || (inf && dist <= INF_SPOT_R) || (fl && dist <= SWEEP_R)) { spotted[d->player] = true; }
				if (s->status != SS_BUILT || !groundUnit(d)) { continue; }
				if (flSpin)
				{
					// Raeumer: Mine vor dem Bug (oder ganz nah) wird zur Explosion gebracht
					const int64_t fx = iSinR(d->rot.direction, 1024), fy = iCosR(d->rot.direction, 1024);
					const int64_t dx = s->pos.x - d->pos.x, dy = s->pos.y - d->pos.y;
					const bool ahead = (fx * dx + fy * dy) * 10 >= static_cast<int64_t>(dist) * 1024 * 2;   // innerhalb etwa +-78 Grad
					if (dist <= FLAIL_NEAR_R || (dist <= FLAIL_R && ahead)) { if (flail == nullptr) { flail = d; } continue; }
				}
				if (inf)
				{
					if (dist <= DISARM_R && (sapper == nullptr || d->id < sapper->id)) { sapper = d; }
					continue;   // Panzerminen: Infanterie loest sie nicht aus
				}
				if (dist <= TRIGGER_R) { trigger = true; }
			}
			if (flail != nullptr) { flailHits.emplace_back(s, flail->id); s_disarm.erase(s->id); }
			else if (!trigger && sapper != nullptr)
			{
				Disarm &dz = s_disarm[s->id];
				if (dz.droid != sapper->id) { dz.droid = sapper->id; dz.start = gameTime; }
				else if (gameTime - dz.start >= DISARM_MS) { disarmed.push_back(s); }
			}
			else { s_disarm.erase(s->id); }
			// fuer fremde Spieler verstecken (Verbuendete sehen die Minen ihrer Partner)
			for (unsigned q = 0; q < MAX_PLAYERS; ++q)
			{
				if (q == p || aiCheckAlliances(q, p) || spotted[q]) { continue; }
				s->visible[q] = 0;
			}
			if (trigger && flail == nullptr) { boom.push_back(s); }
		}
	}
	for (STRUCTURE *s : boom)
	{
		if (!s->died) { blast(s); }
	}
	// Raeumer: Explosion mit Effekt, aber nur leichter Schaden am Raeumer selbst (die Kette faengt ihn ab)
	for (const auto &fh : flailHits)
	{
		STRUCTURE *s = fh.first;
		if (s->died) { continue; }
		const unsigned it = gameTime - deltaGameTime / 2 + 1;
		const int z = map_Height(gameWorld.map, s->pos.x, s->pos.y);
		Vector3i c(s->pos.x, z + 8, s->pos.y);
		addEffect(&c, EFFECT_EXPLOSION, EXPLOSION_TYPE_LARGE, false, nullptr, 0, it);
		Vector3i sp(s->pos.x, z + 24, s->pos.y);
		addEffect(&sp, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH, false, nullptr, 0, it);
		audio_PlayStaticTrack(s->pos.x, s->pos.y, ID_SOUND_EXPLOSION);
		for (unsigned q = 0; q < MAX_PLAYERS; ++q)
		{
			for (DROID *d : gameWorld.objects.droids[q])
			{
				if (d->id == fh.second && !d->died) { droidDamage(gameWorld, d, nullptr, BLAST_DMG / 16u, WC_KINETIC, WSC_BOMB, it, false, 0, false); }
			}
		}
		if (!s->died) { destroyStruct(s, it, gameWorld); }
	}
	// Infanterie: Mine entschaerft (verschwindet ohne Explosion, kleine Staubwolke)
	for (STRUCTURE *s : disarmed)
	{
		if (s->died) { continue; }
		Vector3i c(s->pos.x, map_Height(gameWorld.map, s->pos.x, s->pos.y) + 6, s->pos.y);
		addEffect(&c, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING, false, nullptr, 0, gameTime - deltaGameTime / 2 + 1);
		s_disarm.erase(s->id);
		removeStruct(s, true, gameWorld);
	}
	if (s_disarm.size() > 512) { s_disarm.clear(); }
}

int clearMines(unsigned player)
{
	if (player >= MAX_PLAYERS) { return 0; }
	const STRUCTURE_STATS *ms = mineStats();
	// bekannte feindliche Minen (fuer diesen Spieler sichtbar)
	std::vector<const STRUCTURE *> mines;
	if (ms != nullptr)
	{
		for (unsigned p = 0; p < MAX_PLAYERS; ++p)
		{
			if (p == player || aiCheckAlliances(p, player)) { continue; }
			for (const STRUCTURE *s : gameWorld.objects.structures[p])
			{
				if (s != nullptr && !s->died && s->pStructureType == ms && s->visible[player] > 0) { mines.push_back(s); }
			}
		}
	}
	// (Wunsch 17.11. 20:06) gewaehlte Minenraeumer: Walze an/aus (Zweitbefehl DSO_MINECLEAR, ueber das Netz abgeglichen); beim
	// Einschalten fahren sie zu den bekannten Minen (dabei schlagen sie auch unbekannte vor dem Bug ab)
	std::vector<DROID *> flails;
	bool flailsOn = true;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (d->selected && !d->died && isFlail(d)) { flails.push_back(d); if (!flailing(d)) { flailsOn = false; } }
	}
	int sent = 0;
	if (!flails.empty())
	{
		for (DROID *d : flails)
		{
			secondarySetState(d, gameWorld.objects, DSO_MINECLEAR, flailsOn ? DSS_NONE : DSS_MINECLEAR_SET);
		}
		if (player == selectedPlayer) { addConsoleMessage(flailsOn ? _("Mine flail: off") : _("Mine flail: on - the drum spins and beats off mines ahead"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
		if (!flailsOn && !mines.empty())
		{
			std::vector<bool> taken(mines.size(), false);
			for (DROID *d : flails)
			{
				int best = -1;
				int64_t bestD = static_cast<int64_t>(DIST_TILE * 40) * (DIST_TILE * 40);
				for (size_t i = 0; i < mines.size(); ++i)
				{
					if (taken[i]) { continue; }
					const int64_t dx = d->pos.x - mines[i]->pos.x, dy = d->pos.y - mines[i]->pos.y;
					if (dx * dx + dy * dy < bestD) { bestD = dx * dx + dy * dy; best = static_cast<int>(i); }
				}
				if (best < 0) { continue; }
				taken[static_cast<size_t>(best)] = true;
				orderDroidLoc(d, DORDER_MOVE, mines[static_cast<size_t>(best)]->pos.x, mines[static_cast<size_t>(best)]->pos.y, ModeQueue);
				++sent;
			}
		}
	}
	bool anySelected = false;
	for (const DROID *d : gameWorld.objects.droids[player]) { if (d->selected && !d->died && modoil::isInfantry(d)) { anySelected = true; break; } }
	if (!anySelected && !flails.empty()) { return sent; }   // nur Raeumer gewaehlt: Infanterie bleibt, wo sie ist
	std::vector<DROID *> crew;
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (!d->died && modoil::isInfantry(d) && (!anySelected || d->selected)) { crew.push_back(d); }
	}
	if (crew.empty()) { return sent; }
	if (mines.empty())
	{
		if (player == selectedPlayer) { addConsoleMessage(_("Clear mines: no known mines nearby - infantry spots mines within 2.5 tiles and does not set off anti-tank mines"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
		return sent;
	}
	// jede Mine an den naechsten freien Infanteristen (hoechstens 30 Felder entfernt)
	std::vector<bool> used(crew.size(), false);
	for (const STRUCTURE *m : mines)
	{
		int best = -1;
		int64_t bestD = static_cast<int64_t>(DIST_TILE * 30) * (DIST_TILE * 30);
		for (size_t i = 0; i < crew.size(); ++i)
		{
			if (used[i]) { continue; }
			const int64_t dx = crew[i]->pos.x - m->pos.x, dy = crew[i]->pos.y - m->pos.y;
			if (dx * dx + dy * dy < bestD) { bestD = dx * dx + dy * dy; best = static_cast<int>(i); }
		}
		if (best < 0) { continue; }
		used[static_cast<size_t>(best)] = true;
		orderDroidLoc(crew[static_cast<size_t>(best)], DORDER_MOVE, m->pos.x, m->pos.y, ModeQueue);
		++sent;
	}
	return sent;
}

void drawOverlay(const glm::mat4 &m)
{
	if (selectedPlayer >= MAX_PLAYERS || !(keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT))) { return; }
	const STRUCTURE_STATS *ms = mineStats();
	if (ms == nullptr) { return; }
	const float sc = modui::uiScale();
	const float r = 8.f * sc;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s == nullptr || s->died || s->pStructureType != ms || s->visible[selectedPlayer] == 0) { continue; }
			if (modlayer::layerOf(s) != modlayer::current()) { continue; }
			const Vector3i pos(s->pos.x, map_Height(gameWorld.map, s->pos.x, s->pos.y) + 6, -s->pos.y);
			Vector2i sp(0, 0);
			if (pie_RotateProjectWithPerspective(&pos, m, &sp) <= 0) { continue; }
			const float x = static_cast<float>(sp.x), y = static_cast<float>(sp.y);
			// gelbes Warnzeichen: weicher Lichthof, gelbe Scheibe mit dunklem Rand, schwarze Mine (Kreis mit vier Zuendstiften)
			pie_DrawSoftRoundedBox(x - r * 2.2f, y - r * 2.2f, x + r * 2.2f, y + r * 2.2f, pal_RGBA(255, 220, 40, 60), r * 2.2f, r * 1.4f);
			pie_DrawSoftRoundedBox(x - r - 1.5f, y - r - 1.5f, x + r + 1.5f, y + r + 1.5f, pal_RGBA(40, 30, 0, 230), r + 1.5f, 1.2f);
			pie_DrawSoftRoundedBox(x - r, y - r, x + r, y + r, pal_RGBA(255, 214, 30, 255), r, 1.2f);
			const float k = r * 0.42f;
			pie_DrawSoftRoundedBox(x - k, y - k, x + k, y + k, pal_RGBA(25, 22, 10, 255), k, 1.f);
			for (int i = 0; i < 4; ++i)
			{
				const float a = static_cast<float>(i) * 1.5707963f + 0.785398f;
				const float cx = x + std::cos(a) * k * 1.45f, cy = y + std::sin(a) * k * 1.45f;
				pie_DrawSoftRoundedBoxRotated(cx, cy, k * 0.9f, std::max(1.5f, k * 0.38f), a, pal_RGBA(25, 22, 10, 255), 1.f, 1.f);
			}
		}
	}
}

void reset()
{
	s_disarm.clear();
	s_flail = -2;
	s_mine = nullptr;
	s_looked = false;
}
}
