/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "mapping.h"

#include "lib/framework/frame.h"
#include "lib/framework/wzconfig.h"
#include "lib/gamelib/gtime.h" // For gameTime

#include "keyconfig.h"

static bool isCombination(const KeyMapping& mapping)
{
	return mapping.keys.meta.source != KeyMappingMetaSource::NONE;
}

static bool isActiveSingleKey(const KeyMapping& mapping)
{
	switch (mapping.keys.action)
	{
	case KeyAction::PRESSED:
		return mapping.keys.input.isPressed();
	case KeyAction::DOWN:
		return mapping.keys.input.isDown();
	case KeyAction::RELEASED:
		return mapping.keys.input.isReleased();
	default:
		debug(LOG_WARNING, "Unknown key action (action code %u) while processing keymap.", static_cast<unsigned int>(mapping.keys.action));
		return false;
	}
}

static KEY_CODE getAlternativeForMetaKey(const KEY_CODE meta)
{
	auto altMeta = KEY_CODE::KEY_IGNORE;
	if (meta == KEY_CODE::KEY_LCTRL)
	{
		altMeta = KEY_CODE::KEY_RCTRL;
	}
	else if (meta == KEY_CODE::KEY_LALT)
	{
		altMeta = KEY_CODE::KEY_RALT;
	}
	else if (meta == KEY_CODE::KEY_LSHIFT)
	{
		altMeta = KEY_CODE::KEY_RSHIFT;
	}
	else if (meta == KEY_CODE::KEY_LMETA)
	{
		altMeta = KEY_CODE::KEY_RMETA;
	}

	return altMeta;
}

static bool isActiveCombination(const KeyMapping& mapping)
{
	ASSERT(mapping.hasMeta(), "isActiveCombination called for non-meta key mapping!");

	const bool bSubKeyIsPressed = mapping.keys.input.isPressed();

	if (const auto gamepadMeta = mapping.keys.meta.asGamepadInput())
	{
		return bSubKeyIsPressed && gamepadButtonDown(gamepadMeta.value());
	}

	const auto metaKey = mapping.keys.meta.asKeyCode();
	if (!metaKey.has_value())
	{
		return false;
	}
	const bool bMetaIsDown = keyDown(metaKey.value());

	const auto altMeta = getAlternativeForMetaKey(metaKey.value());
	const bool bHasAlt = altMeta != KEY_IGNORE;
	const bool bAltMetaIsDown = bHasAlt && keyDown(altMeta);

	return bSubKeyIsPressed && (bMetaIsDown || bAltMetaIsDown);
}

bool KeyMapping::isInvalid() const
{
	return keys.input.is(KEY_CODE::KEY_MAXSCAN);
}

bool KeyMapping::isActivated() const
{
	return isCombination(*this)
		? isActiveCombination(*this)
		: isActiveSingleKey(*this);
}

bool KeyMapping::hasMeta() const
{
	return keys.meta.source != KeyMappingMetaSource::NONE;
}

std::string KeyMapping::toString() const
{
	// Figure out if the keycode is for mouse or keyboard and print the name of
	// the respective key/mouse button to `asciiSub`
	char asciiSub[20] = "\0";
	switch (keys.input.source)
	{
	case KeyMappingInputSource::KEY_CODE:
		keyScanToString(keys.input.value.keyCode, (char*)&asciiSub, 20);
		break;
	case KeyMappingInputSource::MOUSE_KEY_CODE:
		mouseKeyCodeToString(keys.input.value.mouseKeyCode, (char*)&asciiSub, 20);
		break;
	case KeyMappingInputSource::GAMEPAD:
		sstrcpy(asciiSub, gamepadButtonName(keys.input.value.gamepadInput));
		break;
	default:
		debug(LOG_WZ, "Encountered invalid key mapping source %u while converting mapping to string!", static_cast<unsigned int>(keys.input.source));
		return std::string("NOT VALID");
	}

	if (hasMeta())
	{
		if (const auto gamepadMeta = keys.meta.asGamepadInput())
		{
			return astringf("%s %s", gamepadButtonName(gamepadMeta.value()), asciiSub);
		}

		char asciiMeta[20] = "\0";
		keyScanToString(keys.meta.asKeyCode().value_or(KEY_CODE::KEY_IGNORE), (char*)&asciiMeta, 20);

		return astringf("%s %s", asciiMeta, asciiSub);
	}
	else
	{
		return std::string(asciiSub);
	}
}


