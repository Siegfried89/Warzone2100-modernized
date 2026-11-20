// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 20.11. 17:46): Anzeigen zur Oel-Wirtschaft - siehe modoilui.h
#include "modoilui.h"

#include <physfs.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/button.h"
#include "lib/ivis_opengl/bitimage.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "display.h"
#include "edit3d.h"
#include "map.h"
#include "game_world.h"
#include "hci.h"
#include "intfac.h"
#include "intimage.h"
#include "moddesigner.h"
#include "modgrid.h"
#include "modoil.h"
#include "modui.h"
#include "modwzhd.h"
#include "structure.h"

namespace modoilui
{
namespace
{
// ---------------------------------------------------------------- Sinnbilder ueber den Gebaeuden

bool projectWorld(int wx, int wy, int wz, const glm::mat4 &m, Vector2i &out)
{
	const Vector3i pos(wx, wz, -wy);
	return pie_RotateProjectWithPerspective(&pos, m, &out) > 0;
}

PIELIGHT withAlpha(PIELIGHT c, int a)
{
	c.byte.a = static_cast<UBYTE>(std::clamp(a, 0, 255));
	return c;
}

/// Plakette: dunkle Scheibe mit farbigem Doppelrand (WZ-Formularfarben)
void badge(int cx, int cy, int r, PIELIGHT rim, int alpha)
{
	modui::disc(cx, cy, r, withAlpha(pal_RGBA(16, 18, 40, 255), alpha * 225 / 255));
	modui::ring(cx, cy, r, withAlpha(rim, alpha));
	modui::ring(cx, cy, r - 1, withAlpha(rim, alpha));
}

/// "Kein Strom": WZ-Blitz (Einheiten-Editor-Sinnbild) rot durchgestrichen
void iconNoPower(int cx, int cy, int r, int alpha)
{
	badge(cx, cy, r, pal_RGBA(255, 50, 40, 255), alpha);
	const int iw = iV_GetImageWidth(IntImages, IMAGE_DES_POWER), ih = iV_GetImageHeight(IntImages, IMAGE_DES_POWER);
	const float k = static_cast<float>(r) * 1.35f / static_cast<float>(std::max(iw, ih));
	const int dw = std::max(4, static_cast<int>(iw * k)), dh = std::max(4, static_cast<int>(ih * k));
	modwzhd::drawIntfac(IMAGE_DES_POWER, cx - dw / 2, cy - dh / 2, dw, dh, true, static_cast<unsigned char>(alpha));
	const PIELIGHT red = withAlpha(pal_RGBA(255, 40, 30, 255), alpha);
	const int d = r * 7 / 10;
	for (int o = -1; o <= 1; ++o)
	{
		iV_Line(cx - d + o, cy + d, cx + d + o, cy - d, red);
	}
}

/// "Wenig Oel": Oelfass (wie in der Tankstellen-Anzeige) in einer Plakette
void iconLowOil(int cx, int cy, int r, bool empty, int alpha)
{
	const PIELIGHT rim = empty ? pal_RGBA(255, 50, 40, 255) : pal_RGBA(255, 160, 30, 255);
	badge(cx, cy, r, rim, alpha);
	const int bw = r, bh = r * 13 / 10;
	const int x0 = cx - bw / 2, y0 = cy - bh / 2;
	const PIELIGHT body = withAlpha(empty ? pal_RGBA(200, 40, 30, 255) : pal_RGBA(230, 130, 25, 255), alpha);
	const PIELIGHT hoop = withAlpha(pal_RGBA(40, 20, 10, 255), alpha);
	pie_BoxFill_alpha(x0, y0 + 2, x0 + bw, y0 + bh, body);
	pie_BoxFill_alpha(x0 + 1, y0, x0 + bw - 1, y0 + 3, withAlpha(pal_RGBA(255, 210, 120, 255), alpha));
	pie_BoxFill_alpha(x0, y0 + bh / 3, x0 + bw, y0 + bh / 3 + 1, hoop);
	pie_BoxFill_alpha(x0, y0 + bh * 2 / 3, x0 + bw, y0 + bh * 2 / 3 + 1, hoop);
	pie_BoxFill_alpha(x0 + 2, y0 + 3, x0 + 3, y0 + bh - 1, withAlpha(pal_RGBA(255, 220, 160, 255), alpha / 2));
}

int structureTop(const STRUCTURE *s)
{
	int top = 120;
	if (s->sDisplay.imd != nullptr && s->sDisplay.imd->displayModel() != nullptr)
	{
		top = std::max(40, s->sDisplay.imd->displayModel()->max.y);
	}
	return top + 36;
}

// ---------------------------------------------------------------- Oel-Fenster

enum ImgFleet { IC_BTN_UP = 9, IC_BTN_DOWN = 10, IC_BTN_HI = 11 };
constexpr int BTN_W = 36, BTN_H = 24, PANEL_W = 300, ROW_H = 17, PAD = 6;

std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_panel;
std::shared_ptr<W_BUTTON> s_toggle;
IMAGEFILE *s_img = nullptr;
bool s_tried = false;
bool s_on = true;   // Standard: ein

bool imagesReady()
{
	if (!s_tried)
	{
		s_tried = true;
		if (PHYSFS_exists("images/modfleets.img")) { s_img = iV_LoadImageFile("images/modfleets.img"); }
	}
	return s_img != nullptr && s_img->imageDefs.size() > static_cast<size_t>(IC_BTN_HI);
}

void drawImg(int id, int x, int y, int w, int h)
{
	if (!imagesReady()) { return; }
	iV_DrawImageFileAnisotropic(s_img, static_cast<UWORD>(id), x, y, Vector2f(static_cast<float>(w), static_cast<float>(h)), defaultProjectionMatrix(), 255);
}

std::string mmss(int secs)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "%d:%02d", std::max(0, secs) / 60, std::max(0, secs) % 60);
	return buf;
}

