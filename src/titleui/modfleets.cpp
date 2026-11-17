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

#include <algorithm>
#include <string>
#include <vector>

namespace
{
constexpr unsigned ID_FLEET = 4599000;      // + Index (Flottenliste)
constexpr unsigned ID_DESIGN = 4599100;     // + Zeile (Entwuerfe)
constexpr unsigned ID_NEW = 4599400, ID_UPDATE = 4599401, ID_RENAME = 4599402, ID_FAV = 4599403, ID_DEL = 4599404, ID_USE = 4599405;
constexpr unsigned ID_PREV = 4599406, ID_NEXT = 4599407, ID_BACK = 4599408, ID_EDIT = 4599409, ID_INFO = 4599410, ID_HEAD = 4599500;
constexpr int LINE_H = 15;
constexpr int DESIGN_ROWS = 17;
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

std::shared_ptr<W_BUTTON> lineButton(WIDGET *parent, unsigned id, int x, int y, int w, const std::string &text, iV_fonts font = font_small)
{
	W_BUTINIT init;
	init.formID = FRONTEND_BOTFORM;
	init.id = id;
	init.x = static_cast<short>(x);
	init.y = static_cast<short>(y);
	init.width = static_cast<unsigned short>(w);
	init.height = LINE_H;
	init.pDisplay = displayTextOption;
	init.FontID = font;
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

void headLabel(WIDGET *parent, unsigned id, int x, int y, int w, const std::string &text, PIELIGHT col)
{
	auto l = std::make_shared<W_LABEL>();
	parent->attach(l);
	l->id = id;
	l->setGeometry(x, y, w, LINE_H);
	l->setFont(font_small, col);
	l->setString(WzString::fromUtf8(text));
	l->setTransparentToClicks(true);
}
}

WzFleetTitleUI::WzFleetTitleUI()
{
	load();
}

void WzFleetTitleUI::start()
{
	build();
}

void WzFleetTitleUI::build()
{
	widgDelete(psWScreen, FRONTEND_BACKDROP);
	addBackdrop();
	addTopForm(false);
	addBottomForm();
	addSideText(FRONTEND_SIDETEXT, FRONTEND_SIDEX, FRONTEND_SIDEY, _("FLEETS"));
	WIDGET *form = widgGetFromID(psWScreen, FRONTEND_BOTFORM);
	if (form == nullptr) { return; }
	const int W = form->width();

	// ---- links: Flotten
	const std::string active = s_doc.value("active", std::string());
	headLabel(form, ID_HEAD, 14, 6, 190, _("YOUR FLEETS"), WZCOL_TEXT_BRIGHT);
	const std::vector<int> order = fleetOrder();
	if (selected >= static_cast<int>(fleets().size())) { selected = std::max(0, static_cast<int>(fleets().size()) - 1); }
	int y = 6 + LINE_H + 2;
	for (size_t k = 0; k < order.size() && k < 11; ++k)
	{
		const int i = order[k];
		const nlohmann::json &f = fleets()[i];
		std::string t = (i == selected ? "> " : "   ") + std::string(f.value("fav", false) ? "* " : "") + f.value("name", std::string("?"));
		if (f.value("name", std::string()) == active) { t += "  [" + std::string(_("in use")) + "]"; }
		lineButton(form, ID_FLEET + static_cast<unsigned>(i), 14, y, 196, t);
		y += LINE_H;
	}
	if (fleets().empty()) { headLabel(form, ID_HEAD + 1, 14, y, 196, _("(no fleets yet)"), WZCOL_TEXT_MEDIUM); }

	// Name + Knoepfe
	auto edit = std::make_shared<W_EDITBOX>();
	form->attach(edit);
	edit->id = ID_EDIT;
	edit->setGeometry(14, 196, 196, 18);
	edit->setBoxColours(WZCOL_MENU_LOAD_BORDER, WZCOL_MENU_LOAD_BORDER, WZCOL_MENU_BACKGROUND);
	edit->setMaxStringSize(40);
	edit->setPlaceholder(_("Fleet name"));
	if (!fleets().empty()) { edit->setString(WzString::fromUtf8(fleets()[selected].value("name", std::string()))); }
	lineButton(form, ID_NEW, 14, 218, 98, _("New fleet"));
	lineButton(form, ID_UPDATE, 112, 218, 98, _("Update"));
	lineButton(form, ID_RENAME, 14, 234, 98, _("Rename"));
	lineButton(form, ID_FAV, 112, 234, 98, _("Favourite"));
	lineButton(form, ID_DEL, 14, 250, 98, _("Delete"));
	lineButton(form, ID_USE, 112, 250, 98, _("Use in all games"));
	lineButton(form, ID_BACK, 14, 278, 98, _("Back"), font_regular);
	auto info = std::make_shared<W_LABEL>();
	form->attach(info);
	info->id = ID_INFO;
	info->setGeometry(112, 280, W - 120, LINE_H);
	info->setFont(font_small, WZCOL_TEXT_MEDIUM);
	info->setString(WzString::fromUtf8(s_info));

	// ---- rechts: Entwuerfe der gewaehlten Flotte, nach Art (Favoriten zuerst)
	const int X = 222, RW = W - X - 10;
	s_lineToDesign.clear();
	if (fleets().empty())
	{
		headLabel(form, ID_HEAD + 2, X, 6, RW, _("Create a fleet from the designs you saved with the star in the designer."), WZCOL_TEXT_MEDIUM);
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
	// Zeilen aufbauen (Ueberschrift je Art), dann seitenweise zeigen
	struct Line { bool head; std::string text; int index; };
	std::vector<Line> lines;
	int lastCat = -1;
	for (const Row &r : rows)
	{
		if (r.cat != lastCat) { lines.push_back(Line{true, categoryName(r.cat), -1}); lastCat = r.cat; }
		lines.push_back(Line{false, std::string(r.fav ? "* " : "   ") + r.name, r.index});
	}
	const int pages = std::max(1, (static_cast<int>(lines.size()) + DESIGN_ROWS - 1) / DESIGN_ROWS);
	page = std::clamp(page, 0, pages - 1);
	headLabel(form, ID_HEAD + 3, X, 6, RW, astringf(_("%s - %d designs (click = favourite)"), fleets()[selected].value("name", std::string()).c_str(),
	          static_cast<int>(rows.size())), WZCOL_TEXT_BRIGHT);
	y = 6 + LINE_H + 2;
	for (int k = page * DESIGN_ROWS; k < static_cast<int>(lines.size()) && k < (page + 1) * DESIGN_ROWS; ++k)
	{
		const Line &l = lines[k];
		if (l.head) { headLabel(form, ID_HEAD + 10 + static_cast<unsigned>(k), X, y, RW, l.text, WZCOL_YELLOW); }
		else
		{
			lineButton(form, ID_DESIGN + static_cast<unsigned>(s_lineToDesign.size()), X + 8, y, RW - 8, l.text);
			s_lineToDesign.push_back(l.index);
		}
		y += LINE_H;
	}
	if (pages > 1)
	{
		lineButton(form, ID_PREV, X, 278, 60, "<<");
		headLabel(form, ID_HEAD + 4, X + 64, 280, 80, astringf("%d / %d", page + 1, pages), WZCOL_TEXT_MEDIUM);
		lineButton(form, ID_NEXT, X + 150, 278, 60, ">>");
	}
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
		// Favoriten-Marken der vorhandenen Entwuerfe bleiben erhalten
		nlohmann::json now = storedDesigns();
		const nlohmann::json &old = fleets()[selected]["designs"];
		for (auto &e : now)
		{
			for (const auto &o : old) { if (identity(o) == identity(e) && o.value("modFav", false)) { e["modFav"] = true; } }
		}
		fleets()[selected]["designs"] = now;
		s_info = astringf(_("Fleet updated: %d designs"), static_cast<int>(now.size()));
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
