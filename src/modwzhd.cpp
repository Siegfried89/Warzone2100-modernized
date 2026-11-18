// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: WZ-Knoepfe in hoher Aufloesung (siehe modwzhd.h)
#include "modwzhd.h"

#include <physfs.h>
#include <unordered_map>
#include <vector>
#include <memory>

#include "lib/framework/frame.h"
#include "lib/framework/wzstring.h"
#include "lib/framework/frameresource.h"
#include "lib/ivis_opengl/bitimage.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/textdraw.h"

namespace modwzhd
{
namespace
{
IMAGEFILE *s_hd = nullptr;
bool s_triedHd = false;
std::vector<std::string> s_intfacNames;   // Bildnummer -> Dateiname ohne ".png"
IMAGEFILE *s_intfacNamed = nullptr;       // fuer welches intfac die Liste gilt
std::unordered_map<std::string, std::unique_ptr<WzText>> s_text;

IMAGEFILE *intfac()
{
	return static_cast<IMAGEFILE *>(resGetData("IMG", "intfac.img"));
}

IMAGEFILE *hd()
{
	if (!s_triedHd)
	{
		s_triedHd = true;
		if (PHYSFS_exists("images/wzhd.img")) { s_hd = iV_LoadImageFile("images/wzhd.img"); }
	}
	return s_hd;
}

int idOf(IMAGEFILE *f, const std::string &name)
{
	if (f == nullptr) { return -1; }
	auto it = f->imageNamesMap.find(WzString::fromUtf8(name));
	if (it == f->imageNamesMap.end() || it->second == nullptr) { return -1; }
	return static_cast<int>(it->second - f->imageDefs.data());
}

const std::string &nameOf(unsigned id)
{
	static const std::string empty;
	IMAGEFILE *f = intfac();
	if (f == nullptr) { return empty; }
	if (f != s_intfacNamed)
	{
		s_intfacNamed = f;
		s_intfacNames.assign(f->imageDefs.size(), std::string());
		for (const auto &kv : f->imageNamesMap)
		{
			if (kv.second == nullptr) { continue; }
			const size_t i = static_cast<size_t>(kv.second - f->imageDefs.data());
			if (i >= s_intfacNames.size()) { continue; }
			std::string n = kv.first.toUtf8();
			const size_t slash = n.find_last_of('/');
			if (slash != std::string::npos) { n = n.substr(slash + 1); }
			if (n.size() > 4 && n.compare(n.size() - 4, 4, ".png") == 0) { n.resize(n.size() - 4); }
			s_intfacNames[i] = n;
		}
	}
	return id < s_intfacNames.size() ? s_intfacNames[id] : empty;
}

/// HD-Bild (id im HD-Paket) ins Rechteck; die glow-Fassung hat ringsum 10 Punkte Rand (bei 4-facher Groesse)
void drawHd(int hdId, int x, int y, int w, int h, bool glow, unsigned char alpha)
{
	IMAGEFILE *f = hd();
	const AtlasImageDef &d = f->imageDefs[static_cast<size_t>(hdId)];
	if (glow)
	{
		const int innerW = std::max(1, static_cast<int>(d.Width) - 20);
		const float sc = static_cast<float>(w) / static_cast<float>(innerW);
		const int pad = static_cast<int>(10.f * sc + 0.5f);
		iV_DrawImageFileAnisotropic(f, static_cast<UWORD>(hdId), x - pad, y - pad, Vector2f(static_cast<float>(w + 2 * pad), static_cast<float>(h + 2 * pad)), defaultProjectionMatrix(), alpha);
		return;
	}
	iV_DrawImageFileAnisotropic(f, static_cast<UWORD>(hdId), x, y, Vector2f(static_cast<float>(w), static_cast<float>(h)), defaultProjectionMatrix(), alpha);
}
}

bool ready()
{
	return hd() != nullptr;
}

void shutdown()
{
	if (s_hd != nullptr) { iV_FreeImageFile(s_hd); s_hd = nullptr; }
	s_triedHd = false;
	s_intfacNames.clear();
	s_intfacNamed = nullptr;
	s_text.clear();
}

int intfacWidth(unsigned intfacId)
{
	IMAGEFILE *f = intfac();
	return (f != nullptr && intfacId < f->imageDefs.size()) ? static_cast<int>(f->imageDefs[intfacId].Width) : 0;
}

int intfacHeight(unsigned intfacId)
{
	IMAGEFILE *f = intfac();
	return (f != nullptr && intfacId < f->imageDefs.size()) ? static_cast<int>(f->imageDefs[intfacId].Height) : 0;
}

void drawIntfac(unsigned intfacId, int x, int y, int w, int h, bool glow, unsigned char alpha)
{
	const std::string &n = nameOf(intfacId);
	if (!n.empty() && hd() != nullptr)
	{
		int hid = idOf(hd(), n + (glow ? "_hdglow.png" : "_hd.png"));
		if (hid < 0 && glow) { hid = idOf(hd(), n + "_hd.png"); glow = false; }
		if (hid >= 0) { drawHd(hid, x, y, w, h, glow, alpha); return; }
	}
	IMAGEFILE *f = intfac();
	if (f == nullptr || intfacId >= f->imageDefs.size()) { return; }
	iV_DrawImageFileAnisotropic(f, static_cast<UWORD>(intfacId), x, y, Vector2f(static_cast<float>(w), static_cast<float>(h)), defaultProjectionMatrix(), alpha);
}

bool drawNamed(const char *base, int x, int y, int w, int h, bool glow, unsigned char alpha)
{
	if (hd() == nullptr || base == nullptr) { return false; }
	const std::string n(base);
	int hid = idOf(hd(), n + (glow ? "_hdglow.png" : "_hd.png"));
	if (hid < 0 && glow) { hid = idOf(hd(), n + "_hd.png"); glow = false; }
	if (hid < 0) { return false; }
	drawHd(hid, x, y, w, h, glow, alpha);
	return true;
}

void textButton(int x0, int y0, int w, int h, const std::string &label, bool selected, bool hover, bool down)
{
	// WZ-Farben (aus image_des_*: Rand 103,103,163 / hell 166,186,220; Flaeche 46,50,86 / hell 63,63,111)
	const bool hi = selected || down;
	const PIELIGHT fill = hi ? pal_RGBA(63, 63, 111, 255) : pal_RGBA(46, 50, 86, 255);
	const PIELIGHT edge = hi ? pal_RGBA(166, 186, 220, 255) : pal_RGBA(103, 103, 163, 255);
	if (hover || selected)
	{
		// Lichthof wie bei den HD-Knoepfen (weich, hellblau)
		for (int g = 5; g >= 1; --g)
		{
			PIELIGHT c = pal_RGBA(140, 200, 255, static_cast<uint8_t>((selected ? 46 : 34) * (6 - g) / 5));
			pie_UniTransBoxFill(static_cast<float>(x0 - g), static_cast<float>(y0 - g), static_cast<float>(x0 + w + g), static_cast<float>(y0 + h + g), c);
		}
	}
	pie_UniTransBoxFill(static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x0 + w), static_cast<float>(y0 + h), fill);
	// dezenter Glanz in der oberen Haelfte
	pie_UniTransBoxFill(static_cast<float>(x0 + 1), static_cast<float>(y0 + 1), static_cast<float>(x0 + w - 1), static_cast<float>(y0 + h / 2), pal_RGBA(255, 255, 255, 18));
	// abgerundeter Rahmen: Ecken frei lassen wie bei den WZ-Bildern
	pie_UniTransBoxFill(static_cast<float>(x0 + 2), static_cast<float>(y0), static_cast<float>(x0 + w - 2), static_cast<float>(y0 + 1), edge);
	pie_UniTransBoxFill(static_cast<float>(x0 + 2), static_cast<float>(y0 + h - 1), static_cast<float>(x0 + w - 2), static_cast<float>(y0 + h), edge);
	pie_UniTransBoxFill(static_cast<float>(x0), static_cast<float>(y0 + 2), static_cast<float>(x0 + 1), static_cast<float>(y0 + h - 2), edge);
	pie_UniTransBoxFill(static_cast<float>(x0 + w - 1), static_cast<float>(y0 + 2), static_cast<float>(x0 + w), static_cast<float>(y0 + h - 2), edge);
	pie_UniTransBoxFill(static_cast<float>(x0 + 1), static_cast<float>(y0 + 1), static_cast<float>(x0 + 2), static_cast<float>(y0 + 2), edge);
	pie_UniTransBoxFill(static_cast<float>(x0 + w - 2), static_cast<float>(y0 + 1), static_cast<float>(x0 + w - 1), static_cast<float>(y0 + 2), edge);
	pie_UniTransBoxFill(static_cast<float>(x0 + 1), static_cast<float>(y0 + h - 2), static_cast<float>(x0 + 2), static_cast<float>(y0 + h - 1), edge);
	pie_UniTransBoxFill(static_cast<float>(x0 + w - 2), static_cast<float>(y0 + h - 2), static_cast<float>(x0 + w - 1), static_cast<float>(y0 + h - 1), edge);
	if (label.empty()) { return; }
	auto &t = s_text[label];
	if (!t) { t = std::make_unique<WzText>(WzString::fromUtf8(label), font_regular); }
	const int tx = x0 + (w - t->width()) / 2;
	const int ty = y0 + (h - t->lineSize()) / 2 - t->aboveBase();
	t->render(tx, ty, hi ? pal_RGBA(235, 240, 255, 255) : pal_RGBA(190, 196, 236, 255));
}
}
