// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 01:35): Rumpf-Baukasten (siehe modhullforge.h)
//
// Aufbau (virtuelles 640x480-Bild, auf den Bildschirm skaliert):
//   links   Rumpfliste (Klick = als neues Teil anbauen)
//   Mitte   3D-Ansicht des Verbundrumpfes (ziehen = drehen, Mausrad = Zoom); gewaehltes Teil mit Rahmen, Waffenplaetze als Marken
//   rechts  Teile-Liste (Teil 1 = Fahrgestell) und Waffenplaetze
//   unten   Schrittweiten, Verschieben/Drehen, Teil-Befehle, Name, Werte, Neu/Laden/Speichern/Loeschen/Schliessen
// Tasten (nicht beim Schreiben des Namens): Pfeile = x / vor-zurueck, Bild hoch/runter = Hoehe, Q/E = Gieren, R/F = Nicken,
// Z/C = Rollen, Umschalt = feinste Stufe, Tab = naechstes Teil, Entf = Teil entfernen.

#include "modhullforge.h"
#include "modcompbody.h"
#include "modpanel.h"
#include "modlayer.h"
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
#include "lib/widget/editbox.h"
#include "lib/widget/form.h"
#include "lib/widget/widget.h"

#include "basedef.h"
#include "component.h"
#include "console.h"
#include "hci.h"
#include "design.h"
#include "stats.h"
#include "statsdef.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace modhullforge
{

namespace
{

using modcompbody::Def;
using modcompbody::Mount;
using modcompbody::Part;

constexpr int BODY_ROWS = 22;
constexpr float BODY_ROW_H = 14.f;
constexpr int PART_ROWS = 11;
constexpr int MOUNT_ROWS = 3;
constexpr float LIST_ROW_H = 13.f;
constexpr float BAY_DEPTH = 1200.f;
const float STEPS[3] = { 0.5f, 2.f, 8.f };
const float ANGLES[3] = { 5.f, 15.f, 90.f };

struct Ctx
{
	float S = 1.f;
	int ox = 0, oy = 0;
	int X(float v) const { return ox + static_cast<int>(std::lround(v * S)); }
	int Y(float v) const { return oy + static_cast<int>(std::lround(v * S)); }
	int L(float v) const { return static_cast<int>(std::lround(v * S)); }
};

Ctx ctx()
{
	Ctx c;
	const float sx = static_cast<float>(screenWidth) / 640.f, sy = static_cast<float>(screenHeight) / 480.f;
	c.S = std::max(0.5f, std::floor(std::min(sx, sy) * 4.f) / 4.f);
	c.ox = (static_cast<int>(screenWidth) - static_cast<int>(std::lround(640.f * c.S))) / 2;
	c.oy = (static_cast<int>(screenHeight) - static_cast<int>(std::lround(480.f * c.S))) / 2;
	return c;
}

// ---------------------------------------------------------------- Zustand
bool s_open = false;
std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_FULLSCREENOVERLAY_CLICKFORM> s_root;
std::shared_ptr<W_EDITBOX> s_nameBox;

struct Btn
{
	std::shared_ptr<W_BUTTON> w;
	float x = 0, y = 0, wdt = 0, hgt = 0;
	std::string label;
	std::function<bool()> active;   // hervorgehoben (z.B. gewaehlte Schrittweite)
};
std::vector<Btn> s_btns;

Def s_def;
int s_sel = -1;                 // gewaehltes Teil
int s_selMount = -1;            // gewaehlter Waffenplatz (>= 0: Verschieben wirkt auf ihn)
int s_step = 1, s_angle = 1;
float s_yaw = 215.f, s_pitch = 24.f, s_zoom = 1.f;
bool s_autoRot = false;
std::vector<size_t> s_bodies;   // Rumpfe fuer die Liste (Index in asBodyStats)
int s_bodyScroll = 0;
int s_partScroll = 0;
int s_loadIdx = -1;
std::string s_status;
bool s_statusBad = false;
unsigned s_statusUntil = 0;
bool s_dragging = false;
int s_dragX = 0, s_dragY = 0;
bool s_dirty = false;
unsigned s_availTick = 0;
int s_lastW = 0, s_lastH = 0;

void play(const char *file)
{
	const int id = audio_GetTrackID(file);
	if (id >= 0) { audio_PlayTrack(id); }
}

void setStatus(const std::string &t, bool bad = false)
{
	s_status = t;
	s_statusBad = bad;
	s_statusUntil = realTime + 7000;
}

const char *sizeName(int s)
{
	switch (s)
	{
	case SIZE_LIGHT: return "Light";
	case SIZE_MEDIUM: return "Medium";
	case SIZE_HEAVY: return "Heavy";
	default: return "Super heavy";
	}
}

std::string bodyName(const std::string &id)
{
	const BODY_STATS *b = modcompbody::bodyById(id);
	if (b == nullptr) { return id; }
	std::string n = getLocalizedStatsName(b);
	return n.empty() ? id : n;
}

void rebuildBodyList()
{
	s_bodies.clear();
	const bool havePlayer = selectedPlayer < MAX_PLAYERS && apCompLists[selectedPlayer][COMP_BODY] != nullptr;
	for (size_t i = 0; i < asBodyStats.size(); ++i)
	{
		const BODY_STATS &b = asBodyStats[i];
		if (!b.designable || b.pIMD == nullptr || checkIfZNullStat(&b) || modcompbody::isComposite(i)) { continue; }
		if (havePlayer && apCompLists[selectedPlayer][COMP_BODY][i] != AVAILABLE) { continue; }
		// Rumpfe mit demselben Modell nur einmal
		bool dup = false;
		for (size_t j : s_bodies) { if (asBodyStats[j].pIMD == b.pIMD) { dup = true; break; } }
		if (!dup) { s_bodies.push_back(i); }
	}
	std::sort(s_bodies.begin(), s_bodies.end(), [](size_t a, size_t b) {
		return std::string(getLocalizedStatsName(&asBodyStats[a])) < std::string(getLocalizedStatsName(&asBodyStats[b]));
	});
	s_bodyScroll = std::min(s_bodyScroll, std::max(0, static_cast<int>(s_bodies.size()) - BODY_ROWS));
}

// ---------------------------------------------------------------- Geometrie-Hilfen
/// Huelle eines Teils (Eckpunkte der Modell-Grenzen, in Verbund-Koordinaten)
bool partCorners(const Part &p, glm::vec3 out[8])
{
	const BODY_STATS *b = modcompbody::bodyById(p.body);
	if (b == nullptr || b->pIMD == nullptr) { return false; }
	const glm::mat4 m = modcompbody::partMatrix(p);
	const Vector3i mn = b->pIMD->min, mx = b->pIMD->max;
	for (int k = 0; k < 8; ++k)
	{
		const glm::vec4 q = m * glm::vec4((k & 1) ? mx.x : mn.x, (k & 2) ? mx.y : mn.y, (k & 4) ? mx.z : mn.z, 1.f);
		out[k] = glm::vec3(q);
	}
	return true;
}

float compositeExtent()
{
	float ext = 20.f;
	for (const Part &p : s_def.parts)
	{
		glm::vec3 c[8];
		if (!partCorners(p, c)) { continue; }
		for (const glm::vec3 &v : c) { ext = std::max({ ext, std::fabs(v.x), std::fabs(v.y - 10.f), std::fabs(v.z) }); }
	}
	for (const Mount &m : s_def.mounts) { ext = std::max({ ext, std::fabs(m.pos.x), std::fabs(m.pos.z) }); }
	return ext;
}

float topOfComposite()
{
	float top = 0.f;
	bool any = false;
	for (const Part &p : s_def.parts)
	{
		glm::vec3 c[8];
		if (!partCorners(p, c)) { continue; }
		for (const glm::vec3 &v : c) { top = any ? std::max(top, v.y) : v.y; any = true; }
	}
	return top;
}

// ---------------------------------------------------------------- Befehle
void selectPart(int i)
{
	if (s_def.parts.empty()) { s_sel = -1; return; }
	s_sel = std::clamp(i, 0, static_cast<int>(s_def.parts.size()) - 1);
	s_selMount = -1;
	if (s_sel < s_partScroll) { s_partScroll = s_sel; }
	if (s_sel >= s_partScroll + PART_ROWS) { s_partScroll = s_sel - PART_ROWS + 1; }
}

void addPart(size_t bodyIdx)
{
	if (bodyIdx >= asBodyStats.size()) { return; }
	Part p;
	p.body = asBodyStats[bodyIdx].id.toStdString();
	if (!s_def.parts.empty())
	{
		// oben auf den bisherigen Aufbau setzen (danach frei verschieben)
		const BODY_STATS &b = asBodyStats[bodyIdx];
		const float bottom = b.pIMD != nullptr ? static_cast<float>(b.pIMD->min.y) : 0.f;
		p.pos = glm::vec3(0.f, std::round((topOfComposite() - bottom) * 2.f) / 2.f, 0.f);
	}
	s_def.parts.push_back(p);
	if (s_def.parts.size() == 1 && s_def.mounts.empty()) { s_def.mounts = modcompbody::defaultMounts(p); }
	selectPart(static_cast<int>(s_def.parts.size()) - 1);
	s_dirty = true;
	play("mfui_clank.ogg");
	setStatus(s_def.parts.size() == 1 ? "Chassis set: " + bodyName(p.body) + " (its propulsion is used)" : "Added: " + bodyName(p.body));
}

void move(int axis, float sign)
{
	const bool fine = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	const float d = (fine ? STEPS[0] : STEPS[s_step]) * sign;
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		s_def.mounts[static_cast<size_t>(s_selMount)].pos[axis] += d;
	}
	else if (s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size()))
	{
		s_def.parts[static_cast<size_t>(s_sel)].pos[axis] += d;
	}
	else { return; }
	s_dirty = true;
}

