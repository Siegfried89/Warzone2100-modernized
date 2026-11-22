// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: "Frei malen" - Schlachtfeld-Planung durch Zeichnen auf der Karte (siehe modpaint.h)

#include "modpaint.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/button.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"

#include "droid.h"
#include "game_world.h"
#include "hci.h"
#include "map.h"
#include "modarty.h"
#include "modbuild.h"
#include "modcombot.h"
#include "moddesigner.h"
#include "modlayer.h"
#include "modui.h"
#include "modglow.h"
#include "modrosette.h"
#include "multiplay.h"
#include "modincoming.h"
#include "modrecover.h"
#include "modcrates.h"
#include "modcontrol.h"
#include "console.h"
#include "feature.h"
#include "featuredef.h"
#include "mapgrid.h"
#include "objmem.h"
#include "radar.h"
#include "modcover.h"   // MOD 17.11.: Take cover
#include "modmines.h"   // MOD 17.11.: Minen raeumen
#include "modoil.h"     // MOD 19.11.: isInfantry (Minen raeumen im Gebiet)
#include "order.h"
#include "ordersource.h"
#include "stats.h"
#include "structure.h"
#include "transporter.h"
#include "group.h"
#include "ai.h"
#include "modtunnel.h"   // MOD 22.11.: Tunnel (Nydus)
#include "modferry.h"    // MOD 22.11.: SupCom-Transportpfeil fuer die Fahrschleifen des Planers
#include "modlayerdig.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace modpaint
{
namespace
{
enum Mode
{
	M_MOVE = 0, M_RALLY, M_ATTACK, M_DEFEND, M_ARTYBASE, M_ARTY, M_AIR, M_COMBOT, M_SPREAD, M_RECOVER, M_COVER, M_TRAPS, M_MINES, M_OFFENSE, M_CLEARMINES, M_DEMOLISH, M_DEFNET, M_RAIL,
	M_STAGING, M_FERRY, M_BRIDGEHEAD, M_GO, M_CLEARPLAN, M_TUNNEL,   // (22.11.) Angriffsplaner + Tunnel; M_GO/M_CLEARPLAN = Sofort-Befehle
	M_COUNT   // M_SPREAD, M_COVER = Sofort-Befehle (kein Zeichnen)
};

struct ModeInfo
{
	const char *label;
	const char *tip;
	const char *cursor;
	unsigned char r, g, b;
	bool area;   // Gebiet (geschlossene Form) statt Linie
};

// MOD (Wunsch 21.11. 18:31): klarere/coolere Namen (Attack -> Attack Vector, Defend -> Defensive Line ...), Tooltips erklaeren genau,
// was der Befehl tut (Reihenfolge = enum Mode)
const ModeInfo kModes[M_COUNT] = {
	{ "Move", "Move: draw a path - the selected units drive exactly along it", "Move: drag a path (right click cancels)", 60, 220, 90, false },
	{ "Rally Point", "Rally Point: units gather at this spot (for example before an attack) and wait there", "Rally Point: drag around the rally point (right click cancels)", 255, 150, 40, false },
	{ "Attack Vector", "Attack Vector: draw the direction of attack. With selected units they advance along it right away and fight everything on the way. Without a selection it becomes part of the attack plan: the waves from the nearest bridgehead or assault line storm along it (vectors that start close together merge into one thick arrow that splits). Badge = priority (1 gets the most units), right click on it removes the vector", "Attack Vector: drag the direction of attack (right click cancels)", 235, 50, 40, false },
	// MOD (Wunsch 21.11. 00:21): "Die Defensive Line ist weiterhin nur zur Formation fuer Fahrzeuge, um sie an eine Frontlinie zu schicken
	// wie bisher, ohne Bauwerke" - gebaut wird nur noch ueber den eigenen Knopf (M_DEFNET)
	{ "Defensive Line", "Defensive Line: combat units take up positions along the line, facing the enemy side, and hold it. Nothing is built - use \"Trench Network\" for trenches", "Defensive Line: drag the line to hold (right click cancels)", 60, 130, 255, false },
	{ "Firebase", "Firebase: builders set up an artillery base here - sensor tower, artillery emplacements, wall ring, hedgehogs in front. Small circle = 4 emplacements, medium = 8, large = 12", "Firebase: drag a circle around the place - its size sets the base size (right click cancels)", 170, 120, 60, true },
	{ "Barrage", "Barrage: all artillery in range bombards this area", "Barrage: drag the target area (right click cancels)", 240, 200, 90, true },
	{ "Air Cover", "Air Cover: aircraft patrol over this area and attack every enemy that shows up there", "Air Cover: drag the patrol area (right click cancels)", 255, 235, 50, true },
	{ "Combot Hunt", "Combot Hunt: combots roam this area on their own and hunt down every enemy in it", "Combot Hunt: drag the hunting area (right click cancels)", 190, 90, 255, true },
	{ "Spread Out", "Spread Out: the selected units spread out evenly (against artillery and bombs)", "", 150, 200, 200, false },
	{ "Salvage Op", "Salvage Op: drag a circle around parts, artefacts and wrecks - an armed, escorted salvage team gathers at the base first, then recovers everything in it (wrecks are towed to the repair facility by flatbed trucks)", "Salvage Op: drag a circle around the salvage (right click cancels)", 120, 220, 255, true },
	{ "Take Cover", "Take Cover: the selected infantry (or all infantry, if none is selected) mans the nearest trenches, pits, sandbag positions and buildings", "", 200, 170, 110, false },
	// MOD (Wunsch 17.11. 18:12/18:13): Panzersperrenfeld (gruene Pyramiden) und Minenfeld
	{ "Tank Traps", "Tank Traps: drag an area - builders put a staggered field of tank traps into it (vehicles cannot pass)", "Tank Traps: drag the area (right click cancels)", 40, 200, 80, true },
	{ "Minefield", "Minefield: drag an area - builders lay hidden mines in it; they blow up enemy ground units that drive over them", "Minefield: drag the area (right click cancels)", 255, 90, 40, true },
	// MOD (Wunsch 17.11. 18:37): Offensivlinie (rot) - wie die Verteidigungslinie, aber auf Angriff ausgerichtet
	{ "Assault Line", "Assault Line: with selected units they mass on the line in two ranks, ready to storm forward (builders add a few short trenches and MG posts). Without a selection it becomes part of the attack plan: units are brought here, wait in a double row until the target strength is reached (badge, click to change) or you press Attack!, then storm along the attack vectors", "Assault Line: drag the line to mass on (right click cancels)", 240, 45, 45, false },
	// MOD (Wunsch 17.11. 19:36 / 19.11. 17:34): Raeumpanzer (Walze an) und Infanterie durchkaemmen das Gebiet in Bahnen
	{ "Mine Sweep", "Mine Sweep: drag a circle - mine flails (drum on) and infantry sweep the whole area lane by lane, also where no mines are known. Infantry spots mines within 2.5 tiles, disarms them (3 s each) and does not set off anti-tank mines; flails beat them off ahead", "Mine Sweep: drag a circle around the area to sweep (right click cancels)", 230, 200, 60, true },
	// MOD (Wunsch 19.11. 17:34 "Neue kreis ziehen = demolish alles in diesem Gebiet")
	{ "Demolish", "Demolish: drag a circle - builders tear down all your own structures in it (nearest first, spread over all builders)", "Demolish: drag a circle around what should be torn down (right click cancels)", 200, 90, 60, true },
	// MOD (Wunsch 21.11. 00:21 "Lieber einen neuen circle machen mit etwas wie baue Verteidigungsnetz"): das Grabennetz hat einen eigenen Befehl
	{ "Trench Network", "Trench Network: drag a line - builders dig a trench network along it (main trench, pits, MG posts, connecting trenches to the rear), front towards the enemy side. Combat units are not moved", "Trench Network: drag the line of the trenches (right click cancels)", 120, 190, 230, false },
	// MOD (Wunsch 21.11. 00:21, Zugsystem): Gleisstrecke frei zeichnen (gerade Stuecke gehen auch wie Mauern ueber das Baumenue)
	{ "Railway", "Railway: drag a route - builders lay a railway track along it (straight and diagonal, around obstacles, joining your existing tracks). Locomotives and wagons come from the train depot and run on the tracks", "Railway: drag the route of the track (right click cancels)", 175, 140, 95, false },
	// MOD (Wunsch 22.11. 01:11): Angriffsplaner - "Kein Micromanager. Indirekte Steuerung. Eher lenken, als alles selbst zu machen."
	{ "Staging Zone", "Staging Zone: drag a circle (or click) where your units gather - new units from the factories and idle units in the zone join the attack plan and are brought on to the bridgeheads (by ferry, by tunnel or on their own). Badge = priority 1-5 (click), right click on it removes the zone", "Staging Zone: drag a circle around the gathering area (right click cancels)", 70, 205, 215, true },
	{ "Ferry Beacon", "Ferry Beacon: click into a staging zone - your transports pick up the units waiting there and fly them to the bridgeheads in a loop. Clicked into a bridgehead it marks the landing spot. Placed by button, nothing is built. Right click on the beacon removes it", "Ferry Beacon: click into a staging zone (right click cancels)", 125, 100, 235, false },
	{ "Bridgehead", "Bridgehead: drag a circle near the enemy base - units are brought here and wait until the target strength is reached (second badge, click: 4 / 8 / 12 / 16 / 24 / 32 / Hold) or you press Attack!, then they storm along the attack vectors that start here. Priority badge: who gets units and transports first", "Bridgehead: drag a circle around the jump-off area (right click cancels)", 245, 115, 45, true },
	{ "Attack!", "Attack!: every bridgehead and assault line attacks now with the units that have arrived - the next wave keeps gathering", "", 255, 70, 55, false },
	{ "Clear Plan", "Clear Plan: click twice - removes all staging zones, beacons, bridgeheads, assault lines and vectors of the attack plan (units keep their last orders)", "", 160, 165, 175, false },
	{ "Dig Tunnel", "Dig Tunnel: drag a line from the entrance to the exit - a drill rig (selected, or the nearest one) digs a tunnel underground; send units onto a tunnel mouth (right click) and they come out at the other end. Easy to destroy: if a mouth falls, the tunnel caves in and everyone inside is lost", "Dig Tunnel: drag from the entrance to the exit (right click cancels)", 185, 140, 80, false },
};

// MOD (Wunsch 21.11. 17:59 "Die Befehlsbuttons besser nach Kategorie sortieren (aehnliches zu aehnlichem), ausserdem Icons zu jedem dazu"):
// vier Gruppen mit Ueberschrift, jeder Knopf mit eigenem Symbol in seiner Farbe; die Knoepfe sind dafuer breiter
// (Wunsch 22.11. 16:19 "Das Battle-Plan-Menue in der Groesse halbieren und generell alles unter Icon-Symbolen wie ein, zwei und drei
// Generalssternen verstecken"): oben fuenf Reiter mit Generalssternen (1 bis 5 Sterne = die fuenf Gruppen), darunter nur die Befehle der
// gewaehlten Gruppe als reine Symbol-Knoepfe (Name und Erklaerung im Tooltip). Etwa halb so breit wie vorher.
constexpr int BTN_W = 25;      // Symbol-Knopf (quadratisch)
constexpr int BTN_H = 25;
constexpr int GAP = 3;
constexpr int PAD = 4;
constexpr int COLS = 4;
constexpr int TITLE_H = 13;    // Titelzeile mit dem Namen der Gruppe
constexpr int TAB_H = 20;      // Reiter mit den Sternen
struct Group { const char *title; int n; int modes[8]; };
constexpr Group kGroups[] = {
	{ "Units",   4, { M_MOVE, M_RALLY, M_SPREAD, M_COVER, 0, 0, 0, 0 } },
	// (22.11.) Angriffsplaner: Zonen, Leuchtfeuer, Brueckenkopf, Angriffslinie, Vektoren, Tunnel, Angriff, Plan loeschen
	{ "Attack plan", 8, { M_STAGING, M_FERRY, M_BRIDGEHEAD, M_OFFENSE, M_ATTACK, M_TUNNEL, M_GO, M_CLEARPLAN } },
	{ "Combat",  4, { M_DEFEND, M_ARTY, M_AIR, M_COMBOT, 0, 0, 0, 0 } },
	{ "Build",   6, { M_DEFNET, M_ARTYBASE, M_TRAPS, M_MINES, M_RAIL, M_DEMOLISH, 0, 0 } },
	{ "Support", 2, { M_CLEARMINES, M_RECOVER, 0, 0, 0, 0, 0, 0 } },
};
constexpr int NGROUPS = static_cast<int>(sizeof(kGroups) / sizeof(kGroups[0]));
constexpr int groupRows(int n) { return (n + COLS - 1) / COLS; }
constexpr int maxRows()
{
	int r = 1;
	for (int g = 0; g < NGROUPS; ++g) { r = std::max(r, groupRows(kGroups[g].n)); }
	return r;
}
constexpr int TOTAL_W = COLS * BTN_W + (COLS - 1) * GAP + 2 * PAD;
constexpr int TAB_W = (TOTAL_W - 2 * PAD - (NGROUPS - 1) * 2) / NGROUPS;
constexpr int TOTAL_H = PAD + TITLE_H + TAB_H + GAP + maxRows() * (BTN_H + GAP) - GAP + PAD;
constexpr unsigned SHOW_MS = 25000;   // so lange bleibt eine Zeichnung sichtbar
constexpr int NEAR_TILES = 16;        // ohne Auswahl: passende Einheiten in diesem Umkreis
constexpr size_t MAX_UNITS = 40;

struct Stroke
{
	Mode mode;
	std::vector<Vector2i> pts;
	unsigned born = 0;
	std::vector<Vector2i> marks;   // Panzersperren / Minen: geplante Stellen
	int side = 0;                  // Verteidigungslinie: Seite der Querstriche (zum Feind)
};

std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_root;
std::vector<std::shared_ptr<W_BUTTON>> s_buttons;
std::vector<std::shared_ptr<W_BUTTON>> s_tabs;   // (22.11.) Reiter mit Generalssternen
int s_group = 1;                 // gewaehlte Gruppe (Start: Angriffsplan)
int s_mode = -1;                 // aktiver Zeichenmodus (-1 = keiner)
bool s_drawing = false;
std::vector<Vector2i> s_cur;     // Linie, die gerade gezeichnet wird
std::vector<Stroke> s_done;      // fertige Zeichnungen (verblassen)
// MOD (Wunsch 18.11. 00:59, berichtigt 02:44): "Wenn man Shift drueckt, lassen sich neue zeichnen, auch mehrere. Aber wenn man nicht Shift
// drueckt, dann wird sie ersetzt." - mit gedrueckter Umschalttaste kommt jede Zeichnung als WEITERE Linie dazu (eigener Marker, eigene
// Einheiten; der Zeichenmodus bleibt an); ohne Shift ersetzt eine neue Zeichnung die bisherigen Linien derselben Art.
bool s_append = false;           // Zeichnung mit Shift
std::vector<uint32_t> s_chain;   // Einheiten, die in der laufenden Shift-Folge schon eine Linie bekommen haben
// MOD (Wunsch 14.11., Infanterie baut): eigener WZ-Knopf direkt ueber der Rosette, sobald Infanterie mit Spaten gewaehlt ist -> oeffnet das WZ-Baumenue
std::shared_ptr<W_SCREEN> s_infScreen;
std::shared_ptr<W_CLICKFORM> s_infRoot;
std::shared_ptr<W_BUTTON> s_infBtn;
// (22.11.) Angriffsplaner (Abschnitt weiter unten)
unsigned s_planClearArmed = 0;   // "Clear Plan": erster Klick macht scharf, zweiter (binnen 4 s) loescht
namespace plan
{
void attackNow();
void clearAll();
bool hasElems();
void fromStroke(Mode m, const std::vector<Vector2i> &raw);
}
bool s_lastPlanned = false;   // die letzte Zeichnung wurde Teil des Angriffsplans (kein verblassender Strich)

// ------------------------------------------------------------------------------------------------- Anzeige der Knoepfe

int groupOf(int m)
{
	for (int g = 0; g < NGROUPS; ++g) { for (int i = 0; i < kGroups[g].n; ++i) { if (kGroups[g].modes[i] == m) { return g; } } }
	return -1;
}

// Lage eines Knopfes im Formular (unskaliert): nur die Befehle der gewaehlten Gruppe, vier nebeneinander
bool buttonPos(int m, int &x, int &y)
{
	if (s_group < 0 || s_group >= NGROUPS) { return false; }
	const int top = PAD + TITLE_H + TAB_H + GAP;
	for (int i = 0; i < kGroups[s_group].n; ++i)
	{
		if (kGroups[s_group].modes[i] == m)
		{
			x = PAD + (i % COLS) * (BTN_W + GAP);
			y = top + (i / COLS) * (BTN_H + GAP);
			return true;
		}
	}
	return false;
}

/// Generalssterne (1..5) in einem Feld: 1 einzeln, 2 nebeneinander, 3 im Dreieck, 4 im Quadrat, 5 im Fuenfeck
void drawStars(float cx, float cy, float box, int n, bool bright)
{
	n = std::clamp(n, 1, 5);
	static const float P[6][5][2] = {
		{ { 0, 0 } }, { { 0, 0 } }, { { -0.5f, 0 }, { 0.5f, 0 } }, { { 0, -0.45f }, { -0.5f, 0.4f }, { 0.5f, 0.4f } },
		{ { -0.5f, -0.45f }, { 0.5f, -0.45f }, { -0.5f, 0.45f }, { 0.5f, 0.45f } },
		{ { 0, -0.55f }, { -0.58f, -0.12f }, { 0.58f, -0.12f }, { -0.36f, 0.55f }, { 0.36f, 0.55f } } };
	const float spread = box * 0.42f;
	const float rOut = n == 1 ? box * 0.36f : (n == 2 ? box * 0.24f : (n == 3 ? box * 0.21f : box * 0.18f));
	const modglow::Colour gold{ static_cast<unsigned char>(bright ? 255 : 225), static_cast<unsigned char>(bright ? 214 : 182), static_cast<unsigned char>(bright ? 90 : 70) };
	for (int k = 0; k < n; ++k)
	{
		const glm::vec2 c(cx + P[n][k][0] * spread, cy + P[n][k][1] * spread);
		std::vector<glm::vec2> ring;
		for (int j = 0; j < 10; ++j)
		{
			const float a = -1.5707963f + j * 0.6283185f;
			const float r = (j % 2 == 0) ? rOut : rOut * 0.42f;
			ring.emplace_back(c.x + r * std::cos(a), c.y + r * std::sin(a));
		}
		modglow::fan(c, ring, gold, bright ? 255.f : 220.f, bright ? 0.45f : 0.2f);
	}
}

void drawTab(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const int g = static_cast<int>(psWidget->UserData);
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0, down = (b->getState() & WBUT_DOWN) != 0;
	const bool on = g == s_group;
	modui::wzButton(x0, y0, w, h, "", on, hl, down, modui::sizeClassFor(modui::uiScale()), nullptr);
	drawStars(static_cast<float>(x0) + w * 0.5f, static_cast<float>(y0) + h * 0.5f, static_cast<float>(std::min(w, h)) - 2.f, g + 1, on || hl);
	// kleiner Punkt: in dieser Gruppe ist gerade ein Befehl aktiv
	if (s_mode >= 0 && groupOf(s_mode) == g && !on) { pie_UniTransBoxFill(x0 + w - 5, y0 + 2, x0 + w - 2, y0 + 5, pal_RGBA(120, 255, 140, 255)); }
}

void drawRoot(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const float s = modui::uiScale();
	const int x0 = static_cast<int>(xOffset) + psWidget->x();
	const int y0 = static_cast<int>(yOffset) + psWidget->y();
	const int sc = modui::sizeClassFor(s);
	// Titel: Name der gewaehlten Gruppe (bzw. des Befehls, der gerade aktiv ist)
	std::string title = (s_group >= 0 && s_group < NGROUPS) ? kGroups[s_group].title : "Battle plan";
	if (s_mode >= 0 && groupOf(s_mode) == s_group) { title = kModes[s_mode].label; }
	modui::wzPanel(x0, y0, x0 + static_cast<int>(TOTAL_W * s), y0 + static_cast<int>(TOTAL_H * s), title.c_str(), sc);   // MOD: WZ-Stil
}

// Symbole der Befehle: Strichzeichnungen in der Farbe des Befehls (Einheitsquadrat -1..1, Mitte cx/cy, halbe Groesse r)
struct Pen
{
	float cx, cy, r;
	PIELIGHT c;
	bool thick;
	int X(float u) const { return static_cast<int>(std::lround(cx + u * r)); }
	int Y(float v) const { return static_cast<int>(std::lround(cy + v * r)); }
	void l(float x0, float y0, float x1, float y1) const
	{
		iV_Line(X(x0), Y(y0), X(x1), Y(y1), c);
		if (thick)
		{
			const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
			iV_Line(X(x0) + (steep ? 1 : 0), Y(y0) + (steep ? 0 : 1), X(x1) + (steep ? 1 : 0), Y(y1) + (steep ? 0 : 1), c);
		}
	}
	void arc(float x, float y, float rr, float a0, float a1, int n = 14) const
	{
		for (int i = 0; i < n; ++i)
		{
			const float t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n;
			l(x + rr * std::cos(t0), y + rr * std::sin(t0), x + rr * std::cos(t1), y + rr * std::sin(t1));
		}
	}
	void circ(float x, float y, float rr) const { arc(x, y, rr, 0.f, 6.2831853f, 18); }
	void dot(float x, float y, float rr) const
	{
		const int R = std::max(1, static_cast<int>(std::lround(rr * r)));
		for (int dy = -R; dy <= R; ++dy)
		{
			const int hw = static_cast<int>(std::sqrt(static_cast<float>(R * R - dy * dy)));
			pie_UniTransBoxFill(static_cast<float>(X(x) - hw), static_cast<float>(Y(y) + dy), static_cast<float>(X(x) + hw + 1), static_cast<float>(Y(y) + dy + 1), c);
		}
	}
};

void drawIcon(int m, const Pen &p)
{
	constexpr float kPi = 3.14159265f;
	switch (m)
	{
	case M_MOVE:   // Pfeil
		p.l(-0.85f, 0.f, 0.7f, 0.f); p.l(0.25f, -0.5f, 0.8f, 0.f); p.l(0.25f, 0.5f, 0.8f, 0.f);
		break;
	case M_RALLY:   // Fahne
		p.l(-0.55f, 0.9f, -0.55f, -0.9f); p.l(-0.55f, -0.9f, 0.75f, -0.5f); p.l(0.75f, -0.5f, -0.55f, -0.1f); p.l(-0.9f, 0.9f, -0.2f, 0.9f);
		break;
	case M_SPREAD:   // Pfeile nach aussen
		for (int k = 0; k < 4; ++k)
		{
			const float dx = (k & 1) ? 1.f : -1.f, dy = (k & 2) ? 1.f : -1.f;
			p.l(dx * 0.2f, dy * 0.2f, dx * 0.85f, dy * 0.85f); p.l(dx * 0.85f, dy * 0.85f, dx * 0.45f, dy * 0.85f); p.l(dx * 0.85f, dy * 0.85f, dx * 0.85f, dy * 0.45f);
		}
		break;
	case M_COVER:   // Helm ueber Graben
		p.arc(0.f, 0.1f, 0.65f, kPi, 2.f * kPi); p.l(-0.9f, 0.1f, 0.9f, 0.1f); p.l(-0.9f, 0.55f, -0.5f, 0.85f); p.l(-0.5f, 0.85f, 0.5f, 0.85f); p.l(0.5f, 0.85f, 0.9f, 0.55f);
		break;
	case M_ATTACK:   // gekreuzte Schwerter
		p.l(-0.8f, 0.8f, 0.75f, -0.75f); p.l(0.8f, 0.8f, -0.75f, -0.75f); p.l(-0.75f, 0.35f, -0.35f, 0.75f); p.l(0.75f, 0.35f, 0.35f, 0.75f);
		break;
	case M_OFFENSE:   // Doppelpfeil nach vorn
		p.l(-0.75f, -0.7f, -0.05f, 0.f); p.l(-0.05f, 0.f, -0.75f, 0.7f); p.l(0.f, -0.7f, 0.7f, 0.f); p.l(0.7f, 0.f, 0.f, 0.7f);
		break;
	case M_DEFEND:   // Schild
		p.l(-0.7f, -0.75f, 0.7f, -0.75f); p.l(-0.7f, -0.75f, -0.7f, 0.05f); p.l(0.7f, -0.75f, 0.7f, 0.05f);
		p.l(-0.7f, 0.05f, 0.f, 0.9f); p.l(0.7f, 0.05f, 0.f, 0.9f); p.l(0.f, -0.55f, 0.f, 0.55f);
		break;
	case M_ARTY:   // Flugbahn ins Ziel
		p.arc(0.f, 0.75f, 0.85f, kPi, 1.85f * kPi); p.circ(0.55f, 0.45f, 0.3f); p.l(0.55f, 0.05f, 0.55f, 0.85f); p.l(0.15f, 0.45f, 0.95f, 0.45f);
		break;
	case M_AIR:   // Flugzeug von oben
		p.l(0.f, -0.9f, 0.f, 0.85f); p.l(-0.9f, 0.05f, 0.9f, 0.05f); p.l(-0.9f, 0.05f, 0.f, -0.25f); p.l(0.9f, 0.05f, 0.f, -0.25f); p.l(-0.4f, 0.8f, 0.4f, 0.8f);
		break;
	case M_COMBOT:   // Kampfroboter
		p.l(-0.35f, -0.85f, 0.35f, -0.85f); p.l(-0.35f, -0.85f, -0.35f, -0.45f); p.l(0.35f, -0.85f, 0.35f, -0.45f);
		p.l(-0.6f, -0.35f, 0.6f, -0.35f); p.l(-0.6f, -0.35f, -0.45f, 0.25f); p.l(0.6f, -0.35f, 0.45f, 0.25f); p.l(-0.45f, 0.25f, 0.45f, 0.25f);
		p.l(-0.3f, 0.25f, -0.45f, 0.9f); p.l(0.3f, 0.25f, 0.45f, 0.9f); p.l(-0.6f, -0.35f, -0.95f, 0.2f); p.l(0.6f, -0.35f, 0.95f, 0.2f);
		break;
	case M_DEFNET:   // Graben-Zickzack mit Stacheldraht
		p.l(-0.9f, 0.2f, -0.5f, -0.2f); p.l(-0.5f, -0.2f, -0.1f, 0.2f); p.l(-0.1f, 0.2f, 0.3f, -0.2f); p.l(0.3f, -0.2f, 0.7f, 0.2f); p.l(0.7f, 0.2f, 0.9f, 0.f);
		for (int k = 0; k < 4; ++k) { const float x = -0.75f + k * 0.5f; p.l(x - 0.12f, -0.75f, x + 0.12f, -0.55f); p.l(x + 0.12f, -0.75f, x - 0.12f, -0.55f); }
		p.l(-0.9f, -0.65f, 0.9f, -0.65f); p.l(-0.9f, 0.65f, 0.9f, 0.65f);
		break;
	case M_ARTYBASE:   // Ring mit Stellungen
		p.circ(0.f, 0.f, 0.8f); p.dot(0.f, 0.f, 0.2f);
		for (int k = 0; k < 4; ++k) { const float a = kPi * 0.25f + k * kPi * 0.5f; p.dot(0.8f * std::cos(a), 0.8f * std::sin(a), 0.16f); }
		break;
	case M_TRAPS:   // Panzersperre (Igel)
		p.l(-0.75f, 0.8f, 0.75f, -0.6f); p.l(0.75f, 0.8f, -0.75f, -0.6f); p.l(0.f, -0.9f, 0.f, 0.9f); p.l(-0.9f, 0.8f, 0.9f, 0.8f);
		break;
	case M_MINES:   // Mine
		p.circ(0.f, 0.15f, 0.5f); p.dot(0.f, 0.15f, 0.22f);
		p.l(0.f, -0.35f, 0.f, -0.85f); p.l(-0.5f, 0.15f, -0.9f, 0.15f); p.l(0.5f, 0.15f, 0.9f, 0.15f); p.l(-0.35f, -0.2f, -0.65f, -0.5f); p.l(0.35f, -0.2f, 0.65f, -0.5f);
		break;
	case M_RAIL:   // Gleis
		p.l(-0.4f, -0.95f, -0.4f, 0.95f); p.l(0.4f, -0.95f, 0.4f, 0.95f);
		for (int k = 0; k < 4; ++k) { const float y = -0.75f + k * 0.5f; p.l(-0.75f, y, 0.75f, y); }
		break;
	case M_DEMOLISH:   // Abrissbirne
		p.l(-0.8f, -0.9f, 0.4f, -0.9f); p.l(-0.8f, -0.9f, -0.8f, 0.9f); p.l(0.25f, -0.9f, 0.25f, 0.1f); p.dot(0.25f, 0.45f, 0.35f);
		break;
	case M_CLEARMINES:   // Mine, durchgestrichen
		p.circ(0.f, 0.f, 0.45f); p.dot(0.f, 0.f, 0.2f); p.l(0.f, -0.45f, 0.f, -0.75f); p.l(-0.45f, 0.f, -0.75f, 0.f); p.l(0.45f, 0.f, 0.75f, 0.f);
		p.circ(0.f, 0.f, 0.9f); p.l(-0.65f, -0.65f, 0.65f, 0.65f);
		break;
	case M_RECOVER:   // Kranhaken (Bergung)
		p.l(-0.85f, -0.85f, 0.6f, -0.85f); p.l(0.1f, -0.85f, 0.1f, 0.15f); p.arc(-0.15f, 0.15f, 0.25f, 0.f, kPi); p.l(-0.4f, 0.15f, -0.4f, 0.f);
		p.l(-0.75f, 0.55f, 0.75f, 0.55f); p.l(-0.75f, 0.55f, -0.55f, 0.9f); p.l(0.75f, 0.55f, 0.55f, 0.9f); p.l(-0.55f, 0.9f, 0.55f, 0.9f);
		break;
	case M_STAGING:   // Kreis mit Sammelpunkten
		p.circ(0.f, 0.f, 0.85f); p.dot(-0.3f, -0.15f, 0.14f); p.dot(0.3f, -0.15f, 0.14f); p.dot(0.f, 0.32f, 0.14f);
		break;
	case M_FERRY:   // Landeplatz mit Schleife
		p.l(0.f, -0.85f, 0.85f, 0.f); p.l(0.85f, 0.f, 0.f, 0.85f); p.l(0.f, 0.85f, -0.85f, 0.f); p.l(-0.85f, 0.f, 0.f, -0.85f);
		p.arc(0.f, 0.f, 0.4f, -2.6f, 2.6f, 10); p.l(0.34f, -0.21f, 0.5f, -0.45f);
		break;
	case M_BRIDGEHEAD:   // Kreis mit Fadenkreuz
		p.circ(0.f, 0.f, 0.8f); p.l(-0.95f, 0.f, -0.35f, 0.f); p.l(0.35f, 0.f, 0.95f, 0.f); p.l(0.f, -0.95f, 0.f, -0.35f); p.l(0.f, 0.35f, 0.f, 0.95f);
		break;
	case M_GO:   // Pfeilbuendel nach vorn
		p.l(-0.85f, 0.6f, 0.75f, -0.7f); p.l(0.75f, -0.7f, 0.25f, -0.7f); p.l(0.75f, -0.7f, 0.75f, -0.2f);
		p.l(-0.85f, -0.1f, 0.3f, -0.1f); p.l(-0.4f, 0.85f, 0.3f, 0.2f);
		break;
	case M_CLEARPLAN:   // Kreuz
		p.l(-0.7f, -0.7f, 0.7f, 0.7f); p.l(0.7f, -0.7f, -0.7f, 0.7f);
		break;
	case M_TUNNEL:   // Bogen ueber gestricheltem Gang
		p.arc(-0.45f, 0.35f, 0.4f, kPi, 2.f * kPi, 10); p.arc(0.45f, 0.35f, 0.4f, kPi, 2.f * kPi, 10);
		for (int k = 0; k < 3; ++k) { const float x = -0.3f + k * 0.3f; p.l(x - 0.08f, 0.7f, x + 0.08f, 0.7f); }
		break;
	default:
		p.dot(0.f, 0.f, 0.4f);
		break;
	}
}

void drawButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const float s = modui::uiScale();
	const int m = static_cast<int>(psWidget->UserData);
	const ModeInfo &mi = kModes[m];
	const int x0 = static_cast<int>(xOffset) + psWidget->x();
	const int y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = static_cast<int>(BTN_W * s), h = static_cast<int>(BTN_H * s);
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const bool active = s_mode == m;
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	const int sc = modui::sizeClassFor(s);
	// (22.11.) reiner Symbol-Knopf: Flaeche/Rahmen wie modui::wzButton, das Symbol in der Farbe des Befehls mittig (Name im Tooltip)
	modui::wzButton(x0, y0, w, h, "", active, hl, down, sc, nullptr);
	const float ir = (std::min(w, h) - 8.f * s) * 0.5f;
	const PIELIGHT col = (active || hl) ? pal_RGBA(static_cast<UBYTE>(std::min(255, mi.r + 50)), static_cast<UBYTE>(std::min(255, mi.g + 50)), static_cast<UBYTE>(std::min(255, mi.b + 50)), 255)
	                                    : pal_RGBA(mi.r, mi.g, mi.b, 245);
	const Pen pen{ x0 + w * 0.5f, y0 + h * 0.5f, ir, col, ir >= 6.f };
	drawIcon(m, pen);
	const bool armed = m == M_CLEARPLAN && s_planClearArmed != 0 && realTime - s_planClearArmed < 4000;
	if (armed)
	{
		// "Clear Plan" scharf: rot blinkender Rahmen - zweiter Klick loescht
		const PIELIGHT red = ((realTime / 250) % 2 == 0) ? pal_RGBA(255, 80, 70, 255) : pal_RGBA(255, 170, 120, 255);
		iV_Box(x0, y0, x0 + w - 1, y0 + h - 1, red);
		iV_Box(x0 + 1, y0 + 1, x0 + w - 2, y0 + h - 2, red);
	}
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
	for (int m = 0; m < M_COUNT; ++m)
	{
		auto b = std::make_shared<W_BUTTON>();
		root->attach(b);
		b->displayFunction = drawButton;
		b->UserData = static_cast<UDWORD>(m);
		b->setTip(kModes[m].tip);
		b->addOnClickHandler([m](W_BUTTON &) { widgScheduleTask([m]() {
			if (m == M_GO)
			{
				// MOD (22.11.): Angriffsplaner - alle Brueckenkoepfe und Angriffslinien greifen jetzt an
				plan::attackNow();
				s_mode = -1; s_drawing = false; s_cur.clear();
				return;
			}
			if (m == M_CLEARPLAN)
			{
				if (s_planClearArmed != 0 && realTime - s_planClearArmed < 4000)
				{
					plan::clearAll();
					s_planClearArmed = 0;
					addConsoleMessage(_("Attack plan cleared"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
				}
				else if (plan::hasElems()) { s_planClearArmed = std::max(1u, realTime); }
				s_mode = -1; s_drawing = false; s_cur.clear();
				return;
			}
			if (m == M_COVER)
			{
				// MOD (Wunsch 17.11.): Sofort-Befehl "Take cover" - Infanterie bemannt Graeben/Gruben/Haeuser in der Naehe
				if (selectedPlayer < MAX_PLAYERS) { modcover::takeCover(selectedPlayer); }
				s_mode = -1;
				s_drawing = false;
				s_cur.clear();
				return;
			}
			if (m == M_SPREAD)
			{
				// MOD (16.11. Punkt 2): Sofort-Befehl "Spread out" fuer die Auswahl
				std::vector<DROID *> sel;
				if (selectedPlayer < MAX_PLAYERS)
				{
					for (DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d->selected) { sel.push_back(d); } }
				}
				modincoming::spreadUnits(sel);
				s_mode = -1;
				s_drawing = false;
				s_cur.clear();
				return;
			}
			s_mode = (s_mode == m) ? -1 : m;
			s_drawing = false;
			s_cur.clear();
		}); });
		s_buttons.push_back(b);
	}
	s_tabs.clear();
	for (int g = 0; g < NGROUPS; ++g)
	{
		auto t = std::make_shared<W_BUTTON>();
		root->attach(t);
		t->displayFunction = drawTab;
		t->UserData = static_cast<UDWORD>(g);
		std::string tip = std::string(kGroups[g].title) + ":";
		for (int i = 0; i < kGroups[g].n; ++i) { tip += std::string(i == 0 ? " " : ", ") + kModes[kGroups[g].modes[i]].label; }
		t->setTip(tip);
		t->addOnClickHandler([g](W_BUTTON &) { widgScheduleTask([g]() { s_group = g; }); });
		s_tabs.push_back(t);
	}
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_root = root;
}

void drawInfButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const float sc = modui::uiScale();
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x();
	const int y0 = static_cast<int>(yOffset) + psWidget->y();
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	const PIELIGHT accent = pal_RGBA(230, 190, 90, 255);
	modui::wzButton(x0, y0, psWidget->width(), psWidget->height(), _("Infantry: build"), intMode == INT_STAT, hl, down, modui::sizeClassFor(sc), &accent);
}

bool infantryBuilderSelected()
{
	if (selectedPlayer >= MAX_PLAYERS) { return false; }
	for (const DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d->selected && !d->died && modbuild::isInfantryBuilder(d)) { return true; }
	}
	return false;
}

