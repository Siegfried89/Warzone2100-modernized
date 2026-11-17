// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Drei Ebenen-Knoepfe (Orbit / Oberflaeche / Untergrund) mit weicher Ueberblendung, siehe modlayer.h

#include "modlayer.h"
#include "modrocket.h"
#include "lib/sound/tracklib.h"
#include "modlayermap.h"
#include "modui.h"
#include "modrosette.h"
#include "modcombot.h"
#include "modoil.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/button.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"

#include "lib/netplay/sync_debug.h"

#include "ai.h"
#include "console.h"
#include "droid.h"
#include "featuredef.h"
#include "effects.h"
#include "game_world.h"
#include "hci.h"
#include "map.h"
#include "order.h"
#include "projectile.h"
#include "structure.h"
#include "visibility.h"

#include <algorithm>
#include <memory>
#include <string>
#include "modlayerdig.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace modlayer
{

namespace
{

constexpr int BTN_W = 78;
constexpr int BTN_H = 22;
constexpr int BTN_GAP = 3;
constexpr int PAD = 3;
constexpr int TOTAL_W = BTN_W + 2 * PAD;
constexpr int TOTAL_H = 3 * BTN_H + 2 * BTN_GAP + 2 * PAD;
constexpr UDWORD FADE_TOTAL_MS = 700;   // erste Haelfte ausblenden, in der Mitte umschalten, zweite Haelfte einblenden

std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_root;
std::vector<std::shared_ptr<W_BUTTON>> s_buttons;
bool s_suppressed = false;

Layer s_current = SURFACE;   // was gerade angezeigt wird
Layer s_target = SURFACE;    // wohin gewechselt wird
bool s_busy = false;
UDWORD s_start = 0;

const char *layerName(Layer l)
{
	switch (l)
	{
	case ORBIT: return "ORBIT";
	case SURFACE: return "SURFACE";
	default: return "UNDERGROUND";
	}
}

const char *layerTip(Layer l)
{
	switch (l)
	{
	case ORBIT: return "Orbit layer: floating asteroids and solar panels, only fliers go here: select Hoverjets (or VTOLs) and press this button to climb, press Surface to descend";
	case SURFACE: return "Surface layer: the battlefield";
	default: return "Underground layer: units that drive into a Shaft (build one with a truck) travel here unseen; right-click a Shaft to enter or leave";
	}
}

PIELIGHT veilColour(Layer l, unsigned char a)
{
	switch (l)
	{
	case ORBIT: return pal_RGBA(2, 8, 28, a);
	case UNDERGROUND: return pal_RGBA(24, 10, 2, a);
	default: return pal_RGBA(0, 0, 0, a);
	}
}

// Fortschritt der Ueberblendung 0..1 (1 = fertig)
float progress()
{
	if (!s_busy) { return 1.f; }
	const UDWORD el = realTime - s_start;
	return std::min(1.f, static_cast<float>(el) / static_cast<float>(FADE_TOTAL_MS));
}

// Spielzustand: Ebene je Objekt-Kennung (nur Objekte ausserhalb der Oberflaeche), Abklingzeit nach einem Schachtwechsel
std::unordered_map<uint32_t, Layer> s_layerOf;
std::unordered_map<uint32_t, UDWORD> s_cool;
std::unordered_set<uint32_t> s_openedShafts;   // Schaechte, deren Untergrund-Kammer schon freigelegt ist

constexpr int PORTAL_ORDER_RANGE = 192;   // Zielpunkt des Marschbefehls liegt am Schacht
constexpr int PORTAL_REACH = 240;         // so nah muss die Einheit am Schacht sein (Weg endet neben dem blockierten Feld)
constexpr UDWORD PORTAL_COOLDOWN = 4000;  // ms bis die Einheit wieder wechseln darf

bool isShaft(const STRUCTURE_STATS *st)
{
	return st != nullptr && st->type == REF_GENERIC && st->id.startsWith("A0LayerShaft");
}

Layer layerOfId(uint32_t id)
{
	const auto it = s_layerOf.find(id);
	return it == s_layerOf.end() ? SURFACE : it->second;
}

std::unordered_map<uint32_t, Layer> s_flyReq;   // Flieger -> Ebene, in die sie auf Knopfdruck wechseln sollen (wird im Spielschritt abgearbeitet)

// MOD (16.11. 19:34): fliessender Ebenenwechsel wie in MF. Phase A (in der alten Ebene): der Flieger steigt steil auf (Orbit) bzw. sinkt
// in die verschwommene Tiefe unter den Inseln (Oberflaeche). Danach wechselt er die Ebene; Phase B (in der neuen Ebene): er taucht aus der
// verschwommenen Untertextur auf und steigt auf Flughoehe (Orbit) bzw. kommt von hoch oben herab (Oberflaeche). Spielzustand: nur der
// Zeitpunkt des Wechsels (gameTime, deterministisch); Hoehe nur Anzeige.
constexpr UDWORD TRANS_A_MS = 900;
constexpr UDWORD TRANS_B_MS = 1600;
struct Transition { Layer to; UDWORD start; };
std::unordered_map<uint32_t, Transition> s_trans;

// beim Umschalten der Anzeige: ausgewaehlte Einheiten, die in der neuen Ebene nicht sichtbar sind, abwaehlen
void deselectOtherLayers(Layer shown)
{
	if (selectedPlayer >= MAX_PLAYERS) { return; }   // auch ohne Objekte in anderen Ebenen: Oberflaechen-Einheiten duerfen im Untergrund nicht ausgewaehlt bleiben
	for (DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (!d->selected || layerOfId(d->id) == shown) { continue; }
		const auto rq = s_flyReq.find(d->id);
		if (rq != s_flyReq.end() && rq->second == shown) { continue; }   // Flieger, die gerade in die gezeigte Ebene steigen, bleiben ausgewaehlt
		const auto tr = s_trans.find(d->id);
		if (tr != s_trans.end() && tr->second.to == shown) { continue; }
		DeSelectDroid(d);
	}
}

/// Ebenenwechsel einer Einheit: Sicht in der alten Karte abmelden, in der neuen anmelden, Hoehe aus der neuen Karte
void applyLayer(DROID *d, Layer next)
{
	const Layer prev = layerOfId(d->id);
	if (prev == next) { return; }
	{
		modlayermap::Scope sc(prev);
		visRemoveVisibility(d, gameWorld.map);
	}
	if (next == SURFACE) { s_layerOf.erase(d->id); } else { s_layerOf[d->id] = next; }
	{
		modlayermap::Scope sc(next);
		d->pos.z = map_Height(gameWorld.map, d->pos.x, d->pos.y);
		visTilesUpdate(d, gameWorld.map);
	}
	syncDebug("modlayer: droid %u -> layer %d", d->id, static_cast<int>(next));
}

/// Flieger: Knopfdruck-Wuensche abarbeiten; im Orbit kehren Flieger mit Rueckkehrbefehl oder leere VTOLs zur Oberflaeche zurueck
void flierTick()
{
	if (!s_flyReq.empty())
	{
		if (selectedPlayer < MAX_PLAYERS)
		{
			for (DROID *d : gameWorld.objects.droids[selectedPlayer])
			{
				const auto rq = s_flyReq.find(d->id);
				if (rq == s_flyReq.end() || d->died != 0) { continue; }
				if (layerOfId(d->id) == UNDERGROUND || layerOfId(d->id) == rq->second || s_trans.count(d->id)) { continue; }
				if (rq->second == ORBIT && d->droidType == DROID_CYBORG_CONSTRUCT) { continue; }   // (20:59) hinauf nur mit der Rakete
				s_trans[d->id] = Transition{rq->second, gameTime};
				orderDroid(d, DORDER_STOP, ModeImmediate);
				const unsigned it = gameTime - deltaGameTime / 2 + 1;
				for (int i = 0; i < 3; i++)
				{
					Vector3i sp(d->pos.x + (i - 1) * 20, d->pos.z + 10 + i * 12, d->pos.y + (1 - i) * 14);
					addEffect(&sp, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH, false, nullptr, 0, it);
				}
			}
		}
		s_flyReq.clear();
	}
	// laufende Wechsel: nach Phase A die Ebene wechseln, nach Phase B fertig
	for (auto it = s_trans.begin(); it != s_trans.end();)
	{
		DROID *d = nullptr;
		for (int p = 0; p < MAX_PLAYERS && d == nullptr; p++)
		{
			for (DROID *c : gameWorld.objects.droids[p]) { if (c->id == it->first) { d = c; break; } }
		}
		if (d == nullptr || d->died != 0) { it = s_trans.erase(it); continue; }
		const UDWORD age = gameTime - it->second.start;
		if (layerOfId(d->id) != it->second.to && age >= TRANS_A_MS)
		{
			applyLayer(d, it->second.to);
			orderDroid(d, DORDER_STOP, ModeImmediate);
		}
		if (age >= TRANS_A_MS + TRANS_B_MS) { it = s_trans.erase(it); continue; }
		++it;
	}
	if (s_layerOf.empty()) { return; }
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d->died != 0 || layerOfId(d->id) != ORBIT) { continue; }
			const auto ot = d->order.type;
			const bool homeward = ot == DORDER_RTR || ot == DORDER_RTB || ot == DORDER_RTR_SPECIFIED || ot == DORDER_REARM
			                      || d->action == DACTION_MOVETOREARM || d->action == DACTION_WAITFORREARM || d->action == DACTION_MOVETOREARMPOINT
			                      || d->action == DACTION_WAITDURINGREARM;
			if (homeward || (d->isVtol() && vtolEmpty(d)))
			{
				applyLayer(d, SURFACE);            // Heimat, Reparatur- und Landeplaetze liegen auf der Oberflaeche; der Befehl bleibt bestehen
				if (ot == DORDER_RTR || ot == DORDER_RTB || ot == DORDER_REARM) { orderDroid(d, ot, ModeImmediate); }
				if (static_cast<unsigned>(d->player) == selectedPlayer && d->selected) { request(SURFACE); }
			}
		}
	}
}