void rotate(int axis, float sign)
{
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size()) || s_selMount >= 0) { return; }
	const bool fine = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	float &a = s_def.parts[static_cast<size_t>(s_sel)].rot[axis];
	a = std::fmod(a + (fine ? 1.f : ANGLES[s_angle]) * sign + 720.f, 360.f);
	if (a > 180.f) { a -= 360.f; }
	s_dirty = true;
}

void duplicatePart(bool mirror)
{
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		if (s_def.mounts.size() >= modcompbody::MAX_SLOTS) { setStatus("At most " + std::to_string(modcompbody::MAX_SLOTS) + " weapon slots (the designer offers three weapons).", true); return; }
		Mount m = s_def.mounts[static_cast<size_t>(s_selMount)];
		if (mirror) { m.pos.x = -m.pos.x; } else { m.pos.x += STEPS[s_step]; }
		s_def.mounts.push_back(m);
		s_selMount = static_cast<int>(s_def.mounts.size()) - 1;
		s_dirty = true;
		return;
	}
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size())) { return; }
	Part p = s_def.parts[static_cast<size_t>(s_sel)];
	if (mirror) { p.pos.x = -p.pos.x; p.rot.x = -p.rot.x; p.rot.z = -p.rot.z; }
	else { p.pos.x += 8.f; }
	s_def.parts.push_back(p);
	selectPart(static_cast<int>(s_def.parts.size()) - 1);
	s_dirty = true;
	play("mfui_clank.ogg");
}

void deleteSelected()
{
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		s_def.mounts.erase(s_def.mounts.begin() + s_selMount);
		s_selMount = s_def.mounts.empty() ? -1 : std::min(s_selMount, static_cast<int>(s_def.mounts.size()) - 1);
		s_dirty = true;
		return;
	}
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size())) { return; }
	s_def.parts.erase(s_def.parts.begin() + s_sel);
	if (s_sel == 0 && !s_def.parts.empty()) { setStatus("New chassis: " + bodyName(s_def.parts[0].body)); }
	selectPart(std::min(s_sel, static_cast<int>(s_def.parts.size()) - 1));
	s_dirty = true;
	play("mfui_rattle.ogg");
}

