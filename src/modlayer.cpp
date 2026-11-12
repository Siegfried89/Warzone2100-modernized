// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Drei Ebenen-Knoepfe (Orbit / Oberflaeche / Untergrund) mit weicher Ueberblendung, siehe modlayer.h

#include "modlayer.h"
#include "modui.h"
#include "modcombot.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/button.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"

#include "hci.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace modlayer
{

namespace
{

constexpr int BTN_W = 78;
constexpr int BTN_H = 22;
constexpr int BTN_GAP = 3;
constexpr int PAD = 3;
constexpr int TOTAL_W = BTN_W + 2 * PAD;
constexpr int TOTAL_H = 3 * BTN_H + 2 * BTN_GAP + 2 * PAD;
constexpr UDWORD FADE_TOTAL_MS = 700;   // erste Haelfte ausblenden, in der Mitte umschalten, zweite Haelfte einblenden

std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_root;
std::vector<std::shared_ptr<W_BUTTON>> s_buttons;
bool s_suppressed = false;

Layer s_current = SURFACE;   // was gerade angezeigt wird
Layer s_target = SURFACE;    // wohin gewechselt wird
bool s_busy = false;
UDWORD s_start = 0;

const char *layerName(Layer l)
{
	switch (l)
	{
	case ORBIT: return "ORBIT";
	case SURFACE: return "SURFACE";
	default: return "UNDERGROUND";
	}
}

const char *layerTip(Layer l)
{
	switch (l)
	{
	case ORBIT: return "Orbit layer: floating asteroids and solar panels, only aerial units travel here (view only for now)";
	case SURFACE: return "Surface layer: the battlefield (view only for now)";
	default: return "Underground layer: lava caverns and drilled tunnels (view only for now)";
	}
}

PIELIGHT veilColour(Layer l, unsigned char a)
{
	switch (l)
	{
	case ORBIT: return pal_RGBA(2, 8, 28, a);
	case UNDERGROUND: return pal_RGBA(24, 10, 2, a);
	default: return pal_RGBA(0, 0, 0, a);
	}
}

// Fortschritt der Ueberblendung 0..1 (1 = fertig)
float progress()
{
	if (!s_busy) { return 1.f; }
	const UDWORD el = realTime - s_start;
	return std::min(1.f, static_cast<float>(el) / static_cast<float>(FADE_TOTAL_MS));
}

void update()
{
	if (!s_busy) { return; }
	const float p = progress();
	if (p >= 0.5f) { s_current = s_target; }
	if (p >= 1.f) { s_busy = false; }
}

// Schleier ueber das ganze Bild: Dauer-Farbton der Ebene + Ueberblendung beim Wechsel
void drawVeil()
{
	const int w = static_cast<int>(screenWidth), h = static_cast<int>(screenHeight);
	if (s_current == ORBIT) { pie_BoxFill_alpha(0, 0, w, h, pal_RGBA(10, 30, 90, 50)); }
	else if (s_current == UNDERGROUND) { pie_BoxFill_alpha(0, 0, w, h, pal_RGBA(50, 20, 0, 70)); }
	if (s_busy)
	{
		const float p = progress();
		const float k = p < 0.5f ? p * 2.f : (1.f - p) * 2.f;
		const int a = static_cast<int>(k * 235.f);
		if (a > 0) { pie_BoxFill_alpha(0, 0, w, h, veilColour(s_target, static_cast<unsigned char>(a))); }
	}
}

void drawRoot(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	update();
	drawVeil();
	const int x0 = static_cast<int>(xOffset) + psWidget->x();
	const int y0 = static_cast<int>(yOffset) + psWidget->y();
	const float s = modui::uiScale();
	pie_BoxFill_alpha(x0, y0, x0 + static_cast<int>(TOTAL_W * s), y0 + static_cast<int>(TOTAL_H * s), pal_RGBA(4, 10, 24, 150));
	iV_Box(x0, y0, x0 + static_cast<int>(TOTAL_W * s) - 1, y0 + static_cast<int>(TOTAL_H * s) - 1, pal_RGBA(70, 110, 150, 200));
}

void drawButton(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const float s = modui::uiScale();
	const Layer l = static_cast<Layer>(psWidget->UserData);
	const int x0 = static_cast<int>(xOffset) + psWidget->x();
	const int y0 = static_cast<int>(yOffset) + psWidget->y();
	const int w = static_cast<int>(BTN_W * s), h = static_cast<int>(BTN_H * s);
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const bool active = (s_busy ? s_target : s_current) == l;
	const bool down = (b->getState() & WBUT_DOWN) != 0;
	const bool hl = (b->getState() & WBUT_HIGHLIGHT) != 0;

	PIELIGHT fill = active ? pal_RGBA(20, 90, 120, 230) : pal_RGBA(16, 28, 48, 200);
	if (down) { fill = pal_RGBA(30, 120, 150, 240); }
	pie_BoxFill_alpha(x0, y0, x0 + w, y0 + h, fill);
	iV_Box(x0, y0, x0 + w - 1, y0 + h - 1, active ? modui::cyan(240) : pal_RGBA(60, 90, 120, 220));
	if (hl) { iV_Box(x0 + 1, y0 + 1, x0 + w - 2, y0 + h - 2, pal_RGBA(255, 255, 255, 150)); }

	const int sc = modui::sizeClassFor(s);
	const std::string label = layerName(l);
	const int tw = modui::textWidth(label, sc);
	const PIELIGHT col = active ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(150, 185, 215, 255);
	modui::text(label, x0 + (w - tw) / 2, y0 + (h - 9 * static_cast<int>(s)) / 2 - 1, col, sc);
}

void ensureScreen()
{
	if (s_screen) { return; }
	auto screen = W_SCREEN::make();
	auto root = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(root);
	root->displayFunction = drawRoot;
	s_buttons.clear();
	// von oben nach unten: Orbit, Oberflaeche, Untergrund
	const Layer order[3] = { ORBIT, SURFACE, UNDERGROUND };
	for (const Layer l : order)
	{
		auto b = std::make_shared<W_BUTTON>();
		root->attach(b);
		b->displayFunction = drawButton;
		b->UserData = static_cast<UDWORD>(l);
		b->setTip(layerTip(l));
		b->addOnClickHandler([l](W_BUTTON &) { widgScheduleTask([l]() { request(l); }); });
		s_buttons.push_back(b);
	}
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_root = root;
}

void layout()
{
	const float s = modui::uiScale();
	const int w = static_cast<int>(TOTAL_W * s), h = static_cast<int>(TOTAL_H * s);
	s_root->setGeometry(static_cast<int>(screenWidth) - w - 4, 44, w, h);
	for (size_t i = 0; i < s_buttons.size(); ++i)
	{
		s_buttons[i]->setGeometry(static_cast<int>(PAD * s), static_cast<int>((PAD + static_cast<int>(i) * (BTN_H + BTN_GAP)) * s),
		                          static_cast<int>(BTN_W * s), static_cast<int>(BTN_H * s));
	}
}

} // namespace

Layer current()
{
	return s_current;
}

void request(Layer target)
{
	if (s_busy || target == s_current) { return; }
	s_target = target;
	s_start = realTime;
	s_busy = true;
}

void setSuppressed(bool suppressed)
{
	s_suppressed = suppressed;
}

void tick()
{
	if (!modcombot::ready()) { return; }   // nur wenn die Combot-Mod geladen ist
	const bool want = !s_suppressed;
	if (!want && !s_screen) { return; }
	ensureScreen();
	if (!s_root) { return; }
	s_root->show(want);
	if (want) { layout(); }
}

void shutdown()
{
	if (s_screen)
	{
		widgRemoveOverlayScreen(s_screen);
		s_screen = nullptr;
	}
	s_root = nullptr;
	s_buttons.clear();
	s_current = s_target = SURFACE;
	s_busy = false;
}

}
