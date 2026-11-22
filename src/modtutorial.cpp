// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Tutorial (siehe modtutorial.h)
#include "modtutorial.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "lib/framework/frame.h"
#include "lib/framework/wzapp.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "lib/widget/button.h"
#include "lib/widget/form.h"
#include "lib/widget/widget.h"

#include "challenge.h"
#include "game_world.h"
#include "hci.h"
#include "modgrid.h"
#include "modlayer.h"
#include "modpaint.h"
#include "modtunnel.h"
#include "modui.h"
#include "multiplay.h"
#include "objmem.h"
#include "structure.h"

namespace modtutorial
{
namespace
{
struct Step
{
	const char *title;
	const char *text;
	bool (*done)(unsigned p);
};

int countStruct(unsigned p, bool (*pred)(const STRUCTURE *), bool builtOnly = true)
{
	int n = 0;
	for (const STRUCTURE *s : gameWorld.objects.structures[p])
	{
		if (s->died || (builtOnly && s->status != SS_BUILT)) { continue; }
		if (pred(s)) { n++; }
	}
	return n;
}

bool idHas(const STRUCTURE *s, const char *part) { return s->pStructureType != nullptr && s->pStructureType->id.toStdString().find(part) != std::string::npos; }

unsigned s_startDroids = 0;

const Step kSteps[] = {
	{ "Willkommen", "Ziehe mit der linken Maustaste einen Rahmen um deine Lastwagen, um sie auszuwählen. Kamera: Pfeiltasten, Mausrad zoomt, mittlere Maustaste gedrückt halten dreht.",
	  [](unsigned p) { for (const DROID *d : gameWorld.objects.droids[p]) { if (d->selected) { return true; } } return false; } },
	{ "Öl fördern", "Baue mit einem Lastwagen einen Bohrturm auf einer Ölquelle (schwarzer Fleck am Boden). Taste F öffnet das Baumenü.",
	  [](unsigned p) { return countStruct(p, [](const STRUCTURE *s) { return s->pStructureType->type == REF_RESOURCE_EXTRACTOR; }) > 0; } },
	{ "Raffinerie", "Rohöl muss veredelt werden: Baue eine Raffinerie. Lastwagen bringen die Fässer vom Bohrturm dorthin.",
	  [](unsigned p) { return countStruct(p, [](const STRUCTURE *s) { return idHas(s, "Refinery"); }) > 0; } },
	{ "Strom", "Baue ein Kraftwerk und ein Umspannwerk. Strommasten verbinden alles; Gebäude im blauen Kreis haben Strom.",
	  [](unsigned p) { return countStruct(p, [](const STRUCTURE *s) { return s->pStructureType->type == REF_POWER_GEN; }) > 0
	                          && countStruct(p, [](const STRUCTURE *s) { return modgrid::isSubstation(s->pStructureType); }) > 0; } },
	{ "Produktion", "Wähle die Fabrik und gib eine Einheit in Auftrag. Die Kommandozentrale liefert etwas Notstrom, auch ohne Öl.",
	  [](unsigned p) { return static_cast<unsigned>(gameWorld.objects.droids[p].size()) > s_startDroids; } },
	{ "Befestigen", "Baue einen Graben, eine Sandsackmauer oder eine Stellung. Bagger graben schneller; Infanterie geht darin in Deckung.",
	  [](unsigned p) { return countStruct(p, [](const STRUCTURE *s) { return idHas(s, "Trench") || idHas(s, "Sandbag") || idHas(s, "Pit"); }) > 0; } },
	{ "Schlachtplan", "Öffne den Schlachtplan und ziehe einen Bereitstellungsraum (\"Staging Zone\"): Neue Einheiten sammeln sich dort.",
	  [](unsigned) { return modpaint::planHasElements(); } },
	{ "Tunnel", "Baue ein Bohrfahrzeug und gib ihm im Baumenü den Auftrag \"Tunnel Mouth\" am Ziel. Es gräbt sich unterirdisch dorthin.",
	  [](unsigned p) { return !modtunnel::tunnels(p).empty(); } },
	{ "Untergrund", "Baue einen Schacht und schicke eine Einheit hinein (Rechtsklick auf den Schacht). Unten bohrt das Bohrfahrzeug Gänge.",
	  [](unsigned p) { for (const DROID *d : gameWorld.objects.droids[p]) { if (!d->died && modlayer::layerOf(d) == modlayer::UNDERGROUND) { return true; } } return false; } },
	{ "Eisenbahn", "Ziehe im Schlachtplan mit \"Railway\" eine Gleisstrecke (entlang der alten Bahnlinie) und baue einen Lokschuppen.",
	  [](unsigned p) { return countStruct(p, [](const STRUCTURE *s) { return idHas(s, "A0RailTrack"); }) >= 6 && countStruct(p, [](const STRUCTURE *s) { return idHas(s, "RailDepot"); }) > 0; } },
	{ "Brücke", "Überbrücke den Kanal westlich der Oase: Brücke im Baumenü wählen und wie eine Mauer über das Wasser ziehen.",
	  [](unsigned p) { return countStruct(p, [](const STRUCTURE *s) { return idHas(s, "A0Bridge") || idHas(s, "RailTrackBridge"); }) > 0; } },
};
constexpr int NSTEPS = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));

bool s_done[NSTEPS] = {};
bool s_active = false, s_hidden = false, s_init = false;
unsigned s_flashUntil = 0;
int s_flashStep = -1;
std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_panel;
std::shared_ptr<W_BUTTON> s_next, s_hide;
constexpr int PANEL_W = 330, PANEL_H = 128, PAD = 8;

