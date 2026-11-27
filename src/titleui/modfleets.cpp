// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * titleui/modfleets.cpp - MOD (Wunsch 17.11.): "Flotten-Menue: unbedingt. Abspeicherbar fuer alle Modi. Verschiedene Flotten. Eigene Namen
 * benennbar. Favoritensektion, nach Art sortieren (Infanterie, Cyborgs, Panzer, Artillerie, Systeme ...)."
 *
 * Eine Flotte ist eine benannte Sammlung von Entwuerfen (Vorlagen), wie sie das Spiel mit dem Stern im Designer dauerhaft speichert
 * (userdata/mp/templates.json fuer Gefechte/Mehrspieler, userdata/campaign/templates.json fuer die Kampagne).
 *  - "New fleet": alle gespeicherten Entwuerfe beider Spielarten werden zu einer neuen Flotte (Name aus dem Eingabefeld).
 *  - "Update": die gewaehlte Flotte uebernimmt die aktuell gespeicherten Entwuerfe.
 *  - "Use in all games": die Entwuerfe der Flotte werden zu den gespeicherten Entwuerfen ALLER Spielarten (Kampagne, Gefecht,
 *    Mehrspieler) - sie stehen in jeder Fabrik bereit.
 *  - Umbenennen, Loeschen, Flotte als Favorit (Favoriten oben); Klick auf einen Entwurf = Favorit an/aus (Favoriten oben in ihrer Art).
 *  - Entwuerfe nach Art: Infanterie, Cyborgs/Combots, Panzer, Artillerie, Luft, Systeme.
 * Gespeichert in userdata/fleets.json. Rein lokal (Hauptmenue), beruehrt keinen Spielzustand.
 */

#include <nlohmann/json.hpp> // Must come before WZ includes

#include "titleui.h"
#include "lib/framework/frame.h"
#include "lib/framework/file.h"
#include "lib/framework/physfs_ext.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/bitimage.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/widget/button.h"
#include "lib/widget/editbox.h"
#include "lib/widget/label.h"
#include "../frontend.h"
#include "../frend.h"
#include "../game.h"
#include "../hci.h"
#include "../intdisplay.h"
#include "../multiint.h"
#include "../modfleetdesigner.h"
#include "../modcompbody.h"
#include "../modhullforge.h"
#include "../display.h"
#include "../intfac.h"
#include "../modwzhd.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
constexpr unsigned ID_FLEET = 4599000;      // + Index (Flottenliste)
constexpr unsigned ID_DESIGN = 4599100;     // + Zeile (Entwuerfe)
constexpr unsigned ID_NEW = 4599400, ID_UPDATE = 4599401, ID_RENAME = 4599402, ID_FAV = 4599403, ID_DEL = 4599404, ID_USE = 4599405;
constexpr unsigned ID_PREV = 4599406, ID_NEXT = 4599407, ID_BACK = 4599408, ID_EDIT = 4599409, ID_INFO = 4599410, ID_DESIGNER = 4599411, ID_HEAD = 4599500;
constexpr unsigned ID_FORGE = 4599412;   // (23.11. 04:23) Rumpf-Baukasten
constexpr unsigned ID_DIM = 4599590;
constexpr int LINE_H = 22;
constexpr int DESIGN_ROWS = 15;
const char *const FILE_FLEETS = "userdata/fleets.json";
const char *const TAGS[] = {"mp", "campaign"};

nlohmann::json s_doc;          // {"version":1, "active":"Name", "fleets":[{"name":..., "fav":bool, "designs":[...]}]}
std::string s_info;
std::vector<int> s_lineToDesign;   // Zeile -> Index in designs der gewaehlten Flotte

void load()
{
	s_doc = nlohmann::json{{"version", 1}, {"active", ""}, {"fleets", nlohmann::json::array()}};
	if (!PHYSFS_exists(FILE_FLEETS)) { return; }
	auto j = parseJsonFile(FILE_FLEETS);
	if (j.has_value() && j->is_object() && j->contains("fleets") && (*j)["fleets"].is_array()) { s_doc = std::move(*j); }
}

void save()
{
	PHYSFS_mkdir("userdata");
	if (!saveJSONToFile(s_doc, FILE_FLEETS)) { s_info = _("Could not save the fleets file"); }
}

nlohmann::json &fleets() { return s_doc["fleets"]; }

/// Vergleichsschluessel eines Entwurfs (wie das Spiel: Name + Teile)
std::string identity(const nlohmann::json &e)
{
	static const char *keys[] = {"name", "type", "body", "propulsion", "brain", "repair", "ecm", "sensor", "construct", "weapons", "modSlots"};
	nlohmann::json id = nlohmann::json::object();
	for (const char *k : keys)
	{
		auto it = e.find(k);
		id[k] = (it != e.end()) ? *it : nlohmann::json(nullptr);
	}
	return id.dump();
}

/// alle gespeicherten Entwuerfe beider Spielarten (ohne Doppelte)
nlohmann::json storedDesigns()
{
	nlohmann::json out = nlohmann::json::array();
	std::vector<std::string> seen;
	for (const char *tag : TAGS)
	{
		const std::string path = std::string("userdata/") + tag + "/templates.json";
		if (!PHYSFS_exists(path.c_str())) { continue; }
		auto j = parseJsonFile(path.c_str());
		if (!j.has_value() || !j->is_object()) { continue; }
		auto it = j->find("templates");
		if (it == j->end() || !it->is_array()) { continue; }
		for (const auto &e : *it)
		{
			if (!e.is_object()) { continue; }
			const std::string id = identity(e);
			if (std::find(seen.begin(), seen.end(), id) != seen.end()) { continue; }
			seen.push_back(id);
			out.push_back(e);
		}
	}
	return out;
}

bool hasWord(const nlohmann::json &e, const char *key, std::initializer_list<const char *> words)
{
	auto check = [&](const std::string &s) {
		for (const char *w : words) { if (s.find(w) != std::string::npos) { return true; } }
		return false;
	};
	auto it = e.find(key);
	if (it == e.end()) { return false; }
	if (it->is_string()) { return check(it->get<std::string>()); }
	if (it->is_array())
	{
		for (const auto &v : *it) { if (v.is_string() && check(v.get<std::string>())) { return true; } }
	}
	return false;
}

/// Art eines Entwurfs: 0 Infanterie, 1 Cyborgs/Combots, 2 Panzer, 3 Artillerie, 4 Luft, 5 Systeme
int category(const nlohmann::json &e)
{
	const std::string type = e.value("type", std::string());
	if (type == "PERSON") { return 0; }
	if (type.rfind("CYBORG", 0) == 0 || hasWord(e, "body", {"MFT_", "Cyborg", "Mech"})) { return 1; }
	if (hasWord(e, "propulsion", {"V-Tol", "VTOL", "Vtol", "Helicopter", "MFJet", "Jet"})) { return 4; }
	const bool armed = e.contains("weapons") && e["weapons"].is_array() && !e["weapons"].empty();
	if (!armed) { return 5; }
	if (hasWord(e, "weapons", {"Mortar", "Howitzer", "Arty", "MdArt", "HvyArt", "MRL", "IDF", "Ripple", "Seraph", "Archangel", "Bombard", "Pepperpot"})) { return 3; }
	return 2;
}

const char *categoryName(int c)
{
	switch (c)
	{
	case 0: return _("INFANTRY");
	case 1: return _("CYBORGS / COMBOTS");
	case 2: return _("TANKS");
	case 3: return _("ARTILLERY");
	case 4: return _("AIRCRAFT");
	default: return _("SYSTEMS");
	}
}

std::vector<int> fleetOrder()
{
	std::vector<int> idx;
	for (int i = 0; i < static_cast<int>(fleets().size()); ++i) { idx.push_back(i); }
	std::stable_sort(idx.begin(), idx.end(), [](int a, int b) {
		return fleets()[a].value("fav", false) && !fleets()[b].value("fav", false);
	});
	return idx;
}

// (Wunsch 17.11. 21:48) "einfacher und uebersichtlicher, weniger Ablenkung, groessere Schrift, bessere Platzierung":
// dunkle, ruhige Tafel ueber dem abgedunkelten Menuebild, normale statt kleiner Schrift, klare Spalten.
constexpr int PANEL_X = 12, PANEL_Y = 8, PANEL_W = 616, PANEL_H = 464;
unsigned s_selectedButton = 0;     // hervorgehobene Zeile (gewaehlte Flotte)

// (Wunsch 22:13) "generell verschoenern - wunderschoene Icons und leuchtende UIs, es soll Bombe aussehen":
// Verlaufs-Tafel mit leuchtendem Rahmen und Eck-Akzenten, Neon-Icons (images/modfleets.img), Leuchtbalken beim Ueberfahren,
// pulsierender Editor-Knopf.
enum FleetIcon { IC_INFANTRY = 0, IC_CYBORG, IC_TANK, IC_ARTY, IC_AIR, IC_SYSTEMS, IC_DESIGNER, IC_FLEET, IC_STAR,
                 IC_BTN_UP, IC_BTN_DOWN, IC_BTN_HI, IC_NEW, IC_RENAME, IC_UPDATE, IC_DELETE, IC_USE, IC_LEFT, IC_RIGHT };
std::map<unsigned, int> s_btnIcon;    // Knopf-ID -> Sinnbild
IMAGEFILE *s_icons = nullptr;
bool s_iconsTried = false;
IMAGEFILE *icons()
{
	if (!s_iconsTried)
	{
		s_iconsTried = true;
		if (PHYSFS_exists("images/modfleets.img")) { s_icons = iV_LoadImageFile("images/modfleets.img"); }
	}
	return s_icons;
}
void drawImg(int id, int x, int y, int w, int h, uint8_t alpha = 255)
{
	IMAGEFILE *img = icons();
	if (img == nullptr) { return; }
	iV_DrawImageFileAnisotropic(img, static_cast<UWORD>(id), x, y, Vector2f(static_cast<float>(w), static_cast<float>(h)), defaultProjectionMatrix(), alpha);
}
void drawIcon(int id, int x, int y, int size, uint8_t alpha = 255)
{
	drawImg(id, x, y, size, size, alpha);
}
struct IconSpot { int id; int x, y, size; };
std::vector<IconSpot> s_iconSpots;     // Icons auf der Tafel (relativ zur Tafel), beim Aufbau gesammelt

float pulse(unsigned periodMs)
{
	return 0.5f + 0.5f * std::sin(static_cast<float>(realTime % periodMs) / static_cast<float>(periodMs) * 6.2831853f);
}
void gradientBox(int x0, int y0, int x1, int y1, PIELIGHT top, PIELIGHT bottom, int steps = 12)
{
	const int h = std::max(1, y1 - y0);
	for (int i = 0; i < steps; ++i)
	{
		const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(steps);
		PIELIGHT c;
		c.byte.r = static_cast<uint8_t>(top.byte.r + (bottom.byte.r - top.byte.r) * t);
		c.byte.g = static_cast<uint8_t>(top.byte.g + (bottom.byte.g - top.byte.g) * t);
		c.byte.b = static_cast<uint8_t>(top.byte.b + (bottom.byte.b - top.byte.b) * t);
		c.byte.a = static_cast<uint8_t>(top.byte.a + (bottom.byte.a - top.byte.a) * t);
		const int ya = y0 + h * i / steps, yb = y0 + h * (i + 1) / steps;
		pie_UniTransBoxFill(static_cast<float>(x0), static_cast<float>(ya), static_cast<float>(x1), static_cast<float>(yb), c);
	}
}
/// leuchtender Rahmen: helle Linie + nach aussen schwaecher werdender Lichthof
void glowFrame(int x0, int y0, int x1, int y1, PIELIGHT col, int glow, float strength)
{
	for (int g = glow; g >= 1; --g)
	{
		PIELIGHT c = col;
		c.byte.a = static_cast<uint8_t>(std::clamp(strength * 110.f * (1.f - static_cast<float>(g - 1) / static_cast<float>(glow)), 0.f, 255.f));
		iV_Box(x0 - g, y0 - g, x1 + g, y1 + g, c);
	}
	PIELIGHT c = col; c.byte.a = 255;
	iV_Box(x0, y0, x1, y1, c);
}

class FleetDim : public WIDGET
{
public:
	void display(int xOffset, int yOffset) override
	{
		const int x0 = xOffset + x(), y0 = yOffset + y();
		gradientBox(x0, y0, x0 + width(), y0 + height(), pal_RGBA(0, 4, 14, 200), pal_RGBA(0, 0, 0, 225), 8);
	}
};

class FleetPanel : public W_FORM
{
public:
	void display(int xOffset, int yOffset) override
	{
		const int x0 = xOffset + x(), y0 = yOffset + y(), x1 = x0 + width(), y1 = y0 + height();
		gradientBox(x0, y0, x1, y1, pal_RGBA(20, 30, 54, 246), pal_RGBA(8, 10, 20, 250), 16);
		glowFrame(x0, y0, x1, y1, pal_RGBA(90, 190, 255, 255), 6, 0.55f + 0.25f * pulse(4200));
		// Eck-Akzenten (helle L-Winkel)
		const PIELIGHT acc = pal_RGBA(170, 235, 255, 255);
		for (int cx : {x0, x1})
		{
			for (int cy : {y0, y1})
			{
				const int dx = cx == x0 ? 1 : -1, dy = cy == y0 ? 1 : -1;
				pie_UniTransBoxFill(static_cast<float>(std::min(cx, cx + dx * 26)), static_cast<float>(std::min(cy, cy + dy * 3)), static_cast<float>(std::max(cx, cx + dx * 26)), static_cast<float>(std::max(cy, cy + dy * 3)), acc);
				pie_UniTransBoxFill(static_cast<float>(std::min(cx, cx + dx * 3)), static_cast<float>(std::min(cy, cy + dy * 26)), static_cast<float>(std::max(cx, cx + dx * 3)), static_cast<float>(std::max(cy, cy + dy * 26)), acc);
			}
		}
		// Leuchtlinie unter dem Titel, Spaltentrenner, Linie ueber der Fusszeile
		gradientBox(x0 + 16, y0 + 56, x1 - 16, y0 + 58, pal_RGBA(120, 220, 255, 230), pal_RGBA(40, 120, 200, 90), 2);
		pie_UniTransBoxFill(static_cast<float>(x0 + 230), static_cast<float>(y0 + 68), static_cast<float>(x0 + 231), static_cast<float>(y1 - 44), pal_RGBA(90, 170, 240, 120));
		pie_UniTransBoxFill(static_cast<float>(x0 + 12), static_cast<float>(y1 - 40), static_cast<float>(x1 - 12), static_cast<float>(y1 - 39), pal_RGBA(90, 170, 240, 140));
		for (const IconSpot &ic : s_iconSpots) { drawIcon(ic.id, x0 + ic.x, y0 + ic.y, ic.size); }
	}
};

/// Zeile: Leuchtbalken beim Ueberfahren, gewaehlte Zeile mit hellem Balken und Akzentstreifen links
void displayFleetLine(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const int x0 = static_cast<int>(xOffset) + psWidget->x() - 6, y0 = static_cast<int>(yOffset) + psWidget->y();
	const int x1 = x0 + psWidget->width() + 6, y1 = y0 + psWidget->height();
	const bool sel = psWidget->id == s_selectedButton;
	const bool hover = static_cast<W_BUTTON *>(psWidget)->isMouseOverWidget();
	if (sel || hover)
	{
		gradientBox(x0, y0, x1, y1, sel ? pal_RGBA(50, 120, 200, 170) : pal_RGBA(40, 80, 140, 110), sel ? pal_RGBA(30, 70, 140, 140) : pal_RGBA(20, 40, 80, 70), 4);
		pie_UniTransBoxFill(static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x0 + 3), static_cast<float>(y1), sel ? pal_RGBA(140, 230, 255, 255) : pal_RGBA(90, 170, 240, 200));
	}
	displayTextOption(psWidget, xOffset, yOffset);
}

/// (Wunsch 22:42) "jeden neuen Knopf im Stil der WZ-Knoepfe im Kommandomenue (Patrouille, Feuerunterstuetzung ...): genau gleiche Form,
/// Rand und Hintergrund - einziger Unterschied: das farbige Leuchten der neuen Icons". Vorlagen aus image_ord_* (fleeticons.py), Ueberfahren =
/// WZ-Hervorhebung (image_des_hilight), gedrueckt = heller Hintergrund wie image_ord_*down.
// (Wunsch 18.11. 12:03) "Den Stil der WZ-Buttons beibehalten, aber schoener machen mit Glow ... die sehen nicht annaehernd so aus wie die
// originalen WZ-Buttons ... fuer Muelltonne, Turm 1, Turm 2 usw. keine neuen, einfach die originalen verschoenern": Knoepfe mit Original-WZ-Bild
// (intfac, in HD aus images/wzhd.img) - normal / Maus darueber mit Lichthof / gewaehlt = WZ-Hervorhebungsbild + Lichthof.
// Eigene neue Knoepfe (Neu, Umbenennen, Ueberall, Pfeile) im selben WZ-Stil ("wz_*" im HD-Paket). Reiter mit Text: WZ-Rahmen + Text.
struct WzLook { int img = -1, hi = -1; std::string named; std::string text; };
std::map<unsigned, WzLook> s_btnWz;

void displayWzButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	W_BUTTON *b = static_cast<W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const bool down = (b->getState() & (WBUT_DOWN | WBUT_CLICKLOCK)) != 0;
	const bool sel = (b->getState() & WBUT_CLICKLOCK) != 0;
	const bool hover = b->isMouseOverWidget();
	const auto wz = s_btnWz.find(psWidget->id);
	if (wz != s_btnWz.end())
	{
		const WzLook &l = wz->second;
		if (!l.text.empty()) { modwzhd::textButton(x0, y0, w, h, l.text, sel, hover, down); return; }
		if (!l.named.empty())
		{
			if (modwzhd::drawNamed((l.named + ((down || sel) ? "h" : "")).c_str(), x0, y0, w, h, hover || sel)
			    || modwzhd::drawNamed(l.named.c_str(), x0, y0, w, h, hover || sel))
			{
				return;
			}
		}
		else if (l.img >= 0)
		{
			const int img = ((down || sel) && l.hi >= 0) ? l.hi : l.img;
			modwzhd::drawIntfac(static_cast<unsigned>(img), x0, y0, w, h, hover || sel);
			return;
		}
	}
	drawImg(down ? IC_BTN_DOWN : IC_BTN_UP, x0, y0, w, h);
	const auto it = s_btnIcon.find(psWidget->id);
	if (it != s_btnIcon.end())
	{
		const int sz = h * 9 / 10;
		drawIcon(it->second, x0 + (w - sz) / 2, y0 + (h - sz) / 2, sz);
	}
	if (b->isMouseOverWidget())
	{
		const int ex = std::max(1, w / 36), ey = std::max(1, h / 24);
		drawImg(IC_BTN_HI, x0 - ex, y0 - ey, w + 2 * ex, h + 2 * ey);
	}
}