void resetSelected()
{
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size())) { s_def.mounts[static_cast<size_t>(s_selMount)].pos = glm::vec3(0.f, 20.f, 0.f); s_dirty = true; return; }
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size())) { return; }
	Part &p = s_def.parts[static_cast<size_t>(s_sel)];
	p.rot = glm::vec3(0.f);
	if (s_sel == 0) { p.pos = glm::vec3(0.f); }
	s_dirty = true;
}

void addMount()
{
	if (s_def.mounts.size() >= modcompbody::MAX_SLOTS) { setStatus("At most " + std::to_string(modcompbody::MAX_SLOTS) + " weapon slots (the designer offers three weapons).", true); return; }
	Mount m;
	m.pos = glm::vec3(0.f, std::round(topOfComposite()), 0.f);
	s_def.mounts.push_back(m);
	s_selMount = static_cast<int>(s_def.mounts.size()) - 1;
	s_dirty = true;
	setStatus("Weapon slot added - move it with the arrow buttons.");
}

void newHull()
{
	s_def = Def();
	s_def.name = "New hull";
	s_sel = -1; s_selMount = -1; s_partScroll = 0; s_loadIdx = -1;
	if (s_nameBox) { s_nameBox->setString(WzString::fromUtf8(s_def.name)); }
	s_dirty = false;
	setStatus("Pick the chassis from the hull list on the left (its propulsion is used), then add more hulls.");
}

void loadCycle(int dir)
{
	std::vector<int> idx;
	const auto &all = modcompbody::defs();
	for (size_t i = 0; i < all.size(); ++i) { if (!all[i].deleted) { idx.push_back(static_cast<int>(i)); } }
	if (idx.empty()) { setStatus("No saved super hulls yet.", true); return; }
	int pos = 0;
	for (size_t k = 0; k < idx.size(); ++k) { if (idx[k] == s_loadIdx) { pos = static_cast<int>(k); break; } }
	if (s_loadIdx >= 0) { pos = (pos + dir + static_cast<int>(idx.size())) % static_cast<int>(idx.size()); }
	s_loadIdx = idx[static_cast<size_t>(pos)];
	s_def = all[static_cast<size_t>(s_loadIdx)];
	s_sel = s_def.parts.empty() ? -1 : 0; s_selMount = -1; s_partScroll = 0;
	if (s_nameBox) { s_nameBox->setString(WzString::fromUtf8(s_def.name)); }
	s_dirty = false;
	setStatus(std::string("Loaded: ") + s_def.name + (s_def.preset ? " (preset)" : ""));
}

void doSave()
{
	if (s_nameBox) { s_def.name = s_nameBox->getString().toStdString(); }
	std::string err;
	const bool ok = modcompbody::save(s_def, err);
	if (!err.empty()) { setStatus(err, !ok); }
	else { setStatus(ok ? "Saved: \"" + s_def.name + "\" - now available in the hull list (campaign, skirmish and fleet editor)." : "Could not save.", !ok); }
	if (ok) { s_dirty = false; play("mfui_clank.ogg"); }
	const auto &all = modcompbody::defs();
	for (size_t i = 0; i < all.size(); ++i) { if (all[i].id == s_def.id) { s_loadIdx = static_cast<int>(i); break; } }
}

void doDeleteHull()
{
	if (s_def.id.empty()) { newHull(); return; }
	std::string err;
	if (modcompbody::remove(s_def.id, err)) { setStatus("Deleted: " + s_def.name); newHull(); }
	else { setStatus(err, true); }
}

// ---------------------------------------------------------------- Zeichnen
void drawFrame(int x, int y, int w, int h)
{
	pie_UniTransBoxFill(x, y, x + w, y + h, pal_RGBA(4, 9, 20, 232));
	iV_Box(x, y, x + w - 1, y + h - 1, pal_RGBA(46, 84, 112, 255));
}

glm::mat4 bayBase(const Ctx &c, float &scaleOut)
{
	const float ext = compositeExtent();
	const float sc = 155.f * c.S / std::max(10.f, ext) * s_zoom;
	scaleOut = sc;
	return glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, BAY_DEPTH))
	       * glm::rotate(glm::mat4(1.f), glm::radians(-s_pitch), glm::vec3(1.f, 0.f, 0.f))
	       * glm::rotate(glm::mat4(1.f), glm::radians(s_yaw), glm::vec3(0.f, 1.f, 0.f))
	       * glm::scale(glm::mat4(1.f), glm::vec3(sc))
	       * glm::translate(glm::mat4(1.f), glm::vec3(0.f, -12.f, 0.f));
}

bool project(const glm::mat4 &base, const glm::vec3 &p, Vector2i &out)
{
	const glm::mat4 m = pie_UIPerspectiveGet() * base * glm::translate(glm::mat4(1.f), p);
	const Vector3i zero(0, 0, 0);
	return pie_RotateProjectWithPerspective(&zero, m, &out) > 0 && out.x != LONG_WAY;
}

