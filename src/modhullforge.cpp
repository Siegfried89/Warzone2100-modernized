// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 23.11. 01:35): Rumpf-Baukasten (siehe modhullforge.h)
//
// (Wunsch 23.11. 04:23) Ueberarbeitet:
//  - "Die Kamera im Rumpfeditor soll ganz einfach per Maus-Drag zu steuern sein": linke Maustaste ziehen = drehen, rechte (oder mittlere)
//    ziehen = verschieben, Mausrad = Zoom; Klick auf einen Waffenplatz waehlt ihn, Klick auf einen Rumpf waehlt das Teil.
//  - "Rechtsklick auf den Body in der rechten Leiste loescht ihn" - auch Waffenplaetze (Liste und 3D-Ansicht) loescht ein Rechtsklick.
//  - "Die Positionen besser und uebersichtlicher trennen. Ausserdem originalen WZ-2100-Stil (Knopfstil) beibehalten": WZ-Fensterrahmen,
//    WZ-Knoepfe (Schattenkasten, WZ-Schrift), eigene Gruppen VERSCHIEBEN / DREHEN / TEIL / UEBER-RUMPF / ANSICHT.
//  - "Die Waffenplaetze sollen so aussehen wie der originale WZ-Waffenturmplatz (der gelbe Platzhalter)": jeder Platz zeigt den
//    WZ-Platzhalter-Turm (znullturret + znullgun) in seiner Blickrichtung.
//  - "Jeder Rumpf soll gleich seinen natuerlichen Waffenplatz spawnen - wie im normalen WZ-Einheiteneditor. Man kann ihn mit Rechtsklick
//    entfernen": jedes neue Teil bringt die Waffenplaetze seines Rumpfes mit. Diese Plaetze haengen am Teil: verschieben, drehen, spiegeln,
//    kopieren oder entfernen des Teils nimmt sie mit (auch nach dem Laden erkannt).
//
// Aufbau (virtuelles 640x480-Bild, auf den Bildschirm skaliert):
//   links   Rumpfliste (Klick = als neues Teil anbauen)
//   Mitte   3D-Ansicht des Verbundrumpfes
//   rechts  Teile-Liste (Teil 1 = Fahrgestell), Waffenplaetze, Werte
//   unten   Auswahl-Zeile, Gruppen VERSCHIEBEN / DREHEN / TEIL, UEBER-RUMPF (Name, Neu/Laden/Speichern/Loeschen), ANSICHT, Meldungen
// Tasten (nicht beim Schreiben des Namens): Pfeile = x / vor-zurueck, Bild hoch/runter = Hoehe, Q/E = Gieren, R/F = Nicken,
// Z/C = Rollen, Umschalt = feinste Stufe, Tab = naechstes Teil, Entf = Teil/Platz entfernen.

#include "modhullforge.h"
#include "modcompbody.h"
#include "modpanel.h"
#include "modlayer.h"
#include "modui.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/ivis_opengl/textdraw.h"
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
#include "intimage.h"
#include "stats.h"
#include "statsdef.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
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

constexpr int BODY_ROWS = 21;
constexpr float BODY_ROW_H = 14.f;
constexpr int PART_ROWS = 8;
constexpr int MOUNT_ROWS = 8;
constexpr float LIST_ROW_H = 13.f;
constexpr float BAY_DEPTH = 1200.f;
const float STEPS[3] = { 0.5f, 2.f, 8.f };
const float ANGLES[3] = { 5.f, 15.f, 90.f };

// Bereiche (virtuelle Einheiten)
constexpr float BAY_X = 166, BAY_Y = 6, BAY_W = 308, BAY_H = 330;
constexpr float PARTS_Y = 20, SLOTS_HEAD_Y = 134, SLOTS_Y = 148, VALUES_Y = 282;
constexpr float GROUP_Y = 352, HULL_Y = 400;

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

// ---------------------------------------------------------------- WZ-Schrift (zwischengespeichert)
enum TextKind { TK_ROW = 0, TK_BUTTON, TK_HEAD };

iV_fonts fontFor(TextKind k, float S)
{
	if (S < 1.5f) { return k == TK_HEAD ? font_regular_bold : font_small; }
	if (S < 2.25f) { return k == TK_HEAD ? font_regular_bold : font_regular; }
	return k == TK_HEAD ? font_large : font_medium;
}

std::map<std::string, WzText> s_texts;

WzText &cachedText(const std::string &s, iV_fonts f)
{
	const std::string key = std::to_string(static_cast<int>(f)) + "|" + s;
	auto it = s_texts.find(key);
	if (it != s_texts.end()) { return it->second; }
	if (s_texts.size() > 600) { s_texts.clear(); }
	WzText &t = s_texts[key];
	t.setText(WzString::fromUtf8(s), f);
	return t;
}

int textW(const std::string &s, TextKind k, float S)
{
	return s.empty() ? 0 : cachedText(s, fontFor(k, S)).width();
}

/// Text mit Oberkante bei top (WZ-Schrift)
void wzText(const std::string &s, int x, int top, PIELIGHT col, TextKind k, float S, int maxW = -1)
{
	if (s.empty()) { return; }
	WzText &t = cachedText(s, fontFor(k, S));
	t.render(static_cast<float>(x), static_cast<float>(top - t.aboveBase()), col, 0.f, maxW);
}

