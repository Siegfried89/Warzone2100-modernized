// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (23.11. 02:16): Turm-Belegung (modslots.h) in Vorlagen und Spielstaenden - nur in Dateien, die nlohmann/json schon vorher einbinden
#pragma once

#include "lib/framework/wzconfig.h"   // nlohmann/json mit dem MinGW-Umweg fuer snprintf

struct ModSlots;

namespace modslots
{
/// {"weapConn":[...], "systems":[{"type":"sensor"|"ecm","id":...,"slot":n}]}
nlohmann::json toJson(const ModSlots &s);
bool fromJson(const nlohmann::json &j, ModSlots &s);
}
