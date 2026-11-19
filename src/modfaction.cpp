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
#include "lib/sound/tracklib.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace modfaction
{
namespace
{
constexpr int NUM = 5;   // MOD: mit Project
constexpr unsigned SPLASH_MS = 3200;

struct Anim
{
	int frames = 0;        // Bilder (S und L); X hat (frames + xstep - 1) / xstep
	int xstep = 2;         // MOD (17.11.): X jedes xstep-te Bild (6. Wert in factanim.txt, fehlt er: 2 wie bisher)
	int ms = 80;           // Takt
	int hold = 0;          // Halten des Endbilds nach dem Ablauf (0 = Schleife)
	bool pingpong = false; // Schleife vor/zurueck
	int loop0 = 0;         // MOD (19.11.): Menue-Schleife ab diesem Bild (Einschalten aus dem Rauschen nur beim ersten Durchlauf)
	int splashN = 0;       // MOD (19.11.): das grosse Symbol spielt die Bilder 0..splashN-1 (0 = alle)
	int base[3] = { 0, 0, 0 };   // erste Bildnummer je Groesse in der Ladeliste
};

IMAGEFILE *s_img = nullptr;
bool s_tried = false;
Anim s_anim[NUM];
int s_splashFaction = -1;
unsigned s_splashStart = 0;
int s_track[NUM] = { -2, -2, -2, -2, -2 };
AUDIO_STREAM *s_stream = nullptr;   // MOD (19.11.): laufender Fraktions-Klang (wird bei neuem Klick / Abbruch gestoppt)

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
		int id = -1, n = 0, ms = 80, hold = 0, pp = 0, xs = 2, l0 = 0, sn = 0;
		if (sscanf(line.c_str(), "%d %d %d %d %d %d %d %d", &id, &n, &ms, &hold, &pp, &xs, &l0, &sn) < 5) { continue; }
		if (id < 0 || id >= NUM || n <= 0 || n > 400) { ok = false; break; }
		Anim &a = s_anim[id];
		a.frames = n; a.ms = std::max(20, ms); a.hold = std::max(0, hold); a.pingpong = pp != 0; a.xstep = std::clamp(xs, 1, 4);
		a.loop0 = std::clamp(l0, 0, n - 1); a.splashN = (sn > 0) ? std::min(sn, n) : n;
		a.base[SIZE_S] = idx; idx += n;
		a.base[SIZE_L] = idx; idx += n;
		a.base[SIZE_X] = idx; idx += (n + a.xstep - 1) / a.xstep;
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
	if (!animate) { return (a.hold > 0) ? n - 1 : a.loop0; }
	if (a.pingpong)
	{
		const int period = 2 * n - 2;
		const int k = static_cast<int>((t / static_cast<unsigned>(a.ms)) % static_cast<unsigned>(period));
		return k < n ? k : period - k;
	}
	// MOD (19.11.): erster Durchlauf ab Bild 0 (beim Ueberfahren mit der Maus von vorn), danach Schleife loop0..n-1 (+ Halten)
	const unsigned first = static_cast<unsigned>(n * a.ms + a.hold);
	if (t < first)
	{
		return std::min(n - 1, static_cast<int>(t / static_cast<unsigned>(a.ms)));
	}
	const unsigned cycle = static_cast<unsigned>((n - a.loop0) * a.ms + a.hold);
	const unsigned tt = (t - first) % std::max(1u, cycle);
	return std::min(n - 1, a.loop0 + static_cast<int>(tt / static_cast<unsigned>(a.ms)));
}

void stopSound()
{
	if (s_stream != nullptr)
	{
		sound_StopStream(s_stream);   // wird beim naechsten Audio-Update zerstoert (Rueckruf setzt s_stream nur zurueck, wenn er noch passt)
		s_stream = nullptr;
	}
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
	if (size == SIZE_X) { k /= a.xstep; }
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
	// im Hauptmenue sind die Spiel-Klaenge (audio.wrf) noch nicht geladen: die Datei direkt als Stream abspielen
	char fn[48];
	snprintf(fn, sizeof(fn), "audio/mf/faction_%d.ogg", faction);
	stopSound();   // MOD (19.11.): kein Ueberlagern mit dem Klang der vorher angeklickten Fraktion
	if (PHYSFS_exists(fn))
	{
		s_stream = audio_PlayStream(fn, 0.9f, [](const AUDIO_STREAM *st, const void *) { if (s_stream == st) { s_stream = nullptr; } }, nullptr);
		if (s_stream == nullptr)
		{
			debug(LOG_WARNING, "Fraktions-Klang %s konnte nicht abgespielt werden", fn);
		}
	}
}

/// (16.11. 19:54) das grosse Symbol spielt die Animation bis zum Schluss: Dauer = ein ganzer Durchlauf + kurz stehen lassen
static unsigned splashMs(int faction)
{
	if (faction < 0 || faction >= NUM) { return SPLASH_MS; }
	const Anim &a = s_anim[faction];
	if (a.frames <= 1) { return SPLASH_MS; }
	const unsigned run = static_cast<unsigned>(a.splashN > 0 ? a.splashN : a.frames) * static_cast<unsigned>(a.ms);
	return std::clamp(run + 1100u, SPLASH_MS, 15000u);   // MOD (19.11.): Nexus (Scheibe + Kopf + Lachen) braucht ~13 s
}

void drawSplash()
{
	if (s_splashFaction < 0) { return; }
	const unsigned t = realTime - s_splashStart;
	const unsigned total = splashMs(s_splashFaction);
	if (t >= total || !load())
	{
		s_splashFaction = -1;
		return;
	}
	// einblenden 0,25 s, ausblenden die letzten 0,7 s (das Endbild steht dabei)
	float al = 1.0f;
	if (t < 250) { al = static_cast<float>(t) / 250.0f; }
	else if (t > total - 700) { al = static_cast<float>(total - t) / 700.0f; }
	const int sz = std::min(static_cast<int>(screenHeight) * 2 / 5, 320);
	const int x = (static_cast<int>(screenWidth) - sz) / 2;
	const int y = (static_cast<int>(screenHeight) - sz) / 2;
	// MOD (16.11. 18:00): kein grauer Kasten mehr - das Menue wird waehrend des Symbols ausgeblendet (frontend.cpp), das Symbol steht frei
	// ein Durchlauf (vor/zurueck-Animationen nur vorwaerts), danach steht das letzte Bild
	const Anim &an = s_anim[s_splashFaction];
	const int nsp = an.splashN > 0 ? an.splashN : an.frames;
	const unsigned tt = std::min(t, static_cast<unsigned>(std::max(0, nsp - 1)) * static_cast<unsigned>(an.ms));
	const int k = std::min(nsp - 1, static_cast<int>(tt / static_cast<unsigned>(std::max(1, an.ms))));
	const int id = an.base[SIZE_X] + k / an.xstep;
	if (an.frames > 0 && id >= 0 && id < static_cast<int>(s_img->imageDefs.size()))
	{
		iV_DrawImageFileAnisotropic(s_img, static_cast<UWORD>(id), x, y, Vector2f(static_cast<float>(sz), static_cast<float>(sz)), defaultProjectionMatrix(), static_cast<uint8_t>(255.0f * al));
	}
}

bool splashActive()
{
	return s_splashFaction >= 0 && (realTime - s_splashStart) < splashMs(s_splashFaction);
}

void endSplash()
{
	s_splashFaction = -1;
	stopSound();   // MOD (19.11.): Klick/Esc beendet auch den Klang
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
	stopSound();
}
} // namespace modfaction