void ensureInfButton()
{
	if (s_infScreen) { return; }
	auto screen = W_SCREEN::make();
	screen->psForm->hide();   // durchlaessig fuer Mausklicks
	auto root = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(root);
	auto b = std::make_shared<W_BUTTON>();
	root->attach(b);
	b->displayFunction = drawInfButton;
	b->setTip(_("Open the build menu for the selected infantry (trenches, sandbags, MG nests, ammo crates)"));
	b->addOnClickHandler([](W_BUTTON &) { widgScheduleTask([]() {
		if (!infantryBuilderSelected()) { return; }
		intResetScreen(true);
		intShowWidget(RETBUT_BUILD);
	}); });
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_infScreen = screen;
	s_infRoot = root;
	s_infBtn = b;
}

void infButtonTick(bool want)
{
	const bool show = want && infantryBuilderSelected() && (intMode == INT_NORMAL || intMode == INT_OBJECT || intMode == INT_STAT);
	if (!show && !s_infScreen) { return; }
	ensureInfButton();
	s_infRoot->show(show);
	if (!show) { return; }
	const float sc = modui::uiScale();
	const int w = static_cast<int>(RET_FORMWIDTH), h = static_cast<int>(22 * sc);
	const int y = std::max(0, static_cast<int>(RET_Y) - h - 4);
	s_infRoot->setGeometry(RET_X, y, w, h);
	s_infBtn->setGeometry(0, 0, w, h);
}

void calcRect(int &x, int &y, int &w, int &h)
{
	const float s = modui::uiScale();
	w = static_cast<int>(TOTAL_W * s); h = static_cast<int>(TOTAL_H * s);
	// (17.11.) nicht ueber der Karte unten rechts (Radar): links daneben, unten buendig
	// (18.11. 12:10) ohne Minikarte unten rechts in der Ecke - Map/Symbol-Knoepfe und Rosette stapeln sich darueber (siehe stackTop)
	x = static_cast<int>(screenWidth) - w - 4;
	y = static_cast<int>(screenHeight) - h - 8;
	const std::shared_ptr<WIDGET> radar = getRadarWidget();
	if (radar && radar->visible() && radar->width() > 0)
	{
		const int m = rotateRadar ? static_cast<int>(radar->width() * 0.21f) : 0;   // (18.11.) Abstand zur gedrehten Minikarte
		x = std::max(4, radar->x() - w - 6 - m);
		y = std::max(4, static_cast<int>(screenHeight) - h - 8);
	}
}

void layout()
{
	const float s = modui::uiScale();
	int x, y, w, h;
	calcRect(x, y, w, h);
	s_root->setGeometry(x, y, w, h);
	for (size_t g = 0; g < s_tabs.size(); ++g)
	{
		s_tabs[g]->setGeometry(static_cast<int>((PAD + static_cast<int>(g) * (TAB_W + 2)) * s), static_cast<int>((PAD + TITLE_H) * s), static_cast<int>(TAB_W * s), static_cast<int>(TAB_H * s));
	}
	for (size_t i = 0; i < s_buttons.size(); ++i)
	{
		int bx = 0, by = 0;
		if (!buttonPos(static_cast<int>(i), bx, by)) { s_buttons[i]->hide(); continue; }
		s_buttons[i]->show();
		s_buttons[i]->setGeometry(static_cast<int>(bx * s), static_cast<int>(by * s), static_cast<int>(BTN_W * s), static_cast<int>(BTN_H * s));
	}
}

// ------------------------------------------------------------------------------------------------- Geometrie

int64_t dist2(const Vector2i &a, const Vector2i &b)
{
	const int64_t dx = a.x - b.x, dy = a.y - b.y;
	return dx * dx + dy * dy;
}

Vector2i centroid(const std::vector<Vector2i> &p)
{
	int64_t sx = 0, sy = 0;
	for (const Vector2i &v : p) { sx += v.x; sy += v.y; }
	const int64_t n = std::max<int64_t>(1, static_cast<int64_t>(p.size()));
	return Vector2i(static_cast<int>(sx / n), static_cast<int>(sy / n));
}

/// n gleichmaessig verteilte Punkte entlang der Linie (nach Laenge)
std::vector<Vector2i> resample(const std::vector<Vector2i> &p, size_t n)
{
	std::vector<Vector2i> out;
	if (p.empty() || n == 0) { return out; }
	if (p.size() == 1) { out.assign(n, p.front()); return out; }
	if (n == 1) { out.push_back(p[p.size() / 2]); return out; }
	std::vector<double> acc(p.size(), 0.0);
	for (size_t i = 1; i < p.size(); ++i) { acc[i] = acc[i - 1] + std::sqrt(static_cast<double>(dist2(p[i], p[i - 1]))); }
	const double total = acc.back();
	size_t seg = 1;
	for (size_t k = 0; k < n; ++k)
	{
		const double t = total * static_cast<double>(k) / static_cast<double>(n - 1);
		while (seg < p.size() - 1 && acc[seg] < t) { ++seg; }
		const double l = acc[seg] - acc[seg - 1];
		const double f = l > 0.0 ? (t - acc[seg - 1]) / l : 0.0;
		out.emplace_back(static_cast<int>(p[seg - 1].x + (p[seg].x - p[seg - 1].x) * f), static_cast<int>(p[seg - 1].y + (p[seg].y - p[seg - 1].y) * f));
	}
	return out;
}

/// Spiral-Versatz fuer die i-te Einheit, damit nicht alle auf einen Punkt fahren
Vector2i spread(const Vector2i &c, size_t i)
{
	if (i == 0) { return c; }
	const int ring = 1 + static_cast<int>(std::sqrt(static_cast<double>(i)));
	const uint16_t ang = static_cast<uint16_t>(i * 10273u);
	const int r = ring * (DIST_TILE * 3 / 4);
	Vector2i p(c.x + iSinR(ang, r), c.y + iCosR(ang, r));
	p.x = std::clamp(p.x, TILE_UNITS, world_coord(gameWorld.map.width - 2));
	p.y = std::clamp(p.y, TILE_UNITS, world_coord(gameWorld.map.height - 2));
	return p;
}

// ------------------------------------------------------------------------------------------------- Einheiten-Klassen

bool isAir(const DROID *d) { return d->isVtol() || modlayer::isFlier(d); }
// (19.11.) Pickups/Oeltanker sind DROID_CONSTRUCT, bauen aber nicht (DROID::isConstructionDroid) - nie als Bauer einplanen
bool isTruck(const DROID *d) { return (d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT) && d->isConstructionDroid(); }
bool hasWeapon(const DROID *d) { return d->numWeaps > 0 && d->asWeaps[0].nStat > 0; }
bool isArty(const DROID *d)
{
	if (!hasWeapon(d)) { return false; }
	const WEAPON_STATS *ws = d->getWeaponStats(0);
	return (ws != nullptr && ws->movementModel == MM_INDIRECT) || modarty::isArtyWeapon(d->asWeaps[0].nStat);
}
bool isCombat(const DROID *d)
{
	return hasWeapon(d) && !isAir(d) && (d->droidType == DROID_WEAPON || d->droidType == DROID_CYBORG || d->droidType == DROID_CYBORG_SUPER || d->droidType == DROID_PERSON);
}
bool isBuilder(const DROID *d) { return (isTruck(d) && !isAir(d)) || modbuild::isInfantryBuilder(d); }

bool fits(Mode m, const DROID *d)
{
	if (d->isTransporter()) { return false; }
	switch (m)
	{
	case M_MOVE: return !isAir(d);
	case M_RALLY: return isCombat(d) || modcombot::isModular(d);
	case M_ATTACK: return (isCombat(d) && !isArty(d)) || modcombot::isModular(d);
	case M_DEFEND: return isCombat(d);   // (21.11.) nur Aufstellung - Bauer bauen ueber M_DEFNET
	case M_DEFNET: return isBuilder(d);
	case M_RAIL: return isBuilder(d);
	case M_ARTYBASE: return isTruck(d) && !isAir(d);
	case M_ARTY: return isArty(d);
	case M_AIR: return isAir(d) && hasWeapon(d);
	case M_COMBOT: return modcombot::isModular(d);
	case M_TRAPS:
	case M_MINES: return isBuilder(d);
	case M_OFFENSE: return isCombat(d) || isBuilder(d) || modcombot::isModular(d);
	case M_CLEARMINES: return (modmines::isFlail(d) || modoil::isInfantry(d)) && !isAir(d);
	case M_DEMOLISH: return isBuilder(d);
	default: return false;
	}
}

std::vector<DROID *> pickUnits(Mode m, const std::vector<Vector2i> &pts)
{
	std::vector<DROID *> sel, nearby;
	if (selectedPlayer >= MAX_PLAYERS) { return sel; }
	const modlayer::Layer view = modlayer::current();
	bool anySelected = false;
	for (DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d == nullptr || d->died) { continue; }
		if (d->selected) { anySelected = true; }
		if (modlayer::layerOf(d) != view || !fits(m, d)) { continue; }
		if (d->selected) { sel.push_back(d); continue; }
		// ohne Auswahl: Einheiten in der Naehe der Zeichnung
		const int64_t nearR = static_cast<int64_t>(m == M_OFFENSE ? NEAR_TILES * 3 / 2 : NEAR_TILES) * DIST_TILE;   // Offensive: mehr Einheiten
		const int64_t r2 = nearR * nearR;
		for (const Vector2i &p : pts)
		{
			if (dist2(d->pos.xy(), p) <= r2) { nearby.push_back(d); break; }
		}
	}
	if (anySelected) { return sel; }   // gewaehlte Einheiten haben Vorrang (auch wenn keine davon passt)
	const Vector2i c = centroid(pts);
	std::sort(nearby.begin(), nearby.end(), [&c](const DROID *a, const DROID *b) {
		const int64_t da = dist2(a->pos.xy(), c), db = dist2(b->pos.xy(), c);
		return da != db ? da < db : a->id < b->id;
	});
	const size_t cap = (m == M_OFFENSE) ? MAX_UNITS * 8 / 5 : MAX_UNITS;
	if (nearby.size() > cap) { nearby.resize(cap); }
	return nearby;
}

void send(DROID *d, const DroidOrder &o, bool add)
{
	sendDroidInfo(d, o, add, currentOrderSource());
}

// ------------------------------------------------------------------------------------------------- Bauwerke fuer die Artilleriebasis

bool available(int idx)
{
	return idx >= 0 && static_cast<UDWORD>(idx) < numStructureStats && selectedPlayer < MAX_PLAYERS && apStructTypeLists[selectedPlayer][idx] == AVAILABLE;
}

STRUCTURE_STATS *firstAvailable(std::initializer_list<const char *> ids)
{
	for (const char *id : ids)
	{
		const int idx = getStructStatFromName(WzString::fromUtf8(id));
		if (available(idx)) { return &asStructureStats[idx]; }
	}
	return nullptr;
}

STRUCTURE_STATS *bestArtyEmplacement()
{
	STRUCTURE_STATS *best = nullptr;
	unsigned bestRange = 0;
	for (UDWORD i = 0; i < numStructureStats; ++i)
	{
		STRUCTURE_STATS &s = asStructureStats[i];
		if (!available(static_cast<int>(i)) || s.type != REF_DEFENSE || s.numWeaps == 0 || s.psWeapStat[0] == nullptr) { continue; }
		if (s.psWeapStat[0]->movementModel != MM_INDIRECT) { continue; }
		const unsigned range = s.psWeapStat[0]->upgrade[selectedPlayer].maxRange;
		if (best == nullptr || range > bestRange) { best = &s; bestRange = range; }
	}
	return best;
}

/// Groesse der Artilleriebasis aus dem gezogenen Kreis (Wunsch 20.11. 02:13 "Die Arty base ist gut. Aber jetzt gibt es nur noch eine
/// Groesse"): mittlerer Abstand der Strichpunkte zur Mitte - klein (bis 2,5 Felder), mittel (bis 4,5), gross
int artyBaseSize(const std::vector<Vector2i> &raw)
{
	if (raw.size() < 3) { return 1; }
	const Vector2i c = centroid(raw);
	double sum = 0.0;
	for (const Vector2i &q : raw) { sum += std::sqrt(static_cast<double>(dist2(q, c))); }
	const double r = sum / static_cast<double>(raw.size()) / DIST_TILE;   // (20.11.) in Original-Kacheln (auch in der Viertel-Fassung)
	return r < 2.5 ? 1 : (r < 4.5 ? 2 : 3);
}

