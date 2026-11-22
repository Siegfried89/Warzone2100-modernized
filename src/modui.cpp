// SPDX-License-Identifier: GPL-2.0-or-later
#include "modui.h"

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/bitimage.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/ivis_opengl/textdraw.h"

#include <cmath>
#include <memory>
#include <unordered_map>

namespace modui
{

namespace
{
IMAGEFILE *s_img = nullptr;
bool s_tried = false;
std::unordered_map<std::string, std::unique_ptr<WzText>> s_text;

iV_fonts fontOf(int sizeClass)
{
	return sizeClass <= 0 ? font_small : (sizeClass == 1 ? font_regular : font_large);
}

WzText &textObj(const std::string &s, iV_fonts f)
{
	const std::string key = std::to_string(static_cast<int>(f)) + "|" + s;
	auto it = s_text.find(key);
	if (it == s_text.end())
	{
		if (s_text.size() > 500) { s_text.clear(); }
		it = s_text.emplace(key, std::make_unique<WzText>(WzString::fromUtf8(s), f)).first;
	}
	return *it->second;
}
}

bool ready()
{
	if (!s_tried)
	{
		s_tried = true;
		s_img = iV_LoadImageFile("images/mfui.img");
	}
	return s_img != nullptr;
}

void shutdown()
{
	s_text.clear();
	if (s_img != nullptr)
	{
		iV_FreeImageFile(s_img);
		s_img = nullptr;
	}
	s_tried = false;
}

bool has(Img id)
{
	return ready() && static_cast<size_t>(id) < s_img->numImages();
}

int imgWidth(Img id)
{
	return ready() ? static_cast<int>(iV_GetImageWidth(s_img, static_cast<UWORD>(id))) : 0;
}

int imgHeight(Img id)
{
	return ready() ? static_cast<int>(iV_GetImageHeight(s_img, static_cast<UWORD>(id))) : 0;
}

void drawSized(Img id, int x, int y, int w, int h, unsigned char alpha)
{
	if (!ready()) { return; }
	iV_DrawImageFileAnisotropic(s_img, static_cast<UWORD>(id), x, y, Vector2f(static_cast<float>(w), static_cast<float>(h)), defaultProjectionMatrix(), alpha);
}

void draw(Img id, int x, int y, float scale, unsigned char alpha)
{
	if (!ready()) { return; }
	drawSized(id, x, y, static_cast<int>(std::lround(imgWidth(id) * scale)), static_cast<int>(std::lround(imgHeight(id) * scale)), alpha);
}

float uiScale()
{
	return screenHeight >= 1200 ? 1.5f : 1.f;
}

void text(const std::string &s, int x, int top, PIELIGHT colour, int sizeClass)
{
	if (s.empty()) { return; }
	WzText &t = textObj(s, fontOf(sizeClass));
	t.render(static_cast<float>(x), static_cast<float>(top - t.aboveBase()), colour);
}

int textWidth(const std::string &s, int sizeClass)
{
	if (s.empty()) { return 0; }
	return textObj(s, fontOf(sizeClass)).width();
}

int sizeClassFor(float scale)
{
	return scale < 1.45f ? 0 : (scale < 2.3f ? 1 : 2);
}

std::string fit(const std::string &s, int maxW, int sizeClass)
{
	std::string r = s;
	while (!r.empty() && textWidth(r, sizeClass) > maxW) { r.pop_back(); }
	return r;
}

PIELIGHT cyan(unsigned char a) { return pal_RGBA(40, 235, 255, a); }

PIELIGHT healthColour(int pct)
{
	if (pct > 66) { return pal_RGBA(40, 235, 255, 255); }
	if (pct > 33) { return pal_RGBA(255, 220, 40, 255); }
	return pal_RGBA(255, 60, 40, 255);
}

void wzPanel(int x0, int y0, int x1, int y1, const char *title, int sizeClass)
{
	// wie WZ-Formulare: dunkelblaue Flaeche, heller Aussenrand, dunkler Innenrand
	PIELIGHT bg = WZCOL_MENU_BACKGROUND;
	bg.byte.a = 210;
	pie_UniTransBoxFill(x0, y0, x1, y1, bg);
	iV_Box(x0, y0, x1 - 1, y1 - 1, WZCOL_MENU_BORDER);
	iV_Box(x0 + 1, y0 + 1, x1 - 2, y1 - 2, WZCOL_FORM_DARK);
	if (title != nullptr)
	{
		text(title, x0 + 5, y0 + 2, WZCOL_TEXT_MEDIUM, sizeClass);
	}
}

void wzButton(int x0, int y0, int w, int h, const std::string &label, bool active, bool hover, bool down, int sizeClass, const PIELIGHT *accent)
{
	const int x1 = x0 + w, y1 = y0 + h;
	// (Wunsch 22.11. 15:47 "Der Hintergrund ist richtig schlecht mit dem grau, der Kontrast ist nicht gut"): statt grauer Flaeche mit
	// blauer Schrift jetzt dunkles Marineblau (wie die WZ-Formulare) mit heller, fast weisser Schrift; aktiv = kraeftiges Blau
	PIELIGHT fill = active ? pal_RGBA(34, 62, 150, 240) : pal_RGBA(12, 18, 62, 230);
	if (hover && !active) { fill = pal_RGBA(24, 38, 104, 238); }
	if (down) { fill = pal_RGBA(52, 92, 196, 245); }
	pie_UniTransBoxFill(x0, y0, x1, y1, fill);
	iV_Box(x0, y0, x1 - 1, y1 - 1, active ? pal_RGBA(150, 200, 255, 255) : (hover ? pal_RGBA(120, 170, 255, 255) : pal_RGBA(58, 84, 172, 255)));
	if (accent != nullptr)
	{
		pie_BoxFill(x0 + 2, y0 + 3, x0 + 5, y1 - 3, *accent);   // kleine Farbmarke (Art des Befehls)
	}
	const int tw = textWidth(label, sizeClass);
	const int textX = x0 + (accent ? 6 : 0) + std::max(0, (w - (accent ? 6 : 0) - tw) / 2);
	const int fh = (sizeClass <= 0) ? 9 : (sizeClass == 1 ? 12 : 16);
	text(label, textX, y0 + (h - fh) / 2 - 1, buttonText(active || hover), sizeClass);
}

PIELIGHT buttonText(bool bright)
{
	return bright ? pal_RGBA(255, 255, 255, 255) : pal_RGBA(214, 224, 246, 255);
}

void disc(int cx, int cy, int r, PIELIGHT colour)
{
	for (int dy = -r; dy <= r; ++dy)
	{
		const int hw = static_cast<int>(std::sqrt(static_cast<double>(r * r - dy * dy)));
		pie_UniTransBoxFill(cx - hw, cy + dy, cx + hw + 1, cy + dy + 1, colour);
	}
}

void ring(int cx, int cy, int r, PIELIGHT colour)
{
	const int n = 28;
	for (int i = 0; i < n; ++i)
	{
		const float a0 = i * 6.2831853f / n, a1 = (i + 1) * 6.2831853f / n;
		iV_Line(cx + static_cast<int>(r * std::cos(a0)), cy + static_cast<int>(r * std::sin(a0)),
		        cx + static_cast<int>(r * std::cos(a1)), cy + static_cast<int>(r * std::sin(a1)), colour);
	}
}

}