void drawBay(const Ctx &c)
{
	const int bx = c.X(166), by = c.Y(6), bw = c.L(308), bh = c.L(330);
	drawFrame(bx, by, bw, bh);
	// Bodenraster
	const int team = static_cast<int>(getPlayerColour(selectedPlayer));
	pie_SetGeometricOffset(bx + bw / 2, by + bh / 2 + c.L(30));
	float sc = 1.f;
	const glm::mat4 base = bayBase(c, sc);
	const glm::mat4 view(1.f);
	{
		const float g = std::ceil(compositeExtent() / 16.f) * 16.f;
		for (float t = -g; t <= g + 0.1f; t += 16.f)
		{
			Vector2i a, b2;
			if (project(base, glm::vec3(t, 0.f, -g), a) && project(base, glm::vec3(t, 0.f, g), b2)) { iV_Line(a.x, a.y, b2.x, b2.y, pal_RGBA(40, 120, 170, 70)); }
			if (project(base, glm::vec3(-g, 0.f, t), a) && project(base, glm::vec3(g, 0.f, t), b2)) { iV_Line(a.x, a.y, b2.x, b2.y, pal_RGBA(40, 120, 170, 70)); }
		}
		// Pfeil "vorn" (-z)
		Vector2i a, b2;
		if (project(base, glm::vec3(0.f, 0.f, -g * 0.6f), a) && project(base, glm::vec3(0.f, 0.f, -g - 10.f), b2)) { iV_Line(a.x, a.y, b2.x, b2.y, pal_RGBA(120, 255, 170, 200)); }
	}
	for (size_t i = 0; i < s_def.parts.size(); ++i)
	{
		const Part &p = s_def.parts[i];
		const BODY_STATS *b = modcompbody::bodyById(p.body);
		if (b == nullptr || b->pIMD == nullptr || b->pIMD->displayModel() == nullptr) { continue; }
		pie_Draw3DShape(b->pIMD->displayModel(), 0, team, WZCOL_WHITE, pie_BUTTON, 0, base * modcompbody::partMatrix(p), view);
	}
	// gewaehltes Teil: Rahmen
	if (s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size()) && s_selMount < 0)
	{
		glm::vec3 cr[8];
		if (partCorners(s_def.parts[static_cast<size_t>(s_sel)], cr))
		{
			const unsigned char a = static_cast<unsigned char>(170 + 80 * std::sin(static_cast<float>(realTime) * 0.008f));
			const PIELIGHT col = pal_RGBA(255, 220, 90, a);
			static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
			Vector2i sp[8];
			bool ok = true;
			for (int k = 0; k < 8; ++k) { ok = ok && project(base, cr[k], sp[k]); }
			if (ok) { for (const auto &e : edges) { iV_Line(sp[e[0]].x, sp[e[0]].y, sp[e[1]].x, sp[e[1]].y, col); } }
		}
	}
	// Waffenplaetze
	for (size_t i = 0; i < s_def.mounts.size(); ++i)
	{
		Vector2i sp;
		if (!project(base, s_def.mounts[i].pos, sp)) { continue; }
		const bool sel = static_cast<int>(i) == s_selMount;
		const int r = sel ? 6 : 4;
		pie_BoxFill(sp.x - r, sp.y - r, sp.x + r, sp.y + r, sel ? pal_RGBA(255, 80, 60, 255) : pal_RGBA(255, 150, 60, 230));
		iV_Box(sp.x - r - 1, sp.y - r - 1, sp.x + r + 1, sp.y + r + 1, pal_RGBA(20, 20, 20, 255));
		modui::text(std::to_string(i + 1), sp.x + r + 3, sp.y - r - 2, pal_RGBA(255, 230, 200, 255), 0);
	}
	pie_SetGeometricOffset(static_cast<int>(screenWidth) / 2, static_cast<int>(screenHeight) / 2);
	// Hinweis
	const int sc0 = modui::sizeClassFor(c.S);
	modui::text("Drag = turn view, wheel = zoom", bx + c.L(6), by + bh - c.L(14), pal_RGBA(140, 190, 220, 200), sc0);
	if (s_def.parts.empty()) { modui::text("Pick a chassis from the list", bx + c.L(80), by + c.L(150), modui::cyan(230), sc0); }
}