void update()
{
	if (!s_busy) { return; }
	const float p = progress();
	if (p >= 0.5f && s_current != s_target)
	{
		s_current = s_target;
		modlayermap::setView(s_current);   // eigene Karte der Ebene anzeigen (Gelaende, Beleuchtung neu aufbauen)
		deselectOtherLayers(s_current);
	}
	if (p >= 1.f) { s_busy = false; }
}

// Schleier ueber das ganze Bild: Dauer-Farbton der Ebene + Ueberblendung beim Wechsel
void drawVeil()
{
	const int w = static_cast<int>(screenWidth), h = static_cast<int>(screenHeight);
	if (s_current == ORBIT) { pie_BoxFill_alpha(0, 0, w, h, pal_RGBA(10, 30, 90, 50)); }
	else if (s_current == UNDERGROUND) { pie_BoxFill_alpha(0, 0, w, h, pal_RGBA(40, 16, 0, 55)); }
	if (s_busy)
	{
		const float p = progress();
		const float k = p < 0.5f ? p * 2.f : (1.f - p) * 2.f;
		const int a = static_cast<int>(k * 235.f);
		if (a > 0) { pie_BoxFill_alpha(0, 0, w, h, veilColour(s_target, static_cast<unsigned char>(a))); }
	}
}

void drawRoot(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	drawVeil();
	const int x0 = static_cast<int>(xOffset) + psWidget->x();
	const int y0 = static_cast<int>(yOffset) + psWidget->y();
	const float s = modui::uiScale();
	modui::wzPanel(x0, y0, x0 + static_cast<int>(TOTAL_W * s), y0 + static_cast<int>(TOTAL_H * s));   // MOD: WZ-Stil
}

void drawButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const float s = modui::uiScale();
	const Layer l = static_cast<Layer>(psWidget->UserData);
	const int x0 = static_cast<int>(xOffset) + psWidget->x();
	const int y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = static_cast<int>(BTN_W * s), h = static_cast<int>(BTN_H * s);
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const bool active = (s_busy ? s_target : s_current) == l;
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;

	modui::wzButton(x0, y0, w, h, layerName(l), active, hl, down, modui::sizeClassFor(s));   // MOD (16.11. Punkt 1): WZ-Stil
}

void ensureScreen()
{
	if (s_screen) { return; }
	auto screen = W_SCREEN::make();
	screen->psForm->hide();   // MOD-FIX: verborgene Wurzel = durchlaessig fuer Mausklicks (sonst schluckt sie JEDEN Klick im Spiel, siehe notifications.cpp)
	auto root = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(root);
	root->displayFunction = drawRoot;
	s_buttons.clear();
	// von oben nach unten: Orbit, Oberflaeche, Untergrund
	const Layer order[3] = { ORBIT, SURFACE, UNDERGROUND };
	for (const Layer l : order)
	{
		auto b = std::make_shared<W_BUTTON>();
		root->attach(b);
		b->displayFunction = drawButton;
		b->UserData = static_cast<UDWORD>(l);
		b->setTip(layerTip(l));
		b->addOnClickHandler([l](W_BUTTON &) { widgScheduleTask([l]() { sendSelectedFliers(l); request(l); }); });
		s_buttons.push_back(b);
	}
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_root = root;
}

