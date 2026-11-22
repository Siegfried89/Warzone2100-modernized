// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 20.11. 17:46): Anzeigen zur Oel-Wirtschaft - siehe modoilui.h
#include "modoilui.h"
#include "modlayer.h"   // MOD (21.11.): Platz der Ebenen-Knoepfe

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
#include "modunitstats.h"
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
	pie_UniTransBoxFill(x0, y0 + 2, x0 + bw, y0 + bh, body);
	pie_UniTransBoxFill(x0 + 1, y0, x0 + bw - 1, y0 + 3, withAlpha(pal_RGBA(255, 210, 120, 255), alpha));
	pie_UniTransBoxFill(x0, y0 + bh / 3, x0 + bw, y0 + bh / 3 + 1, hoop);
	pie_UniTransBoxFill(x0, y0 + bh * 2 / 3, x0 + bw, y0 + bh * 2 / 3 + 1, hoop);
	pie_UniTransBoxFill(x0 + 2, y0 + 3, x0 + 3, y0 + bh - 1, withAlpha(pal_RGBA(255, 220, 160, 255), alpha / 2));
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
// MOD (Wunsch 21.11. 18:31 "die Oel-Uebersicht einfacher gestalten mit Icons im WZ-Stil und kuerzerer, aber immer noch informativer
// Beschreibung, die Probleme schnell erkennen laesst. Es soll nicht so viel vom Bildschirm einnehmen"): statt elf Textzeilen (300 Punkte
// breit) zwei Reihen Bildkacheln der echten Gebaeude/Fahrzeuge (images/modoil.img, oilicons.py) mit Statusrahmen (gruen/gelb/rot) und
// einer kurzen Zahl darunter; die Einzelheiten stehen im Tooltip der Kachel; unten EINE Zeile mit dem wichtigsten Problem.
constexpr int BTN_W = 36, BTN_H = 24, PAD = 5;
constexpr int TILE = 28, TGAP = 5, VAL_H = 10, TITLE_H = 12, PROB_H = 13;
constexpr int COLS = 6;
constexpr int PANEL_W = PAD * 2 + COLS * TILE + (COLS - 1) * TGAP;
constexpr int ROWS = 2;
constexpr int PANEL_H = PAD * 2 + TITLE_H + ROWS * (TILE + VAL_H + 3) + PROB_H;
enum OilImg { OI_DERRICK = 0, OI_PICKUP, OI_REFINERY, OI_TANKER, OI_STORE, OI_POWERGEN, OI_SUBSTATION, OI_SOLAR, OI_STATION, OI_COUNT,
              OI_POWER = 100, OI_NOPOWER };   // POWER/NOPOWER: WZ-Blitz aus der Oberflaeche

std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_panel;
std::shared_ptr<W_BUTTON> s_toggle;
std::vector<std::shared_ptr<W_BUTTON>> s_tiles;   // nur fuer die Tooltips (gezeichnet wird im Fenster)
bool s_on = true;   // Standard: ein

// MOD (20.11.): Bildliste mit der Werteleiste teilen (modunitstats laedt und gibt frei) - kein zweites Laden derselben Datei
IMAGEFILE *images()
{
	IMAGEFILE *img = modunitstats::fleetImages();
	return (img != nullptr && img->imageDefs.size() > static_cast<size_t>(IC_BTN_HI)) ? img : nullptr;
}

// (22.11.) Absturz beim zweiten Spielstart in derselben Sitzung (Warzone2100.RPT 22.11. 16:28, bind_textures mit ungueltiger Textur aus
// images/modoil.img): die Bildliste lag in einer Funktions-Statik und ueberlebte das Spielende, ihre Texturen aber nicht. Jetzt wie bei
// den anderen Bildlisten (modrosette, modunitstats): in shutdown() freigeben und beim naechsten Spiel neu laden.
IMAGEFILE *s_oilImg = nullptr;
bool s_oilTried = false;

