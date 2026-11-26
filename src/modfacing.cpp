// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Ausrichten (Ziehen / Taste) und Fahren in der Verfolgerkamera, siehe modfacing.h

#include "modfacing.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "baseobject.h"
#include "console.h"
#include "display.h"
#include "display3d.h"
#include "droid.h"
#include "fpath.h"
#include "geometry.h"
#include "game_world.h"
#include "modcontrol.h"
#include "modrail.h"
#include "move.h"
#include "objmem.h"
#include "order.h"
#include "warcam.h"

#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

namespace modfacing
{
namespace
{
constexpr int DRAG_PX = 14;                 // ab so viel Bildpunkten Mausweg ist es Ausrichten statt Klick (wie der Klick-Schutz)

struct Pending
{
	int player = 0;
	bool look = false;                      // true: zum Punkt (tx, ty) schauen, sonst parallel in Richtung (vx, vy)
	int tx = 0, ty = 0;
	int vx = 0, vy = 0;
	DROID_ORDER orderType = DORDER_NONE;    // Befehl beim Ausrichten (faehrt die Einheit, dreht sie erst nach der Ankunft)
	Vector2i orderPos = Vector2i(0, 0);
	bool awaitMove = false;                 // (26.11.) Rechtsklick-Ziehen: der Fahrbefehl kommt erst mit dem naechsten Spieltakt an
	UDWORD since = 0;
};
std::unordered_map<uint32_t, Pending> s_pending;

// Ziehen
bool s_armed = false;                       // linke Taste auf einer gewaehlten Einheit gedrueckt
bool s_active = false;                      // weit genug gezogen: es wird ausgerichtet
int s_pressX = 0, s_pressY = 0;
Vector2i s_anchor(0, 0), s_world(0, 0);

// (Wunsch 26.11.) Rechtsklick-Ziehen: Fahrbefehl zum Druckpunkt, Ziehrichtung = Blickrichtung nach der Ankunft
bool s_rArmed = false, s_rActive = false;
int s_rPressX = 0, s_rPressY = 0;
Vector2i s_rWorld(0, 0), s_rCur(0, 0);

// Fahren
bool s_driving = false;
uint32_t s_driveId = 0;
int s_drivePlayer = 0;
int s_fwd = 0, s_turn = 0, s_lastFwd = 9, s_lastTurn = 9;
UDWORD s_seen = 0, s_lastSet = 0;

DROID *findOwn(int player, uint32_t id)
{
	if (player < 0 || player >= MAX_PLAYERS) { return nullptr; }
	for (DROID *d : gameWorld.objects.droids[player])
	{
		if (d != nullptr && d->id == id && !d->died) { return d; }
	}
	return nullptr;
}

bool canTurn(const DROID *d)
{
	return d != nullptr && !d->died && !d->isVtol() && !modrail::isTrain(d);
}

/// Befehle, bei denen die Einheit "steht" (dann darf sie sich drehen)
bool idleOrder(DROID_ORDER t)
{
	return t == DORDER_NONE || t == DORDER_HOLD || t == DORDER_GUARD || t == DORDER_STOP;
}

void request(DROID *d, bool look, int x, int y)
{
	if (!canTurn(d)) { return; }
	Pending p;
	p.player = static_cast<int>(d->player);
	p.look = look;
	if (look) { p.tx = x; p.ty = y; } else { p.vx = x; p.vy = y; }
	p.orderType = d->order.type;
	p.orderPos = d->order.pos;
	s_pending[d->id] = p;
	modcontrol::onPlayerOrder(d);   // Spieler-Befehl: die Einheit handelt eine Weile nicht selbststaendig (kein Wegdrehen)
}

/// Zielpunkt fuer die Drehung (moveTurnDroid braucht einen Punkt)
Vector2i turnPoint(const DROID *d, const Pending &p)
{
	if (p.look) { return Vector2i(p.tx, p.ty); }
	return d->pos.xy() + Vector2i(p.vx, p.vy);
}

void forSelected(const std::function<void(DROID *)> &fn)
{
	if (selectedPlayer >= MAX_PLAYERS) { return; }
	for (DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d != nullptr && !d->died && d->selected) { fn(d); }
	}
}

void thickLine(int x0, int y0, int x1, int y1, PIELIGHT c)
{
	for (int o = -1; o <= 1; ++o)
	{
		iV_Line(x0 + o, y0, x1 + o, y1, c);
		iV_Line(x0, y0 + o, x1, y1 + o, c);
	}
}

void arrow(int x0, int y0, int x1, int y1, PIELIGHT c)
{
	const double dx = x1 - x0, dy = y1 - y0;
	const double len = std::sqrt(dx * dx + dy * dy);
	if (len < 4.0) { return; }
	const double ux = dx / len, uy = dy / len;
	const double head = std::min(16.0, len * 0.4);
	thickLine(x0, y0, x1, y1, c);
	for (int s = -1; s <= 1; s += 2)
	{
		const double hx = x1 - ux * head + (-uy) * head * 0.55 * s;
		const double hy = y1 - uy * head + ux * head * 0.55 * s;
		thickLine(x1, y1, static_cast<int>(std::lround(hx)), static_cast<int>(std::lround(hy)), c);
	}
}

void stopDriving()
{
	if (!s_driving) { return; }
	s_driving = false;
	DROID *d = findOwn(s_drivePlayer, s_driveId);
	if (d != nullptr && d->sMove.Status != MOVEINACTIVE) { moveStopDroid(d); }
}

void steerNow(DROID *d)
{
	const uint16_t h = d->rot.direction;
	const Vector2i pos = d->pos.xy();
	const auto ahead = [](uint16_t a, int r) { return Vector2i(iSinR(a, r), iCosR(a, r)); };
	if (s_fwd > 0)
	{
		// Richtung waechst gegen den Uhrzeigersinn (Blick von oben): rechts lenken = kleinere Richtung
		const uint16_t a = static_cast<uint16_t>(h - s_turn * DEG(35));
		const Vector2i t = pos + ahead(a, 3 * 128);
		fpathSetDirectRoute(d, t.x, t.y);
		d->sMove.Status = MOVENAVIGATE;
		d->sMove.pathIndex = 0;
	}
	else if (s_fwd < 0)
	{
		// zurueck: kurzes Ziel hinter der Einheit (der Rueckwaertsgang greift fuer nahe Ziele), lenken wie beim Auto
		const uint16_t a = static_cast<uint16_t>(h + s_turn * DEG(35));
		const Vector2i t = pos - ahead(a, 3 * 64);
		fpathSetDirectRoute(d, t.x, t.y);
		d->sMove.Status = MOVENAVIGATE;
		d->sMove.pathIndex = 0;
	}
	else if (s_turn != 0)
	{
		const uint16_t a = static_cast<uint16_t>(h - s_turn * DEG(60));
		const Vector2i t = pos + ahead(a, 2 * 128);
		moveTurnDroid(d, t.x, t.y);
	}
	else if (d->sMove.Status != MOVEINACTIVE)
	{
		moveStopDroid(d);
	}
	s_lastSet = realTime;
	s_lastFwd = s_fwd;
	s_lastTurn = s_turn;
}
}