void layout()
{
	const float s = modui::uiScale();
	const int w = static_cast<int>(TOTAL_W * s), h = static_cast<int>(TOTAL_H * s);
	s_root->setGeometry(static_cast<int>(screenWidth) - w - 4, 44, w, h);
	for (size_t i = 0; i < s_buttons.size(); ++i)
	{
		s_buttons[i]->setGeometry(static_cast<int>(PAD * s), static_cast<int>((PAD + static_cast<int>(i) * (BTN_H + BTN_GAP)) * s),
		                          static_cast<int>(BTN_W * s), static_cast<int>(BTN_H * s));
	}
}

} // namespace

Layer current()
{
	return s_current;
}

void request(Layer target)
{
	if (s_busy || target == s_current) { return; }
	s_target = target;
	s_start = realTime;
	s_busy = true;
}

// MOD (Wunsch 14.11. Punkt 3): steht die Klangquelle (Infanterist) in einem besetzten Haus/Hochhaus? Nur Anzeige/Klang.
static int soundInBuilding(const SIMPLE_OBJECT *psObj)
{
	if (psObj == nullptr || psObj->type != OBJ_DROID) { return 0; }
	const DROID *d = static_cast<const DROID *>(psObj);
	if (!modoil::isHouseOccupant(d)) { return 0; }
	return modoil::tileHasHouse(gameWorld.map, map_coord(d->pos.x), map_coord(d->pos.y)) ? 1 : 0;
}

void setSuppressed(bool suppressed)
{
	s_suppressed = suppressed;
}

void tick()
{
	if (!modcombot::ready()) { return; }   // nur wenn die Combot-Mod geladen ist
	update();                              // vor dem Zeichnen: Ebene am Ueberblend-Mittelpunkt umschalten
	modlayermap::tick();                   // aufgeschobenes Neuaufbauen des Gelaendes (nach Grabungen)
	// MOD (Punkt 3): Umgebung - Untergrund = still und gedaempft (kein Hall), Stadt-Karten (Urban) an der Oberflaeche = Hall zwischen
	// den Haeusern; Infanterie in besetzten Gebaeuden = dumpf mit Treppenhaus-Hall (je Klangquelle, siehe soundInBuilding)
	sound_SetSampleEnvironmentHook(&soundInBuilding);
	sound_SetEnvironment(s_current == UNDERGROUND ? 2 : ((s_current == SURFACE && currentMapTileset == MAP_TILESET::URBAN) ? 1 : 0));
	const bool want = !s_suppressed && !modrosette::active();   // (17.11.) Ebenen-Knoepfe jetzt in der Mod-Rosette
	if (!want && !s_screen) { return; }
	ensureScreen();
	if (!s_root) { return; }
	s_root->show(want);
	if (want) { layout(); }
}