IMAGEFILE *oilImages()
{
	IMAGEFILE *&s_img = s_oilImg;
	bool &s_tried = s_oilTried;
	if (!s_tried)
	{
		s_tried = true;
		if (PHYSFS_exists("images/modoil.img")) { s_img = iV_LoadImageFile("images/modoil.img"); }
	}
	return (s_img != nullptr && s_img->imageDefs.size() >= static_cast<size_t>(OI_COUNT)) ? s_img : nullptr;
}

void drawImg(int id, int x, int y, int w, int h)
{
	IMAGEFILE *img = images();
	if (img == nullptr) { return; }
	iV_DrawImageFileAnisotropic(img, static_cast<UWORD>(id), x, y, Vector2f(static_cast<float>(w), static_cast<float>(h)), defaultProjectionMatrix(), 255);
}

std::string mmss(int secs)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "%d:%02d", std::max(0, secs) / 60, std::max(0, secs) % 60);
	return buf;
}

std::string of(int a, int b) { return std::to_string(a) + "/" + std::to_string(b); }
std::string pct(int a, int b) { return b > 0 ? std::to_string(std::clamp(a * 100 / b, 0, 100)) + "%" : std::string("-"); }

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

struct Tile
{
	Lamp lamp;
	int icon;
	std::string value;     // kurz unter der Kachel
	std::string tip;       // Einzelheiten (Tooltip)
	std::string problem;   // kurze Problemmeldung (nur bei gelb/rot)
	int prio;              // Wichtigkeit des Problems (kleiner = wichtiger)
};