bool operator==(const KeyMapping& lhs, const KeyMapping& rhs)
{
	return lhs.keys == rhs.keys
		&& lhs.slot == rhs.slot
		&& &lhs.info == &rhs.info; // Assume infos are immutable with only one copy existing at a time.
}

bool operator!=(const KeyMapping& lhs, const KeyMapping& rhs)
{
	return !(lhs == rhs);
}


KeyMapping& KeyMappings::add(const KeyCombination keys, const KeyFunctionInfo& info, const KeyMappingSlot slot)
{
	/* Make sure the meta key is the left variant */
	KeyCombination keysWithLeftMeta = keys;
	const auto metaKey = keys.meta.asKeyCode();
	if (metaKey == KEY_RCTRL)
	{
		keysWithLeftMeta.meta = KEY_LCTRL;
	}
	else if (metaKey == KEY_RALT)
	{
		keysWithLeftMeta.meta = KEY_LALT;
	}
	else if (metaKey == KEY_RSHIFT)
	{
		keysWithLeftMeta.meta = KEY_LSHIFT;
	}
	else if (metaKey == KEY_RMETA)
	{
		keysWithLeftMeta.meta = KEY_LMETA;
	}

	/* Create the mapping as the last element in the list */
	keyMappings.push_back({
		info,
		gameTime,
		keysWithLeftMeta,
		slot
		});

	/* Invalidate the sorting order and return the newly created mapping */
	bDirty = true;
	return keyMappings.back();
}

nonstd::optional<std::reference_wrapper<KeyMapping>> KeyMappings::get(const std::string& name, const KeyMappingSlot slot)
{
	auto mapping = std::find_if(keyMappings.begin(), keyMappings.end(), [&name, slot](const KeyMapping& mapping) {
		return mapping.info.name == name && mapping.slot == slot;
	});
	if (mapping != keyMappings.end())
	{
		return *mapping;
	}

	return nonstd::nullopt;
}

nonstd::optional<std::reference_wrapper<KeyMapping>> KeyMappings::get(const KeyFunctionInfo& info, const KeyMappingSlot slot)
{
	return get(info.name, slot);
}

std::vector<std::reference_wrapper<KeyMapping>> KeyMappings::find(const KeyMappingMeta meta, const KeyMappingInput input)
{
	std::vector<std::reference_wrapper<KeyMapping>> matches;
	for (KeyMapping& mapping : keyMappings)
	{
		if (mapping.keys.meta == meta && mapping.keys.input == input)
		{
			matches.push_back(mapping);
		}
	}

	return matches;
}

bool KeyMappings::remove(const KeyMapping& mappingToRemove)
{
	auto mapping = std::find_if(keyMappings.begin(), keyMappings.end(), [mappingToRemove](const KeyMapping& mapping) {
		return mapping == mappingToRemove;
	});
	if (mapping != keyMappings.end())
	{
		keyMappings.erase(mapping);
		bDirty = true;
		return true;
	}
	return false;
}

std::vector<std::reference_wrapper<KeyMapping>> KeyMappings::findConflicting(const KeyMappingMeta meta, const KeyMappingInput input, const ContextId contextId, const ContextManager& contexts)
{
	/* Find any mapping with same keys */
	const auto matches = find(meta, input);
	std::vector<std::reference_wrapper<KeyMapping>> conflicts;
	for (KeyMapping& mapping : matches)
	{
		const InputContext mappingContext = contexts.get(mapping.info.context);

		/* Keys conflict if they are for the same context. Always active contexts are special (always have highest priority), so those keys will always conflict. */
		const bool bConflicts = mappingContext.isAlwaysActive() || std::string(mapping.info.context) == std::string(contextId);
		if (bConflicts)
		{
			conflicts.push_back(mapping);
		}
	}

	return conflicts;
}