// ---------------------------------------------------------------------------------------------------------------------
// Spielzustand
// ---------------------------------------------------------------------------------------------------------------------

Layer layerOf(const SIMPLE_OBJECT *obj)
{
	if (s_layerOf.empty() || obj == nullptr) { return SURFACE; }
	return layerOfId(obj->id);
}

void assignLayer(const SIMPLE_OBJECT *obj, Layer l)
{
	if (obj == nullptr) { return; }
	if (l == SURFACE) { s_layerOf.erase(obj->id); } else { s_layerOf[obj->id] = l; }
}

bool active()
{
	return !s_layerOf.empty();
}

bool canInteract(const SIMPLE_OBJECT *a, const SIMPLE_OBJECT *b)
{
	if (s_layerOf.empty()) { return true; }
	return layerOf(a) == layerOf(b);
}

bool projectileCanHit(const PROJECTILE *proj, const SIMPLE_OBJECT *target)
{
	// Ebenen-Sonderwaffe: wirkt nur in ihrer Zielebene (auch wenn sonst nichts ausserhalb der Oberflaeche existiert)
	if (proj != nullptr && proj->psWStats != nullptr && proj->psWStats->crossLayer >= 0) { return static_cast<int>(layerOf(target)) == proj->psWStats->crossLayer; }
	if (s_layerOf.empty() || proj == nullptr) { return true; }
	const SIMPLE_OBJECT *ref = proj->psSource != nullptr ? static_cast<const SIMPLE_OBJECT *>(proj->psSource)
	                           : (proj->psDest != nullptr ? static_cast<const SIMPLE_OBJECT *>(proj->psDest) : nullptr);
	if (ref == nullptr) { return true; }
	return layerOf(ref) == layerOf(target);
}

Layer projectileLayer(const PROJECTILE *proj)
{
	if (proj != nullptr && proj->psWStats != nullptr && proj->psWStats->crossLayer >= 0) { return static_cast<Layer>(proj->psWStats->crossLayer); }
	if (s_layerOf.empty() || proj == nullptr) { return SURFACE; }
	const SIMPLE_OBJECT *ref = proj->psSource != nullptr ? static_cast<const SIMPLE_OBJECT *>(proj->psSource)
	                           : (proj->psDest != nullptr ? static_cast<const SIMPLE_OBJECT *>(proj->psDest) : nullptr);
	return ref != nullptr ? layerOf(ref) : SURFACE;
}

bool hiddenInView(const BASE_OBJECT *obj)
{
	// Ohne Zusatzkarten (Mod nicht geladen / Erzeugen fehlgeschlagen) bleibt alles sichtbar und waehlbar.
	if (obj == nullptr || !modlayermap::worldsExist()) { return false; }
	if (obj->type == OBJ_DROID && modrocket::aboard(static_cast<const DROID *>(obj))) { return true; }   // (17.11. 20:56) sitzt in der Rakete
	// Oberflaechen-Ansicht und nichts ausserhalb der Oberflaeche: nichts zu verbergen
	if (s_current == SURFACE && s_layerOf.empty()) { return false; }
	if (obj->type == OBJ_STRUCTURE && isShaft(static_cast<const STRUCTURE *>(obj)->pStructureType)) { return false; }   // Schaechte in jeder Ebene sichtbar
	return layerOf(obj) != s_current;
}