std::vector<Tile> tilesFor(const modoil::EconomyView &v)
{
	std::vector<Tile> t;
	// Reihe 1: der Oelkreislauf von links nach rechts
	{
		const Lamp l = v.derricks == 0 ? L_BAD : (v.derricksFull > 0 ? L_WARN : L_OK);
		std::string tip = "\xC3\x96lt\xC3\xBCrme: " + (v.derricks == 0 ? std::string("keiner") : std::to_string(v.derricks) + " | F\xC3\xA4sser " + of(v.derrickBarrels, v.derrickCap));
		if (v.derricksFull > 0) { tip += " | " + std::to_string(v.derricksFull) + " voll - es wird nichts abgeholt"; }
		const std::string prob = v.derricks == 0 ? "Keine \xC3\x96lt\xC3\xBCrme" : std::to_string(v.derricksFull) + " \xC3\x96lturm voll - mehr Pickups";
		t.push_back({l, OI_DERRICK, std::to_string(v.derricks) + (v.derricksFull > 0 ? "!" : ""), tip, prob, v.derricks == 0 ? 3 : 20});
	}
	{
		const Lamp l = (v.pickups > 0 || v.pipeDerricks > 0) ? L_OK : (v.derricks > 0 ? L_BAD : L_OFF);
		std::string tip = "Transport (Pickups holen die F\xC3\xA4sser): " + std::to_string(v.pickups);
		if (v.pickups > 0) { tip += " | geladen " + of(v.pickupBarrels, v.pickupCap); }
		if (v.pipeDerricks > 0) { tip += " | per Rohr: " + std::to_string(v.pipeDerricks) + " \xC3\x96lt\xC3\xBCrme"; }
		t.push_back({l, OI_PICKUP, std::to_string(v.pickups), tip, "Keine Pickups f\xC3\xBCr das \xC3\x96l", 4});
	}
	{
		const bool full = v.refCap > 0 && v.refIn >= v.refCap;
		const Lamp l = v.refineries == 0 ? (v.derricks > 0 ? L_BAD : L_OFF) : (v.refNoPower > 0 || full ? L_WARN : L_OK);
		std::string tip = "Raffinerie: " + (v.refineries == 0 ? std::string("keine") : std::to_string(v.refineries) + " | Eingang " + of(v.refIn, v.refCap) + " | Ausgang " + of(v.refOut, v.refCap));
		if (v.refNoPower > 0) { tip += " | ohne Strom: Notbetrieb (langsam)"; }
		const std::string prob = v.refineries == 0 ? "Keine Raffinerie" : (v.refNoPower > 0 ? "Raffinerie ohne Strom (Notbetrieb)" : "Raffinerie voll - mehr Tanker");
		t.push_back({l, OI_REFINERY, std::to_string(v.refineries), tip, prob, v.refineries == 0 ? 5 : 21});
	}
	{
		const Lamp l = v.tankers > 0 ? L_OK : (v.refineries > 0 ? L_BAD : L_OFF);
		std::string tip = "\xC3\x96ltanker (bringen das Raffinat zu Lager/Kraftwerk): " + std::to_string(v.tankers);
		if (v.tankers > 0) { tip += " | geladen " + of(v.tankerBarrels, v.tankerCap); }
		t.push_back({l, OI_TANKER, std::to_string(v.tankers), tip, "Kein \xC3\x96ltanker", 6});
	}
	{
		const Lamp l = v.stores > 0 ? L_OK : L_OFF;
		std::string tip = "\xC3\x96llager: " + (v.stores == 0 ? std::string("keins (mit Lager +10 % Ertrag)") : std::to_string(v.stores) + " | " + of(v.storeBarrels, v.storeCap) + " F\xC3\xA4sser");
		t.push_back({l, OI_STORE, v.stores == 0 ? std::string("-") : pct(v.storeBarrels, v.storeCap), tip, "", 99});
	}
	{
		const Lamp l = v.gens == 0 ? L_BAD : (v.minFuel == 0 ? L_BAD : (v.gensHungry > 0 ? L_WARN : L_OK));
		std::string tip = "Kraftwerke: " + (v.gens == 0 ? std::string("keins") : std::to_string(v.gens));
		if (v.gens > 0 && v.minFuel >= 0) { tip += " | \xC3\x96l reicht noch mindestens " + mmss(v.minFuel); }
		const std::string prob = v.gens == 0 ? "Kein Kraftwerk" : (v.minFuel == 0 ? "Kraftwerk ohne \xC3\x96l" : "Kraftwerk braucht bald \xC3\x96l");
		t.push_back({l, OI_POWERGEN, v.gens == 0 ? std::string("0") : (v.minFuel >= 0 ? mmss(v.minFuel) : std::to_string(v.gens)), tip, prob, v.gens == 0 ? 1 : (v.minFuel == 0 ? 2 : 22)});
	}
	// Reihe 2: Stromnetz und Ergebnis
	{
		int fed = 0;
		const int subs = modgrid::substations(static_cast<int>(selectedPlayer), fed);
		const Lamp l = subs == 0 ? L_BAD : (fed == 0 ? L_BAD : (fed < subs ? L_WARN : L_OK));
		const std::string tip = "Umspannwerk (Pflicht - versorgt \xC3\xBC" "ber die Masten alle Geb\xC3\xA4ude): " + (subs == 0 ? std::string("keins - kein Strom!") : std::to_string(subs) + " | davon mit Strom " + std::to_string(fed));
		const std::string prob = subs == 0 ? "Umspannwerk fehlt - kein Strom!" : (fed == 0 ? "Umspannwerk bekommt keinen Strom" : "Ein Umspannwerk ohne Strom");
		t.push_back({l, OI_SUBSTATION, subs == 0 ? std::string("0") : of(fed, subs), tip, prob, subs == 0 || fed == 0 ? 0 : 23});
	}
	{
		const int sp = modgrid::solarPanelsConnected(static_cast<int>(selectedPlayer));
		const Lamp l = sp > 0 ? L_OK : L_OFF;
		const std::string tip = "Solar: " + (sp == 0 ? std::string("keine Paneele am Umspannwerk") : std::to_string(sp) + " Paneele | +" + std::to_string(sp * 6) + "/min (tags\xC3\xBC" "ber)");
		t.push_back({l, OI_SOLAR, std::to_string(sp), tip, "", 99});
	}
	{
		const Lamp l = v.stations == 0 ? L_OFF : (v.stationBarrels * 10 < v.stationCap * 3 ? L_WARN : L_OK);
		const std::string tip = "Tankstellen (Treibstoff f\xC3\xBCr die Fahrzeuge): " + (v.stations == 0 ? std::string("keine") : std::to_string(v.stations) + " | " + of(v.stationBarrels, v.stationCap) + " F\xC3\xA4sser");
		t.push_back({l, OI_STATION, v.stations == 0 ? std::string("-") : pct(v.stationBarrels, v.stationCap), tip, "Tankstellen fast leer", 24});
	}
	{
		const bool wasting = v.wastedLastMinute > 0 || (v.powerMax > 0 && v.power >= v.powerMax);
		const Lamp l = wasting ? L_WARN : L_OK;
		std::string tip = "Energie: " + of(v.power, v.powerMax) + " | +" + std::to_string(v.incomeLastMinute) + " in der letzten Minute";
		if (v.wastedLastMinute > 0) { tip += " | verfallen " + std::to_string(v.wastedLastMinute) + " (Speicher voll)"; }
		t.push_back({l, OI_POWER, "+" + std::to_string(v.incomeLastMinute), tip, "Speicher voll - Energie verf\xC3\xA4llt", 25});
	}
	{
		const Lamp l = v.buildingsDown > 0 ? L_BAD : L_OK;
		const std::string tip = "Geb\xC3\xA4ude ohne Strom: " + (v.buildingsDown == 0 ? std::string("keine") : std::to_string(v.buildingsDown) + " (stehen still - Masten/Umspannwerk pr\xC3\xBC" "fen)");
		t.push_back({l, OI_NOPOWER, std::to_string(v.buildingsDown), tip, std::to_string(v.buildingsDown) + " Geb\xC3\xA4ude ohne Strom", 7});
	}
	return t;
}