/// Text auf eine Breite kuerzen ("...")
std::string fitW(const std::string &s, int maxW, TextKind k, float S)
{
	if (textW(s, k, S) <= maxW) { return s; }
	std::string t = s;
	while (!t.empty() && textW(t + "...", k, S) > maxW) { t.pop_back(); }
	return t + "...";
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

/// Waffenplatz am Teil: Lage und Blickrichtung im Modellraum des Teils (nur im Editor; beim Laden wiedererkannt)
struct Link
{
	int part = -1;
	glm::vec3 local = glm::vec3(0.f);
	float localYaw = 0.f;
};

Def s_def;
std::vector<Link> s_links;      // parallel zu s_def.mounts
int s_sel = -1;                 // gewaehltes Teil
int s_selMount = -1;            // gewaehlter Waffenplatz (>= 0: Verschieben wirkt auf ihn)
int s_step = 1, s_angle = 1;
float s_yaw = 215.f, s_pitch = 24.f, s_zoom = 1.f;
float s_panX = 0.f, s_panY = 0.f;   // Verschiebung der Ansicht (Bildpunkte)
bool s_autoRot = false;
std::vector<size_t> s_bodies;   // Rumpfe fuer die Liste (Index in asBodyStats)
int s_bodyScroll = 0;
int s_partScroll = 0;
int s_mountScroll = 0;
int s_loadIdx = -1;
std::string s_status;
bool s_statusBad = false;
unsigned s_statusUntil = 0;
// Maus in der 3D-Ansicht
enum DragMode { DRAG_NONE = 0, DRAG_TURN, DRAG_PAN };
DragMode s_drag = DRAG_NONE;
int s_dragBtn = -1;              // 0 links, 1 rechts, 2 Mitte
int s_pressX = 0, s_pressY = 0, s_lastX = 0, s_lastY = 0;
bool s_moved = false;
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

glm::vec3 dirFromYaw(float yawDeg)
{
	const float r = glm::radians(yawDeg);
	return glm::vec3(std::sin(r), 0.f, -std::cos(r));
}

bool yawFromDir(const glm::vec3 &d, float &yawDeg)
{
	if (std::fabs(d.x) + std::fabs(d.z) < 1e-4f) { return false; }
	yawDeg = glm::degrees(std::atan2(d.x, -d.z));
	return true;
}

/// natuerliche Waffenplaetze eines Rumpfes (Modellraum des Teils: x, Hoehe, Tiefe) - wie im WZ-Designer: so viele wie der Rumpf Plaetze hat
std::vector<glm::vec3> naturalSlots(const std::string &bodyId)
{
	std::vector<glm::vec3> out;
	const BODY_STATS *b = modcompbody::bodyById(bodyId);
	if (b == nullptr || b->pIMD == nullptr) { return out; }
	const size_t n = std::min<size_t>(std::max<unsigned>(1u, b->weaponSlots), b->pIMD->connectors.size());
	for (size_t i = 0; i < n; ++i)
	{
		const Vector3i &c = b->pIMD->connectors[i];   // x, Tiefe, Hoehe
		out.emplace_back(static_cast<float>(c.x), static_cast<float>(c.z), static_cast<float>(c.y));
	}
	return out;
}

bool linkValid(size_t k)
{
	return k < s_links.size() && s_links[k].part >= 0 && s_links[k].part < static_cast<int>(s_def.parts.size());
}

/// Waffenplatz k an seinem Teil neu ausrichten (nach Verschieben/Drehen/Spiegeln des Teils)
void applyLink(size_t k)
{
	if (!linkValid(k) || k >= s_def.mounts.size()) { return; }
	const Link &l = s_links[k];
	const glm::mat4 M = modcompbody::partMatrix(s_def.parts[static_cast<size_t>(l.part)]);
	Mount &m = s_def.mounts[k];
	m.pos = glm::vec3(M * glm::vec4(l.local, 1.f));
	float y = m.yaw;
	if (yawFromDir(glm::mat3(M) * dirFromYaw(l.localYaw), y)) { m.yaw = std::remainder(y, 360.f); }
}

/// nach eigenem Verschieben/Drehen eines Waffenplatzes: Lage am Teil neu merken
void relink(size_t k)
{
	if (!linkValid(k) || k >= s_def.mounts.size()) { return; }
	Link &l = s_links[k];
	const glm::mat4 Mi = glm::inverse(modcompbody::partMatrix(s_def.parts[static_cast<size_t>(l.part)]));
	const Mount &m = s_def.mounts[k];
	l.local = glm::vec3(Mi * glm::vec4(m.pos, 1.f));
	float y = l.localYaw;
	if (yawFromDir(glm::mat3(Mi) * dirFromYaw(m.yaw), y)) { l.localYaw = y; }
}

void syncPart(int partIdx)
{
	for (size_t k = 0; k < s_links.size(); ++k) { if (s_links[k].part == partIdx) { applyLink(k); } }
}

/// nach dem Laden: Waffenplaetze, die genau auf einem natuerlichen Platz eines Teils liegen, haengen wieder an diesem Teil
void inferLinks()
{
	s_links.assign(s_def.mounts.size(), Link());
	for (size_t k = 0; k < s_def.mounts.size(); ++k)
	{
		for (size_t i = 0; i < s_def.parts.size() && s_links[k].part < 0; ++i)
		{
			const glm::mat4 M = modcompbody::partMatrix(s_def.parts[i]);
			for (const glm::vec3 &loc : naturalSlots(s_def.parts[i].body))
			{
				if (glm::length(glm::vec3(M * glm::vec4(loc, 1.f)) - s_def.mounts[k].pos) < 0.75f)
				{
					s_links[k].part = static_cast<int>(i);
					relink(k);
					break;
				}
			}
		}
	}
}

void addSlot(const Mount &m, const Link &l)
{
	s_def.mounts.push_back(m);
	s_links.push_back(l);
}

void removeSlot(size_t k)
{
	if (k >= s_def.mounts.size()) { return; }
	s_def.mounts.erase(s_def.mounts.begin() + static_cast<std::ptrdiff_t>(k));
	if (k < s_links.size()) { s_links.erase(s_links.begin() + static_cast<std::ptrdiff_t>(k)); }
	if (s_selMount == static_cast<int>(k)) { s_selMount = -1; }
	else if (s_selMount > static_cast<int>(k)) { --s_selMount; }
	s_dirty = true;
}

/// die natuerlichen Waffenplaetze des Teils i anlegen; Rueckgabe: wie viele
int addNaturalSlots(int partIdx)
{
	if (partIdx < 0 || partIdx >= static_cast<int>(s_def.parts.size())) { return 0; }
	int n = 0;
	for (const glm::vec3 &loc : naturalSlots(s_def.parts[static_cast<size_t>(partIdx)].body))
	{
		if (s_def.mounts.size() >= modcompbody::MAX_SLOTS) { break; }
		Link l; l.part = partIdx; l.local = loc; l.localYaw = 0.f;
		addSlot(Mount(), l);
		applyLink(s_def.mounts.size() - 1);
		++n;
	}
	return n;
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

void selectMount(int k)
{
	if (k < 0 || k >= static_cast<int>(s_def.mounts.size())) { return; }
	s_selMount = k;
	if (s_selMount < s_mountScroll) { s_mountScroll = s_selMount; }
	if (s_selMount >= s_mountScroll + MOUNT_ROWS) { s_mountScroll = s_selMount - MOUNT_ROWS + 1; }
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
	const int idx = static_cast<int>(s_def.parts.size()) - 1;
	const int n = addNaturalSlots(idx);   // (04:23) wie im WZ-Designer: der Rumpf bringt seine Waffenplaetze mit
	selectPart(idx);
	s_dirty = true;
	play("mfui_clank.ogg");
	const std::string slots = n > 0 ? " - " + std::to_string(n) + (n == 1 ? " weapon slot" : " weapon slots") + " (right-click removes)" : "";
	setStatus((s_def.parts.size() == 1 ? "Chassis set: " + bodyName(p.body) + " (its propulsion is used)" : "Added: " + bodyName(p.body)) + slots);
}

void move(int axis, float sign)
{
	const bool fine = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	const float d = (fine ? STEPS[0] : STEPS[s_step]) * sign;
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		s_def.mounts[static_cast<size_t>(s_selMount)].pos[axis] += d;
		relink(static_cast<size_t>(s_selMount));
	}
	else if (s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size()))
	{
		s_def.parts[static_cast<size_t>(s_sel)].pos[axis] += d;
		syncPart(s_sel);
	}
	else { return; }
	s_dirty = true;
}

void rotate(int axis, float sign)
{
	const bool fine = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
	// (Wunsch 23.11. 03:05) Waffenplatz: Gieren dreht seine Blickrichtung - die Waffe schwenkt 180 Grad um sie
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		if (axis != 0) { setStatus("Weapon slots only turn left/right (yaw): the weapon covers 180 degrees around the slot's facing."); return; }
		float &y = s_def.mounts[static_cast<size_t>(s_selMount)].yaw;
		y = std::remainder(y - (fine ? 1.f : ANGLES[s_angle]) * sign, 360.f);
		relink(static_cast<size_t>(s_selMount));
		s_dirty = true;
		return;
	}
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size())) { return; }
	float &a = s_def.parts[static_cast<size_t>(s_sel)].rot[axis];
	a = std::fmod(a + (fine ? 1.f : ANGLES[s_angle]) * sign + 720.f, 360.f);
	if (a > 180.f) { a -= 360.f; }
	syncPart(s_sel);
	s_dirty = true;
}

