// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: animierte Fraktionssymbole (siehe modfaction.h)

#include "modfaction.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"
#include "lib/framework/frameresource.h"
#include "lib/framework/physfs_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/bitimage.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/sound/audio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace modfaction
{
namespace
{
constexpr int NUM = 4;
constexpr unsigned SPLASH_MS = 3200;

struct Anim
{
	int frames = 0;        // Bilder (S und L); X hat (frames + 1) / 2
	int ms = 80;           // Takt
	int hold = 0;          // Halten des Endbilds nach dem Ablauf (0 = Schleife)
	bool pingpong = false; // Schleife vor/zurueck
	int base[3] = { 0, 0, 0 };   // erste Bildnummer je Groesse in der Ladeliste
};

IMAGEFILE *s_img = nullptr;
bool s_tried = false;
Anim s_anim[NUM];
int s_splashFaction = -1;
unsigned s_splashStart = 0;
int s_track[NUM] = { -2, -2, -2, -2 };

bool load()
{
	if (s_tried) { return s_img != nullptr; }
	s_tried = true;
	char *buf = nullptr;
	UDWORD size = 0;
	if (!PHYSFS_exists("images/factanim.txt") || !PHYSFS_exists("images/factanim.img")) { return false; }
	if (!loadFile("images/factanim.txt", &buf, &size, false) || buf == nullptr) { return false; }
	std::string txt(buf, size);
	free(buf);
	int lineStart = 0, idx = 0;
	bool ok = true;
	while (lineStart < static_cast<int>(txt.size()))
	{
		size_t e = txt.find('\n', static_cast<size_t>(lineStart));
		if (e == std::string::npos) { e = txt.size(); }
		const std::string line = txt.substr(static_cast<size_t>(lineStart), e - static_cast<size_t>(lineStart));
		lineStart = static_cast<int>(e) + 1;
		int id = -1, n = 0, ms = 80, hold = 0, pp = 0;
		if (sscanf(line.c_str(), "%d %d %d %d %d", &id, &n, &ms, &hold, &pp) != 5) { continue; }
		if (id < 0 || id >= NUM || n <= 0 || n > 400) { ok = false; break; }
		Anim &a = s_anim[id];
		a.frames = n; a.ms = std::max(20, ms); a.hold = std::max(0, hold); a.pingpong = pp != 0;
		a.base[SIZE_S] = idx; idx += n;
		a.base[SIZE_L] = idx; idx += n;
		a.base[SIZE_X] = idx; idx += (n + 1) / 2;
	}
	if (!ok || idx == 0) { return false; }
	s_img = iV_LoadImageFile("images/factanim.img");
	if (s_img != nullptr && static_cast<int>(s_img->imageDefs.size()) < idx)
	{
		debug(LOG_ERROR, "factanim: %d Bilder erwartet, %d geladen", idx, static_cast<int>(s_img->imageDefs.size()));
		iV_FreeImageFile(s_img);
		s_img = nullptr;
	}
	return s_img != nullptr;
}

int frameAt(const Anim &a, unsigned t, bool animate)
{
	const int n = a.frames;
	if (n <= 1) { return 0; }
	if (!animate) { return (a.hold > 0) ? n - 1 : 0; }
	if (a.pingpong)
	{
		const int period = 2 * n - 2;
		const int k = static_cast<int>((t / static_cast<unsigned>(a.ms)) % static_cast<unsigned>(period));
		return k < n ? k : period - k;
	}
	if (a.hold > 0)
	{
		const unsigned cycle = static_cast<unsigned>(n * a.ms + a.hold);
		const unsigned tt = t % cycle;
		return std::min(n - 1, static_cast<int>(tt / static_cast<unsigned>(a.ms)));
	}
	return static_cast<int>((t / static_cast<unsigned>(a.ms)) % static_cast<unsigned>(n));
}
} // namespace

bool ready()
{
	return load();
}

bool draw(int faction, Size size, int x, int y, int w, int h, unsigned elapsedMs, bool animate, uint8_t alpha)
{
	if (faction < 0 || faction >= NUM || !load()) { return false; }
	const Anim &a = s_anim[faction];
	if (a.frames <= 0) { return false; }
	int k = frameAt(a, elapsedMs, animate);
	if (size == SIZE_X) { k /= 2; }
	const int id = a.base[size] + k;
	if (id < 0 || id >= static_cast<int>(s_img->imageDefs.size())) { return false; }
	iV_DrawImageFileAnisotropic(s_img, static_cast<UWORD>(id), x, y, Vector2f(static_cast<float>(w), static_cast<float>(h)), defaultProjectionMatrix(), alpha);
	return true;
}

void splash(int faction)
{
	if (faction < 0 || faction >= NUM) { return; }
	s_splashFaction = faction;
	s_splashStart = realTime;
	if (s_track[faction] == -2)
	{
		char fn[32];
		snprintf(fn, sizeof(fn), "faction_%d.ogg", faction);
		s_track[faction] = audio_GetTrackID(fn);
	}
	if (s_track[faction] >= 0)
	{
		audio_PlayTrack(s_track[faction]);
	}
}

void drawSplash()
{
	if (s_splashFaction < 0) { return; }
	const unsigned t = realTime - s_splashStart;
	if (t >= SPLASH_MS || !load())
	{
		s_splashFaction = -1;
		return;
	}
	// einblenden 0,25 s, ausblenden die letzten 0,7 s
	float al = 1.0f;
	if (t < 250) { al = static_cast<float>(t) / 250.0f; }
	else if (t > SPLASH_MS - 700) { al = static_cast<float>(SPLASH_MS - t) / 700.0f; }
	const int sz = std::min(static_cast<int>(screenHeight) * 2 / 5, 320);
	const int x = (static_cast<int>(screenWidth) - sz) / 2;
	const int y = (static_cast<int>(screenHeight) - sz) / 2;
	// dunkler weicher Hintergrund, damit das Symbol vor dem Menue steht
	pie_UniTransBoxFill(static_cast<float>(x - 16), static_cast<float>(y - 16), static_cast<float>(x + sz + 16), static_cast<float>(y + sz + 16),
	                    pal_RGBA(0, 0, 0, static_cast<uint8_t>(120.0f * al)));
	draw(s_splashFaction, SIZE_X, x, y, sz, sz, t, true, static_cast<uint8_t>(255.0f * al));
}

void shutdown()
{
	if (s_img != nullptr)
	{
		iV_FreeImageFile(s_img);
		s_img = nullptr;
	}
	s_tried = false;
	for (int &t : s_track) { t = -2; }
}
} // namespace modfaction