void tileRect(size_t i, int &x, int &y)
{
	const int row = i < COLS ? 0 : 1;
	const int col = i < COLS ? static_cast<int>(i) : static_cast<int>(i) - COLS;
	x = PAD + col * (TILE + TGAP);
	y = PAD + TITLE_H + row * (TILE + VAL_H + 3);
}

void drawIcon(int icon, int x, int y, int w, int h, bool dim)
{
	const unsigned char a = dim ? 120 : 255;
	if (icon == OI_POWER || icon == OI_NOPOWER)
	{
		const int iw = iV_GetImageWidth(IntImages, IMAGE_DES_POWER), ih = iV_GetImageHeight(IntImages, IMAGE_DES_POWER);
		const float k = std::min(static_cast<float>(w - 6) / std::max(1, iw), static_cast<float>(h - 6) / std::max(1, ih));
		const int dw = std::max(1, static_cast<int>(iw * k)), dh = std::max(1, static_cast<int>(ih * k));
		modwzhd::drawIntfac(IMAGE_DES_POWER, x + (w - dw) / 2, y + (h - dh) / 2, dw, dh, true, a);
		if (icon == OI_NOPOWER)
		{
			const PIELIGHT red = withAlpha(pal_RGBA(255, 50, 40, 255), a);
			for (int o = -1; o <= 1; ++o) { iV_Line(x + 4 + o, y + h - 4, x + w - 4 + o, y + 4, red); }
		}
		return;
	}
	IMAGEFILE *img = oilImages();
	if (img == nullptr) { return; }
	iV_DrawImageFileAnisotropic(img, static_cast<UWORD>(icon), x, y, Vector2f(static_cast<float>(w), static_cast<float>(h)), defaultProjectionMatrix(), a);
}