std::shared_ptr<W_BUTTON> wzButton(WIDGET *parent, unsigned id, int x, int y, int w, int h, int icon, const char *tip)
{
	W_BUTINIT init;
	init.formID = FRONTEND_BOTFORM;
	init.id = id;
	init.x = static_cast<short>(x);
	init.y = static_cast<short>(y);
	init.width = static_cast<unsigned short>(w);
	init.height = static_cast<unsigned short>(h);
	init.pDisplay = displayWzButton;
	auto btn = std::make_shared<W_BUTTON>(&init);
	parent->attach(btn);
	if (tip != nullptr) { btn->setTip(tip); }
	s_btnIcon[id] = icon;
	return btn;
}

/// Knopf mit Original-WZ-Bild (intfac-Nummern normal / hervorgehoben)
std::shared_ptr<W_BUTTON> wzImgButton(WIDGET *parent, unsigned id, int x, int y, int w, int h, int img, int hi, const char *tip, int fallbackIcon)
{
	auto b = wzButton(parent, id, x, y, w, h, fallbackIcon, tip);
	WzLook l; l.img = img; l.hi = hi;
	s_btnWz[id] = l;
	return b;
}
/// eigener Knopf im WZ-Stil aus dem HD-Paket ("wz_new" usw.; "...h" = hervorgehoben, falls vorhanden)
std::shared_ptr<W_BUTTON> wzNamedButton(WIDGET *parent, unsigned id, int x, int y, int w, int h, const char *named, const char *tip, int fallbackIcon)
{
	auto b = wzButton(parent, id, x, y, w, h, fallbackIcon, tip);
	WzLook l; l.named = named;
	s_btnWz[id] = l;
	return b;
}
/// Reiter mit Beschriftung im WZ-Stil
std::shared_ptr<W_BUTTON> wzTextButton(WIDGET *parent, unsigned id, int x, int y, int w, int h, const std::string &text, const char *tip)
{
	auto b = wzButton(parent, id, x, y, w, h, -1, tip);
	WzLook l; l.text = text;
	s_btnWz[id] = l;
	return b;
}

std::shared_ptr<W_BUTTON> lineButton(WIDGET *parent, unsigned id, int x, int y, int w, const std::string &text, iV_fonts font = font_regular, int h = LINE_H,
                                     bool framed = false, bool centre = false)
{
	W_BUTINIT init;
	init.formID = FRONTEND_BOTFORM;
	init.id = id;
	init.x = static_cast<short>(x);
	init.y = static_cast<short>(y);
	init.width = static_cast<unsigned short>(w);
	init.height = static_cast<unsigned short>(h);
	init.pDisplay = displayFleetLine;
	(void)framed;
	init.FontID = font;
	if (centre) { init.style |= WBUT_TXTCENTRE; }
	init.pText = text.c_str();   // der Knopf kopiert den Text
	init.pUserData = new DisplayTextOptionCache();
	init.onDelete = [](WIDGET *psWidget) {
		delete static_cast<DisplayTextOptionCache *>(psWidget->pUserData);
		psWidget->pUserData = nullptr;
	};
	auto b = std::make_shared<W_BUTTON>(&init);
	parent->attach(b);
	return b;
}

void headLabel(WIDGET *parent, unsigned id, int x, int y, int w, const std::string &text, PIELIGHT col, iV_fonts font = font_regular_bold, int h = LINE_H,
               bool centre = false)
{
	auto l = std::make_shared<W_LABEL>();
	parent->attach(l);
	l->id = id;
	l->setGeometry(x, y, w, h);
	if (centre) { l->setTextAlignment(WLAB_ALIGNCENTRE); }
	l->setFont(font, col);
	l->setString(WzString::fromUtf8(text));
	l->setTransparentToClicks(true);
}

/// gewaehlte Flotte uebernimmt die gespeicherten Entwuerfe (Favoriten-Marken bleiben); ohne Flotte entsteht "Fleet 1"
int updateFleet(int index)
{
	nlohmann::json now = storedDesigns();
	if (fleets().empty())
	{
		nlohmann::json f = nlohmann::json::object();
		f["name"] = astringf(_("Fleet %d"), 1); f["fav"] = false; f["designs"] = now;
		fleets().push_back(f);
		return static_cast<int>(now.size());
	}
	index = std::clamp(index, 0, static_cast<int>(fleets().size()) - 1);
	const nlohmann::json &old = fleets()[index]["designs"];
	for (auto &e : now)
	{
		for (const auto &o : old) { if (identity(o) == identity(e) && o.value("modFav", false)) { e["modFav"] = true; } }
	}
	fleets()[index]["designs"] = now;
	return static_cast<int>(now.size());
}
}

namespace { int s_fleetReturnSelect = -1; }   // (18.11.) Rueckkehr aus dem Hauptmenue-Editor: diese Flotte wieder waehlen

WzFleetTitleUI::WzFleetTitleUI()
{
	load();
	if (s_fleetReturnSelect >= 0) { selected = s_fleetReturnSelect; s_fleetReturnSelect = -1; }
}

void WzFleetTitleUI::start()
{
	// (21:48) zurueck aus dem Einheiteneditor: die gewaehlte Flotte (oder eine neue erste) uebernimmt die gespeicherten Entwuerfe
	if (modfleetdesigner::consumeFleetUpdate())
	{
		int idx = 0;
		const std::string active = s_doc.value("active", std::string());
		for (int i = 0; i < static_cast<int>(fleets().size()); ++i) { if (fleets()[i].value("name", std::string()) == active) { idx = i; } }
		if (!fleets().empty() && active.empty()) { idx = std::clamp(selected, 0, static_cast<int>(fleets().size()) - 1); }
		const int n = updateFleet(idx);
		selected = fleets().empty() ? 0 : std::clamp(idx, 0, static_cast<int>(fleets().size()) - 1);
		s_info = astringf(_("Back from the unit designer: \"%s\" now has %d designs"), fleets()[selected].value("name", std::string()).c_str(), n);
		save();
	}
	build();
}

void WzFleetTitleUI::build()
{
	widgDelete(psWScreen, FRONTEND_BACKDROP);
	W_FORM *backdrop = addBackdrop();
	if (backdrop == nullptr) { return; }
	// ruhiger Hintergrund: Menuebild abgedunkelt, dunkle Tafel davor (kein Logo, kein Seitentext)
	auto dim = std::make_shared<FleetDim>();
	backdrop->attach(dim);
	dim->id = ID_DIM;
	dim->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
		psWidget->setGeometry(-(static_cast<int>(pie_GetVideoBufferWidth()) - HIDDEN_FRONTEND_WIDTH) / 2 - 2, -(static_cast<int>(pie_GetVideoBufferHeight()) - HIDDEN_FRONTEND_HEIGHT) / 2 - 2,
		                      static_cast<int>(pie_GetVideoBufferWidth()) + 4, static_cast<int>(pie_GetVideoBufferHeight()) + 4);
	}));
	dim->setTransparentToClicks(true);
	auto panel = std::make_shared<FleetPanel>();
	backdrop->attach(panel);
	panel->id = FRONTEND_BOTFORM;
	panel->setGeometry(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
	WIDGET *form = panel.get();
	const int W = PANEL_W;
	s_iconSpots.clear();
	s_btnIcon.clear();
	s_btnWz.clear();

	// ---- Kopf: Titel, eine Zeile Erklaerung, grosser Knopf zum Einheiteneditor
	s_iconSpots.push_back({IC_FLEET, 14, 10, 40});
	headLabel(form, ID_HEAD + 20, 60, 8, 300, _("FLEETS"), pal_RGBA(200, 240, 255, 255), font_large, 30);
	headLabel(form, ID_HEAD + 21, 60, 34, 330, _("Your unit designs - for every game mode"), pal_RGBA(130, 180, 220, 255), font_regular, 18);

	// ---- links: Flotten
	const std::string active = s_doc.value("active", std::string());
	s_iconSpots.push_back({IC_FLEET, 14, 63, 20});
	headLabel(form, ID_HEAD, 38, 62, 180, _("Your fleets"), pal_RGBA(255, 214, 110, 255));
	const std::vector<int> order = fleetOrder();
	if (selected >= static_cast<int>(fleets().size())) { selected = std::max(0, static_cast<int>(fleets().size()) - 1); }
	s_selectedButton = fleets().empty() ? 0 : ID_FLEET + static_cast<unsigned>(selected);
	int y = 62 + LINE_H + 4;
	for (size_t k = 0; k < order.size() && k < 8; ++k)
	{
		const int i = order[k];
		const nlohmann::json &f = fleets()[i];
		std::string t = f.value("name", std::string("?"));
		if (f.value("name", std::string()) == active) { t += "  (" + std::string(_("in use")) + ")"; }
		if (f.value("fav", false)) { s_iconSpots.push_back({IC_STAR, 18, y + 4, 14}); }
		lineButton(form, ID_FLEET + static_cast<unsigned>(i), 36, y, 186, t);
		y += LINE_H;
	}
	if (fleets().empty()) { headLabel(form, ID_HEAD + 1, 20, y, 200, _("No fleets yet"), WZCOL_TEXT_MEDIUM, font_regular); }

	// Name + Aktionen (gerahmte Knoepfe, 2 Spalten)
	auto edit = std::make_shared<W_EDITBOX>();
	form->attach(edit);
	edit->id = ID_EDIT;
	edit->FontID = font_regular;
	edit->setGeometry(16, 270, 204, 24);
	edit->setBoxColours(pal_RGBA(80, 100, 150, 255), pal_RGBA(80, 100, 150, 255), pal_RGBA(10, 12, 20, 255));
	edit->setMaxStringSize(40);
	edit->setPlaceholder(_("Fleet name"));
	if (!fleets().empty()) { edit->setString(WzString::fromUtf8(fleets()[selected].value("name", std::string()))); }
	// Aktionen als WZ-Befehlsknoepfe (1,5-fach), Beschriftung darunter
	struct Act { unsigned id; int icon; const char *cap; const char *tip; };
	// (Wunsch 23.11. 17:55 "die beiden Buttons Hull Forge und Unit Designer oben rechts sollen auch nach links unten ... warum hat der
	// Hull-Forge-Button kein Symbol, und warum ist der Fleet-Designer-Button (das Oktagon mit dem Panzer) nicht im WZ-Stil?"): beide jetzt
	// unten links bei den anderen Aktionen, als WZ-Knopf mit Sinnbild und Wort darunter (wie Neu/Umbenennen/Aktualisieren)
	const Act acts[8] = {
		{ID_NEW, IC_NEW, _("New"), _("New fleet from starred designs")},
		{ID_RENAME, IC_RENAME, _("Rename"), _("Rename fleet")},
		{ID_UPDATE, IC_UPDATE, _("Update"), _("Take over starred designs")},
		{ID_FAV, IC_STAR, _("Favourite"), _("Favourites come first")},
		{ID_DEL, IC_DELETE, _("Delete"), _("Delete fleet")},
		{ID_USE, IC_USE, _("Everywhere"), _("Use this fleet in every game mode")},
		{ID_DESIGNER, IC_DESIGNER, _("Designer"), _("Unit designer")},
		{ID_FORGE, IC_DESIGNER, _("Hull Forge"), _("Hull Forge: build super hulls")}};
	// (18.11. 12:03) Original-WZ-Bilder, wo es sie gibt (Diskette, Stern, Muelltonne); Neu/Umbenennen/Ueberall im selben WZ-Stil
	struct Look { int img, hi; const char *named; };
	const Look looks[8] = {{-1, -1, "wz_new"}, {-1, -1, "wz_rename"}, {IMAGE_DES_SAVE, IMAGE_DES_SAVEH, nullptr},
	                       {IMAGE_DES_COMMAND, IMAGE_DES_COMMANDDOWN, nullptr}, {IMAGE_DES_BIN, IMAGE_DES_BINH, nullptr}, {-1, -1, "wz_globe"},
	                       {-1, -1, "wz_designer"}, {-1, -1, "wz_forge"}};
	// drei Reihen; in der dritten bleibt die erste Spalte frei (darunter liegt der Zurueck-Knopf)
	static const int slot[8] = {0, 1, 2, 3, 4, 5, 7, 8};
	for (int k = 0; k < 8; ++k)
	{
		const int cx = 16 + (slot[k] % 3) * 70, cy = 298 + (slot[k] / 3) * 52;
		const int bw = 45 * 13 / 10, bh = 24 * 13 / 10;
		if (looks[k].named != nullptr) { wzNamedButton(form, acts[k].id, cx + 8 + (54 - bw) / 2, cy + 2, bw, bh, looks[k].named, acts[k].tip, acts[k].icon); }
		else { wzImgButton(form, acts[k].id, cx + 8 + (54 - bw) / 2, cy + 2, bw, bh, looks[k].img, looks[k].hi, acts[k].tip, acts[k].icon); }
		headLabel(form, ID_HEAD + 60 + static_cast<unsigned>(k), cx - 4, cy + 35, 78, acts[k].cap, pal_RGBA(190, 210, 240, 255), font_small, 16, true);
	}

	// ---- Fusszeile: Zurueck + Meldung
	// TRANSLATORS: "Return", in this context, means "return to previous screen/menu"
	addMultiBut(*form, ID_BACK, 16, PANEL_H - 34, 30, 29, P_("menu", "Return"), IMAGE_RETURN, IMAGE_RETURN_HI, IMAGE_RETURN_HI);   // WZ-Zurueck-Knopf
	auto info = std::make_shared<W_LABEL>();
	form->attach(info);
	info->id = ID_INFO;
	info->setGeometry(240, PANEL_H - 31, W - 256, 22);
	info->setFont(font_regular, WZCOL_TEXT_BRIGHT);
	info->setString(WzString::fromUtf8(s_info));

	// ---- rechts: Entwuerfe der gewaehlten Flotte, nach Art (Favoriten zuerst)
	const int X = 244, RW = W - X - 16;
	s_lineToDesign.clear();
	if (fleets().empty())
	{
		s_iconSpots.push_back({IC_DESIGNER, X, 62, 20});
		headLabel(form, ID_HEAD + 2, X + 24, 62, RW - 24, _("How it works:"), pal_RGBA(255, 214, 110, 255));
		headLabel(form, ID_HEAD + 5, X, 90, RW, _("1. Click \"Designer\" (bottom left)."), WZCOL_TEXT_BRIGHT, font_regular);
		headLabel(form, ID_HEAD + 6, X, 114, RW, _("2. Build units and save each with the star."), WZCOL_TEXT_BRIGHT, font_regular);
		headLabel(form, ID_HEAD + 7, X, 138, RW, _("3. Close the designer - your fleet appears here."), WZCOL_TEXT_BRIGHT, font_regular);
		headLabel(form, ID_HEAD + 8, X, 162, RW, _("4. \"Use everywhere\" puts it in every factory."), WZCOL_TEXT_BRIGHT, font_regular);
		return;
	}
	const nlohmann::json &designs = fleets()[selected]["designs"];
	struct Row { int cat; bool fav; std::string name; int index; };
	std::vector<Row> rows;
	for (int i = 0; designs.is_array() && i < static_cast<int>(designs.size()); ++i)
	{
		const nlohmann::json &e = designs[i];
		rows.push_back(Row{category(e), e.value("modFav", false), e.value("name", std::string("?")), i});
	}
	std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
		if (a.cat != b.cat) { return a.cat < b.cat; }
		if (a.fav != b.fav) { return a.fav; }
		return a.name < b.name;
	});
	struct Line { bool head; std::string text; int index; bool fav = false; int cat = 0; };
	std::vector<Line> lines;
	int lastCat = -1;
	for (const Row &r : rows)
	{
		if (r.cat != lastCat) { lines.push_back(Line{true, categoryName(r.cat), -1, false, r.cat}); lastCat = r.cat; }
		lines.push_back(Line{false, r.name, r.index, r.fav, r.cat});
	}
	const int pages = std::max(1, (static_cast<int>(lines.size()) + DESIGN_ROWS - 1) / DESIGN_ROWS);
	page = std::clamp(page, 0, pages - 1);
	s_iconSpots.push_back({IC_STAR, X, 63, 20});
	headLabel(form, ID_HEAD + 3, X + 24, 62, RW - 24, astringf(_("%s - %d designs"), fleets()[selected].value("name", std::string()).c_str(), static_cast<int>(rows.size())), pal_RGBA(255, 214, 110, 255));
	y = 62 + LINE_H + 4;
	if (rows.empty())
	{
		headLabel(form, ID_HEAD + 9, X, y, RW, _("No designs yet - open the unit designer."), WZCOL_TEXT_MEDIUM, font_regular);
	}
	for (int k = page * DESIGN_ROWS; k < static_cast<int>(lines.size()) && k < (page + 1) * DESIGN_ROWS; ++k)
	{
		const Line &l = lines[k];
		if (l.head)
		{
			s_iconSpots.push_back({l.cat, X, y, 22});
			headLabel(form, ID_HEAD + 30 + static_cast<unsigned>(k - page * DESIGN_ROWS), X + 26, y, RW - 26, l.text, pal_RGBA(120, 210, 255, 255), font_regular_bold);
		}
		else
		{
			if (l.fav) { s_iconSpots.push_back({IC_STAR, X + 30, y + 4, 14}); }
			lineButton(form, ID_DESIGN + static_cast<unsigned>(s_lineToDesign.size()), X + 48, y, RW - 48, l.text);
			s_lineToDesign.push_back(l.index);
		}
		y += LINE_H;
	}
	const int fy = 62 + LINE_H + 4 + DESIGN_ROWS * LINE_H + 2;
	if (pages > 1)
	{
		wzNamedButton(form, ID_PREV, X, fy - 2, 36, 24, "wz_left", _("Previous page"), IC_LEFT);   // MOD (21.11.): Tooltip
		headLabel(form, ID_HEAD + 4, X + 52, fy, 60, astringf("%d / %d", page + 1, pages), WZCOL_TEXT_MEDIUM, font_regular, 22);
		wzNamedButton(form, ID_NEXT, X + 112, fy - 2, 36, 24, "wz_right", _("Next page"), IC_RIGHT);
	}
	headLabel(form, ID_HEAD + 22, X + 170, fy, RW - 170, _("Click: favourite on/off - double-click: edit"), WZCOL_TEXT_MEDIUM, font_small, 22);
}