std::vector<KeyMapping> KeyMappings::removeConflicting(const KeyMappingMeta meta, const KeyMappingInput input, const ContextId& contextId, const ContextManager& contexts)
{
	/* Find any mapping with same keys */
	const auto conflicting = findConflicting(meta, input, contextId, contexts);
	std::vector<KeyMapping> removed;
	for (KeyMapping& mapping : conflicting)
	{
		/* Clear only if the mapping is for an assignable binding */
		if (mapping.info.type == KeyMappingType::ASSIGNABLE)
		{
			removed.push_back(mapping);
			remove(mapping);
		}
	}

	return removed;
}

bool KeyMappings::isDirty() const
{
	return bDirty;
}

int KeyMappings::loadedFileVersion() const
{
	return fileVersion;
}

void KeyMappings::clear(nonstd::optional<KeyMappingType> filter)
{
	if (!filter.has_value())
	{
		keyMappings.clear();
	}
	else
	{
		keyMappings.remove_if([filter](const KeyMapping& mapping) {
			return mapping.info.type == filter.value();
		});
	}

	bDirty = true;
}

void KeyMappings::sort(const ContextManager& contexts)
{
	keyMappings.sort([&contexts](const KeyMapping& a, const KeyMapping& b) {
		// Sort by meta. This causes all mappings with meta to be checked before non-meta mappings,
		// avoiding having to check for meta-conflicts in the processing loop. (e.g. if we should execute
		// a mapping with right arrow key, depending on if another binding on shift+right-arrow is executed
		// or not). In other words, if any mapping with meta is executed, it will consume the respective input,
		// preventing any non-meta mappings with the same input from being executed.
		if (a.hasMeta() != b.hasMeta()) {
			return a.hasMeta() && !b.hasMeta();
		}

		// Sort by priority
		const unsigned int priorityA = contexts.getPriority(a.info.context);
		const unsigned int priorityB = contexts.getPriority(b.info.context);
		return priorityA > priorityB;
	});
	bDirty = false;
}

static KeyMappingInput createInputForSource(const KeyMappingInputSource source, const unsigned int keyCode)
{
	switch (source) {
	case KeyMappingInputSource::KEY_CODE:
		if (keyCode > static_cast<unsigned int>(KEY_MAXSCAN))
		{
			debug(LOG_WZ, "Encountered invalid key code %u while loading keymap!", keyCode);
			return KEY_CODE::KEY_MAXSCAN;
		}
		return (KEY_CODE)keyCode;
	case KeyMappingInputSource::MOUSE_KEY_CODE:
		if (keyCode >= static_cast<unsigned int>(MOUSE_END))
		{
			debug(LOG_WZ, "Encountered invalid mouse button %u while loading keymap!", keyCode);
			return KEY_CODE::KEY_MAXSCAN;
		}
		return (MOUSE_KEY_CODE)keyCode;
	case KeyMappingInputSource::GAMEPAD:
		if (keyCode >= static_cast<unsigned int>(GPAD_BTN_MAX))
		{
			debug(LOG_WZ, "Encountered invalid gamepad button %u while loading keymap!", keyCode);
			return KEY_CODE::KEY_MAXSCAN;
		}
		return (GAMEPAD_INPUT)keyCode;
	default:
		debug(LOG_WZ, "Encountered invalid key mapping source %u while loading keymap!", static_cast<unsigned int>(source));
		return KEY_CODE::KEY_MAXSCAN;
	}
}

