// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Combot-Auswahlfeld im Metal-Fatigue-Stil.
// Oben das blaue Combot-Diagramm (Torso, beide Arme, Beine): Farbe = Zustand des Teils, fehlende Teile werden abgedunkelt.
// Rechts daneben vier Textzeilen mit Teilnamen und Waffen. Darunter die zwoelf MF-Tasten:
//   Waffeneinsatz: MELEE / ALL / RANGED     Verhalten: STAY / TETHER / PURSUE
//   Befehle:       GUARD / ATTACK / MOVE    PATROL / RETFIRE / STOP

#include "modpanel.h"
#include "modui.h"
#include "modcombot.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/netplay/netplay.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/button.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"

#include "console.h"
#include "droid.h"
#include "game_world.h"
#include "hci.h"
#include "order.h"
#include "orderdef.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <memory>
#include <vector>

namespace modpanel
{

namespace
{

constexpr int PANEL_W = 161;
constexpr int PANEL_H = 247;       // sichtbarer Teil des Bildes
constexpr int BTN_W = 51;
constexpr int BTN_H = 28;
constexpr int BTN_ROWS = 4;
constexpr int BTN_GAP = 1;
constexpr int TOTAL_H = PANEL_H + BTN_ROWS * (BTN_H + BTN_GAP) + 2;

enum Cmd
{
	C_MELEE, C_ALL, C_RANGED,
	C_STAY, C_TETHER, C_PURSUE,
	C_GUARD, C_ATTACK, C_MOVE,
	C_PATROL, C_RETFIRE, C_STOP,
	C_COUNT
};

std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_root;
std::vector<std::shared_ptr<W_BUTTON>> s_buttons;
bool s_suppressed = false;

DROID *currentSel()
{
	if (selectedPlayer >= MAX_PLAYERS) { return nullptr; }
	for (DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d != nullptr && !d->died && d->selected && modcombot::isModular(d)) { return d; }
	}
	return nullptr;
}

template <typename F>
void forEachSelected(F f)
{
	if (selectedPlayer >= MAX_PLAYERS) { return; }
	for (DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d != nullptr && !d->died && d->selected) { f(d); }
	}
}

bool cmdActive(Cmd c, const DROID *d)
{
	if (d == nullptr) { return false; }
	switch (c)
	{
	case C_MELEE: return modcombot::weaponMode(d) == modcombot::MODE_MELEE;
	case C_ALL: return modcombot::weaponMode(d) == modcombot::MODE_ALL;
	case C_RANGED: return modcombot::weaponMode(d) == modcombot::MODE_RANGED;
	case C_STAY: return secondaryGetState(d, DSO_HALTTYPE, ModeQueue) == DSS_HALT_HOLD;
	case C_TETHER: return secondaryGetState(d, DSO_HALTTYPE, ModeQueue) == DSS_HALT_GUARD;
	case C_PURSUE: return secondaryGetState(d, DSO_HALTTYPE, ModeQueue) == DSS_HALT_PURSUE;
	case C_GUARD: return d->order.type == DORDER_GUARD;
	case C_ATTACK: return secondaryGetState(d, DSO_ATTACK_LEVEL, ModeQueue) == DSS_ALEV_ALWAYS;
	case C_MOVE: return secondaryGetState(d, DSO_ATTACK_LEVEL, ModeQueue) == DSS_ALEV_NEVER;
	case C_PATROL: return secondaryGetState(d, DSO_PATROL, ModeQueue) == DSS_PATROL_SET;
	case C_RETFIRE: return secondaryGetState(d, DSO_ATTACK_LEVEL, ModeQueue) == DSS_ALEV_ATTACKED;
	default: return false;
	}
}

void setSecondary(SECONDARY_ORDER sec, SECONDARY_STATE st)
{
	forEachSelected([&](DROID *d) { secondarySetState(d, gameWorld.objects, sec, st); });
}