PROPULSION_TYPE effectivePropulsion(const DROID *psDroid, PROPULSION_TYPE realType)
{
	// Jede Ebene hat jetzt ihre eigene Karte: Einheiten bewegen sich dort mit ihrem echten Antrieb (Fels blockiert wirklich).
	// Kampfroboter (Combots) haben auch "Beine", duerfen aber nicht durch Haeuser/Graeben laufen: sie bewegen sich wie Radfahrzeuge
	// (gleiche Sperrbits, andere Pfad-Domaene), nur echte Infanterie behaelt Legged
	// (16.11. 19:34) Combots mit Sprungduesen / Jetpack fliegen ueber Hindernisse und die Orbit-Leere hinweg
	if (psDroid != nullptr && realType == PROPULSION_TYPE_LEGGED && modcombot::canFly(psDroid))
	{
		return PROPULSION_TYPE_LIFT;
	}
	if (realType == PROPULSION_TYPE_LEGGED && psDroid != nullptr && !modoil::isInfantry(psDroid))
	{
		return PROPULSION_TYPE_WHEELED;
	}
	// Schwebende Drohnen (Balldrohne) fliegen ueber alles hinweg: Wegfindung und Blockierung wie ein Flieger
	if (psDroid != nullptr && psDroid->getPropulsionStats()->alwaysFlying)
	{
		return PROPULSION_TYPE_LIFT;
	}
	return realType;
}

bool isPortalTile(int player, int tileX, int tileY)
{
	if (tileX < 0 || tileY < 0 || tileX >= gameWorld.map.width || tileY >= gameWorld.map.height) { return false; }
	const MAPTILE *tile = mapTile(gameWorld.map, tileX, tileY);
	if (tile == nullptr || tile->psObject == nullptr || tile->psObject->type != OBJ_STRUCTURE) { return false; }
	const STRUCTURE *s = static_cast<const STRUCTURE *>(tile->psObject);
	return s->status == SS_BUILT && isShaft(s->pStructureType) && aiCheckAlliances(s->player, player);
}

void gameTick()
{
	if (!modlayermap::worldsExist()) { return; }
	modlayerdig::tick(gameWorld);   // Bohrfahrzeuge   // ohne Zusatzkarten gibt es keinen Schacht-Verkehr
	flierTick();                    // Flieger: Orbit <-> Oberflaeche

	// Schaechte einsammeln (ohne Schacht passiert nichts und nichts kostet Zeit)
	std::vector<const STRUCTURE *> shafts;
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[p])
		{
			if (s->died == 0 && s->status == SS_BUILT && isShaft(s->pStructureType)) { shafts.push_back(s); }
		}
	}
	if (shafts.empty()) { return; }

	// fertiger Schacht: im Untergrund eine kleine Kammer freilegen (MF: Aufzugskammer 10x10 Whixel, ohne Gangnetz-Anbindung; weiter nur durch Bohren)
	for (const STRUCTURE *s : shafts)
	{
		if (s_openedShafts.insert(s->id).second)
		{
			modlayermap::openSite(map_coord(s->pos.x), map_coord(s->pos.y), s->player, 1, 3);
		}
	}

	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		for (DROID *d : gameWorld.objects.droids[p])
		{
			if (d->died != 0 || d->order.type != DORDER_MOVE) { continue; }
			if (d->isVtol() || d->isTransporter() || modoil::isCargo(d->id) || layerOfId(d->id) == ORBIT) { continue; }   // Orbit: Schaechte sind nur Oberflaeche <-> Untergrund
			const auto cool = s_cool.find(d->id);
			if (cool != s_cool.end())
			{
				if (gameTime < cool->second) { continue; }
				s_cool.erase(cool);
			}
			for (const STRUCTURE *s : shafts)
			{
				if (!aiCheckAlliances(s->player, d->player)) { continue; }
				if (iHypot(d->order.pos - s->pos.xy()) > PORTAL_ORDER_RANGE) { continue; }
				if (iHypot(d->pos.xy() - s->pos.xy()) > PORTAL_REACH) { continue; }
				const Layer next = layerOfId(d->id) == SURFACE ? UNDERGROUND : SURFACE;
				applyLayer(d, next);
				s_cool[d->id] = gameTime + PORTAL_COOLDOWN;
				orderDroid(d, DORDER_STOP, ModeImmediate);
				// Staubwolke am Schacht
				const unsigned it = gameTime - deltaGameTime / 2 + 1;
				for (int i = 0; i < 3; i++)
				{
					Vector3i sp(s->pos.x + (i - 1) * 24, s->pos.z + 40 + i * 18, s->pos.y + (1 - i) * 16);
					addEffect(&sp, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH, false, nullptr, 0, it);
				}
				// die Anzeige folgt der gewaehlten Einheit
				if (static_cast<unsigned>(d->player) == selectedPlayer && d->selected) { request(next); }
				break;
			}
		}
	}
}

