// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Grafik "Art Revolution" umschaltbar (siehe modargfx.h)
#include "modargfx.h"

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "configuration.h"
#include "console.h"
#include "warzoneconfig.h"

#include <physfs.h>

namespace modargfx
{
namespace
{
constexpr const char *PACKAGE = "mods/grafik/ARGrafik.wz";
std::string s_mounted;   // reale Pfadangabe des eingehaengten Pakets (leer = nicht eingehaengt)

bool stillMounted()
{
	return !s_mounted.empty() && PHYSFS_getMountPoint(s_mounted.c_str()) != nullptr;
}

std::string realPackagePath()
{
	const char *dir = PHYSFS_getRealDir(PACKAGE);
	if (dir == nullptr)
	{
		return std::string();
	}
	std::string p(dir);
	const char *sep = PHYSFS_getDirSeparator();
	if (!p.empty() && p.back() != '/' && p.back() != '\\')
	{
		p += sep;
	}
	std::string rel(PACKAGE);
	if (sep[0] != '/')
	{
		for (char &c : rel) { if (c == '/') { c = sep[0]; } }
	}
	return p + rel;
}

bool mountNow()
{
	if (stillMounted())
	{
		return true;
	}
	s_mounted.clear();
	const std::string real = realPackagePath();
	if (real.empty())
	{
		return false;
	}
	if (PHYSFS_mount(real.c_str(), WZ_CURRENT_GRAPHICS_OVERRIDES_PREFIX, 1) == 0)
	{
		debug(LOG_ERROR, "AR-Grafik: Paket konnte nicht eingehaengt werden (%s): %s", real.c_str(), PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
		return false;
	}
	s_mounted = real;
	return true;
}

void unmountNow()
{
	if (stillMounted())
	{
		PHYSFS_unmount(s_mounted.c_str());
	}
	s_mounted.clear();
}
}

bool installed()
{
	return PHYSFS_exists(PACKAGE) != 0;
}

void applyMount()
{
	if (war_getArtRevolution())
	{
		mountNow();
	}
	else
	{
		unmountNow();
	}
}

void toggle(bool inGame)
{
	const bool on = !war_getArtRevolution();
	if (on && !installed())
	{
		if (inGame) { addConsoleMessage(_("Art Revolution graphics are not installed"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE); }
		return;
	}
	war_setArtRevolution(on);
	saveConfig();
	if (on) { mountNow(); } else { unmountNow(); }
	if (inGame)
	{
		debugReloadAllDisplayModels();   // Anzeige-Modelle tauschen (Texturen laden beim ersten Zeichnen)
		addConsoleMessage(on ? _("Graphics: Art Revolution") : _("Graphics: Standard"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
	debug(LOG_INFO, "AR-Grafik %s", on ? "an" : "aus");
}
}