void runCmd(Cmd c)
{
	switch (c)
	{
	case C_MELEE:
	case C_ALL:
	case C_RANGED:
	{
		if (NetPlay.bComms)
		{
			// der Waffenmodus ist nicht ueber das Netzwerk abgeglichen -> nur Einzelspieler / Skirmish gegen KI
			addConsoleMessage("Weapon mode is only available in single-player and skirmish games.", DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
			return;
		}
		const modcombot::WeaponMode m = c == C_MELEE ? modcombot::MODE_MELEE : (c == C_RANGED ? modcombot::MODE_RANGED : modcombot::MODE_ALL);
		forEachSelected([&](DROID *d) { modcombot::setWeaponMode(d, m); });
		break;
	}
	case C_STAY: setSecondary(DSO_HALTTYPE, DSS_HALT_HOLD); break;
	case C_TETHER: setSecondary(DSO_HALTTYPE, DSS_HALT_GUARD); break;
	case C_PURSUE: setSecondary(DSO_HALTTYPE, DSS_HALT_PURSUE); break;
	case C_GUARD: forEachSelected([&](DROID *d) { orderDroid(d, DORDER_GUARD, ModeQueue); }); break;
	case C_ATTACK: setSecondary(DSO_ATTACK_LEVEL, DSS_ALEV_ALWAYS); break;
	case C_MOVE: setSecondary(DSO_ATTACK_LEVEL, DSS_ALEV_NEVER); break;
	case C_RETFIRE: setSecondary(DSO_ATTACK_LEVEL, DSS_ALEV_ATTACKED); break;
	case C_PATROL:
	{
		const DROID *d = currentSel();
		const bool on = d != nullptr && secondaryGetState(d, DSO_PATROL, ModeQueue) == DSS_PATROL_SET;
		setSecondary(DSO_PATROL, on ? DSS_NONE : DSS_PATROL_SET);
		break;
	}
	case C_STOP: forEachSelected([&](DROID *d) { orderDroid(d, DORDER_STOP, ModeQueue); }); break;
	default: break;
	}
}

modui::Img cmdImage(Cmd c, bool on)
{
	switch (c)
	{
	case C_MELEE: return on ? modui::MELEE_ON : modui::MELEE_OFF;
	case C_ALL: return on ? modui::ALL_ON : modui::ALL_OFF;
	case C_RANGED: return on ? modui::RANGED_ON : modui::RANGED_OFF;
	case C_STAY: return on ? modui::STAY_ON : modui::STAY_OFF;
	case C_TETHER: return on ? modui::TETHER_ON : modui::TETHER_OFF;
	case C_PURSUE: return on ? modui::PURSUE_ON : modui::PURSUE_OFF;
	case C_GUARD: return modui::B_GUARD;
	case C_ATTACK: return modui::B_ATTACK;
	case C_MOVE: return modui::B_MOVE;
	case C_PATROL: return modui::B_PATROL;
	case C_RETFIRE: return modui::B_RETFIRE;
	default: return modui::B_STOP;
	}
}

const char *cmdTip(Cmd c)
{
	switch (c)
	{
	case C_MELEE: return "Melee weapons only";
	case C_ALL: return "Use all weapons";
	case C_RANGED: return "Ranged weapons only";
	case C_STAY: return "Stay: hold position, never chase";
	case C_TETHER: return "Tether: chase only a short distance";
	case C_PURSUE: return "Pursue: chase the target";
	case C_GUARD: return "Guard this position";
	case C_ATTACK: return "Attack: fire at will";
	case C_MOVE: return "Move: do not fire";
	case C_PATROL: return "Patrol: next move order patrols between both points";
	case C_RETFIRE: return "Return fire: shoot only when attacked";
	default: return "Stop";
	}
}

// ---------------------------------------------------------------- Zeichnen

void drawCmdButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const float s = modui::uiScale();
	const Cmd c = static_cast<Cmd>(psWidget->UserData);
	const int x0 = static_cast<int>(xOffset) + psWidget->x();
	const int y0 = static_cast<int>(yOffset) + psWidget->y();
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const DROID *d = currentSel();
	const bool on = cmdActive(c, d);
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;
	const modui::Img img = cmdImage(c, on);
	const int w = static_cast<int>(modui::imgWidth(img) * s), h = static_cast<int>(modui::imgHeight(img) * s);
	modui::drawSized(img, x0, y0 + (down ? 1 : 0), w, h, down ? 200 : 255);
	if (c >= C_GUARD)
	{
		if (on) { iV_Box(x0 + 1, y0 + 1, x0 + w - 2, y0 + h - 2, modui::cyan(230)); }
	}
	if (hl) { iV_Box(x0, y0, x0 + w - 1, y0 + h - 1, pal_RGBA(255, 255, 255, 160)); }
}