std::string of(int a, int b) { return std::to_string(a) + "/" + std::to_string(b); }

enum Lamp { L_OFF, L_OK, L_WARN, L_BAD };

PIELIGHT lampColour(Lamp l)
{
	switch (l)
	{
	case L_OK: return pal_RGBA(70, 230, 90, 255);
	case L_WARN: return pal_RGBA(255, 200, 40, 255);
	case L_BAD: return pal_RGBA(255, 60, 50, 255);
	default: return pal_RGBA(90, 96, 120, 255);
	}
}

struct Row
{
	Lamp lamp;
	std::string name;
	std::string value;
	bool arrow;   // Pfeil zur naechsten Stufe darunter
};

std::vector<Row> rowsFor(const modoil::EconomyView &v)
{
	std::vector<Row> r;
	{
		Lamp l = v.derricks == 0 ? L_BAD : (v.derricksFull > 0 ? L_WARN : L_OK);
		std::string val = v.derricks == 0 ? std::string("keiner") : std::to_string(v.derricks) + " | F\xC3\xA4sser " + of(v.derrickBarrels, v.derrickCap);
		if (v.derricksFull > 0) { val += " (" + std::to_string(v.derricksFull) + " voll)"; }
		r.push_back({l, "\xC3\x96lt\xC3\xBCrme", val, true});
	}
	{
		Lamp l = (v.pickups > 0 || v.pipeDerricks > 0) ? L_OK : (v.derricks > 0 ? L_BAD : L_OFF);
		std::string val = "Pickups " + std::to_string(v.pickups);
		if (v.pickups > 0) { val += " (" + of(v.pickupBarrels, v.pickupCap) + ")"; }
		if (v.pipeDerricks > 0) { val += " | Rohr " + std::to_string(v.pipeDerricks); }
		r.push_back({l, "Transport", val, true});
	}
	{
		Lamp l = v.refineries == 0 ? (v.derricks > 0 ? L_BAD : L_OFF) : (v.refNoPower > 0 || (v.refCap > 0 && v.refIn >= v.refCap) ? L_WARN : L_OK);
		std::string val = v.refineries == 0 ? std::string("keine") : std::to_string(v.refineries) + " | ein " + of(v.refIn, v.refCap) + " aus " + of(v.refOut, v.refCap);
		if (v.refNoPower > 0) { val += " | Notbetrieb"; }
		r.push_back({l, "Raffinerie", val, true});
	}
	{
		Lamp l = v.tankers > 0 ? L_OK : (v.refineries > 0 ? L_BAD : L_OFF);
		std::string val = std::to_string(v.tankers);
		if (v.tankers > 0) { val += " | geladen " + of(v.tankerBarrels, v.tankerCap); }
		r.push_back({l, "\xC3\x96ltanker", val, true});
	}
	{
		Lamp l = v.stores > 0 ? L_OK : L_OFF;
		std::string val = v.stores == 0 ? std::string("keins (Bonus +10 %)") : std::to_string(v.stores) + " | " + of(v.storeBarrels, v.storeCap);
		r.push_back({l, "\xC3\x96llager", val, true});
	}
	{
		Lamp l = v.gens == 0 ? L_BAD : (v.minFuel == 0 ? L_BAD : (v.gensHungry > 0 ? L_WARN : L_OK));
		std::string val = v.gens == 0 ? std::string("keins") : std::to_string(v.gens);
		if (v.gens > 0 && v.minFuel >= 0) { val += " | Vorrat min. " + mmss(v.minFuel); }
		r.push_back({l, "Kraftwerke", val, true});
	}
	{
		// (20.11. 19:06) Umspannwerk ist Pflicht: das Endstueck, es versorgt ueber die Masten alle Gebaeude
		int fed = 0;
		const int subs = modgrid::substations(static_cast<int>(selectedPlayer), fed);
		Lamp l = subs == 0 ? L_BAD : (fed == 0 ? L_BAD : (fed < subs ? L_WARN : L_OK));
		std::string val = subs == 0 ? std::string("keins - kein Strom!") : std::to_string(subs) + " | mit Strom " + std::to_string(fed);
		r.push_back({l, "Umspannwerk", val, false});
	}
	{
		const int sp = modgrid::solarPanelsConnected(static_cast<int>(selectedPlayer));
		Lamp l = sp > 0 ? L_OK : L_OFF;
		std::string val = sp == 0 ? std::string("keine am Umspannwerk") : std::to_string(sp) + " Paneele | +" + std::to_string(sp * 6) + "/min (tags)";
		r.push_back({l, "Solar", val, false});
	}
	{
		Lamp l = v.stations == 0 ? L_OFF : (v.stationBarrels * 10 < v.stationCap * 3 ? L_WARN : L_OK);
		std::string val = v.stations == 0 ? std::string("keine") : std::to_string(v.stations) + " | " + of(v.stationBarrels, v.stationCap);
		r.push_back({l, "Tankstellen", val, false});
	}
	{
		Lamp l = (v.wastedLastMinute > 0 || (v.powerMax > 0 && v.power >= v.powerMax)) ? L_WARN : L_OK;
		std::string val = of(v.power, v.powerMax) + " | +" + std::to_string(v.incomeLastMinute) + "/min";
		if (v.wastedLastMinute > 0) { val += " | verfallen " + std::to_string(v.wastedLastMinute); }
		r.push_back({l, "Energie", val, false});
	}
	{
		Lamp l = v.buildingsDown > 0 ? L_BAD : L_OK;
		r.push_back({l, "Ohne Strom", v.buildingsDown == 0 ? std::string("nichts") : std::to_string(v.buildingsDown) + " Geb\xC3\xA4ude", false});
	}
	return r;
}