// (Wunsch 24.11. 14:20 "Bei Doppelklick auf ein gespeichertes Design sollte es wieder in den Unit Designer gehen, falls man es noch
// weiter ausruesten will") - Entwurf in den Designer laden (Definition hinter dem Designer)
namespace modfleetsedit
{
void editDesign(const nlohmann::json &e, int fleet, int designIdx);
void clearEdit();
}

TITLECODE WzFleetTitleUI::run()
{
	screen_disableMapPreview();
	WidgetTriggers const &triggers = widgRunScreen(psWScreen);
	const unsigned id = triggers.empty() ? 0 : triggers.front().widget->id;
	if (id == 0)
	{
		if (CancelPressed()) { changeTitleMode(TITLE); }
		return TITLECODE_CONTINUE;
	}
	auto editText = [&]() -> std::string {
		WIDGET *w = widgGetFromID(psWScreen, ID_EDIT);
		std::string s = w ? w->getString().toUtf8() : std::string();
		s.erase(0, s.find_first_not_of(' '));
		while (!s.empty() && s.back() == ' ') { s.pop_back(); }
		return s;
	};
	bool rebuild = true;
	s_info.clear();
	if (id >= ID_FLEET && id < ID_FLEET + 100)
	{
		selected = static_cast<int>(id - ID_FLEET);
		page = 0;
	}
	else if (id >= ID_DESIGN && id < ID_DESIGN + 200)
	{
		const size_t line = id - ID_DESIGN;
		static unsigned s_lastClickId = 0;
		static UDWORD s_lastClickTime = 0;
		if (!fleets().empty() && line < s_lineToDesign.size())
		{
			nlohmann::json &e = fleets()[selected]["designs"][s_lineToDesign[line]];
			const bool dbl = id == s_lastClickId && realTime - s_lastClickTime < 500;
			s_lastClickId = id;
			s_lastClickTime = realTime;
			e["modFav"] = !e.value("modFav", false);   // beim Doppelklick schaltet der zweite Klick die Marke wieder zurueck
			save();
			if (dbl)
			{
				s_lastClickId = 0;
				modfleetsedit::editDesign(e, selected, static_cast<int>(s_lineToDesign[line]));
				changeTitleUI(std::make_shared<WzUnitDesignTitleUI>(selected));
				rebuild = false;
			}
		}
	}
	else if (id == ID_NEW)
	{
		nlohmann::json f = nlohmann::json::object();
		std::string name = editText();
		if (name.empty() || (!fleets().empty() && name == fleets()[selected].value("name", std::string()))) { name = astringf(_("Fleet %d"), static_cast<int>(fleets().size()) + 1); }
		f["name"] = name; f["fav"] = false; f["designs"] = storedDesigns();
		fleets().push_back(f);
		selected = static_cast<int>(fleets().size()) - 1;
		s_info = astringf(_("New fleet \"%s\" with %d designs"), name.c_str(), static_cast<int>(f["designs"].size()));
		save();
	}
	else if (!fleets().empty() && id == ID_UPDATE)
	{
		const int n = updateFleet(selected);   // Favoriten-Marken der vorhandenen Entwuerfe bleiben erhalten
		s_info = astringf(_("Fleet updated: %d designs"), n);
		save();
	}
	else if (!fleets().empty() && id == ID_RENAME)
	{
		const std::string name = editText();
		if (!name.empty())
		{
			if (s_doc.value("active", std::string()) == fleets()[selected].value("name", std::string())) { s_doc["active"] = name; }
			fleets()[selected]["name"] = name;
			save();
		}
	}
	else if (!fleets().empty() && id == ID_FAV)
	{
		fleets()[selected]["fav"] = !fleets()[selected].value("fav", false);
		save();
	}
	else if (!fleets().empty() && id == ID_DEL)
	{
		s_info = astringf(_("Fleet \"%s\" deleted"), fleets()[selected].value("name", std::string()).c_str());
		fleets().erase(fleets().begin() + selected);
		selected = std::max(0, selected - 1);
		save();
	}
	else if (!fleets().empty() && id == ID_USE)
	{
		// Entwuerfe der Flotte werden zu den gespeicherten Entwuerfen aller Spielarten (ohne die Favoriten-Marke, die das Spiel nicht kennt)
		nlohmann::json list = nlohmann::json::array();
		for (const auto &e : fleets()[selected]["designs"])
		{
			nlohmann::json c = e;
			c.erase("modFav");
			list.push_back(c);
		}
		bool ok = true;
		for (const char *tag : TAGS)
		{
			PHYSFS_mkdir((std::string("userdata/") + tag).c_str());
			const nlohmann::json doc = nlohmann::json{{"version", 1}, {"templates", list}};
			ok = saveJSONToFile(doc, (std::string("userdata/") + tag + "/templates.json").c_str()) && ok;
		}
		s_doc["active"] = fleets()[selected].value("name", std::string());
		save();
		s_info = ok ? astringf(_("\"%s\" is now available in every game mode"), fleets()[selected].value("name", std::string()).c_str())
		            : std::string(_("Could not write the design files"));
	}
	else if (id == ID_DESIGNER) { modfleetsedit::clearEdit(); changeTitleUI(std::make_shared<WzUnitDesignTitleUI>(selected)); rebuild = false; }   // (18.11.) direkt im Hauptmenue
	else if (id == ID_FORGE) { changeTitleUI(std::make_shared<WzHullForgeTitleUI>(wzTitleUICurrent)); rebuild = false; }   // (17:55) reines Menue-Werkzeug
	else if (id == ID_PREV) { --page; }
	else if (id == ID_NEXT) { ++page; }
	else if (id == ID_BACK) { changeTitleMode(TITLE); rebuild = false; }
	else { rebuild = false; }
	if (rebuild) { build(); }
	return TITLECODE_CONTINUE;
}

void WzFleetTitleUI::render()
{
	widgDisplayScreen(psWScreen);
}

// =====================================================================================================================
// MOD (Wunsch 18.11. 00:51): "Der Einheiteneditor soll ganz einfach im Hauptmenue funktionieren, mit allem freigeschaltet - ohne in ein
// Spiel reinzugehen." Eigener Editor im Flotten-Menue: die Bauteile kommen direkt aus den Stats-Dateien (alles, was im Spiel im Designer
// erscheinen kann = "designable"), Regeln wie im Spiel (VTOL nur mit VTOL-Waffen, Waffenplaetze des Rumpfs, keine Systeme mit Waffen),
// drehende 3D-Vorschau der Einheit. "Save to fleet" legt den Entwurf in die gewaehlte Flotte UND in die gespeicherten Entwuerfe aller
// Spielarten (er steht in jeder Fabrik bereit). Rein lokal, beruehrt keinen Spielzustand.
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/pietypes.h"
#include "lib/framework/fixedpoint.h"
#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/transform.hpp>