void drawScreen(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	(void)psWidget; (void)xOffset; (void)yOffset;
	if (!modui::ready()) { return; }
	const Ctx c = ctx();
	const int sc = modui::sizeClassFor(c.S);
	pie_BoxFill(0, 0, static_cast<int>(screenWidth), static_cast<int>(screenHeight), pal_RGBA(6, 10, 20, 255));
	const int mw = c.L(320.f), mh = c.L(480.f);
	modui::drawSized(modui::METALPANEL, c.ox, c.oy, mw, mh);
	modui::drawSized(modui::METALPANEL, c.ox + mw, c.oy, mw, mh);
	pie_UniTransBoxFill(c.ox, c.oy, c.ox + 2 * mw, c.oy + mh, pal_RGBA(2, 5, 12, 160));

	// Rumpfliste
	modui::text("HULLS (click = add)", c.X(8), c.Y(7), pal_RGBA(235, 250, 255, 255), sc);
	drawFrame(c.X(6), c.Y(20), c.L(154), c.L(BODY_ROWS * BODY_ROW_H + 2));
	for (int i = 0; i < BODY_ROWS; ++i)
	{
		const int idx = s_bodyScroll + i;
		if (idx >= static_cast<int>(s_bodies.size())) { break; }
		const BODY_STATS &b = asBodyStats[s_bodies[static_cast<size_t>(idx)]];
		const int ry = c.Y(21.f + i * BODY_ROW_H);
		modui::text(modui::fit(getLocalizedStatsName(&b), c.L(146), sc), c.X(10), ry + c.L(2), modui::cyan(230), sc);
	}
	// Teile-Liste
	modui::text("PARTS (1 = chassis)", c.X(482), c.Y(7), pal_RGBA(235, 250, 255, 255), sc);
	drawFrame(c.X(480), c.Y(20), c.L(154), c.L(PART_ROWS * LIST_ROW_H + 2));
	for (int i = 0; i < PART_ROWS; ++i)
	{
		const int idx = s_partScroll + i;
		if (idx >= static_cast<int>(s_def.parts.size())) { break; }
		const int ry = c.Y(21.f + i * LIST_ROW_H);
		const bool sel = idx == s_sel && s_selMount < 0;
		if (sel) { pie_UniTransBoxFill(c.X(481), ry, c.X(633), ry + c.L(LIST_ROW_H) - 1, pal_RGBA(20, 140, 190, 130)); }
		const std::string t = std::to_string(idx + 1) + " " + bodyName(s_def.parts[static_cast<size_t>(idx)].body);
		modui::text(modui::fit(t, c.L(146), sc), c.X(484), ry + c.L(1), sel ? pal_RGBA(255, 255, 255, 255) : modui::cyan(230), sc);
	}
	// Waffenplaetze
	modui::text("WEAPON SLOTS", c.X(482), c.Y(170), pal_RGBA(235, 250, 255, 255), sc);
	drawFrame(c.X(480), c.Y(183), c.L(154), c.L(MOUNT_ROWS * LIST_ROW_H + 2));
	for (int i = 0; i < MOUNT_ROWS && i < static_cast<int>(s_def.mounts.size()); ++i)
	{
		const int ry = c.Y(184.f + i * LIST_ROW_H);
		const bool sel = i == s_selMount;
		if (sel) { pie_UniTransBoxFill(c.X(481), ry, c.X(633), ry + c.L(LIST_ROW_H) - 1, pal_RGBA(190, 80, 40, 140)); }
		const glm::vec3 &p = s_def.mounts[static_cast<size_t>(i)].pos;
		char buf[64];
		snprintf(buf, sizeof(buf), "Slot %d  (%.0f, %.0f, %.0f)", i + 1, p.x, p.y, p.z);
		modui::text(modui::fit(buf, c.L(146), sc), c.X(484), ry + c.L(1), sel ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(255, 200, 150, 230), sc);
	}

	drawBay(c);

	// gewaehltes Element: Lage
	{
		std::string t;
		char buf[160];
		if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
		{
			const glm::vec3 &p = s_def.mounts[static_cast<size_t>(s_selMount)].pos;
			snprintf(buf, sizeof(buf), "Weapon slot %d   x %.1f  y %.1f  z %.1f", s_selMount + 1, p.x, p.y, p.z);
			t = buf;
		}
		else if (s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size()))
		{
			const Part &p = s_def.parts[static_cast<size_t>(s_sel)];
			snprintf(buf, sizeof(buf), "%s   x %.1f  y %.1f  z %.1f   yaw %.0f  pitch %.0f  roll %.0f", bodyName(p.body).c_str(), p.pos.x, p.pos.y, p.pos.z, p.rot.x, p.rot.y, p.rot.z);
			t = buf;
		}
		modui::text(modui::fit(t, c.L(468), sc), c.X(166), c.Y(338), pal_RGBA(255, 230, 140, 255), sc);
	}
	// Beschriftungen der Knopfreihen
	modui::text("Step", c.X(8), c.Y(357), pal_RGBA(200, 220, 235, 255), sc);
	modui::text("Angle", c.X(176), c.Y(357), pal_RGBA(200, 220, 235, 255), sc);
	modui::text("View", c.X(372), c.Y(357), pal_RGBA(200, 220, 235, 255), sc);
	// Werte
	{
		const modcompbody::Totals t = modcompbody::totals(s_def);
		char buf[200];
		snprintf(buf, sizeof(buf), "HP %u   Armour %u/%u   Weight %u   Engine %u", t.hp, t.armour, t.thermal, t.weight, t.power);
		modui::text(buf, c.X(330), c.Y(400), pal_RGBA(235, 250, 255, 255), sc);
		snprintf(buf, sizeof(buf), "Cost %u   Build %u   %s   Slots %u", t.cost, t.buildPoints, sizeName(t.size), t.slots);
		modui::text(buf, c.X(330), c.Y(414), pal_RGBA(235, 250, 255, 255), sc);
	}
	if (s_dirty) { modui::text("unsaved changes", c.X(522), c.Y(432), pal_RGBA(255, 190, 90, 255), sc); }
	if (!s_status.empty() && realTime < s_statusUntil)
	{
		modui::text(modui::fit(s_status, c.L(628), sc), c.X(6), c.Y(460), s_statusBad ? pal_RGBA(255, 130, 100, 255) : pal_RGBA(120, 255, 170, 255), sc);
	}
}

void btnDisplay(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const size_t k = psWidget->UserData;
	if (k >= s_btns.size()) { return; }
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const bool active = s_btns[k].active ? s_btns[k].active() : false;
	modui::wzButton(x0, y0, psWidget->width(), psWidget->height(), s_btns[k].label, active, (b->getState() & WBUT_HIGHLIGHT) != 0, (b->getState() & WBUT_DOWN) != 0, modui::sizeClassFor(ctx().S));
}

void hotDisplay(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int x1 = x0 + psWidget->width() - 1, y1 = y0 + psWidget->height() - 1;
	if ((b->getState() & WBUT_HIGHLIGHT) != 0) { iV_Box(x0, y0, x1, y1, pal_RGBA(255, 255, 255, 150)); }
	if ((b->getState() & WBUT_DOWN) != 0) { pie_UniTransBoxFill(x0, y0, x1, y1, pal_RGBA(255, 255, 255, 50)); }
}

// ---------------------------------------------------------------- Aufbau
void addBtn(const std::shared_ptr<WIDGET> &parent, float x, float y, float w, float h, const std::string &label, const char *tip, std::function<void()> onClick,
            std::function<bool()> active = nullptr, bool invisible = false)
{
	Btn b;
	b.x = x; b.y = y; b.wdt = w; b.hgt = h; b.label = label; b.active = std::move(active);
	b.w = std::make_shared<W_BUTTON>();
	parent->attach(b.w);
	b.w->displayFunction = invisible ? hotDisplay : btnDisplay;
	b.w->UserData = static_cast<UDWORD>(s_btns.size());
	if (tip != nullptr) { b.w->setTip(tip); }
	b.w->addOnClickHandler([onClick](W_BUTTON &) { widgScheduleTask(onClick); });
	s_btns.push_back(b);
}