int panelHeight(size_t rows)
{
	return PAD * 2 + 16 + static_cast<int>(rows) * ROW_H + 6;
}

void drawPanel(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	if (!s_on || selectedPlayer >= MAX_PLAYERS) { return; }
	const modoil::EconomyView v = modoil::economyView(static_cast<int>(selectedPlayer));
	if (!v.active) { return; }
	const std::vector<Row> rows = rowsFor(v);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = panelHeight(rows.size());
	modui::wzPanel(x0, y0, x0 + w, y0 + h, "\xC3\x96lkreislauf", 1);
	int y = y0 + PAD + 16;
	const int nameX = x0 + PAD + 14, valX = x0 + PAD + 100;
	for (size_t i = 0; i < rows.size(); ++i)
	{
		const Row &r = rows[i];
		// Trennlinie vor dem zweiten Block (Endempfaenger und Ergebnis)
		if (i == 7)
		{
			iV_Line(x0 + PAD, y - 2, x0 + w - PAD, y - 2, WZCOL_FORM_DARK);
		}
		const int cy = y + ROW_H / 2 - 1;
		modui::disc(x0 + PAD + 5, cy, 4, lampColour(r.lamp));
		modui::ring(x0 + PAD + 5, cy, 4, pal_RGBA(0, 0, 0, 200));
		if (r.arrow && i + 1 < rows.size())
		{
			// kleiner Pfeil nach unten: die Faesser laufen zur naechsten Stufe
			const int ax = x0 + PAD + 5, ay = y + ROW_H - 3;
			iV_Line(ax, ay - 2, ax, ay + 3, WZCOL_TEXT_MEDIUM);
			iV_Line(ax - 2, ay + 1, ax, ay + 3, WZCOL_TEXT_MEDIUM);
			iV_Line(ax + 2, ay + 1, ax, ay + 3, WZCOL_TEXT_MEDIUM);
		}
		modui::text(r.name, nameX, y + 1, WZCOL_TEXT_BRIGHT, 0);
		modui::text(modui::fit(r.value, x0 + w - PAD - valX, 0), valX, y + 1, r.lamp == L_BAD ? pal_RGBA(255, 120, 100, 255) : WZCOL_TEXT_MEDIUM, 0);
		y += ROW_H;
	}
}