void drawLimb(int px, int py, float s, int pct, int cx, int cy, int r, int rx0, int ry0, int rx1, int ry1)
{
	const int X = px, Y = py;
	if (pct < 0)
	{
		// Teil fehlt: Schemen abdunkeln und rotes Kreuz
		pie_BoxFill_alpha(X + static_cast<int>(rx0 * s), Y + static_cast<int>(ry0 * s), X + static_cast<int>(rx1 * s), Y + static_cast<int>(ry1 * s), pal_RGBA(6, 12, 30, 225));
		const int cxs = X + static_cast<int>(cx * s), cys = Y + static_cast<int>(cy * s), rr = static_cast<int>(r * s * 0.7f);
		const PIELIGHT red = pal_RGBA(255, 50, 40, 255);
		iV_Line(cxs - rr, cys - rr, cxs + rr, cys + rr, red);
		iV_Line(cxs - rr, cys + rr, cxs + rr, cys - rr, red);
		return;
	}
	const PIELIGHT col = modui::healthColour(pct);
	PIELIGHT fill = col;
	fill.byte.a = 120;
	const int cxs = X + static_cast<int>(cx * s), cys = Y + static_cast<int>(cy * s);
	modui::disc(cxs, cys, static_cast<int>(r * s) - 1, fill);
	modui::ring(cxs, cys, static_cast<int>(r * s), col);
}

std::string line(const char *prefix, const std::string &name, int pct)
{
	std::string t = prefix;
	t += name.empty() ? std::string("--") : name;
	if (pct >= 0 && !name.empty()) { t += " " + std::to_string(pct) + "%"; }
	return t;
}

void drawPanel(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	if (!modui::ready()) { return; }
	const float s = modui::uiScale();
	const int px = static_cast<int>(xOffset) + psWidget->x();
	const int py = static_cast<int>(yOffset) + psWidget->y();
	modui::drawSized(modui::PANEL, px, py, static_cast<int>(modui::imgWidth(modui::PANEL) * s), static_cast<int>(modui::imgHeight(modui::PANEL) * s));
	const DROID *d = currentSel();
	if (d == nullptr) { return; }

	const int pct[4] = { modcombot::partHealth(d, modcombot::SLOT_TORSO), modcombot::partHealth(d, modcombot::SLOT_RARM),
	                     modcombot::partHealth(d, modcombot::SLOT_LARM), modcombot::partHealth(d, modcombot::SLOT_LEGS) };
	const modcombot::Part *pT = modcombot::partInSlot(d, modcombot::SLOT_TORSO);
	const modcombot::Part *pG = modcombot::partInSlot(d, modcombot::SLOT_LEGS);
	const std::string names[4] = { pT ? pT->file : std::string(), modcombot::weaponLabel(d, modcombot::SLOT_RARM),
	                               modcombot::weaponLabel(d, modcombot::SLOT_LARM), pG ? pG->file : std::string() };
	drawBlueprint(px, py, s, pct, names);
}

// ---------------------------------------------------------------- Aufbau

void ensureScreen()
{
	if (s_screen) { return; }
	auto screen = W_SCREEN::make();
	auto root = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(root);
	root->displayFunction = drawPanel;
	root->hide();
	s_buttons.clear();
	for (int i = 0; i < C_COUNT; ++i)
	{
		auto b = std::make_shared<W_BUTTON>();
		root->attach(b);
		b->displayFunction = drawCmdButton;
		b->UserData = i;
		b->setTip(cmdTip(static_cast<Cmd>(i)));
		const Cmd c = static_cast<Cmd>(i);
		b->addOnClickHandler([c](W_BUTTON &) { widgScheduleTask([c]() { runCmd(c); }); });
		s_buttons.push_back(b);
	}
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_root = root;
}