void layout()
{
	if (!s_root) { return; }
	const Ctx c = ctx();
	for (Btn &b : s_btns) { b.w->setGeometry(c.X(b.x), c.Y(b.y), std::max(1, c.L(b.wdt)), std::max(1, c.L(b.hgt))); }
	if (s_nameBox) { s_nameBox->setGeometry(c.X(6), c.Y(432), c.L(160), c.L(18)); }
	s_lastW = static_cast<int>(screenWidth);
	s_lastH = static_cast<int>(screenHeight);
}

void buildScreen()
{
	auto screen = W_SCREEN::make();
	screen->psForm->hide();   // verborgene Wurzel = durchlaessig fuer Klicks (wie beim Combot-Designer)
	auto root = W_FULLSCREENOVERLAY_CLICKFORM::make();
	root->backgroundColor = pal_RGBA(0, 0, 0, 0);
	root->displayFunction = drawScreen;
	root->onClickedFunc = []() {};
	root->onCancelPressed = []() { widgScheduleTask([]() { close(); }); };
	screen->psForm->attach(root);
	const std::shared_ptr<WIDGET> parent = root;
	s_btns.clear();

	// Rumpfliste (unsichtbare Zeilenknoepfe)
	for (int i = 0; i < BODY_ROWS; ++i)
	{
		addBtn(parent, 7, 21.f + i * BODY_ROW_H, 152, BODY_ROW_H, "", "Add this hull as a new part (the first part is the chassis and brings the propulsion)", [i]() {
			const size_t idx = static_cast<size_t>(s_bodyScroll + i);
			if (idx < s_bodies.size()) { addPart(s_bodies[idx]); }
		}, nullptr, true);
	}
	addBtn(parent, 6, 332, 75, 14, "Up", "Scroll the hull list up", []() { s_bodyScroll = std::max(0, s_bodyScroll - 10); });
	addBtn(parent, 85, 332, 75, 14, "Down", "Scroll the hull list down", []() { s_bodyScroll = std::min(std::max(0, static_cast<int>(s_bodies.size()) - BODY_ROWS), s_bodyScroll + 10); });
	// Teile- und Waffenplatz-Zeilen
	for (int i = 0; i < PART_ROWS; ++i)
	{
		addBtn(parent, 481, 21.f + i * LIST_ROW_H, 152, LIST_ROW_H, "", "Select this part", [i]() { selectPart(s_partScroll + i); }, nullptr, true);
	}
	for (int i = 0; i < MOUNT_ROWS; ++i)
	{
		addBtn(parent, 481, 184.f + i * LIST_ROW_H, 152, LIST_ROW_H, "", "Select this weapon slot (the move buttons then move the slot)", [i]() {
			if (i < static_cast<int>(s_def.mounts.size())) { s_selMount = i; }
		}, nullptr, true);
	}
	addBtn(parent, 480, 226, 75, 16, "+ Slot", "Add a weapon slot", []() { addMount(); });
	addBtn(parent, 559, 226, 75, 16, "- Slot", "Remove the selected weapon slot", []() {
		if (s_selMount >= 0) { deleteSelected(); } else if (!s_def.mounts.empty()) { s_def.mounts.pop_back(); s_dirty = true; }
	});
	addBtn(parent, 480, 246, 154, 16, "Edit parts", "Back to editing the selected part", []() { s_selMount = -1; if (s_sel < 0 && !s_def.parts.empty()) { s_sel = 0; } });

	// Schrittweiten
	static const char *stepLbl[3] = { "0.5", "2", "8" };
	for (int k = 0; k < 3; ++k) { addBtn(parent, 40.f + k * 42.f, 352, 40, 17, stepLbl[k], "Move step (Shift = finest step)", [k]() { s_step = k; }, [k]() { return s_step == k; }); }
	static const char *angLbl[3] = { "5", "15", "90" };
	for (int k = 0; k < 3; ++k) { addBtn(parent, 212.f + k * 50.f, 352, 48, 17, std::string(angLbl[k]) + " deg", "Rotation step (Shift = 1 degree)", [k]() { s_angle = k; }, [k]() { return s_angle == k; }); }
	addBtn(parent, 404, 352, 34, 17, "<", "Turn the view", []() { s_yaw -= 30.f; });
	addBtn(parent, 440, 352, 34, 17, ">", "Turn the view", []() { s_yaw += 30.f; });
	addBtn(parent, 476, 352, 46, 17, "Auto", "Turn the view automatically", []() { s_autoRot = !s_autoRot; }, []() { return s_autoRot; });
	addBtn(parent, 524, 352, 54, 17, "Front", "View from the front", []() { s_yaw = 180.f; s_pitch = 8.f; });
	addBtn(parent, 580, 352, 54, 17, "Side", "View from the side", []() { s_yaw = 270.f; s_pitch = 4.f; });
	// Verschieben / Drehen
	struct MB { const char *l; const char *tip; int axis; float sign; bool rot; };
	static const MB mbs[12] = {
		{ "X-", "Move left (arrow left)", 0, -1.f, false }, { "X+", "Move right (arrow right)", 0, 1.f, false },
		{ "Down", "Move down (page down)", 1, -1.f, false }, { "Up", "Move up (page up)", 1, 1.f, false },
		{ "Fwd", "Move forward (arrow up)", 2, -1.f, false }, { "Back", "Move back (arrow down)", 2, 1.f, false },
		{ "Yaw-", "Turn left (Q)", 0, 1.f, true }, { "Yaw+", "Turn right (E)", 0, -1.f, true },
		{ "Pitch-", "Tilt nose down (F)", 1, -1.f, true }, { "Pitch+", "Tilt nose up (R)", 1, 1.f, true },
		{ "Roll-", "Roll left (Z)", 2, -1.f, true }, { "Roll+", "Roll right (C)", 2, 1.f, true },
	};
	for (int k = 0; k < 12; ++k)
	{
		const MB m = mbs[k];
		addBtn(parent, 6.f + k * 52.5f, 374, 50, 18, m.l, m.tip, [m]() { if (m.rot) { rotate(m.axis, m.sign); } else { move(m.axis, m.sign); } });
	}
	// Teil-Befehle
	addBtn(parent, 6, 396, 76, 18, "Duplicate", "Copy the selected part (or weapon slot)", []() { duplicatePart(false); });
	addBtn(parent, 86, 396, 76, 18, "Mirror", "Copy the selected part (or slot) mirrored to the other side", []() { duplicatePart(true); });
	addBtn(parent, 166, 396, 76, 18, "Remove", "Remove the selected part (or weapon slot) (Del)", []() { deleteSelected(); });
	addBtn(parent, 246, 396, 76, 18, "Reset", "Reset the rotation of the selected part", []() { resetSelected(); });
	// Rumpf-Befehle
	addBtn(parent, 172, 432, 50, 18, "New", "Start a new super hull", []() { newHull(); });
	addBtn(parent, 226, 432, 46, 18, "< Load", "Load the previous saved super hull", []() { loadCycle(-1); });
	addBtn(parent, 276, 432, 46, 18, "Load >", "Load the next saved super hull", []() { loadCycle(1); });
	addBtn(parent, 326, 432, 60, 18, "Save", "Save: the super hull is written to your user folder and appears in the hull list everywhere", []() { doSave(); });
	addBtn(parent, 390, 432, 60, 18, "Delete", "Delete this super hull (old savegames keep working)", []() { doDeleteHull(); });
	addBtn(parent, 584, 432, 50, 18, "Close", "Close the hull forge", []() { close(); });

	// Name
	s_nameBox = std::make_shared<W_EDITBOX>();
	root->attach(s_nameBox);
	s_nameBox->setBoxColours(pal_RGBA(46, 84, 112, 255), pal_RGBA(90, 200, 255, 255), pal_RGBA(4, 9, 20, 255));
	s_nameBox->setPlaceholder(WzString::fromUtf8("Name of the super hull"));
	s_nameBox->setMaxStringSize(40);
	s_nameBox->setString(WzString::fromUtf8(s_def.name));
	s_nameBox->setTip("Name of the super hull");
	s_nameBox->setOnEditingStoppedHandler([](W_EDITBOX &w) { s_def.name = w.getString().toStdString(); s_dirty = true; });

	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_root = root;
}

