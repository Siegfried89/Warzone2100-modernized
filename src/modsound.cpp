// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: MF-Klaenge fuer Gebaeude - siehe modsound.h
#include "modsound.h"

#include "lib/framework/frame.h"
#include "lib/framework/file.h"
#include "lib/framework/physfs_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "lib/sound/track.h"
#include "structure.h"
#include "structuredef.h"

#include <sstream>
#include <string>
#include <unordered_map>

namespace modsound
{
namespace
{
struct Entry { std::string sel, die; int selId = -2, dieId = -2; };
std::unordered_map<std::string, Entry> s_map;
bool s_loaded = false;
unsigned s_lastSel = 0;

void load()
{
	if (s_loaded) { return; }
	s_loaded = true;
	const char *fn = "audio/mf/mf_struct.txt";
	if (!PHYSFS_exists(fn)) { return; }
	char *data = nullptr;
	UDWORD size = 0;
	if (!loadFile(fn, &data, &size) || data == nullptr) { return; }
	std::istringstream in(std::string(data, size));
	free(data);
	std::string id, sel, die;
	while (in >> id >> sel >> die)
	{
		Entry e;
		e.sel = sel == "-" ? std::string() : sel;
		e.die = die == "-" ? std::string() : die;
		s_map[id] = e;
	}
	debug(LOG_INFO, "MOD: %zu MF-Gebaeudeklaenge", s_map.size());
}

int track(const std::string &name, int &cache)
{
	if (cache != -2) { return cache; }
	cache = NO_SOUND;
	if (name.empty()) { return cache; }
	int id = audio_GetIDFromStr(name.c_str());
	if (id == NO_SOUND) { id = sound_GetTrackIDByName(name.c_str()); }
	if (id > 0) { cache = id; }
	return cache;
}

Entry *find(const STRUCTURE *s)
{
	if (s == nullptr || s->pStructureType == nullptr) { return nullptr; }
	load();
	if (s_map.empty()) { return nullptr; }
	const auto it = s_map.find(s->pStructureType->id.toUtf8());
	return it == s_map.end() ? nullptr : &it->second;
}
}

void structureSelected(const STRUCTURE *psStructure)
{
	Entry *e = find(psStructure);
	if (e == nullptr) { return; }
	if (realTime - s_lastSel < 400) { return; }   // nicht mehrfach bei Doppelklick
	const int id = track(e->sel, e->selId);
	if (id > 0)
	{
		s_lastSel = realTime;
		audio_PlayTrack(id);
	}
}

void structureDestroyed(const STRUCTURE *psStructure)
{
	Entry *e = find(psStructure);
	if (e == nullptr) { return; }
	const int id = track(e->die, e->dieId);
	if (id > 0) { audio_PlayStaticTrack(psStructure->pos.x, psStructure->pos.y, id); }
}
}