void drawToggle(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const bool down = s_on || (b->getState() & WBUT_DOWN) != 0;
	drawImg(down ? IC_BTN_DOWN : IC_BTN_UP, x0, y0, w, h);
	const int iw = iV_GetImageWidth(IntImages, IMAGE_DES_POWER), ih = iV_GetImageHeight(IntImages, IMAGE_DES_POWER);
	const float k = std::min(static_cast<float>(w - 8) / std::max(1, iw), static_cast<float>(h - 6) / std::max(1, ih));
	const int dw = std::max(1, static_cast<int>(iw * k)), dh = std::max(1, static_cast<int>(ih * k));
	modwzhd::drawIntfac(IMAGE_DES_POWER, x0 + (w - dw) / 2, y0 + (h - dh) / 2, dw, dh, true);
	if (b->isMouseOverWidget()) { drawImg(IC_BTN_HI, x0 - 1, y0 - 1, w + 2, h + 2); }
}

void ensureScreen()
{
	if (s_screen) { return; }
	auto screen = W_SCREEN::make();
	screen->psForm->hide();   // verborgene Wurzel = durchlaessig fuer Mausklicks
	auto panel = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(panel);
	panel->displayFunction = drawPanel;
	panel->setTransparentToClicks(true);
	auto t = std::make_shared<W_BUTTON>();
	screen->psForm->attach(t);
	t->displayFunction = drawToggle;
	t->setTip("\xC3\x96lkreislauf ein/aus");
	t->addOnClickHandler([](W_BUTTON &) { s_on = !s_on; });
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_panel = panel;
	s_toggle = t;
}
}

namespace
{
// (Wunsch 20.11. 19:06 "Beim Platzieren von Strommasten bereits Reichweitenradius anzeigen (blau)" / "Bei Oellager auch Radius anzeigen")
// Reichweiten wie in modgrid/modoil (Weltmass, 128 = 1 Feld)
constexpr int R_POLE_SUPPLY = 448;     // Verbraucher bis 3,5 Felder um den Mast
constexpr int R_POLE_LINK = 6 * 128;   // Mast an Mast bis 6 Felder
constexpr int R_SUB_SUPPLY = 5 * 128;  // Verbraucher bis 5 Felder um das Umspannwerk
constexpr int R_SOURCE_LINK = 576;     // Kraftwerk / Solarpaneel / Kommandozentrale an Mast bis 4,5 Felder
constexpr int R_STORE = 12 * 128;      // Oellager versorgt Kraftwerke bis 12 Felder

void ringAt(const glm::mat4 &m, int cx, int cy, int r, PIELIGHT col, bool dashed)
{
	std::vector<glm::ivec4> lines;
	Vector2i prev(0, 0);
	bool havePrev = false;
	const int n = 64;
	for (int k = 0; k <= n; k++)
	{
		const double a = k * 6.283185307 / n;
		const int wx = static_cast<int>(cx + std::cos(a) * r), wy = static_cast<int>(cy + std::sin(a) * r);
		Vector2i sp(0, 0);
		const bool ok = projectWorld(wx, wy, map_Height(gameWorld.map, wx, wy) + 6, m, sp);
		if (ok && havePrev && (!dashed || (k % 2) == 0))
		{
			lines.emplace_back(prev.x, prev.y, sp.x, sp.y);
			lines.emplace_back(prev.x, prev.y + 1, sp.x, sp.y + 1);
		}
		prev = sp;
		havePrev = ok;
	}
	if (!lines.empty()) { iV_Lines(lines, col); }
}

void drawPlacementRanges(const glm::mat4 &m)
{
	if ((buildState != BUILD3D_VALID && buildState != BUILD3D_POS) || sBuildDetails.psStats == nullptr || !sBuildDetails.psStats->hasType(STAT_STRUCTURE))
	{
		return;
	}
	const STRUCTURE_STATS *st = static_cast<const STRUCTURE_STATS *>(sBuildDetails.psStats);
	const bool pole = modgrid::isPole(st), sub = modgrid::isSubstation(st);
	const bool store = st->id.startsWith("A0BaBaOilStore");
	const bool source = st->type == REF_POWER_GEN || st->incomeProvide > 0;
	const bool consumer = st->requiresPower;
	if (!pole && !sub && !store && !source && !consumer) { return; }
	const int cx = world_coord(static_cast<int>(sBuildDetails.x)) + world_coord(static_cast<int>(sBuildDetails.width)) / 2;
	const int cy = world_coord(static_cast<int>(sBuildDetails.y)) + world_coord(static_cast<int>(sBuildDetails.height)) / 2;
	const PIELIGHT blue = pal_RGBA(70, 150, 255, 235), light = pal_RGBA(150, 210, 255, 200), dim = pal_RGBA(40, 95, 180, 150);
	// bestehende Versorgungsbereiche (gedimmt), damit man sieht, wo schon Strom hinkommt
	if (pole || sub || consumer)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[selectedPlayer])
		{
			if (s->died || s->status != SS_BUILT) { continue; }
			if (modgrid::isPole(s->pStructureType)) { ringAt(m, s->pos.x, s->pos.y, R_POLE_SUPPLY, dim, false); }
			else if (modgrid::isSubstation(s->pStructureType)) { ringAt(m, s->pos.x, s->pos.y, R_SUB_SUPPLY, dim, false); }
		}
	}
	if (pole) { ringAt(m, cx, cy, R_POLE_SUPPLY, blue, false); ringAt(m, cx, cy, R_POLE_LINK, light, true); }
	if (sub) { ringAt(m, cx, cy, R_SUB_SUPPLY, blue, false); ringAt(m, cx, cy, R_POLE_LINK, light, true); }
	if (source) { ringAt(m, cx, cy, R_SOURCE_LINK, light, true); }
	if (store) { ringAt(m, cx, cy, R_STORE, blue, false); }
}
}