bool mouseInBay()
{
	const Ctx c = ctx();
	const int mx = mouseX(), my = mouseY();
	return mx >= c.X(166) && mx <= c.X(474) && my >= c.Y(6) && my <= c.Y(336);
}

} // namespace

bool isOpen()
{
	return s_open;
}

bool open()
{
	if (s_open) { return true; }
	if (!modui::ready())
	{
		addConsoleMessage("The hull forge needs the mod interface images.", DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		return false;
	}
	if (s_def.parts.empty() && s_def.name.empty()) { s_def.name = "New hull"; }
	rebuildBodyList();
	if (!s_screen) { buildScreen(); }
	s_open = true;
	modpanel::setSuppressed(true);
	modlayer::setSuppressed(true);
	s_root->show(true);
	layout();
	if (s_def.parts.empty()) { setStatus("Pick the chassis from the hull list on the left (its propulsion is used), then add more hulls."); }
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
		s_nameBox = nullptr;
		s_btns.clear();
	}
	modpanel::setSuppressed(false);
	modlayer::setSuppressed(false);
	if (intMode == INT_DESIGN) { intDesignRefreshComponents(); }   // neue/geaenderte Verbundrumpfe sofort in der Liste
}

void tick()
{
	// Verbundrumpfe: baubar, sobald alle Teil-Rumpfe erforscht sind (Fabriklisten fragen die Verfuegbarkeit ab)
	if (realTime - s_availTick > 1000 || realTime < s_availTick)
	{
		s_availTick = realTime;
		if (selectedPlayer < MAX_PLAYERS) { modcompbody::refreshAvailability(selectedPlayer); }
	}
	if (!s_open || !s_root) { return; }
	if (static_cast<int>(screenWidth) != s_lastW || static_cast<int>(screenHeight) != s_lastH) { layout(); }
	if (s_autoRot) { s_yaw = std::fmod(s_yaw + 0.35f, 360.f); }
	// Ansicht: ziehen dreht, Mausrad zoomt bzw. blaettert in den Listen
	const Vector2i wheel = getMouseWheelSpeed();
	const Ctx c = ctx();
	const int mx = mouseX(), my = mouseY();
	if (wheel.y != 0)
	{
		if (mouseInBay()) { s_zoom = std::clamp(s_zoom * (wheel.y > 0 ? 1.12f : 1.f / 1.12f), 0.3f, 5.f); }
		else if (mx >= c.X(6) && mx <= c.X(160) && my >= c.Y(20) && my <= c.Y(20.f + BODY_ROWS * BODY_ROW_H))
		{
			s_bodyScroll = std::clamp(s_bodyScroll + (wheel.y > 0 ? -3 : 3), 0, std::max(0, static_cast<int>(s_bodies.size()) - BODY_ROWS));
		}
		else if (mx >= c.X(480) && my >= c.Y(20) && my <= c.Y(20.f + PART_ROWS * LIST_ROW_H))
		{
			s_partScroll = std::clamp(s_partScroll + (wheel.y > 0 ? -1 : 1), 0, std::max(0, static_cast<int>(s_def.parts.size()) - PART_ROWS));
		}
	}
	if (mouseDown(MOUSE_LMB) && (s_dragging || mouseInBay()))
	{
		if (s_dragging)
		{
			s_yaw += static_cast<float>(mx - s_dragX) * 0.5f;
			s_pitch = std::clamp(s_pitch + static_cast<float>(my - s_dragY) * 0.35f, -10.f, 85.f);
		}
		s_dragging = true;
		s_dragX = mx; s_dragY = my;
	}
	else { s_dragging = false; }
	// Tasten (nicht beim Schreiben des Namens)
	if (W_EDITBOX::currentEditBox() != nullptr) { return; }
	if (keyPressed(KEY_LEFTARROW)) { move(0, -1.f); }
	if (keyPressed(KEY_RIGHTARROW)) { move(0, 1.f); }
	if (keyPressed(KEY_UPARROW)) { move(2, -1.f); }
	if (keyPressed(KEY_DOWNARROW)) { move(2, 1.f); }
	if (keyPressed(KEY_PAGEUP)) { move(1, 1.f); }
	if (keyPressed(KEY_PAGEDOWN)) { move(1, -1.f); }
	if (keyPressed(KEY_Q)) { rotate(0, 1.f); }
	if (keyPressed(KEY_E)) { rotate(0, -1.f); }
	if (keyPressed(KEY_R)) { rotate(1, 1.f); }
	if (keyPressed(KEY_F)) { rotate(1, -1.f); }
	if (keyPressed(KEY_Z)) { rotate(2, -1.f); }
	if (keyPressed(KEY_C)) { rotate(2, 1.f); }
	if (keyPressed(KEY_TAB) && !s_def.parts.empty()) { selectPart((s_sel + 1) % static_cast<int>(s_def.parts.size())); }
	if (keyPressed(KEY_DELETE)) { deleteSelected(); }
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
	s_nameBox = nullptr;
	s_btns.clear();
	s_bodies.clear();
}