bool KeyMappings::load(const char* path, const KeyFunctionConfiguration& keyFuncConfig)
{
	/* Clear all assignable mappings */
	clear(KeyMappingType::ASSIGNABLE);

	WzConfig ini(path, WzConfig::ReadOnly);
	if (!ini.status())
	{
		debug(LOG_WZ, "%s not found", path);
		return false;
	}

	fileVersion = ini.value("version", 1).toInt();

	for (ini.beginArray("mappings"); ini.remainingArrayItems(); ini.nextArrayItem())
	{
		const int metaValue = ini.value("meta", 0).toInt();
		const KEY_CODE meta = (metaValue >= 0 && metaValue < KEY_MAXSCAN) ? (KEY_CODE)metaValue : KEY_IGNORE;
		auto sub = ini.value("sub", 0).toInt();
		auto action = (KeyAction)ini.value("action", 0).toInt();
		auto functionName = ini.value("function", "").toWzString();
		auto info = keyFuncConfig.keyFunctionInfoByName(functionName.toUtf8());
		if (!info.has_value())
		{
			debug(LOG_WARNING, "Skipping unknown keymap function \"%s\".", functionName.toUtf8().c_str());
			continue;
		}
		else if (info->get().type != KeyMappingType::ASSIGNABLE)
		{
			/* No need to load non-assignable mappings */
			debug(LOG_WARNING, "Skipping non-assignable keymap function \"%s\".", functionName.toUtf8().c_str());
			continue;
		}

		const WzString sourceName = ini.value("source", "default").toWzString();
		const KeyMappingInputSource source = keyMappingSourceByName(sourceName.toUtf8().c_str());
		const KeyMappingInput input = createInputForSource(source, sub);

		const WzString slotName = ini.value("slot", "primary").toWzString();
		const KeyMappingSlot slot = keyMappingSlotByName(slotName.toUtf8().c_str());

		KeyCombination keys(meta, input, action);
		const int gamepadMeta = ini.value("gamepadMeta", GPAD_BTN_MAX).toInt();
		if (gamepadMeta >= 0 && gamepadMeta < GPAD_BTN_MAX)
		{
			keys.meta = (GAMEPAD_INPUT)gamepadMeta;
		}
		add(keys, *info, slot);
	}
	ini.endArray();

	// MOD (Wunsch 18.11. 01:08, 12:41 getauscht): "Q dreht die Kamera nach links, E nach rechts" - auch fuer eine schon gespeicherte keymap.json (der Spieler soll
	// nichts selbst einstellen muessen): die alten Standardbelegungen Q = Patrouille und E = Feuer erwidern fallen weg (beides bleibt ueber die
	// Befehlsknoepfe erreichbar), Q/E drehen die Kamera (zweite Belegung neben Ziffernblock 6/4).
	for (auto it = keyMappings.begin(); it != keyMappings.end();)
	{
		const bool qe = (it->keys.input.is(KEY_Q) || it->keys.input.is(KEY_E)) && !it->hasMeta();
		// (Wunsch 18.11. 12:41 "Q und E fuer die Kamerasteuerung tauschen"): die Belegung von 01:08 (Q = rechts, E = links) faellt weg
		const bool oldRot = (it->keys.input.is(KEY_Q) && it->info.name == "RotateRight") || (it->keys.input.is(KEY_E) && it->info.name == "RotateLeft");
		if (qe && (oldRot || it->info.name == "SetDroidMovePatrol" || it->info.name == "SetDroidAttackReturn"))
		{
			it = keyMappings.erase(it);
			bDirty = true;
		}
		else
		{
			++it;
		}
	}
	auto bindOnce = [&](KEY_CODE key, const char *fn, KeyAction action = KeyAction::DOWN) {
		for (const KeyMapping &m : keyMappings)
		{
			if (m.keys.input.is(key) && !m.hasMeta() && m.info.name == fn) { return; }
		}
		auto rot = keyFuncConfig.keyFunctionInfoByName(fn);
		if (!rot.has_value()) { return; }
		add(KeyCombination(KEY_IGNORE, KeyMappingInput(key), action), *rot, KeyMappingSlot::SECONDARY);
		bDirty = true;
	};
	// MOD (Wunsch 19.11. 16:44 "Zeit erhoehen und verringern Tasten funktionieren nicht richtig, ausserdem Doppelbelegung mit Zoom"):
	// Ziffernblock + und - steuern jetzt nur die Zeit (+ schneller, - langsamer, * bleibt zusaetzlich "schneller"); Zoom liegt auf dem
	// Mausrad und Bild auf/ab. Alte Belegungen von Ziffernblock +/-/* fuer Zoom werden entfernt, Bild auf wird "Zoom naeher".
	bool zoomInKey = false;
	for (auto it = keyMappings.begin(); it != keyMappings.end();)
	{
		const bool kp = (it->keys.input.is(KEY_KP_PLUS) || it->keys.input.is(KEY_KP_MINUS) || it->keys.input.is(KEY_KP_STAR)) && !it->hasMeta();
		if (kp && (it->info.name == "ZoomIn" || it->info.name == "ZoomOut" || it->info.name == "RadarZoomIn" || it->info.name == "RadarZoomOut"))
		{
			it = keyMappings.erase(it);
			bDirty = true;
			continue;
		}
		if (it->info.name == "ZoomIn" && it->keys.input.is(KEY_PAGEUP)) { zoomInKey = true; }
		++it;
	}
	if (!zoomInKey)
	{
		auto zi = keyFuncConfig.keyFunctionInfoByName("ZoomIn");
		if (zi.has_value())
		{
			add(KeyCombination(KEY_IGNORE, KeyMappingInput(KEY_PAGEUP), KeyAction::DOWN), *zi, KeyMappingSlot::PRIMARY);
			bDirty = true;
		}
	}
	bindOnce(KEY_KP_PLUS, "ModTimeFaster", KeyAction::PRESSED);
	bindOnce(KEY_Q, "RotateLeft");     // (18.11. 12:41) Q dreht nach links, E nach rechts
	bindOnce(KEY_E, "RotateRight");
	bindOnce(KEY_INSERT, "ToggleArtRevolution", KeyAction::PRESSED);   // (18.11.) AR-Grafik im Spiel umschalten
	return true;
}