bool processMouse(bool overUi, const Vector2i &world)
{
	if (!s_armed)
	{
		// (Wunsch 22.11. 15:47 "Doppelklick auf Einheiten ... funktioniert nicht mehr"): der zweite Klick eines Doppelklicks gehoert WZ
		// (alle gleichen Einheiten im Bild waehlen) - nicht zum Ausrichten
		if (!mousePressed(MOUSE_LMB) || mouseDClicked(MOUSE_LMB) || overUi || ctrlShiftDown())
		{
			return false;
		}
		BASE_OBJECT *o = mouseTarget();
		if (o == nullptr || o->type != OBJ_DROID || o->player != selectedPlayer || !static_cast<DROID *>(o)->selected)
		{
			return false;
		}
		s_armed = true;
		s_active = false;
		s_pressX = mouseX();
		s_pressY = mouseY();
		s_anchor = o->pos.xy();
		s_world = world;
		return true;   // kein Auswahlrahmen, solange die Taste auf der gewaehlten Einheit gehalten wird
	}
	if (mouseDown(MOUSE_RMB))
	{
		// rechte Taste bricht ab
		s_armed = false;
		s_active = false;
		return true;
	}
	const int dx = mouseX() - s_pressX, dy = mouseY() - s_pressY;
	if (!s_active && dx * dx + dy * dy >= DRAG_PX * DRAG_PX)
	{
		s_active = true;
	}
	if (s_active)
	{
		s_world = world;
	}
	if (mouseReleased(MOUSE_LMB) || !mouseDown(MOUSE_LMB))
	{
		const bool wasActive = s_active;
		s_armed = false;
		s_active = false;
		if (!wasActive)
		{
			return false;   // nur ein Klick: WZ waehlt die Einheit wie gewohnt
		}
		const int vx = s_world.x - s_anchor.x, vy = s_world.y - s_anchor.y;
		const double len = std::sqrt(static_cast<double>(vx) * vx + static_cast<double>(vy) * vy);
		if (len >= 24.0)
		{
			const int nx = static_cast<int>(std::lround(vx * 512.0 / len)), ny = static_cast<int>(std::lround(vy * 512.0 / len));
			int n = 0;
			forSelected([&](DROID *d) { request(d, false, nx, ny); ++n; });
			if (n > 0)
			{
				addConsoleMessage(n == 1 ? _("Unit turns to the new direction") : _("Units turn to the new direction"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
			}
		}
		return true;   // Ausrichten: kein Klick dazu
	}
	return true;
}

bool processRightDrag(bool overUi, bool rightClickOrders, const Vector2i &world)
{
	if (!s_rArmed)
	{
		if (!rightClickOrders || !mousePressed(MOUSE_RMB) || overUi || ctrlShiftDown() || mouseTarget() != nullptr)
		{
			return false;
		}
		bool any = false;
		forSelected([&](DROID *d) { if (canTurn(d)) { any = true; } });
		if (!any)
		{
			return false;
		}
		s_rArmed = true;
		s_rActive = false;
		s_rPressX = mouseX();
		s_rPressY = mouseY();
		s_rWorld = world;
		s_rCur = world;
		return false;   // noch ein gewoehnlicher Rechtsklick, bis gezogen wird
	}
	if (mouseDown(MOUSE_LMB))
	{
		// linke Taste bricht ab
		s_rArmed = false;
		s_rActive = false;
		return true;
	}
	const int dx = mouseX() - s_rPressX, dy = mouseY() - s_rPressY;
	if (!s_rActive && dx * dx + dy * dy >= DRAG_PX * DRAG_PX)
	{
		s_rActive = true;
	}
	if (s_rActive)
	{
		s_rCur = world;
	}
	if (mouseReleased(MOUSE_RMB) || !mouseDown(MOUSE_RMB))
	{
		const bool wasActive = s_rActive;
		s_rArmed = false;
		s_rActive = false;
		if (!wasActive)
		{
			return false;   // nur ein Klick: WZ gibt den Befehl wie gewohnt
		}
		// Fahrbefehl zum Druckpunkt, danach in Ziehrichtung drehen
		orderSelectedLoc(selectedPlayer, static_cast<UDWORD>(std::max(0, s_rWorld.x)), static_cast<UDWORD>(std::max(0, s_rWorld.y)), false);
		const int vx = s_rCur.x - s_rWorld.x, vy = s_rCur.y - s_rWorld.y;
		const double len = std::sqrt(static_cast<double>(vx) * vx + static_cast<double>(vy) * vy);
		if (len >= 24.0)
		{
			const int nx = static_cast<int>(std::lround(vx * 512.0 / len)), ny = static_cast<int>(std::lround(vy * 512.0 / len));
			forSelected([&](DROID *d) {
				if (!canTurn(d)) { return; }
				request(d, false, nx, ny);
				auto it = s_pending.find(d->id);
				if (it != s_pending.end()) { it->second.awaitMove = true; it->second.since = gameTime; }
			});
		}
		assignDestTarget();
		audio_PlayTrack(ID_SOUND_SELECT);
		return true;
	}
	return s_rActive;   // beim Ziehen keine anderen Klicks (kein Auswahlrahmen)
}

void draw()
{
	if (s_rArmed && s_rActive)
	{
		// (26.11.) Rechtsklick-Ziehen: Pfeil vom Zielpunkt in die Blickrichtung
		const PIELIGHT c = pal_RGBA(120, 255, 150, 235);
		const PIELIGHT shadow = pal_RGBA(0, 0, 0, 160);
		arrow(s_rPressX + 1, s_rPressY + 2, mouseX() + 1, mouseY() + 2, shadow);
		arrow(s_rPressX, s_rPressY, mouseX(), mouseY(), c);
		return;
	}
	if (!s_armed || !s_active)
	{
		return;
	}
	const PIELIGHT c = pal_RGBA(120, 220, 255, 235);
	const PIELIGHT shadow = pal_RGBA(0, 0, 0, 160);
	// Hauptpfeil: vom Druckpunkt zur Maus
	arrow(s_pressX + 1, s_pressY + 2, mouseX() + 1, mouseY() + 2, shadow);
	arrow(s_pressX, s_pressY, mouseX(), mouseY(), c);
	// kleine Pfeile an jeder gewaehlten Einheit (Bildschirmrichtung des Zuges)
	const double dx = mouseX() - s_pressX, dy = mouseY() - s_pressY;
	const double len = std::sqrt(dx * dx + dy * dy);
	if (len < 1.0)
	{
		return;
	}
	const double ux = dx / len, uy = dy / len;
	forSelected([&](DROID *d) {
		if (!DrawnInLastFrame(d->sDisplay.frameNumber)) { return; }
		const int x0 = static_cast<int>(d->sDisplay.screenX), y0 = static_cast<int>(d->sDisplay.screenY);
		arrow(x0, y0, x0 + static_cast<int>(ux * 28), y0 + static_cast<int>(uy * 28), c);
	});
}

void faceCursor()
{
	const Vector2i p = mousePos;
	int n = 0;
	forSelected([&](DROID *d) { request(d, true, p.x, p.y); ++n; });
	if (n > 0)
	{
		addConsoleMessage(n == 1 ? _("Unit turns toward the cursor") : _("Units turn toward the cursor"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
}

bool drive(int forward, int turn)
{
	DROID *d = getTrackingDroid();
	if (d == nullptr || d->died || d->player != selectedPlayer || !canTurn(d))
	{
		return false;
	}
	s_fwd = forward > 0 ? 1 : (forward < 0 ? -1 : 0);
	s_turn = turn > 0 ? 1 : (turn < 0 ? -1 : 0);
	s_seen = realTime;
	if (!s_driving || s_driveId != d->id)
	{
		stopDriving();
		orderDroid(d, DORDER_STOP, ModeImmediate);   // laufenden Befehl beenden: die Einheit gehoert jetzt den Tasten
		s_driving = true;
		s_driveId = d->id;
		s_drivePlayer = static_cast<int>(d->player);
		s_lastFwd = s_lastTurn = 9;
		s_lastSet = 0;
		s_pending.erase(d->id);
	}
	modcontrol::onPlayerOrder(d);
	if (realTime - s_lastSet >= 120 || s_fwd != s_lastFwd || s_turn != s_lastTurn)
	{
		steerNow(d);
	}
	return true;
}

void tick()
{
	// Fahren endet, sobald keine Fahrtaste mehr kommt (oder die Verfolgerkamera aus ist)
	if (s_driving && (realTime - s_seen > 150 || !getWarCamStatus()))
	{
		stopDriving();
	}
	for (auto it = s_pending.begin(); it != s_pending.end();)
	{
		const Pending &p = it->second;
		DROID *d = findOwn(p.player, it->first);
		if (d == nullptr || !canTurn(d))
		{
			it = s_pending.erase(it);
			continue;
		}
		// (26.11.) Rechtsklick-Ziehen: auf den eigenen Fahrbefehl warten (hoechstens 3 s), dann gilt er als "der" Befehl
		if (p.awaitMove)
		{
			if (d->order.type == DORDER_MOVE || d->order.type == DORDER_SCOUT)
			{
				it->second.awaitMove = false;
				it->second.orderType = d->order.type;
				it->second.orderPos = d->order.pos;
			}
			else if (gameTime - p.since < 3000)
			{
				++it;
				continue;
			}
			else
			{
				it->second.awaitMove = false;
			}
		}
		// neuer Befehl seit dem Ausrichten (nicht nur "angekommen") -> Ausrichten verworfen
		if (!idleOrder(d->order.type) && (d->order.type != p.orderType || d->order.pos != p.orderPos))
		{
			it = s_pending.erase(it);
			continue;
		}
		// noch unterwegs oder im Gefecht: spaeter
		if (!idleOrder(d->order.type) || d->action != DACTION_NONE
		    || (d->sMove.Status != MOVEINACTIVE && d->sMove.Status != MOVETURNTOTARGET))
		{
			++it;
			continue;
		}
		const Vector2i t = turnPoint(d, p);
		const uint16_t want = calcDirection(d->pos.x, d->pos.y, t.x, t.y);
		const int diff = std::abs(static_cast<int16_t>(static_cast<uint16_t>(want - d->rot.direction)));
		if (diff <= DEG(2) || (t - d->pos.xy()) == Vector2i(0, 0))
		{
			it = s_pending.erase(it);
			continue;
		}
		if (d->sMove.Status != MOVETURNTOTARGET || d->sMove.target != t)
		{
			moveTurnDroid(d, static_cast<UDWORD>(std::max(0, t.x)), static_cast<UDWORD>(std::max(0, t.y)));
		}
		++it;
	}
}

void reset()
{
	s_pending.clear();
	s_armed = s_active = false;
	s_rArmed = s_rActive = false;
	s_driving = false;
}
}