namespace
{
enum PartKind { PK_BODY = 0, PK_PROP, PK_WEAPON, PK_SENSOR, PK_CONSTRUCT, PK_REPAIR, PK_ECM, PK_BRAIN };   // (24.11.) + Kommandoturm
struct DesignPart
{
	std::string id, name, model, mount, size, ptype;
	std::string walker;        // Antrieb: Lauf-Beine (walkerModel) - der Rumpf sitzt auf ihrer Huefte
	int kind = 0, power = 0, weight = 0, hp = 0, armour = 0, engine = 0, speed = 0, slots = 1, damage = 0, range = 0, firePause = 0, wsize = 2;
	bool vtolWeapon = false, direct = true;
	nlohmann::json extra;      // Rumpf: propulsionExtraModels
	glm::mat4 propM = glm::mat4(1.f);   // Verbundrumpf: Lage des Fahrgestells (Laufwerk folgt ihm)
	float lift = 0.f;                   // Verbundrumpf: Hub ueber dem Laufwerk (modcompbody::liftOf)
	std::vector<glm::vec3> tilt;        // (27.11.) Verbundrumpf: je Platz Blickrichtung/Nase/Rolle in Grad (Hull Forge)
	std::vector<modcompbody::Drive> drives;   // (27.11.) Verbundrumpf: Antriebsplaetze (body leer = Fahrgestell, siehe driveBody)
	std::vector<std::string> driveBody;       // (27.11.) je Antriebsplatz: Rumpf, dessen Laufwerk-Modelle er traegt
	bool noProp = false;                // (27.11.) SupCom-Einheit (bzw. Verbundrumpf mit ihr als Fahrgestell): Laufwerk steckt im Modell
	std::string source;                 // Rumpf: Herkunft/Fraktion wie im Rumpf-Baukasten ("Scavengers", "Project", "Metal Fatigue" ...)
	int sourceRank = 99;                // Reihenfolge der Fraktions-Reiter
	std::string brainWeapon;            // Kommandoturm: seine Waffe (CommandTurret1)
	std::string origin = "WZ";          // andere Teile: wer sie einfuehrt - "WZ" (Grundspiel), "Scavengers", "Metal Fatigue", "C&C 3", "Naval", "Custom"
};
std::vector<DesignPart> s_parts;
std::map<std::string, int> s_powerRatio;
std::map<std::string, std::string> s_propType;   // alle Antriebe (auch nicht waehlbare): ID -> Art (fuer die Laufwerk-Modelle der Rumpfe)
std::map<std::string, std::string> s_propModel;  // alle Antriebe: ID -> eigenes Modell (Ersatz-Laufwerk, wenn der Rumpf keines hat)
bool s_partsLoaded = false;
// (Wunsch 24.11. 01:23 "Die mehreren Turrets sind nicht ausruestbar ... so gestaltet, dass man sehr sehr viele Turrets ausruesten kann"):
// Turm-Plaetze wie im Spiel bis 30 (MAX_WEAPONS); (01:26 "Systemtuerme und Waffentuerme gleichzeitig") jeder Platz traegt eine Waffe ODER
// einen Systemturm (Sensor/Schild, Stoersender, Bau- oder Reparaturturm - Bau/Reparatur hoechstens einer), wie modslots im Spiel.
constexpr int MAX_DSLOTS = 30;
constexpr int MAX_DSYS = 16;       // = MOD_MAX_SYS
struct DesignState
{
	int body = -1, prop = -1;
	int slot[MAX_DSLOTS];
	int cur = 0;                   // gewaehlter Turm-Platz
	int slotPage = 0;              // Seite der Platz-Leiste
	int tab = 0, page = 0;         // Reiter: 0 Rumpf, 1 Antrieb, 2 Tuerme
	std::string faction;           // gewaehlte Fraktion (Herkunft der Rumpfe, oben die Reiterleiste)
	int editFleet = -1, editIdx = -1;   // per Doppelklick geladener Entwurf (Flotte, Platz) - Speichern ersetzt ihn, solange der Name bleibt
	std::string editId, editName;
	int sub[3] = {0, 0, 0};   // (18.11. 12:03) Unterreiter je Reiter: Rumpf leicht/mittel/schwer/ueberschwer, Antrieb Land/Wasser/Luft, Turm Waffen/Systeme
	std::string name;
	bool edited = false;
	std::string info;
	int driveProp[8];              // (27.11.) Antrieb je Antriebsplatz (Index in s_parts; -1 = wie Antrieb 1 = prop)
	int curDrive = 0;              // (27.11.) gewaehlter Antriebsplatz (Reiter Antrieb)
	DesignState() { for (int &v : slot) { v = -1; } for (int &v : driveProp) { v = -1; } }
};
DesignState s_ds;
// (Wunsch 27.11. 19:24 "im Unit Designer sehe ich keine Waffenslots leuchten ... aus- und abschaltbar, einzeln aufschaltbar ... ein Button fuer
// alle anzeigen/ausblenden, standardmaessig eingeblendet"): welche Plaetze im Vorschaufenster leuchten
bool s_slotShow[MAX_DSLOTS];
const bool s_slotShowInit = []() { for (bool &b : s_slotShow) { b = true; } return true; }();
constexpr unsigned ID_D_TAB = 4599600, ID_D_ITEM = 4599620, ID_D_PREV = 4599660, ID_D_NEXT = 4599661, ID_D_SAVE = 4599662, ID_D_BACK = 4599663;
constexpr unsigned ID_D_NAME = 4599664, ID_D_VIEW = 4599665, ID_D_INFO = 4599666, ID_D_HEAD = 4599700, ID_D_SUB = 4599680;
constexpr unsigned ID_D_FORGE = 4599667;   // (23.11. 04:23) Rumpf-Baukasten
constexpr unsigned ID_D_SLOT = 4599800, ID_D_SPREV = 4599840, ID_D_SNEXT = 4599841, ID_D_SCLEAR = 4599842, ID_D_SFILL = 4599843;
constexpr unsigned ID_D_SLOTEYE = 4599844;   // (27.11.) alle Plaetze anzeigen/ausblenden
constexpr unsigned ID_D_DRIVE = 4599870;     // (27.11.) + Index: Antriebsplatz-Knoepfe (bis 8)
constexpr unsigned ID_D_FACTION = 4599850;   // + Index (Fraktions-Reiter)
constexpr int D_TOP = 84;                    // Oberkante der Reiter/Vorschau (darueber die Fraktions-Reiter)
constexpr int D_ROWS = 12;
int s_rows = D_ROWS;               // Zeilen der Teile-Liste (je nach Platz ueber der Liste)

bool jsonTruthy(const nlohmann::json &j)
{
	if (j.is_boolean()) { return j.get<bool>(); }
	if (j.is_number()) { return j.get<double>() != 0.0; }
	return false;
}
int jsonInt(const nlohmann::json &e, const char *k, int def = 0)
{
	auto it = e.find(k);
	if (it == e.end() || !it->is_number()) { return def; }
	return static_cast<int>(it->get<double>());
}
std::string jsonStr(const nlohmann::json &e, const char *k)
{
	auto it = e.find(k);
	return (it != e.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

void loadParts()
{
	if (s_partsLoaded) { return; }
	s_partsLoaded = true;
	struct Src { const char *file; int kind; };
	const Src srcs[] = {{"stats/body.json", PK_BODY}, {"stats/propulsion.json", PK_PROP}, {"stats/weapons.json", PK_WEAPON}, {"stats/sensor.json", PK_SENSOR},
	                    {"stats/construction.json", PK_CONSTRUCT}, {"stats/repair.json", PK_REPAIR}, {"stats/ecm.json", PK_ECM}, {"stats/brain.json", PK_BRAIN}};
	std::map<std::string, std::pair<std::string, std::string>> weaponModels;   // alle Waffen: Modell + Halterung (Kommandoturm zeigt seine Waffe)
	for (const Src &src : srcs)
	{
		if (!PHYSFS_exists(src.file)) { continue; }
		auto j = parseJsonFile(src.file);
		if (!j.has_value() || !j->is_object()) { continue; }
		for (auto it = j->begin(); it != j->end(); ++it)
		{
			const nlohmann::json &e = it.value();
			if (!e.is_object()) { continue; }
			if (src.kind == PK_PROP) { s_propType[it.key()] = jsonStr(e, "type"); s_propModel[it.key()] = jsonStr(e, "model"); }
			if (src.kind == PK_WEAPON) { weaponModels[it.key()] = {jsonStr(e, "model"), jsonStr(e, "mountModel")}; }
			auto des = e.find("designable");
			const bool designable = des != e.end() && jsonTruthy(*des);
			// (Wunsch 23.11. 23:24 "Der Fleet-Designer hat nicht alle Vanilla-Fraktionsruempfe"): die WZ-Fraktionsruempfe sind im Paket
			// nicht "designable" (im Spiel ersetzt durch die Scav-Fassungen) - hier gehoeren sie trotzdem dazu.
			// (Wunsch 24.11. 02:13 "natuerlich ALLE Antriebe einbauen ... sauber zwischen Scavenger und anderen Fraktionen trennen"):
			// alle Rumpfe des Rumpf-Baukastens (nach Fraktion getrennt), alle Fahrzeug-Antriebe (auch Luftkissen und die WZ-Originale der
			// Scav-Laufwerke) und die im Paket versteckten Vanilla-Teile (VTOL-MG/-Kanone, Plasma-Werfer, Spaeher-Turm, Spaten)
			bool want = designable;
			if (src.kind == PK_BODY)
			{
				const modcompbody::BodyInfo *bi = modcompbody::bodyInfo(it.key());
				want = designable || modcompbody::isWzFactionBody(it.key()) || (bi != nullptr && bi->listed);
				// (27.11.) SupCom-Gebaeude (SC_xxB/SC_xxC) sind nur Bauteile im Rumpf-Baukasten, keine Einheiten-Rumpfe
				const std::string &k = it.key();
				if (k.size() >= 6 && k.rfind("SC_", 0) == 0 && (k[5] == 'B' || k[5] == 'C')) { want = false; }
			}
			else if (src.kind == PK_PROP)
			{
				const std::string &k = it.key();
				want = k != "ZNULLPROP" && k.rfind("BaBa", 0) != 0 && k.rfind("CyborgLegs", 0) != 0 && k.rfind("MFL_", 0) != 0 && k != "RailBogie"
				       && (jsonStr(e, "type") != "Legged" || !jsonStr(e, "walkerModel").empty());
			}
			else if (src.kind == PK_WEAPON)
			{
				static const char *const HIDDEN_VANILLA[] = {"Cannon1-VTOL", "MG1-VTOL", "MG2-VTOL", "PlasmaHeavy", "SpyTurret01"};
				for (const char *h : HIDDEN_VANILLA) { if (it.key() == h) { want = true; } }
			}
			else
			{
				// (Wunsch 24.11. 15:19 "Commander Turret fehlt ueberall ... Bitte jetzt endlich mal ALLE SYSTEMTUERME EGAL WAS verfuegbar machen"):
				// jeder Sensor-, Stoersender-, Bau-, Reparatur- und Kommandoturm - nur keine Platzhalter (ZNULL...) und keine eingebauten Teile
				// (Lage DEFAULT: Standard-Sensor, Auto-Reparatur, Fatboy-Schild - die sind kein Turm)
				want = it.key().rfind("ZNULL", 0) != 0 && jsonStr(e, "location") != "DEFAULT";
			}
			if (!want) { continue; }
			DesignPart p;
			p.id = it.key();
			p.kind = src.kind;
			p.name = jsonStr(e, "name");
			p.name.erase(std::remove(p.name.begin(), p.name.end(), '*'), p.name.end());
			if (p.name.empty()) { p.name = p.id; }
			p.power = jsonInt(e, "buildPower");
			p.weight = jsonInt(e, "weight");
			p.hp = jsonInt(e, "hitpoints");
			switch (src.kind)
			{
			case PK_BODY:
				p.model = jsonStr(e, "model"); p.size = jsonStr(e, "size"); p.slots = std::max(1, jsonInt(e, "weaponSlots", 1));
				p.armour = jsonInt(e, "armourKinetic"); p.engine = jsonInt(e, "powerOutput");
				if (e.contains("propulsionExtraModels") && e["propulsionExtraModels"].is_object()) { p.extra = e["propulsionExtraModels"]; }
				break;
			case PK_PROP:
				p.model = jsonStr(e, "model"); p.ptype = jsonStr(e, "type"); p.speed = jsonInt(e, "speed"); p.hp = jsonInt(e, "hitpointPctOfBody");
				p.walker = jsonStr(e, "walkerModel");
				break;
			case PK_WEAPON:
			{
				p.model = jsonStr(e, "model"); p.mount = jsonStr(e, "mountModel");
				p.vtolWeapon = jsonInt(e, "numAttackRuns") > 0;
				const std::string mv = jsonStr(e, "movement");
				p.direct = mv.empty() || mv == "DIRECT" || mv == "HOMING-DIRECT";
				p.damage = jsonInt(e, "damage"); p.range = jsonInt(e, "longRange"); p.firePause = jsonInt(e, "firePause");
				p.wsize = jsonInt(e, "weaponSize", 2);
				break;
			}
			case PK_REPAIR:
				p.model = jsonStr(e, "model"); p.mount = jsonStr(e, "mountModel");
				break;
			case PK_BRAIN:
			{
				// Kommandoturm: sein Modell ist das der Kommando-Waffe ("turret")
				p.mount = jsonStr(e, "turret");   // vorlaeufig: Waffen-ID, unten durch Modell/Halterung ersetzt
				break;
			}
			default:
				p.model = jsonStr(e, "sensorModel"); p.mount = jsonStr(e, "mountModel");
				break;
			}
			s_parts.push_back(p);
		}
	}
	for (DesignPart &p : s_parts)
	{
		if (p.kind != PK_BRAIN) { continue; }
		const auto wm = weaponModels.find(p.mount);
		p.brainWeapon = p.mount;
		p.model = wm != weaponModels.end() ? wm->second.first : std::string();
		p.mount = wm != weaponModels.end() ? wm->second.second : std::string();
	}
	// Fraktion je Teil
	for (DesignPart &p : s_parts)
	{
		if (p.kind == PK_BODY)
		{
			const modcompbody::BodyInfo *bi = modcompbody::bodyInfo(p.id);
			p.source = bi != nullptr ? bi->source : std::string("Mods");
			p.sourceRank = bi != nullptr ? bi->sourceRank : 11;
			continue;
		}
		// (Wunsch 24.11. 14:20 "Was nicht im Mod/Version/Fraktion urspruenglich vorhanden ist, kommt jeweils NUR in seinen spezifischen Mod,
		// der ihn einfuehrt"): Herkunft je Teil
		auto ends = [&](const char *t) { const size_t n = std::strlen(t); return p.id.size() >= n && p.id.compare(p.id.size() - n, n, t) == 0; };
		auto starts = [&](const char *t) { return p.id.rfind(t, 0) == 0; };
		auto oneOf = [&](std::initializer_list<const char *> l) { for (const char *x : l) { if (p.id == x) { return true; } } return false; };
		if (ends("-Scav") || ends("-SCAVS") || starts("Baba") || starts("Scav") || starts("scav")
		    || oneOf({"Helicopter", "WreckBall"})) { p.origin = "Scavengers"; }   // Hubschrauber: Ultimate Scavengers; Abrissbirne am Scav-Bagger
		else if (oneOf({"MechWalker", "AMS-AssaultGun", "AMS-Laser", "CollectiveLaserGunner", "MineFlail", "NukeTEL", "ShieldEmitterMobile"}))
		{
			p.origin = "Custom";   // eigene Bauten dieses Mods (Mech-Beine des Mechbauprojekts, AMS, Kollektiv-Schuetze, Minenraeumer, Atomrakete, Schild)
		}
		else if (starts("MF") || p.id == "DrillRig") { p.origin = "Metal Fatigue"; }
		else if (p.id == "HarvesterTracks") { p.origin = "C&C 3"; }
		else if (oneOf({"Naval", "RocketSuperQuarter"})) { p.origin = "Naval"; }
	}
	std::stable_sort(s_parts.begin(), s_parts.end(), [](const DesignPart &a, const DesignPart &b) {
		if (a.kind != b.kind) { return a.kind < b.kind; }
		if (a.power != b.power) { return a.power < b.power; }
		return a.name < b.name;
	});
	if (PHYSFS_exists("stats/propulsiontype.json"))
	{
		auto j = parseJsonFile("stats/propulsiontype.json");
		if (j.has_value() && j->is_object())
		{
			for (auto it = j->begin(); it != j->end(); ++it) { if (it->is_object()) { s_powerRatio[it.key()] = jsonInt(*it, "powerRatioMult", 100); } }
		}
	}
}

const DesignPart *partAt(int i) { return (i >= 0 && i < static_cast<int>(s_parts.size())) ? &s_parts[static_cast<size_t>(i)] : nullptr; }

const iIMDShape *shapeOf(const std::string &model)
{
	if (model.empty()) { return nullptr; }
	iIMDBaseShape *b = modelGet(WzString::fromUtf8(model));
	return b ? b->displayModel() : nullptr;
}

void drawShape(const iIMDShape *shape, const glm::mat4 &m)
{
	for (const iIMDShape *imd = shape; imd != nullptr; imd = imd->next.get())
	{
		pie_Draw3DButton(imd, imd->getTeamColourForModel(0), m, glm::mat4(1.f));
	}
}

/// (Wunsch 23.11. 17:55) Ueber-Rumpfe aus dem Rumpf-Baukasten (im Spiel oder im Menue gespeichert) als Rumpfe im Einheiten-Designer:
/// Modell aus den Teilen, Werte zusammengerechnet; Laufwerke wie beim Fahrgestell
void refreshCompositeParts()
{
	auto idOf = [](int i) { const DesignPart *p = partAt(i); return p ? p->id : std::string(); };
	const std::string keepBody = idOf(s_ds.body), keepProp = idOf(s_ds.prop);
	std::string keepT[MAX_DSLOTS];
	for (int k = 0; k < MAX_DSLOTS; ++k) { keepT[k] = idOf(s_ds.slot[k]); }
	modcompbody::refreshDefs();
	s_parts.erase(std::remove_if(s_parts.begin(), s_parts.end(), [](const DesignPart &p) { return p.kind == PK_BODY && p.id.rfind("CompBody", 0) == 0; }), s_parts.end());
	std::vector<DesignPart> add;
	for (const modcompbody::Def &d : modcompbody::defs())
	{
		if (d.deleted || d.parts.empty() || modcompbody::bodyInfo(d.parts[0].body) == nullptr) { continue; }
		const std::string model = modcompbody::previewModel(d);
		if (model.empty()) { continue; }
		const modcompbody::Totals t = modcompbody::totals(d);
		DesignPart p;
		p.id = d.id; p.kind = PK_BODY; p.name = d.name; p.model = model;
		p.power = static_cast<int>(t.cost); p.weight = static_cast<int>(t.weight); p.hp = static_cast<int>(t.hp);
		p.armour = static_cast<int>(t.armour); p.engine = static_cast<int>(t.power); p.slots = std::max(1, static_cast<int>(t.slots));
		p.size = modcompbody::sizeText(t.size);
		p.propM = modcompbody::partMatrix(d.parts[0]);   // (24.11.) Laufwerk unter dem Fahrgestell wie im Spiel
		p.lift = d.drives.empty() ? modcompbody::liftOf(d) : 0.f;   // (27.11.) mit Antriebsplaetzen: stehen, wo sie gesetzt sind (wie im Spiel)
		p.noProp = d.parts[0].body.rfind("SC_", 0) == 0;
		for (const modcompbody::Drive &dr : d.drives)
		{
			if (p.drives.size() >= modcompbody::MAX_DRIVES) { break; }
			p.drives.push_back(dr);
			p.driveBody.push_back(dr.body.empty() ? d.parts[0].body : dr.body);
		}
		for (const modcompbody::Mount &mt : d.mounts) { p.tilt.emplace_back(mt.yaw, mt.pitch, mt.roll); }
		p.source = "Custom"; p.sourceRank = -1;   // (Wunsch 24.11. 14:20 "Alle eigenen Builds kommen unter einen Custom-Reiter")
		for (const DesignPart &q : s_parts) { if (q.kind == PK_BODY && q.id == d.parts[0].body) { p.extra = q.extra; break; } }
		add.push_back(p);
	}
	s_parts.insert(s_parts.end(), add.begin(), add.end());
	std::stable_sort(s_parts.begin(), s_parts.end(), [](const DesignPart &a, const DesignPart &b) {
		if (a.kind != b.kind) { return a.kind < b.kind; }
		if (a.power != b.power) { return a.power < b.power; }
		return a.name < b.name;
	});
	auto find = [](const std::string &id, int kindMin, int kindMax) {
		if (id.empty()) { return -1; }
		for (size_t i = 0; i < s_parts.size(); ++i) { if (s_parts[i].id == id && s_parts[i].kind >= kindMin && s_parts[i].kind <= kindMax) { return static_cast<int>(i); } }
		return -1;
	};
	s_ds.body = find(keepBody, PK_BODY, PK_BODY);
	s_ds.prop = find(keepProp, PK_PROP, PK_PROP);
	for (int k = 0; k < MAX_DSLOTS; ++k) { s_ds.slot[k] = find(keepT[k], PK_WEAPON, PK_BRAIN); }
	if (s_ds.body < 0) { for (size_t i = 0; i < s_parts.size(); ++i) { if (s_parts[i].kind == PK_BODY) { s_ds.body = static_cast<int>(i); break; } } }
	if (s_ds.prop < 0) { for (size_t i = 0; i < s_parts.size(); ++i) { if (s_parts[i].kind == PK_PROP) { s_ds.prop = static_cast<int>(i); break; } } }
}
bool vtolProp() { const DesignPart *p = partAt(s_ds.prop); return p != nullptr && p->ptype == "Lift"; }
bool flyOnlyBody() { const DesignPart *b = partAt(s_ds.body); return b != nullptr && b->id == "Body-Manta"; }   // Nurfluegler: nur Senkrechtstarter
bool isSystemKind(int kind) { return kind >= PK_SENSOR; }
bool isRoleKind(int kind) { return kind == PK_CONSTRUCT || kind == PK_REPAIR; }

/// (Wunsch 24.11. 02:13 / 14:20) Fraktions-Reiter: Rumpfe nach Herkunft. Teile:
///  - Scavs: nur Scav-Antriebe (keine WZ-Antriebe), Scav-Tuerme und die Tuerme des Grundspiels
///  - WZ-Fraktionen, WZ2120 und die Mod-Reiter: Antriebe und Tuerme des Grundspiels, dazu was der jeweilige Mod selbst einfuehrt
///    (MF-Antriebe nur bei MF, Schiffsantrieb/-raketen nur bei Naval, Ernter-Ketten nur bei C&C 3)
///  - Custom (eigene Rumpfe aus dem Baukasten): alles, auch die eigenen Teile dieses Mods (Mech-Beine, AMS, Schild ...), die es NUR hier gibt
[[maybe_unused]] bool scavFaction() { return s_ds.faction == "Scavengers"; }
bool inFaction(const DesignPart &p)
{
	const std::string &f = s_ds.faction;
	if (p.kind == PK_BODY) { return p.source == f; }
	if (f == "Custom") { return true; }
	if (p.kind >= PK_SENSOR) { return true; }   // (Wunsch 24.11. 15:19 "ALLE Systemtuerme, egal was") Systemtuerme in jedem Reiter
	if (p.origin == "WZ") { return p.kind != PK_PROP || f != "Scavengers"; }
	return p.origin == f;
}
/// Fraktionen mit Rumpfen, in der Reihenfolge des Rumpf-Baukastens
std::vector<std::string> factionList()
{
	std::vector<std::pair<int, std::string>> v;
	for (const DesignPart &p : s_parts)
	{
		if (p.kind != PK_BODY) { continue; }
		bool have = false;
		for (const auto &e : v) { if (e.second == p.source) { have = true; } }
		if (!have) { v.emplace_back(p.sourceRank, p.source); }
	}
	std::stable_sort(v.begin(), v.end(), [](const std::pair<int, std::string> &a, const std::pair<int, std::string> &b) { return a.first < b.first; });
	std::vector<std::string> out;
	for (const auto &e : v) { out.push_back(e.second); }
	return out;
}
const char *factionLabel(const std::string &s)
{
	if (s == "Project") { return "Projekt"; }
	if (s == "New Paradigm") { return "NP"; }
	if (s == "Collective") { return "Kollektiv"; }
	if (s == "Warzone 2100") { return "WZ Transp."; }
	if (s == "Scavengers") { return "Scavs"; }
	if (s == "Metal Fatigue") { return "MF"; }
	if (s == "Supreme Commander") { return "SupCom"; }
	if (s == "Custom") { return "Custom"; }
	return s.c_str();
}

/// Turm-Plaetze des Rumpfs (wie im Spiel: hoechstens so viele, wie das Modell Anschluesse hat; bis 30)
/// (27.11.) Antriebsplaetze des gewaehlten Rumpfes (0 = ein Laufwerk wie bisher)
int bodyDrives()
{
	const DesignPart *b = partAt(s_ds.body);
	return b != nullptr ? std::min(static_cast<int>(b->drives.size()), 8) : 0;
}

/// (27.11.) Antrieb des Antriebsplatzes k (Platz 1 = der Antrieb des Entwurfs)
const DesignPart *driveProp(int k)
{
	if (k > 0 && k < 8 && s_ds.driveProp[k] >= 0)
	{
		const DesignPart *q = partAt(s_ds.driveProp[k]);
		if (q != nullptr && q->kind == PK_PROP) { return q; }
	}
	return partAt(s_ds.prop);
}

int bodySlots()
{
	const DesignPart *b = partAt(s_ds.body);
	if (b == nullptr) { return 1; }
	int n = std::clamp(b->slots, 1, MAX_DSLOTS);
	if (const iIMDShape *m = shapeOf(b->model))
	{
		if (!m->connectors.empty()) { n = std::min(n, static_cast<int>(m->connectors.size())); }
	}
	return std::max(1, n);
}
/// freie Belegung (Waffen und Systeme gemischt, jeder Platz frei) wie modslots::usable: mindestens zwei Plaetze, kein Senkrechtstarter
bool freeSlots() { return bodySlots() >= 2 && !vtolProp(); }

bool turretValid(int slot, const DesignPart &t)
{
	if (t.kind < PK_WEAPON) { return false; }
	if (!inFaction(t)) { return false; }
	// Kommandoturm: nur allein auf Platz 1 (Kommandeure tragen nur ihn); steht er dort, bleiben die anderen Plaetze leer
	if (t.kind == PK_BRAIN && slot != 0) { return false; }
	if (slot > 0) { const DesignPart *t0 = partAt(s_ds.slot[0]); if (t0 != nullptr && t0->kind == PK_BRAIN) { return false; } }
	if (slot < 0 || slot >= bodySlots()) { return false; }
	if (vtolProp())
	{
		if (t.kind != PK_WEAPON || !t.vtolWeapon || !t.direct) { return false; }
	}
	else if (t.kind == PK_WEAPON && t.vtolWeapon)
	{
		return false;
	}
	if (t.kind == PK_WEAPON)
	{
		const DesignPart *b = partAt(s_ds.body);
		const bool light = b != nullptr && b->size == "LIGHT";
		if (t.wsize == 0 && !light) { return false; }
		if (t.wsize == 1 && light) { return false; }
	}
	if (!freeSlots())
	{
		// wie bisher (ein Platz, Senkrechtstarter): weitere Plaetze nur Waffen neben einer Waffe
		if (slot > 0)
		{
			const DesignPart *t0 = partAt(s_ds.slot[0]);
			if (t.kind != PK_WEAPON || t0 == nullptr || t0->kind != PK_WEAPON) { return false; }
		}
		return true;
	}
	if (isSystemKind(t.kind))
	{
		int sys = 0;
		for (int k = 0; k < bodySlots(); ++k)
		{
			const DesignPart *o = k != slot ? partAt(s_ds.slot[k]) : nullptr;
			if (o != nullptr && isSystemKind(o->kind)) { ++sys; }
		}
		if (sys >= MAX_DSYS) { return false; }
	}
	return true;
}

/// (Wunsch 24.11. 01:23 "alle Antriebe ... nicht sichtbar (an der Einheit nach dem Ausruesten)" / 02:13 "natuerlich ALLE Antriebe"):
/// hat der Rumpf fuer diesen Antrieb kein eigenes Laufwerk-Modell, zeigt die Vorschau das Modell des Antriebs selbst (wie im Spiel) -
/// ausser bei Rumpfen mit eingebautem Fahrwerk (Pluenderer-Autos ...: kein einziges Boden-Laufwerk) fuer Raeder/Ketten
const iIMDShape *fallbackProp(const DesignPart &b, const DesignPart &p)
{
	if (b.noProp || b.id.rfind("SC_", 0) == 0) { return nullptr; }   // (27.11.) SupCom-Einheiten bringen Ketten/Beine/Rumpf im Modell mit
	if (!p.walker.empty()) { return nullptr; }
	if (b.extra.is_object() && b.extra.contains(p.id)) { return nullptr; }
	const bool ground = p.ptype == "Wheeled" || p.ptype == "Half-Tracked" || p.ptype == "Tracked" || p.ptype == "Legged";
	if (ground)
	{
		bool builtIn = true;
		if (b.extra.is_object())
		{
			for (auto it = b.extra.begin(); it != b.extra.end(); ++it)
			{
				const auto ty = s_propType.find(it.key());
				if (ty == s_propType.end() || ty->second != "Lift") { builtIn = false; break; }
			}
		}
		if (builtIn) { return nullptr; }
	}
	std::string m = p.model;
	if (m.empty() && p.id.size() > 5 && p.id.compare(p.id.size() - 5, 5, "-Scav") == 0)
	{
		const auto it = s_propModel.find(p.id.substr(0, p.id.size() - 5));   // Scav-Fassung ohne eigenes Modell: das des Originals
		if (it != s_propModel.end()) { m = it->second; }
	}
	return shapeOf(m);
}

// (Wunsch 18.11. 12:03) "statt 1-5 besser sortiert in leicht, mittel, schwer, ueberschwer; Antrieb vorab eingeteilt in Land, Wasser, Luft;
// die Turmsektion wieder eingeteilt in Waffen und Systeme - genau wie bei WZ"
int bodyClass(const DesignPart &p)
{
	if (p.size == "LIGHT") { return 0; }
	if (p.size == "MEDIUM") { return 1; }
	if (p.size == "HEAVY") { return 2; }
	return 3;   // SUPER HEAVY und groesser
}
bool propIn(const DesignPart &p, int cat)
{
	const bool air = p.ptype == "Lift";
	const bool water = p.ptype == "Propellor" || p.ptype == "Hover";   // Luftkissen faehrt auf Land und Wasser
	const bool land = !air && p.ptype != "Propellor";
	return cat == 0 ? land : (cat == 1 ? water : air);
}
/// Systemtuerme auf dem gewaehlten Platz moeglich? (freie Belegung: ueberall; sonst nur auf Platz 1)
bool systemsHere() { return freeSlots() || s_ds.cur == 0; }
int subCount(int tab)
{
	if (tab == 0) { return 4; }
	if (tab == 1) { return 3; }
	return systemsHere() ? 2 : 1;
}
bool inSub(int tab, const DesignPart &p)
{
	const int c = s_ds.sub[std::clamp(tab, 0, 2)];
	if (tab == 0) { return bodyClass(p) == c; }
	if (tab == 1) { return propIn(p, c); }
	return c == 0 ? p.kind == PK_WEAPON : p.kind != PK_WEAPON;
}
/// Unterreiter, in dem das gewaehlte Teil steht (beim Wechsel des Reiters oder Platzes)
int subOfCurrent(int tab)
{
	if (tab == 0) { const DesignPart *b = partAt(s_ds.body); return b ? bodyClass(*b) : 0; }
	if (tab == 1) { const DesignPart *p = partAt(s_ds.prop); if (!p) { return 0; } for (int c = 0; c < 3; ++c) { if (propIn(*p, c)) { return c; } } return 0; }
	const DesignPart *t = partAt(s_ds.slot[std::clamp(s_ds.cur, 0, MAX_DSLOTS - 1)]);
	return (t && t->kind != PK_WEAPON) ? 1 : 0;
}

std::vector<int> candidates(int tab)
{
	std::vector<int> out;
	for (int i = 0; i < static_cast<int>(s_parts.size()); ++i)
	{
		const DesignPart &p = s_parts[static_cast<size_t>(i)];
		if (tab == 0 && p.kind == PK_BODY && inFaction(p) && inSub(0, p)) { out.push_back(i); }
		else if (tab == 1 && p.kind == PK_PROP && inFaction(p) && (!flyOnlyBody() || p.ptype == "Lift") && inSub(1, p)) { out.push_back(i); }
		else if (tab == 2 && turretValid(s_ds.cur, p) && inSub(2, p)) { out.push_back(i); }
	}
	if (tab == 0)
	{
		// (Wunsch 23.11. 23:24) Ruempfe nach Herkunft: Projekt, New Paradigm, Kollektiv, Nexus, Transporter, Scavengers, WZ2120, MF, ...
		auto rank = [](const DesignPart &p) { const modcompbody::BodyInfo *bi = modcompbody::bodyInfo(p.id); return bi != nullptr ? bi->sourceRank : 99; };
		std::stable_sort(out.begin(), out.end(), [&](int a, int c) { return rank(s_parts[static_cast<size_t>(a)]) < rank(s_parts[static_cast<size_t>(c)]); });
	}
	return out;
}

/// kurze Herkunft eines Rumpfes fuer die Liste (WZ-Fraktion bzw. Mod-Quelle)
std::string bodyTag(const DesignPart &p)
{
	const modcompbody::BodyInfo *b = modcompbody::bodyInfo(p.id);
	if (b == nullptr) { return std::string(); }
	const std::string &s = b->source;
	if (s == "Project") { return "Projekt"; }
	if (s == "New Paradigm") { return "NP"; }
	if (s == "Collective") { return "Kollektiv"; }
	if (s == "Nexus") { return "Nexus"; }
	if (s == "Warzone 2100") { return "WZ"; }
	if (s == "Scavengers") { return "Scav"; }
	if (s == "Metal Fatigue") { return "MF"; }
	if (s == "Supreme Commander") { return "SupCom"; }
	return s;
}

/// Entwurf an Rumpf/Antrieb anpassen: Antrieb mit Modell, ungueltige Tuerme weg, hoechstens ein Bau-/Reparaturturm
void fixTurrets()
{
	if (flyOnlyBody() && !vtolProp())
	{
		for (int i = 0; i < static_cast<int>(s_parts.size()); ++i)
		{
			if (s_parts[static_cast<size_t>(i)].kind == PK_PROP && s_parts[static_cast<size_t>(i)].ptype == "Lift") { s_ds.prop = i; break; }
		}
	}
	// Rumpf aus der gewaehlten Fraktion (Wechsel des Fraktions-Reiters): der erste ihrer Rumpfe
	const DesignPart *b = partAt(s_ds.body);
	if (b == nullptr || !inFaction(*b))
	{
		for (int i = 0; i < static_cast<int>(s_parts.size()); ++i)
		{
			if (s_parts[static_cast<size_t>(i)].kind == PK_BODY && inFaction(s_parts[static_cast<size_t>(i)])) { s_ds.body = i; break; }
		}
	}
	// Antrieb aus der Fraktion: das Gegenstueck (Scav-Laufwerk <-> WZ-Original), sonst dieselbe Art, sonst der erste
	const DesignPart *p = partAt(s_ds.prop);
	if (p == nullptr || !inFaction(*p) || (flyOnlyBody() && p->ptype != "Lift"))
	{
		std::string twin;
		if (p != nullptr) { twin = (p->id.size() > 5 && p->id.compare(p->id.size() - 5, 5, "-Scav") == 0) ? p->id.substr(0, p->id.size() - 5) : p->id + "-Scav"; }
		int best = -1;
		for (int i = 0; i < static_cast<int>(s_parts.size()); ++i)
		{
			const DesignPart &q = s_parts[static_cast<size_t>(i)];
			if (q.kind != PK_PROP || !inFaction(q) || (flyOnlyBody() && q.ptype != "Lift")) { continue; }
			if (q.id == twin) { best = i; break; }
			if (best < 0 || (p != nullptr && q.ptype == p->ptype && s_parts[static_cast<size_t>(best)].ptype != p->ptype)) { best = i; }
		}
		if (best >= 0) { s_ds.prop = best; }
	}
	const int n = bodySlots();
	bool role = false;
	for (int s = 0; s < MAX_DSLOTS; ++s)
	{
		const DesignPart *t = partAt(s_ds.slot[s]);
		if (t == nullptr) { s_ds.slot[s] = -1; continue; }
		if (s >= n || !turretValid(s, *t)) { s_ds.slot[s] = -1; continue; }
		if (isRoleKind(t->kind)) { if (role) { s_ds.slot[s] = -1; continue; } role = true; }
	}
	if (!freeSlots() && s_ds.slot[0] < 0) { for (int s = 1; s < MAX_DSLOTS; ++s) { s_ds.slot[s] = -1; } }
	s_ds.cur = std::clamp(s_ds.cur, 0, n - 1);
}

/// erster Turm, wenn der Entwurf noch keinen hat (neuer Entwurf)
void defaultTurret()
{
	for (int s = 0; s < bodySlots(); ++s) { if (s_ds.slot[s] >= 0) { return; } }
	for (int i = 0; i < static_cast<int>(s_parts.size()); ++i)
	{
		const DesignPart &t = s_parts[static_cast<size_t>(i)];
		if (t.kind == PK_WEAPON && turretValid(0, t)) { s_ds.slot[0] = i; return; }
	}
}

void defaultDesign()
{
	if (s_ds.faction.empty()) { s_ds.faction = "Scavengers"; }   // die eigene Fraktion
	if (s_ds.body >= 0) { return; }
	for (int i = 0; i < static_cast<int>(s_parts.size()); ++i)
	{
		const DesignPart &p = s_parts[static_cast<size_t>(i)];
		if (s_ds.body < 0 && p.kind == PK_BODY && inFaction(p)) { s_ds.body = i; }
		if (s_ds.prop < 0 && p.kind == PK_PROP && p.ptype != "Lift" && inFaction(p)) { s_ds.prop = i; }
	}
	fixTurrets();
	defaultTurret();
}

/// erster belegter Turm-Platz (Hauptturm fuer Namen)
const DesignPart *mainTurret()
{
	for (int s = 0; s < bodySlots(); ++s) { if (const DesignPart *t = partAt(s_ds.slot[s])) { return t; } }
	return nullptr;
}

std::string autoName()
{
	const DesignPart *t = mainTurret(), *b = partAt(s_ds.body), *p = partAt(s_ds.prop);
	std::string n;
	if (t) { n += t->name; }
	if (b) { n += (n.empty() ? "" : " ") + b->name; }
	if (p) { n += (n.empty() ? "" : " ") + p->name; }
	return n;
}

struct DesignNumbers { int power = 0, hp = 0, weight = 0, speed = 0, damage = 0, range = 0; };
DesignNumbers numbers()
{
	DesignNumbers r;
	const DesignPart *b = partAt(s_ds.body), *p = partAt(s_ds.prop);
	if (b == nullptr || p == nullptr) { return r; }
	// wie im Spiel: Laufwerk-Kosten/-Gewicht/-Lebenspunkte in Prozent des Rumpfs
	r.power = b->power + p->power * b->power / 100;
	r.weight = b->weight + p->weight * b->weight / 100;
	r.hp = b->hp + p->hp * b->hp / 100;
	// (27.11.) weitere Antriebsplaetze: ihr Antriebs-Anteil (wie im Spiel); mehr Laufwerke ziehen kraeftiger
	const int nd = bodyDrives();
	for (int k = 1; k < nd; ++k)
	{
		const DesignPart *q = driveProp(k);
		if (q == nullptr) { continue; }
		r.power += q->power * b->power / 100; r.weight += q->weight * b->weight / 100; r.hp += q->hp * b->hp / 100;
	}
	for (int s = 0; s < bodySlots(); ++s)
	{
		const DesignPart *t = partAt(s_ds.slot[s]);
		if (t == nullptr) { continue; }
		r.power += t->power; r.weight += t->weight; r.hp += t->hp;
		if (t->kind == PK_WEAPON) { r.damage += t->damage; r.range = std::max(r.range, t->range); }
	}
	const auto pr = s_powerRatio.find(p->ptype);
	const int ratio = pr != s_powerRatio.end() ? pr->second : 100;
	const int pullWeight = nd > 1 ? static_cast<int>(static_cast<double>(r.weight) / (1.0 + 0.35 * (nd - 1))) : r.weight;
	int spd = ratio * b->engine / std::max(1, pullWeight);
	if (p->ptype == "Lift") { spd = (b->size == "HEAVY") ? spd / 4 : (b->size == "MEDIUM" ? spd * 3 / 4 : spd); }
	if (b->engine > r.weight) { spd = spd * 3 / 2; }
	r.speed = p->speed > 0 ? std::min(p->speed, spd) : spd;
	return r;
}

/// (Wunsch 27.11. 19:24) leuchtende Markierung eines Turm-Platzes im Vorschaufenster
struct SlotMark { glm::mat4 m; int slot; bool empty; };

/// (27.11.) Neigung eines Platzes (Verbundrumpf aus der Hull Forge); aimYaw/aimPitch: Ruhestellung des Turms in den Achsen des Platzes
/// (Bogenmass), damit das Rohr wie im Spiel nach vorn zeigt
glm::mat4 slotTilt(const DesignPart &b, int s, float &aimYaw, float &aimPitch)
{
	aimYaw = 0.f; aimPitch = 0.f;
	if (s < 0 || static_cast<size_t>(s) >= b.tilt.size()) { return glm::mat4(1.f); }
	const glm::vec3 &t = b.tilt[static_cast<size_t>(s)];
	if (std::fabs(t.y) < 1e-3f && std::fabs(t.z) < 1e-3f) { return glm::mat4(1.f); }
	const glm::mat4 T = modcompbody::tiltMatrix(t.x, t.y, t.z);
	const glm::vec3 L = glm::transpose(glm::mat3(T)) * glm::vec3(0.f, 0.f, -1.f);
	aimPitch = std::asin(std::clamp(L.y, -1.f, 1.f));
	aimYaw = std::atan2(L.x, -L.z);
	return T;
}

bool projectUI(const glm::mat4 &m, const glm::vec3 &p, Vector2i &out)
{
	const glm::mat4 mm = pie_UIPerspectiveGet() * m * glm::translate(p);
	const Vector3i zero(0, 0, 0);
	return pie_RotateProjectWithPerspective(&zero, mm, &out) > 0 && out.x != LONG_WAY;
}

WzText &slotNumberText(int n)
{
	static std::map<int, WzText> cache;
	auto it = cache.find(n);
	if (it == cache.end()) { it = cache.emplace(n, WzText(WzString::fromUtf8(std::to_string(n)), font_small)).first; }
	return it->second;
}

/// leere Plaetze: der gelbe WZ-Platzhalter-Turm (wie in der Hull Forge); jeder angezeigte Platz: pulsierender goldener Leuchtring + Nummer,
/// der gewaehlte Platz (Reiter Tuerme) weiss
void drawSlotMarks(const std::vector<SlotMark> &marks, float pxPerModelUnit)
{
	if (marks.empty()) { return; }
	const iIMDShape *nt = shapeOf("znullturret.pie"), *ng = shapeOf("znullgun.pie");
	const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(realTime) * 0.006f);
	for (const SlotMark &mk : marks)
	{
		if (mk.empty && nt != nullptr)
		{
			drawShape(nt, mk.m);
			glm::mat4 gm = mk.m;
			if (!nt->connectors.empty()) { gm *= glm::translate(glm::vec3(nt->connectors[0].xzy())); }
			if (ng != nullptr) { drawShape(ng, gm); }
		}
	}
	// Ringe und Nummern ueber dem Modell (2D, in Bildpunkten)
	const float r3 = 9.f / std::max(pxPerModelUnit, 1e-3f);   // Ring-Halbmesser im Modell: etwa 9 Bildpunkte
	for (const SlotMark &mk : marks)
	{
		const bool sel = s_ds.tab == 2 && mk.slot == s_ds.cur;
		constexpr int N = 28;
		Vector2i pts[N];
		bool ok = true;
		for (int k = 0; k < N && ok; ++k)
		{
			const float a = 6.2831853f * static_cast<float>(k) / static_cast<float>(N);
			ok = projectUI(mk.m, glm::vec3(std::cos(a) * r3, 1.f, std::sin(a) * r3), pts[k]);
		}
		Vector2i c;
		if (!ok || !projectUI(mk.m, glm::vec3(0.f, 1.f, 0.f), c)) { continue; }
		const uint8_t base = static_cast<uint8_t>(sel ? 255 : 150 + static_cast<int>(90.f * pulse));
		const PIELIGHT core = sel ? pal_RGBA(255, 255, 255, base) : pal_RGBA(255, 214, 90, base);
		// weicher Lichthof: dieselbe Linie 1 und 2 Bildpunkte versetzt, schwaecher; darauf die helle Linie
		for (int g = 2; g >= 0; --g)
		{
			const PIELIGHT col = g == 0 ? core : pal_RGBA(core.byte.r, core.byte.g, static_cast<uint8_t>(core.byte.b / 2), static_cast<uint8_t>(base / (2 * g + 1)));
			for (int k = 0; k < N; ++k)
			{
				const Vector2i &p0 = pts[k], &p1 = pts[(k + 1) % N];
				if (g == 0) { iV_Line(p0.x, p0.y, p1.x, p1.y, col); continue; }
				iV_Line(p0.x, p0.y - g, p1.x, p1.y - g, col);
				iV_Line(p0.x, p0.y + g, p1.x, p1.y + g, col);
			}
		}
		WzText &t = slotNumberText(mk.slot + 1);
		const int tx = c.x + 8, ty = c.y - 14;
		pie_UniTransBoxFill(static_cast<float>(tx - 2), static_cast<float>(ty - 1), static_cast<float>(tx + t.width() + 2), static_cast<float>(ty + t.lineSize() + 1), pal_RGBA(0, 0, 0, 120));
		t.render(tx, ty - t.aboveBase(), sel ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(255, 220, 110, 255));
	}
}

/// (27.11.) Laufwerk-Modelle eines Antriebsplatzes: Antrieb prop in der Groesse des Rumpfes sizeBodyId (Lauf-Beine: Stand-Modell)
void driveShapes(const std::string &sizeBodyId, const DesignPart &prop, const iIMDShape *&L, const iIMDShape *&R)
{
	L = nullptr; R = nullptr;
	if (!prop.walker.empty()) { L = shapeOf(prop.walker); return; }
	const DesignPart *sb = nullptr;
	for (const DesignPart &q : s_parts) { if (q.kind == PK_BODY && q.id == sizeBodyId) { sb = &q; break; } }
	if (sb != nullptr && sb->extra.is_object())
	{
		auto pe = sb->extra.find(prop.id);
		if (pe != sb->extra.end() && pe->is_object())
		{
			L = shapeOf(jsonStr(*pe, "left"));
			R = shapeOf(jsonStr(*pe, "right"));
		}
	}
	if (L == nullptr && R == nullptr)
	{
		if (sb != nullptr) { L = fallbackProp(*sb, prop); }
		if (L == nullptr) { auto pm = s_propModel.find(prop.id); if (pm != s_propModel.end()) { L = shapeOf(pm->second); } }
	}
}

/// (27.11.) alle Antriebsplaetze eines Verbundrumpfes in der Vorschau (je Platz sein Antrieb)
void drawDrives(const DesignPart &b, const glm::mat4 &base)
{
	for (size_t k = 0; k < b.drives.size() && k < 8; ++k)
	{
		const DesignPart *q = driveProp(static_cast<int>(k));
		if (q == nullptr) { continue; }
		const iIMDShape *L = nullptr, *R = nullptr;
		driveShapes(k < b.driveBody.size() ? b.driveBody[k] : std::string(), *q, L, R);
		const glm::mat4 m = base * modcompbody::driveMatrix(b.drives[k]);
		if (L != nullptr) { drawShape(L, m); }
		if (R != nullptr && R != L) { drawShape(R, m); }
	}
}

class DesignView : public WIDGET
{
public:
	void display(int xOffset, int yOffset) override
	{
		const int x0 = xOffset + x(), y0 = yOffset + y(), x1 = x0 + width(), y1 = y0 + height();
		gradientBox(x0, y0, x1, y1, pal_RGBA(12, 22, 44, 240), pal_RGBA(4, 8, 18, 245), 10);
		glowFrame(x0, y0, x1, y1, pal_RGBA(90, 190, 255, 255), 3, 0.5f);
		const DesignPart *b = partAt(s_ds.body), *p = partAt(s_ds.prop);
		const iIMDShape *bs = b ? shapeOf(b->model) : nullptr;
		if (bs == nullptr) { return; }
		static int s_rot = 45;
		s_rot = (s_rot + realTimeAdjustedAverage(BUTTONOBJ_ROTSPEED)) % 360;
		// Laufwerk: Modell des Rumpfs fuer diesen Antrieb (links/rechts), bzw. Lauf-Beine
		const iIMDShape *propL = nullptr, *propR = nullptr;
		if (p != nullptr && b->extra.is_object())
		{
			auto pe = b->extra.find(p->id);
			if (pe != b->extra.end() && pe->is_object())
			{
				propL = shapeOf(jsonStr(*pe, "left"));
				propR = shapeOf(jsonStr(*pe, "right"));
			}
		}
		const iIMDShape *legs = (p != nullptr && !p->walker.empty()) ? shapeOf(p->walker) : nullptr;
		if (p != nullptr && propL == nullptr && propR == nullptr && legs == nullptr) { propL = fallbackProp(*b, *p); }
		const bool withDrives = !b->drives.empty();   // (27.11.) Antriebsplaetze statt des einen Laufwerks
		if (withDrives) { propL = nullptr; propR = nullptr; legs = nullptr; }
		glm::vec3 hip(0.f);
		if (legs != nullptr && !legs->connectors.empty()) { hip = glm::vec3(legs->connectors[0].xzy()); }
		const float lift = legs != nullptr ? 0.f : b->lift;   // Verbundrumpf mit Teilen unter dem Fahrgestell steht hoeher (wie im Spiel)
		const glm::vec3 bodyOff = legs != nullptr ? hip : glm::vec3(0.f, lift, 0.f);

		// (Wunsch 18.11. 12:03 "Das Modell wird nicht richtig dargestellt": winzig und flach) Groesse aus der tatsaechlichen Projektion:
		// das ganze Fahrzeug (Rumpf, Antrieb, Turm) fuellt gut die Haelfte des Fensters - die Mod-Modelle sind halb so gross wie die
		// Originale, daher passte die feste Groesse des Spiel-Designers nicht. (24.11. 02:18: Kamera wieder wie vorher - mit den Tuermen)
		float ext = static_cast<float>(std::max({std::abs(bs->min.x), std::abs(bs->max.x), std::abs(bs->min.z), std::abs(bs->max.z), 1}));
		float top = static_cast<float>(std::max(bs->max.y, 1)) + bodyOff.y;
		float bottom = std::min(static_cast<float>(bs->min.y) + bodyOff.y, 0.f);
		for (int s = 0; s < bodySlots(); ++s)
		{
			const DesignPart *t = partAt(s_ds.slot[s]);
			const iIMDShape *gun = t ? shapeOf(t->model) : nullptr;
			if (gun == nullptr || bs->connectors.empty()) { continue; }
			const size_t ci = bs->connectors.size() > static_cast<size_t>(s) ? static_cast<size_t>(s) : 0;
			top = std::max(top, static_cast<float>(bs->connectors[ci].z + gun->max.y) + bodyOff.y);
			ext = std::max(ext, static_cast<float>(std::max(std::abs(gun->min.z), std::abs(gun->max.z)) + std::abs(bs->connectors[ci].y)));
		}
		for (const iIMDShape *ps : {propL, legs})
		{
			if (ps == nullptr) { continue; }
			ext = std::max(ext, static_cast<float>(std::max({std::abs(ps->min.x), std::abs(ps->max.x), std::abs(ps->min.z), std::abs(ps->max.z)})));
			bottom = std::min(bottom, static_cast<float>(ps->min.y));
		}
		const float radius = std::max({ext, (top - bottom) * 0.75f, 8.f});
		const float midY = (top + bottom) * 0.5f;
		pie_SetGeometricOffset((x0 + x1) / 2, (y0 + y1) / 2);
		// Bildpunkte je Modelleinheit in der Tiefe BUTTON_DEPTH (aus der UI-Projektion gemessen)
		const glm::mat4 &proj = pie_UIPerspectiveGet();
		auto ndcX = [&](float wx) { const glm::vec4 c = proj * glm::vec4(wx, 0.f, static_cast<float>(BUTTON_DEPTH), 1.f); return c.x / c.w; };
		const float pxPerUnit = std::max(1e-4f, std::abs(ndcX(100.f) - ndcX(0.f)) * 0.5f * static_cast<float>(pie_GetVideoBufferWidth()) / 100.f);
		const float want = 0.40f * static_cast<float>(std::min(width(), height()));
		const float scale = want / (pxPerUnit * radius);
		const glm::mat4 base = glm::translate(glm::vec3(0.f, 0.f, static_cast<float>(BUTTON_DEPTH)))
		                     * glm::rotate(glm::radians(-30.f), glm::vec3(1.f, 0.f, 0.f))
		                     * glm::rotate(glm::radians(static_cast<float>(s_rot)), glm::vec3(0.f, 1.f, 0.f))
		                     * glm::scale(glm::vec3(scale))
		                     * glm::translate(glm::vec3(0.f, -midY, 0.f));
		const glm::mat4 propBase = legs != nullptr ? base : base * b->propM;   // Verbundrumpf: Laufwerk unter dem Fahrgestell
		const glm::mat4 bodyBase = base * glm::translate(bodyOff);
		// Laufwerk links/rechts (die vom Betrachter abgewandte Seite zuerst)
		const bool leftFirst = s_rot > 180;
		const iIMDShape *firstProp = leftFirst ? propL : propR, *secondProp = leftFirst ? propR : propL;
		if (legs) { drawShape(legs, base); }
		if (firstProp) { drawShape(firstProp, propBase); }
		drawShape(bs, bodyBase);
		std::vector<SlotMark> marks;
		for (int s = 0; s < bodySlots(); ++s)
		{
			const DesignPart *t = partAt(s_ds.slot[s]);
			glm::mat4 m = bodyBase;
			glm::mat4 markAt = bodyBase * glm::translate(glm::vec3(0.f, static_cast<float>(bs->max.y), 0.f));
			if (!bs->connectors.empty())
			{
				const size_t ci = bs->connectors.size() > static_cast<size_t>(s) ? static_cast<size_t>(s) : 0;
				m *= glm::translate(glm::vec3(bs->connectors[ci].xzy()));
				markAt = m;
			}
			// (27.11.) geneigter Platz aus der Hull Forge: Turm in den Achsen des Platzes, Rohr in Ruhe weiter nach vorn
			float aimYaw = 0.f, aimPitch = 0.f;
			const glm::mat4 tilt = slotTilt(*b, s, aimYaw, aimPitch);
			m *= tilt;
			markAt *= tilt;
			if (s < MAX_DSLOTS && s_slotShow[s]) { marks.push_back({markAt, s, t == nullptr}); }
			if (t == nullptr) { continue; }
			m *= glm::rotate(-aimYaw, glm::vec3(0.f, 1.f, 0.f));
			const iIMDShape *mount = shapeOf(t->mount);
			if (mount != nullptr)
			{
				drawShape(mount, m);
				if (!mount->connectors.empty()) { m *= glm::translate(glm::vec3(mount->connectors[0].xzy())); }
			}
			m *= glm::rotate(aimPitch, glm::vec3(1.f, 0.f, 0.f));
			const iIMDShape *gun = shapeOf(t->model);
			if (gun != nullptr && gun != mount) { drawShape(gun, m); }
		}
		if (secondProp) { drawShape(secondProp, propBase); }
		if (withDrives) { drawDrives(*b, base); }
		drawSlotMarks(marks, pxPerUnit * scale);
	}
};

/// Knopf der Platz-Leiste: Nummer im WZ-Stil, darunter ein Strich in der Farbe des Inhalts (Waffe orange, System gruen, leer ohne)
void displaySlotButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	W_BUTTON *b = static_cast<W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const int slot = static_cast<int>(psWidget->UserData);
	const bool sel = slot == s_ds.cur;
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	const bool shown = slot >= 0 && slot < MAX_DSLOTS && s_slotShow[slot];
	// (Wunsch 27.11. 19:24) golden = dieser Platz leuchtet im Vorschaufenster
	if (shown)
	{
		for (int g = 3; g >= 1; --g)
		{
			pie_UniTransBoxFill(static_cast<float>(x0 - g), static_cast<float>(y0 - g), static_cast<float>(x0 + w + g), static_cast<float>(y0 + h + g), pal_RGBA(255, 200, 70, static_cast<uint8_t>(36 * (4 - g))));
		}
	}
	modwzhd::textButton(x0, y0, w, h, std::string(), sel, b->isMouseOverWidget(), down);
	if (shown) { iV_Box(x0, y0, x0 + w - 1, y0 + h - 1, pal_RGBA(255, 210, 90, 255)); }
	{
		static std::map<int, WzText> nums;
		auto it = nums.find(slot + 1);
		if (it == nums.end()) { it = nums.emplace(slot + 1, WzText(WzString::fromUtf8(std::to_string(slot + 1)), font_regular)).first; }
		WzText &tx = it->second;
		const PIELIGHT col = shown ? pal_RGBA(255, 222, 120, 255) : (sel ? pal_RGBA(235, 240, 255, 255) : pal_RGBA(150, 156, 196, 255));
		tx.render(x0 + (w - tx.width()) / 2, y0 + (h - tx.lineSize()) / 2 - tx.aboveBase(), col);
	}
	const DesignPart *t = (slot >= 0 && slot < MAX_DSLOTS) ? partAt(s_ds.slot[slot]) : nullptr;
	if (t != nullptr)
	{
		const PIELIGHT col = t->kind == PK_WEAPON ? pal_RGBA(255, 170, 60, 255) : pal_RGBA(110, 230, 140, 255);
		pie_UniTransBoxFill(x0 + 3, y0 + h - 5, x0 + w - 3, y0 + h - 2, col);
	}
}
/// (27.11.) Knopf eines Antriebsplatzes: Nummer, darunter gruen, wenn er einen eigenen Antrieb hat
void displayDriveButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	W_BUTTON *b = static_cast<W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const int k = static_cast<int>(psWidget->UserData);
	modwzhd::textButton(x0, y0, w, h, std::to_string(k + 1), k == s_ds.curDrive, b->isMouseOverWidget(), (b->getState() & WBUT_DOWN) != 0);
	if (k > 0 && k < 8 && s_ds.driveProp[k] >= 0) { pie_UniTransBoxFill(x0 + 3, y0 + h - 5, x0 + w - 3, y0 + h - 2, pal_RGBA(110, 230, 140, 255)); }
}