int currentStep()
{
	for (int i = 0; i < NSTEPS; ++i) { if (!s_done[i]) { return i; } }
	return NSTEPS;
}

std::vector<std::string> wrap(const std::string &s, int maxW)
{
	std::vector<std::string> lines;
	std::string cur, word;
	auto flush = [&]() {
		const std::string tryLine = cur.empty() ? word : cur + " " + word;
		if (!cur.empty() && modui::textWidth(tryLine, 0) > maxW) { lines.push_back(cur); cur = word; }
		else { cur = tryLine; }
		word.clear();
	};
	for (char c : s)
	{
		if (c == ' ') { if (!word.empty()) { flush(); } }
		else { word += c; }
	}
	if (!word.empty()) { flush(); }
	if (!cur.empty()) { lines.push_back(cur); }
	return lines;
}

void drawPanel(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	if (!s_active || s_hidden) { return; }
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const int cur = currentStep();
	char title[96];
	if (cur >= NSTEPS) { snprintf(title, sizeof(title), "Tutorial geschafft!"); }
	else { snprintf(title, sizeof(title), "Tutorial %d/%d: %s", cur + 1, NSTEPS, kSteps[cur].title); }
	modui::wzPanel(x0, y0, x0 + w, y0 + h, title, 0);
	int ty = y0 + 24;
	const std::string body = cur >= NSTEPS ? std::string("Du kennst jetzt alle Mod-Systeme: Ölkreislauf, Strom, Befestigung, Schlachtplan, Tunnel, Untergrund, Züge und Brücken. Viel Spaß!") : std::string(kSteps[cur].text);
	for (const std::string &l : wrap(body, w - 2 * PAD))
	{
		modui::text(l, x0 + PAD, ty, WZCOL_TEXT_BRIGHT, 0);
		ty += 14;
	}
	// Fortschritt: ein Punkt je Schritt (gruen = erledigt, gelb = aktuell)
	const int dy = y0 + h - 14;
	for (int i = 0; i < NSTEPS; ++i)
	{
		const PIELIGHT c = s_done[i] ? pal_RGBA(70, 220, 90, 255) : (i == cur ? pal_RGBA(255, 210, 80, 255) : pal_RGBA(90, 100, 130, 255));
		modui::disc(x0 + PAD + 4 + i * 12, dy + 4, 4, c);
	}
	if (s_flashStep >= 0 && realTime < s_flashUntil)
	{
		const std::string ok = std::string("Geschafft: ") + kSteps[s_flashStep].title;
		modui::text(ok, x0 + w - PAD - modui::textWidth(ok, 0), dy - 2, pal_RGBA(120, 240, 140, 255), 0);
	}
}

void drawBtn(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0, down = (b->getState() & WBUT_DOWN) != 0;
	const bool on = psWidget->UserData == 1 && !s_hidden;
	modui::wzButton(x0, y0, psWidget->width(), psWidget->height(), psWidget->UserData == 1 ? std::string("?") : std::string("Weiter"), on, hl, down, 0, nullptr);
}

void ensureScreen()
{
	if (s_screen) { return; }
	auto screen = W_SCREEN::make();
	screen->psForm->hide();
	auto panel = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(panel);
	panel->displayFunction = drawPanel;
	auto next = std::make_shared<W_BUTTON>();
	panel->attach(next);
	next->displayFunction = drawBtn;
	next->UserData = 0;
	next->setTip("Diesen Schritt überspringen");
	next->addOnClickHandler([](W_BUTTON &) { const int c = currentStep(); if (c < NSTEPS) { s_done[c] = true; } });
	auto hide = std::make_shared<W_BUTTON>();
	screen->psForm->attach(hide);
	hide->displayFunction = drawBtn;
	hide->UserData = 1;
	hide->setTip("Tutorial ein-/ausblenden");
	hide->addOnClickHandler([](W_BUTTON &) { s_hidden = !s_hidden; });
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen; s_panel = panel; s_next = next; s_hide = hide;
}
}

void tick()
{
	const bool want = challengeActive && selectedPlayer < MAX_PLAYERS && std::strstr(game.map, "Wuestenland") != nullptr;
	if (!want)
	{
		if (s_screen) { shutdown(); }
		return;
	}
	if (!s_init)
	{
		s_init = true;
		for (bool &d : s_done) { d = false; }
		s_startDroids = static_cast<unsigned>(gameWorld.objects.droids[selectedPlayer].size());
	}
	s_active = true;
	ensureScreen();
	const int x = 8, y = 120;
	s_hide->setGeometry(x, y, 18, 18);
	s_panel->setGeometry(x + 22, y, PANEL_W, PANEL_H);
	s_next->setGeometry(PANEL_W - 64, 4, 56, 16);
	s_panel->show(!s_hidden);
	s_next->show(currentStep() < NSTEPS);
	// Schritte pruefen (erledigt bleibt erledigt)
	for (int i = 0; i < NSTEPS; ++i)
	{
		if (!s_done[i] && kSteps[i].done(selectedPlayer))
		{
			s_done[i] = true;
			s_flashStep = i;
			s_flashUntil = realTime + 4000;
			audio_PlayTrack(ID_SOUND_OBJECTIVE_ACCOMPLISHED);
		}
	}
}

void shutdown()
{
	if (s_screen) { widgRemoveOverlayScreen(s_screen); }
	s_screen = nullptr; s_panel = nullptr; s_next = nullptr; s_hide = nullptr;
	s_active = false; s_init = false; s_flashStep = -1;
}
}