/// Bauplan der Artilleriebasis (Sensor in der Mitte, Stellungen dicht drumherum, Mauerring, davor ein Ring Panzerigel) - fuer den Bau
/// UND die Vorschau unter der Maus (18.11. 12:14 "beim Klicken auf Arty base kommt das falsche Layout, beim Bestaetigen das richtige")
/// size 1: 4 Stellungen, Mauer 5 x 5; size 2: 8 Stellungen, Mauer 7 x 7; size 3: 12 Stellungen, Mauer 9 x 9.
/// (20.11. "sie hat auch nicht mehr Hedgehog-Panzersperren davor"): Igel-Ring ein Feld vor der Mauer, in der Mitte jeder Seite eine Luecke
std::vector<modbuild::Placement> artyBasePlan(const Vector2i &centerWorld, int size)
{
	std::vector<modbuild::Placement> out;
	size = std::clamp(size, 1, 3);
	STRUCTURE_STATS *arty = bestArtyEmplacement();
	STRUCTURE_STATS *sensor = firstAvailable({ "Sys-SensoTowerWS", "Sys-SensoTower02", "Sys-SensoTower01", "A0BaBaOutpost" });
	STRUCTURE_STATS *wall = firstAvailable({ "A0BaBaHorizontalWall", "A0HardcreteMk1Wall", "CollectiveWall", "NEXUSWall", "A0BaBaSandbagWall" });
	STRUCTURE_STATS *hedge = firstAvailable({ "A0BaBaHedgehog", "A0TankTrapHeavy", "A0TankTrap" });
	// (20.11.) in PLAN-Kacheln (DIST_TILE = Original-Kachel) wie der Befestigungsplan und seine Vorschau - in der Viertel-Fassung waren
	// es Feinfelder: Vorschau an falscher Stelle, Stellungen nur ein Feinfeld auseinander (blockierten sich gegenseitig)
	const Vector2i ct(std::max(0, centerWorld.x) / DIST_TILE, std::max(0, centerWorld.y) / DIST_TILE);
	auto one = [&](STRUCTURE_STATS *st, int tx, int ty) { modbuild::Placement p; p.stats = st; p.tile = Vector2i(tx, ty); p.tile2 = p.tile; out.push_back(p); };
	auto line = [&](STRUCTURE_STATS *st, int x0, int y0, int x1, int y1) {
		modbuild::Placement p; p.stats = st; p.tile = Vector2i(x0, y0); p.tile2 = Vector2i(x1, y1); p.line = true; out.push_back(p);
	};
	if (sensor != nullptr) { one(sensor, ct.x, ct.y); }
	if (arty != nullptr)
	{
		// (16.11. 22:50 "immer noch zu gross, nicht so viel Platz verschwenden") dicht um den Sensor
		static const int IN4[4][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
		static const int R2[8][2] = { { -2, -2 }, { 0, -2 }, { 2, -2 }, { 2, 0 }, { 2, 2 }, { 0, 2 }, { -2, 2 }, { -2, 0 } };
		static const int R3[8][2] = { { -3, -3 }, { 0, -3 }, { 3, -3 }, { 3, 0 }, { 3, 3 }, { 0, 3 }, { -3, 3 }, { -3, 0 } };
		if (size != 2) { for (const auto &o : IN4) { one(arty, ct.x + o[0], ct.y + o[1]); } }
		if (size == 2) { for (const auto &o : R2) { one(arty, ct.x + o[0], ct.y + o[1]); } }
		if (size == 3) { for (const auto &o : R3) { one(arty, ct.x + o[0], ct.y + o[1]); } }
	}
	const int r = size + 1;   // Mauerring direkt um den Stellungsblock
	if (wall != nullptr)
	{
		line(wall, ct.x - r, ct.y - r, ct.x + r, ct.y - r);
		line(wall, ct.x + r, ct.y - r + 1, ct.x + r, ct.y + r);
		line(wall, ct.x + r - 1, ct.y + r, ct.x - r, ct.y + r);
		line(wall, ct.x - r, ct.y + r - 1, ct.x - r, ct.y - r + 1);
	}
	if (hedge != nullptr)
	{
		const int h = r + 2;   // ein freies Feld zwischen Mauer und Igeln
		// je Seite zwei Stuecke mit einer Luecke von einem Feld in der Mitte
		line(hedge, ct.x - h, ct.y - h, ct.x - 1, ct.y - h); line(hedge, ct.x + 1, ct.y - h, ct.x + h, ct.y - h);
		line(hedge, ct.x + h, ct.y - h + 1, ct.x + h, ct.y - 1); line(hedge, ct.x + h, ct.y + 1, ct.x + h, ct.y + h);
		line(hedge, ct.x + h - 1, ct.y + h, ct.x + 1, ct.y + h); line(hedge, ct.x - 1, ct.y + h, ct.x - h, ct.y + h);
		line(hedge, ct.x - h, ct.y + h - 1, ct.x - h, ct.y + 1); line(hedge, ct.x - h, ct.y - 1, ct.x - h, ct.y - h + 1);
	}
	return out;
}

// ------------------------------------------------------------------------------------------------- Bergetrupp (Wunsch 14.11. Punkt 25)
// "Bergen": Kreis ziehen -> das Spiel stellt vollautomatisch einen bewaffneten, eskortierten Bergetrupp zusammen (freie Bau-/Reparatur-
// fahrzeuge fuer Teile und Artefakte, Scav-Lkw mit Ladeflaeche fuer Wracks, Kampfeinheiten als Eskorte). Der Trupp sammelt sich zuerst
// vollstaendig am Bereitstellungspunkt (Rand der eigenen Basis in Richtung Ziel), faehrt dann gemeinsam los, birgt alles im Kreis und
// kehrt zurueck. Wracks bringt der Lkw zur Reparaturstation, dort werden sie wieder zu Fahrzeugen (modrecover).
// Die Verwaltung laeuft beim befehlenden Spieler und gibt gewoehnliche Befehle (wie Mausklicks) - also auch im Mehrspieler synchron.
// Nicht im Spielstand (nach dem Laden ist der Trupp aufgeloest, die Einheiten behalten ihren letzten Befehl).
struct SalvageOp
{
	bool active = false;
	int phase = 0;                  // 0 sammeln, 1 anfahren, 2 bergen
	Vector2i c, deploy;
	int r = 0;
	unsigned phaseStart = 0, nextStep = 0, nextEscort = 0;
	std::vector<uint32_t> collectors, towers, escorts;
};
SalvageOp s_op;

// MOD (Wunsch 14.11. Punkt 12, Rest): "Kommen nach einer Zeit keine weiteren Befehle, nehmen sie wieder an den Oberbefehlen teil."
// Jede Einheit merkt sich ihren letzten Schlachtplan-Auftrag (Bewegung, Sammeln, Angriff, Verteidigung, Combot-Gebiet). Hat der Spieler
// sie einzeln woanders hingeschickt, ist sie fertig (kein Befehl mehr), die 45-s-Spielerkontrolle (modcontrol) ist vorbei und sie steht
// weit weg vom Planziel, kehrt sie von selbst zu ihrem Planauftrag zurueck. Ein Planauftrag gilt 15 Minuten.
struct PlanTask
{
	DroidOrder order = DroidOrder(DORDER_NONE);
	Vector2i anchor;
	unsigned born = 0;
	int face = -1;        // (19.11. 16:44) Verteidigungslinie: Blickrichtung am Ziel (Richtung der Querstriche), -1 = keine
	bool faced = false;   // schon eingedreht (bis die Einheit ihren Platz wieder verlaesst)
	unsigned sent = 0;    // letzter eigener Befehl (Linienbefehl oder Rueckkehr) - spaetere Befehle sind Eingriffe des Spielers
};
std::map<uint32_t, PlanTask> s_plan;
unsigned s_nextPlanCheck = 0;
constexpr unsigned PLAN_LIFE = 15 * 60 * 1000;

constexpr unsigned OP_STEP_MS = 1000;
constexpr unsigned OP_GATHER_MAX = 75000;   // laenger wartet der Trupp nicht auf Nachzuegler
constexpr unsigned OP_GO_MAX = 120000;
constexpr unsigned OP_WORK_MAX = 300000;

bool isSupportUnit(const DROID *d)
{
	return d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT || d->droidType == DROID_REPAIR || d->droidType == DROID_CYBORG_REPAIR;
}
bool idleUnit(const DROID *d)
{
	return (d->order.type == DORDER_NONE || d->order.type == DORDER_GUARD || d->order.type == DORDER_HOLD) && !modrecover::carrying(d) && modcrates::crates(d) == 0;
}
bool inOp(uint32_t id)
{
	if (!s_op.active) { return false; }
	for (const auto *v : { &s_op.collectors, &s_op.towers, &s_op.escorts })
	{
		if (std::find(v->begin(), v->end(), id) != v->end()) { return true; }
	}
	return false;
}
DROID *opDroid(uint32_t id)
{
	if (selectedPlayer >= MAX_PLAYERS) { return nullptr; }
	DROID *d = IdToDroid(gameWorld.objects, id, selectedPlayer);
	return (d != nullptr && !d->died) ? d : nullptr;
}

/// bergbare Dinge im Kreis: Wracks (Abschlepp-Lkw) und Teile/Artefakte/Faesser (aufsammeln)
void opItems(std::vector<FEATURE *> &wrecks, std::vector<FEATURE *> &pickups)
{
	wrecks.clear();
	pickups.clear();
	if (selectedPlayer >= MAX_PLAYERS) { return; }
	for (BASE_OBJECT *o : gridStartIterate(s_op.c.x, s_op.c.y, s_op.r))
	{
		if (o->type != OBJ_FEATURE || o->died || !o->visible[selectedPlayer]) { continue; }
		FEATURE *f = static_cast<FEATURE *>(o);
		if (modrecover::isTowable(f)) { wrecks.push_back(f); continue; }
		if (f->psStats->subType == FEAT_OIL_DRUM || f->psStats->subType == FEAT_GEN_ARTE) { pickups.push_back(f); }
	}
	auto byId = [](const FEATURE *a, const FEATURE *b) { return a->id < b->id; };
	std::sort(wrecks.begin(), wrecks.end(), byId);
	std::sort(pickups.begin(), pickups.end(), byId);
}

void opEnd(bool sendHome)
{
	if (sendHome)
	{
		size_t i = 0;
		for (const auto *v : { &s_op.collectors, &s_op.escorts })
		{
			for (uint32_t id : *v)
			{
				DROID *d = opDroid(id);
				if (d != nullptr && !modrecover::carrying(d)) { send(d, DroidOrder(DORDER_MOVE, spread(s_op.deploy, i++)), false); }
			}
		}
	}
	s_op = SalvageOp();
}

void startSalvage(const Vector2i &c, int r)
{
	if (selectedPlayer >= MAX_PLAYERS) { return; }
	if (s_op.active) { opEnd(false); }
	s_op.c = c;
	s_op.r = r;
	std::vector<FEATURE *> wrecks, pickups;
	opItems(wrecks, pickups);
	if (wrecks.empty() && pickups.empty())
	{
		addConsoleMessage(_("Salvage: nothing to recover in this area"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		return;
	}
	// Bereitstellungspunkt: am Rand der eigenen Basis (Gebaeude, das dem Ziel am naechsten liegt), 5 Felder in Richtung Ziel
	const STRUCTURE *home = nullptr;
	int64_t homeD = 0;
	for (const STRUCTURE *st : gameWorld.objects.structures[selectedPlayer])
	{
		if (st->died || st->status != SS_BUILT) { continue; }
		const int64_t d = dist2(st->pos.xy(), c);
		if (home == nullptr || d < homeD) { home = st; homeD = d; }
	}
	const modlayer::Layer view = modlayer::current();
	std::vector<DROID *> pool;
	for (DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d->died || d->isTransporter() || isAir(d) || modlayer::layerOf(d) != view || !idleUnit(d)) { continue; }
		pool.push_back(d);
	}
	Vector2i deploy = c;
	if (home != nullptr)
	{
		const double dx = c.x - home->pos.x, dy = c.y - home->pos.y;
		const double len = std::max(1.0, std::sqrt(dx * dx + dy * dy));
		const double step = std::min(len * 0.5, 5.0 * DIST_TILE);
		deploy = Vector2i(home->pos.x + static_cast<int>(dx / len * step), home->pos.y + static_cast<int>(dy / len * step));
	}
	else if (!pool.empty())
	{
		std::vector<Vector2i> ps;
		for (const DROID *d : pool) { ps.push_back(d->pos.xy()); }
		deploy = centroid(ps);
	}
	deploy.x = std::clamp(deploy.x, TILE_UNITS * 2, world_coord(gameWorld.map.width - 3));
	deploy.y = std::clamp(deploy.y, TILE_UNITS * 2, world_coord(gameWorld.map.height - 3));
	s_op.deploy = deploy;
	std::sort(pool.begin(), pool.end(), [&deploy](const DROID *a, const DROID *b) {
		const int64_t da = dist2(a->pos.xy(), deploy), db = dist2(b->pos.xy(), deploy);
		return da != db ? da < db : a->id < b->id;
	});
	const size_t wantTow = std::min<size_t>(wrecks.size(), 3);
	const size_t wantCol = pickups.empty() ? 0 : std::min<size_t>(2, (pickups.size() + 2) / 3);
	for (DROID *d : pool)
	{
		if (modrecover::isTowTruck(d))
		{
			if (s_op.towers.size() < wantTow) { s_op.towers.push_back(d->id); }
			else if (s_op.collectors.size() < wantCol) { s_op.collectors.push_back(d->id); }
		}
		else if (isSupportUnit(d) && s_op.collectors.size() < wantCol)
		{
			s_op.collectors.push_back(d->id);
		}
	}
	if (s_op.towers.empty() && s_op.collectors.empty())
	{
		addConsoleMessage(wrecks.empty() ? _("Salvage: no free trucks or repair units for a salvage team")
		                                 : _("Salvage: no free trucks - wrecks need a flatbed truck (scavenger truck with cargo bed)"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		s_op = SalvageOp();
		return;
	}
	const size_t wantEsc = std::min<size_t>(6, 2 + s_op.towers.size() + s_op.collectors.size());
	for (DROID *d : pool)
	{
		if (s_op.escorts.size() >= wantEsc) { break; }
		if (isCombat(d) && !isArty(d) && d->droidType != DROID_PERSON && !inOp(d->id)) { s_op.escorts.push_back(d->id); }
	}
	for (DROID *d : pool)   // zur Not auch Infanterie als Eskorte
	{
		if (s_op.escorts.size() >= wantEsc) { break; }
		if (isCombat(d) && !isArty(d) && !inOp(d->id)) { s_op.escorts.push_back(d->id); }
	}
	s_op.active = true;
	s_op.phase = 0;
	s_op.phaseStart = gameTime;
	s_op.nextStep = gameTime + OP_STEP_MS;
	size_t i = 0;
	for (const auto *v : { &s_op.escorts, &s_op.collectors, &s_op.towers })
	{
		for (uint32_t id : *v)
		{
			DROID *d = opDroid(id);
			if (d != nullptr) { send(d, DroidOrder(DORDER_MOVE, spread(deploy, i++)), false); }
		}
	}
	char msg[200];
	snprintf(msg, sizeof(msg), _("Salvage team: %u salvage units, %u escorts - gathering at the deployment point"),
	         static_cast<unsigned>(s_op.towers.size() + s_op.collectors.size()), static_cast<unsigned>(s_op.escorts.size()));
	addConsoleMessage(msg, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
}

void opPrune(std::vector<uint32_t> &v, bool dropCarriers)
{
	v.erase(std::remove_if(v.begin(), v.end(), [dropCarriers](uint32_t id) {
		DROID *d = opDroid(id);
		return d == nullptr || (dropCarriers && modrecover::carrying(d));
	}), v.end());
}

void rememberPlan(const DROID *d, const DroidOrder &o, const Vector2i &anchor, int face = -1)
{
	PlanTask t;
	t.order = o;
	t.anchor = anchor;
	t.born = gameTime;
	t.face = face;
	t.sent = gameTime;
	s_plan[d->id] = t;
}

// MOD (Wunsch 19.11. 16:44): "Die Einheiten in der Defensive line sollen sich zum schluss in Richtung der Stäbe drehen."
// Wer seinen Platz auf der Verteidigungslinie erreicht hat (kein Befehl, steht still, kaempft nicht, hoechstens 2 Felder vom Platz),
// dreht sich sichtbar (gut 100 Grad pro Sekunde) in die Richtung der Querstriche - also zur Feindseite. Einmal pro Ankunft:
// verlaesst die Einheit ihren Platz (Kampf, Spielerbefehl) und kommt zurueck, dreht sie sich wieder ein.
unsigned s_nextFace = 0;
void faceTick()
{
	if (gameTime < s_nextFace || selectedPlayer >= MAX_PLAYERS) { return; }
	s_nextFace = gameTime + 150;
	const int64_t near2 = static_cast<int64_t>(2 * DIST_TILE) * (2 * DIST_TILE);
	for (auto &kv : s_plan)
	{
		PlanTask &t = kv.second;
		if (t.face < 0) { continue; }
		DROID *d = IdToDroid(gameWorld.objects, kv.first, selectedPlayer);
		if (d == nullptr || d->died) { continue; }
		const bool atSpot = dist2(d->pos.xy(), t.anchor) <= near2;
		// (der Linienbefehl selbst zaehlt als Spielerbefehl - nur ein spaeterer Befehl des Spielers hebt die Blickrichtung auf)
		const unsigned sincePlayer = modcontrol::sincePlayerOrder(d);
		if (sincePlayer != 0xffffffffu && sincePlayer + 800u < gameTime - t.sent) { t.face = -1; continue; }
		const bool idle = d->order.type == DORDER_NONE && d->action == DACTION_NONE && d->sMove.Status == MOVEINACTIVE;
		if (!atSpot || !idle) { if (!atSpot) { t.faced = false; } continue; }
		if (t.faced) { continue; }
		const uint16_t want = static_cast<uint16_t>(t.face);
		const int16_t diff = static_cast<int16_t>(want - d->rot.direction);
		if (std::abs(static_cast<int>(diff)) <= DEG(2)) { d->rot.direction = want; t.faced = true; continue; }
		const int step = std::clamp<int>(diff, -DEG(16), DEG(16));
		d->rot.direction = static_cast<uint16_t>(d->rot.direction + step);
	}
}

void planTick()
{
	if (gameTime < s_nextPlanCheck || selectedPlayer >= MAX_PLAYERS) { return; }
	s_nextPlanCheck = gameTime + 2000;
	const int64_t far2 = static_cast<int64_t>(6 * DIST_TILE) * (6 * DIST_TILE);
	for (auto it = s_plan.begin(); it != s_plan.end();)
	{
		DROID *d = IdToDroid(gameWorld.objects, it->first, selectedPlayer);
		if (d == nullptr || d->died || gameTime - it->second.born > PLAN_LIFE) { it = s_plan.erase(it); continue; }
		if (d->order.type == DORDER_NONE && !modcontrol::manual(d) && !inOp(d->id) && !modrecover::carrying(d)
		    && dist2(d->pos.xy(), it->second.anchor) > far2)
		{
			send(d, it->second.order, false);   // zurueck zum Oberbefehl
			it->second.sent = gameTime;
		}
		++it;
	}
}

// MOD (19.11. 17:34): Minen raeumen im Gebiet. Jede Einheit faehrt/laeuft ihre Bahnen (Wegpunkte) ab, einer nach dem anderen. Infanterie
// sieht eine feindliche Mine (2,5 Felder), geht hin und bleibt stehen, bis sie entschaerft ist (modmines: 3 s), dann geht es weiter.
// Raeumpanzer fahren mit drehender Walze. Ein eigener Spielerbefehl (modcontrol) beendet den Auftrag der Einheit.
struct SweepJob
{
	std::vector<Vector2i> wp;
	size_t idx = 0;
	uint32_t mine = 0;          // Infanterie: gerade angesteuerte Mine
	unsigned lastSend = 0;
	bool resend = false;
	bool flail = false;
};
std::map<uint32_t, SweepJob> s_sweep;
unsigned s_nextSweep = 0;

bool insidePoly(const std::vector<Vector2i> &poly, int64_t x, int64_t y);

const STRUCTURE *enemyMineNear(const DROID *d, int r, const std::map<uint32_t, SweepJob> &jobs)
{
	static int s_mineIdx = -2;
	if (s_mineIdx == -2) { s_mineIdx = getStructStatFromName(WzString::fromUtf8("A0Mine")); }
	if (s_mineIdx < 0) { return nullptr; }
	const STRUCTURE_STATS *ms = &asStructureStats[s_mineIdx];
	const STRUCTURE *best = nullptr;
	int64_t bestD = static_cast<int64_t>(r) * r;
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		if (p == d->player || aiCheckAlliances(p, d->player)) { continue; }
		for (const STRUCTURE *st : gameWorld.objects.structures[p])
		{
			if (st == nullptr || st->died || st->pStructureType != ms || st->visible[d->player] == 0) { continue; }
			const int64_t dd = dist2(d->pos.xy(), st->pos.xy());
			if (dd >= bestD) { continue; }
			bool taken = false;
			for (const auto &kv : jobs) { if (kv.first != d->id && kv.second.mine == st->id) { taken = true; break; } }
			if (!taken) { best = st; bestD = dd; }
		}
	}
	return best;
}

void sweepTick()
{
	if (s_sweep.empty() || gameTime < s_nextSweep || selectedPlayer >= MAX_PLAYERS) { return; }
	s_nextSweep = gameTime + 400;
	const int64_t reach2 = static_cast<int64_t>(DIST_TILE * 6 / 5) * (DIST_TILE * 6 / 5);
	for (auto it = s_sweep.begin(); it != s_sweep.end();)
	{
		DROID *d = IdToDroid(gameWorld.objects, it->first, selectedPlayer);
		SweepJob &j = it->second;
		if (d == nullptr || d->died) { it = s_sweep.erase(it); continue; }
		// eigene Befehle kommen ueber die Befehlswarteschlange zurueck und zaehlen dort als Spielerbefehl - nur ein Befehl, der
		// deutlich nach unserem letzten kam, ist ein Eingriff des Spielers (dann endet der Raeumauftrag dieser Einheit)
		const unsigned sincePlayer = modcontrol::sincePlayerOrder(d);
		if (j.lastSend != 0 && sincePlayer != 0xffffffffu && sincePlayer + 800u < gameTime - j.lastSend) { it = s_sweep.erase(it); continue; }
		// Infanterie: entdeckte Mine zuerst
		if (!j.flail)
		{
			if (j.mine != 0)
			{
				const STRUCTURE *ms = IdToStruct(j.mine, ANYPLAYER);
				if (ms == nullptr || ms->died) { j.mine = 0; j.resend = true; }
				else { ++it; continue; }   // steht an der Mine und entschaerft
			}
			if (const STRUCTURE *mine = enemyMineNear(d, DIST_TILE * 5 / 2, s_sweep))
			{
				j.mine = mine->id;
				send(d, DroidOrder(DORDER_MOVE, mine->pos.xy()), false);
				j.lastSend = gameTime;
				++it;
				continue;
			}
		}
		while (j.idx < j.wp.size() && dist2(d->pos.xy(), j.wp[j.idx]) <= reach2) { ++j.idx; j.resend = true; }
		if (j.idx >= j.wp.size())
		{
			if (j.flail && modmines::flailing(d)) { secondarySetState(d, gameWorld.objects, DSO_MINECLEAR, DSS_NONE); }
			it = s_sweep.erase(it);
			if (s_sweep.empty()) { addConsoleMessage(_("Mine sweep finished"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
			continue;
		}
		// naechsten Wegpunkt ansteuern (neu senden, wenn die Einheit steht, oder alle 8 s als Sicherheit)
		if (j.resend || j.lastSend == 0 || (d->order.type == DORDER_NONE && gameTime - j.lastSend > 1500) || gameTime - j.lastSend > 8000)
		{
			send(d, DroidOrder(DORDER_MOVE, j.wp[j.idx]), false);
			j.lastSend = gameTime;
			j.resend = false;
		}
		++it;
	}
}

/// Bahnen durch die Flaeche (hin und her, Abstand `lane`), nur Punkte im Gebiet; Bahnen reihum auf `n` Einheiten verteilt
std::vector<std::vector<Vector2i>> sweepLanes(const std::vector<Vector2i> &poly, int lane, size_t n)
{
	std::vector<std::vector<Vector2i>> out(std::max<size_t>(1, n));
	int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
	for (const Vector2i &p : poly) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
	if (x1 <= x0 || y1 <= y0) { return out; }
	const bool alongX = (x1 - x0) >= (y1 - y0);   // Bahnen laengs der laengeren Seite
	const int a0 = alongX ? y0 : x0, a1 = alongX ? y1 : x1;    // quer
	const int b0 = alongX ? x0 : y0, b1 = alongX ? x1 : y1;    // laengs
	std::vector<std::vector<Vector2i>> lanes;
	for (int a = a0 + lane / 2; a <= a1; a += lane)
	{
		std::vector<Vector2i> ln;
		for (int b = b0; b <= b1; b += DIST_TILE)
		{
			const int x = alongX ? b : a, y = alongX ? a : b;
			if (insidePoly(poly, x, y)) { ln.emplace_back(x, y); }
		}
		if (ln.empty()) { continue; }
		// nur Anfang und Ende jeder Bahn (gerade Strecke), lange Bahnen in Stuecken von 6 Feldern (Wegsuche bleibt kurz)
		std::vector<Vector2i> pts;
		for (size_t k = 0; k < ln.size(); k += 6) { pts.push_back(ln[k]); }
		if (pts.back() != ln.back()) { pts.push_back(ln.back()); }
		if (lanes.size() % 2 == 1) { std::reverse(pts.begin(), pts.end()); }
		lanes.push_back(pts);
	}
	if (lanes.empty()) { out[0].push_back(centroid(poly)); return out; }
	// zusammenhaengende Bahnbloecke je Einheit (kurze Wege zwischen den Bahnen)
	const size_t per = (lanes.size() + out.size() - 1) / out.size();
	for (size_t i = 0; i < lanes.size(); ++i)
	{
		auto &dst = out[std::min(out.size() - 1, i / std::max<size_t>(1, per))];
		dst.insert(dst.end(), lanes[i].begin(), lanes[i].end());
	}
	return out;
}

void opTick()
{
	if (!s_op.active || gameTime < s_op.nextStep) { return; }
	s_op.nextStep = gameTime + OP_STEP_MS;
	opPrune(s_op.escorts, false);
	opPrune(s_op.collectors, true);   // traegt ein Teil zur Werkstatt -> faellt aus dem Trupp
	opPrune(s_op.towers, true);       // hat ein Wrack geladen -> faehrt zur Werkstatt
	const unsigned age = gameTime - s_op.phaseStart;
	if (s_op.phase == 0)
	{
		bool all = true;
		const int64_t near2 = static_cast<int64_t>(5 * DIST_TILE) * (5 * DIST_TILE);
		for (const auto *v : { &s_op.escorts, &s_op.collectors, &s_op.towers })
		{
			for (uint32_t id : *v) { DROID *d = opDroid(id); if (d != nullptr && dist2(d->pos.xy(), s_op.deploy) > near2) { all = false; } }
		}
		if (s_op.collectors.empty() && s_op.towers.empty()) { addConsoleMessage(_("Salvage team lost"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); opEnd(true); return; }
		if (all || age >= OP_GATHER_MAX)
		{
			s_op.phase = 1;
			s_op.phaseStart = gameTime;
			size_t i = 0;
			for (uint32_t id : s_op.escorts) { DROID *d = opDroid(id); if (d != nullptr) { send(d, DroidOrder(DORDER_SCOUT, spread(s_op.c, i++)), false); } }
			for (const auto *v : { &s_op.collectors, &s_op.towers })
			{
				for (uint32_t id : *v) { DROID *d = opDroid(id); if (d != nullptr) { send(d, DroidOrder(DORDER_MOVE, spread(s_op.c, i++)), false); } }
			}
			addConsoleMessage(_("Salvage team assembled - moving out"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		}
		return;
	}
	std::vector<FEATURE *> wrecks, pickups;
	opItems(wrecks, pickups);
	if (s_op.phase == 1)
	{
		const int64_t arrive2 = static_cast<int64_t>(s_op.r + 2 * DIST_TILE) * (s_op.r + 2 * DIST_TILE);
		bool arrived = false;
		for (const auto *v : { &s_op.collectors, &s_op.towers })
		{
			for (uint32_t id : *v) { DROID *d = opDroid(id); if (d != nullptr && dist2(d->pos.xy(), s_op.c) <= arrive2) { arrived = true; } }
		}
		if (s_op.collectors.empty() && s_op.towers.empty()) { opEnd(true); return; }
		if (!arrived && age < OP_GO_MAX) { return; }
		s_op.phase = 2;
		s_op.phaseStart = gameTime;
	}
	// Phase 2: bergen - jeder freie Berger holt das naechste noch nicht vergebene Ding
	std::vector<uint32_t> taken;
	auto busyWith = [](const DROID *d) -> uint32_t {
		return (d->order.type == DORDER_RECOVER && d->order.psObj != nullptr && !d->order.psObj->died) ? d->order.psObj->id : 0u;
	};
	for (const auto *v : { &s_op.collectors, &s_op.towers })
	{
		for (uint32_t id : *v) { DROID *d = opDroid(id); if (d != nullptr) { const uint32_t f = busyWith(d); if (f != 0) { taken.push_back(f); } } }
	}
	auto assign = [&](std::vector<uint32_t> &units, std::vector<FEATURE *> &items) {
		for (uint32_t id : units)
		{
			DROID *d = opDroid(id);
			if (d == nullptr || busyWith(d) != 0) { continue; }
			FEATURE *best = nullptr;
			int64_t bestD = 0;
			for (FEATURE *f : items)
			{
				if (std::find(taken.begin(), taken.end(), f->id) != taken.end()) { continue; }
				const int64_t dd = dist2(d->pos.xy(), f->pos.xy());
				if (best == nullptr || dd < bestD) { best = f; bestD = dd; }
			}
			if (best == nullptr) { continue; }
			taken.push_back(best->id);
			send(d, DroidOrder(DORDER_RECOVER, best), false);
		}
	};
	assign(s_op.towers, wrecks);
	assign(s_op.collectors, pickups);
	if (s_op.collectors.empty() && !pickups.empty()) { assign(s_op.escorts, pickups); }   // keine Berger mehr: die Eskorte sammelt Beute/Artefakte
	// Eskorte: sichert das Gebiet (alle 15 s ein neuer Punkt im Kreis)
	if (gameTime >= s_op.nextEscort)
	{
		s_op.nextEscort = gameTime + 15000;
		size_t i = 0;
		for (uint32_t id : s_op.escorts)
		{
			DROID *d = opDroid(id);
			if (d == nullptr || busyWith(d) != 0 || (d->order.type != DORDER_NONE && d->order.type != DORDER_SCOUT)) { ++i; continue; }
			const uint16_t ang = static_cast<uint16_t>((gameTime / 15000u) * 9973u + i * 21011u);
			const int rr = s_op.r * static_cast<int>(1 + (i * 37u + gameTime / 15000u) % 3u) / 4;
			send(d, DroidOrder(DORDER_SCOUT, Vector2i(s_op.c.x + iSinR(ang, rr), s_op.c.y + iCosR(ang, rr))), false);
			++i;
		}
	}
	const bool workLeft = (!wrecks.empty() && !s_op.towers.empty()) || !pickups.empty();
	bool anyBusy = false;
	for (const auto *v : { &s_op.collectors, &s_op.towers, &s_op.escorts })
	{
		for (uint32_t id : *v) { DROID *d = opDroid(id); if (d != nullptr && busyWith(d) != 0) { anyBusy = true; } }
	}
	if ((!workLeft && !anyBusy) || (s_op.towers.empty() && s_op.collectors.empty() && pickups.empty()) || gameTime - s_op.phaseStart >= OP_WORK_MAX)
	{
		addConsoleMessage(_("Salvage complete - team returning"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		opEnd(true);
	}
}

std::vector<Vector2i> circlePts(const Vector2i &c, int r, int n)
{
	std::vector<Vector2i> p;
	for (int k = 0; k < n; ++k)
	{
		const uint16_t a = static_cast<uint16_t>(k * 65536 / n);
		p.emplace_back(c.x + iSinR(a, r), c.y + iCosR(a, r));
	}
	return p;
}

// ------------------------------------------------------------------------------------------------- Auftraege

// MOD (17.11. 18:12/18:13): Stellen fuer Panzersperren (Schachbrett - Fahrzeuge kommen nicht durch, auch nicht schraeg) bzw. Minen
// (verstreut, etwa jedes dritte Feld) in der gezeichneten Flaeche; nur freie, befahrbare Felder
bool insidePoly(const std::vector<Vector2i> &poly, int64_t x, int64_t y)
{
	bool in = false;
	for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
	{
		const int64_t xi = poly[i].x, yi = poly[i].y, xj = poly[j].x, yj = poly[j].y;
		if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) { in = !in; }
	}
	return in;
}

std::vector<Vector2i> fieldSpots(const std::vector<Vector2i> &raw, bool mines)
{
	std::vector<Vector2i> out;
	if (raw.size() < 3) { return out; }
	int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
	for (const Vector2i &p : raw) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
	// (20.11.) Raster in Plan-Kacheln (DIST_TILE = Original-Kachel): ein Igel/eine Mine belegt eine ganze Plan-Kachel - in der Viertel-Fassung
	// lag das Schachbrett auf Feinfeldern und die Bauwerke ueberlappten (viele Bauauftraege schlugen fehl)
	const int pw = world_coord(gameWorld.map.width) / DIST_TILE, ph = world_coord(gameWorld.map.height) / DIST_TILE;
	const int tx0 = std::max(1, std::max(0, x0) / DIST_TILE), ty0 = std::max(1, std::max(0, y0) / DIST_TILE);
	const int tx1 = std::min(pw - 2, std::max(0, x1) / DIST_TILE), ty1 = std::min(ph - 2, std::max(0, y1) / DIST_TILE);
	if (tx1 < tx0 || ty1 < ty0 || (tx1 - tx0) * (ty1 - ty0) > 4096) { return out; }
	for (int ty = ty0; ty <= ty1; ++ty)
	{
		for (int tx = tx0; tx <= tx1; ++tx)
		{
			const int cx = tx * DIST_TILE + DIST_TILE / 2, cy = ty * DIST_TILE + DIST_TILE / 2;
			if (!insidePoly(raw, cx, cy)) { continue; }
			if (mines)
			{
				uint32_t h = static_cast<uint32_t>(tx) * 73856093u ^ static_cast<uint32_t>(ty) * 19349663u;
				h *= 2654435761u;
				if ((h >> 16) % 100u >= 36u) { continue; }
			}
			else if ((tx + ty) % 2 != 0) { continue; }
			bool ok = true;   // alle Kartenfelder der Plan-Kachel frei
			for (int fy = map_coord(ty * DIST_TILE); ok && fy <= map_coord(ty * DIST_TILE + DIST_TILE - 1); ++fy)
			{
				for (int fx = map_coord(tx * DIST_TILE); ok && fx <= map_coord(tx * DIST_TILE + DIST_TILE - 1); ++fx)
				{
					const MAPTILE *t = mapTile(gameWorld.map, fx, fy);
					ok = !(t == nullptr || TileHasStructure(t) || TileHasFeature(t) || terrainType(t) == TER_WATER || terrainType(t) == TER_CLIFFFACE);
				}
			}
			if (!ok) { continue; }
			out.emplace_back(cx, cy);
		}
	}
	// (Wunsch 22.11. 15:59 "Die Minen und Panzerfallen sollen weiter auseinander verteilt sein bis fast an den Rand"): bei zu vielen Stellen
	// nicht mehr nur die mittleren behalten (das ergab einen Klumpen in der Mitte), sondern gleichmaessig ueber die ganze Flaeche verteilen:
	// immer die Stelle nehmen, die am weitesten von allen schon gewaehlten entfernt ist (beginnt am Rand, fuellt dann die Luecken)
	const size_t cap = mines ? 44 : 64;
	if (out.size() > cap)
	{
		std::vector<Vector2i> pick;
		std::vector<int64_t> best(out.size(), INT64_MAX);
		const Vector2i c = centroid(raw);
		size_t cur = 0;
		for (size_t i = 1; i < out.size(); ++i) { if (dist2(out[i], c) > dist2(out[cur], c)) { cur = i; } }
		while (pick.size() < cap)
		{
			pick.push_back(out[cur]);
			best[cur] = -1;
			size_t nxt = cur;
			int64_t farthest = -1;   // nicht "far": unter Windows ein Makro
			for (size_t i = 0; i < out.size(); ++i)
			{
				if (best[i] < 0) { continue; }
				best[i] = std::min(best[i], dist2(out[i], out[cur]));
				if (best[i] > farthest) { farthest = best[i]; nxt = i; }
			}
			if (farthest < 0) { break; }
			cur = nxt;
		}
		out.swap(pick);
	}
	return out;
}

// (Wunsch 22.11. 15:59 "Diese Schlenker beim Auswahlgebiet zeichnen duerfen nicht mehr vorkommen"): gezeichnete Flaechen werden sauber
// geschlossen - Anlauf am Anfang und Ueberschwung am Ende abschneiden (dort, wo Anfang und Ende sich am naechsten kommen), dann Schleifen
// entfernen (kreuzt sich der Umriss, bleibt nur die groesste Schleife)
namespace
{
double polyArea(const std::vector<glm::dvec2> &p)
{
	double a = 0.0;
	for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) { a += p[j].x * p[i].y - p[i].x * p[j].y; }
	return std::fabs(a) * 0.5;
}

bool segCross(const glm::dvec2 &a, const glm::dvec2 &b, const glm::dvec2 &c, const glm::dvec2 &d, glm::dvec2 &x)
{
	const glm::dvec2 r = b - a, s = d - c;
	const double den = r.x * s.y - r.y * s.x;
	if (std::fabs(den) < 1e-9) { return false; }
	const double t = ((c.x - a.x) * s.y - (c.y - a.y) * s.x) / den;
	const double u = ((c.x - a.x) * r.y - (c.y - a.y) * r.x) / den;
	if (t <= 1e-6 || t >= 1.0 - 1e-6 || u <= 1e-6 || u >= 1.0 - 1e-6) { return false; }
	x = a + r * t;
	return true;
}
}

std::vector<Vector2i> cleanLoop(const std::vector<Vector2i> &raw)
{
	if (raw.size() < 8) { return raw; }
	const size_t n0 = raw.size();
	// 1. Anlauf/Ueberschwung: naechstes Paar aus erstem und letztem Viertel
	size_t bi = 0, bj = n0 - 1;
	int64_t bd = INT64_MAX;
	for (size_t i = 0; i <= n0 / 4; ++i)
	{
		for (size_t j = n0 - 1 - n0 / 4; j < n0; ++j)
		{
			if (j <= i + 2) { continue; }
			const int64_t d = dist2(raw[i], raw[j]);
			if (d < bd) { bd = d; bi = i; bj = j; }
		}
	}
	std::vector<glm::dvec2> p;
	for (size_t i = bi; i <= bj; ++i) { p.emplace_back(raw[i].x, raw[i].y); }
	// 2. Schleifen: bei jeder Kreuzung die groessere Teilschleife behalten (hoechstens einige Durchgaenge)
	for (int pass = 0; pass < 6 && p.size() >= 4; ++pass)
	{
		const size_t n = p.size();
		std::vector<glm::dvec2> bestLoop;
		double bestA = -1.0;
		for (size_t a = 0; a < n; ++a)
		{
			const glm::dvec2 A0 = p[a], A1 = p[(a + 1) % n];
			for (size_t b = a + 2; b < n; ++b)
			{
				if (a == 0 && b == n - 1) { continue; }   // ueber den Schluss benachbart
				glm::dvec2 x;
				if (!segCross(A0, A1, p[b], p[(b + 1) % n], x)) { continue; }
				std::vector<glm::dvec2> in, out;
				in.push_back(x);
				for (size_t k = a + 1; k <= b; ++k) { in.push_back(p[k]); }
				out.push_back(x);
				for (size_t k = b + 1; k < n; ++k) { out.push_back(p[k]); }
				for (size_t k = 0; k <= a; ++k) { out.push_back(p[k]); }
				const double ai = in.size() >= 3 ? polyArea(in) : 0.0, ao = out.size() >= 3 ? polyArea(out) : 0.0;
				std::vector<glm::dvec2> &keep = ai >= ao ? in : out;
				const double ak = std::max(ai, ao);
				// die erste Kreuzung reicht fuer diesen Durchgang (die groessere Haelfte); unter mehreren die mit der groessten Restflaeche
				if (ak > bestA) { bestA = ak; bestLoop = keep; }
			}
		}
		if (bestA < 0.0) { break; }
		p.swap(bestLoop);
	}
	std::vector<Vector2i> out;
	for (const glm::dvec2 &q : p) { out.emplace_back(static_cast<int>(std::lround(q.x)), static_cast<int>(std::lround(q.y))); }
	if (out.size() < 3) { return raw; }
	return out;
}

// Verteidigungslinie: die Querstriche zeigen zur Feindseite = weg von der eigenen Basis (Mitte der eigenen Bauwerke)
int defendSide(const std::vector<Vector2i> &pts)
{
	if (pts.size() < 2 || selectedPlayer >= MAX_PLAYERS) { return 1; }
	int64_t sx = 0, sy = 0, cnt = 0;
	for (const STRUCTURE *st : gameWorld.objects.structures[selectedPlayer]) { if (st != nullptr && !st->died) { sx += st->pos.x; sy += st->pos.y; ++cnt; } }
	if (cnt == 0)
	{
		for (const DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d != nullptr && !d->died) { sx += d->pos.x; sy += d->pos.y; ++cnt; } }
	}
	if (cnt == 0) { return 1; }
	const int64_t hx0 = sx / cnt, hy0 = sy / cnt;
	int64_t acc = 0;
	for (size_t i = 1; i < pts.size(); ++i)
	{
		const int64_t tx = pts[i].x - pts[i - 1].x, ty = pts[i].y - pts[i - 1].y;
		const int64_t hx = hx0 - pts[i].x, hy = hy0 - pts[i].y;
		acc += (tx * hy - ty * hx) / 64;
	}
	return acc > 0 ? -1 : 1;   // Normale (-ty, tx) zeigt zur Basis, wenn acc > 0 -> Striche auf die andere Seite
}

/// MOD (20.11./21.11.): Bauplan (Grabennetz, Gleisstrecke) auf die Bauer verteilen - jeder Bauer bekommt einen zusammenhaengenden
/// Abschnitt (ungefaehr gleich viele Felder), in der Reihenfolge ihrer Lage an der Linie - niemand laeuft hin und her.
void distributePlan(const std::vector<DROID *> &builders, const std::vector<modbuild::Placement> &plan)
{
	if (!plan.empty())
	{
		auto worldOfPlan = [](const Vector2i &t) { return Vector2i(t.x * DIST_TILE + DIST_TILE / 2, t.y * DIST_TILE + DIST_TILE / 2); };
		std::vector<int64_t> cum(plan.size() + 1, 0);
		for (size_t k = 0; k < plan.size(); ++k)
		{
			cum[k + 1] = cum[k] + 1 + std::abs(plan[k].tile2.x - plan[k].tile.x) + std::abs(plan[k].tile2.y - plan[k].tile.y);
		}
		// Lage eines Bauers an der Linie = Index des naechsten Planeintrags
		std::vector<std::pair<size_t, DROID *>> order;
		for (DROID *b : builders)
		{
			size_t best = 0;
			int64_t bd = INT64_MAX;
			for (size_t k = 0; k < plan.size(); ++k)
			{
				const int64_t dd = dist2(b->pos.xy(), worldOfPlan(plan[k].tile));
				if (dd < bd) { bd = dd; best = k; }
			}
			order.emplace_back(best, b);
		}
		std::stable_sort(order.begin(), order.end(), [](const std::pair<size_t, DROID *> &a, const std::pair<size_t, DROID *> &b) {
			return a.first != b.first ? a.first < b.first : a.second->id < b.second->id;
		});
		const size_t nb = std::min(order.size(), plan.size());
		const int64_t total = cum.back();
		for (size_t bi = 0; bi < order.size(); ++bi)
		{
			// mehr Bauer als Planeintraege: die uebrigen helfen im Abschnitt eines anderen mit
			const size_t chunk = bi % nb;
			const int64_t lo = total * static_cast<int64_t>(chunk) / static_cast<int64_t>(nb);
			const int64_t hi = total * static_cast<int64_t>(chunk + 1) / static_cast<int64_t>(nb);
			bool first = true;
			for (size_t k = 0; k < plan.size(); ++k)
			{
				const int64_t mid = (cum[k] + cum[k + 1]) / 2;   // Eintrag gehoert zum Abschnitt, in dem seine Mitte liegt
				if (mid < lo || mid >= hi) { continue; }
				const modbuild::Placement &pl = plan[k];
				send(order[bi].second, DroidOrder(DORDER_LINEBUILD, pl.stats, worldOfPlan(pl.tile), worldOfPlan(pl.tile2), static_cast<uint16_t>(pl.direction)), !first);
				first = false;
			}
		}
	}
}

void applyStroke(Mode m, const std::vector<Vector2i> &raw)
{
	s_lastPlanned = false;
	if (raw.empty()) { return; }
	// MOD (Wunsch 22.11. 01:11): Angriffsplaner - Zonen, Leuchtfeuer, Brueckenkopf und Tunnel immer; Angriffslinie und Angriffsvektor,
	// wenn keine Einheit gewaehlt ist (mit Auswahl bleiben sie Sofort-Befehle wie bisher)
	bool anySel = false;
	if (selectedPlayer < MAX_PLAYERS) { for (const DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d != nullptr && !d->died && d->selected) { anySel = true; break; } } }
	if (m == M_STAGING || m == M_BRIDGEHEAD || m == M_FERRY || m == M_TUNNEL || ((m == M_ATTACK || m == M_OFFENSE) && !anySel))
	{
		plan::fromStroke(m, raw);
		s_lastPlanned = true;
		return;
	}
	if (m == M_RECOVER)
	{
		const Vector2i c = centroid(raw);
		int64_t far2 = 0;
		for (const Vector2i &p : raw) { far2 = std::max(far2, dist2(p, c)); }
		const int r = std::clamp(static_cast<int>(std::sqrt(static_cast<double>(far2))), 3 * DIST_TILE, 16 * DIST_TILE);
		startSalvage(c, r);
		return;
	}
	std::vector<DROID *> units = pickUnits(m, raw);
	if (s_append && !s_chain.empty() && selectedPlayer < MAX_PLAYERS)
	{
		// (18.11.) weitere Linie mit Shift: ohne Auswahl behalten die Einheiten der vorigen Linien ihre Linie
		bool anySelected = false;
		for (const DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d != nullptr && d->selected) { anySelected = true; break; } }
		if (!anySelected)
		{
			units.erase(std::remove_if(units.begin(), units.end(), [](const DROID *d) {
				return std::find(s_chain.begin(), s_chain.end(), d->id) != s_chain.end();
			}), units.end());
		}
	}
	const Vector2i c = centroid(raw);
	if (units.empty())
	{
		return;
	}
	for (const DROID *d : units) { s_chain.push_back(d->id); }
	switch (m)
	{
	case M_MOVE:
	case M_ATTACK:
	{
		const std::vector<Vector2i> wp = resample(raw, std::min<size_t>(5, std::max<size_t>(2, raw.size())));
		const DROID_ORDER ord = (m == M_MOVE) ? DORDER_MOVE : DORDER_SCOUT;
		for (size_t i = 0; i < units.size(); ++i)
		{
			for (size_t k = 1; k < wp.size(); ++k)
			{
				const Vector2i p = (k + 1 == wp.size()) ? spread(wp[k], i) : wp[k];
				send(units[i], DroidOrder(ord, p), k > 1);
				if (k + 1 == wp.size()) { rememberPlan(units[i], DroidOrder(ord, p), p); }
			}
		}
		break;
	}
	case M_RALLY:
		for (size_t i = 0; i < units.size(); ++i) { send(units[i], DroidOrder(DORDER_MOVE, spread(c, i)), false); rememberPlan(units[i], DroidOrder(DORDER_MOVE, spread(c, i)), spread(c, i)); }
		break;
	case M_DEFEND:
	{
		// (21.11. 00:21) nur Aufstellung: keine Bauwerke, Bauer werden nicht bewegt (Grabennetz = eigener Befehl M_DEFNET)
		std::vector<DROID *> fighters;
		for (DROID *d : units) { if (!isBuilder(d) || isCombat(d)) { fighters.push_back(d); } }
		// Kaempfer verteilen sich gleichmaessig auf der Linie
		const std::vector<Vector2i> spots = resample(raw, std::max<size_t>(1, fighters.size()));
		// Blickrichtung am Platz = Richtung der Querstriche (Normale der Linie zur Feindseite, wie tickSegs sie zeichnet)
		const int fside = defendSide(raw);
		const std::vector<Vector2i> fline = resample(raw, std::max<size_t>(24, raw.size()));
		auto faceAt = [&fline, fside](const Vector2i &p) {
			size_t bi = 0;
			int64_t bd = INT64_MAX;
			for (size_t k = 0; k < fline.size(); ++k) { const int64_t dd = dist2(fline[k], p); if (dd < bd) { bd = dd; bi = k; } }
			const Vector2i a = fline[bi > 0 ? bi - 1 : 0], b = fline[std::min(bi + 1, fline.size() - 1)];
			const int64_t tx = b.x - a.x, ty = b.y - a.y;
			if (tx == 0 && ty == 0) { return -1; }
			return static_cast<int>(iAtan2(static_cast<int32_t>(-ty * fside), static_cast<int32_t>(tx * fside)));
		};
		for (size_t i = 0; i < fighters.size(); ++i) { send(fighters[i], DroidOrder(DORDER_MOVE, spots[i]), false); rememberPlan(fighters[i], DroidOrder(DORDER_MOVE, spots[i]), spots[i], faceAt(spots[i])); }
		break;
	}
	case M_DEFNET:
	{
		// MOD (Wunsch 21.11. 00:21): eigener Befehl fuer das Grabennetz - nur Bauer, die Kampfeinheiten bleiben, wo sie sind
		std::vector<DROID *> builders;
		for (DROID *d : units) { if (isBuilder(d)) { builders.push_back(d); } }
		const int fside = defendSide(raw);
		// MOD (Wunsch 20.11.): Bauer heben ein Grabennetz aus - Hauptgraben lueckenlos entlang der Linie, Gruben, Verbindungsgraeben nach
		// hinten; alle Stellungen mit der Front zu den Querstrichen (modbuild::planDefenseNet). Jeder Bauer bekommt einen zusammenhaengenden
		// Abschnitt (ungefaehr gleich viele Felder), in der Reihenfolge ihrer Lage an der Linie - niemand laeuft hin und her.
		if (!builders.empty() && selectedPlayer < MAX_PLAYERS)
		{
			distributePlan(builders, modbuild::planDefenseNet(selectedPlayer, raw, fside));
		}
		break;
	}
	case M_RAIL:
	{
		// MOD (21.11., Zugsystem): Bauer legen eine Gleisstrecke entlang der Linie (modbuild::planRailLine), in zusammenhaengenden Abschnitten
		std::vector<DROID *> builders;
		for (DROID *d : units) { if (isBuilder(d)) { builders.push_back(d); } }
		if (!builders.empty() && selectedPlayer < MAX_PLAYERS)
		{
			const std::vector<modbuild::Placement> plan = modbuild::planRailLine(selectedPlayer, raw);
			if (plan.empty()) { addConsoleMessage(_("Railway: no free ground for a track there (or rails are not available yet)"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
			distributePlan(builders, plan);
		}
		break;
	}
	case M_OFFENSE:
	{
		// MOD (Wunsch 17.11. 18:37): "Im Prinzip das Gleiche wie die Verteidigungslinie, aber das Netzwerk ist auf Offensive ausgerichtet:
		// mehr Einheiten, weniger starre Verteidigung (aber nicht ganz ohne)."
		//  - Kampfeinheiten (auch Infanterie mit Spaten) sammeln sich in zwei Reihen auf der Linie (Angriffsbewegung: kaempfen unterwegs)
		//    und verfolgen danach Gegner von selbst;
		//  - nur Baufahrzeuge bauen (gibt es keine, graben hoechstens 3 Infanteristen): drei kurze Grabenstuecke und zwei MG-Posten
		//    zur Feindseite.
		std::vector<DROID *> fighters, builders;
		for (DROID *d : units) { if (isTruck(d) && !isAir(d)) { builders.push_back(d); } else if (isCombat(d) || modcombot::isModular(d)) { fighters.push_back(d); } }
		if (builders.empty())
		{
			for (DROID *d : units)
			{
				if (builders.size() < 3 && modbuild::isInfantryBuilder(d)) { builders.push_back(d); }
			}
			fighters.erase(std::remove_if(fighters.begin(), fighters.end(), [&builders](DROID *d) { return std::find(builders.begin(), builders.end(), d) != builders.end(); }), fighters.end());
		}
		const int side = defendSide(raw);
		const std::vector<Vector2i> line = resample(raw, std::max<size_t>(9, raw.size()));
		// Feindrichtung an einer Stelle der Linie (Normale zur Seite der Querstriche)
		auto enemyDir = [&line, side](size_t i) {
			const Vector2i a = line[i > 0 ? i - 1 : 0], b = line[std::min(i + 1, line.size() - 1)];
			const double tx = b.x - a.x, ty = b.y - a.y, l = std::max(1.0, std::sqrt(tx * tx + ty * ty));
			return std::make_pair(-ty / l * side, tx / l * side);
		};
		const bool twoRanks = fighters.size() > 10;
		const size_t front = twoRanks ? (fighters.size() + 1) / 2 : fighters.size();
		const std::vector<Vector2i> spotsA = resample(raw, std::max<size_t>(1, front));
		const std::vector<Vector2i> spotsB = resample(raw, std::max<size_t>(1, fighters.size() - front));
		for (size_t i = 0; i < fighters.size(); ++i)
		{
			Vector2i p = (i < front) ? spotsA[i] : spotsB[i - front];
			if (i >= front)
			{
				// zweite Reihe gut ein Feld hinter der Linie
				const size_t li = std::min(line.size() - 1, (i - front) * line.size() / std::max<size_t>(1, fighters.size() - front));
				const auto e = enemyDir(li);
				p.x -= static_cast<int>(e.first * DIST_TILE * 1.2);
				p.y -= static_cast<int>(e.second * DIST_TILE * 1.2);
			}
			p.x = std::clamp(p.x, TILE_UNITS, world_coord(gameWorld.map.width - 2));
			p.y = std::clamp(p.y, TILE_UNITS, world_coord(gameWorld.map.height - 2));
			send(fighters[i], DroidOrder(DORDER_SCOUT, p), false);
			rememberPlan(fighters[i], DroidOrder(DORDER_SCOUT, p), p);
			if (!modcombot::isModular(fighters[i])) { secondarySetState(fighters[i], gameWorld.objects, DSO_HALTTYPE, DSS_HALT_PURSUE); }
		}
		if (builders.empty()) { break; }
		std::vector<DroidOrder> plan;
		STRUCTURE_STATS *trench = firstAvailable({ "A0BaBaTrench", "A0BaBaTrenchEarth", "A0BaBaSandbagWall" });
		STRUCTURE_STATS *post = firstAvailable({ "A0BaBaMGNestPost" });
		const std::vector<Vector2i> seg = resample(raw, 7);
		if (trench != nullptr)
		{
			for (size_t k = 0; k + 1 < seg.size(); k += 2) { plan.emplace_back(DORDER_LINEBUILD, trench, seg[k], seg[k + 1], 0); }
		}
		if (post != nullptr)
		{
			for (size_t k : { static_cast<size_t>(2), static_cast<size_t>(4) })
			{
				const size_t li = std::min(line.size() - 1, k * (line.size() - 1) / 6);
				const auto e = enemyDir(li);
				Vector2i p = seg[k];
				p.x += static_cast<int>(e.first * DIST_TILE * 0.8);
				p.y += static_cast<int>(e.second * DIST_TILE * 0.8);
				const uint16_t dir = static_cast<uint16_t>(iAtan2(static_cast<int>(e.first * 4096), static_cast<int>(-e.second * 4096)));
				plan.emplace_back(DORDER_BUILD, post, p, dir);
			}
		}
		for (size_t k = 0; k < plan.size(); ++k) { send(builders[k % builders.size()], plan[k], k >= builders.size()); }
		break;
	}
	case M_TRAPS:
	case M_MINES:
	{
		// MOD (17.11. 18:12/18:13): Baufahrzeuge/Infanterie mit Spaten bauen das Feld, Stellen reihum verteilt
		STRUCTURE_STATS *st = (m == M_TRAPS) ? firstAvailable({ "A0BaBaHedgehog", "A0TankTrapHeavy", "A0TankTrap" }) : firstAvailable({ "A0Mine" });   // (20.11.) Sprite-Igel nur noch als letzte Wahl
		if (st == nullptr) { break; }
		std::vector<DROID *> builders;
		for (DROID *d : units) { if (isTruck(d) && !isAir(d)) { builders.push_back(d); } }
		if (builders.empty()) { for (DROID *d : units) { if (isBuilder(d)) { builders.push_back(d); } } }
		if (builders.empty()) { break; }
		const std::vector<Vector2i> spots = fieldSpots(raw, m == M_MINES);
		for (size_t k = 0; k < spots.size(); ++k)
		{
			send(builders[k % builders.size()], DroidOrder(DORDER_BUILD, st, spots[k], 0), k >= builders.size());
		}
		break;
	}
	case M_ARTYBASE:
	{
		auto worldOf = [](const Vector2i &t) { return Vector2i(t.x * DIST_TILE + DIST_TILE / 2, t.y * DIST_TILE + DIST_TILE / 2); };   // Plan-Kacheln
		std::vector<DroidOrder> plan;
		for (const modbuild::Placement &pl : artyBasePlan(c, artyBaseSize(raw)))
		{
			if (pl.line) { plan.emplace_back(DORDER_LINEBUILD, pl.stats, worldOf(pl.tile), worldOf(pl.tile2), 0); }
			else { plan.emplace_back(DORDER_BUILD, pl.stats, worldOf(pl.tile), 0); }
		}
		// alle Baufahrzeuge arbeiten denselben Plan ab (sie helfen sich gegenseitig an derselben Baustelle)
		for (DROID *d : units)
		{
			for (size_t k = 0; k < plan.size(); ++k) { send(d, plan[k], k > 0); }
		}
		break;
	}
	case M_ARTY:
	{
		const std::vector<Vector2i> tgt = resample(raw, std::max<size_t>(1, std::min<size_t>(units.size(), 8)));
		for (size_t i = 0; i < units.size(); ++i) { send(units[i], DroidOrder(DORDER_ATTACKGROUND, tgt[i % tgt.size()]), false); }
		break;
	}
	case M_AIR:
		for (size_t i = 0; i < units.size(); ++i)
		{
			send(units[i], DroidOrder(units[i]->isVtol() ? DORDER_PATROL : DORDER_SCOUT, spread(c, i)), false);
		}
		break;
	case M_CLEARMINES:
	{
		// Raeumpanzer: Walze an, Bahnen im Abstand von 1,5 Feldern (die Walze erfasst gut 1,5 Felder vor dem Bug);
		// Infanterie: Bahnen im Abstand von 4 Feldern (sie sieht Minen 2,5 Felder weit)
		std::vector<DROID *> flails, crew;
		for (DROID *d : units) { (modmines::isFlail(d) ? flails : crew).push_back(d); }
		for (DROID *d : units) { s_sweep.erase(d->id); }
		const auto give = [](std::vector<DROID *> &grp, const std::vector<std::vector<Vector2i>> &lanes, bool flail) {
			// jedem Bahnblock die naechste Einheit
			std::vector<bool> used(grp.size(), false);
			for (const auto &ln : lanes)
			{
				if (ln.empty()) { continue; }
				int best = -1; int64_t bd = INT64_MAX;
				for (size_t i = 0; i < grp.size(); ++i) { if (!used[i]) { const int64_t dd = dist2(grp[i]->pos.xy(), ln.front()); if (dd < bd) { bd = dd; best = static_cast<int>(i); } } }
				if (best < 0) { break; }
				used[static_cast<size_t>(best)] = true;
				DROID *d = grp[static_cast<size_t>(best)];
				SweepJob j; j.wp = ln; j.flail = flail;
				s_sweep[d->id] = j;
				if (flail && !modmines::flailing(d)) { secondarySetState(d, gameWorld.objects, DSO_MINECLEAR, DSS_MINECLEAR_SET); }
			}
		};
		if (!flails.empty()) { give(flails, sweepLanes(raw, DIST_TILE * 3 / 2, flails.size()), true); }
		if (!crew.empty()) { give(crew, sweepLanes(raw, DIST_TILE * 4, std::min<size_t>(crew.size(), 12)), false); }
		s_nextSweep = 0;
		addConsoleMessage(_("Mine sweep: the area is combed lane by lane"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		break;
	}
	case M_DEMOLISH:
	{
		// eigene Bauwerke im Gebiet; jeder Bauer nimmt reihum das naechstgelegene noch freie Bauwerk (Kette), erster Befehl ersetzt
		std::vector<STRUCTURE *> todo;
		const modlayer::Layer view = modlayer::current();
		for (STRUCTURE *st : gameWorld.objects.structures[selectedPlayer])
		{
			if (st == nullptr || st->died || modlayer::layerOf(st) != view) { continue; }
			if (insidePoly(raw, st->pos.x, st->pos.y)) { todo.push_back(st); }
		}
		if (todo.empty()) { addConsoleMessage(_("Demolish: none of your structures in this area"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); break; }
		std::vector<Vector2i> at;
		for (DROID *d : units) { at.push_back(d->pos.xy()); }
		std::vector<bool> done(todo.size(), false);
		std::vector<int> count(units.size(), 0);
		size_t left = todo.size();
		while (left > 0)
		{
			for (size_t u = 0; u < units.size() && left > 0; ++u)
			{
				int best = -1; int64_t bd = INT64_MAX;
				for (size_t i = 0; i < todo.size(); ++i) { if (!done[i]) { const int64_t dd = dist2(at[u], todo[i]->pos.xy()); if (dd < bd) { bd = dd; best = static_cast<int>(i); } } }
				if (best < 0) { left = 0; break; }
				done[static_cast<size_t>(best)] = true; --left;
				STRUCTURE *st = todo[static_cast<size_t>(best)];
				send(units[u], DroidOrder(DORDER_DEMOLISH, st), count[u] > 0 || s_append);   // (17:45) mit Shift: an die Warteschlange anhaengen
				++count[u];
				at[u] = st->pos.xy();
			}
		}
		addConsoleMessage(_("Demolish: builders tear down the structures in the area"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		break;
	}
	case M_COMBOT:
	{
		// zum Gebiet vorruecken, dann zwischen zwei entfernten Punkten des Gebiets patrouillieren
		Vector2i a = raw.front(), b = raw.front();
		int64_t best = -1;
		for (const Vector2i &p : raw) { const int64_t d2 = dist2(p, a); if (d2 > best) { best = d2; b = p; } }
		for (size_t i = 0; i < units.size(); ++i)
		{
			send(units[i], DroidOrder(DORDER_SCOUT, spread(a, i)), false);
			send(units[i], DroidOrder(DORDER_PATROL, spread(b, i)), true);
			rememberPlan(units[i], DroidOrder(DORDER_SCOUT, spread(a, i)), spread(a, i));
		}
		break;
	}
	default:
		break;
	}
}

// ------------------------------------------------------------------------------------------------- Zeichnen im 3D-Bild

std::function<bool(const Vector2i &, Vector2i &)> s_proj;   // (17.11.) grosse Karte: Weltpunkt -> Bildschirmpunkt statt 3D-Projektion

bool project(const Vector2i &w, const glm::mat4 &m, Vector2i &out)
{
	if (s_proj) { return s_proj(w, out); }
	const int wx = std::clamp(w.x, 0, world_coord(gameWorld.map.width) - 1);
	const int wy = std::clamp(w.y, 0, world_coord(gameWorld.map.height) - 1);
	const Vector3i pos(wx, map_Height(gameWorld.map, wx, wy) + 10, -wy);
	return pie_RotateProjectWithPerspective(&pos, m, &out) > 0;
}


// MOD (Wunsch 16.11. 21:34): dicke, sich verjuengende Kriegskarten-Pfeile (wie auf den Beispielbildern) und Linien/Kreise mit leuchtendem Hof
// - Bildschirmpunkte gleichmaessig neu verteilt (glatt), Strich aus weichen, gedrehten Rundboxen; Schwanz blendet ein; Pfeilspitze aus Scheiben.
std::vector<glm::vec2> screenPath(const std::vector<Vector2i> &pts, const glm::mat4 &m, bool closed)
{
	std::vector<glm::vec2> raw;
	for (const Vector2i &p : pts)
	{
		Vector2i sp(0, 0);
		if (project(p, m, sp)) { raw.emplace_back(static_cast<float>(sp.x), static_cast<float>(sp.y)); }
	}
	if (closed && raw.size() >= 3) { raw.push_back(raw.front()); }
	if (raw.size() < 2) { return raw; }
	// Glaetten (Chaikin, 2 Durchgaenge) - Zeichnungen mit der Maus sind zittrig
	for (int it = 0; it < 2; ++it)
	{
		std::vector<glm::vec2> o;
		o.push_back(raw.front());
		for (size_t i = 0; i + 1 < raw.size(); ++i)
		{
			o.push_back(raw[i] * 0.75f + raw[i + 1] * 0.25f);
			o.push_back(raw[i] * 0.25f + raw[i + 1] * 0.75f);
		}
		o.push_back(raw.back());
		raw.swap(o);
	}
	// gleichmaessig neu verteilen (etwa alle 7 Bildpunkte, hoechstens 160 Punkte)
	float total = 0.f;
	for (size_t i = 1; i < raw.size(); ++i) { total += glm::length(raw[i] - raw[i - 1]); }
	if (total < 1.f) { return raw; }
	const int n = std::clamp(static_cast<int>(total / 7.f), 2, 160);
	std::vector<glm::vec2> out;
	out.push_back(raw.front());
	float want = total / static_cast<float>(n), acc = 0.f;
	for (size_t i = 1; i < raw.size(); ++i)
	{
		const glm::vec2 a = raw[i - 1], b = raw[i];
		float seg = glm::length(b - a);
		float t0 = 0.f;
		while (acc + seg * (1.f - t0) >= want && seg > 0.f)
		{
			const float t = t0 + (want - acc) / seg;
			out.push_back(a + (b - a) * t);
			t0 = t; acc = 0.f;
		}
		acc += seg * (1.f - t0);
	}
	if (glm::length(out.back() - raw.back()) > 1.f) { out.push_back(raw.back()); }
	return out;
}

// MOD (Wunsch 17.11. 18:10): "ALLE Pfeil- und Befehlsmarker genauso schoen machen wie unsere Sensoren- und Reichweitenmarker. Gleich
// leuchtend und schoen rendern. Und noch dicker." - Aufbau wie die Reichweitenringe: heller, fast weisser Kern, darunter ein weicher
// farbiger Lichthof in zwei Stufen, in der Mitte eine heisse Linie. Lagen: 0 aeusserer Hof, 1 innerer Hof, 2 Kern, 3 heisse Mitte.
PIELIGHT mixCol(const ModeInfo &mi, float toWhite, float a)
{
	const auto ch = [toWhite](unsigned char c) { return static_cast<UBYTE>(std::min(255, static_cast<int>(c + (255 - c) * toWhite))); };
	return pal_RGBA(ch(mi.r), ch(mi.g), ch(mi.b), static_cast<UBYTE>(std::clamp(static_cast<int>(a), 0, 255)));
}

struct Seg
{
	glm::vec2 a, b;
	float w;        // Breite des Kerns
	float alpha;    // 0..255
	bool box;       // Scheibe (Pfeilspitze/Pyramide): eckig statt Kapsel
};

void drawSegLayer(const Seg &g, int layer, const ModeInfo &mi, float twOverride = -1.f)
{
	const glm::vec2 d = g.b - g.a;
	const float len = glm::length(d);
	if (g.w <= 0.5f || g.alpha <= 0.f) { return; }
	const float sc = modui::uiScale();
	float ww = g.w, soft = 1.6f, tw = 0.35f, am = 1.f, corner = g.box ? 1.f : g.w * 0.5f;
	switch (layer)
	{
	case 0: ww = g.w + 34.f * sc; soft = ww * 0.5f; tw = 0.15f; am = 0.075f; corner = ww * 0.5f; break;
	case 1: ww = g.w + 14.f * sc; soft = 7.f * sc; tw = 0.35f; am = 0.2f; corner = ww * 0.5f; break;
	case 2: break;
	default:
		if (g.box) { return; }
		ww = std::max(2.f, g.w * 0.32f); soft = 1.2f; tw = 0.8f; am = 0.9f; corner = ww * 0.5f; break;
	}
	if (twOverride >= 0.f) { tw = twOverride; }
	const glm::vec2 c = (g.a + g.b) * 0.5f;
	const float along = g.box ? std::max(1.f, len) + (layer <= 1 ? ww - g.w : 0.f) : len + ww;
	pie_DrawSoftRoundedBoxRotated(c.x, c.y, along, ww, len > 0.01f ? std::atan2(d.y, d.x) : 0.f, mixCol(mi, tw, g.alpha * am), corner, soft);
}

void drawSegs(const std::vector<Seg> &segs, const ModeInfo &mi)
{
	for (int layer = 0; layer < 4; ++layer)
	{
		for (const Seg &g : segs) { drawSegLayer(g, layer, mi); }
	}
}

// gefuelltes Dreieck aus Querscheiben (Pfeilspitze, Pyramide): Spitze tip, Grundlinie bei base, halbe Grundbreite halfW
void triangleSegs(std::vector<Seg> &out, const glm::vec2 &base, const glm::vec2 &tip, float halfW, float alpha)
{
	const glm::vec2 d = tip - base;
	const float L = glm::length(d);
	if (L < 1.f) { return; }
	const glm::vec2 dir = d / L, perp(-dir.y, dir.x);
	const int K = std::clamp(static_cast<int>(L / 2.5f), 6, 16);
	for (int k = 0; k < K; ++k)
	{
		const float f = (static_cast<float>(k) + 0.5f) / static_cast<float>(K);
		const glm::vec2 c = base + d * f;
		const float across = std::max(1.f, 2.f * halfW * (1.f - f));
		// Scheibe quer zur Richtung: a/b liegen auf der Querachse, Breite = Dicke entlang der Richtung
		out.push_back(Seg{ c - perp * (across * 0.5f), c + perp * (across * 0.5f), L / static_cast<float>(K) + 1.5f, alpha, true });
	}
}

// Chaikin-Glaettung (wie screenPath), damit Querstriche/Felder genau an der gezeichneten Linie sitzen
std::vector<glm::vec2> smoothWorld(const std::vector<Vector2i> &pts)
{
	std::vector<glm::vec2> raw;
	for (const Vector2i &p : pts) { raw.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y)); }
	if (raw.size() < 3) { return raw; }
	for (int it = 0; it < 2; ++it)
	{
		std::vector<glm::vec2> o;
		o.push_back(raw.front());
		for (size_t i = 0; i + 1 < raw.size(); ++i)
		{
			o.push_back(raw[i] * 0.75f + raw[i + 1] * 0.25f);
			o.push_back(raw[i] * 0.25f + raw[i + 1] * 0.75f);
		}
		o.push_back(raw.back());
		raw.swap(o);
	}
	return raw;
}

bool projectF(const glm::vec2 &w, const glm::mat4 &m, glm::vec2 &out)
{
	Vector2i sp(0, 0);
	if (!project(Vector2i(static_cast<int>(w.x), static_cast<int>(w.y)), m, sp)) { return false; }
	out = glm::vec2(static_cast<float>(sp.x), static_cast<float>(sp.y));
	return true;
}

// MOD (Wunsch 17.11. 18:12): Panzersperrenfeld - gruene Pyramiden an den geplanten Stellen
void pyramidSegs(std::vector<Seg> &out, std::vector<Seg> &facets, const std::vector<Vector2i> &marks, float alpha, const glm::mat4 &m)
{
	for (const Vector2i &p : marks)
	{
		glm::vec2 g, t;
		const glm::vec2 w(static_cast<float>(p.x), static_cast<float>(p.y));
		if (!projectF(w, m, g) || !projectF(w + glm::vec2(DIST_TILE * 0.32f, 0.f), m, t)) { continue; }
		const float r = std::clamp(glm::length(t - g), 4.f, 40.f);   // perspektivische Groesse
		const glm::vec2 base = g + glm::vec2(0.f, r * 0.35f), tip = g - glm::vec2(0.f, r * 1.35f);
		triangleSegs(out, base, tip, r * 0.95f, alpha);
		// helle linke Flanke (Pyramide)
		const glm::vec2 fb = base - glm::vec2(r * 0.95f * 0.5f, 0.f);
		triangleSegs(facets, fb, tip - glm::vec2(r * 0.03f, 0.f), r * 0.95f * 0.5f, alpha * 0.55f);
	}
}

// MOD (Wunsch 17.11. 18:13): Minenfeld - Minen-Zeichen (Scheibe mit Zuendkreuz) an den geplanten Stellen
void mineSegs(std::vector<Seg> &out, std::vector<Seg> &cross, const std::vector<Vector2i> &marks, float alpha, const glm::mat4 &m)
{
	for (const Vector2i &p : marks)
	{
		glm::vec2 g, t;
		const glm::vec2 w(static_cast<float>(p.x), static_cast<float>(p.y));
		if (!projectF(w, m, g) || !projectF(w + glm::vec2(DIST_TILE * 0.28f, 0.f), m, t)) { continue; }
		const float r = std::clamp(glm::length(t - g), 4.f, 34.f);
		out.push_back(Seg{ g, g, r * 1.6f, alpha, false });   // Scheibe (Kapsel der Laenge 0 = Kreis)
		cross.push_back(Seg{ g - glm::vec2(r * 0.6f, r * 0.6f), g + glm::vec2(r * 0.6f, r * 0.6f), std::max(1.5f, r * 0.22f), alpha, false });
		cross.push_back(Seg{ g - glm::vec2(r * 0.6f, -r * 0.6f), g + glm::vec2(r * 0.6f, -r * 0.6f), std::max(1.5f, r * 0.22f), alpha, false });
	}
}

// MOD (Wunsch 22.11. 15:47/15:49): Striche jetzt aus sauberen Baendern (modglow) statt aus ueberlappenden Kapseln - durchgehend leuchtend
// wie die Reichweitenringe, ohne Perlen und ohne "Zwischenraum". Pfeile: dicker Stamm, Spitze nur etwa doppelt so breit wie der Stamm und
// mit der Pfeillaenge begrenzt (auf der verkleinerten Karte deckten die Spitzen alles zu). Verteidigungs-/Angriffslinien: kraeftige Linie
// mit kurzen, dichten Querstrichen (wie auf Lagekarten), Abstand und Laenge in Bildpunkten - auf jeder Zoomstufe gleich.
bool s_noTaper = false;   // Zweige eines gespaltenen Angriffspfeils beginnen in voller Breite am Stamm

float pathLength(const std::vector<glm::vec2> &p)
{
	float L = 0.f;
	for (size_t i = 1; i < p.size(); ++i) { L += glm::length(p[i] - p[i - 1]); }
	return L;
}

/// Bildschirmseite der Feindseite: +1 = links der Laufrichtung (Normale (-dy, dx)), -1 = rechts; aus der Weltrichtung abgeleitet
float screenSide(const std::vector<Vector2i> &pts, int side, const std::vector<glm::vec2> &sp, const glm::mat4 &m)
{
	const std::vector<glm::vec2> wp = smoothWorld(pts);
	if (wp.size() < 2 || sp.size() < 2 || side == 0) { return 0.f; }
	const size_t mi = wp.size() / 2;
	glm::vec2 t = wp[std::min(wp.size() - 1, mi + 1)] - wp[mi > 0 ? mi - 1 : 0];
	if (glm::length(t) < 0.01f) { return static_cast<float>(side); }
	t = glm::normalize(t);
	const glm::vec2 nW = glm::vec2(-t.y, t.x) * static_cast<float>(side);
	glm::vec2 a, b;
	if (!projectF(wp[mi], m, a) || !projectF(wp[mi] + nW * (DIST_TILE * 0.4f), m, b)) { return static_cast<float>(side); }
	// naechster Bildschirmpunkt und dessen Normale
	size_t best = 0;
	float bd = 1e30f;
	for (size_t i = 0; i < sp.size(); ++i) { const float d = glm::length(sp[i] - a); if (d < bd) { bd = d; best = i; } }
	glm::vec2 ts = sp[std::min(sp.size() - 1, best + 1)] - sp[best > 0 ? best - 1 : 0];
	if (glm::length(ts) < 0.01f) { return static_cast<float>(side); }
	ts = glm::normalize(ts);
	const glm::vec2 nS(-ts.y, ts.x);
	return glm::dot(nS, b - a) >= 0.f ? 1.f : -1.f;
}

void drawStroke(const std::vector<Vector2i> &pts, int mode, int alpha, const glm::mat4 &m, const std::vector<Vector2i> *marks = nullptr, int side = 0, float widthScale = 1.f, bool head = true)
{
	const ModeInfo &mi = kModes[mode];
	const bool closed = mi.area;
	const bool field = mode == M_TRAPS || mode == M_MINES;
	const bool lineWithTicks = mode == M_DEFEND || mode == M_OFFENSE || mode == M_DEFNET;   // Verteidigungs-/Offensivlinie, Grabennetz: Linie mit Querstrichen zum Feind
	const bool shaftOnly = !head && !closed && !lineWithTicks;            // (22.11.) Pfeilstamm ohne Spitze (Angriffsplaner: Vektoren spalten sich)
	const bool arrow = !closed && mode != M_RALLY && !lineWithTicks && head;    // Rally = Kreis/Spirale
	std::vector<glm::vec2> p = screenPath(pts, m, closed);
	if (p.size() < 2 || alpha <= 0) { return; }
	const float sc = modui::uiScale();
	const modglow::Colour col{ mi.r, mi.g, mi.b };
	const float L = pathLength(p);
	if (L < 2.f) { return; }
	if (arrow || shaftOnly)
	{
		// halbe Stammbreite: kraeftig, aber bei kurzen Pfeilen (verkleinerte Karte) im Verhaeltnis zur Laenge
		const float hw = std::clamp(std::min(8.f * sc * widthScale, L * 0.065f), 2.5f * sc, 40.f * sc);
		const float glow = 3.f * sc + hw * 0.5f;
		float headL = 0.f, headHalf = 0.f;
		glm::vec2 tip = p.back(), dir(1.f, 0.f);
		if (arrow)
		{
			headL = std::min(hw * 3.4f, L * 0.36f);
			headHalf = std::min(hw * 2.35f, headL * 0.9f);
			// Richtung der Spitze aus dem letzten Stueck des Weges
			size_t k = p.size() - 1;
			float back = 0.f;
			while (k > 0 && back < headL) { back += glm::length(p[k] - p[k - 1]); --k; }
			dir = tip - p[k];
			dir = glm::length(dir) < 0.01f ? glm::vec2(1.f, 0.f) : glm::normalize(dir);
			// Stamm endet knapp hinter der Basis der Spitze (unter der Flaeche der Spitze)
			const glm::vec2 base = tip - dir * headL;
			while (p.size() > 2 && glm::dot(p.back() - base, dir) > -headL * 0.05f) { p.pop_back(); }
			p.push_back(base + dir * (headL * 0.18f));
		}
		const size_t n = p.size();
		std::vector<float> scl(n, 1.f), am(n, 1.f);
		float acc = 0.f;
		const float Lb = std::max(1.f, pathLength(p));
		for (size_t i = 0; i < n; ++i)
		{
			if (i > 0) { acc += glm::length(p[i] - p[i - 1]); }
			const float s = acc / Lb;
			// Schwanz: schmal und durchsichtig, nach gut einem Drittel voll (wie Kriegskarten-Pfeile)
			scl[i] = s_noTaper ? 1.f : 0.5f + 0.5f * std::min(1.f, s / 0.32f);
			am[i] = s_noTaper ? 1.f : std::min(1.f, 0.3f + s / 0.32f);
		}
		modglow::ribbon(p, hw, glow, col, static_cast<float>(alpha), false, scl, am, !arrow);
		if (arrow) { modglow::arrowHead(tip - dir * headL, tip, headHalf, glow * 0.8f, col, static_cast<float>(alpha)); }
		return;
	}
	if (lineWithTicks)
	{
		const float hw = std::clamp(std::min(5.5f * sc * widthScale, L * 0.05f), 2.f * sc, 24.f * sc);
		const float glow = 3.f * sc + hw * 0.7f;
		// kurze, dichte Querstriche zur Feindseite (in Bildpunkten): erst die Striche, dann die Linie darueber
		const float sgn = screenSide(pts, side, p, m);
		if (sgn != 0.f)
		{
			std::vector<std::pair<glm::vec2, glm::vec2>> ticks;
			const float spacing = hw * 1.9f, len = hw * 1.55f;
			float acc = 0.f, next = spacing * 0.5f;
			for (size_t i = 1; i < p.size() && ticks.size() < 600; ++i)
			{
				const glm::vec2 a = p[i - 1], b = p[i];
				const float seg = glm::length(b - a);
				if (seg < 1e-3f) { continue; }
				const glm::vec2 t = (b - a) / seg, nS = glm::vec2(-t.y, t.x) * sgn;
				while (acc + seg >= next)
				{
					const glm::vec2 q = a + t * (next - acc);
					if (next > spacing * 0.3f && next < L - spacing * 0.3f) { ticks.emplace_back(q + nS * (hw * 0.4f), q + nS * (hw + len)); }
					next += spacing;
				}
				acc += seg;
			}
			modglow::bars(ticks, std::max(1.2f, hw * 0.36f), col, static_cast<float>(alpha) * 0.95f);
		}
		modglow::ribbon(p, hw, glow, col, static_cast<float>(alpha), false);
		return;
	}
	// Gebiete und einfache Linien (Kreise, Sammelpunkt-Spirale, Felder)
	{
		const float hw = std::clamp(4.5f * sc * widthScale, 1.5f * sc, 20.f * sc);
		const float glow = 3.f * sc + hw * 0.8f;
		// Felder (Panzersperren/Minen): Umriss nur schwach, die Zeichen tragen die Aussage
		const float outlineA = (field && marks != nullptr && !marks->empty()) ? static_cast<float>(alpha) * 0.45f : static_cast<float>(alpha);
		modglow::ribbon(p, field ? hw * 0.7f : hw, glow, col, outlineA, closed);
	}
	if (field && marks != nullptr && !marks->empty())
	{
		std::vector<Seg> segs, extra;
		if (mode == M_TRAPS) { pyramidSegs(segs, extra, *marks, static_cast<float>(alpha), m); }
		else { mineSegs(segs, extra, *marks, static_cast<float>(alpha), m); }
		drawSegs(segs, mi);
		// Pyramiden-Flanke heller, Zuendkreuz der Mine fast weiss
		for (const Seg &g : extra) { drawSegLayer(g, 2, mi, mode == M_TRAPS ? 0.7f : 0.92f); }
	}
}

// ------------------------------------------------------------------------------------------------- Angriffsplaner (Wunsch 22.11. 01:11)
// "Es soll ein Angriffsplaner sein. Kein Micromanager. Indirekte Steuerung. Eher lenken, als alles selbst zu machen."
// Bausteine (bleiben stehen, bis man sie mit Rechtsklick auf ihr Zahlenschild oder "Clear Plan" entfernt):
//  - Staging Zone (Bereitstellungsraum): neue Einheiten aus den Fabriken und untaetige Einheiten in der Zone kommen in den Plan und
//    sammeln sich dort. Ferry Beacon (per Knopf, nicht gebaut) in der Zone: Transporter holen sie ab und fliegen sie in einer Schleife
//    wie in Supreme Commander zu den Brueckenkoepfen. Liegt ein Tunnel passend, fahren sie durch den Tunnel, sonst auf eigene Faust.
//  - Bridgehead (Brueckenkopf) / Assault Line (Doppelreihe): dort warten die Einheiten, bis die Sollstaerke erreicht ist (Schild "7/12",
//    Klick schaltet um, "Hold" = nur auf Knopfdruck) oder man "Attack!" drueckt; dann stuermen sie entlang der Attack Vectors, die am
//    naechsten bei ihnen anfangen (ohne Vektor zum naechsten bekannten Feindgebaeude). Die naechste Welle sammelt sich schon.
//  - Prioritaet 1-5 (Zahlenschild, Klick schaltet weiter): bestimmt, wer zuerst Einheiten und Transporter bekommt (1 = zuerst) und bei
//    Vektoren, wie viele Einheiten jeder bekommt. Alles arbeitet gleichzeitig, sobald es etwas zu tun hat.
// Die Verwaltung laeuft beim eigenen Spieler und gibt gewoehnliche Befehle (wie Mausklicks) - also auch im Mehrspieler synchron.
// Ein eigener Befehl an eine Einheit nimmt sie aus dem Plan. Im Spielstand gespeichert (modsave).
namespace plan
{
constexpr unsigned STEP_MS = 1000;
constexpr int GROUP_MIN = 5;                  // so viele fahren gemeinsam aus dem Bereitstellungsraum los
constexpr unsigned STAGE_WAIT_MAX = 25000;    // laenger wartet niemand auf eine volle Gruppe
constexpr unsigned ATTACK_MAX = 240000;       // danach ist eine Einheit aus dem Angriff entlassen
constexpr unsigned LOAD_MAX_MS = 20000;
constexpr unsigned LOAD_MIN_MS = 5000;
constexpr int kGoals[] = { 4, 8, 12, 16, 24, 32, 0 };   // 0 = Hold (nur auf Knopfdruck)
constexpr int NGOALS = static_cast<int>(sizeof(kGoals) / sizeof(kGoals[0]));

enum Kind { K_STAGING = 0, K_BRIDGEHEAD, K_ASSAULT, K_VECTOR };
struct Elem
{
	uint32_t id = 0;
	int kind = K_STAGING;
	int prio = 1;
	std::vector<Vector2i> pts;   // Angriffslinie / Vektor (Welt)
	Vector2i c{0, 0};            // Zonen: Mitte
	int r = 0;                   //        Radius
	bool beacon = false;
	Vector2i beaconPos{0, 0};
	int goal = 12;
	int side = 1;
	unsigned waves = 0;
};
enum UPhase { U_TO_STAGE = 0, U_AT_STAGE, U_FERRY, U_TO_TARGET, U_AT_TARGET, U_ATTACK };
struct URec
{
	uint32_t stage = 0, target = 0;
	int phase = U_TO_STAGE;
	unsigned since = 0, sent = 0;
	int slot = -1;
};
enum TPhase { T_IDLE = 0, T_TO_PICKUP, T_LOADING, T_OUTBOUND, T_RETURN };
struct TRec
{
	int phase = T_IDLE;
	uint32_t stage = 0, target = 0;
	unsigned since = 0, sent = 0;
};
struct Hit { uint32_t elem; int what; int x0, y0, x1, y1; };   // what: 0 Prioritaet, 1 Sollstaerke, 2 Leuchtfeuer

std::vector<Elem> s_elems;
uint32_t s_nextId = 1;
std::map<uint32_t, URec> s_units;
std::map<uint32_t, TRec> s_trans;
std::set<uint32_t> s_known;
bool s_knownInit = false;
unsigned s_nextStep = 0;
unsigned s_clearArmed = 0;
std::vector<Hit> s_hits;

Elem *elem(uint32_t id)
{
	for (Elem &e : s_elems) { if (e.id == id) { return &e; } }
	return nullptr;
}
bool isTarget(const Elem &e) { return e.kind == K_BRIDGEHEAD || e.kind == K_ASSAULT; }
bool anyKind(int k) { for (const Elem &e : s_elems) { if (e.kind == k) { return true; } } return false; }
bool anyTarget() { return anyKind(K_BRIDGEHEAD) || anyKind(K_ASSAULT); }
bool active() { return anyTarget() || anyKind(K_STAGING); }

Vector2i clampMap(Vector2i p)
{
	p.x = std::clamp(p.x, TILE_UNITS * 2, world_coord(gameWorld.map.width - 3));
	p.y = std::clamp(p.y, TILE_UNITS * 2, world_coord(gameWorld.map.height - 3));
	return p;
}
Vector2i lineMid(const Elem &e) { return e.pts.empty() ? e.c : resample(e.pts, 3)[1]; }
Vector2i anchor(const Elem &e)
{
	if (e.kind == K_STAGING || e.kind == K_BRIDGEHEAD) { return e.c; }
	if (e.kind == K_ASSAULT) { return lineMid(e); }
	return e.pts.empty() ? e.c : e.pts.front();
}
bool inside(const Elem &z, const Vector2i &p, int extra = 0)
{
	const int64_t r = z.r + extra;
	return dist2(z.c, p) <= r * r;
}
int64_t distTo(const Elem &e, const Vector2i &p)
{
	if (e.kind == K_STAGING || e.kind == K_BRIDGEHEAD)
	{
		const int64_t d = static_cast<int64_t>(std::sqrt(static_cast<double>(dist2(e.c, p))));
		return std::max<int64_t>(0, d - e.r);
	}
	int64_t best = INT64_MAX;
	for (const Vector2i &q : resample(e.pts, std::max<size_t>(8, e.pts.size()))) { best = std::min(best, dist2(q, p)); }
	return static_cast<int64_t>(std::sqrt(static_cast<double>(best)));
}

bool fitsPlan(const DROID *d)
{
	if (d == nullptr || d->died || d->isTransporter() || isAir(d)) { return false; }
	return (isCombat(d) && !isArty(d)) || modcombot::isModular(d);
}

/// eigene Einheit, auch wenn sie gerade in einem Transporter sitzt
DROID *findDroid(uint32_t id, bool *aboard = nullptr)
{
	if (aboard != nullptr) { *aboard = false; }
	if (selectedPlayer >= MAX_PLAYERS) { return nullptr; }
	DROID *d = IdToDroid(gameWorld.objects, id, selectedPlayer);
	if (d != nullptr) { return d->died ? nullptr : d; }
	for (DROID *t : gameWorld.objects.droids[selectedPlayer])
	{
		if (t == nullptr || t->died || !t->isTransporter() || t->psGroup == nullptr) { continue; }
		for (DROID *c : t->psGroup->psList)
		{
			if (c != nullptr && c != t && c->id == id) { if (aboard != nullptr) { *aboard = true; } return c->died ? nullptr : c; }
		}
	}
	return nullptr;
}

void order(DROID *d, URec &u, const DroidOrder &o, bool add = false)
{
	send(d, o, add);
	u.sent = gameTime;
}

/// hat der Spieler der Einheit seit unserem letzten Befehl selbst etwas befohlen? (dann gehoert sie wieder ihm)
bool overridden(const DROID *d, unsigned sent)
{
	const unsigned since = modcontrol::sincePlayerOrder(d);
	if (since >= gameTime) { return false; }
	return gameTime - since > sent + 2500;
}

int countUnits(uint32_t target, int phaseFrom, int phaseTo)
{
	int n = 0;
	for (const auto &kv : s_units) { if (kv.second.target == target && kv.second.phase >= phaseFrom && kv.second.phase <= phaseTo) { ++n; } }
	return n;
}
int assigned(uint32_t target) { return countUnits(target, U_TO_STAGE, U_AT_TARGET); }
int arrived(uint32_t target) { return countUnits(target, U_AT_TARGET, U_AT_TARGET); }

/// Ziel fuer die naechste Einheit: erst fuellt Prioritaet 1 ihre Sollstaerke, dann 2 ... - danach beginnt die naechste Welle wieder bei 1
uint32_t pickTarget()
{
	const Elem *best = nullptr;
	int64_t bestKey = INT64_MAX;
	for (const Elem &e : s_elems)
	{
		if (!isTarget(e)) { continue; }
		const int n = assigned(e.id);
		const int g = e.goal > 0 ? e.goal : 100000;
		const int64_t key = static_cast<int64_t>(n / g) * 1000000 + e.prio * 100000 + std::min(n, 99999);
		if (key < bestKey) { bestKey = key; best = &e; }
	}
	return best != nullptr ? best->id : 0u;
}

uint32_t nearestStage(const Vector2i &p)
{
	const Elem *best = nullptr;
	int64_t bd = INT64_MAX;
	for (const Elem &e : s_elems)
	{
		if (e.kind != K_STAGING) { continue; }
		const int64_t d = dist2(e.c, p) + static_cast<int64_t>(e.prio - 1) * DIST_TILE * DIST_TILE * 4;   // Prioritaet als leichter Vorzug
		if (d < bd) { bd = d; best = &e; }
	}
	return best != nullptr ? best->id : 0u;
}

int freeSlot(uint32_t target)
{
	std::set<int> used;
	for (const auto &kv : s_units) { if (kv.second.target == target && kv.second.phase < U_ATTACK && kv.second.slot >= 0) { used.insert(kv.second.slot); } }
	int s = 0;
	while (used.count(s)) { ++s; }
	return s;
}

/// Feindrichtung der Angriffslinie an der Stelle p (Normale zur Seite der Querstriche)
std::pair<double, double> lineEnemyDir(const Elem &t, const Vector2i &p)
{
	const std::vector<Vector2i> fl = resample(t.pts, std::max<size_t>(24, t.pts.size()));
	size_t bi = 0;
	int64_t bd = INT64_MAX;
	for (size_t k = 0; k < fl.size(); ++k) { const int64_t dd = dist2(fl[k], p); if (dd < bd) { bd = dd; bi = k; } }
	const Vector2i a = fl[bi > 0 ? bi - 1 : 0], b = fl[std::min(bi + 1, fl.size() - 1)];
	const double tx = b.x - a.x, ty = b.y - a.y, l = std::max(1.0, std::sqrt(tx * tx + ty * ty));
	return std::make_pair(-ty / l * t.side, tx / l * t.side);
}

Vector2i targetSpot(const Elem &t, int slot)
{
	slot = std::max(0, slot);
	if (t.kind == K_BRIDGEHEAD) { return clampMap(spread(t.c, static_cast<size_t>(slot))); }
	// Angriffslinie: zwei Reihen (vorn / gut ein Feld dahinter), weitere Einheiten in weiteren Reihen
	const int cap = std::max(8, t.goal > 0 ? t.goal : 16);
	const int perRank = std::max(1, (cap + 1) / 2);
	const int row = slot / perRank;   // 0,1 = Doppelreihe; abwechselnd vorn/hinten verteilt
	const int idx = slot % perRank;
	const std::vector<Vector2i> line = resample(t.pts, static_cast<size_t>(perRank));
	Vector2i p = line[static_cast<size_t>(std::min(idx, static_cast<int>(line.size()) - 1))];
	const auto e = lineEnemyDir(t, p);
	const double back = DIST_TILE * 1.2 * row;
	p.x -= static_cast<int>(e.first * back);
	p.y -= static_cast<int>(e.second * back);
	return clampMap(p);
}

Vector2i dropPoint(const Elem &t)
{
	if (t.beacon) { return t.beaconPos; }
	if (t.kind == K_BRIDGEHEAD) { return t.c; }
	const Vector2i m = lineMid(t);
	const auto e = lineEnemyDir(t, m);
	return clampMap(Vector2i(m.x - static_cast<int>(e.first * DIST_TILE * 3), m.y - static_cast<int>(e.second * DIST_TILE * 3)));
}

bool haveTransporters()
{
	if (selectedPlayer >= MAX_PLAYERS) { return false; }
	for (const DROID *t : gameWorld.objects.droids[selectedPlayer])
	{
		if (t != nullptr && !t->died && t->isTransporter() && modlayer::layerOf(t) == modlayer::SURFACE) { return true; }
	}
	return false;
}

/// passender Tunnel vom Bereitstellungsraum zum Ziel: Eingang hoechstens 4 Felder vor der Zone, Ausgang hoechstens 10 Felder vom Ziel
bool tunnelFor(const Elem &s, const Elem &t, Vector2i &entry)
{
	bool found = false;
	int64_t best = INT64_MAX;
	for (const modtunnel::Info &tn : modtunnel::tunnels(selectedPlayer))
	{
		for (int dir = 0; dir < 2; ++dir)
		{
			const Vector2i in = dir == 0 ? tn.a : tn.b, out = dir == 0 ? tn.b : tn.a;
			if (!inside(s, in, 4 * DIST_TILE)) { continue; }
			const int64_t dOut = distTo(t, out);
			if (dOut > 10 * DIST_TILE) { continue; }
			if (dOut < best) { best = dOut; entry = in; found = true; }
		}
	}
	return found;
}

void join(DROID *d, uint32_t stage, uint32_t target)
{
	if (d == nullptr || s_units.count(d->id)) { return; }
	URec u;
	u.since = gameTime;
	if (target != 0 && elem(target) != nullptr)
	{
		u.target = target;
		u.slot = freeSlot(target);
		u.phase = U_TO_TARGET;
		order(d, u, DroidOrder(DORDER_SCOUT, targetSpot(*elem(target), u.slot)));
	}
	else
	{
		if (stage == 0) { stage = nearestStage(d->pos.xy()); }
		if (stage != 0)
		{
			u.stage = stage;
			u.phase = U_TO_STAGE;
			const size_t k = static_cast<size_t>(d->id % 23u);
			order(d, u, DroidOrder(DORDER_MOVE, clampMap(spread(elem(stage)->c, k))));
		}
		else
		{
			u.target = pickTarget();
			if (u.target == 0) { return; }
			u.slot = freeSlot(u.target);
			u.phase = U_TO_TARGET;
			order(d, u, DroidOrder(DORDER_SCOUT, targetSpot(*elem(u.target), u.slot)));
		}
	}
	s_units[d->id] = u;
}

void initKnown()
{
	s_known.clear();
	if (selectedPlayer < MAX_PLAYERS)
	{
		for (const DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d != nullptr) { s_known.insert(d->id); } }
		for (const DROID *t : gameWorld.objects.droids[selectedPlayer])
		{
			if (t == nullptr || !t->isTransporter() || t->psGroup == nullptr) { continue; }
			for (const DROID *c : t->psGroup->psList) { if (c != nullptr) { s_known.insert(c->id); } }
		}
	}
	s_knownInit = true;
}

/// neue Einheiten aus den Fabriken und untaetige Einheiten in einem Bereitstellungsraum kommen in den Plan
void intake()
{
	if (!s_knownInit) { initKnown(); }
	const bool on = active();
	for (DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d == nullptr || d->died) { continue; }
		const bool isNew = s_known.insert(d->id).second;
		if (!on || s_units.count(d->id) || !fitsPlan(d) || modlayer::layerOf(d) != modlayer::SURFACE || modtunnel::inTransit(d)) { continue; }
		bool take = isNew;
		if (!take && idleUnit(d) && !modcontrol::manual(d) && !inOp(d->id) && s_plan.count(d->id) == 0)
		{
			for (const Elem &e : s_elems) { if (e.kind == K_STAGING && inside(e, d->pos.xy())) { take = true; break; } }
		}
		if (take) { join(d, 0, 0); }
	}
}

/// nach einer Aenderung am Plan: Einheiten, deren Zone/Ziel weg ist, bekommen neue (oder werden entlassen)
void fixRefs()
{
	for (auto it = s_units.begin(); it != s_units.end();)
	{
		URec &u = it->second;
		if (u.stage != 0 && elem(u.stage) == nullptr) { u.stage = 0; }
		if (u.target != 0 && elem(u.target) == nullptr) { u.target = 0; u.slot = -1; }
		if (u.phase == U_ATTACK) { ++it; continue; }
		bool aboard = false;
		DROID *d = findDroid(it->first, &aboard);
		if (d == nullptr) { it = s_units.erase(it); continue; }
		if ((u.phase == U_TO_STAGE || u.phase == U_AT_STAGE || u.phase == U_FERRY) && u.stage == 0)
		{
			u.stage = nearestStage(d->pos.xy());
			if (u.stage == 0 && u.target == 0) { it = s_units.erase(it); continue; }
			u.phase = u.stage != 0 ? U_TO_STAGE : U_TO_TARGET;
			u.sent = 0;   // im naechsten Schritt neu losschicken
		}
		if ((u.phase == U_TO_TARGET || u.phase == U_AT_TARGET) && u.target == 0)
		{
			u.target = pickTarget();
			if (u.target == 0)
			{
				u.stage = nearestStage(d->pos.xy());
				if (u.stage == 0) { it = s_units.erase(it); continue; }
				u.phase = U_TO_STAGE;
			}
			else { u.slot = freeSlot(u.target); u.phase = U_TO_TARGET; }
			u.sent = 0;
		}
		if (u.phase == U_FERRY)
		{
			const Elem *s = elem(u.stage);
			if (u.target == 0) { u.target = pickTarget(); u.slot = -1; }   // Ziel weg: neues Ziel (auch fuer Einheiten im Transporter)
			if ((s == nullptr || !s->beacon || u.target == 0) && !aboard) { u.phase = U_AT_STAGE; u.since = gameTime; }
		}
		++it;
	}
}

uint32_t linkedTarget(const Elem &v)
{
	if (v.pts.empty()) { return 0; }
	const Elem *best = nullptr;
	int64_t bd = INT64_MAX;
	for (const Elem &e : s_elems)
	{
		if (!isTarget(e)) { continue; }
		const int64_t d = distTo(e, v.pts.front());
		if (d < bd) { bd = d; best = &e; }
	}
	return best != nullptr ? best->id : 0u;
}

/// naechstes bekanntes Feindgebaeude (oder sichtbare Feindeinheit) zum Punkt
bool enemyNear(const Vector2i &p, Vector2i &out)
{
	bool found = false;
	int64_t bd = INT64_MAX;
	for (unsigned q = 0; q < MAX_PLAYERS; ++q)
	{
		if (q == selectedPlayer || aiCheckAlliances(q, selectedPlayer)) { continue; }
		for (const STRUCTURE *st : gameWorld.objects.structures[q])
		{
			if (st == nullptr || st->died || st->visible[selectedPlayer] == 0) { continue; }
			const int64_t d = dist2(st->pos.xy(), p);
			if (d < bd) { bd = d; out = st->pos.xy(); found = true; }
		}
	}
	return found;
}

void launch(Elem &t)
{
	std::vector<std::pair<uint32_t, DROID *>> units;
	for (auto &kv : s_units)
	{
		if (kv.second.target != t.id || kv.second.phase != U_AT_TARGET) { continue; }
		bool aboard = false;
		DROID *d = findDroid(kv.first, &aboard);
		if (d != nullptr && !aboard) { units.emplace_back(kv.first, d); }
	}
	if (units.empty()) { return; }
	std::vector<const Elem *> vecs;
	for (const Elem &v : s_elems) { if (v.kind == K_VECTOR && v.pts.size() >= 2 && linkedTarget(v) == t.id) { vecs.push_back(&v); } }
	std::stable_sort(vecs.begin(), vecs.end(), [](const Elem *a, const Elem *b) { return a->prio != b->prio ? a->prio < b->prio : a->id < b->id; });
	std::vector<std::vector<Vector2i>> routes(units.size());
	if (!vecs.empty())
	{
		// Einheiten nach Gewicht (Prioritaet 1 = 5 Teile ... 5 = 1 Teil) auf die Vektoren verteilen, jeder bekommt die naechsten Einheiten
		int wsum = 0;
		for (const Elem *v : vecs) { wsum += 6 - std::clamp(v->prio, 1, 5); }
		std::vector<size_t> want(vecs.size(), 0);
		size_t given = 0;
		for (size_t k = 0; k < vecs.size(); ++k) { want[k] = units.size() * static_cast<size_t>(6 - std::clamp(vecs[k]->prio, 1, 5)) / static_cast<size_t>(std::max(1, wsum)); given += want[k]; }
		for (size_t k = 0; given < units.size(); k = (k + 1) % vecs.size()) { ++want[k]; ++given; }
		std::vector<bool> used(units.size(), false);
		for (size_t k = 0; k < vecs.size(); ++k)
		{
			const std::vector<Vector2i> wp = resample(vecs[k]->pts, std::min<size_t>(6, std::max<size_t>(2, vecs[k]->pts.size())));
			for (size_t n = 0; n < want[k]; ++n)
			{
				int best = -1;
				int64_t bd = INT64_MAX;
				for (size_t i = 0; i < units.size(); ++i) { if (!used[i]) { const int64_t dd = dist2(units[i].second->pos.xy(), wp.front()); if (dd < bd) { bd = dd; best = static_cast<int>(i); } } }
				if (best < 0) { break; }
				used[static_cast<size_t>(best)] = true;
				std::vector<Vector2i> r(wp.begin() + 1, wp.end());
				r.back() = clampMap(spread(r.back(), n));
				routes[static_cast<size_t>(best)] = r;
			}
		}
	}
	Vector2i goal;
	const bool haveEnemy = enemyNear(anchor(t), goal);
	if (!haveEnemy)
	{
		// kein Feind bekannt: 20 Felder weiter, weg von der eigenen Basis
		int64_t sx = 0, sy = 0, cnt = 0;
		for (const STRUCTURE *st : gameWorld.objects.structures[selectedPlayer]) { if (st != nullptr && !st->died) { sx += st->pos.x; sy += st->pos.y; ++cnt; } }
		const Vector2i a = anchor(t);
		double dx = 1.0, dy = 0.0;
		if (cnt > 0) { dx = a.x - static_cast<double>(sx / cnt); dy = a.y - static_cast<double>(sy / cnt); }
		const double l = std::max(1.0, std::sqrt(dx * dx + dy * dy));
		goal = clampMap(Vector2i(a.x + static_cast<int>(dx / l * DIST_TILE * 20), a.y + static_cast<int>(dy / l * DIST_TILE * 20)));
	}
	for (size_t i = 0; i < units.size(); ++i)
	{
		DROID *d = units[i].second;
		URec &u = s_units[units[i].first];
		if (routes[i].empty()) { routes[i].push_back(clampMap(spread(goal, i))); }
		for (size_t k = 0; k < routes[i].size(); ++k) { order(d, u, DroidOrder(DORDER_SCOUT, routes[i][k]), k > 0); }
		if (!modcombot::isModular(d)) { secondarySetState(d, gameWorld.objects, DSO_HALTTYPE, DSS_HALT_PURSUE); }
		u.phase = U_ATTACK;
		u.since = gameTime;
		u.slot = -1;
	}
	++t.waves;
	char msg[200];
	snprintf(msg, sizeof(msg), _("Attack plan: wave %u from the %s attacks (%u units)"), t.waves, t.kind == K_BRIDGEHEAD ? _("bridgehead") : _("assault line"),
	         static_cast<unsigned>(units.size()));
	addConsoleMessage(msg, DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
}

void attackNow()
{
	bool any = false;
	for (Elem &t : s_elems) { if (isTarget(t) && arrived(t.id) > 0) { launch(t); any = true; } }
	if (!any) { addConsoleMessage(_("Attack plan: no units are waiting at a bridgehead or assault line yet"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
}

/// Bereitstellungsraum: Gruppen fahren gemeinsam los (Tunnel, Transporter oder auf eigene Faust)
void departStep()
{
	const bool trans = haveTransporters();
	for (const Elem &s : s_elems)
	{
		if (s.kind != K_STAGING) { continue; }
		std::map<uint32_t, std::vector<uint32_t>> byTarget;
		for (auto &kv : s_units) { if (kv.second.stage == s.id && kv.second.phase == U_AT_STAGE && kv.second.target != 0) { byTarget[kv.second.target].push_back(kv.first); } }
		for (auto &bt : byTarget)
		{
			const Elem *t = elem(bt.first);
			if (t == nullptr) { continue; }
			Vector2i entry;
			const bool tunnel = tunnelFor(s, *t, entry);
			const bool ferry = !tunnel && s.beacon && trans && distTo(*t, s.c) > 15 * DIST_TILE;
			if (ferry)
			{
				size_t i = 0;
				for (uint32_t id : bt.second)
				{
					DROID *d = findDroid(id);
					if (d == nullptr) { continue; }
					URec &u = s_units[id];
					u.phase = U_FERRY;
					u.since = gameTime;
					order(d, u, DroidOrder(DORDER_MOVE, clampMap(spread(s.beaconPos, 1 + i++))));
				}
				continue;
			}
			unsigned oldest = gameTime;
			for (uint32_t id : bt.second) { oldest = std::min(oldest, s_units[id].since); }
			const int en = countUnits(t->id, U_FERRY, U_AT_TARGET);
			int need = GROUP_MIN;   // naechste Welle: in Gruppen
			if (t->goal > 0 && t->goal - en > 0) { need = std::min(GROUP_MIN, t->goal - en); }   // es fehlen nur noch wenige bis zur Sollstaerke
			if (static_cast<int>(bt.second.size()) < need && gameTime - oldest < STAGE_WAIT_MAX) { continue; }
			for (uint32_t id : bt.second)
			{
				DROID *d = findDroid(id);
				if (d == nullptr) { continue; }
				URec &u = s_units[id];
				if (u.slot < 0) { u.slot = freeSlot(t->id); }
				const Vector2i spot = targetSpot(*t, u.slot);
				if (tunnel)
				{
					order(d, u, DroidOrder(DORDER_MOVE, entry));          // in den Tunnel ...
					order(d, u, DroidOrder(DORDER_SCOUT, spot), true);   // ... und nach dem Austritt zum Platz
				}
				else { order(d, u, DroidOrder(DORDER_SCOUT, spot)); }
				u.phase = U_TO_TARGET;
				u.since = gameTime;
			}
		}
	}
}

void unitStep()
{
	const int64_t near2 = static_cast<int64_t>(5 * DIST_TILE / 2) * (5 * DIST_TILE / 2);
	for (auto it = s_units.begin(); it != s_units.end();)
	{
		URec &u = it->second;
		bool aboard = false;
		DROID *d = findDroid(it->first, &aboard);
		if (d == nullptr) { it = s_units.erase(it); continue; }
		if (!aboard && !modtunnel::inTransit(d) && overridden(d, u.sent)) { it = s_units.erase(it); continue; }   // der Spieler hat uebernommen
		const bool idle = d->order.type == DORDER_NONE || d->order.type == DORDER_GUARD || d->order.type == DORDER_HOLD;
		switch (u.phase)
		{
		case U_TO_STAGE:
		{
			const Elem *s = elem(u.stage);
			if (s == nullptr) { break; }
			if (inside(*s, d->pos.xy(), DIST_TILE)) { u.phase = U_AT_STAGE; u.since = gameTime; }
			else if ((idle && gameTime - u.sent > 6000) || u.sent == 0) { order(d, u, DroidOrder(DORDER_MOVE, clampMap(spread(s->c, d->id % 23u)))); }
			break;
		}
		case U_AT_STAGE:
			if (u.target == 0 || elem(u.target) == nullptr) { u.target = pickTarget(); u.slot = -1; }
			break;
		case U_FERRY:
		{
			if (aboard || modtunnel::inTransit(d)) { break; }
			const Elem *s = elem(u.stage);
			const Elem *t = elem(u.target);
			if (t == nullptr) { u.target = pickTarget(); u.slot = -1; break; }
			if (dist2(d->pos.xy(), dropPoint(*t)) <= static_cast<int64_t>(8 * DIST_TILE) * (8 * DIST_TILE))
			{
				// abgesetzt: zum Platz am Ziel
				if (u.slot < 0) { u.slot = freeSlot(t->id); }
				order(d, u, DroidOrder(DORDER_SCOUT, targetSpot(*t, u.slot)));
				u.phase = U_TO_TARGET;
				u.since = gameTime;
			}
			else if (s != nullptr && idle && d->order.type != DORDER_EMBARK && dist2(d->pos.xy(), s->beaconPos) > static_cast<int64_t>(6 * DIST_TILE) * (6 * DIST_TILE)
			         && gameTime - u.sent > 8000)
			{
				order(d, u, DroidOrder(DORDER_MOVE, clampMap(spread(s->beaconPos, 1 + d->id % 11u))));
			}
			break;
		}
		case U_TO_TARGET:
		{
			const Elem *t = elem(u.target);
			if (t == nullptr || aboard || modtunnel::inTransit(d)) { break; }
			if (u.slot < 0) { u.slot = freeSlot(t->id); }
			const Vector2i spot = targetSpot(*t, u.slot);
			const bool there = dist2(d->pos.xy(), spot) <= near2 || (idle && distTo(*t, d->pos.xy()) <= 3 * DIST_TILE);
			if (there) { u.phase = U_AT_TARGET; u.since = gameTime; }
			else if ((idle && gameTime - u.sent > 8000) || u.sent == 0) { order(d, u, DroidOrder(DORDER_SCOUT, spot)); }
			break;
		}
		case U_AT_TARGET:
		{
			const Elem *t = elem(u.target);
			if (t == nullptr || aboard) { break; }
			const Vector2i spot = targetSpot(*t, u.slot);
			if (idle && dist2(d->pos.xy(), spot) > static_cast<int64_t>(5 * DIST_TILE) * (5 * DIST_TILE) && gameTime - u.sent > 20000)
			{
				order(d, u, DroidOrder(DORDER_MOVE, spot));   // abgedriftet (Gefecht): zurueck auf den Platz
			}
			break;
		}
		case U_ATTACK:
			if ((idle && gameTime - u.since > 20000) || gameTime - u.since > ATTACK_MAX) { it = s_units.erase(it); continue; }
			break;
		default:
			break;
		}
		++it;
	}
}

/// Transportschleife: freie Transporter holen wartende Einheiten an den Leuchtfeuern ab (Ziel-Prioritaet zuerst)
void ferryStep()
{
	if (selectedPlayer >= MAX_PLAYERS) { return; }
	// Nachfrage: (Zone, Ziel) -> wartende Einheiten am Leuchtfeuer
	struct Demand { uint32_t stage, target; std::vector<DROID *> units; int key; };
	std::vector<Demand> demand;
	const int64_t at2 = static_cast<int64_t>(6 * DIST_TILE) * (6 * DIST_TILE);
	for (auto &kv : s_units)
	{
		if (kv.second.phase != U_FERRY) { continue; }
		bool aboard = false;
		DROID *d = findDroid(kv.first, &aboard);
		const Elem *s = elem(kv.second.stage);
		const Elem *t = elem(kv.second.target);
		if (d == nullptr || aboard || s == nullptr || t == nullptr || !s->beacon) { continue; }
		if (dist2(d->pos.xy(), s->beaconPos) > at2 || d->order.type == DORDER_EMBARK) { continue; }
		auto dm = std::find_if(demand.begin(), demand.end(), [&](const Demand &x) { return x.stage == s->id && x.target == t->id; });
		if (dm == demand.end()) { demand.push_back(Demand{ s->id, t->id, {}, t->prio * 10 + s->prio }); dm = demand.end() - 1; }
		dm->units.push_back(d);
	}
	std::stable_sort(demand.begin(), demand.end(), [](const Demand &a, const Demand &b) { return a.key != b.key ? a.key < b.key : a.units.size() > b.units.size(); });
	std::set<uint32_t> served;   // Nachfrage, die schon ein Transporter anfliegt
	for (const auto &kv : s_trans) { if (kv.second.phase == T_TO_PICKUP || kv.second.phase == T_LOADING) { served.insert(kv.second.stage * 65536u + kv.second.target); } }
	for (DROID *t : gameWorld.objects.droids[selectedPlayer])
	{
		if (t == nullptr || t->died || !t->isTransporter() || modlayer::layerOf(t) != modlayer::SURFACE || modlayer::inTransition(t)) { continue; }
		auto ti = s_trans.find(t->id);
		const bool idle = t->order.type == DORDER_NONE || t->order.type == DORDER_HOLD;
		if (ti == s_trans.end())
		{
			if (!idle || modcontrol::manual(t) || demand.empty()) { continue; }
			ti = s_trans.emplace(t->id, TRec()).first;
		}
		TRec &r = ti->second;
		if (overridden(t, r.sent)) { s_trans.erase(ti); continue; }
		auto sendT = [&](const DroidOrder &o) { send(t, o, false); r.sent = gameTime; };
		const Elem *s = elem(r.stage);
		const Elem *tg = elem(r.target);
		switch (r.phase)
		{
		case T_IDLE:
		{
			if (!transporterIsEmpty(t))
			{
				// noch beladen (z. B. Ziel entfernt): zum Ziel der Fracht, sonst an Ort und Stelle absetzen
				if (tg == nullptr) { r.target = pickTarget(); tg = elem(r.target); }
				sendT(DroidOrder(DORDER_DISEMBARK, tg != nullptr ? dropPoint(*tg) : t->pos.xy()));
				r.phase = T_OUTBOUND;
				r.since = gameTime;
				break;
			}
			const Demand *pick = nullptr;
			for (const Demand &dm : demand) { if (!served.count(dm.stage * 65536u + dm.target)) { pick = &dm; break; } }
			if (pick == nullptr && !demand.empty()) { pick = &demand.front(); }   // mehrere Transporter fuer die wichtigste Nachfrage
			if (pick == nullptr) { break; }
			r.stage = pick->stage; r.target = pick->target;
			served.insert(pick->stage * 65536u + pick->target);
			sendT(DroidOrder(DORDER_MOVE, elem(r.stage)->beaconPos));
			r.phase = T_TO_PICKUP;
			r.since = gameTime;
			break;
		}
		case T_TO_PICKUP:
		{
			if (s == nullptr || tg == nullptr || !s->beacon) { r = TRec(); r.sent = gameTime; break; }
			if (dist2(t->pos.xy(), s->beaconPos) <= static_cast<int64_t>(4 * DIST_TILE) * (4 * DIST_TILE) && idle)
			{
				r.phase = T_LOADING;
				r.since = gameTime;
			}
			else if (idle && gameTime - r.sent > 5000) { sendT(DroidOrder(DORDER_MOVE, s->beaconPos)); }
			break;
		}
		case T_LOADING:
		{
			if (s == nullptr || tg == nullptr) { r = TRec(); r.sent = gameTime; break; }
			int space = calcRemainingCapacity(t);
			bool coming = false;
			for (const DROID *u : gameWorld.objects.droids[selectedPlayer]) { if (u != nullptr && !u->died && u->order.type == DORDER_EMBARK && u->order.psObj == t) { coming = true; space -= transporterSpaceRequired(u); } }
			for (const Demand &dm : demand)
			{
				if (dm.stage != r.stage || dm.target != r.target) { continue; }
				for (DROID *u : dm.units)
				{
					if (space <= 0) { break; }
					if (u->order.type == DORDER_EMBARK || !checkTransporterSpace(t, u, false)) { continue; }
					const int need = transporterSpaceRequired(u);
					if (need > space) { continue; }
					send(u, DroidOrder(DORDER_EMBARK, t), false);
					s_units[u->id].sent = gameTime;
					space -= need;
					coming = true;
				}
			}
			const unsigned waited = gameTime - r.since;
			const bool empty = transporterIsEmpty(t);
			if (!empty && (calcRemainingCapacity(t) <= 0 || waited >= LOAD_MAX_MS || (!coming && waited >= LOAD_MIN_MS)))
			{
				sendT(DroidOrder(DORDER_DISEMBARK, dropPoint(*tg)));
				r.phase = T_OUTBOUND;
				r.since = gameTime;
			}
			else if (empty && !coming && waited >= LOAD_MAX_MS) { r.phase = T_IDLE; }
			break;
		}
		case T_OUTBOUND:
			if (transporterIsEmpty(t) && idle && gameTime - r.since > 3000)
			{
				r.phase = T_IDLE;
				r.since = gameTime;
				if (demand.empty() && s != nullptr && s->beacon) { sendT(DroidOrder(DORDER_MOVE, s->beaconPos)); r.phase = T_RETURN; }
			}
			break;
		case T_RETURN:
			if (idle || !demand.empty()) { r.phase = T_IDLE; }
			break;
		default:
			r.phase = T_IDLE;
			break;
		}
	}
	for (auto it = s_trans.begin(); it != s_trans.end();)
	{
		const DROID *t = IdToDroid(gameWorld.objects, it->first, selectedPlayer);
		it = (t == nullptr || t->died) ? s_trans.erase(it) : std::next(it);
	}
}

void step()
{
	if (selectedPlayer >= MAX_PLAYERS) { return; }
	if (s_elems.empty() && s_units.empty() && s_trans.empty()) { return; }
	if (gameTime < s_nextStep) { return; }
	s_nextStep = gameTime + STEP_MS;
	intake();
	unitStep();
	departStep();
	ferryStep();
	// Sollstaerke erreicht -> Angriff
	for (Elem &t : s_elems)
	{
		if (isTarget(t) && t.goal > 0 && arrived(t.id) >= t.goal) { launch(t); }
	}
}

int defaultPrio(int kind)
{
	int n = 0;
	for (const Elem &e : s_elems) { if (e.kind == kind) { ++n; } }
	return std::clamp(n + 1, 1, 5);
}

Elem &add(int kind)
{
	if (!s_knownInit || s_elems.empty()) { initKnown(); }
	Elem e;
	e.id = s_nextId++;
	e.kind = kind;
	e.prio = defaultPrio(kind);
	s_elems.push_back(e);
	return s_elems.back();
}

STRUCTURE_STATS *tunnelStats()
{
	for (unsigned i = 0; i < numStructureStats; ++i) { if (modtunnel::isMouthStat(&asStructureStats[i])) { return &asStructureStats[i]; } }
	return nullptr;
}

void digTunnel(const std::vector<Vector2i> &raw)
{
	STRUCTURE_STATS *st = tunnelStats();
	if (st == nullptr || raw.size() < 2) { return; }
	const Vector2i a = raw.front(), b = raw.back();
	std::vector<DROID *> drills;
	for (DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d != nullptr && !d->died && d->selected && modlayerdig::isDrill(d) && !modtunnel::digging(d)) { drills.push_back(d); } }
	if (drills.empty())
	{
		DROID *best = nullptr;
		int64_t bd = INT64_MAX;
		for (DROID *d : gameWorld.objects.droids[selectedPlayer])
		{
			if (d == nullptr || d->died || !modlayerdig::isDrill(d) || modtunnel::digging(d) || modlayer::layerOf(d) != modlayer::SURFACE) { continue; }
			const int64_t dd = dist2(d->pos.xy(), a) + (idleUnit(d) ? 0 : static_cast<int64_t>(40 * DIST_TILE) * (40 * DIST_TILE));
			if (dd < bd) { bd = dd; best = d; }
		}
		if (best != nullptr) { drills.push_back(best); }
	}
	if (drills.empty())
	{
		addConsoleMessage(_("Dig Tunnel: no drill rig - train one at the factory first"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		return;
	}
	DROID *d = drills.front();   // ein Bohrer je Tunnel
	const Vector2i dv = b - a;
	const uint16_t dir = static_cast<uint16_t>(iAtan2(dv.x, dv.y));
	if (dist2(d->pos.xy(), a) > static_cast<int64_t>(2 * DIST_TILE) * (2 * DIST_TILE))
	{
		send(d, DroidOrder(DORDER_MOVE, a), false);
		send(d, DroidOrder(DORDER_BUILD, st, b, dir), true);
	}
	else { send(d, DroidOrder(DORDER_BUILD, st, b, dir), false); }
	addConsoleMessage(_("Dig Tunnel: the drill rig drives to the entrance and digs towards the exit"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
}

/// eine Zeichnung im Schlachtplan wird Teil des Angriffsplans
void fromStroke(Mode m, const std::vector<Vector2i> &raw)
{
	if (selectedPlayer >= MAX_PLAYERS || raw.empty()) { return; }
	std::vector<DROID *> sel;
	for (DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d != nullptr && !d->died && d->selected && fitsPlan(d) && modlayer::layerOf(d) == modlayer::SURFACE) { sel.push_back(d); } }
	const Vector2i c = centroid(raw);
	int64_t far2 = 0;
	for (const Vector2i &p : raw) { far2 = std::max(far2, dist2(p, c)); }
	const int rr = static_cast<int>(std::sqrt(static_cast<double>(far2)));
	switch (m)
	{
	case M_STAGING:
	{
		Elem &e = add(K_STAGING);
		e.c = clampMap(c);
		e.r = rr < DIST_TILE ? 5 * DIST_TILE : std::clamp(rr, 3 * DIST_TILE, 16 * DIST_TILE);
		const uint32_t id = e.id;
		for (DROID *d : sel) { s_units.erase(d->id); join(d, id, 0); }
		addConsoleMessage(_("Attack plan: staging zone set - new units gather here"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		break;
	}
	case M_BRIDGEHEAD:
	{
		Elem &e = add(K_BRIDGEHEAD);
		e.c = clampMap(c);
		e.r = rr < DIST_TILE ? 6 * DIST_TILE : std::clamp(rr, 3 * DIST_TILE, 14 * DIST_TILE);
		e.goal = 12;
		const uint32_t id = e.id;
		for (DROID *d : sel) { s_units.erase(d->id); join(d, 0, id); }
		fixRefs();
		addConsoleMessage(_("Attack plan: bridgehead set - units are brought here and attack at full strength (or on Attack!)"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		break;
	}
	case M_OFFENSE:
	{
		if (raw.size() < 2) { return; }
		Elem &e = add(K_ASSAULT);
		e.pts = raw;
		e.c = c;
		e.side = defendSide(raw);
		e.goal = 12;
		fixRefs();
		addConsoleMessage(_("Attack plan: assault line set - units line up in a double row and attack at full strength (or on Attack!)"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		break;
	}
	case M_ATTACK:
	{
		if (raw.size() < 2) { return; }
		Elem &e = add(K_VECTOR);
		e.pts = raw;
		e.c = c;
		break;
	}
	case M_FERRY:
	{
		const Vector2i p = clampMap(raw.back());
		Elem *zone = nullptr;
		int64_t bd = INT64_MAX;
		for (Elem &e : s_elems)
		{
			if ((e.kind == K_STAGING || e.kind == K_BRIDGEHEAD) && inside(e, p, DIST_TILE))
			{
				const int64_t d = dist2(e.c, p);
				if (d < bd) { bd = d; zone = &e; }
			}
		}
		if (zone == nullptr)
		{
			zone = &add(K_STAGING);
			zone->c = p;
			zone->r = 5 * DIST_TILE;
		}
		zone->beacon = true;
		zone->beaconPos = p;
		addConsoleMessage(zone->kind == K_STAGING ? _("Attack plan: ferry beacon set - transports pick up the units here and fly them to the bridgeheads")
		                                          : _("Attack plan: landing spot of the transports set"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		break;
	}
	case M_TUNNEL:
		digTunnel(raw);
		break;
	default:
		break;
	}
}

void removeElem(uint32_t id)
{
	s_elems.erase(std::remove_if(s_elems.begin(), s_elems.end(), [id](const Elem &e) { return e.id == id; }), s_elems.end());
	fixRefs();
}

bool hasElems() { return !s_elems.empty(); }

void clearAll()
{
	s_elems.clear();
	s_units.clear();
	s_trans.clear();
	s_known.clear();
	s_knownInit = false;
	s_nextStep = 0;
	s_clearArmed = 0;
	s_hits.clear();
}

// ---------------------------------------------------------------- Anzeige
/// Punkt im Abstand s entlang des Weges
Vector2i pathAt(const std::vector<Vector2i> &p, double s)
{
	if (p.empty()) { return Vector2i(0, 0); }
	for (size_t i = 1; i < p.size(); ++i)
	{
		const double l = std::sqrt(static_cast<double>(dist2(p[i], p[i - 1])));
		if (s <= l && l > 0.0)
		{
			const double f = s / l;
			return Vector2i(p[i - 1].x + static_cast<int>((p[i].x - p[i - 1].x) * f), p[i - 1].y + static_cast<int>((p[i].y - p[i - 1].y) * f));
		}
		s -= l;
	}
	return p.back();
}
double pathLen(const std::vector<Vector2i> &p)
{
	double L = 0.0;
	for (size_t i = 1; i < p.size(); ++i) { L += std::sqrt(static_cast<double>(dist2(p[i], p[i - 1]))); }
	return L;
}
/// Weg ab dem Abstand s (erster Punkt = pathAt(s))
std::vector<Vector2i> pathFrom(const std::vector<Vector2i> &p, double s)
{
	std::vector<Vector2i> out;
	out.push_back(pathAt(p, s));
	double acc = 0.0;
	for (size_t i = 1; i < p.size(); ++i)
	{
		acc += std::sqrt(static_cast<double>(dist2(p[i], p[i - 1])));
		if (acc > s) { out.push_back(p[i]); }
	}
	return out;
}

PIELIGHT prioCol(int prio, int a)
{
	static const unsigned char C[5][3] = { { 255, 215, 80 }, { 255, 165, 70 }, { 235, 235, 235 }, { 165, 190, 215 }, { 130, 140, 155 } };
	const int k = std::clamp(prio, 1, 5) - 1;
	return pal_RGBA(C[k][0], C[k][1], C[k][2], static_cast<UBYTE>(std::clamp(a, 0, 255)));
}

void badge(const Elem &e, const Vector2i &world, const glm::mat4 &m, int alpha, bool hits)
{
	Vector2i sp(0, 0);
	if (!project(world, m, sp)) { return; }
	const float s = modui::uiScale();
	const int sc = modui::sizeClassFor(s);
	const int fh = (sc <= 0) ? 9 : (sc == 1 ? 12 : 16);
	const int h = fh + static_cast<int>(8 * s);
	char num[8];
	snprintf(num, sizeof(num), "%d", e.prio);
	const int w0 = std::max(h, modui::textWidth(num, sc) + static_cast<int>(10 * s));
	const int x0 = sp.x - w0 / 2, y0 = sp.y - h / 2;
	pie_DrawSoftRoundedBoxRotated(static_cast<float>(sp.x), static_cast<float>(sp.y), static_cast<float>(w0 + 6 * s), static_cast<float>(h + 6 * s), 0.f, pal_RGBA(0, 0, 0, static_cast<UBYTE>(alpha * 0.45f)), h * 0.5f, 4.f * s);
	pie_DrawSoftRoundedBoxRotated(static_cast<float>(sp.x), static_cast<float>(sp.y), static_cast<float>(w0), static_cast<float>(h), 0.f, prioCol(e.prio, alpha), h * 0.45f, 1.5f);
	modui::text(num, sp.x - modui::textWidth(num, sc) / 2, y0 + (h - fh) / 2 - 1, pal_RGBA(20, 20, 25, static_cast<UBYTE>(alpha)), sc);
	if (hits) { s_hits.push_back(Hit{ e.id, 0, x0, y0, x0 + w0, y0 + h }); }
	// zweites Schild: Staerke (Ziele) bzw. wartende Einheiten (Bereitstellungsraum)
	char info[48] = "";
	if (isTarget(e))
	{
		const int n = arrived(e.id), all = assigned(e.id);
		if (e.goal > 0) { snprintf(info, sizeof(info), "%d/%d", n, e.goal); } else { snprintf(info, sizeof(info), "%d  %s", n, _("Hold")); }
		if (all > n) { const size_t l = strlen(info); snprintf(info + l, sizeof(info) - l, "  +%d", all - n); }
	}
	else if (e.kind == K_STAGING)
	{
		int n = 0;
		for (const auto &kv : s_units) { if (kv.second.stage == e.id && (kv.second.phase == U_AT_STAGE || kv.second.phase == U_FERRY)) { ++n; } }
		snprintf(info, sizeof(info), "%d", n);
	}
	if (info[0] == '\0') { return; }
	const int w1 = modui::textWidth(info, sc) + static_cast<int>(12 * s);
	const int x1 = x0 + w0 + static_cast<int>(4 * s);
	pie_DrawSoftRoundedBoxRotated(static_cast<float>(x1 + w1 / 2), static_cast<float>(sp.y), static_cast<float>(w1), static_cast<float>(h), 0.f, pal_RGBA(15, 22, 32, static_cast<UBYTE>(alpha * 0.85f)), h * 0.3f, 1.5f);
	modui::text(info, x1 + static_cast<int>(6 * s), y0 + (h - fh) / 2 - 1, pal_RGBA(230, 235, 240, static_cast<UBYTE>(alpha)), sc);
	if (hits && isTarget(e)) { s_hits.push_back(Hit{ e.id, 1, x1, y0, x1 + w1, y0 + h }); }
}

void beaconMark(const Elem &e, const glm::mat4 &m, int alpha, bool hits)
{
	const int pulse = static_cast<int>(alpha * (0.75 + 0.25 * std::sin(static_cast<double>(realTime) / 220.0)));
	drawStroke(circlePts(e.beaconPos, DIST_TILE * 7 / 10, 14), M_FERRY, pulse, m);
	drawStroke(circlePts(e.beaconPos, DIST_TILE / 4, 8), M_FERRY, pulse, m);
	Vector2i sp(0, 0);
	if (hits && project(e.beaconPos, m, sp))
	{
		const int r = static_cast<int>(14 * modui::uiScale());
		s_hits.push_back(Hit{ e.id, 2, sp.x - r, sp.y - r, sp.x + r, sp.y + r });
	}
}

/// Angriffsvektoren: nahe beieinander beginnende Pfeile (bis 3) laufen erst als ein dicker Pfeil und spalten sich dann auf
void drawVectors(const glm::mat4 &m, int alpha, bool hits)
{
	std::vector<const Elem *> vs;
	for (const Elem &e : s_elems) { if (e.kind == K_VECTOR && e.pts.size() >= 2) { vs.push_back(&e); } }
	std::vector<int> grp(vs.size(), -1);
	auto startDir = [](const Elem *v) {
		const Vector2i a = v->pts.front(), b = pathAt(v->pts, std::min(pathLen(v->pts) * 0.5, 3.0 * DIST_TILE));
		const double dx = b.x - a.x, dy = b.y - a.y, l = std::max(1.0, std::sqrt(dx * dx + dy * dy));
		return std::make_pair(dx / l, dy / l);
	};
	int ng = 0;
	for (size_t i = 0; i < vs.size(); ++i)
	{
		if (grp[i] >= 0) { continue; }
		grp[i] = ng;
		int members = 1;
		const auto di = startDir(vs[i]);
		for (size_t j = i + 1; j < vs.size() && members < 3; ++j)
		{
			if (grp[j] >= 0) { continue; }
			if (dist2(vs[i]->pts.front(), vs[j]->pts.front()) > static_cast<int64_t>(5 * DIST_TILE) * (5 * DIST_TILE)) { continue; }
			const auto dj = startDir(vs[j]);
			if (di.first * dj.first + di.second * dj.second < 0.77) { continue; }   // mehr als ~40 Grad auseinander
			grp[j] = ng;
			++members;
		}
		++ng;
	}
	for (int g = 0; g < ng; ++g)
	{
		std::vector<const Elem *> mem;
		for (size_t i = 0; i < vs.size(); ++i) { if (grp[i] == g) { mem.push_back(vs[i]); } }
		if (mem.size() == 1)
		{
			drawStroke(mem[0]->pts, M_ATTACK, alpha, m, nullptr, 0, 1.4f);
			badge(*mem[0], mem[0]->pts.front(), m, alpha, hits);
			continue;
		}
		// gemeinsamer Stamm: Mittel der Wege, solange sie nah beieinander bleiben (hoechstens 70 % des kuerzesten Weges)
		double minL = 1e30;
		for (const Elem *v : mem) { minL = std::min(minL, pathLen(v->pts)); }
		std::vector<Vector2i> trunk;
		double split = 0.0;
		for (double s = 0.0; s <= minL * 0.7; s += DIST_TILE * 0.5)
		{
			std::vector<Vector2i> at;
			for (const Elem *v : mem) { at.push_back(pathAt(v->pts, s)); }
			int64_t spreadMax = 0;
			for (size_t a = 0; a < at.size(); ++a) { for (size_t b = a + 1; b < at.size(); ++b) { spreadMax = std::max(spreadMax, dist2(at[a], at[b])); } }
			if (s > 0.0 && spreadMax > static_cast<int64_t>(4 * DIST_TILE) * (4 * DIST_TILE)) { break; }
			trunk.push_back(centroid(at));
			split = s;
		}
		if (trunk.size() < 3)
		{
			for (const Elem *v : mem) { drawStroke(v->pts, M_ATTACK, alpha, m, nullptr, 0, 1.4f); badge(*v, v->pts.front(), m, alpha, hits); }
			continue;
		}
		drawStroke(trunk, M_ATTACK, alpha, m, nullptr, 0, 2.2f, false);
		for (const Elem *v : mem)
		{
			std::vector<Vector2i> br = pathFrom(v->pts, split);
			br.front() = trunk.back();
			s_noTaper = true;
			if (br.size() >= 2) { drawStroke(br, M_ATTACK, alpha, m, nullptr, 0, 1.3f); }
			s_noTaper = false;
		}
		// Schilder nebeneinander am Stamm
		for (size_t k = 0; k < mem.size(); ++k)
		{
			const Vector2i p = trunk.front() + Vector2i(static_cast<int>((static_cast<int>(k) - static_cast<int>(mem.size() / 2)) * DIST_TILE * 1.1), 0);
			badge(*mem[k], p, m, alpha, hits);
		}
	}
}

void draw(const glm::mat4 &m)
{
	const bool hits = !s_proj;
	if (hits) { s_hits.clear(); }
	if (s_elems.empty() || selectedPlayer >= MAX_PLAYERS) { return; }
	const bool open = modrosette::planOpen() || s_proj;
	const int alpha = open ? 215 : 95;
	const bool clickable = hits && modrosette::planOpen();
	// Transportschleifen (SupCom-Pfeile) von den Leuchtfeuern zu den Zielen, die gerade per Transporter versorgt werden
	if (haveTransporters())
	{
		std::set<uint64_t> routes;
		for (const auto &kv : s_units) { if (kv.second.phase == U_FERRY && kv.second.stage != 0 && kv.second.target != 0) { routes.insert(static_cast<uint64_t>(kv.second.stage) << 32 | kv.second.target); } }
		for (const auto &kv : s_trans) { if (kv.second.stage != 0 && kv.second.target != 0 && kv.second.phase != T_IDLE) { routes.insert(static_cast<uint64_t>(kv.second.stage) << 32 | kv.second.target); } }
		for (uint64_t k : routes)
		{
			const Elem *s = elem(static_cast<uint32_t>(k >> 32));
			const Elem *t = elem(static_cast<uint32_t>(k & 0xffffffffu));
			if (s == nullptr || t == nullptr || !s->beacon) { continue; }
			if (s_proj) { drawStroke({ s->beaconPos, dropPoint(*t) }, M_FERRY, alpha, m); }
			else { modferry::drawRoute(s->beaconPos, dropPoint(*t), m, open); }
		}
	}
	for (const Elem &e : s_elems)
	{
		switch (e.kind)
		{
		case K_STAGING:
			drawStroke(circlePts(e.c, e.r, 40), M_STAGING, alpha, m);
			if (e.beacon) { beaconMark(e, m, alpha, clickable); }
			badge(e, e.c + Vector2i(0, -e.r), m, alpha, clickable);
			break;
		case K_BRIDGEHEAD:
			drawStroke(circlePts(e.c, e.r, 40), M_BRIDGEHEAD, alpha, m);
			drawStroke(circlePts(e.c, std::max(DIST_TILE, e.r / 3), 20), M_BRIDGEHEAD, alpha * 2 / 3, m);
			if (e.beacon) { beaconMark(e, m, alpha, clickable); }
			badge(e, e.c + Vector2i(0, -e.r), m, alpha, clickable);
			break;
		case K_ASSAULT:
			drawStroke(e.pts, M_OFFENSE, alpha, m, nullptr, e.side, 1.25f);
			badge(e, e.pts.front(), m, alpha, clickable);
			break;
		default:
			break;
		}
	}
	drawVectors(m, alpha, clickable);
}

/// Klicks auf die Schilder: Links = Prioritaet / Sollstaerke weiterschalten, Rechts = Baustein (bzw. Leuchtfeuer) entfernen
bool click(bool overUi)
{
	if (overUi || s_hits.empty() || !modrosette::planOpen()) { return false; }
	const bool lmb = mousePressed(MOUSE_LMB), rmb = mousePressed(MOUSE_RMB);
	if (!lmb && !rmb) { return false; }
	const int mx = mouseX(), my = mouseY();
	for (auto it = s_hits.rbegin(); it != s_hits.rend(); ++it)
	{
		if (mx < it->x0 || mx > it->x1 || my < it->y0 || my > it->y1) { continue; }
		Elem *e = elem(it->elem);
		if (e == nullptr) { return true; }
		if (rmb)
		{
			if (it->what == 2) { e->beacon = false; fixRefs(); }
			else { removeElem(e->id); }
			return true;
		}
		if (it->what == 0) { e->prio = e->prio % 5 + 1; }
		else if (it->what == 1)
		{
			int k = 0;
			while (k < NGOALS && kGoals[k] != e->goal) { ++k; }
			e->goal = kGoals[(k + 1) % NGOALS];
		}
		return true;
	}
	return false;
}

void save(std::vector<int64_t> &out)
{
	out.clear();
	if (s_elems.empty()) { return; }
	out.push_back(1);
	out.push_back(s_nextId);
	out.push_back(static_cast<int64_t>(s_elems.size()));
	for (const Elem &e : s_elems)
	{
		out.insert(out.end(), { static_cast<int64_t>(e.id), e.kind, e.prio, e.c.x, e.c.y, e.r, e.beacon ? 1 : 0, e.beaconPos.x, e.beaconPos.y, e.goal, e.side, e.waves,
		                        static_cast<int64_t>(e.pts.size()) });
		for (const Vector2i &p : e.pts) { out.push_back(p.x); out.push_back(p.y); }
	}
	out.push_back(static_cast<int64_t>(s_units.size()));
	for (const auto &kv : s_units)
	{
		const URec &u = kv.second;
		out.insert(out.end(), { static_cast<int64_t>(kv.first), u.stage, u.target, u.phase, u.since, u.sent, u.slot });
	}
	out.push_back(static_cast<int64_t>(s_trans.size()));
	for (const auto &kv : s_trans)
	{
		const TRec &t = kv.second;
		out.insert(out.end(), { static_cast<int64_t>(kv.first), t.phase, t.stage, t.target, t.since, t.sent });
	}
}

void load(const std::vector<int64_t> &in)
{
	clearAll();
	size_t i = 0;
	auto next = [&in, &i](int64_t &v) { if (i >= in.size()) { return false; } v = in[i++]; return true; };
	int64_t ver = 0, v = 0, n = 0;
	if (!next(ver) || ver != 1 || !next(v)) { return; }
	s_nextId = static_cast<uint32_t>(std::max<int64_t>(1, v));
	if (!next(n)) { return; }
	for (int64_t k = 0; k < n; ++k)
	{
		int64_t f[13];
		for (int64_t &x : f) { if (!next(x)) { clearAll(); return; } }
		Elem e;
		e.id = static_cast<uint32_t>(f[0]); e.kind = static_cast<int>(f[1]); e.prio = std::clamp(static_cast<int>(f[2]), 1, 5);
		e.c = Vector2i(static_cast<int>(f[3]), static_cast<int>(f[4])); e.r = static_cast<int>(f[5]);
		e.beacon = f[6] != 0; e.beaconPos = Vector2i(static_cast<int>(f[7]), static_cast<int>(f[8]));
		e.goal = static_cast<int>(f[9]); e.side = static_cast<int>(f[10]); e.waves = static_cast<unsigned>(f[11]);
		for (int64_t q = 0; q < f[12]; ++q)
		{
			int64_t x = 0, y = 0;
			if (!next(x) || !next(y)) { clearAll(); return; }
			e.pts.emplace_back(static_cast<int>(x), static_cast<int>(y));
		}
		s_elems.push_back(e);
	}
	if (!next(n)) { return; }
	for (int64_t k = 0; k < n; ++k)
	{
		int64_t f[7];
		for (int64_t &x : f) { if (!next(x)) { return; } }
		URec u;
		u.stage = static_cast<uint32_t>(f[1]); u.target = static_cast<uint32_t>(f[2]); u.phase = static_cast<int>(f[3]);
		u.since = static_cast<unsigned>(f[4]); u.sent = static_cast<unsigned>(f[5]); u.slot = static_cast<int>(f[6]);
		s_units[static_cast<uint32_t>(f[0])] = u;
	}
	if (!next(n)) { return; }
	for (int64_t k = 0; k < n; ++k)
	{
		int64_t f[6];
		for (int64_t &x : f) { if (!next(x)) { return; } }
		TRec t;
		t.phase = static_cast<int>(f[1]); t.stage = static_cast<uint32_t>(f[2]); t.target = static_cast<uint32_t>(f[3]);
		t.since = static_cast<unsigned>(f[4]); t.sent = static_cast<unsigned>(f[5]);
		s_trans[static_cast<uint32_t>(f[0])] = t;
	}
	initKnown();
}
} // namespace plan

} // namespace

void tick()
{
	// (17.11. 14:14) Mod-Rosette: der Schlachtplan-Knopf blendet die Leiste ein/aus; "Infantry: build" liegt jetzt in der Rosette
	const bool want = !moddesigner::isOpen() && selectedPlayer < MAX_PLAYERS && modrosette::planOpen() && intMode != INT_DESIGN;
	infButtonTick(false);
	if (!want && !s_screen) { opTick(); planTick(); faceTick(); sweepTick(); plan::step(); return; }
	ensureScreen();
	if (!s_root) { return; }
	s_root->show(want);
	if (want) { layout(); }
	if (!want) { s_mode = -1; s_drawing = false; s_cur.clear(); }
	if (s_mode >= 0 && keyPressed(KEY_ESC)) { s_mode = -1; s_drawing = false; s_cur.clear(); }
	opTick();   // Bergetrupp (laeuft auch, wenn die Knoepfe verborgen sind)
	planTick(); // Oberbefehle wieder aufnehmen
	faceTick(); // Verteidigungslinie: am Platz zur Feindseite drehen
	sweepTick(); // Minen raeumen im Gebiet
	plan::step(); // (22.11.) Angriffsplaner
	// alte Zeichnungen entfernen
	s_done.erase(std::remove_if(s_done.begin(), s_done.end(), [](const Stroke &st) { return realTime - st.born > SHOW_MS; }), s_done.end());
}

bool processMouse(bool overUi, const Vector2i &worldPos)
{
	// (18.11. 12:14) Artilleriebasis: der echte Bauplan folgt der Maus (beim Ziehen: Mitte der Zeichnung) - statt der Befestigungs-Vorschau
	if (s_mode == M_ARTYBASE && (!overUi || s_drawing))
	{
		static std::vector<modbuild::Placement> s_artyPreview;
		s_artyPreview = artyBasePlan(s_drawing && !s_cur.empty() ? centroid(s_cur) : worldPos, s_drawing ? artyBaseSize(s_cur) : 1);
		modbuild::setExternalPreview(&s_artyPreview);
	}
	else if (s_mode == M_RAIL && s_drawing && s_cur.size() >= 2 && selectedPlayer < MAX_PLAYERS)
	{
		// MOD (21.11.): Vorschau der Gleisstrecke beim Ziehen (nur mit Bauern)
		static std::vector<modbuild::Placement> s_railPreview;
		static size_t s_railPts = 0;
		static unsigned s_railAt = 0;
		std::vector<Vector2i> pts = s_cur;
		if (pts.back() != worldPos) { pts.push_back(worldPos); }
		if (pts.size() != s_railPts || realTime - s_railAt > 150)
		{
			s_railPts = pts.size();
			s_railAt = realTime;
			bool anyBuilder = false;
			for (const DROID *d : pickUnits(M_RAIL, pts)) { if (isBuilder(d)) { anyBuilder = true; break; } }
			s_railPreview = anyBuilder ? modbuild::planRailLine(selectedPlayer, pts) : std::vector<modbuild::Placement>();
		}
		modbuild::setExternalPreview(&s_railPreview);
	}
	else if (s_mode == M_DEFNET && s_drawing && s_cur.size() >= 2 && selectedPlayer < MAX_PLAYERS)
	{
		// MOD (Wunsch 20.11. / 21.11. eigener Befehl): beim Ziehen zeigt die Bauplan-Vorschau das Grabennetz, das die Bauer ausheben werden
		// (nur wenn Bauer dabei sind; neu gerechnet, wenn die Linie waechst oder hoechstens alle 0,15 s)
		static std::vector<modbuild::Placement> s_netPreview;
		static size_t s_netPts = 0;
		static unsigned s_netAt = 0;
		std::vector<Vector2i> pts = s_cur;
		if (pts.back() != worldPos) { pts.push_back(worldPos); }
		if (pts.size() != s_netPts || realTime - s_netAt > 150)
		{
			s_netPts = pts.size();
			s_netAt = realTime;
			bool anyBuilder = false;
			for (const DROID *d : pickUnits(M_DEFNET, pts)) { if (isBuilder(d)) { anyBuilder = true; break; } }
			s_netPreview = anyBuilder ? modbuild::planDefenseNet(selectedPlayer, pts, defendSide(pts)) : std::vector<modbuild::Placement>();
		}
		modbuild::setExternalPreview(&s_netPreview);
	}
	else
	{
		modbuild::setExternalPreview(nullptr);
	}
	if (plan::click(overUi)) { return true; }   // (22.11.) Schilder des Angriffsplans: Prioritaet / Sollstaerke / entfernen
	if (s_mode < 0) { return false; }
	if (mouseReleased(MOUSE_RMB))
	{
		s_mode = -1;
		s_drawing = false;
		s_cur.clear();
		return true;
	}
	if (!s_drawing)
	{
		if (overUi) { return false; }
		if (mousePressed(MOUSE_LMB))
		{
			s_drawing = true;
			s_cur.clear();
			s_cur.push_back(worldPos);
		}
		return true;   // im Zeichenmodus keine Auswahl / Befehle per Maus
	}
	if (mouseDown(MOUSE_LMB))
	{
		if (dist2(worldPos, s_cur.back()) >= static_cast<int64_t>(DIST_TILE / 2) * (DIST_TILE / 2) && s_cur.size() < 400) { s_cur.push_back(worldPos); }
		return true;
	}
	// losgelassen: Zeichnung fertig
	s_drawing = false;
	if (s_cur.back() != worldPos) { s_cur.push_back(worldPos); }
	const Mode m = static_cast<Mode>(s_mode);
	// (Wunsch 18.11. 13:06 "wenn man sich verklickt, ist immer noch der Befehl ausgewaehlt, den man geben will"): ein blosses Antippen
	// (Strich kuerzer als eine halbe Kachel) gilt als Verklicken - nichts wird befohlen, der gewaehlte Befehl bleibt aktiv.
	// Ausnahme: Artilleriebasis und Sammelpunkt duerfen mit einem Klick gesetzt werden.
	if (m != M_ARTYBASE && m != M_RALLY && m != M_FERRY && m != M_STAGING && m != M_BRIDGEHEAD)
	{
		int64_t len = 0;
		for (size_t i = 1; i < s_cur.size(); ++i) { len += static_cast<int64_t>(std::sqrt(static_cast<double>(dist2(s_cur[i], s_cur[i - 1])))); }
		if (len < DIST_TILE / 2)
		{
			s_cur.clear();
			return true;
		}
	}
	if (kModes[m].area && s_cur.size() >= 8) { s_cur = cleanLoop(s_cur); }   // (22.11.) keine Schlenker an Flaechen
	const bool shift = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	if (!shift) { s_chain.clear(); }
	s_append = shift;
	applyStroke(m, s_cur);
	s_append = false;
	if (s_lastPlanned)
	{
		// Teil des Angriffsplans: bleibt als eigener Baustein stehen (kein verblassender Strich)
		s_cur.clear();
		if (!shift) { s_mode = -1; }
		return true;
	}
	Stroke st;
	st.mode = m;
	st.pts = s_cur;
	st.born = realTime;
	if (m == M_TRAPS || m == M_MINES) { st.marks = fieldSpots(s_cur, m == M_MINES); }
	if (m == M_DEFEND || m == M_OFFENSE || m == M_DEFNET) { st.side = defendSide(s_cur); }
	// MOD (Wunsch 16.11. 21:25): hoechstens ein Marker je Aktion - eine neue Zeichnung ersetzt die alte derselben Art
	// (18.11.) mit Shift bleiben die vorigen Marker stehen (die Befehle wurden ja angehaengt)
	if (!shift) { s_done.erase(std::remove_if(s_done.begin(), s_done.end(), [m](const Stroke &o) { return o.mode == m; }), s_done.end()); }
	s_done.push_back(std::move(st));
	if (s_done.size() > 24) { s_done.erase(s_done.begin()); }
	s_cur.clear();
	if (!shift) { s_mode = -1; }   // ein Zug je Knopfdruck - mit Shift bleibt der Modus fuer den naechsten Strich an
	return true;
}

void drawOverlay(const glm::mat4 &m)
{
	plan::draw(m);   // (22.11.) Angriffsplaner (bleibt stehen)
	for (const Stroke &st : s_done)
	{
		const unsigned age = realTime - st.born;
		const int a = age >= SHOW_MS ? 0 : static_cast<int>(230 - 200 * static_cast<int64_t>(age) / static_cast<int64_t>(SHOW_MS));
		if (a > 0) { drawStroke(st.pts, st.mode, a, m, &st.marks, st.side); }
	}
	if (s_op.active)
	{
		// Bergegebiet und Bereitstellungspunkt (pulsierend), solange der Trupp unterwegs ist
		const int a = 150 + static_cast<int>(60.0 * std::sin(static_cast<double>(realTime) / 300.0));
		drawStroke(circlePts(s_op.c, s_op.r, 36), M_RECOVER, a, m);
		if (s_op.phase == 0) { drawStroke(circlePts(s_op.deploy, DIST_TILE * 3 / 2, 16), M_RECOVER, a, m); }
	}
	if (s_drawing && s_mode >= 0)
	{
		// Vorschau: Querstriche / geplante Panzersperren und Minen schon waehrend des Zeichnens
		std::vector<Vector2i> marks;
		if ((s_mode == M_TRAPS || s_mode == M_MINES) && s_cur.size() >= 3) { marks = fieldSpots(s_cur, s_mode == M_MINES); }
		drawStroke(s_cur, s_mode, 255, m, &marks, (s_mode == M_DEFEND || s_mode == M_OFFENSE || s_mode == M_DEFNET) ? defendSide(s_cur) : 0);
	}
}

int stackTop()
{
	// (18.11. 12:10 "die Menues ueberlappen sich immer noch extrem") Oberkante des Stapels unten rechts, wenn keine Minikarte sichtbar ist:
	// offener Schlachtplan (unten rechts in der Ecke) - sonst der Bildschirmrand
	int x, y, w, h;
	calcRect(x, y, w, h);
	return modrosette::planOpen() ? y : static_cast<int>(screenHeight) - 4;
}

void selectMode(const char *label)
{
	for (int m = 0; m < M_COUNT; ++m)
	{
		if (label != nullptr && strcmp(kModes[m].label, label) == 0) { s_mode = m; s_drawing = false; s_cur.clear(); if (groupOf(m) >= 0) { s_group = groupOf(m); } return; }
	}
}

void drawOnMap(const std::function<bool(const Vector2i &, Vector2i &)> &toScreen)
{
	s_proj = toScreen;
	drawOverlay(glm::mat4(1.f));
	s_proj = nullptr;
}

const char *activeLabel()
{
	return s_mode >= 0 ? kModes[s_mode].cursor : nullptr;
}

void shutdown()
{
	if (s_screen)
	{
		widgRemoveOverlayScreen(s_screen);
	}
	if (s_infScreen)
	{
		widgRemoveOverlayScreen(s_infScreen);
	}
	s_infScreen = nullptr;
	s_infRoot = nullptr;
	s_infBtn = nullptr;
	s_screen = nullptr;
	s_root = nullptr;
	s_buttons.clear();
	s_tabs.clear();
	s_mode = -1;
	s_drawing = false;
	s_cur.clear();
	s_done.clear();
	s_chain.clear();
	s_append = false;
	s_op = SalvageOp();
	s_plan.clear();
	s_sweep.clear();
	s_nextPlanCheck = 0;
	plan::clearAll();
	s_planClearArmed = 0;
}

void savePlan(std::vector<int64_t> &out)
{
	plan::save(out);
}

void loadPlan(const std::vector<int64_t> &in)
{
	plan::load(in);
}
}