void duplicatePart(bool mirror)
{
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		if (s_def.mounts.size() >= modcompbody::MAX_SLOTS) { setStatus("At most " + std::to_string(modcompbody::MAX_SLOTS) + " weapon slots.", true); return; }
		Mount m = s_def.mounts[static_cast<size_t>(s_selMount)];
		if (mirror) { m.pos.x = -m.pos.x; m.yaw = -m.yaw; } else { m.pos.x += STEPS[s_step]; }
		addSlot(m, Link());
		selectMount(static_cast<int>(s_def.mounts.size()) - 1);
		s_dirty = true;
		return;
	}
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size())) { return; }
	const int src = s_sel;
	Part p = s_def.parts[static_cast<size_t>(src)];
	if (mirror) { p.pos.x = -p.pos.x; p.rot.x = -p.rot.x; p.rot.z = -p.rot.z; p.flip ^= 1; }   // echte Spiegelung an der Mittelebene
	else { p.pos.x += 8.f; }
	s_def.parts.push_back(p);
	const int dst = static_cast<int>(s_def.parts.size()) - 1;
	// die Waffenplaetze des Teils wandern mit (gespiegelt ueber die gespiegelte Teil-Lage)
	const size_t nm = s_def.mounts.size();
	int copied = 0;
	for (size_t k = 0; k < nm; ++k)
	{
		if (k >= s_links.size() || s_links[k].part != src) { continue; }
		if (s_def.mounts.size() >= modcompbody::MAX_SLOTS) { break; }
		Link l = s_links[k]; l.part = dst;
		addSlot(s_def.mounts[k], l);
		applyLink(s_def.mounts.size() - 1);
		++copied;
	}
	selectPart(dst);
	s_dirty = true;
	play("mfui_clank.ogg");
	if (copied > 0) { setStatus(std::string(mirror ? "Mirrored" : "Copied") + " with " + std::to_string(copied) + (copied == 1 ? " weapon slot." : " weapon slots.")); }
}

void removePart(int i)
{
	if (i < 0 || i >= static_cast<int>(s_def.parts.size())) { return; }
	// zuerst die Waffenplaetze dieses Teils
	for (size_t k = s_def.mounts.size(); k-- > 0;)
	{
		if (k < s_links.size() && s_links[k].part == i) { removeSlot(k); }
	}
	for (Link &l : s_links) { if (l.part > i) { --l.part; } }
	const std::string name = bodyName(s_def.parts[static_cast<size_t>(i)].body);
	s_def.parts.erase(s_def.parts.begin() + i);
	if (i == 0 && !s_def.parts.empty()) { setStatus("Removed " + name + " - new chassis: " + bodyName(s_def.parts[0].body)); }
	else { setStatus("Removed: " + name); }
	if (s_def.parts.empty()) { s_sel = -1; }
	else { selectPart(std::min(std::max(0, s_sel > i ? s_sel - 1 : s_sel), static_cast<int>(s_def.parts.size()) - 1)); }
	s_selMount = -1;
	s_dirty = true;
	play("mfui_rattle.ogg");
}

void deleteSelected()
{
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		const int k = s_selMount;
		removeSlot(static_cast<size_t>(k));
		if (!s_def.mounts.empty()) { selectMount(std::min(k, static_cast<int>(s_def.mounts.size()) - 1)); }
		play("mfui_rattle.ogg");
		return;
	}
	removePart(s_sel);
}

void flipSelected(int axisBit)
{
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size()) || s_selMount >= 0) { return; }
	s_def.parts[static_cast<size_t>(s_sel)].flip ^= axisBit;
	syncPart(s_sel);
	s_dirty = true;
	play("mfui_clank.ogg");
}

void resetSelected()
{
	if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
	{
		const size_t k = static_cast<size_t>(s_selMount);
		if (linkValid(k))
		{
			// Platz am Teil: zurueck auf den naechsten natuerlichen Platz des Rumpfes, Blick nach vorn
			const std::vector<glm::vec3> nat = naturalSlots(s_def.parts[static_cast<size_t>(s_links[k].part)].body);
			float best = 1e9f;
			for (const glm::vec3 &n : nat) { const float d = glm::length(n - s_links[k].local); if (d < best) { best = d; s_links[k].local = n; } }
			s_links[k].localYaw = 0.f;
			applyLink(k);
		}
		else { s_def.mounts[k].pos = glm::vec3(0.f, 20.f, 0.f); s_def.mounts[k].yaw = 0.f; }
		s_dirty = true;
		return;
	}
	if (s_sel < 0 || s_sel >= static_cast<int>(s_def.parts.size())) { return; }
	Part &p = s_def.parts[static_cast<size_t>(s_sel)];
	p.rot = glm::vec3(0.f);
	p.flip = 0;
	if (s_sel == 0) { p.pos = glm::vec3(0.f); }
	syncPart(s_sel);
	s_dirty = true;
}

void addMount()
{
	if (s_def.mounts.size() >= modcompbody::MAX_SLOTS) { setStatus("At most " + std::to_string(modcompbody::MAX_SLOTS) + " weapon slots.", true); return; }
	Mount m;
	m.pos = glm::vec3(0.f, std::round(topOfComposite()), 0.f);
	Link l;
	if (s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size())) { l.part = s_sel; }   // haengt am gewaehlten Teil
	addSlot(m, l);
	relink(s_def.mounts.size() - 1);
	selectMount(static_cast<int>(s_def.mounts.size()) - 1);
	s_dirty = true;
	setStatus("Weapon slot added - move it with the MOVE buttons, turn its facing with Yaw (right-click removes it).");
}