void layout()
{
	const float s = modui::uiScale();
	const int w = static_cast<int>(PANEL_W * s), h = static_cast<int>(TOTAL_H * s);
	const int x = static_cast<int>(screenWidth) - w - 4;
	const int y = std::max(40, (static_cast<int>(screenHeight) - h) / 2);
	s_root->setGeometry(x, y, w, h);
	for (int i = 0; i < C_COUNT; ++i)
	{
		const int row = i / 3, col = i % 3;
		s_buttons[static_cast<size_t>(i)]->setGeometry(static_cast<int>((5 + col * (BTN_W + 1)) * s), static_cast<int>((PANEL_H + 2 + row * (BTN_H + BTN_GAP)) * s),
		                                               static_cast<int>(BTN_W * s), static_cast<int>(BTN_H * s));
	}
}

} // namespace

void drawBlueprint(int px, int py, float s, const int pct[4], const std::string names[4], int sizeClass)
{
	const int hT = pct[0], hR = pct[1], hL = pct[2], hG = pct[3];
	// Zeichnung im Bild (Original-Pixel): Torso/Kopf (45,47), rechter Arm = linke Bildseite (26,59), linker Arm (64,59), Beine (45,76)
	drawLimb(px, py, s, hR, 26, 59, 8, 17, 25, 36, 75);
	drawLimb(px, py, s, hL, 64, 59, 8, 54, 25, 73, 75);
	drawLimb(px, py, s, hG, 45, 76, 9, 29, 80, 62, 113);
	drawLimb(px, py, s, hT, 45, 47, 9, 0, 0, 0, 0);

	// vier Textzeilen im blauen Feld
	const std::string rows[4] = { line("T ", names[0], hT), line("R ", names[1], hR), line("L ", names[2], hL), line("G ", names[3], hG) };
	const int maxW = static_cast<int>(66 * s) - 2;
	for (int i = 0; i < 4; ++i)
	{
		const int rowTop = py + static_cast<int>((21 + i * 9.f) * s);
		const PIELIGHT col = (pct[i] < 0) ? pal_RGBA(90, 120, 150, 255) : modui::healthColour(pct[i]);
		modui::text(modui::fit(rows[i], maxW, sizeClass), px + static_cast<int>(73 * s), rowTop, col, sizeClass);
	}

	// drei Felder unten: Waffe rechter Arm / Torso / Waffe linker Arm (Kuerzel + Farbe)
	struct Slot { int x; int pct; std::string label; };
	const Slot slots[3] = { { 16, hR, names[1] }, { 67, hT, names[0] }, { 118, hL, names[2] } };
	for (const Slot &sl : slots)
	{
		const int bx = px + static_cast<int>(sl.x * s), by = py + static_cast<int>(175 * s);
		const int bw = static_cast<int>(26 * s), bh = static_cast<int>(24 * s);
		if (sl.pct < 0)
		{
			pie_BoxFill_alpha(bx, by, bx + bw, by + bh, pal_RGBA(6, 12, 30, 200));
			continue;
		}
		PIELIGHT f = modui::healthColour(sl.pct);
		f.byte.a = 70;
		pie_BoxFill_alpha(bx, by, bx + bw, by + bh, f);
		std::string abbr = sl.label.substr(0, 3);
		for (char &ch : abbr) { ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); }
		const int tw = modui::textWidth(abbr, sizeClass);
		modui::text(abbr, bx + (bw - tw) / 2, by + static_cast<int>(7 * s), pal_RGBA(255, 255, 255, 255), sizeClass);
	}
}

void setSuppressed(bool suppressed)
{
	s_suppressed = suppressed;
}

void tick()
{
	if (!modcombot::ready() || !modui::ready()) { return; }
	const bool want = !s_suppressed && currentSel() != nullptr;
	if (!want && !s_screen) { return; }
	ensureScreen();
	if (!s_root) { return; }
	s_root->show(want);
	if (want) { layout(); }
}

void shutdown()
{
	if (s_screen)
	{
		widgRemoveOverlayScreen(s_screen);
		s_screen = nullptr;
	}
	s_root = nullptr;
	s_buttons.clear();
	modui::shutdown();
}

}