bool KeyMappings::save(const char* path) const
{
	WzConfig ini(path, WzConfig::ReadAndWrite);
	if (!ini.status() || !ini.isWritable())
	{
		// NOTE: Changed to LOG_FATAL, since we want to inform user via pop-up (windows only)
		debug(LOG_FATAL, "Could not open %s", ini.fileName().toUtf8().c_str());
		return false;
	}

	ini.setValue("version", KEYMAP_FORMAT_VERSION);

	ini.beginArray("mappings");
	for (const KeyMapping& mapping : keyMappings)
	{
		/* No need to save non-assignable mappings */
		if (mapping.info.type != KeyMappingType::ASSIGNABLE)
		{
			continue;
		}

		ini.setValue("name", mapping.info.name);
		ini.setValue("meta", mapping.keys.meta.asKeyCode().value_or(KEY_CODE::KEY_IGNORE));
		if (const auto gamepadMeta = mapping.keys.meta.asGamepadInput())
		{
			ini.setValue("gamepadMeta", gamepadMeta.value());
		}

		switch (mapping.keys.input.source) {
		case KeyMappingInputSource::KEY_CODE:
			ini.setValue("source", "default");
			ini.setValue("sub", mapping.keys.input.value.keyCode);
			break;
		case KeyMappingInputSource::MOUSE_KEY_CODE:
			ini.setValue("source", "mouse_key");
			ini.setValue("sub", mapping.keys.input.value.mouseKeyCode);
			break;
		case KeyMappingInputSource::GAMEPAD:
			ini.setValue("source", "gamepad");
			ini.setValue("sub", mapping.keys.input.value.gamepadInput);
			break;
		default:
			debug(LOG_WZ, "Encountered invalid key mapping source %u while saving keymap!", static_cast<unsigned int>(mapping.keys.input.source));
			break;
		}
		switch (mapping.slot)
		{
		case KeyMappingSlot::PRIMARY:
			ini.setValue("slot", "primary");
			break;
		case KeyMappingSlot::SECONDARY:
			ini.setValue("slot", "secondary");
			break;
		case KeyMappingSlot::GAMEPAD:
			ini.setValue("slot", "gamepad");
			break;
		default:
			debug(LOG_WZ, "Encountered invalid key mapping slot %u while saving keymap!", static_cast<unsigned int>(mapping.slot));
			break;
		}

		ini.setValue("action", mapping.keys.action);
		ini.setValue("function", mapping.info.name);

		ini.nextArrayItem();
	}
	ini.endArray();

	debug(LOG_WZ, "Keymap written ok to %s.", path);
	return true;
}