/// (Wunsch 27.11. 19:24) Auge-Knopf im Vorschaufenster: alle Plaetze anzeigen/ausblenden (golden = alle an, blasser = einige, durchgestrichen = keiner)
void displaySlotEye(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	W_BUTTON *b = static_cast<W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	int on = 0;
	const int n = std::max(1, std::min(bodySlots(), MAX_DSLOTS));
	for (int k = 0; k < n; ++k) { on += s_slotShow[k] ? 1 : 0; }
	modwzhd::textButton(x0, y0, w, h, std::string(), false, b->isMouseOverWidget(), (b->getState() & WBUT_DOWN) != 0);
	const PIELIGHT col = on == n ? pal_RGBA(255, 214, 90, 255) : (on > 0 ? pal_RGBA(220, 190, 120, 255) : pal_RGBA(140, 150, 190, 255));
	// Auge: Lid oben und unten (Boegen), Pupille
	const float cx = static_cast<float>(x0) + static_cast<float>(w) * 0.5f, cy = static_cast<float>(y0) + static_cast<float>(h) * 0.5f;
	const float rx = static_cast<float>(w) * 0.36f, ry = static_cast<float>(h) * 0.26f;
	constexpr int N = 16;
	for (int k = 0; k < N; ++k)
	{
		const float a0 = 3.14159265f * static_cast<float>(k) / N, a1 = 3.14159265f * static_cast<float>(k + 1) / N;
		const int xa = static_cast<int>(cx - std::cos(a0) * rx), xb = static_cast<int>(cx - std::cos(a1) * rx);
		iV_Line(xa, static_cast<int>(cy - std::sin(a0) * ry), xb, static_cast<int>(cy - std::sin(a1) * ry), col);
		iV_Line(xa, static_cast<int>(cy + std::sin(a0) * ry), xb, static_cast<int>(cy + std::sin(a1) * ry), col);
	}
	const float pr = std::max(2.f, ry * 0.7f);
	if (on > 0) { pie_UniTransBoxFill(cx - pr, cy - pr, cx + pr, cy + pr, col); }
	else { iV_Line(x0 + 4, y0 + h - 4, x0 + w - 4, y0 + 4, col); }
}
} // namespace