void drawWorldIcons(const glm::mat4 &m)
{
	if (selectedPlayer < MAX_PLAYERS && modoil::manages(static_cast<int>(selectedPlayer))) { drawPlacementRanges(m); }
	if (selectedPlayer >= MAX_PLAYERS || !modoil::manages(static_cast<int>(selectedPlayer))) { return; }
	const float sc = modui::uiScale();
	const int r = static_cast<int>(13.f * sc);
	// Blinken: weich pulsierend (Anzeige, nicht Spielablauf)
	const double t = static_cast<double>(realTime) / 1000.0;
	const int pulse = 150 + static_cast<int>(105.0 * (0.5 + 0.5 * std::sin(t * 5.0)));
	const int pulseFast = 130 + static_cast<int>(125.0 * (0.5 + 0.5 * std::sin(t * 10.0)));
	for (const STRUCTURE *s : gameWorld.objects.structures[selectedPlayer])
	{
		if (s->died || s->status != SS_BUILT || !DrawnInLastFrame(s->sDisplay.frameNumber)) { continue; }
		const bool down = modgrid::isPoweredDown(s);
		const bool hungry = s->pStructureType->type == REF_POWER_GEN && modoil::generatorHungry(s);
		if (!down && !hungry) { continue; }
		Vector2i sp(0, 0);
		if (!projectWorld(s->pos.x, s->pos.y, s->pos.z + structureTop(s), m, sp)) { continue; }
		if (sp.x < -40 || sp.y < -40 || sp.x > static_cast<int>(pie_GetVideoBufferWidth()) + 40 || sp.y > static_cast<int>(pie_GetVideoBufferHeight()) + 40) { continue; }
		int cx = sp.x;
		if (down && hungry) { cx -= r + 2; }
		if (down) { iconNoPower(cx, sp.y, r, pulse); cx += 2 * r + 4; }
		if (hungry)
		{
			const bool empty = modoil::generatorFuel(s) <= 0;
			iconLowOil(cx, sp.y, r, empty, empty ? pulseFast : pulse);
		}
	}
}

void tick()
{
	const bool want = selectedPlayer < MAX_PLAYERS && modoil::manages(static_cast<int>(selectedPlayer)) && !moddesigner::isOpen() && intMode != INT_DESIGN;
	if (!want && !s_screen) { return; }
	ensureScreen();
	s_toggle->show(want);
	s_panel->show(want && s_on);
	if (!want) { return; }
	const modoil::EconomyView v = modoil::economyView(static_cast<int>(selectedPlayer));
	const int rows = static_cast<int>(rowsFor(v).size());
	const int x = static_cast<int>(screenWidth) - PANEL_W - 8, y = 44;
	s_toggle->setGeometry(x + PANEL_W - BTN_W, y, BTN_W, BTN_H);
	s_panel->setGeometry(x, y + BTN_H + 4, PANEL_W, panelHeight(static_cast<size_t>(rows)));
}

void shutdown()
{
	if (s_screen) { widgRemoveOverlayScreen(s_screen); }
	s_screen = nullptr; s_panel = nullptr; s_toggle = nullptr;
	if (s_img != nullptr) { iV_FreeImageFile(s_img); s_img = nullptr; }
	s_tried = false;
}
}
