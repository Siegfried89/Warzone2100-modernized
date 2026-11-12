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

void disc(int cx, int cy, int r, PIELIGHT colour)
{
	for (int dy = -r; dy <= r; ++dy)
	{
		const int hw = static_cast<int>(std::sqrt(static_cast<double>(r * r - dy * dy)));
		pie_BoxFill_alpha(cx - hw, cy + dy, cx + hw + 1, cy + dy + 1, colour);
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
