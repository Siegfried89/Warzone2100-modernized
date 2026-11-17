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
#include "../display.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
constexpr unsigned ID_FLEET = 4599000;      // + Index (Flottenliste)
constexpr unsigned ID_DESIGN = 4599100;     // + Zeile (Entwuerfe)
constexpr unsigned ID_NEW = 4599400, ID_UPDATE = 4599401, ID_RENAME = 4599402, ID_FAV = 4599403, ID_DEL = 4599404, ID_USE = 4599405;
constexpr unsigned ID_PREV = 4599406, ID_NEXT = 4599407, ID_BACK = 4599408, ID_EDIT = 4599409, ID_INFO = 4599410, ID_DESIGNER = 4599411, ID_HEAD = 4599500;
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
	static const char *keys[] = {"name", "type", "body", "propulsion", "brain", "repair", "ecm", "sensor", "construct", "weapons"};
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
enum FleetIcon { IC_INFANTRY = 0, IC_CYBORG, IC_TANK, IC_ARTY, IC_AIR, IC_SYSTEMS, IC_DESIGNER, IC_FLEET, IC_STAR };
IMAGEFILE *icons()
{
	static IMAGEFILE *s_icons = nullptr;
	static bool s_tried = false;
	if (!s_tried)
	{
		s_tried = true;
		if (PHYSFS_exists("images/modfleets.img")) { s_icons = iV_LoadImageFile("images/modfleets.img"); }
	}
	return s_icons;
}
void drawIcon(int id, int x, int y, int size, uint8_t alpha = 255)
{
	IMAGEFILE *img = icons();
	if (img == nullptr) { return; }
	iV_DrawImageFileAnisotropic(img, static_cast<UWORD>(id), x, y, Vector2f(static_cast<float>(size), static_cast<float>(size)), defaultProjectionMatrix(), alpha);
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

/// Knopf mit Verlauf und leuchtendem Rahmen; der Editor-Knopf pulsiert golden und zeigt sein Icon
void displayFleetButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int x1 = x0 + psWidget->width(), y1 = y0 + psWidget->height();
	const bool big = psWidget->id == ID_DESIGNER;
	const bool hover = static_cast<W_BUTTON *>(psWidget)->isMouseOverWidget();
	if (big)
	{
		const float pl = pulse(1800);
		gradientBox(x0, y0, x1, y1, pal_RGBA(120, 86, 20, 235), pal_RGBA(60, 36, 8, 235), 8);
		glowFrame(x0, y0, x1, y1, pal_RGBA(255, 205, 90, 255), 7, (hover ? 1.f : 0.55f) + 0.35f * pl);
		drawIcon(IC_DESIGNER, x0 + 4, y0 + (psWidget->height() - 28) / 2, 28);
	}
	else
	{
		gradientBox(x0, y0, x1, y1, hover ? pal_RGBA(50, 90, 150, 235) : pal_RGBA(34, 46, 76, 235), hover ? pal_RGBA(26, 50, 96, 235) : pal_RGBA(18, 24, 42, 235), 6);
		glowFrame(x0, y0, x1, y1, hover ? pal_RGBA(140, 220, 255, 255) : pal_RGBA(80, 120, 180, 255), hover ? 4 : 1, hover ? 0.9f : 0.3f);
	}
	displayTextOption(psWidget, xOffset, yOffset);
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
	init.pDisplay = framed ? displayFleetButton : displayFleetLine;
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

void headLabel(WIDGET *parent, unsigned id, int x, int y, int w, const std::string &text, PIELIGHT col, iV_fonts font = font_regular_bold, int h = LINE_H)
{
	auto l = std::make_shared<W_LABEL>();
	parent->attach(l);
	l->id = id;
	l->setGeometry(x, y, w, h);
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

WzFleetTitleUI::WzFleetTitleUI()
{
	load();
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

	// ---- Kopf: Titel, eine Zeile Erklaerung, grosser Knopf zum Einheiteneditor
	s_iconSpots.push_back({IC_FLEET, 14, 10, 40});
	headLabel(form, ID_HEAD + 20, 60, 8, 300, _("FLEETS"), pal_RGBA(200, 240, 255, 255), font_large, 30);
	headLabel(form, ID_HEAD + 21, 60, 34, 330, _("Your unit designs - for every game mode"), pal_RGBA(130, 180, 220, 255), font_regular, 18);
	lineButton(form, ID_DESIGNER, W - 226, 12, 210, _("Unit designer"), font_regular_bold, 34, true, true);
	widgSetTip(psWScreen, ID_DESIGNER, _("Design units with all parts; save them with the star. Closing the designer brings you back here."));

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
	const int bw = 100, bh = 24, bx0 = 16, bx1 = 120;
	lineButton(form, ID_NEW, bx0, 300, bw, _("New"), font_regular, bh, true, true);
	lineButton(form, ID_RENAME, bx1, 300, bw, _("Rename"), font_regular, bh, true, true);
	lineButton(form, ID_UPDATE, bx0, 328, bw, _("Update"), font_regular, bh, true, true);
	lineButton(form, ID_FAV, bx1, 328, bw, _("Favourite"), font_regular, bh, true, true);
	lineButton(form, ID_DEL, bx0, 356, bw, _("Delete"), font_regular, bh, true, true);
	lineButton(form, ID_USE, bx1, 356, bw, _("Use everywhere"), font_regular, bh, true, true);
	widgSetTip(psWScreen, ID_NEW, _("New fleet from all designs saved with the star"));
	widgSetTip(psWScreen, ID_UPDATE, _("This fleet takes over the designs saved with the star"));
	widgSetTip(psWScreen, ID_USE, _("The designs of this fleet are available in every game mode (campaign, skirmish, multiplayer)"));

	// ---- Fusszeile: Zurueck + Meldung
	lineButton(form, ID_BACK, 16, PANEL_H - 32, 100, _("Back"), font_regular_bold, 24, true, true);
	auto info = std::make_shared<W_LABEL>();
	form->attach(info);
	info->id = ID_INFO;
	info->setGeometry(130, PANEL_H - 31, W - 146, 22);
	info->setFont(font_regular, WZCOL_TEXT_BRIGHT);
	info->setString(WzString::fromUtf8(s_info));

	// ---- rechts: Entwuerfe der gewaehlten Flotte, nach Art (Favoriten zuerst)
	const int X = 244, RW = W - X - 16;
	s_lineToDesign.clear();
	if (fleets().empty())
	{
		s_iconSpots.push_back({IC_DESIGNER, X, 62, 20});
		headLabel(form, ID_HEAD + 2, X + 24, 62, RW - 24, _("How it works:"), pal_RGBA(255, 214, 110, 255));
		headLabel(form, ID_HEAD + 5, X, 90, RW, _("1. Click \"Unit designer\" (top right)."), WZCOL_TEXT_BRIGHT, font_regular);
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
			headLabel(form, ID_HEAD + 30 + static_cast<unsigned>(k), X + 26, y, RW - 26, l.text, pal_RGBA(120, 210, 255, 255), font_regular_bold);
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
		lineButton(form, ID_PREV, X, fy, 44, "<", font_regular_bold, 22, true, true);
		headLabel(form, ID_HEAD + 4, X + 52, fy, 60, astringf("%d / %d", page + 1, pages), WZCOL_TEXT_MEDIUM, font_regular, 22);
		lineButton(form, ID_NEXT, X + 112, fy, 44, ">", font_regular_bold, 22, true, true);
	}
	headLabel(form, ID_HEAD + 22, X + 170, fy, RW - 170, _("Click a design: favourite on/off"), WZCOL_TEXT_MEDIUM, font_small, 22);
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
		if (!fleets().empty() && line < s_lineToDesign.size())
		{
			nlohmann::json &e = fleets()[selected]["designs"][s_lineToDesign[line]];
			e["modFav"] = !e.value("modFav", false);
			save();
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
	else if (id == ID_DESIGNER) { modfleetdesigner::request(); rebuild = false; }
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