void newHull()
{
	s_def = Def();
	s_def.name = "New hull";
	s_links.clear();
	s_sel = -1; s_selMount = -1; s_partScroll = 0; s_mountScroll = 0; s_loadIdx = -1;
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
	// alte Entwuerfe ohne eigene Plaetze: die des Fahrgestells (wie im Spiel)
	if (s_def.mounts.empty() && !s_def.parts.empty()) { s_def.mounts = modcompbody::defaultMounts(s_def.parts[0]); }
	inferLinks();
	s_sel = s_def.parts.empty() ? -1 : 0; s_selMount = -1; s_partScroll = 0; s_mountScroll = 0;
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
	else { setStatus(ok ? "Saved: \"" + s_def.name + "\" - now available in the hull list (campaign, skirmish and the fleet's unit designer session)." : "Could not save.", !ok); }
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
/// WZ-Fensterrahmen (wie die Formulare im Spiel)
void wzFrame(int x, int y, int w, int h)
{
	RenderWindowFrame(FRAME_NORMAL, static_cast<uint32_t>(std::max(0, x)), static_cast<uint32_t>(std::max(0, y)), static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
}

/// Gruppen-Rahmen mit Ueberschrift
void groupFrame(const Ctx &c, float x, float y, float w, float h, const std::string &title)
{
	wzFrame(c.X(x), c.Y(y), c.L(w), c.L(h));
	wzText(title, c.X(x + 6), c.Y(y + 4), pal_RGBA(255, 214, 110, 255), TK_HEAD, c.S);
}

/// Listenfeld: dunkler Grund mit WZ-Kante
void listFrame(int x, int y, int w, int h)
{
	pie_UniTransBoxFill(x, y, x + w, y + h, pal_RGBA(0, 6, 20, 210));
	iV_Box(x, y, x + w - 1, y + h - 1, WZCOL_FORM_LIGHT);
}

const iIMDShape *nullTurret()
{
	static iIMDBaseShape *s = nullptr;
	static unsigned gen = 0;
	if (modelCacheStale(gen)) { s = modelGet("znullturret.pie"); }
	return s ? s->displayModel() : nullptr;
}

const iIMDShape *nullGun()
{
	static iIMDBaseShape *s = nullptr;
	static unsigned gen = 0;
	if (modelCacheStale(gen)) { s = modelGet("znullgun.pie"); }
	return s ? s->displayModel() : nullptr;
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

void setBayOffset(const Ctx &c)
{
	pie_SetGeometricOffset(c.X(BAY_X) + c.L(BAY_W) / 2 + static_cast<int>(s_panX), c.Y(BAY_Y) + c.L(BAY_H) / 2 + c.L(30) + static_cast<int>(s_panY));
}

bool project(const glm::mat4 &base, const glm::vec3 &p, Vector2i &out)
{
	const glm::mat4 m = pie_UIPerspectiveGet() * base * glm::translate(glm::mat4(1.f), p);
	const Vector3i zero(0, 0, 0);
	return pie_RotateProjectWithPerspective(&zero, m, &out) > 0 && out.x != LONG_WAY;
}

/// Bildschirmlage aller Waffenplaetze (fuer Klicks); -1 = keiner in der Naehe
int mountAt(int mx, int my)
{
	const Ctx c = ctx();
	setBayOffset(c);
	float sc = 1.f;
	const glm::mat4 base = bayBase(c, sc);
	int best = -1;
	int bestD = std::max(8, c.L(9)) * std::max(8, c.L(9));
	for (size_t i = 0; i < s_def.mounts.size(); ++i)
	{
		Vector2i sp;
		if (!project(base, s_def.mounts[i].pos + glm::vec3(0.f, 3.f, 0.f), sp)) { continue; }
		const int d = (sp.x - mx) * (sp.x - mx) + (sp.y - my) * (sp.y - my);
		if (d < bestD) { bestD = d; best = static_cast<int>(i); }
	}
	pie_SetGeometricOffset(static_cast<int>(screenWidth) / 2, static_cast<int>(screenHeight) / 2);
	return best;
}

/// Teil unter dem Mauszeiger (kleinste Bildschirm-Huelle, die den Punkt enthaelt); -1 = keines
int partAt(int mx, int my)
{
	const Ctx c = ctx();
	setBayOffset(c);
	float sc = 1.f;
	const glm::mat4 base = bayBase(c, sc);
	int best = -1;
	long bestA = 0;
	for (size_t i = 0; i < s_def.parts.size(); ++i)
	{
		glm::vec3 cr[8];
		if (!partCorners(s_def.parts[i], cr)) { continue; }
		int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
		bool ok = true;
		for (const glm::vec3 &v : cr)
		{
			Vector2i sp;
			if (!project(base, v, sp)) { ok = false; break; }
			x0 = std::min(x0, sp.x); y0 = std::min(y0, sp.y); x1 = std::max(x1, sp.x); y1 = std::max(y1, sp.y);
		}
		if (!ok || mx < x0 || mx > x1 || my < y0 || my > y1) { continue; }
		const long a = static_cast<long>(x1 - x0) * static_cast<long>(y1 - y0);
		if (best < 0 || a < bestA) { best = static_cast<int>(i); bestA = a; }
	}
	pie_SetGeometricOffset(static_cast<int>(screenWidth) / 2, static_cast<int>(screenHeight) / 2);
	return best;
}

void drawBay(const Ctx &c)
{
	const int bx = c.X(BAY_X), by = c.Y(BAY_Y), bw = c.L(BAY_W), bh = c.L(BAY_H);
	wzFrame(bx, by, bw, bh);
	pie_UniTransBoxFill(bx + 4, by + 4, bx + bw - 4, by + bh - 4, pal_RGBA(0, 4, 14, 150));
	const int team = static_cast<int>(getPlayerColour(selectedPlayer));
	setBayOffset(c);
	float sc = 1.f;
	const glm::mat4 base = bayBase(c, sc);
	const glm::mat4 view(1.f);
	// Bodenraster und Pfeil "vorn"
	{
		const float g = std::ceil(compositeExtent() / 16.f) * 16.f;
		for (float t = -g; t <= g + 0.1f; t += 16.f)
		{
			Vector2i a, b2;
			if (project(base, glm::vec3(t, 0.f, -g), a) && project(base, glm::vec3(t, 0.f, g), b2)) { iV_Line(a.x, a.y, b2.x, b2.y, pal_RGBA(40, 120, 170, 70)); }
			if (project(base, glm::vec3(-g, 0.f, t), a) && project(base, glm::vec3(g, 0.f, t), b2)) { iV_Line(a.x, a.y, b2.x, b2.y, pal_RGBA(40, 120, 170, 70)); }
		}
		Vector2i a, b2;
		if (project(base, glm::vec3(0.f, 0.f, -g * 0.6f), a) && project(base, glm::vec3(0.f, 0.f, -g - 10.f), b2)) { iV_Line(a.x, a.y, b2.x, b2.y, pal_RGBA(120, 255, 170, 200)); }
	}
	for (size_t i = 0; i < s_def.parts.size(); ++i)
	{
		const Part &p = s_def.parts[i];
		const BODY_STATS *b = modcompbody::bodyById(p.body);
		if (b == nullptr || b->pIMD == nullptr || b->pIMD->displayModel() == nullptr) { continue; }
		const iIMDShape *flipped = modcompbody::flippedShape(p.body, p.flip);
		if (flipped != nullptr) { pie_Draw3DShape(flipped, 0, team, WZCOL_WHITE, pie_BUTTON, 0, base * modcompbody::partMatrix(p, false), view); }
		else { pie_Draw3DShape(b->pIMD->displayModel(), 0, team, WZCOL_WHITE, pie_BUTTON, 0, base * modcompbody::partMatrix(p), view); }
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
	// Waffenplaetze: der gelbe WZ-Platzhalter-Turm (znullturret + znullgun) in der Blickrichtung des Platzes
	const iIMDShape *nt = nullTurret(), *ng = nullGun();
	for (size_t i = 0; i < s_def.mounts.size(); ++i)
	{
		const Mount &m = s_def.mounts[i];
		const bool sel = static_cast<int>(i) == s_selMount;
		const glm::mat4 mm = base * glm::translate(glm::mat4(1.f), m.pos) * glm::rotate(glm::mat4(1.f), glm::radians(-m.yaw), glm::vec3(0.f, 1.f, 0.f));
		const PIELIGHT light = sel ? pal_SetBrightness(static_cast<UBYTE>(205 + 50 * std::sin(static_cast<float>(realTime) * 0.012f))) : WZCOL_WHITE;
		if (nt != nullptr)
		{
			pie_Draw3DShape(nt, 0, team, light, pie_BUTTON, 0, mm, view);
			glm::mat4 gm = mm;
			if (!nt->connectors.empty()) { gm *= glm::translate(glm::mat4(1.f), glm::vec3(nt->connectors[0].xzy())); }
			if (ng != nullptr) { pie_Draw3DShape(ng, 0, team, light, pie_BUTTON, 0, gm, view); }
		}
		Vector2i sp;
		if (!project(base, m.pos + glm::vec3(0.f, 3.f, 0.f), sp)) { continue; }
		if (nt == nullptr)
		{
			const int r = sel ? 6 : 4;
			pie_BoxFill(sp.x - r, sp.y - r, sp.x + r, sp.y + r, pal_RGBA(255, 210, 40, 255));
		}
		// gewaehlt: Blickrichtung und die Grenzen des 180-Grad-Schwenkbereichs (Wunsch 23.11. 03:05)
		if (sel)
		{
			const glm::vec3 fwd = dirFromYaw(m.yaw), side(-fwd.z, 0.f, fwd.x);
			Vector2i a, l, rr;
			if (project(base, m.pos + fwd * 18.f, a)) { iV_Line(sp.x, sp.y, a.x, a.y, pal_RGBA(255, 240, 120, 255)); }
			if (project(base, m.pos + side * 14.f, rr) && project(base, m.pos - side * 14.f, l))
			{
				iV_Line(sp.x, sp.y, rr.x, rr.y, pal_RGBA(255, 240, 120, 120));
				iV_Line(sp.x, sp.y, l.x, l.y, pal_RGBA(255, 240, 120, 120));
			}
			const int r = c.L(9);
			iV_Box(sp.x - r, sp.y - r, sp.x + r, sp.y + r, pal_RGBA(255, 255, 255, 200));
		}
		wzText(std::to_string(i + 1), sp.x + c.L(7), sp.y - c.L(9), sel ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(255, 226, 120, 255), TK_ROW, c.S);
	}
	pie_SetGeometricOffset(static_cast<int>(screenWidth) / 2, static_cast<int>(screenHeight) / 2);
	// Hinweis
	wzText("Left drag = turn   Right drag = move view   Wheel = zoom   Right-click a slot = remove", bx + c.L(8), by + bh - c.L(14), pal_RGBA(150, 195, 225, 220), TK_ROW, c.S, bw - c.L(12));
	if (s_def.parts.empty()) { wzText("Pick a chassis from the hull list", bx + c.L(70), by + c.L(150), pal_RGBA(120, 230, 255, 255), TK_BUTTON, c.S); }
}

void drawScreen(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	(void)psWidget; (void)xOffset; (void)yOffset;
	const Ctx c = ctx();
	const float S = c.S;
	pie_BoxFill(0, 0, static_cast<int>(screenWidth), static_cast<int>(screenHeight), pal_RGBA(4, 8, 18, 255));

	// ---- links: Rumpfliste
	groupFrame(c, 2, 2, 162, 336, "HULLS");
	wzText("click = add", c.X(76), c.Y(6), pal_RGBA(150, 195, 225, 230), TK_ROW, S);
	listFrame(c.X(6), c.Y(20), c.L(154), c.L(BODY_ROWS * BODY_ROW_H + 2));
	for (int i = 0; i < BODY_ROWS; ++i)
	{
		const int idx = s_bodyScroll + i;
		if (idx >= static_cast<int>(s_bodies.size())) { break; }
		const BODY_STATS &b = asBodyStats[s_bodies[static_cast<size_t>(idx)]];
		const int ry = c.Y(21.f + i * BODY_ROW_H);
		wzText(fitW(getLocalizedStatsName(&b), c.L(146), TK_ROW, S), c.X(10), ry + c.L(2), WZCOL_FORM_TEXT, TK_ROW, S);
	}

	// ---- rechts: Teile
	groupFrame(c, 476, 2, 162, 128, "PARTS");
	wzText("1 = chassis", c.X(536), c.Y(6), pal_RGBA(150, 195, 225, 230), TK_ROW, S);
	listFrame(c.X(480), c.Y(PARTS_Y), c.L(154), c.L(PART_ROWS * LIST_ROW_H + 2));
	for (int i = 0; i < PART_ROWS; ++i)
	{
		const int idx = s_partScroll + i;
		if (idx >= static_cast<int>(s_def.parts.size())) { break; }
		const int ry = c.Y(PARTS_Y + 1.f + i * LIST_ROW_H);
		const bool sel = idx == s_sel && s_selMount < 0;
		if (sel) { pie_UniTransBoxFill(c.X(481), ry, c.X(633), ry + c.L(LIST_ROW_H) - 1, pal_RGBA(40, 110, 190, 170)); }
		int nSlots = 0;
		for (const Link &l : s_links) { if (l.part == idx) { ++nSlots; } }
		const std::string t = std::to_string(idx + 1) + "  " + bodyName(s_def.parts[static_cast<size_t>(idx)].body) + (nSlots > 0 ? "  [" + std::to_string(nSlots) + "]" : "");
		wzText(fitW(t, c.L(146), TK_ROW, S), c.X(484), ry + c.L(1), sel ? pal_RGBA(255, 255, 255, 255) : WZCOL_FORM_TEXT, TK_ROW, S);
	}

	// ---- rechts: Waffenplaetze
	if (!s_def.mounts.empty()) { s_mountScroll = std::max(0, std::min(s_mountScroll, static_cast<int>(s_def.mounts.size()) - MOUNT_ROWS)); }
	else { s_mountScroll = 0; }
	groupFrame(c, 476, SLOTS_HEAD_Y - 4, 162, 150, "WEAPON SLOTS " + std::to_string(s_def.mounts.size()) + "/" + std::to_string(modcompbody::MAX_SLOTS));
	listFrame(c.X(480), c.Y(SLOTS_Y), c.L(154), c.L(MOUNT_ROWS * LIST_ROW_H + 2));
	for (int r = 0; r < MOUNT_ROWS; ++r)
	{
		const int i = s_mountScroll + r;
		if (i >= static_cast<int>(s_def.mounts.size())) { break; }
		const int ry = c.Y(SLOTS_Y + 1.f + r * LIST_ROW_H);
		const bool sel = i == s_selMount;
		if (sel) { pie_UniTransBoxFill(c.X(481), ry, c.X(633), ry + c.L(LIST_ROW_H) - 1, pal_RGBA(150, 110, 20, 170)); }
		std::string t = "Slot " + std::to_string(i + 1);
		if (linkValid(static_cast<size_t>(i))) { t += "  on " + std::to_string(s_links[static_cast<size_t>(i)].part + 1) + " " + bodyName(s_def.parts[static_cast<size_t>(s_links[static_cast<size_t>(i)].part)].body); }
		else { t += "  (free)"; }
		wzText(fitW(t, c.L(146), TK_ROW, S), c.X(484), ry + c.L(1), sel ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(255, 226, 140, 255), TK_ROW, S);
	}

	// ---- rechts unten: Werte
	{
		groupFrame(c, 476, VALUES_Y - 2, 162, 58, "VALUES");
		const modcompbody::Totals t = modcompbody::totals(s_def);
		char buf[160];
		snprintf(buf, sizeof(buf), "HP %u   Armour %u/%u", t.hp, t.armour, t.thermal);
		wzText(buf, c.X(482), c.Y(VALUES_Y + 13), WZCOL_TEXT_BRIGHT, TK_ROW, S, c.L(150));
		snprintf(buf, sizeof(buf), "Weight %u   Engine %u", t.weight, t.power);
		wzText(buf, c.X(482), c.Y(VALUES_Y + 25), WZCOL_TEXT_BRIGHT, TK_ROW, S, c.L(150));
		snprintf(buf, sizeof(buf), "Cost %u  Build %u  %s", t.cost, t.buildPoints, sizeName(t.size));
		wzText(buf, c.X(482), c.Y(VALUES_Y + 37), WZCOL_TEXT_BRIGHT, TK_ROW, S, c.L(150));
	}

	drawBay(c);

	// ---- Auswahl-Zeile
	{
		std::string t;
		char buf[200];
		if (s_selMount >= 0 && s_selMount < static_cast<int>(s_def.mounts.size()))
		{
			const glm::vec3 &p = s_def.mounts[static_cast<size_t>(s_selMount)].pos;
			snprintf(buf, sizeof(buf), "Selected: weapon slot %d    x %.1f   y %.1f   z %.1f    facing %.0f deg (the weapon turns 180 deg)", s_selMount + 1, p.x, p.y, p.z, s_def.mounts[static_cast<size_t>(s_selMount)].yaw);
			t = buf;
		}
		else if (s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size()))
		{
			const Part &p = s_def.parts[static_cast<size_t>(s_sel)];
			snprintf(buf, sizeof(buf), "Selected: part %d %s    x %.1f   y %.1f   z %.1f    yaw %.0f   pitch %.0f   roll %.0f%s%s%s", s_sel + 1, bodyName(p.body).c_str(), p.pos.x, p.pos.y, p.pos.z, p.rot.x, p.rot.y, p.rot.z,
			         (p.flip & 1) ? "   flip X" : "", (p.flip & 2) ? "   flip Y" : "", (p.flip & 4) ? "   flip Z" : "");
			t = buf;
		}
		else { t = "Nothing selected - click a part or a weapon slot (list or 3D view)"; }
		wzText(fitW(t, c.L(628), TK_ROW, S), c.X(6), c.Y(339), pal_RGBA(255, 226, 140, 255), TK_ROW, S);
	}

	// ---- Gruppen unten
	groupFrame(c, 2, GROUP_Y - 2, 204, 46, "MOVE");
	wzText("step", c.X(84), c.Y(GROUP_Y + 3), pal_RGBA(150, 195, 225, 230), TK_ROW, S);
	groupFrame(c, 206, GROUP_Y - 2, 238, 46, "ROTATE");
	wzText("angle", c.X(300), c.Y(GROUP_Y + 3), pal_RGBA(150, 195, 225, 230), TK_ROW, S);
	groupFrame(c, 444, GROUP_Y - 2, 194, 46, "PART");
	groupFrame(c, 2, HULL_Y - 2, 442, 56, "SUPER HULL");
	if (s_dirty) { wzText("unsaved changes", c.X(12), c.Y(HULL_Y + 38), pal_RGBA(255, 190, 90, 255), TK_ROW, S); }
	groupFrame(c, 444, HULL_Y - 2, 194, 56, "VIEW");

	if (!s_status.empty() && realTime < s_statusUntil)
	{
		wzText(fitW(s_status, c.L(628), TK_ROW, S), c.X(6), c.Y(462), s_statusBad ? pal_RGBA(255, 130, 100, 255) : pal_RGBA(120, 255, 170, 255), TK_ROW, S);
	}
}

/// WZ-Knopf: Schattenkasten wie W_BUTTON (hell oben links, dunkel unten rechts; gedrueckt umgekehrt), Rahmen beim Ueberfahren, WZ-Schrift
void btnDisplay(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const size_t k = psWidget->UserData;
	if (k >= s_btns.size()) { return; }
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int x1 = x0 + psWidget->width(), y1 = y0 + psWidget->height();
	const bool active = s_btns[k].active ? s_btns[k].active() : false;
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	const PIELIGHT fill = active ? pal_RGBA(40, 86, 150, 255) : WZCOL_FORM_BACKGROUND;
	iV_ShadowBox(x0, y0, x1, y1, 0, down ? WZCOL_FORM_DARK : WZCOL_FORM_LIGHT, down ? WZCOL_FORM_LIGHT : WZCOL_FORM_DARK, fill);
	if (hl) { iV_Box(x0 + 2, y0 + 2, x1 - 3, y1 - 3, WZCOL_FORM_HILITE); }
	const float S = ctx().S;
	const std::string &lab = s_btns[k].label;
	WzText &t = cachedText(lab, fontFor(TK_BUTTON, S));
	const int tx = x0 + std::max(1, (psWidget->width() - t.width()) / 2) + (down ? 1 : 0);
	const int ty = y0 + (psWidget->height() - t.lineSize()) / 2 - t.aboveBase() + (down ? 1 : 0);
	t.render(static_cast<float>(tx), static_cast<float>(ty), (hl || active) ? WZCOL_TEXT_BRIGHT : WZCOL_FORM_TEXT, 0.f, psWidget->width() - 2);
}

void hotDisplay(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int x1 = x0 + psWidget->width() - 1, y1 = y0 + psWidget->height() - 1;
	if ((b->getState() & WBUT_HIGHLIGHT) != 0) { iV_Box(x0, y0, x1, y1, WZCOL_FORM_HILITE); }
	if ((b->getState() & WBUT_DOWN) != 0) { pie_UniTransBoxFill(x0, y0, x1, y1, pal_RGBA(255, 255, 255, 50)); }
}

// ---------------------------------------------------------------- Aufbau
/// onClick bekommt true bei Rechtsklick (nur wenn secondary gesetzt ist)
void addBtn(const std::shared_ptr<WIDGET> &parent, float x, float y, float w, float h, const std::string &label, const char *tip, std::function<void(bool)> onClick,
            std::function<bool()> active = nullptr, bool invisible = false, bool secondary = false)
{
	Btn b;
	b.x = x; b.y = y; b.wdt = w; b.hgt = h; b.label = label; b.active = std::move(active);
	b.w = std::make_shared<W_BUTTON>();
	parent->attach(b.w);
	if (secondary) { b.w->style |= WBUT_SECONDARY; }
	b.w->displayFunction = invisible ? hotDisplay : btnDisplay;
	b.w->UserData = static_cast<UDWORD>(s_btns.size());
	if (tip != nullptr) { b.w->setTip(tip); }
	b.w->addOnClickHandler([onClick](W_BUTTON &btn) {
		const bool right = btn.getOnClickButtonPressed() == WKEY_SECONDARY;
		widgScheduleTask([onClick, right]() { onClick(right); });
	});
	s_btns.push_back(b);
}

void addBtn0(const std::shared_ptr<WIDGET> &parent, float x, float y, float w, float h, const std::string &label, const char *tip, std::function<void()> onClick,
             std::function<bool()> active = nullptr)
{
	addBtn(parent, x, y, w, h, label, tip, [onClick](bool) { onClick(); }, std::move(active));
}

void layout()
{
	if (!s_root) { return; }
	const Ctx c = ctx();
	for (Btn &b : s_btns) { b.w->setGeometry(c.X(b.x), c.Y(b.y), std::max(1, c.L(b.wdt)), std::max(1, c.L(b.hgt))); }
	if (s_nameBox) { s_nameBox->setGeometry(c.X(10), c.Y(HULL_Y + 15), c.L(150), c.L(18)); }
	s_lastW = static_cast<int>(screenWidth);
	s_lastH = static_cast<int>(screenHeight);
}

bool partSelected()
{
	return s_sel >= 0 && s_sel < static_cast<int>(s_def.parts.size()) && s_selMount < 0;
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

	// ---- Rumpfliste (unsichtbare Zeilenknoepfe)
	for (int i = 0; i < BODY_ROWS; ++i)
	{
		addBtn(parent, 7, 21.f + i * BODY_ROW_H, 152, BODY_ROW_H, "", "Add this hull as a new part - it brings its own weapon slots (the first part is the chassis and brings the propulsion)", [i](bool) {
			const size_t idx = static_cast<size_t>(s_bodyScroll + i);
			if (idx < s_bodies.size()) { addPart(s_bodies[idx]); }
		}, nullptr, true);
	}
	addBtn0(parent, 6, 318, 75, 16, "Up", "Scroll the hull list up", []() { s_bodyScroll = std::max(0, s_bodyScroll - 10); });
	addBtn0(parent, 85, 318, 75, 16, "Down", "Scroll the hull list down", []() { s_bodyScroll = std::min(std::max(0, static_cast<int>(s_bodies.size()) - BODY_ROWS), s_bodyScroll + 10); });

	// ---- Teile (Links = waehlen, Rechts = entfernen)
	for (int i = 0; i < PART_ROWS; ++i)
	{
		addBtn(parent, 481, PARTS_Y + 1.f + i * LIST_ROW_H, 152, LIST_ROW_H, "", "Left-click: select this part.  Right-click: remove it (with its weapon slots)", [i](bool right) {
			const int idx = s_partScroll + i;
			if (idx >= static_cast<int>(s_def.parts.size())) { return; }
			if (right) { removePart(idx); } else { selectPart(idx); }
		}, nullptr, true, true);
	}
	// ---- Waffenplaetze (Links = waehlen, Rechts = entfernen)
	for (int i = 0; i < MOUNT_ROWS; ++i)
	{
		addBtn(parent, 481, SLOTS_Y + 1.f + i * LIST_ROW_H, 152, LIST_ROW_H, "", "Left-click: select this weapon slot (MOVE/ROTATE then act on it).  Right-click: remove it", [i](bool right) {
			const int idx = s_mountScroll + i;
			if (idx >= static_cast<int>(s_def.mounts.size())) { return; }
			if (right) { removeSlot(static_cast<size_t>(idx)); play("mfui_rattle.ogg"); } else { selectMount(idx); }
		}, nullptr, true, true);
	}
	addBtn0(parent, 598, SLOTS_HEAD_Y - 2, 17, 12, "^", "Scroll the weapon slots up", []() { s_mountScroll = std::max(0, s_mountScroll - MOUNT_ROWS); });
	addBtn0(parent, 617, SLOTS_HEAD_Y - 2, 17, 12, "v", "Scroll the weapon slots down", []() { s_mountScroll += MOUNT_ROWS; });
	addBtn0(parent, 480, SLOTS_Y + MOUNT_ROWS * LIST_ROW_H + 6, 50, 16, "+ Slot", "Add a free weapon slot on the selected part", []() { addMount(); });
	addBtn0(parent, 532, SLOTS_Y + MOUNT_ROWS * LIST_ROW_H + 6, 50, 16, "- Slot", "Remove the selected weapon slot", []() {
		if (s_selMount >= 0) { deleteSelected(); } else if (!s_def.mounts.empty()) { removeSlot(s_def.mounts.size() - 1); }
	});
	addBtn0(parent, 584, SLOTS_Y + MOUNT_ROWS * LIST_ROW_H + 6, 50, 16, "Parts", "Back to editing the selected part", []() { s_selMount = -1; if (s_sel < 0 && !s_def.parts.empty()) { s_sel = 0; } });

	// ---- VERSCHIEBEN
	static const char *stepLbl[3] = { "0.5", "2", "8" };
	for (int k = 0; k < 3; ++k) { addBtn0(parent, 112.f + k * 30.f, GROUP_Y + 1, 28, 15, stepLbl[k], "Move step (Shift = finest step)", [k]() { s_step = k; }, [k]() { return s_step == k; }); }
	struct MB { const char *l; const char *tip; int axis; float sign; };
	static const MB mv[6] = {
		{ "Left", "Move left (arrow left)", 0, -1.f }, { "Right", "Move right (arrow right)", 0, 1.f },
		{ "Down", "Move down (page down)", 1, -1.f }, { "Up", "Move up (page up)", 1, 1.f },
		{ "Fwd", "Move forward (arrow up)", 2, -1.f }, { "Back", "Move back (arrow down)", 2, 1.f },
	};
	for (int k = 0; k < 6; ++k) { const MB m = mv[k]; addBtn0(parent, 6.f + k * 33.f, GROUP_Y + 20, 31, 20, m.l, m.tip, [m]() { move(m.axis, m.sign); }); }
	// ---- DREHEN
	static const char *angLbl[3] = { "5", "15", "90" };
	for (int k = 0; k < 3; ++k) { addBtn0(parent, 340.f + k * 34.f, GROUP_Y + 1, 32, 15, angLbl[k], "Rotation step in degrees (Shift = 1 degree)", [k]() { s_angle = k; }, [k]() { return s_angle == k; }); }
	static const MB rt[6] = {
		{ "Yaw <", "Turn left (Q) - for a weapon slot: its facing", 0, 1.f }, { "Yaw >", "Turn right (E) - for a weapon slot: its facing", 0, -1.f },
		{ "Nose v", "Tilt nose down (F)", 1, -1.f }, { "Nose ^", "Tilt nose up (R)", 1, 1.f },
		{ "Roll <", "Roll left (Z)", 2, -1.f }, { "Roll >", "Roll right (C)", 2, 1.f },
	};
	for (int k = 0; k < 6; ++k) { const MB m = rt[k]; addBtn0(parent, 210.f + k * 39.f, GROUP_Y + 20, 37, 20, m.l, m.tip, [m]() { rotate(m.axis, m.sign); }); }
	// ---- TEIL (oben neben der Ueberschrift: Kopieren/Spiegeln/Entfernen; unten: Zuruecksetzen und Spiegeln der Form)
	addBtn0(parent, 484, GROUP_Y + 1, 49, 15, "Copy", "Copy the selected part (with its weapon slots) or weapon slot", []() { duplicatePart(false); });
	addBtn0(parent, 535, GROUP_Y + 1, 49, 15, "Mirror", "Copy the selected part (or slot) mirrored to the other side", []() { duplicatePart(true); });
	addBtn0(parent, 586, GROUP_Y + 1, 48, 15, "Remove", "Remove the selected part (with its weapon slots) or weapon slot (Del)", []() { deleteSelected(); });
	addBtn0(parent, 448, GROUP_Y + 20, 46, 20, "Reset", "Reset rotation and flips of the selected part (a slot goes back to its natural place)", []() { resetSelected(); });
	addBtn0(parent, 496, GROUP_Y + 20, 45, 20, "Flip X", "Mirror the part's shape left/right", []() { flipSelected(1); }, []() { return partSelected() && (s_def.parts[static_cast<size_t>(s_sel)].flip & 1); });
	addBtn0(parent, 543, GROUP_Y + 20, 45, 20, "Flip Y", "Mirror the part's shape upside down", []() { flipSelected(2); }, []() { return partSelected() && (s_def.parts[static_cast<size_t>(s_sel)].flip & 2); });
	addBtn0(parent, 590, GROUP_Y + 20, 44, 20, "Flip Z", "Mirror the part's shape front/back", []() { flipSelected(4); }, []() { return partSelected() && (s_def.parts[static_cast<size_t>(s_sel)].flip & 4); });
	// ---- UEBER-RUMPF
	addBtn0(parent, 166, HULL_Y + 15, 44, 18, "New", "Start a new super hull", []() { newHull(); });
	addBtn0(parent, 212, HULL_Y + 15, 52, 18, "< Load", "Load the previous saved super hull", []() { loadCycle(-1); });
	addBtn0(parent, 266, HULL_Y + 15, 52, 18, "Load >", "Load the next saved super hull", []() { loadCycle(1); });
	addBtn0(parent, 320, HULL_Y + 15, 56, 18, "Save", "Save: the super hull is written to your user folder and appears in the hull list everywhere", []() { doSave(); });
	addBtn0(parent, 378, HULL_Y + 15, 60, 18, "Delete", "Delete this super hull (old savegames keep working)", []() { doDeleteHull(); });
	// ---- ANSICHT
	addBtn0(parent, 448, HULL_Y + 15, 45, 16, "Front", "View from the front", []() { s_yaw = 180.f; s_pitch = 8.f; });
	addBtn0(parent, 495, HULL_Y + 15, 45, 16, "Side", "View from the side", []() { s_yaw = 270.f; s_pitch = 4.f; });
	addBtn0(parent, 542, HULL_Y + 15, 45, 16, "Top", "View from above", []() { s_yaw = 180.f; s_pitch = 85.f; });
	addBtn0(parent, 589, HULL_Y + 15, 45, 16, "Auto", "Turn the view automatically", []() { s_autoRot = !s_autoRot; }, []() { return s_autoRot; });
	addBtn0(parent, 448, HULL_Y + 34, 92, 16, "Reset view", "Centre the view again (zoom and position)", []() { s_zoom = 1.f; s_panX = 0.f; s_panY = 0.f; s_yaw = 215.f; s_pitch = 24.f; });
	addBtn0(parent, 542, HULL_Y + 34, 92, 16, "Close", "Close the hull forge", []() { close(); });

	// ---- Name
	s_nameBox = std::make_shared<W_EDITBOX>();
	root->attach(s_nameBox);
	s_nameBox->setBoxColours(WZCOL_FORM_LIGHT, WZCOL_FORM_HILITE, WZCOL_FORM_BACKGROUND);
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
	return mx >= c.X(BAY_X) && mx <= c.X(BAY_X + BAY_W) && my >= c.Y(BAY_Y) && my <= c.Y(BAY_Y + BAY_H);
}

bool inBay(int x, int y)
{
	const Ctx c = ctx();
	return x >= c.X(BAY_X) && x <= c.X(BAY_X + BAY_W) && y >= c.Y(BAY_Y) && y <= c.Y(BAY_Y + BAY_H);
}

void dragTo(int x, int y)
{
	if (s_dragBtn < 0) { return; }
	if (!s_moved && (std::abs(x - s_pressX) > 3 || std::abs(y - s_pressY) > 3))
	{
		s_moved = true;
		s_drag = s_dragBtn == 0 ? DRAG_TURN : DRAG_PAN;
		s_autoRot = false;
	}
	if (s_drag == DRAG_TURN)
	{
		s_yaw += static_cast<float>(x - s_lastX) * 0.5f;
		s_pitch = std::clamp(s_pitch + static_cast<float>(y - s_lastY) * 0.35f, -10.f, 89.f);
	}
	else if (s_drag == DRAG_PAN)
	{
		s_panX += static_cast<float>(x - s_lastX);
		s_panY += static_cast<float>(y - s_lastY);
	}
	s_lastX = x; s_lastY = y;
}

void dragEnd(int x, int y)
{
	if (!s_moved)
	{
		const int m = mountAt(x, y);
		if (s_dragBtn == 0)
		{
			if (m >= 0) { selectMount(m); }
			else { const int p = partAt(x, y); if (p >= 0) { selectPart(p); } }
		}
		else if (s_dragBtn == 1 && m >= 0)
		{
			removeSlot(static_cast<size_t>(m));
			play("mfui_rattle.ogg");
			setStatus("Weapon slot removed.");
		}
	}
	s_dragBtn = -1;
	s_drag = DRAG_NONE;
	s_moved = false;
}

/// Maus in der 3D-Ansicht: links ziehen = drehen, rechts/Mitte ziehen = verschieben; Klick links = Platz/Teil waehlen,
/// Klick rechts auf einen Platz = entfernen.
/// Die Vollbild-Form leert jedes Bild den Eingabepuffer (gehaltene Tasten gelten danach als losgelassen) - darum zaehlen hier die
/// rohen Druck-/Loslass-Ereignisse dieses Bildes (inputGetClicks), gehalten wird selbst mitgefuehrt.
void bayMouse()
{
	for (const MousePress &p : inputGetClicks())
	{
		const int b = p.key == MOUSE_LMB ? 0 : (p.key == MOUSE_RMB ? 1 : (p.key == MOUSE_MMB ? 2 : -1));
		if (b < 0) { continue; }
		if (p.action == MousePress::Press)
		{
			if (s_dragBtn < 0 && inBay(p.pos.x, p.pos.y))
			{
				s_dragBtn = b; s_pressX = s_lastX = p.pos.x; s_pressY = s_lastY = p.pos.y; s_moved = false; s_drag = DRAG_NONE;
			}
		}
		else if (p.action == MousePress::Release && b == s_dragBtn)
		{
			dragTo(p.pos.x, p.pos.y);
			dragEnd(p.pos.x, p.pos.y);
		}
	}
	if (s_dragBtn >= 0) { dragTo(mouseX(), mouseY()); }
}

} // namespace

bool isOpen()
{
	return s_open;
}

bool open()
{
	if (s_open) { return true; }
	if (s_def.parts.empty() && s_def.name.empty()) { s_def.name = "New hull"; }
	if (s_links.size() != s_def.mounts.size()) { inferLinks(); }
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
	s_texts.clear();
	s_dragBtn = -1;
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
	if (static_cast<int>(screenWidth) != s_lastW || static_cast<int>(screenHeight) != s_lastH) { layout(); s_texts.clear(); }
	if (s_autoRot) { s_yaw = std::fmod(s_yaw + 0.35f, 360.f); }
	// Mausrad: in der Ansicht Zoom, sonst Listen blaettern
	const Vector2i wheel = getMouseWheelSpeed();
	const Ctx c = ctx();
	const int mx = mouseX(), my = mouseY();
	if (wheel.y != 0)
	{
		if (mouseInBay()) { s_zoom = std::clamp(s_zoom * (wheel.y > 0 ? 1.12f : 1.f / 1.12f), 0.3f, 6.f); }
		else if (mx >= c.X(6) && mx <= c.X(160) && my >= c.Y(20) && my <= c.Y(20.f + BODY_ROWS * BODY_ROW_H))
		{
			s_bodyScroll = std::clamp(s_bodyScroll + (wheel.y > 0 ? -3 : 3), 0, std::max(0, static_cast<int>(s_bodies.size()) - BODY_ROWS));
		}
		else if (mx >= c.X(480) && my >= c.Y(PARTS_Y) && my <= c.Y(PARTS_Y + PART_ROWS * LIST_ROW_H))
		{
			s_partScroll = std::clamp(s_partScroll + (wheel.y > 0 ? -1 : 1), 0, std::max(0, static_cast<int>(s_def.parts.size()) - PART_ROWS));
		}
		else if (mx >= c.X(480) && my >= c.Y(SLOTS_Y) && my <= c.Y(SLOTS_Y + MOUNT_ROWS * LIST_ROW_H))
		{
			s_mountScroll = std::clamp(s_mountScroll + (wheel.y > 0 ? -1 : 1), 0, std::max(0, static_cast<int>(s_def.mounts.size()) - MOUNT_ROWS));
		}
	}
	bayMouse();
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
	s_texts.clear();
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
