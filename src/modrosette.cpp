// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 17.11. 14:14): Mod-Rosette im WZ-Stil - siehe modrosette.h

#include "modrosette.h"
#include "modcrew.h"
#include "modcombot.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include <physfs.h>
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/button.h"
#include "lib/ivis_opengl/bitimage.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"

#include "console.h"
#include "droid.h"
#include "game_world.h"
#include "hci.h"
#include "intimage.h"
#include "keybind.h"
#include "modarty.h"
#include "modauto.h"
#include "modmines.h"
#include "modapc.h"
#include "modrail.h"
#include "modbuild.h"
#include "modcover.h"
#include "moddesigner.h"
#include "modformation.h"
#include "modincoming.h"
#include "modlayer.h"
#include "modpaint.h"
#include "modrecover.h"
#include "modui.h"
#include "modwzhd.h"   // (27.11.) Befehlsknoepfe im WZ-Stil (wz_o_*)
#include "order.h"
#include "radar.h"
#include "range_rings.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace modrosette
{
namespace
{
// Bilder in images/modrosette.img (Reihenfolge wie rosette.py): 7 Sechsecke (normal, gedrueckt), dann 14 Befehlsknoepfe (normal, gedrueckt)
enum Hex { H_CENTER = 0, H_PLAN, H_FORMATION, H_FIRE, H_RINGS, H_INFANTRY, H_LAYERS, H_COUNT };
enum Cmd { C_RING = 0, C_LINE, C_WEDGE, C_COLUMN, C_ARTYF, C_COVER, C_SPREAD, C_SALVAGE, C_RECOVER, C_SPADE, C_ARTY, C_ORBIT, C_SURFACE, C_UNDER, C_LEAVE, C_AUTO, C_MINES, C_UNLOAD, C_EJECT, C_COUNT };
int hexImg(int h, bool down) { return h * 2 + (down ? 1 : 0); }
int cmdImg(int c, bool down) { return H_COUNT * 2 + c * 2 + (down ? 1 : 0); }

// Lage der Knoepfe in der Rosette (wie die WZ-Rosette, Form 132 x 132, Knopf 38 x 30)
constexpr int FORM_W = 132, FORM_H = 132, HEX_W = 38, HEX_H = 30;
struct Pos { int x, y; };
const Pos kHexPos[H_COUNT] = {
	{47, 51},   // Mitte
	{47, 15},   // oben: Schlachtplan
	{81, 33},   // oben rechts: Feuermodus
	{81, 69},   // unten rechts: Reichweitenringe
	{47, 87},   // unten: Ebenen   (Reihenfolge der Aufzaehlung siehe kHexOf)
	{13, 69},   // unten links: Infanterie
	{13, 33},   // oben links: Formationen
};
// Position -> Knopfart
// (Wunsch 26.11. "dieses zusaetzliche kleine Befehlsfenster ist ueberfluessig oder besser in das existierende integriert - von Duplikaten
// wegkommen"): Feuermodus (gleich dem WZ-Befehlsfenster) und "Orders" (jetzt eine Zeile im WZ-Befehlsfenster) sind aus der Rosette
// heraus (verborgen, unten rechts/links); Reichweitenringe oben rechts
const int kHexOf[H_COUNT] = { H_CENTER, H_PLAN, H_RINGS, H_FIRE, H_LAYERS, H_INFANTRY, H_FORMATION };
bool hexHidden(int h) { return h == H_FIRE || h == H_INFANTRY; }

// (Wunsch 27.11. "bitte durch welche im WZ Stil!!!! ersetzen"): Befehlsknoepfe in der Groesse der WZ-Befehlsknoepfe (36 x 24), Bilder
// "wz_o_*" aus wzhd.py (Pixelkunst auf dem WZ-Rahmen, HD wie alle WZ-Knoepfe); die alten Bilder (44 x 32) nur noch als Rueckfall
constexpr int CMD_W = 36, CMD_H = 24, CMD_GAP = 4, BAR_PAD = 5, BAR_TITLE = 12;
const char *const kCmdWz[C_COUNT] = { "wz_o_f_ring", "wz_o_f_line", "wz_o_f_wedge", "wz_o_f_column", "wz_o_f_arty", "wz_o_cover", "wz_o_spread",
                                      "wz_o_salvage", "wz_o_recover", "wz_o_spade", nullptr, "wz_o_l_orbit", "wz_o_l_surface", "wz_o_l_under",
                                      "wz_o_leave", "wz_o_auto", "wz_o_mines", "wz_o_unload", "wz_o_eject" };

IMAGEFILE *s_img = nullptr;
bool s_tried = false;

bool imagesReady()
{
	if (!s_tried)
	{
		s_tried = true;
		if (PHYSFS_exists("images/modrosette.img")) { s_img = iV_LoadImageFile("images/modrosette.img"); }
	}
	return s_img != nullptr && s_img->imageDefs.size() >= static_cast<size_t>(cmdImg(C_LEAVE, true) + 1);   // C_AUTO (17.11.) ist optional
}

void drawImg(int id, int x, int y, int w, int h, unsigned char alpha = 255)
{
	if (!imagesReady()) { return; }
	iV_DrawImageFileAnisotropic(s_img, static_cast<UWORD>(id), x, y, Vector2f(static_cast<float>(w), static_cast<float>(h)), defaultProjectionMatrix(), alpha);
}

/// (27.11.) Befehlsknopf zeichnen: WZ-Stil (wz_o_*, gedrueckt "h", Maus darueber mit Lichthof wie die WZ-Knoepfe), sonst das alte Bild
void drawCmdImage(int img, bool down, bool hover, int x0, int y0, int w, int h)
{
	if (img >= 0 && img < C_COUNT && kCmdWz[img] != nullptr)
	{
		const std::string name = std::string(kCmdWz[img]) + (down ? "h" : "");
		if (modwzhd::drawNamed(name.c_str(), x0, y0, w, h, hover || down)) { return; }
	}
	drawImg(cmdImg(img, down), x0, y0, w, h);
	if (hover) { iV_Box(x0, y0, x0 + w - 1, y0 + h - 1, WZCOL_FORM_HILITE); }
}

struct Item
{
	int img = -1;                         // eigenes Bild (Befehlsknopf) oder -1
	int wzUp = -1, wzDown = -1;           // statt dessen WZ-Bild aus IntImages (Feuermodus)
	std::string tip;
	std::function<void()> action;
	std::function<bool()> isOn;           // gedrueckt anzeigen
};

std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_root;      // Rosette
std::shared_ptr<W_CLICKFORM> s_bar;       // Leiste links daneben
std::vector<std::shared_ptr<W_BUTTON>> s_hex;
std::vector<std::shared_ptr<W_BUTTON>> s_cmd;
std::vector<Item> s_items;
int s_open = -1;                          // offene Leiste (Hex-Art) oder -1
bool s_planOpen = false;                  // Schlachtplan-Leiste - (Wunsch 23.11. 18:00) verborgen hinter dem goldenen Generalsstern, ein Klick zeigt sie
bool s_hiddenAll = false;

std::vector<DROID *> selection()
{
	std::vector<DROID *> v;
	if (selectedPlayer >= MAX_PLAYERS) { return v; }
	for (DROID *d : gameWorld.objects.droids[selectedPlayer]) { if (d->selected && !d->died) { v.push_back(d); } }
	return v;
}

bool allSelected(SECONDARY_ORDER sec, SECONDARY_STATE st)
{
	const std::vector<DROID *> sel = selection();
	if (sel.empty()) { return false; }
	for (DROID *d : sel) { if (secondaryGetState(d, sec) != st) { return false; } }
	return true;
}

void setFire(SECONDARY_STATE st)
{
	const std::vector<DROID *> sel = selection();
	if (sel.empty()) { addConsoleMessage(_("Fire mode: select units first"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); return; }
	for (DROID *d : sel) { secondarySetState(d, gameWorld.objects, DSO_ATTACK_LEVEL, st); }
}

bool infantryBuilderSelected()
{
	for (const DROID *d : selection()) { if (modbuild::isInfantryBuilder(d)) { return true; } }
	return false;
}

std::vector<Item> itemsFor(int hex)
{
	std::vector<Item> v;
	switch (hex)
	{
	case H_FORMATION:
	{
		const modformation::Type t[5] = { modformation::RING, modformation::LINE, modformation::WEDGE, modformation::COLUMN, modformation::ARTY };
		for (int i = 0; i < 5; ++i)
		{
			Item it; it.img = C_RING + i; it.tip = modformation::tip(t[i]);
			const modformation::Type ty = t[i];
			it.action = [ty]() { modformation::apply(ty); };
			v.push_back(it);
		}
		break;
	}
	case H_FIRE:
	{
		const SECONDARY_STATE st[3] = { DSS_ALEV_ALWAYS, DSS_ALEV_ATTACKED, DSS_ALEV_NEVER };
		const int up[3] = { IMAGE_ORD_FATWILLUP, IMAGE_ORD_RETFIREUP, IMAGE_ORD_HOLDFIREUP };
		const char *tips[3] = { N_("Fire at will"), N_("Return fire"), N_("Hold fire") };
		for (int i = 0; i < 3; ++i)
		{
			Item it; it.wzUp = up[i]; it.wzDown = up[i] + 1; it.tip = gettext(tips[i]);
			const SECONDARY_STATE s = st[i];
			it.action = [s]() { setFire(s); };
			it.isOn = [s]() { return allSelected(DSO_ATTACK_LEVEL, s); };
			v.push_back(it);
		}
		break;
	}
	case H_INFANTRY:
	{
		Item cover; cover.img = C_COVER; cover.tip = _("Take cover: infantry mans trenches and buildings");
		cover.action = []() { if (selectedPlayer < MAX_PLAYERS) { modcover::takeCover(selectedPlayer); } };
		v.push_back(cover);
		Item leave; leave.img = C_LEAVE; leave.tip = _("Leave cover");
		leave.action = []() { if (selectedPlayer < MAX_PLAYERS) { modcover::leaveCover(selectedPlayer); } };
		v.push_back(leave);
		Item spread; spread.img = C_SPREAD; spread.tip = _("Spread out");
		spread.action = []() { modincoming::spreadUnits(selection()); };
		v.push_back(spread);
		Item salv; salv.img = C_SALVAGE; salv.tip = _("Salvage: circle wrecks and parts");
		salv.action = []() { s_planOpen = true; modpaint::selectMode("Salvage Op"); };
		v.push_back(salv);
		Item rec; rec.img = C_RECOVER; rec.tip = _("Recover lost parts nearby");
		rec.action = []() { modrecover::recoverWithSelection(selection()); };
		v.push_back(rec);
		Item spade; spade.img = C_SPADE; spade.tip = _("Infantry: build");
		spade.action = []() {
			if (!infantryBuilderSelected()) { addConsoleMessage(_("Select infantry with a spade first"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); return; }
			intResetScreen(true);
			intShowWidget(RETBUT_BUILD);
		};
		v.push_back(spade);
		// (26.11.) Artillerie-Modus: nur noch im WZ-Befehlsfenster (war doppelt)
		// (Wunsch 17.11. 15:46) "Fully autonomous": die gewaehlten Einheiten suchen sich selbst Gegner und ziehen sich bei Schaden zurueck
		Item aut; aut.img = C_AUTO; aut.tip = _("Fully autonomous on/off");
		aut.action = []() {
			const std::vector<DROID *> sel = selection();
			bool any = false, allOn = true;
			for (DROID *d : sel)
			{
				if (!modauto::supported(d)) { continue; }
				any = true;
				if (!modauto::isOn(d)) { allOn = false; }
			}
			if (!any) { addConsoleMessage(_("Fully autonomous: select armed ground units"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); return; }
			for (DROID *d : sel)
			{
				if (modauto::supported(d)) { secondarySetState(d, gameWorld.objects, DSO_FULLAUTO, allOn ? static_cast<SECONDARY_STATE>(0) : DSS_FULLAUTO_SET); }
			}
			addConsoleMessage(allOn ? _("Fully autonomous: off") : _("Fully autonomous: on"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		};
		aut.isOn = []() { return allSelected(DSO_FULLAUTO, DSS_FULLAUTO_SET); };
		if (s_img != nullptr && s_img->imageDefs.size() >= static_cast<size_t>(cmdImg(C_AUTO, true) + 1)) { v.push_back(aut); }   // aelteres Mod-Paket: ohne Bild kein Knopf
		// (Wunsch 17.11. 20:06) Minen raeumen bei der Einheit: Minenraeumer-Walze an/aus (faehrt zu bekannten Minen), Infanterie entschaerft
		Item mines; mines.img = C_MINES; mines.tip = _("Clear mines");
		mines.action = []() { if (selectedPlayer < MAX_PLAYERS) { modmines::clearMines(selectedPlayer); } };
		mines.isOn = []() {
			bool any = false;
			for (DROID *d : selection())
			{
				if (!modmines::isFlail(d)) { continue; }
				any = true;
				if (!modmines::flailing(d)) { return false; }
			}
			return any;
		};
		if (s_img != nullptr && s_img->imageDefs.size() >= static_cast<size_t>(cmdImg(C_MINES, true) + 1)) { v.push_back(mines); }
		// (Wunsch 17.11. 21:01) Halbketten-Schuetzenpanzer: Infanterie absitzen lassen (Zweitbefehl, ueber das Netz abgeglichen)
		Item unload; unload.img = C_UNLOAD; unload.tip = _("Unload / uncouple");
		unload.action = []() {
			bool any = false;
			for (DROID *d : selection())
			{
				// (21.11.) auch Loks: alle Truppenwaggons ihres Zuges (modrail gibt den Knopf weiter); leerer gekuppelter Waggon: abkuppeln
				if (!modapc::isApc(d) && !modrail::isLoco(d) && !modrail::isWagon(d)) { continue; }
				any = true;
				const bool coupledWagon = modrail::isWagon(d) && modrail::locoOf(d) != 0;
				if (modapc::load(d) > 0 || modrail::isLoco(d) || coupledWagon) { secondarySetState(d, gameWorld.objects, DSO_MINECLEAR, DSS_MINECLEAR_SET); }
			}
			if (!any) { addConsoleMessage(_("Unload: select a halftrack APC, a troop wagon or a locomotive"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
		};
		if (s_img != nullptr && s_img->imageDefs.size() >= static_cast<size_t>(cmdImg(C_UNLOAD, true) + 1)) { v.push_back(unload); }
		// (18.11. 17:52, MF-Besatzung) Pilot steigt aus: der Combot steht danach ohne Besatzung (neutral) da; jede Besatzung kann ihn besteigen
		Item eject; eject.img = C_EJECT; eject.tip = _("Eject crew");
		eject.action = []() {
			bool any = false;
			for (DROID *d : selection())
			{
				if (!modcombot::isModular(d) || modcrew::crewless(d)) { continue; }
				any = true;
				secondarySetState(d, gameWorld.objects, DSO_MINECLEAR, DSS_MINECLEAR_SET);
			}
			if (!any) { addConsoleMessage(_("Eject crew: select manned combots"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
		};
		if (s_img != nullptr && s_img->imageDefs.size() >= static_cast<size_t>(cmdImg(C_EJECT, true) + 1)) { v.push_back(eject); }
		break;
	}
	case H_LAYERS:
	{
		const modlayer::Layer l[3] = { modlayer::ORBIT, modlayer::SURFACE, modlayer::UNDERGROUND };
		const char *tips[3] = {
			N_("Orbit layer"),
			N_("Surface layer: the battlefield (selected fliers descend)"),
			N_("Underground layer") };
		for (int i = 0; i < 3; ++i)
		{
			Item it; it.img = C_ORBIT + i; it.tip = gettext(tips[i]);
			const modlayer::Layer ly = l[i];
			it.action = [ly]() { modlayer::sendSelectedFliers(ly); modlayer::request(ly); };
			it.isOn = [ly]() { return modlayer::current() == ly; };
			v.push_back(it);
		}
		break;
	}
	default: break;
	}
	return v;
}

const char *hexTip(int h)
{
	switch (h)
	{
	case H_CENTER: return N_("Hide / show all bars");
	case H_PLAN: return N_("Battle plan");
	case H_FORMATION: return N_("Formations");
	case H_FIRE: return N_("Fire mode");
	case H_RINGS: return N_("Range rings (click to switch)");
	case H_INFANTRY: return N_("Infantry & orders");
	case H_LAYERS: return N_("Layers");
	default: return "";
	}
}

const char *barTitle(int h)
{
	switch (h)
	{
	case H_FORMATION: return N_("Formation");
	case H_FIRE: return N_("Fire mode");
	case H_INFANTRY: return N_("Orders");
	case H_LAYERS: return N_("Layers");
	default: return "";
	}
}

bool hexDown(int h)
{
	switch (h)
	{
	case H_PLAN: return s_planOpen && !s_hiddenAll;
	case H_RINGS: return range_rings::getMode() != 0;
	case H_CENTER: return s_hiddenAll;
	default: return s_open == h && !s_hiddenAll;
	}
}

void drawHex(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const int h = static_cast<int>(psWidget->UserData);
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const bool down = hexDown(h) || (b->getState() & WBUT_DOWN) != 0;
	drawImg(hexImg(h, down), x0, y0, psWidget->width(), psWidget->height());
	if ((b->getState() & WBUT_HIGHLIGHT) != 0)
	{
		iV_DrawImageTint(IntImages, IMAGE_RETICULE_HILIGHT, x0, y0, pal_RGBA(255, 255, 255, 255), Vector2f{psWidget->width(), psWidget->height()});
	}
}

void drawRoot(WIDGET *, UDWORD, UDWORD)
{
	// wie die WZ-Rosette: kein eigener Kasten, nur die Sechsecke
}

void drawBar(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const float s = modui::uiScale();
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	modui::wzPanel(x0, y0, x0 + psWidget->width(), y0 + psWidget->height(), s_open >= 0 ? gettext(barTitle(s_open)) : nullptr, modui::sizeClassFor(s));
}

void drawCmd(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const size_t i = static_cast<size_t>(psWidget->UserData);
	if (i >= s_items.size()) { return; }
	const Item &it = s_items[i];
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const bool down = (b->getState() & WBUT_DOWN) != 0 || (it.isOn && it.isOn());
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;
	if (it.wzUp >= 0)
	{
		// WZ-Befehlsbild (36 x 24), HD wie im WZ-Befehlsfenster
		modwzhd::drawIntfac(static_cast<unsigned>(down ? it.wzDown : it.wzUp), x0, y0, psWidget->width(), psWidget->height(), hl || down);
	}
	else
	{
		drawCmdImage(it.img, down, hl, x0, y0, psWidget->width(), psWidget->height());
	}
}

void openBar(int hex)
{
	s_open = (s_open == hex) ? -1 : hex;
	s_items = (s_open >= 0) ? itemsFor(s_open) : std::vector<Item>();
	for (auto &b : s_cmd) { s_bar->detach(b); }
	s_cmd.clear();
	for (size_t i = 0; i < s_items.size(); ++i)
	{
		auto b = std::make_shared<W_BUTTON>();
		s_bar->attach(b);
		b->displayFunction = drawCmd;
		b->UserData = static_cast<UDWORD>(i);
		b->setTip(s_items[i].tip);
		b->addOnClickHandler([i](W_BUTTON &) { widgScheduleTask([i]() {
			if (i < s_items.size() && s_items[i].action) { s_items[i].action(); }
		}); });
		s_cmd.push_back(b);
	}
}

void hexClicked(int h)
{
	if (h == H_CENTER)
	{
		s_hiddenAll = !s_hiddenAll;
		return;
	}
	// (Wunsch 26.11. 20:16 "Man muss immer zweimal auf die Rosette fuer die Befehle klicken.. warum??"): die Mitte blendet alles aus, laesst
	// den Schlachtplan aber "offen" (verborgen). Der naechste Klick auf den Generalsstern hob das Ausblenden auf UND schaltete den Plan
	// um - also zu; erst der zweite Klick oeffnete ihn. Jetzt: war alles ausgeblendet, zeigt der Klick das gewaehlte Feld (immer offen).
	const bool wasHidden = s_hiddenAll;
	s_hiddenAll = false;
	switch (h)
	{
	case H_PLAN: s_planOpen = wasHidden ? true : !s_planOpen; break;
	case H_RINGS: kf_ToggleSensorDisplay(); break;
	default:
		if (!(wasHidden && s_open == h)) { openBar(h); }   // verborgen offene Leiste nur wieder zeigen
		break;
	}
}

void ensureScreen()
{
	if (s_screen) { return; }
	auto screen = W_SCREEN::make();
	screen->psForm->hide();   // verborgene Wurzel = durchlaessig fuer Mausklicks (siehe modpaint)
	auto root = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(root);
	root->displayFunction = drawRoot;
	s_hex.clear();
	for (int p = 0; p < H_COUNT; ++p)
	{
		const int h = kHexOf[p];
		auto b = std::make_shared<W_BUTTON>();
		root->attach(b);
		b->displayFunction = drawHex;
		b->UserData = static_cast<UDWORD>(h);
		b->setTip(gettext(hexTip(h)));
		b->addOnClickHandler([h](W_BUTTON &) { widgScheduleTask([h]() { hexClicked(h); }); });
		// Trefferflaeche: (26.11. 20:16) das Sechseck des Bildes (Spitzen links/rechts, flach oben/unten) statt der Ellipse - die Ellipse
		// liess die Ecken oben und unten aus; ein Klick dorthin traf nur den Rosetten-Grund und tat nichts
		if (hexHidden(h)) { b->hide(); }
		b->setCustomHitTest([](const WIDGET *psWidget, int x, int y) -> bool {
			const float cx = psWidget->x() + psWidget->width() / 2.f, cy = psWidget->y() + psWidget->height() / 2.f;
			const float ax = psWidget->width() / 2.f, ay = psWidget->height() / 2.f;
			const float px = std::abs((static_cast<float>(x) - cx) / ax), py = std::abs((static_cast<float>(y) - cy) / ay);
			return py <= 1.f && px + 0.5f * py <= 1.f;
		});
		s_hex.push_back(b);
	}
	auto bar = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(bar);
	bar->displayFunction = drawBar;
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_root = root;
	s_bar = bar;
}

void layoutBar(int x, int y, int fw, int fh);

void layout()
{
	const float s = modui::uiScale();
	int x, y, fw, fh;
	formRect(x, y, fw, fh);
	s_root->setGeometry(x, y, fw, fh);
	for (int p = 0; p < H_COUNT; ++p)
	{
		s_hex[static_cast<size_t>(p)]->setGeometry(static_cast<int>(kHexPos[p].x * s), static_cast<int>(kHexPos[p].y * s), static_cast<int>(HEX_W * s), static_cast<int>(HEX_H * s));
	}
	layoutBar(x, y, fw, fh);
}
} // namespace

void formRect(int &x, int &y, int &fw, int &fh)
{
	const float s = modui::uiScale();
	fw = static_cast<int>(FORM_W * s); fh = static_cast<int>(FORM_H * s);
	x = static_cast<int>(screenWidth) - fw - 4;
	// (18.11. 12:10) ohne Minikarte: ueber der Knopfzeile Map/Symbol; (Wunsch 26.11. 19:32 "Das Befehlsmenue soll nicht die Rosette
	// hochschieben, sondern darueber erscheinen. Die Rosette bleibt, wo sie ist.") - der Schlachtplan verschiebt sie nicht mehr
	y = modpaint::stackTop() - static_cast<int>(21 * s) - fh - static_cast<int>(6 * s);
	const std::shared_ptr<WIDGET> radar = getRadarWidget();
	if (radar && radar->visible() && radar->height() > 0)
	{
		// (18.11. 01:14) ueber der Knopfzeile "Map"/"Symbols" (und dem Rand der gedrehten Minikarte), nicht mehr darauf
		const int m = rotateRadar ? static_cast<int>(radar->height() * 0.21f) : 0;
		const int row = static_cast<int>(18 * s) + static_cast<int>(3 * s);
		x = std::min(x, radar->x() + radar->width() - fw);
		y = radar->y() - m - row - fh - static_cast<int>(4 * s);
	}
	x = std::max(4, x); y = std::max(40, y);
}

namespace
{
void layoutBar(int x, int y, int fw, int fh)
{
	const float s = modui::uiScale();
	// Leiste links neben der Rosette, auf Hoehe der Mitte; bei vielen Knoepfen zwei Reihen
	const int n = static_cast<int>(s_cmd.size());
	const bool show = n > 0 && !s_hiddenAll;
	s_bar->show(show);
	if (!show) { return; }
	const int cols = n > 4 ? (n + 1) / 2 : n, rows = n > 4 ? 2 : 1;
	const int bw = static_cast<int>((2 * BAR_PAD + cols * CMD_W + (cols - 1) * CMD_GAP) * s);
	const int bh = static_cast<int>((2 * BAR_PAD + BAR_TITLE + rows * CMD_H + (rows - 1) * CMD_GAP) * s);
	int bx = std::max(4, x - bw - static_cast<int>(2 * s));
	int by = std::max(40, y + fh / 2 - bh / 2);
	// (26.11.) liegt dort der Schlachtplan (links neben der Rosette), kommt die Leiste ueber die Rosette
	int px0 = 0, py0 = 0, px1 = 0, py1 = 0;
	if (modpaint::planRect(px0, py0, px1, py1) && bx < px1 && bx + bw > px0 && by < py1 && by + bh > py0)
	{
		bx = std::max(4, x + fw - bw);
		by = std::max(40, y - bh - static_cast<int>(4 * s));
	}
	s_bar->setGeometry(bx, by, bw, bh);
	for (int i = 0; i < n; ++i)
	{
		const int c = i % cols, r = i / cols;
		s_cmd[static_cast<size_t>(i)]->setGeometry(static_cast<int>((BAR_PAD + c * (CMD_W + CMD_GAP)) * s), static_cast<int>((BAR_PAD + BAR_TITLE + r * (CMD_H + CMD_GAP)) * s),
		                                           static_cast<int>(CMD_W * s), static_cast<int>(CMD_H * s));
	}
}
} // namespace

namespace
{
/// (26.11.) Mod-Befehle im WZ-Befehlsfenster: nur die, die zur Auswahl passen
std::vector<Item> s_orderItems;

bool anyOf(const std::function<bool(const DROID *)> &f)
{
	for (const DROID *d : selection()) { if (f(d)) { return true; } }
	return false;
}

void drawOrderCmd(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const size_t i = static_cast<size_t>(psWidget->UserData);
	if (i >= s_orderItems.size()) { return; }
	const Item &it = s_orderItems[i];
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const bool down = (b->getState() & WBUT_DOWN) != 0 || (it.isOn && it.isOn());
	drawCmdImage(it.img, down, (b->getState() & WBUT_HIGHLIGHT) != 0, x0, y0, psWidget->width(), psWidget->height());
}
} // namespace

int addOrderButtons(const std::shared_ptr<WIDGET> &form, int x, int y, int width)
{
	s_orderItems.clear();
	if (!imagesReady() || form == nullptr) { return 0; }
	const std::vector<Item> all = itemsFor(H_INFANTRY);
	const bool infantry = anyOf([](const DROID *d) { return d->droidType == DROID_PERSON || d->isCyborg(); });
	for (const Item &it : all)
	{
		bool fits = true;
		switch (it.img)
		{
		case C_COVER: case C_LEAVE: fits = infantry; break;
		case C_SPADE: fits = infantryBuilderSelected(); break;
		case C_AUTO: fits = anyOf([](const DROID *d) { return modauto::supported(d); }); break;
		case C_MINES: fits = anyOf([](const DROID *d) { return modmines::isFlail(d) || d->droidType == DROID_PERSON || d->isCyborg(); }); break;
		case C_UNLOAD: fits = anyOf([](const DROID *d) { return modapc::isApc(d) || modrail::isLoco(d) || modrail::isWagon(d); }); break;
		case C_EJECT: fits = anyOf([](const DROID *d) { return modcombot::isModular(d) && !modcrew::crewless(d); }); break;
		default: break;   // Ausschwaermen, Bergen, Teile bergen: fuer jede Auswahl
		}
		if (fits) { s_orderItems.push_back(it); }
	}
	if (s_orderItems.empty()) { return 0; }
	// (27.11.) wie die WZ-Knopfzeilen darueber: Knoepfe 36 x 24, Abstand 4, hoechstens drei je Zeile, mittig
	constexpr int BW = CMD_W, BH = CMD_H, GAP = 4;
	const int cols = std::clamp((width + GAP) / (BW + GAP), 1, 3);
	const int n = static_cast<int>(s_orderItems.size());
	const int rows = (n + cols - 1) / cols;
	for (int i = 0; i < n; ++i)
	{
		const int c = i % cols, r = i / cols;
		const int inRow = std::min(cols, n - r * cols);
		const int rowX = x + (width - (inRow * BW + (inRow - 1) * GAP)) / 2;
		auto b = std::make_shared<W_BUTTON>();
		form->attach(b);
		b->displayFunction = drawOrderCmd;
		b->UserData = static_cast<UDWORD>(i);
		b->setTip(s_orderItems[static_cast<size_t>(i)].tip);
		b->setGeometry(rowX + c * (BW + GAP), y + r * (BH + GAP), BW, BH);
		const size_t idx = static_cast<size_t>(i);
		b->addOnClickHandler([idx](W_BUTTON &) { widgScheduleTask([idx]() {
			if (idx < s_orderItems.size() && s_orderItems[idx].action) { s_orderItems[idx].action(); }
		}); });
	}
	return rows * (BH + GAP);
}

void tick()
{
	// (18.11. 12:30) im Design-Bildschirm ausblenden - lag ueber der Vorlagenliste rechts
	const bool want = imagesReady() && !moddesigner::isOpen() && selectedPlayer < MAX_PLAYERS && intMode != INT_DESIGN;
	if (!want && !s_screen) { return; }
	ensureScreen();
	s_root->show(want);
	if (!want) { s_bar->show(false); return; }
	layout();
}

void shutdown()
{
	if (s_screen) { widgRemoveOverlayScreen(s_screen); }
	s_screen = nullptr; s_root = nullptr; s_bar = nullptr;
	s_hex.clear(); s_cmd.clear(); s_items.clear();
	s_open = -1;
	s_hiddenAll = false;   // (26.11. 20:16) neues Spiel: nichts verborgen
	if (s_img != nullptr) { iV_FreeImageFile(s_img); s_img = nullptr; }
	s_tried = false;
}

bool planOpen()
{
	return !s_screen || (s_planOpen && !s_hiddenAll);
}

bool active()
{
	return s_screen != nullptr && s_root && s_root->visible();
}
} // namespace modrosette