void moveToLayer(DROID *d, Layer next)
{
	if (d != nullptr) { applyLayer(d, next); }
}

bool isFlier(const DROID *psDroid)
{
	return psDroid != nullptr && (psDroid->getPropulsionStats()->alwaysFlying || psDroid->isVtol() || modcombot::canFly(psDroid));
}

float transitionOffset(const DROID *d, UDWORD time)
{
	if (d == nullptr || s_trans.empty()) { return 0.f; }
	const auto it = s_trans.find(d->id);
	if (it == s_trans.end()) { return 0.f; }
	const bool up = it->second.to == ORBIT;
	const int age = static_cast<int>(time - it->second.start);
	constexpr float HIGH = 2200.f;   // so hoch steigt er aus dem Bild (bzw. kommt von dort)
	constexpr float DEEP = 700.f;    // so tief liegt die verschwommene Untertextur unter den Inseln
	if (layerOfId(d->id) != it->second.to)
	{
		const float t = std::clamp(static_cast<float>(age) / TRANS_A_MS, 0.f, 1.f);
		return up ? HIGH * t * t : -DEEP * t * t;
	}
	const float t = std::clamp(static_cast<float>(age - static_cast<int>(TRANS_A_MS)) / TRANS_B_MS, 0.f, 1.f);
	const float r = (1.f - t) * (1.f - t);
	return up ? -DEEP * r : HIGH * r;
}

void requestLayer(DROID *d, Layer target)
{
	if (d == nullptr || d->died || target == UNDERGROUND || !modlayermap::worldsExist() || !isFlier(d)) { return; }
	if (layerOfId(d->id) == target || layerOfId(d->id) == UNDERGROUND || s_trans.count(d->id)) { return; }
	if (target == ORBIT && d->droidType == DROID_CYBORG_CONSTRUCT) { return; }   // (20:59) Cyborg-Pioniere kommen nur mit der Rakete hinauf
	s_trans[d->id] = Transition{target, gameTime};
	orderDroid(d, DORDER_STOP, ModeImmediate);
}

bool inTransition(const DROID *d)
{
	return d != nullptr && !s_trans.empty() && s_trans.count(d->id) != 0;
}

void sendSelectedFliers(Layer target)
{
	if (target == UNDERGROUND || selectedPlayer >= MAX_PLAYERS || !modlayermap::worldsExist()) { return; }
	for (DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d->died != 0 || !d->selected || !isFlier(d) || layerOfId(d->id) == target) { continue; }
		s_flyReq[d->id] = target;
	}
}

void inheritFromBuilder(const STRUCTURE *psStruct, const DROID *psBuilder)
{
	// Die Ebene des Bauwerks steht schon fest: buildStructureDir() legt es in der Karte an, die gerade aktiv ist (= die des Baumeisters,
	// weil der Bauauftrag im Schritt des Baumeisters ausgefuehrt wird) und merkt sich die Ebene; hier ist nichts mehr zu tun.
	(void)psStruct;
	(void)psBuilder;
}

void forget(uint32_t id)
{
	s_trans.erase(id);
	if (s_layerOf.empty() && s_cool.empty()) { return; }
	s_layerOf.erase(id);
	s_cool.erase(id);
}