void drawPanel(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	if (!s_on || selectedPlayer >= MAX_PLAYERS) { return; }
	const modoil::EconomyView v = modoil::economyView(static_cast<int>(selectedPlayer));
	if (!v.active) { return; }
	const std::vector<Tile> tiles = tilesFor(v);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	modui::wzPanel(x0, y0, x0 + w, y0 + h, "\xC3\x96l & Strom", 0);
	const double t = static_cast<double>(realTime) / 1000.0;
	const int pulse = 150 + static_cast<int>(105.0 * (0.5 + 0.5 * std::sin(t * 6.0)));
	for (size_t i = 0; i < tiles.size(); ++i)
	{
		const Tile &tl = tiles[i];
		int tx, ty;
		tileRect(i, tx, ty);
		tx += x0; ty += y0;
		// Kachel: dunkle Flaeche, Rahmen in der Statusfarbe (rot pulsiert), Bild, kurze Zahl darunter
		pie_UniTransBoxFill(static_cast<float>(tx), static_cast<float>(ty), static_cast<float>(tx + TILE), static_cast<float>(ty + TILE), pal_RGBA(8, 12, 30, 220));
		PIELIGHT rim = lampColour(tl.lamp);
		if (tl.lamp == L_BAD) { rim.byte.a = static_cast<UBYTE>(pulse); }
		iV_Box(tx, ty, tx + TILE - 1, ty + TILE - 1, rim);
		if (tl.lamp == L_BAD || tl.lamp == L_WARN) { iV_Box(tx + 1, ty + 1, tx + TILE - 2, ty + TILE - 2, rim); }
		drawIcon(tl.icon, tx + 1, ty + 1, TILE - 2, TILE - 2, tl.lamp == L_OFF);
		// Pfeil zur naechsten Stufe des Kreislaufs (Reihe 1)
		if (i + 1 < COLS)
		{
			const int ax = tx + TILE + 1, ay = ty + TILE / 2;
			iV_Line(ax, ay, ax + TGAP - 2, ay, WZCOL_TEXT_MEDIUM);
			iV_Line(ax + TGAP - 4, ay - 2, ax + TGAP - 2, ay, WZCOL_TEXT_MEDIUM);
			iV_Line(ax + TGAP - 4, ay + 2, ax + TGAP - 2, ay, WZCOL_TEXT_MEDIUM);
		}
		const std::string val = modui::fit(tl.value, TILE + TGAP - 1, 0);
		const int vw = modui::textWidth(val, 0);
		const PIELIGHT vc = tl.lamp == L_BAD ? pal_RGBA(255, 120, 100, 255) : (tl.lamp == L_WARN ? pal_RGBA(255, 215, 90, 255) : WZCOL_TEXT_MEDIUM);
		modui::text(val, tx + (TILE - vw) / 2, ty + TILE + 1, vc, 0);
	}
	// wichtigstes Problem (rot vor gelb, dann nach Wichtigkeit) - oder "alles laeuft"
	const Tile *worst = nullptr;
	for (const Tile &tl : tiles)
	{
		if ((tl.lamp != L_BAD && tl.lamp != L_WARN) || tl.problem.empty()) { continue; }
		if (worst == nullptr || (tl.lamp == L_BAD && worst->lamp != L_BAD) || (tl.lamp == worst->lamp && tl.prio < worst->prio)) { worst = &tl; }
	}
	const int py = y0 + h - PAD - PROB_H + 2;
	if (worst != nullptr)
	{
		const PIELIGHT pc = worst->lamp == L_BAD ? pal_RGBA(255, 110, 90, 255) : pal_RGBA(255, 210, 80, 255);
		modui::disc(x0 + PAD + 4, py + 5, 3, pc);
		modui::text(modui::fit(worst->problem, w - 2 * PAD - 12, 0), x0 + PAD + 11, py, pc, 0);
	}
	else
	{
		modui::disc(x0 + PAD + 4, py + 5, 3, pal_RGBA(70, 230, 90, 255));
		modui::text("Alles l\xC3\xA4uft", x0 + PAD + 11, py, pal_RGBA(140, 230, 150, 255), 0);
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
	s_tiles.clear();
	for (int i = 0; i < 11; ++i)
	{
		auto tb = std::make_shared<W_BUTTON>();
		panel->attach(tb);
		tb->displayFunction = [](WIDGET *, UDWORD, UDWORD) {};   // gezeichnet wird im Fenster (drawPanel)
		s_tiles.push_back(tb);
	}
	auto t = std::make_shared<W_BUTTON>();
	screen->psForm->attach(t);
	t->displayFunction = drawToggle;
	t->setTip("\xC3\x96l & Strom: \xC3\x9C" "bersicht ein/aus");
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

void collectPlacementRings(std::vector<glm::vec3> &supply, std::vector<glm::vec3> &link)
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
	const float cx = static_cast<float>(world_coord(static_cast<int>(sBuildDetails.x)) + world_coord(static_cast<int>(sBuildDetails.width)) / 2);
	const float cy = static_cast<float>(world_coord(static_cast<int>(sBuildDetails.y)) + world_coord(static_cast<int>(sBuildDetails.height)) / 2);
	// bestehende Versorgungsbereiche: verschmelzen mit dem neuen Ring zu EINER leuchtenden Umrisslinie - man sieht, wohin Strom kommt
	if (pole || sub || consumer)
	{
		for (const STRUCTURE *s : gameWorld.objects.structures[selectedPlayer])
		{
			if (s->died || s->status != SS_BUILT) { continue; }
			if (modgrid::isPole(s->pStructureType)) { supply.emplace_back(static_cast<float>(s->pos.x), static_cast<float>(s->pos.y), static_cast<float>(R_POLE_SUPPLY)); }
			else if (modgrid::isSubstation(s->pStructureType)) { supply.emplace_back(static_cast<float>(s->pos.x), static_cast<float>(s->pos.y), static_cast<float>(R_SUB_SUPPLY)); }
		}
	}
	if (pole) { supply.emplace_back(cx, cy, static_cast<float>(R_POLE_SUPPLY)); link.emplace_back(cx, cy, static_cast<float>(R_POLE_LINK)); }
	if (sub) { supply.emplace_back(cx, cy, static_cast<float>(R_SUB_SUPPLY)); link.emplace_back(cx, cy, static_cast<float>(R_POLE_LINK)); }
	if (source) { link.emplace_back(cx, cy, static_cast<float>(R_SOURCE_LINK)); }
	if (store) { supply.emplace_back(cx, cy, static_cast<float>(R_STORE)); }
}
}