WzUnitDesignTitleUI::WzUnitDesignTitleUI(int fleetIndex) : fleet(fleetIndex)
{
	load();
	loadParts();
	defaultDesign();
}

void WzUnitDesignTitleUI::start()
{
	refreshCompositeParts();   // (17:55) Ueber-Rumpfe aus dem Baukasten
	build();
}

// ---------------------------------------------------------------- MOD (Wunsch 23.11. 17:55): Rumpf-Baukasten im Hauptmenue
// "Der Hull-Forge-Button im Hauptmenue - Fleet Designer startet zuerst ein Spiel ... Das soll ein reines Menue-UI-Feature sein": der
// Baukasten liegt als Vollbild-Ebene ueber dem leeren Menue-Bildschirm; Schliessen fuehrt dorthin zurueck, woher er geoeffnet wurde.
WzHullForgeTitleUI::WzHullForgeTitleUI(std::shared_ptr<WzTitleUI> nextUI) : next(std::move(nextUI))
{
}

void WzHullForgeTitleUI::start()
{
	widgDelete(psWScreen, FRONTEND_BACKDROP);
	modhullforge::open(true);
}

TITLECODE WzHullForgeTitleUI::run()
{
	if (!modhullforge::isOpen())
	{
		std::shared_ptr<WzTitleUI> n = next;
		if (n == nullptr) { n = std::make_shared<WzFleetTitleUI>(); }
		changeTitleUI(n);
		return TITLECODE_CONTINUE;
	}
	modhullforge::tick();
	widgRunScreen(psWScreen);
	return TITLECODE_CONTINUE;
}

void WzHullForgeTitleUI::render()
{
	widgDisplayScreen(psWScreen);
}