// ---------------------------------------------------------------- Einstiegstaste im Design-Bildschirm
void displayEntryButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	const int lift = hl ? 26 : 0;
	pie_UniTransBoxFill(x0 + 3, y0 + 3, x0 + w + 3, y0 + h + 3, pal_RGBA(0, 0, 0, 90));
	pie_BoxFill(x0, y0, x0 + w, y0 + h, pal_RGBA(2, 10, 30, 255));
	const int in = 3;
	for (int i = in; i < h - in; ++i)
	{
		const float t = static_cast<float>(i - in) / static_cast<float>(std::max(1, h - 2 * in - 1));
		const int r = static_cast<int>(6 + 10 * t) + lift / 3;
		const int g = static_cast<int>(36 + 58 * t) + lift;
		const int bl = static_cast<int>(104 + 96 * t) + lift;
		pie_BoxFill(x0 + in, y0 + i, x0 + w - in, y0 + i + 1, pal_RGBA(static_cast<uint8_t>(std::min(255, r)), static_cast<uint8_t>(std::min(255, g)), static_cast<uint8_t>(std::min(255, bl)), 255));
	}
	const int px0 = x0 + 8, py0 = y0 + 8, px1 = x0 + w - 8, py1 = y0 + h - 36;
	pie_UniTransBoxFill(px0, py0, px1, py1, pal_RGBA(2, 14, 44, 200));
	for (int gy = py0 + 8; gy < py1; gy += 8) { pie_UniTransBoxFill(px0, gy, px1, gy + 1, pal_RGBA(40, 150, 220, 40)); }
	for (int gx = px0 + 8; gx < px1; gx += 8) { pie_UniTransBoxFill(gx, py0, gx + 1, py1, pal_RGBA(40, 150, 220, 40)); }
	// Sinnbild: drei uebereinander verschobene Rumpf-Kaesten (weiss, rechts abgedunkelt)
	struct R { int x0, y0, x1, y1; };
	static const R blocks[] = { { 3, 26, 21, 32 }, { 7, 18, 19, 24 }, { 1, 20, 6, 25 }, { 18, 20, 23, 25 }, { 10, 8, 14, 17 }, { 9, 6, 15, 9 } };
	const float k = std::min(static_cast<float>(px1 - px0 - 4) / 24.f, static_cast<float>(py1 - py0 - 4) / 36.f);
	const int ox = px0 + static_cast<int>(((px1 - px0) - 24 * k) / 2.f), oy = py0 + 2 + (down ? 1 : 0);
	const unsigned char wa = hl ? 255 : 238;
	for (const R &r : blocks)
	{
		const int ax = ox + static_cast<int>(r.x0 * k), ay = oy + static_cast<int>(r.y0 * k);
		const int bx2 = ox + static_cast<int>(r.x1 * k), by2 = oy + static_cast<int>(r.y1 * k);
		const int mxx = (ax + bx2) / 2;
		pie_BoxFill(ax, ay, mxx, by2, pal_RGBA(255, 255, 255, wa));
		pie_BoxFill(mxx, ay, bx2, by2, pal_RGBA(150, 195, 235, wa));
	}
	const PIELIGHT edgeL = pal_RGBA(170, 230, 255, 255), edgeD = pal_RGBA(0, 20, 60, 255);
	iV_Line(x0 + 1, y0 + 1, x0 + w - 2, y0 + 1, down ? edgeD : edgeL);
	iV_Line(x0 + 1, y0 + 1, x0 + 1, y0 + h - 2, down ? edgeD : edgeL);
	iV_Line(x0 + w - 2, y0 + 1, x0 + w - 2, y0 + h - 2, down ? edgeL : edgeD);
	iV_Line(x0 + 1, y0 + h - 2, x0 + w - 2, y0 + h - 2, down ? edgeL : edgeD);
	iV_Box(x0, y0, x0 + w - 1, y0 + h - 1, hl ? pal_RGBA(70, 245, 255, 255) : pal_RGBA(40, 170, 230, 255));
	if (modui::ready())
	{
		const std::string t1 = "HULL", t2 = "FORGE";
		modui::text(t1, x0 + (w - modui::textWidth(t1)) / 2, y0 + h - 31, pal_RGBA(235, 250, 255, 255));
		modui::text(t2, x0 + (w - modui::textWidth(t2)) / 2, y0 + h - 18, hl ? pal_RGBA(120, 250, 255, 255) : pal_RGBA(150, 215, 255, 255));
	}
}

}