void placementRings(std::vector<glm::vec3> &supply, std::vector<glm::vec3> &link)
{
	if (selectedPlayer < MAX_PLAYERS && modoil::manages(static_cast<int>(selectedPlayer))) { collectPlacementRings(supply, link); }
}

void drawWorldIcons(const glm::mat4 &m)
{
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
	const int x = static_cast<int>(screenWidth) - PANEL_W - 8 - modlayer::reservedRight(), y = 44;   // MOD (21.11.): links neben den Ebenen-Knoepfen
	s_toggle->setGeometry(x + PANEL_W - BTN_W, y, BTN_W, BTN_H);
	s_panel->setGeometry(x, y + BTN_H + 4, PANEL_W, PANEL_H);
	// Tooltips der Kacheln (Einzelheiten) - nur neu setzen, wenn sich der Text geaendert hat
	if (s_on)
	{
		const modoil::EconomyView v = modoil::economyView(static_cast<int>(selectedPlayer));
		const std::vector<Tile> tiles = tilesFor(v);
		static std::vector<std::string> s_lastTips;
		s_lastTips.resize(s_tiles.size());
		for (size_t i = 0; i < s_tiles.size() && i < tiles.size(); ++i)
		{
			int tx, ty;
			tileRect(i, tx, ty);
			s_tiles[i]->setGeometry(tx, ty, TILE, TILE + VAL_H);
			if (s_lastTips[i] != tiles[i].tip) { s_tiles[i]->setTip(tiles[i].tip); s_lastTips[i] = tiles[i].tip; }
		}
	}
}

void shutdown()
{
	if (s_screen) { widgRemoveOverlayScreen(s_screen); }
	s_screen = nullptr; s_panel = nullptr; s_toggle = nullptr; s_tiles.clear();
	if (s_oilImg != nullptr) { iV_FreeImageFile(s_oilImg); s_oilImg = nullptr; }
	s_oilTried = false;
}
}