void WzUnitDesignTitleUI::build()
{
	widgDelete(psWScreen, FRONTEND_BACKDROP);
	W_FORM *backdrop = addBackdrop();
	if (backdrop == nullptr) { return; }
	auto dim = std::make_shared<FleetDim>();
	backdrop->attach(dim);
	dim->id = ID_DIM;
	dim->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
		psWidget->setGeometry(-(static_cast<int>(pie_GetVideoBufferWidth()) - HIDDEN_FRONTEND_WIDTH) / 2 - 2, -(static_cast<int>(pie_GetVideoBufferHeight()) - HIDDEN_FRONTEND_HEIGHT) / 2 - 2,
		                      static_cast<int>(pie_GetVideoBufferWidth()) + 4, static_cast<int>(pie_GetVideoBufferHeight()) + 4);
	}));
	dim->setTransparentToClicks(true);
	auto panel = std::make_shared<FleetPanel>();
	backdrop->attach(panel);
	panel->id = FRONTEND_BOTFORM;
	panel->setGeometry(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
	WIDGET *form = panel.get();
	s_iconSpots.clear();
	s_btnIcon.clear();
	s_btnWz.clear();
	fixTurrets();
	if (!s_ds.edited) { s_ds.name = autoName(); }

	// ---- Kopf
	s_iconSpots.push_back({IC_DESIGNER, 14, 10, 40});
	headLabel(form, ID_D_HEAD, 60, 8, 360, _("UNIT DESIGNER"), pal_RGBA(200, 240, 255, 255), font_large, 30);
	headLabel(form, ID_D_HEAD + 1, 60, 34, 460, _("All parts unlocked - saved designs go into your fleet and every game mode"), pal_RGBA(130, 180, 220, 255), font_regular, 18);
	wzNamedButton(form, ID_D_FORGE, PANEL_W - 78, 4, 58, 31, "wz_forge", _("Hull Forge: build super hulls"), IC_DESIGNER);   // (17:55) mit Sinnbild
	headLabel(form, ID_D_HEAD + 9, PANEL_W - 88, 36, 78, _("Hull Forge"), pal_RGBA(190, 210, 240, 255), font_small, 16, true);
	if (s_parts.empty())
	{
		headLabel(form, ID_D_HEAD + 2, 16, 90, PANEL_W - 32, _("No parts found (stats files missing)."), WZCOL_TEXT_BRIGHT, font_regular);
	}

	// ---- (Wunsch 24.11. 02:13 "sauber zwischen Scavenger und anderen Fraktionen trennen") Fraktions-Reiter ueber allem, wie im Rumpf-Baukasten
	{
		const std::vector<std::string> fl = factionList();
		if (!fl.empty())
		{
			const int n = static_cast<int>(fl.size()), gap = 3;
			const int w = std::min(96, (PANEL_W - 32 - (n - 1) * gap) / n);
			for (int k = 0; k < n; ++k)
			{
				const std::string &f = fl[static_cast<size_t>(k)];
				const std::string tip = f == "Scavengers" ? std::string(_("Scavengers: scav hulls, scav propulsion and scav turrets"))
				                        : f == "Custom" ? std::string(_("Custom: your own super hulls from the Hull Forge - every part, also this mod's own parts"))
				                                        : astringf(_("%s hulls (with the original WZ parts)"), f.c_str());
				auto b = wzTextButton(form, ID_D_FACTION + static_cast<unsigned>(k), 16 + k * (w + gap), 56, w, 22, factionLabel(f), tip.c_str());
				if (f == s_ds.faction) { b->setState(WBUT_CLICKLOCK); }
			}
		}
	}

	// ---- Reiter: Rumpf, Laufwerk, Tuerme - (18.11. 12:03) Original-WZ-Bilder des Design-Bildschirms (verschoenert), darunter die Unterreiter.
	// (24.11. 01:23) Die Tuerme haben EINEN Reiter; die Plaetze waehlt die Platz-Leiste darunter (bis 30 Plaetze - vorher lagen "Turret 2/3"
	// als eigene Reiter hinter dem Vorschaufenster und waren nicht anklickbar)
	const int slots = bodySlots();
	struct Tab { int img, hi; std::string cap; };
	const std::vector<Tab> tabs = {{IMAGE_DES_BODY, IMAGE_DES_BODYH, _("Body")}, {IMAGE_DES_PROPULSION, IMAGE_DES_PROPULSIONH, _("Propulsion")},
	                               {IMAGE_DES_TURRET, IMAGE_DES_TURRETH, slots > 1 ? std::string(_("Turrets")) : std::string(_("Turret"))}};
	s_ds.tab = std::clamp(s_ds.tab, 0, 2);
	const int tabW = 45 * 13 / 10, tabH = 24 * 13 / 10, tabGap = 4;
	for (int k = 0; k < 3; ++k)
	{
		const int cx = 18 + k * (tabW + tabGap);
		const Tab &t = tabs[static_cast<size_t>(k)];
		auto b = wzImgButton(form, ID_D_TAB + static_cast<unsigned>(k), cx, D_TOP, tabW, tabH, t.img, t.hi, t.cap.c_str(), IC_TANK);
		if (k == s_ds.tab) { b->setState(WBUT_CLICKLOCK); }
		headLabel(form, ID_D_HEAD + 10 + static_cast<unsigned>(k), cx - 4, D_TOP + tabH + 1, tabW + 8, t.cap, k == s_ds.tab ? pal_RGBA(255, 214, 110, 255) : pal_RGBA(190, 210, 240, 255), font_small, 14, true);
	}
	// Unterreiter: Rumpf leicht/mittel/schwer/ueberschwer, Antrieb Land/Wasser/Luft, Turm Waffen/Systeme (WZ-Bilder)
	const int nsub = subCount(s_ds.tab);
	s_ds.sub[s_ds.tab] = std::clamp(s_ds.sub[s_ds.tab], 0, nsub - 1);
	const int sy = D_TOP + tabH + 18;
	if (s_ds.tab == 2)
	{
		const int w2 = 37 * 13 / 10, h2 = 24 * 13 / 10;
		const int imgs[2][2] = {{IMAGE_DES_WEAPONS, IMAGE_DES_WEAPONSDOWN}, {IMAGE_DES_SYSTEMS, IMAGE_DES_SYSTEMSDOWN}};
		const char *tips[2] = {_("Weapons"), _("Systems (sensor, shield, construction, repair, ECM)")};
		for (int k = 0; k < nsub; ++k)
		{
			auto b = wzImgButton(form, ID_D_SUB + static_cast<unsigned>(k), 18 + k * (w2 + 6), sy, w2, h2, imgs[k][0], imgs[k][1], tips[k], IC_ARTY);
			if (k == s_ds.sub[2]) { b->setState(WBUT_CLICKLOCK); }
		}
		const std::string cap = slots > 1 ? astringf(_("Slot %d / %d"), s_ds.cur + 1, slots) : std::string(s_ds.sub[2] == 0 ? _("Weapons") : _("Systems"));
		headLabel(form, ID_D_HEAD + 20, 18 + 2 * (w2 + 6) + 4, sy + 6, 112, cap, pal_RGBA(255, 214, 110, 255), font_regular, 20);
	}
	else if (nsub > 1)
	{
		const char *bodyCats[4] = {N_("Light"), N_("Medium"), N_("Heavy"), N_("Super")};
		const char *propCats[3] = {N_("Land"), N_("Water"), N_("Air")};
		const char *bodyTips[4] = {N_("Light bodies"), N_("Medium bodies"), N_("Heavy bodies"), N_("Super heavy bodies")};
		const char *propTips[3] = {N_("Land: wheels, half-tracks, tracks, legs, hover"), N_("Water: ships and hover"), N_("Air: VTOL")};
		const int totalW = 214, gap = 4;
		const int w = (totalW - (nsub - 1) * gap) / nsub;
		for (int k = 0; k < nsub; ++k)
		{
			const char *cap = s_ds.tab == 0 ? bodyCats[k] : propCats[k];
			const char *tip = s_ds.tab == 0 ? bodyTips[k] : propTips[k];
			auto b = wzTextButton(form, ID_D_SUB + static_cast<unsigned>(k), 18 + k * (w + gap), sy, w, 24, gettext(cap), gettext(tip));
			if (k == s_ds.sub[s_ds.tab]) { b->setState(WBUT_CLICKLOCK); }
		}
	}
	int listY = sy + 34;

	// ---- (Wunsch 27.11. 19:16) Antriebsplatz-Leiste (Reiter Antrieb, Verbundrumpf mit Antriebsplaetzen): je Platz ein eigener Antrieb
	s_ds.curDrive = std::clamp(s_ds.curDrive, 0, std::max(0, bodyDrives() - 1));
	if (s_ds.tab == 1 && bodyDrives() > 0)
	{
		const int bw = 22, bh = 22;
		int x = 18;
		for (int k = 0; k < bodyDrives(); ++k)
		{
			W_BUTINIT init;
			init.formID = FRONTEND_BOTFORM;
			init.id = ID_D_DRIVE + static_cast<unsigned>(k);
			init.x = static_cast<short>(x);
			init.y = static_cast<short>(listY);
			init.width = static_cast<unsigned short>(bw);
			init.height = static_cast<unsigned short>(bh);
			init.pDisplay = displayDriveButton;
			init.UserData = static_cast<UDWORD>(k);
			auto btn = std::make_shared<W_BUTTON>(&init);
			form->attach(btn);
			const DesignPart *q = driveProp(k);
			btn->setTip(astringf(_("Drive %d: %s"), k + 1, q != nullptr ? q->name.c_str() : "-")
			            + (k == 0 ? std::string("\n") + _("Drive 1 is the main propulsion (it decides how the unit moves)") : std::string()));
			x += bw + 2;
		}
		headLabel(form, ID_D_HEAD + 21, x + 6, listY + 1, 140, astringf(_("Drive %d / %d"), s_ds.curDrive + 1, bodyDrives()), pal_RGBA(140, 240, 180, 255), font_regular, 20);
		listY += bh + 6;
	}

	// ---- Platz-Leiste (Reiter Tuerme, Rumpf mit mehreren Plaetzen): Nummern 1..n, Pfeile bei vielen Plaetzen, leeren, auf alle verteilen
	if (s_ds.tab == 2 && slots > 1)
	{
		const int bw = 22, bh = 22, step = bw + 2;
		const bool paged = slots > 7;
		const int per = paged ? 5 : slots;
		const int pages = (slots + per - 1) / per;
		if (s_ds.cur / per != s_ds.slotPage) { s_ds.slotPage = s_ds.cur / per; }
		s_ds.slotPage = std::clamp(s_ds.slotPage, 0, pages - 1);
		int x = 18;
		if (paged) { wzNamedButton(form, ID_D_SPREV, x, listY, 18, bh, "wz_left", _("Previous slots"), IC_LEFT); x += 22; }
		for (int k = s_ds.slotPage * per; k < slots && k < (s_ds.slotPage + 1) * per; ++k)
		{
			W_BUTINIT init;
			init.formID = FRONTEND_BOTFORM;
			init.id = ID_D_SLOT + static_cast<unsigned>(k);
			init.x = static_cast<short>(x);
			init.y = static_cast<short>(listY);
			init.width = static_cast<unsigned short>(bw);
			init.height = static_cast<unsigned short>(bh);
			init.pDisplay = displaySlotButton;
			init.UserData = static_cast<UDWORD>(k);
			init.style |= WBUT_SECONDARY;   // (27.11.) Rechtsklick: nur die Anzeige des Platzes an/aus
			auto btn = std::make_shared<W_BUTTON>(&init);
			form->attach(btn);
			const DesignPart *t = partAt(s_ds.slot[k]);
			btn->setTip((t ? astringf(_("Slot %d: %s"), k + 1, t->name.c_str()) : astringf(_("Slot %d: empty"), k + 1))
			            + "\n" + _("Click: choose (click again: show/hide its marker) - right click: show/hide its marker"));
			x += step;
		}
		if (paged) { wzNamedButton(form, ID_D_SNEXT, x, listY, 18, bh, "wz_right", _("Next slots"), IC_RIGHT); x += 22; }
		x = std::max(x + 2, 18 + 7 * step + 2);
		wzTextButton(form, ID_D_SCLEAR, x, listY, 22, bh, "X", _("Empty this slot"));
		wzTextButton(form, ID_D_SFILL, x + 24, listY, 26, bh, _("All"), _("Put this turret on every empty slot"));
		listY += bh + 6;
	}

	// ---- links: Liste der Teile des Unterreiters
	const std::vector<int> cand = candidates(s_ds.tab);
	const bool allowNone = (s_ds.tab == 2 && (freeSlots() || s_ds.cur > 0)) || (s_ds.tab == 1 && bodyDrives() > 0 && s_ds.curDrive > 0);   // Plaetze duerfen leer bleiben (ohne freie Belegung nicht Platz 1); (27.11.) Antriebsplatz 2..n: wie Antrieb 1
	std::vector<int> rows;
	if (allowNone) { rows.push_back(-1); }
	rows.insert(rows.end(), cand.begin(), cand.end());
	const int pagerY = PANEL_H - 66;
	s_rows = std::clamp((pagerY - 2 - listY) / LINE_H, 4, D_ROWS);
	const int pages = std::max(1, (static_cast<int>(rows.size()) + s_rows - 1) / s_rows);
	s_ds.page = std::clamp(s_ds.page, 0, pages - 1);
	const int current = s_ds.tab == 0 ? s_ds.body : (s_ds.tab == 1 ? ((bodyDrives() > 0 && s_ds.curDrive > 0) ? s_ds.driveProp[std::clamp(s_ds.curDrive, 0, 7)] : s_ds.prop) : s_ds.slot[std::clamp(s_ds.cur, 0, MAX_DSLOTS - 1)]);
	s_selectedButton = 0;
	int y = listY;
	for (int k = s_ds.page * s_rows; k < static_cast<int>(rows.size()) && k < (s_ds.page + 1) * s_rows; ++k)
	{
		const int idx = rows[static_cast<size_t>(k)];
		const DesignPart *p = partAt(idx);
		std::string t = p ? p->name : std::string(s_ds.tab == 1 ? _("(like drive 1)") : _("(none)"));
		if (p && p->power > 0) { t += astringf("  (%d)", p->power); }
		if (p && p->kind == PK_BODY) { const std::string tag = bodyTag(*p); if (!tag.empty()) { t += "  - " + tag; } }   // (23.11. 23:24) Herkunft
		const unsigned id = ID_D_ITEM + static_cast<unsigned>(k - s_ds.page * s_rows);
		lineButton(form, id, 24, y, 210, t);
		if (idx == current) { s_selectedButton = id; }
		y += LINE_H;
	}
	if (rows.empty()) { headLabel(form, ID_D_HEAD + 3, 24, y, 210, _("Nothing fits this body/propulsion"), WZCOL_TEXT_MEDIUM, font_regular); }
	if (pages > 1)
	{
		wzNamedButton(form, ID_D_PREV, 24, pagerY, 36, 24, "wz_left", _("Previous page"), IC_LEFT);   // MOD (21.11.): Tooltip
		headLabel(form, ID_D_HEAD + 4, 70, pagerY + 2, 60, astringf("%d / %d", s_ds.page + 1, pages), WZCOL_TEXT_MEDIUM, font_regular, 22, true);
		wzNamedButton(form, ID_D_NEXT, 140, pagerY, 36, 24, "wz_right", _("Next page"), IC_RIGHT);
	}

	// ---- rechts: 3D-Vorschau
	auto view = std::make_shared<DesignView>();
	form->attach(view);
	view->id = ID_D_VIEW;
	view->setGeometry(250, D_TOP, PANEL_W - 266, 298 - D_TOP);
	view->setTransparentToClicks(true);
	{
		// (Wunsch 27.11. 19:24) Auge oben links im Vorschaufenster: alle Waffenplaetze anzeigen/ausblenden
		W_BUTINIT init;
		init.formID = FRONTEND_BOTFORM;
		init.id = ID_D_SLOTEYE;
		init.x = 256;
		init.y = static_cast<short>(D_TOP + 6);
		init.width = 30;
		init.height = 20;
		init.pDisplay = displaySlotEye;
		auto eye = std::make_shared<W_BUTTON>(&init);
		form->attach(eye);
		eye->setTip(_("Show/hide all weapon slot markers"));
	}

	// ---- Werte
	const DesignNumbers n = numbers();
	struct Val { const char *label; int v; };
	const Val vals[6] = {{_("Power cost"), n.power}, {_("Hitpoints"), n.hp}, {_("Weight"), n.weight}, {_("Speed"), n.speed}, {_("Damage"), n.damage}, {_("Range"), n.range}};
	for (int k = 0; k < 6; ++k)
	{
		const int cx = 254 + (k % 2) * 176, cy = 306 + (k / 2) * 22;
		headLabel(form, ID_D_HEAD + 30 + static_cast<unsigned>(k), cx, cy, 100, vals[k].label, pal_RGBA(130, 180, 220, 255), font_regular, 20);
		std::string v = (k == 5) ? astringf("%.1f", static_cast<double>(vals[k].v) / 128.0) : astringf("%d", vals[k].v);
		headLabel(form, ID_D_HEAD + 40 + static_cast<unsigned>(k), cx + 100, cy, 70, v, WZCOL_TEXT_BRIGHT, font_regular_bold, 20);
	}

	// ---- Name + Speichern
	auto edit = std::make_shared<W_EDITBOX>();
	form->attach(edit);
	edit->id = ID_D_NAME;
	edit->FontID = font_regular;
	edit->setGeometry(254, 384, 260, 26);
	edit->setBoxColours(pal_RGBA(80, 100, 150, 255), pal_RGBA(80, 100, 150, 255), pal_RGBA(10, 12, 20, 255));
	edit->setMaxStringSize(60);
	edit->setPlaceholder(_("Design name"));
	edit->setString(WzString::fromUtf8(s_ds.name));
	wzImgButton(form, ID_D_SAVE, 524, 380, 45 * 13 / 10, 24 * 13 / 10, IMAGE_DES_SAVE, IMAGE_DES_SAVEH, _("Save this design into the fleet and into every game mode"), IC_STAR);
	headLabel(form, ID_D_HEAD + 5, 514, 414, 82, _("Save"), pal_RGBA(255, 214, 110, 255), font_small, 16, true);

	// ---- Fusszeile
	addMultiBut(*form, ID_D_BACK, 16, PANEL_H - 34, 30, 29, P_("menu", "Return"), IMAGE_RETURN, IMAGE_RETURN_HI, IMAGE_RETURN_HI);
	std::string fname = fleets().empty() ? std::string(_("(new fleet)")) : fleets()[std::clamp(fleet, 0, static_cast<int>(fleets().size()) - 1)].value("name", std::string());
	headLabel(form, ID_D_HEAD + 6, 56, PANEL_H - 31, 190, astringf(_("Fleet: %s"), fname.c_str()), pal_RGBA(190, 210, 240, 255), font_regular, 22);
	auto info = std::make_shared<W_LABEL>();
	form->attach(info);
	info->id = ID_D_INFO;
	info->setGeometry(250, PANEL_H - 31, PANEL_W - 266, 22);
	info->setFont(font_regular, WZCOL_TEXT_BRIGHT);
	info->setString(WzString::fromUtf8(s_ds.info));
}

