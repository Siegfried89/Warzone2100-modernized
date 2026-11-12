// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Combot-Designer im Metal-Fatigue-Stil.
//
// Aufbau (Koordinaten in einem virtuellen 640x480-Bild, das auf den Bildschirm skaliert wird):
//   links   Reiter ARMS / LEGS / TORSO / ALL und die Teileliste (Mausrad und Pfeile blaettern)
//   Mitte   Montagehalle: das Combot steht auf der MF-Montageplattform und dreht sich langsam, neue Teile werden
//           in Echtzeit angesetzt, entfernte Teile fliegen weg (mit Klack-/Rassel-Geraeuschen)
//   rechts  das blaue MF-Diagramm (Zustand der Teile) mit den Teilenamen
//   unten   Werte-Leisten (Teil unter der Maus / ganzes Combot), Kosten, BUILD, Hilfstasten
// BUILD legt eine Vorlage an, die sofort in den Fabriken baubar ist.

#include "moddesigner.h"
#include "modcombot.h"
#include "modpanel.h"
#include "modui.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/sound/audio.h"
#include "lib/widget/button.h"
#include "lib/widget/form.h"
#include "lib/widget/widget.h"

#include "component.h"
#include "console.h"
#include "design.h"
#include "droid.h"
#include "droiddef.h"
#include "hci.h"
#include "stats.h"
#include "template.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace moddesigner
{

namespace
{

using modcombot::Part;

enum Tab { TAB_ARMS = 0, TAB_LEGS = 1, TAB_TORSO = 2, TAB_ALL = 3 };
enum Act { ACT_BUILD, ACT_MIRROR, ACT_CLR_R, ACT_CLR_L, ACT_RESET, ACT_EXIT, ACT_COUNT };
enum Phase { PH_IDLE = 0, PH_LEAVING = 1, PH_PENDING = 2, PH_ARRIVING = 3 };

constexpr int ROWS = 15;
constexpr int ROW_H = 16;
constexpr unsigned LEAVE_MS = 420, ARRIVE_MS = 620, LAND_MS = 470;
constexpr float DROP_PX = 150.f;          // Hoehe, aus der Teile herabkommen (in virtuellen Punkten)
constexpr float BAY_DEPTH = 1200.f;       // Abstand der Montagehalle (naeher als die Ansicht des Design-Bildschirms)

struct Entry
{
	std::string name;
	const Part *part = nullptr;
	const Part *set[4] = { nullptr, nullptr, nullptr, nullptr };   // nur Voreinstellungen: Torso, rechter Arm, linker Arm, Beine
	bool preset = false;
};

struct SlotAnim
{
	int phase = PH_IDLE;
	unsigned t0 = 0;
	bool clanked = false;
};

struct Ctx
{
	float S = 1.f;
	int ox = 0, oy = 0;
	int X(float v) const { return ox + static_cast<int>(std::lround(v * S)); }
	int Y(float v) const { return oy + static_cast<int>(std::lround(v * S)); }
	int L(float v) const { return static_cast<int>(std::lround(v * S)); }
};

bool s_open = false;
std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_FULLSCREENOVERLAY_CLICKFORM> s_root;
std::vector<std::shared_ptr<W_BUTTON>> s_tabBtn, s_rowBtn, s_arrBtn, s_sideBtn, s_hotBtn, s_actBtn;

int s_tab = TAB_ARMS;
int s_side = 1;                 // welcher Arm in der Reiterliste ARMS bearbeitet wird (+1 rechts, -1 links)
int s_scroll = 0;
int s_hoverNow = -1, s_hoverShown = -1;
std::vector<Entry> s_list;
const Part *s_target[modcombot::SLOT_COUNT] = { nullptr, nullptr, nullptr, nullptr };   // gewaehlt
const Part *s_shown[modcombot::SLOT_COUNT] = { nullptr, nullptr, nullptr, nullptr };    // in der Halle sichtbar
SlotAnim s_anim[modcombot::SLOT_COUNT];
std::string s_status;
bool s_statusBad = false;
unsigned s_statusUntil = 0;
unsigned s_flashStart = 0;
int s_lastW = 0, s_lastH = 0;

// ---------------------------------------------------------------- Hilfen

Ctx ctx()
{
	Ctx c;
	const float sx = static_cast<float>(screenWidth) / 640.f, sy = static_cast<float>(screenHeight) / 480.f;
	c.S = std::max(0.5f, std::floor(std::min(sx, sy) * 4.f) / 4.f);
	c.ox = (static_cast<int>(screenWidth) - static_cast<int>(std::lround(640.f * c.S))) / 2;
	c.oy = (static_cast<int>(screenHeight) - static_cast<int>(std::lround(480.f * c.S))) / 2;
	return c;
}

void play(const char *file)
{
	const int id = audio_GetTrackID(file);
	if (id >= 0) { audio_PlayTrack(id); }
}

void setStatus(const std::string &t, bool bad)
{
	s_status = t;
	s_statusBad = bad;
	s_statusUntil = realTime + 6000;
}

std::string spaced(const std::string &f)
{
	std::string r;
	for (size_t i = 0; i < f.size(); ++i)
	{
		if (i > 0 && std::isupper(static_cast<unsigned char>(f[i])) && !std::isupper(static_cast<unsigned char>(f[i - 1])) && f[i - 1] != ' ') { r += ' '; }
		r += f[i];
	}
	return r;
}

std::string shortArm(const Part *p)
{
	if (p == nullptr) { return std::string(); }
	std::string n = p->name;
	const size_t k = n.find(" Arm");
	if (k != std::string::npos) { n = n.substr(0, k); }
	return n;
}

int slotOfArm(const Part *p) { return p->side < 0 ? modcombot::SLOT_LARM : modcombot::SLOT_RARM; }

const Part *firstOf(modcombot::Kind kind, int side)
{
	for (const Part &p : modcombot::parts())
	{
		if (p.kind == kind && (kind != modcombot::KIND_ARM || p.side == side)) { return &p; }
	}
	return nullptr;
}

const Part *pick(const char *id, modcombot::Kind kind, int side)
{
	const Part *p = modcombot::find(id);
	return p != nullptr ? p : firstOf(kind, side);
}

struct Cost { int power = 0; int points = 0; };

Cost costOf(const Part *p)
{
	Cost c;
	if (p == nullptr) { return c; }
	const char *prefix = p->kind == modcombot::KIND_TORSO ? "MFT_" : (p->kind == modcombot::KIND_LEG ? "MFL_" : "MFA_");
	const COMPONENT_STATS *cs = getCompStatsFromName(WzString::fromUtf8(std::string(prefix) + p->id));
	if (cs != nullptr) { c.power = static_cast<int>(cs->buildPower); c.points = static_cast<int>(cs->buildPoints); }
	return c;
}

// ---------------------------------------------------------------- Liste

void rebuildList()
{
	s_list.clear();
	const auto &parts = modcombot::parts();
	if (s_tab == TAB_ALL)
	{
		for (const Part &t : parts)
		{
			if (t.kind != modcombot::KIND_TORSO || t.id.size() < 3 || t.id.compare(t.id.size() - 3, 3, "_T0") != 0) { continue; }
			Entry e;
			e.preset = true;
			e.name = spaced(t.file);
			e.set[modcombot::SLOT_TORSO] = &t;
			e.set[modcombot::SLOT_LEGS] = modcombot::find(t.file + "_L0") ? modcombot::find(t.file + "_L0") : modcombot::find("Basic_L0");
			for (const Part &a : parts)
			{
				if (a.kind != modcombot::KIND_ARM || a.file != t.file) { continue; }
				if (a.side > 0 && e.set[modcombot::SLOT_RARM] == nullptr) { e.set[modcombot::SLOT_RARM] = &a; }
				if (a.side < 0 && e.set[modcombot::SLOT_LARM] == nullptr) { e.set[modcombot::SLOT_LARM] = &a; }
			}
			e.part = &t;
			s_list.push_back(e);
		}
	}
	else
	{
		for (const Part &p : parts)
		{
			const bool match = (s_tab == TAB_ARMS && p.kind == modcombot::KIND_ARM && p.side == s_side)
			                   || (s_tab == TAB_LEGS && p.kind == modcombot::KIND_LEG)
			                   || (s_tab == TAB_TORSO && p.kind == modcombot::KIND_TORSO);
			if (!match) { continue; }
			Entry e;
			e.name = p.name;
			e.part = &p;
			s_list.push_back(e);
		}
	}
	const int maxScroll = std::max(0, static_cast<int>(s_list.size()) - ROWS);
	s_scroll = std::max(0, std::min(s_scroll, maxScroll));
}

bool isCurrent(const Entry &e)
{
	if (e.preset)
	{
		for (int i = 0; i < modcombot::SLOT_COUNT; ++i) { if (s_target[i] != e.set[i]) { return false; } }
		return true;
	}
	if (e.part == nullptr) { return false; }
	switch (e.part->kind)
	{
	case modcombot::KIND_TORSO: return s_target[modcombot::SLOT_TORSO] == e.part;
	case modcombot::KIND_LEG: return s_target[modcombot::SLOT_LEGS] == e.part;
	default: return s_target[slotOfArm(e.part)] == e.part;
	}
}

void assign(int slot, const Part *p)
{
	s_target[slot] = p;
}

void applyEntry(const Entry &e)
{
	if (e.preset)
	{
		for (int i = 0; i < modcombot::SLOT_COUNT; ++i) { assign(i, e.set[i]); }
		return;
	}
	if (e.part == nullptr) { return; }
	switch (e.part->kind)
	{
	case modcombot::KIND_TORSO: assign(modcombot::SLOT_TORSO, e.part); break;
	case modcombot::KIND_LEG: assign(modcombot::SLOT_LEGS, e.part); break;
	default: assign(slotOfArm(e.part), e.part); break;
	}
}

void setTab(int t)
{
	if (t == s_tab) { return; }
	s_tab = t;
	s_scroll = 0;
	rebuildList();
}

void setSide(int side)
{
	s_side = side;
	s_tab = TAB_ARMS;
	s_scroll = 0;
	rebuildList();
	// zum aktuell gewaehlten Arm blaettern
	const Part *cur = s_target[side > 0 ? modcombot::SLOT_RARM : modcombot::SLOT_LARM];
	for (size_t i = 0; i < s_list.size() && cur != nullptr; ++i)
	{
		if (s_list[i].part == cur) { s_scroll = std::max(0, std::min(static_cast<int>(i), static_cast<int>(s_list.size()) - ROWS)); break; }
	}
}

// ---------------------------------------------------------------- Zusammenbau-Animation

void resetAssembly(bool intro)
{
	const unsigned now = realTime;
	// Reihenfolge: Beine, Torso, rechter Arm, linker Arm
	const int order[4] = { modcombot::SLOT_LEGS, modcombot::SLOT_TORSO, modcombot::SLOT_RARM, modcombot::SLOT_LARM };
	for (int k = 0; k < 4; ++k)
	{
		const int s = order[k];
		if (intro)
		{
			s_shown[s] = nullptr;
			s_anim[s].phase = PH_PENDING;
			s_anim[s].t0 = now + 250u + static_cast<unsigned>(k) * 420u;
			s_anim[s].clanked = false;
		}
		else
		{
			s_shown[s] = s_target[s];
			s_anim[s] = SlotAnim();
		}
	}
}

void defaultsToTarget()
{
	assign(modcombot::SLOT_TORSO, pick("Basic_T0", modcombot::KIND_TORSO, 0));
	assign(modcombot::SLOT_LEGS, pick("Basic_L0", modcombot::KIND_LEG, 0));
	assign(modcombot::SLOT_RARM, pick("Basic_A0", modcombot::KIND_ARM, 1));
	assign(modcombot::SLOT_LARM, pick("Basic_A1", modcombot::KIND_ARM, -1));
	if (s_target[modcombot::SLOT_RARM] && s_target[modcombot::SLOT_RARM]->side <= 0) { assign(modcombot::SLOT_RARM, firstOf(modcombot::KIND_ARM, 1)); }
	if (s_target[modcombot::SLOT_LARM] && s_target[modcombot::SLOT_LARM]->side >= 0) { assign(modcombot::SLOT_LARM, firstOf(modcombot::KIND_ARM, -1)); }
}

void updateAnim(unsigned now)
{
	for (int s = 0; s < modcombot::SLOT_COUNT; ++s)
	{
		SlotAnim &a = s_anim[s];
		switch (a.phase)
		{
		case PH_IDLE:
			if (s_shown[s] != s_target[s])
			{
				if (s == modcombot::SLOT_TORSO || s_shown[s] == nullptr)
				{
					a.phase = PH_PENDING;
					a.t0 = now;
				}
				else
				{
					a.phase = PH_LEAVING;
					a.t0 = now;
					play("mfui_detach.ogg");
				}
			}
			break;
		case PH_LEAVING:
			if (now - a.t0 >= LEAVE_MS)
			{
				s_shown[s] = nullptr;
				a.phase = PH_PENDING;
				a.t0 = now + 80u;
			}
			break;
		case PH_PENDING:
			if (now >= a.t0)
			{
				if (s_target[s] != nullptr)
				{
					s_shown[s] = s_target[s];
					a.phase = PH_ARRIVING;
					a.t0 = now;
					a.clanked = false;
					play("mfui_attach.ogg");
				}
				else
				{
					s_shown[s] = nullptr;
					a.phase = PH_IDLE;
				}
			}
			break;
		case PH_ARRIVING:
			if (!a.clanked && now - a.t0 >= LAND_MS)
			{
				a.clanked = true;
				play("mfui_clank.ogg");
			}
			if (now - a.t0 >= ARRIVE_MS) { a.phase = PH_IDLE; }
			break;
		default: a.phase = PH_IDLE; break;
		}
	}
}

/// Verschiebung eines Teils waehrend der Animation in virtuellen Punkten (dx, dy; dy positiv = nach oben)
void slotOffset(int slot, unsigned now, float &dx, float &dy)
{
	dx = dy = 0.f;
	const SlotAnim &a = s_anim[slot];
	if (a.phase == PH_LEAVING)
	{
		const float p = std::min(1.f, static_cast<float>(now - a.t0) / static_cast<float>(LEAVE_MS));
		dy = DROP_PX * p * p;
		dx = std::sin(static_cast<float>(now) * 0.05f) * 2.5f * (1.f - p);
	}
	else if (a.phase == PH_ARRIVING)
	{
		const float p = std::min(1.f, static_cast<float>(now - a.t0) / static_cast<float>(ARRIVE_MS));
		const float q = 1.f - p;
		dy = DROP_PX * q * q * q;
		if (now - a.t0 >= LAND_MS) { dx = std::sin(static_cast<float>(now) * 0.09f) * 2.2f * (1.f - p) * 4.f; }
	}
}

// ---------------------------------------------------------------- Vorlage

bool makeTemplate(DROID_TEMPLATE &tpl, std::string &err)
{
	const Part *t = s_target[modcombot::SLOT_TORSO], *g = s_target[modcombot::SLOT_LEGS];
	const Part *arms[2] = { s_target[modcombot::SLOT_RARM], s_target[modcombot::SLOT_LARM] };
	if (t == nullptr) { err = "Choose a torso first."; return false; }
	if (g == nullptr) { err = "Choose legs first."; return false; }
	if (arms[0] == nullptr && arms[1] == nullptr) { err = "A combot needs at least one arm."; return false; }
	const COMPONENT_STATS *body = getCompStatsFromName(WzString::fromUtf8("MFT_" + t->id));
	const COMPONENT_STATS *prop = getCompStatsFromName(WzString::fromUtf8("MFL_" + g->id));
	if (body == nullptr || prop == nullptr) { err = "Torso or legs are not available in this game."; return false; }
	tpl = DROID_TEMPLATE();
	tpl.asParts[COMP_BODY] = static_cast<uint8_t>(body->index);
	tpl.asParts[COMP_PROPULSION] = static_cast<uint8_t>(prop->index);
	tpl.numWeaps = 0;
	for (const Part *a : arms)
	{
		if (a == nullptr) { continue; }
		const COMPONENT_STATS *w = getCompStatsFromName(WzString::fromUtf8("MFA_" + a->id));
		if (w == nullptr) { err = "Arm " + a->name + " is not available in this game."; return false; }
		tpl.asWeaps[tpl.numWeaps++] = static_cast<uint32_t>(w->index);
	}
	std::string name = "Combot " + t->file;
	const std::string a0 = shortArm(arms[0]), a1 = shortArm(arms[1]);
	if (!a0.empty() || !a1.empty()) { name += " " + (a0.empty() ? std::string("-") : a0) + "+" + (a1.empty() ? std::string("-") : a1); }
	if (name.size() > 60) { name.resize(60); }
	tpl.name = WzString::fromUtf8(name);
	return true;
}

void doBuild()
{
	DROID_TEMPLATE tpl;
	std::string err;
	if (!makeTemplate(tpl, err)) { setStatus(err, true); play("mfui_clank.ogg"); return; }
	if (!desStoreCombotTemplate(tpl, err)) { setStatus(err.empty() ? std::string("The design was rejected.") : err, true); return; }
	setStatus("Added to your designs: " + tpl.name.toUtf8(), false);
	s_flashStart = realTime;
	play("mfui_done.ogg");
}

void doMirror()
{
	const Part *r = s_target[modcombot::SLOT_RARM];
	const Part *l = s_target[modcombot::SLOT_LARM];
	const Part *src = r != nullptr ? r : l;
	if (src == nullptr) { setStatus("There is no arm to mirror.", true); return; }
	const int wantSide = -src->side;
	const Part *best = nullptr;
	for (const Part &p : modcombot::parts())
	{
		if (p.kind == modcombot::KIND_ARM && p.file == src->file && p.side == wantSide) { best = &p; break; }
	}
	if (best == nullptr) { setStatus("This arm has no mirrored counterpart.", true); return; }
	assign(slotOfArm(best), best);
}

void doClear(int slot)
{
	assign(slot, nullptr);
}

void doReset()
{
	defaultsToTarget();
	resetAssembly(true);
	setStatus("Combot reset.", false);
}

// ---------------------------------------------------------------- Zeichnen

void box(int x0, int y0, int x1, int y1, PIELIGHT col)
{
	iV_Box(x0, y0, x1, y1, col);
}

void drawFrame(int x, int y, int w, int h)
{
	pie_BoxFill_alpha(x, y, x + w, y + h, pal_RGBA(4, 10, 24, 235));
	box(x, y, x + w - 1, y + h - 1, modui::cyan(210));
	box(x + 1, y + 1, x + w - 2, y + h - 2, pal_RGBA(20, 90, 130, 255));
}

struct PartInfo
{
	std::string line1, line2, tag;
	int mj = 0, mp = 0;
};

PartInfo infoOf(const Part *p)
{
	PartInfo i;
	if (p == nullptr) { return i; }
	const Cost c = costOf(p);
	i.mj = c.power;
	i.mp = c.points;
	switch (p->kind)
	{
	case modcombot::KIND_TORSO:
		i.line1 = "HP " + std::to_string(p->st.hp);
		i.line2 = std::to_string(p->st.ak) + "/" + std::to_string(p->st.ah);
		break;
	case modcombot::KIND_LEG:
		i.line1 = "SPD " + std::to_string(p->st.spd);
		i.line2 = "";
		break;
	default:
	{
		i.line1 = std::to_string(p->st.dmg) + (p->st.melee ? " M" : "");
		char buf[24];
		std::snprintf(buf, sizeof(buf), "R%.1f", p->st.rng / 128.0);
		i.line2 = buf;
		break;
	}
	}
	for (const std::string &a : p->abil) { i.tag = a; break; }
	return i;
}

void drawStrip(const Ctx &c, int vy, modui::Img img, const PartInfo &inf, int sizeClass)
{
	const int x0 = c.X(6), y0 = c.Y(static_cast<float>(vy));
	modui::drawSized(img, x0, y0, c.L(static_cast<float>(modui::imgWidth(img))), c.L(static_cast<float>(modui::imgHeight(img))));
	const PIELIGHT white = pal_RGBA(235, 250, 255, 255);
	if (inf.line1.empty() && inf.mj == 0 && inf.mp == 0) { return; }
	auto put = [&](float vx, float vyy, const std::string &s) { modui::text(s, x0 + c.L(vx), y0 + c.L(vyy), white, sizeClass); };
	put(17, 13, std::to_string(inf.mj));
	put(60, 13, std::to_string(inf.mp));
	put(86, 13, inf.line1);
	put(167, 13, inf.line2);
	if (img == modui::STAT_B) { put(203, 13, inf.tag); }
}

void drawBay(const Ctx &c)
{
	const int bx = c.X(168), by = c.Y(6), bw = c.L(304), bh = c.L(330);
	drawFrame(bx, by, bw, bh);
	// Hintergrundstreifen und Rasterboden
	for (int i = 1; i < 12; ++i)
	{
		const int ly = by + bh * i / 12;
		pie_BoxFill_alpha(bx + 2, ly, bx + bw - 2, ly + 1, pal_RGBA(20, 70, 100, 70));
	}
	// Abtast-Strahl
	const float ph = static_cast<float>(realTime % 3600u) / 3600.f;
	const int sy = by + 2 + static_cast<int>((bh - 4) * ph);
	pie_BoxFill_alpha(bx + 2, sy, bx + bw - 2, sy + std::max(1, c.L(1.f)), pal_RGBA(40, 235, 255, 110));
	pie_BoxFill_alpha(bx + 2, sy - c.L(3), bx + bw - 2, sy, pal_RGBA(40, 235, 255, 25));
	modui::text("COMBOT ASSEMBLY BAY", bx + c.L(6), by + c.L(5), modui::cyan(), modui::sizeClassFor(c.S));

	if (modcombot::ready() == false) { return; }
	const unsigned now = realTime;
	const float yaw = std::fmod(static_cast<float>(now) * 0.012f, 360.f);   // 12 Grad pro Sekunde
	const int team = static_cast<int>(getPlayerColour(selectedPlayer));
	pie_SetGeometricOffset(bx + bw / 2, by + bh / 2 + c.L(14));
	const float sc = 3.2f * c.S;
	const float cy = 24.f;     // Mittelhoehe des Combots
	const glm::mat4 base = glm::translate(glm::vec3(0.f, 0.f, BAY_DEPTH))
	                       * glm::rotate(glm::radians(-28.f), glm::vec3(1.f, 0.f, 0.f))
	                       * glm::rotate(glm::radians(yaw), glm::vec3(0.f, 1.f, 0.f))
	                       * glm::scale(glm::vec3(sc))
	                       * glm::translate(glm::vec3(0.f, -cy, 0.f));
	const glm::mat4 view(1.f);

	// Montageplattform (Oberkante bei y = 0)
	iIMDBaseShape *pad = modelGet(WzString::fromUtf8("mfs_x25.pie"));
	if (pad != nullptr && pad->displayModel() != nullptr)
	{
		pie_Draw3DShape(pad->displayModel(), 0, team, WZCOL_WHITE, pie_BUTTON, 0, base * glm::translate(glm::vec3(0.f, -6.56f, 0.f)), view);
	}

	modcombot::Loadout lo;
	lo.torso = s_shown[modcombot::SLOT_TORSO];
	lo.legs = s_shown[modcombot::SLOT_LEGS];
	lo.armR = s_shown[modcombot::SLOT_RARM];
	lo.armL = s_shown[modcombot::SLOT_LARM];
	lo.severed = 0;
	if (lo.torso == nullptr) { return; }
	const float unit = BAY_DEPTH / 990.f / c.S;   // Punkte -> Modelleinheiten im Augenraum
	modcombot::drawLoadout(lo, modcombot::ANIM_IDLE, now, 0, base, [&](const iIMDShape *shape, const glm::mat4 &m, const Part *p) {
		glm::mat4 mm = m;
		for (int s = 0; s < modcombot::SLOT_COUNT; ++s)
		{
			if (s_shown[s] == p && s_anim[s].phase != PH_IDLE)
			{
				float dx, dy;
				slotOffset(s, now, dx, dy);
				mm = glm::translate(glm::vec3(dx * unit * c.S, dy * unit * c.S, 0.f)) * m;
				break;
			}
		}
		pie_Draw3DShape(shape, 0, team, WZCOL_WHITE, pie_BUTTON, 0, mm, view);
	});
}

void drawScreen(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	(void)psWidget; (void)xOffset; (void)yOffset;
	if (!modui::ready()) { return; }
	s_hoverShown = s_hoverNow;
	s_hoverNow = -1;
	const Ctx c = ctx();
	const int sc = modui::sizeClassFor(c.S);
	const unsigned now = realTime;

	// Hintergrund
	pie_BoxFill(0, 0, static_cast<int>(screenWidth), static_cast<int>(screenHeight), pal_RGBA(6, 10, 20, 255));
	const int mw = c.L(320.f), mh = c.L(480.f);
	modui::drawSized(modui::METALPANEL, c.ox, c.oy, mw, mh);
	modui::drawSized(modui::METALPANEL, c.ox + mw, c.oy, mw, mh);

	// Reiter
	const modui::Img tabImg = s_tab == TAB_ARMS ? modui::TAB_ARMS : (s_tab == TAB_LEGS ? modui::TAB_LEGS : (s_tab == TAB_TORSO ? modui::TAB_TORSO : modui::TAB_ALL));
	modui::drawSized(tabImg, c.X(6), c.Y(6), c.L(154.f), c.L(28.f));

	// Liste
	drawFrame(c.X(6), c.Y(38), c.L(154.f), c.L(static_cast<float>(ROWS * ROW_H)));
	const int total = static_cast<int>(s_list.size());
	for (int i = 0; i < ROWS; ++i)
	{
		const int idx = s_scroll + i;
		if (idx >= total) { break; }
		const Entry &e = s_list[static_cast<size_t>(idx)];
		const int ry = c.Y(38.f + static_cast<float>(i * ROW_H));
		const bool cur = isCurrent(e);
		if (cur) { pie_BoxFill_alpha(c.X(8), ry + 1, c.X(158), ry + c.L(static_cast<float>(ROW_H)) - 1, pal_RGBA(20, 140, 190, 120)); }
		const PIELIGHT col = cur ? pal_RGBA(255, 255, 255, 255) : (s_hoverShown == i ? pal_RGBA(180, 255, 255, 255) : modui::cyan(230));
		modui::text(modui::fit(e.name, c.L(146.f), sc), c.X(11), ry + c.L(3.f), col, sc);
	}
	{
		const int first = total == 0 ? 0 : s_scroll + 1, last = std::min(total, s_scroll + ROWS);
		const std::string pg = std::to_string(first) + "-" + std::to_string(last) + " / " + std::to_string(total);
		modui::text(pg, c.X(83) - modui::textWidth(pg, sc) / 2, c.Y(283), modui::cyan(), sc);
		const bool upHl = !s_arrBtn.empty() && (s_arrBtn[0]->getState() & WBUT_HIGHLIGHT) != 0;
		const bool dnHl = s_arrBtn.size() > 1 && (s_arrBtn[1]->getState() & WBUT_HIGHLIGHT) != 0;
		modui::drawSized(upHl ? modui::ARR_UP_HL : modui::ARR_UP, c.X(6), c.Y(282), c.L(23.f), c.L(13.f));
		modui::drawSized(dnHl ? modui::ARR_DN_HL : modui::ARR_DN, c.X(137), c.Y(282), c.L(23.f), c.L(13.f));
	}
	// Armseite
	if (s_tab == TAB_ARMS)
	{
		for (int k = 0; k < 2; ++k)
		{
			const bool on = (k == 0) == (s_side > 0);
			const int x = c.X(6.f + k * 80.f), y = c.Y(300);
			const int w = c.L(74.f), h = c.L(17.f);
			pie_BoxFill_alpha(x, y, x + w, y + h, on ? pal_RGBA(20, 140, 190, 150) : pal_RGBA(4, 10, 24, 200));
			box(x, y, x + w - 1, y + h - 1, modui::cyan(on ? 255 : 150));
			const std::string t = k == 0 ? "RIGHT ARM" : "LEFT ARM";
			modui::text(t, x + (w - modui::textWidth(t, sc)) / 2, y + c.L(4.f), on ? pal_RGBA(255, 255, 255, 255) : modui::cyan(), sc);
		}
	}

	// Montagehalle
	drawBay(c);

	// Diagramm
	{
		const int px = c.X(476), py = c.Y(6);
		modui::drawSized(modui::PANEL, px, py, c.L(static_cast<float>(modui::imgWidth(modui::PANEL))), c.L(static_cast<float>(modui::imgHeight(modui::PANEL))));
		const int pct[4] = { s_target[modcombot::SLOT_TORSO] ? 100 : -1, s_target[modcombot::SLOT_RARM] ? 100 : -1,
		                     s_target[modcombot::SLOT_LARM] ? 100 : -1, s_target[modcombot::SLOT_LEGS] ? 100 : -1 };
		auto nm = [](const Part *p) { return p ? p->file : std::string(); };
		const std::string names[4] = { nm(s_target[modcombot::SLOT_TORSO]), shortArm(s_target[modcombot::SLOT_RARM]), shortArm(s_target[modcombot::SLOT_LARM]), nm(s_target[modcombot::SLOT_LEGS]) };
		modpanel::drawBlueprint(px, py, c.S, pct, names, sc);
	}

	// Werteleisten
	const Part *hoverPart = nullptr;
	if (s_hoverShown >= 0 && s_scroll + s_hoverShown < total) { hoverPart = s_list[static_cast<size_t>(s_scroll + s_hoverShown)].part; }
	if (hoverPart == nullptr)
	{
		hoverPart = s_tab == TAB_TORSO ? s_target[modcombot::SLOT_TORSO] : (s_tab == TAB_LEGS ? s_target[modcombot::SLOT_LEGS] : s_target[s_side > 0 ? modcombot::SLOT_RARM : modcombot::SLOT_LARM]);
	}
	const PartInfo pi = infoOf(hoverPart);
	drawStrip(c, 340, hoverPart != nullptr && hoverPart->kind == modcombot::KIND_ARM ? modui::STAT_B : modui::STAT_A, pi, sc);
	modui::text(hoverPart ? hoverPart->name : std::string("-"), c.X(300), c.Y(341) , pal_RGBA(255, 255, 255, 255), sc);

	PartInfo total2;
	DROID_TEMPLATE tpl;
	std::string err;
	const bool valid = makeTemplate(tpl, err);
	{
		int dmg = 0;
		for (int s : { static_cast<int>(modcombot::SLOT_RARM), static_cast<int>(modcombot::SLOT_LARM) })
		{
			if (s_target[s]) { dmg += s_target[s]->st.dmg; }
		}
		if (const Part *t = s_target[modcombot::SLOT_TORSO])
		{
			total2.line1 = std::to_string(dmg);
			total2.line2 = std::to_string(t->st.ak) + "/" + std::to_string(t->st.ah);
		}
		if (valid)
		{
			total2.mj = static_cast<int>(calcTemplatePower(&tpl));
			total2.mp = static_cast<int>(calcTemplateBuild(&tpl));
		}
		else
		{
			for (const Part *p : s_target) { const Cost k = costOf(p); total2.mj += k.power; total2.mp += k.points; }
		}
	}
	drawStrip(c, 366, modui::STAT_A, total2, sc);
	modui::text("TOTAL", c.X(300), c.Y(367), modui::cyan(), sc);

	// Kosten
	{
		const int x0 = c.X(6), y0 = c.Y(392);
		modui::drawSized(modui::RESBAR, x0, y0, c.L(static_cast<float>(modui::imgWidth(modui::RESBAR))), c.L(static_cast<float>(modui::imgHeight(modui::RESBAR))));
		const PIELIGHT white = pal_RGBA(235, 250, 255, 255);
		modui::text(std::to_string(total2.mj), x0 + c.L(158.f), y0 + c.L(17.f), white, sc);
		modui::text(std::to_string(total2.mp), x0 + c.L(346.f), y0 + c.L(17.f), white, sc);
	}

	// Faehigkeiten und Hinweise
	{
		const int x = c.X(492), y = c.Y(340), w = c.L(144.f), h = c.L(134.f);
		drawFrame(x, y, w, h);
		modui::text("ABILITIES", x + c.L(6.f), y + c.L(5.f), modui::cyan(), sc);
		std::set<std::string> abil;
		for (const Part *p : s_target) { if (p) { for (const std::string &a : p->abil) { abil.insert(a); } } }
		int line = 0;
		auto label = [](const std::string &a) {
			if (a == "jump") { return std::string("Jump jets"); }
			if (a == "fly") { return std::string("Flight"); }
			if (a == "shield") { return std::string("Shield"); }
			if (a == "cloak") { return std::string("Cloak"); }
			if (a == "repair") { return std::string("Self repair"); }
			if (a == "sonar") { return std::string("Sonar"); }
			if (a == "recon") { return std::string("Recon"); }
			if (a == "steady") { return std::string("Steady aim"); }
			if (a == "hth") { return std::string("Melee"); }
			return a;
		};
		for (const std::string &a : abil)
		{
			if (line >= 6) { break; }
			modui::text(label(a), x + c.L(8.f), y + c.L(20.f + line * 11.f), pal_RGBA(235, 250, 255, 255), sc);
			++line;
		}
		if (abil.empty()) { modui::text("none", x + c.L(8.f), y + c.L(20.f), pal_RGBA(120, 150, 175, 255), sc); }
		if (!valid) { modui::text(modui::fit(err, w - c.L(10.f), sc), x + c.L(6.f), y + h - c.L(14.f), pal_RGBA(255, 120, 90, 255), sc); }
	}

	// Tasten
	{
		const int bx = c.X(6), by = c.Y(430), bw = c.L(83.f), bh = c.L(36.f);
		modui::drawSized(modui::DISP_A, bx, by, bw, bh);
		const std::string t = "BUILD";
		modui::text(t, bx + (bw - modui::textWidth(t, sc + 1 > 2 ? 2 : sc + 1)) / 2, by + c.L(11.f), valid ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(140, 160, 180, 255), sc + 1 > 2 ? 2 : sc + 1);
		const char *labels[5] = { "MIRROR", "CLR R", "CLR L", "RESET", "EXIT" };
		const float xs[5] = { 96.f, 148.f, 200.f, 252.f, 586.f };
		for (int k = 0; k < 5; ++k)
		{
			const int x = c.X(xs[k]), y = c.Y(436.f), w = c.L(48.f), h2 = c.L(25.f);
			modui::drawSized(modui::DISP_B, x, y, w, h2);
			const std::string s = labels[k];
			modui::text(s, x + (w - modui::textWidth(s, sc)) / 2, y + c.L(8.f), pal_RGBA(230, 250, 255, 255), sc);
		}
	}

	// Meldung
	if (!s_status.empty() && now < s_statusUntil)
	{
		modui::text(modui::fit(s_status, c.L(300.f), sc), c.X(310), c.Y(446), s_statusBad ? pal_RGBA(255, 130, 100, 255) : pal_RGBA(120, 255, 170, 255), sc);
	}
	// "Prebuild Complete"
	if (s_flashStart != 0 && now - s_flashStart < 2200)
	{
		const int w = c.L(83.f * 2.6f), h = c.L(36.f * 2.6f);
		modui::drawSized(modui::PREBUILD, c.X(168 + 152) - w / 2, c.Y(6 + 165) - h / 2, w, h, now - s_flashStart > 1700 ? static_cast<unsigned char>(255 - (now - s_flashStart - 1700) / 2) : 255);
	}
}

void hotDisplay(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int x1 = x0 + psWidget->width() - 1, y1 = y0 + psWidget->height() - 1;
	if ((b->getState() & WBUT_HIGHLIGHT) != 0) { box(x0, y0, x1, y1, pal_RGBA(255, 255, 255, 170)); }
	if ((b->getState() & WBUT_DOWN) != 0) { pie_BoxFill_alpha(x0, y0, x1, y1, pal_RGBA(255, 255, 255, 50)); }
}

void rowDisplay(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	if ((b->getState() & WBUT_HIGHLIGHT) != 0) { s_hoverNow = static_cast<int>(psWidget->UserData); }
	hotDisplay(psWidget, xOffset, yOffset);
}

// ---------------------------------------------------------------- Aufbau

std::shared_ptr<W_BUTTON> makeBtn(const std::shared_ptr<WIDGET> &parent, WIDGET_DISPLAY disp, UDWORD user, const char *tip, std::function<void()> onClick)
{
	auto b = std::make_shared<W_BUTTON>();
	parent->attach(b);
	b->displayFunction = disp;
	b->UserData = user;
	if (tip != nullptr) { b->setTip(tip); }
	b->addOnClickHandler([onClick](W_BUTTON &) { widgScheduleTask(onClick); });
	return b;
}

void layout()
{
	if (!s_root) { return; }
	const Ctx c = ctx();
	auto set = [&](const std::shared_ptr<W_BUTTON> &b, float x, float y, float w, float h) { b->setGeometry(c.X(x), c.Y(y), std::max(1, c.L(w)), std::max(1, c.L(h))); };
	set(s_tabBtn[0], 6 + 4, 6, 40, 28);
	set(s_tabBtn[1], 6 + 46, 6, 34, 28);
	set(s_tabBtn[2], 6 + 80, 6, 38, 28);
	set(s_tabBtn[3], 6 + 118, 6, 34, 28);
	for (int i = 0; i < ROWS; ++i) { set(s_rowBtn[static_cast<size_t>(i)], 8, 38.f + static_cast<float>(i * ROW_H), 150, static_cast<float>(ROW_H)); }
	set(s_arrBtn[0], 6, 282, 23, 13);
	set(s_arrBtn[1], 137, 282, 23, 13);
	set(s_sideBtn[0], 6, 300, 74, 17);
	set(s_sideBtn[1], 86, 300, 74, 17);
	// Diagramm-Felder (Bildkoordinaten + (476,6))
	const float px = 476.f, py = 6.f;
	set(s_hotBtn[0], px + 17, py + 25, 19, 50);     // rechter Arm
	set(s_hotBtn[1], px + 54, py + 25, 19, 50);     // linker Arm
	set(s_hotBtn[2], px + 37, py + 38, 17, 19);     // Torso
	set(s_hotBtn[3], px + 29, py + 80, 33, 33);     // Beine
	set(s_hotBtn[4], px + 16, py + 175, 26, 24);    // Feld rechter Arm
	set(s_hotBtn[5], px + 67, py + 175, 26, 24);    // Feld Torso
	set(s_hotBtn[6], px + 118, py + 175, 26, 24);   // Feld linker Arm
	set(s_actBtn[ACT_BUILD], 6, 430, 83, 36);
	set(s_actBtn[ACT_MIRROR], 96, 436, 48, 25);
	set(s_actBtn[ACT_CLR_R], 148, 436, 48, 25);
	set(s_actBtn[ACT_CLR_L], 200, 436, 48, 25);
	set(s_actBtn[ACT_RESET], 252, 436, 48, 25);
	set(s_actBtn[ACT_EXIT], 586, 436, 48, 25);
	s_lastW = static_cast<int>(screenWidth);
	s_lastH = static_cast<int>(screenHeight);
}

void scrollBy(int rows)
{
	const int maxScroll = std::max(0, static_cast<int>(s_list.size()) - ROWS);
	s_scroll = std::max(0, std::min(s_scroll + rows, maxScroll));
}

void buildScreen()
{
	auto screen = W_SCREEN::make();
	auto root = W_FULLSCREENOVERLAY_CLICKFORM::make();
	root->backgroundColor = pal_RGBA(0, 0, 0, 0);
	root->displayFunction = drawScreen;
	root->onClickedFunc = []() {};
	root->onCancelPressed = []() { widgScheduleTask([]() { close(); }); };
	screen->psForm->attach(root);
	const std::shared_ptr<WIDGET> parent = root;

	s_tabBtn.clear(); s_rowBtn.clear(); s_arrBtn.clear(); s_sideBtn.clear(); s_hotBtn.clear(); s_actBtn.clear();
	static const char *tabTips[4] = { "Arms: weapons and melee attachments", "Legs: movement", "Torso: body, armour and hit points", "Complete combot designs" };
	for (int t = 0; t < 4; ++t) { s_tabBtn.push_back(makeBtn(parent, hotDisplay, static_cast<UDWORD>(t), tabTips[t], [t]() { setTab(t); })); }
	for (int i = 0; i < ROWS; ++i)
	{
		s_rowBtn.push_back(makeBtn(parent, rowDisplay, static_cast<UDWORD>(i), nullptr, [i]() {
			const size_t idx = static_cast<size_t>(s_scroll + i);
			if (idx < s_list.size()) { applyEntry(s_list[idx]); }
		}));
	}
	s_arrBtn.push_back(makeBtn(parent, hotDisplay, 0, "Scroll up", []() { scrollBy(-5); }));
	s_arrBtn.push_back(makeBtn(parent, hotDisplay, 1, "Scroll down", []() { scrollBy(5); }));
	s_sideBtn.push_back(makeBtn(parent, hotDisplay, 0, "Edit the right arm", []() { setSide(1); }));
	s_sideBtn.push_back(makeBtn(parent, hotDisplay, 1, "Edit the left arm", []() { setSide(-1); }));
	s_hotBtn.push_back(makeBtn(parent, hotDisplay, 0, "Right arm", []() { setSide(1); }));
	s_hotBtn.push_back(makeBtn(parent, hotDisplay, 1, "Left arm", []() { setSide(-1); }));
	s_hotBtn.push_back(makeBtn(parent, hotDisplay, 2, "Torso", []() { setTab(TAB_TORSO); }));
	s_hotBtn.push_back(makeBtn(parent, hotDisplay, 3, "Legs", []() { setTab(TAB_LEGS); }));
	s_hotBtn.push_back(makeBtn(parent, hotDisplay, 4, "Right arm", []() { setSide(1); }));
	s_hotBtn.push_back(makeBtn(parent, hotDisplay, 5, "Torso", []() { setTab(TAB_TORSO); }));
	s_hotBtn.push_back(makeBtn(parent, hotDisplay, 6, "Left arm", []() { setSide(-1); }));
	s_actBtn.push_back(makeBtn(parent, hotDisplay, ACT_BUILD, "Build this combot: it is added to your designs and can be produced in factories", []() { doBuild(); }));
	s_actBtn.push_back(makeBtn(parent, hotDisplay, ACT_MIRROR, "Copy this arm to the other side", []() { doMirror(); }));
	s_actBtn.push_back(makeBtn(parent, hotDisplay, ACT_CLR_R, "Remove the right arm", []() { doClear(modcombot::SLOT_RARM); }));
	s_actBtn.push_back(makeBtn(parent, hotDisplay, ACT_CLR_L, "Remove the left arm", []() { doClear(modcombot::SLOT_LARM); }));
	s_actBtn.push_back(makeBtn(parent, hotDisplay, ACT_RESET, "Start again with the basic combot", []() { doReset(); }));
	s_actBtn.push_back(makeBtn(parent, hotDisplay, ACT_EXIT, "Close the designer", []() { close(); }));

	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_root = root;
}

} // namespace

bool isOpen()
{
	return s_open;
}

bool open()
{
	if (s_open) { return true; }
	if (!modcombot::ready() || !modui::ready())
	{
		addConsoleMessage("The combot designer needs the Metal Fatigue data files.", DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		return false;
	}
	if (!s_screen) { buildScreen(); }
	s_open = true;
	s_tab = TAB_ARMS;
	s_side = 1;
	s_scroll = 0;
	s_status.clear();
	s_flashStart = 0;
	// Bisherige Auswahl beibehalten, sonst Grundmodell
	if (s_target[modcombot::SLOT_TORSO] == nullptr) { defaultsToTarget(); }
	rebuildList();
	resetAssembly(true);
	modpanel::setSuppressed(true);
	s_root->show(true);
	layout();
	return true;
}

void close()
{
	if (!s_open) { return; }
	s_open = false;
	if (s_screen)
	{
		widgRemoveOverlayScreen(s_screen);
		s_screen = nullptr;
		s_root = nullptr;
		s_tabBtn.clear(); s_rowBtn.clear(); s_arrBtn.clear(); s_sideBtn.clear(); s_hotBtn.clear(); s_actBtn.clear();
	}
	modpanel::setSuppressed(false);
}

void tick()
{
	if (!s_open || !s_root) { return; }
	updateAnim(realTime);
	if (static_cast<int>(screenWidth) != s_lastW || static_cast<int>(screenHeight) != s_lastH) { layout(); }
	// Mausrad ueber der Liste
	const Vector2i wheel = getMouseWheelSpeed();
	if (wheel.y != 0)
	{
		const Ctx c = ctx();
		const int mx = mouseX(), my = mouseY();
		if (mx >= c.X(6) && mx <= c.X(160) && my >= c.Y(38) && my <= c.Y(38.f + ROWS * ROW_H)) { scrollBy(wheel.y > 0 ? -3 : 3); }
	}
}

void shutdown()
{
	s_open = false;
	if (s_screen)
	{
		widgRemoveOverlayScreen(s_screen);
		s_screen = nullptr;
	}
	s_root = nullptr;
	s_tabBtn.clear(); s_rowBtn.clear(); s_arrBtn.clear(); s_sideBtn.clear(); s_hotBtn.clear(); s_actBtn.clear();
	for (int i = 0; i < modcombot::SLOT_COUNT; ++i) { s_target[i] = nullptr; s_shown[i] = nullptr; s_anim[i] = SlotAnim(); }
	s_list.clear();
}

// ---------------------------------------------------------------- Einstiegstaste im Design-Bildschirm

void displayEntryButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	const int lift = hl ? 28 : 0;
	// Farbverlauf im WZ-Blau
	for (int i = 0; i < h; ++i)
	{
		const float t = h > 1 ? static_cast<float>(i) / static_cast<float>(h - 1) : 0.f;
		const int r = static_cast<int>(8 + 6 * t) + lift / 3;
		const int g = static_cast<int>(40 + 52 * t) + lift;
		const int bl = static_cast<int>(112 + 84 * t) + lift;
		pie_BoxFill(x0, y0 + i, x0 + w, y0 + i + 1, pal_RGBA(static_cast<uint8_t>(std::min(255, r)), static_cast<uint8_t>(std::min(255, g)), static_cast<uint8_t>(std::min(255, bl)), 255));
	}
	// Rand: hell oben/links, dunkel unten/rechts (bei gedrueckter Taste umgekehrt)
	const PIELIGHT light = pal_RGBA(150, 215, 255, 255), dark = pal_RGBA(0, 18, 56, 255);
	iV_Line(x0, y0, x0 + w - 1, y0, down ? dark : light);
	iV_Line(x0, y0, x0, y0 + h - 1, down ? dark : light);
	iV_Line(x0 + w - 1, y0, x0 + w - 1, y0 + h - 1, down ? light : dark);
	iV_Line(x0, y0 + h - 1, x0 + w - 1, y0 + h - 1, down ? light : dark);
	if (hl) { iV_Box(x0 + 1, y0 + 1, x0 + w - 2, y0 + h - 2, pal_RGBA(40, 235, 255, 230)); }

	// weisse Combot-Silhouette auf einem 24 x 40 Raster
	struct R { int x0, y0, x1, y1; };
	static const R body[] = {
		{ 10, 1, 14, 5 },    // Kopf
		{ 11, 5, 13, 6 },    // Hals
		{ 5, 6, 19, 9 },     // Schultern
		{ 7, 9, 17, 17 },    // Brust
		{ 8, 17, 16, 22 },   // Bauch
		{ 8, 22, 16, 25 },   // Becken
		{ 3, 8, 6, 19 },     // rechter Arm
		{ 18, 8, 21, 19 },   // linker Arm
		{ 2, 19, 6, 27 },    // rechte Waffe
		{ 18, 19, 22, 27 },  // linke Waffe
		{ 8, 25, 11, 34 },   // rechtes Bein
		{ 13, 25, 16, 34 },  // linkes Bein
		{ 7, 34, 11, 38 },   // rechter Fuss
		{ 13, 34, 17, 38 },  // linker Fuss
	};
	const float k = std::min(static_cast<float>(w) / 27.f, static_cast<float>(h - 14) / 40.f);
	const int ox = x0 + static_cast<int>((w - 24 * k) / 2.f), oy = y0 + 4 + (down ? 1 : 0);
	const PIELIGHT white = pal_RGBA(255, 255, 255, hl ? 255 : 235);
	for (const R &r : body)
	{
		pie_BoxFill(ox + static_cast<int>(r.x0 * k), oy + static_cast<int>(r.y0 * k), ox + static_cast<int>(r.x1 * k), oy + static_cast<int>(r.y1 * k), white);
	}
	// Sichtband im Kopf
	pie_BoxFill(ox + static_cast<int>(10.8f * k), oy + static_cast<int>(2.6f * k), ox + static_cast<int>(13.2f * k), oy + static_cast<int>(3.6f * k), pal_RGBA(8, 40, 112, 255));
	if (modui::ready())
	{
		const std::string t = "COMBOT";
		modui::text(t, x0 + (w - modui::textWidth(t)) / 2, y0 + h - 12, pal_RGBA(235, 250, 255, 255));
	}
}

}