void onUnderAttack(const BASE_OBJECT *victim)
{
	// reine Anzeige (Meldung + Ton), veraendert den Spielablauf nicht: nur fuer eigene Objekte, die gerade in einer anderen Ebene als der angezeigten liegen
	if (victim == nullptr || s_layerOf.empty() || victim->player != static_cast<int>(selectedPlayer)) { return; }
	const Layer l = layerOf(victim);
	if (l == s_current) { return; }
	static unsigned s_lastWarn[3] = { 0, 0, 0 };
	const unsigned now = realTime;
	if (s_lastWarn[l] != 0 && now >= s_lastWarn[l] && now - s_lastWarn[l] < 8000) { return; }
	s_lastWarn[l] = now;
	const char *name = l == ORBIT ? "Orbit" : (l == UNDERGROUND ? "Underground" : "Surface");
	addConsoleMessage((std::string("Under attack: ") + name).c_str(), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	audio_QueueTrack(victim->type == OBJ_STRUCTURE ? ID_SOUND_STRUCTURE_UNDER_ATTACK : ID_SOUND_UNIT_UNDER_ATTACK);
}

namespace
{
std::vector<int64_t> s_loadObjs;   // (17.11.) aus dem Spielstand: id, Ebene, id, Ebene, ...
std::vector<int> s_loadDrills;
bool s_loadPending = false;
}

void saveState(std::vector<int64_t> &objs, std::vector<int> &drills)
{
	objs.clear();
	std::vector<std::pair<uint32_t, Layer>> v(s_layerOf.begin(), s_layerOf.end());
	std::sort(v.begin(), v.end(), [](const std::pair<uint32_t, Layer> &a, const std::pair<uint32_t, Layer> &b) { return a.first < b.first; });
	for (const auto &e : v) { objs.push_back(e.first); objs.push_back(static_cast<int64_t>(e.second)); }
	drills = modlayermap::drillLog();
}

void loadState(const std::vector<int64_t> &objs, const std::vector<int> &drills)
{
	s_loadObjs = objs;
	s_loadDrills = drills;
	s_loadPending = !objs.empty() || !drills.empty();
}

void applyLoadedState()
{
	if (!s_loadPending || !modlayermap::worldsExist()) { return; }
	s_loadPending = false;
	for (size_t i = 0; i + 3 < s_loadDrills.size(); i += 4)
	{
		modlayermap::drillTile(s_loadDrills[i], s_loadDrills[i + 1], s_loadDrills[i + 2], s_loadDrills[i + 3]);
	}
	size_t moved = 0;
	for (size_t i = 0; i + 1 < s_loadObjs.size(); i += 2)
	{
		const uint32_t id = static_cast<uint32_t>(s_loadObjs[i]);
		const int l = static_cast<int>(s_loadObjs[i + 1]);
		if (l != UNDERGROUND && l != ORBIT) { continue; }
		bool done = false;
		for (int p = 0; p < MAX_PLAYERS && !done; p++)
		{
			for (DROID *d : gameWorld.objects.droids[p])
			{
				if (d->id == id && !d->died) { applyLayer(d, static_cast<Layer>(l)); done = true; break; }
			}
			if (done) { break; }
			for (STRUCTURE *s : gameWorld.objects.structures[p])
			{
				if (s->id == id && !s->died) { modMoveStructureToLayerMap(s, l); done = true; break; }
			}
		}
		if (!done)
		{
			for (FEATURE *f : gameWorld.objects.features[0])
			{
				if (f->id == id && !f->died) { assignLayer(f, static_cast<Layer>(l)); done = true; break; }
			}
		}
		moved += done ? 1 : 0;
	}
	debug(LOG_INFO, "MOD Ebenen aus dem Spielstand: %zu Objekte, %zu gebohrte Felder", moved, s_loadDrills.size() / 4);
	s_loadObjs.clear();
	s_loadDrills.clear();
}

void resetState()
{
	modlayerdig::reset();
	s_layerOf.clear();
	s_cool.clear();
	s_flyReq.clear();
	s_trans.clear();
	s_openedShafts.clear();
	s_loadObjs.clear(); s_loadDrills.clear(); s_loadPending = false;   // (17.11.) Spielstand
	modlayermap::reset();
	s_current = s_target = SURFACE;
	s_busy = false;
}

void shutdown()
{
	resetState();
	if (s_screen)
	{
		widgRemoveOverlayScreen(s_screen);
		s_screen = nullptr;
	}
	s_root = nullptr;
	s_buttons.clear();
	s_current = s_target = SURFACE;
	s_busy = false;
}

}