namespace
{
/// Entwurf als Vorlage (wie das Spiel sie speichert): Waffen in Platz-Reihenfolge, Systemtuerme; sind Plaetze frei belegt oder Systeme
/// neben Waffen, steht die Belegung in "modSlots" (modslots im Spiel)
bool designJson(nlohmann::json &t, std::string &err)
{
	const DesignPart *b = partAt(s_ds.body), *p = partAt(s_ds.prop);
	if (b == nullptr || p == nullptr || mainTurret() == nullptr) { err = _("A design needs a body, a propulsion and a turret"); return false; }
	t = nlohmann::json::object();
	t["body"] = b->id; t["propulsion"] = p->id;
	nlohmann::json weapons = nlohmann::json::array(), weapConn = nlohmann::json::array(), systems = nlohmann::json::array();
	const DesignPart *sensor = nullptr, *ecm = nullptr, *cons = nullptr, *rep = nullptr;
	bool identity = true;
	if (const DesignPart *br = partAt(s_ds.slot[0]); br != nullptr && br->kind == PK_BRAIN)
	{
		// Kommandeur: Kommandoturm + seine Waffe (wie das Spiel ihn speichert)
		t["type"] = "DROID_COMMAND";
		t["brain"] = br->id;
		t["weapons"] = nlohmann::json::array({br->brainWeapon.empty() ? std::string("CommandTurret1") : br->brainWeapon});
		return true;
	}
	for (int s = 0; s < bodySlots(); ++s)
	{
		const DesignPart *q = partAt(s_ds.slot[s]);
		if (q == nullptr) { continue; }
		if (q->kind == PK_WEAPON)
		{
			if (static_cast<int>(weapons.size()) != s) { identity = false; }
			weapons.push_back(q->id);
			weapConn.push_back(s);
			continue;
		}
		const char *type = q->kind == PK_SENSOR ? "sensor" : q->kind == PK_ECM ? "ecm" : q->kind == PK_CONSTRUCT ? "construct" : "repair";
		systems.push_back(nlohmann::json{{"type", type}, {"id", q->id}, {"slot", s}});
		if (q->kind == PK_SENSOR && sensor == nullptr) { sensor = q; }
		if (q->kind == PK_ECM && ecm == nullptr) { ecm = q; }
		if (q->kind == PK_CONSTRUCT) { cons = q; }
		if (q->kind == PK_REPAIR) { rep = q; }
	}
	t["type"] = cons ? "CONSTRUCT" : rep ? "REPAIR" : !weapons.empty() ? "WEAPON" : sensor ? "SENSOR" : "ECM";
	if (!weapons.empty()) { t["weapons"] = weapons; }
	if (sensor) { t["sensor"] = sensor->id; }
	if (ecm) { t["ecm"] = ecm->id; }
	if (cons) { t["construct"] = cons->id; }
	if (rep) { t["repair"] = rep->id; }
	// bisherige Form: nur Waffen ab Platz 1 lueckenlos, oder ein einziger Systemturm auf Platz 1
	const bool classic = (systems.empty() && identity) || (weapons.empty() && systems.size() == 1 && systems[0]["slot"].get<int>() == 0);
	nlohmann::json ms = nlohmann::json::object();
	if (!classic)
	{
		ms["weapConn"] = weapConn;
		if (!systems.empty()) { ms["systems"] = systems; }
	}
	// (27.11.) Antrieb je Antriebsplatz (Platz 1 = "propulsion"; leer = wie Antrieb 1)
	const int nd = bodyDrives();
	bool ownDrive = false;
	for (int k = 1; k < nd; ++k) { ownDrive = ownDrive || s_ds.driveProp[k] >= 0; }
	if (nd > 0 && ownDrive)
	{
		nlohmann::json drv = nlohmann::json::array();
		for (int k = 0; k < nd; ++k)
		{
			const DesignPart *q = (k > 0 && s_ds.driveProp[k] >= 0) ? partAt(s_ds.driveProp[k]) : nullptr;
			drv.push_back(q != nullptr ? q->id : std::string());
		}
		ms["drives"] = drv;
	}
	if (!ms.empty()) { t["modSlots"] = ms; }
	return true;
}
}

TITLECODE WzUnitDesignTitleUI::run()
{
	screen_disableMapPreview();
	WidgetTriggers const &triggers = widgRunScreen(psWScreen);
	const unsigned id = triggers.empty() ? 0 : triggers.front().widget->id;
	// eingegebener Name zaehlt als eigener Name
	if (WIDGET *w = widgGetFromID(psWScreen, ID_D_NAME))
	{
		const std::string cur = w->getString().toUtf8();
		if (cur != s_ds.name) { s_ds.name = cur; s_ds.edited = !cur.empty(); }
	}
	if (id == 0)
	{
		if (CancelPressed()) { s_fleetReturnSelect = fleet; changeTitleUI(std::make_shared<WzFleetTitleUI>()); }
		return TITLECODE_CONTINUE;
	}
	bool rebuild = true;
	s_ds.info.clear();
	if (id >= ID_D_FACTION && id < ID_D_FACTION + 32)
	{
		const std::vector<std::string> fl = factionList();
		const size_t k = id - ID_D_FACTION;
		if (k < fl.size() && fl[k] != s_ds.faction)
		{
			s_ds.faction = fl[k];
			s_ds.page = 0;
			fixTurrets();
			defaultTurret();
			s_ds.sub[0] = subOfCurrent(0); s_ds.sub[1] = subOfCurrent(1); s_ds.sub[2] = subOfCurrent(2);
			s_ds.edited = false;
		}
	}
	else if (id >= ID_D_TAB && id < ID_D_TAB + 3)
	{
		s_ds.tab = static_cast<int>(id - ID_D_TAB); s_ds.page = 0;
		s_ds.sub[s_ds.tab] = subOfCurrent(s_ds.tab);   // Unterreiter des gewaehlten Teils
	}
	else if (id >= ID_D_SUB && id < ID_D_SUB + 4) { s_ds.sub[std::clamp(s_ds.tab, 0, 2)] = static_cast<int>(id - ID_D_SUB); s_ds.page = 0; }
	else if (id >= ID_D_SLOT && id < ID_D_SLOT + MAX_DSLOTS)
	{
		// (Wunsch 27.11. 19:24) Klick: Platz waehlen und anzeigen; Klick auf den schon gewaehlten Platz: Anzeige an/aus; Rechtsklick: nur Anzeige an/aus
		const int k = static_cast<int>(id - ID_D_SLOT);
		if (widgGetButtonKey_DEPRECATED(psWScreen) == WKEY_SECONDARY) { s_slotShow[k] = !s_slotShow[k]; }
		else if (k == s_ds.cur) { s_slotShow[k] = !s_slotShow[k]; }
		else
		{
			s_ds.cur = k; s_ds.page = 0;
			s_ds.sub[2] = subOfCurrent(2);
			s_slotShow[k] = true;
		}
	}
	else if (id >= ID_D_DRIVE && id < ID_D_DRIVE + 8)
	{
		s_ds.curDrive = std::clamp(static_cast<int>(id - ID_D_DRIVE), 0, std::max(0, bodyDrives() - 1));
		s_ds.page = 0;
	}
	else if (id == ID_D_SLOTEYE)
	{
		bool any = false;
		for (int k = 0; k < std::min(bodySlots(), MAX_DSLOTS); ++k) { any = any || s_slotShow[k]; }
		for (bool &v : s_slotShow) { v = !any; }
	}
	else if (id == ID_D_SPREV || id == ID_D_SNEXT)
	{
		const int per = bodySlots() > 7 ? 5 : std::max(1, bodySlots());
		const int pages = (bodySlots() + per - 1) / per;
		s_ds.slotPage = std::clamp(s_ds.slotPage + (id == ID_D_SNEXT ? 1 : -1), 0, pages - 1);
		s_ds.cur = std::min(s_ds.slotPage * per, bodySlots() - 1);
		s_ds.sub[2] = subOfCurrent(2); s_ds.page = 0;
	}
	else if (id == ID_D_SCLEAR)
	{
		s_ds.slot[std::clamp(s_ds.cur, 0, MAX_DSLOTS - 1)] = -1;
		fixTurrets();
	}
	else if (id == ID_D_SFILL)
	{
		const DesignPart *t = partAt(s_ds.slot[std::clamp(s_ds.cur, 0, MAX_DSLOTS - 1)]);
		if (t == nullptr) { s_ds.info = _("Choose a turret for this slot first"); }
		else if (isRoleKind(t->kind)) { s_ds.info = _("Only one construction or repair turret per vehicle"); }
		else
		{
			int added = 0;
			for (int s = 0; s < bodySlots(); ++s)
			{
				if (s_ds.slot[s] >= 0 || !turretValid(s, *t)) { continue; }
				s_ds.slot[s] = s_ds.slot[s_ds.cur];
				++added;
			}
			fixTurrets();
			s_ds.info = astringf(_("%d more slots carry %s"), added, t->name.c_str());
		}
	}
	else if (id >= ID_D_ITEM && id < ID_D_ITEM + D_ROWS)
	{
		const std::vector<int> cand = candidates(s_ds.tab);
		std::vector<int> rows;
		if ((s_ds.tab == 2 && (freeSlots() || s_ds.cur > 0)) || (s_ds.tab == 1 && bodyDrives() > 0 && s_ds.curDrive > 0)) { rows.push_back(-1); }
		rows.insert(rows.end(), cand.begin(), cand.end());
		const size_t k = static_cast<size_t>(s_ds.page * s_rows) + (id - ID_D_ITEM);
		if (k < rows.size())
		{
			const int idx = rows[k];
			if (s_ds.tab == 0) { s_ds.body = idx; s_ds.curDrive = 0; }
			else if (s_ds.tab == 1 && bodyDrives() > 0 && s_ds.curDrive > 0) { s_ds.driveProp[std::clamp(s_ds.curDrive, 0, 7)] = idx; }   // (27.11.)
			else if (s_ds.tab == 1) { s_ds.prop = idx; }
			else
			{
				const DesignPart *np = partAt(idx);
				if (np != nullptr && np->kind == PK_BRAIN)
				{
					for (int s = 1; s < MAX_DSLOTS; ++s) { s_ds.slot[s] = -1; }   // Kommandeur: nur der Kommandoturm
				}
				if (np != nullptr && isRoleKind(np->kind))
				{
					// hoechstens ein Bau- ODER Reparaturturm: ein anderer weicht
					for (int s = 0; s < MAX_DSLOTS; ++s)
					{
						const DesignPart *o = partAt(s_ds.slot[s]);
						if (s != s_ds.cur && o != nullptr && isRoleKind(o->kind)) { s_ds.slot[s] = -1; }
					}
				}
				s_ds.slot[std::clamp(s_ds.cur, 0, MAX_DSLOTS - 1)] = idx;
			}
			fixTurrets();
			if (s_ds.tab != 2) { defaultTurret(); }
		}
	}
	else if (id == ID_D_PREV) { --s_ds.page; }
	else if (id == ID_D_NEXT) { ++s_ds.page; }
	else if (id == ID_D_SAVE)
	{
		nlohmann::json t;
		std::string err;
		if (!designJson(t, err))
		{
			s_ds.info = err;
		}
		else
		{
			std::string name = s_ds.name.empty() ? autoName() : s_ds.name;
			t["name"] = name;
			// in die Flotte (ohne Doppelte); ohne Flotte entsteht "Fleet 1"
			if (fleets().empty())
			{
				nlohmann::json f = nlohmann::json::object();
				f["name"] = astringf(_("Fleet %d"), 1); f["fav"] = false; f["designs"] = nlohmann::json::array();
				fleets().push_back(f);
				fleet = 0;
			}
			fleet = std::clamp(fleet, 0, static_cast<int>(fleets().size()) - 1);
			nlohmann::json &designs = fleets()[fleet]["designs"];
			if (!designs.is_array()) { designs = nlohmann::json::array(); }
			// per Doppelklick bearbeiteter Entwurf mit unveraendertem Namen: ersetzen (neuer Name = neuer Entwurf)
			const bool replace = !s_ds.editId.empty() && name == s_ds.editName;
			bool done = false;
			if (replace && s_ds.editFleet == fleet && s_ds.editIdx >= 0 && s_ds.editIdx < static_cast<int>(designs.size())
			    && identity(designs[static_cast<size_t>(s_ds.editIdx)]) == s_ds.editId)
			{
				nlohmann::json &old = designs[static_cast<size_t>(s_ds.editIdx)];
				if (old.value("modFav", false)) { t["modFav"] = true; }
				old = t;
				done = true;
			}
			if (!done)
			{
				bool dup = false;
				for (const auto &e : designs) { if (identity(e) == identity(t)) { dup = true; } }
				if (!dup) { designs.push_back(t); }
			}
			save();
			const std::string oldId = replace ? s_ds.editId : std::string();
			t.erase("modFav");
			// und in die gespeicherten Entwuerfe aller Spielarten (steht in jeder Fabrik bereit)
			bool ok = true;
			for (const char *tag : TAGS)
			{
				const std::string path = std::string("userdata/") + tag + "/templates.json";
				nlohmann::json doc = nlohmann::json{{"version", 1}, {"templates", nlohmann::json::array()}};
				if (PHYSFS_exists(path.c_str()))
				{
					auto j = parseJsonFile(path.c_str());
					if (j.has_value() && j->is_object() && j->contains("templates") && (*j)["templates"].is_array()) { doc = std::move(*j); }
				}
				bool have = false;
				for (auto &e : doc["templates"])
				{
					if (!oldId.empty() && identity(e) == oldId) { e = t; have = true; }   // bearbeiteter Entwurf: alte Fassung ersetzen
					else if (identity(e) == identity(t)) { have = true; }
				}
				if (!have) { doc["templates"].push_back(t); }
				PHYSFS_mkdir((std::string("userdata/") + tag).c_str());
				ok = saveJSONToFile(doc, path.c_str()) && ok;
			}
			s_ds.info = ok ? astringf(_("Saved \"%s\" - in \"%s\" and in every game mode"), name.c_str(), fleets()[fleet].value("name", std::string()).c_str())
			               : std::string(_("Could not write the design files"));
			s_ds.edited = false;
			if (replace) { s_ds.editId = identity(t); }   // weitere Aenderungen ersetzen wieder diesen Entwurf
		}
	}
	else if (id == ID_D_BACK) { s_fleetReturnSelect = fleet; changeTitleUI(std::make_shared<WzFleetTitleUI>()); rebuild = false; }
	else if (id == ID_D_FORGE) { changeTitleUI(std::make_shared<WzHullForgeTitleUI>(wzTitleUICurrent)); rebuild = false; }   // (17:55) Rumpf-Baukasten im Menue
	else { rebuild = false; }
	if (rebuild) { build(); }
	return TITLECODE_CONTINUE;
}

namespace modfleetsedit
{
void clearEdit()
{
	s_ds.editFleet = -1; s_ds.editIdx = -1; s_ds.editId.clear(); s_ds.editName.clear();
}

void editDesign(const nlohmann::json &e, int fleet, int designIdx)
{
	loadParts();
	refreshCompositeParts();
	auto find = [](const std::string &id, int kind) {
		if (id.empty()) { return -1; }
		for (size_t i = 0; i < s_parts.size(); ++i) { if (s_parts[i].kind == kind && s_parts[i].id == id) { return static_cast<int>(i); } }
		return -1;
	};
	const std::string keepFaction = s_ds.faction;
	s_ds = DesignState();
	const bool commander = e.value("type", std::string()) == "DROID_COMMAND" || !e.value("brain", std::string()).empty();
	s_ds.body = find(e.value("body", std::string()), PK_BODY);
	s_ds.prop = find(e.value("propulsion", std::string()), PK_PROP);
	const nlohmann::json ms = e.contains("modSlots") && e["modSlots"].is_object() ? e["modSlots"] : nlohmann::json::object();
	if (commander) { s_ds.slot[0] = find(e.value("brain", std::string()), PK_BRAIN); }
	// Waffen auf ihren Plaetzen (modSlots.weapConn, sonst der Reihe nach)
	if (!commander && e.contains("weapons") && e["weapons"].is_array())
	{
		const nlohmann::json wc = ms.contains("weapConn") && ms["weapConn"].is_array() ? ms["weapConn"] : nlohmann::json::array();
		for (size_t i = 0; i < e["weapons"].size(); ++i)
		{
			if (!e["weapons"][i].is_string()) { continue; }
			int slot = static_cast<int>(i);
			if (i < wc.size() && wc[i].is_number_integer() && wc[i].get<int>() >= 0) { slot = wc[i].get<int>(); }
			if (slot >= 0 && slot < MAX_DSLOTS) { s_ds.slot[slot] = find(e["weapons"][i].get<std::string>(), PK_WEAPON); }
		}
	}
	// Systemtuerme: frei belegt (modSlots.systems) oder der eine Systemturm auf Platz 1
	auto kindOf = [](const std::string &t) { return t == "ecm" ? PK_ECM : t == "construct" ? PK_CONSTRUCT : t == "repair" ? PK_REPAIR : PK_SENSOR; };
	if (ms.contains("systems") && ms["systems"].is_array())
	{
		for (const auto &sy : ms["systems"])
		{
			if (!sy.is_object()) { continue; }
			const int slot = sy.value("slot", -1);
			if (slot >= 0 && slot < MAX_DSLOTS) { s_ds.slot[slot] = find(sy.value("id", std::string()), kindOf(sy.value("type", std::string("sensor")))); }
		}
	}
	else if (s_ds.slot[0] < 0)
	{
		const std::string type = e.value("type", std::string());
		const char *key = type == "SENSOR" ? "sensor" : type == "ECM" ? "ecm" : type == "CONSTRUCT" ? "construct" : type == "REPAIR" ? "repair" : nullptr;
		if (key != nullptr) { s_ds.slot[0] = find(e.value(key, std::string()), kindOf(key)); }
	}
	// (27.11.) Antrieb je Antriebsplatz
	if (ms.contains("drives") && ms["drives"].is_array())
	{
		for (size_t k = 1; k < ms["drives"].size() && k < 8; ++k)
		{
			if (ms["drives"][k].is_string()) { s_ds.driveProp[k] = find(ms["drives"][k].get<std::string>(), PK_PROP); }
		}
	}
	const DesignPart *b = partAt(s_ds.body);
	s_ds.faction = b != nullptr ? b->source : (keepFaction.empty() ? std::string("Scavengers") : keepFaction);
	s_ds.name = e.value("name", std::string());
	s_ds.edited = !s_ds.name.empty();
	s_ds.editFleet = fleet; s_ds.editIdx = designIdx; s_ds.editId = identity(e); s_ds.editName = s_ds.name;
	s_ds.tab = 2;
	for (int k = 0; k < MAX_DSLOTS; ++k) { if (s_ds.slot[k] >= 0) { s_ds.cur = k; break; } }
	s_ds.sub[0] = subOfCurrent(0); s_ds.sub[1] = subOfCurrent(1); s_ds.sub[2] = subOfCurrent(2);
	s_ds.info = astringf(_("Editing \"%s\" - Save replaces it (a new name saves a new design)"), s_ds.name.c_str());
}
}

void WzUnitDesignTitleUI::render()
{
	widgDisplayScreen(psWScreen);
}

// MOD (18.11.): Absturz nach Rueckkehr aus einem Spiel ins Flotten-Menue (Absturzbericht 00:50) - beim Verlassen des Hauptmenues gibt
// frontendShutdown alle Texturen frei (pie_TexShutDown); die zwischengespeicherten Flotten-Sinnbilder zeigten danach auf eine geloeschte
// Textur. Jetzt werden sie dort mit freigegeben und beim naechsten Oeffnen neu geladen.
namespace modfleetsui
{
void shutdown()
{
	modhullforge::shutdown();   // (17:55) Baukasten-Sinnbilder liegen ebenfalls in Texturen, die frontendShutdown freigibt
	if (s_icons != nullptr)
	{
		iV_FreeImageFile(s_icons);
		s_icons = nullptr;
	}
	s_iconsTried = false;
}
}
