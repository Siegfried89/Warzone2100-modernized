// SPDX-License-Identifier: GPL-2.0-or-later

/*
	This file is part of Warzone 2100.
	Copyright (C) 2025  Warzone 2100 Project

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
/** \file
 *  Defaults Options form
 */

#include "optionsforms.h"

#include "../../warzoneconfig.h"
#include "lib/ivis_opengl/bitimage.h"
#include "../../difficulty.h"
#include "../../multiint.h"
#include "../../multiplay.h"
#include "../../clparse.h"
#include "../../frend.h"
#include "../campaign.h"

#include <fmt/core.h>

// MARK: -

OptionInfo::AvailabilityResult IsLobbyIPv6Enabled(const OptionInfo&)
{
	OptionInfo::AvailabilityResult result;
	result.available = !war_getLobbyDisableIPv6();
	result.localizedUnavailabilityReason = _("Only useful if Enable IPv6 is On.");
	return result;
}

// MARK: -

OptionsDropdown<uint32_t>::PopulateFunc makeInactivityTimeoutOptionsDropdownPopulateFunc()
{
	return []() {
		OptionChoices<uint32_t> result;
		result.choices = {
			{ _("Off"), "", 0 }
		};
		for (uint32_t inactivityMinutes = MIN_MPINACTIVITY_MINUTES; inactivityMinutes <= (MIN_MPINACTIVITY_MINUTES + 6); inactivityMinutes++)
		{
			result.choices.push_back({ WzString::format(_("%u minutes"), inactivityMinutes), "", inactivityMinutes });
		}
		auto currValue = war_getMPInactivityMinutes();
		if (!result.setCurrentIdxForValue(currValue))
		{
			// add "Custom" item
			result.choices.push_back({ WzString::format(_("%u minutes"), currValue), "", currValue });
			result.currentIdx = result.choices.size() - 1;
		}
		return result;
	};
}

OptionsDropdown<int>::PopulateFunc makeLagKickOptionsDropdownPopulateFunc()
{
	return []() {
		OptionChoices<int> result;
		result.choices = {
			{ _("Off"), "", 0 }
		};
		for (int lagKickSeconds = 60; lagKickSeconds <= 120; lagKickSeconds += 30)
		{
			result.choices.push_back({ WzString::format(_("%u seconds"), lagKickSeconds), "", lagKickSeconds });
		}
		auto currValue = war_getAutoLagKickSeconds();
		if (!result.setCurrentIdxForValue(currValue))
		{
			// add "Custom" item
			result.choices.push_back({ WzString::format(_("%u seconds"), currValue), "", currValue });
			result.currentIdx = result.choices.size() - 1;
		}
		return result;
	};
}

OptionsDropdown<uint32_t>::PopulateFunc makeGameTimeLimitOptionsDropdownPopulateFunc()
{
	return []() {
		OptionChoices<uint32_t> result;
		result.choices = {
			{ _("Off"), "", 0 }
		};
		auto addGameTimeLimitMinutesOption = [&](uint32_t gameTimeLimitMinutes) {
			std::string buttonText;
			if (gameTimeLimitMinutes >= 120)
			{
				std::string hoursFloatStr = fmt::format("{:#}", static_cast<float>(gameTimeLimitMinutes) / 60.f);
				buttonText = astringf(_("%s hours"), hoursFloatStr.c_str());
			}
			else
			{
				buttonText = astringf(_("%u minutes"), gameTimeLimitMinutes);
			}
			result.choices.push_back({ WzString::fromUtf8(buttonText), "", gameTimeLimitMinutes });
		};
		for (uint32_t gameTimeLimitMinutes = MIN_MPGAMETIMELIMIT_MINUTES; gameTimeLimitMinutes <= (MIN_MPGAMETIMELIMIT_MINUTES + (15 * 10)); gameTimeLimitMinutes += 15)
		{
			addGameTimeLimitMinutesOption(gameTimeLimitMinutes);
		}
		auto currValue = war_getMPGameTimeLimitMinutes();
		if (!result.setCurrentIdxForValue(currValue))
		{
			// add custom value
			addGameTimeLimitMinutesOption(currValue);
			result.currentIdx = result.choices.size() - 1;
		}
		return result;
	};
}

std::shared_ptr<OptionsForm> makeDefaultsOptionsForm()
{
	auto result = OptionsForm::make();

	// Campaign:
	result->addSection(OptionsSection(N_("Campaign"), ""), true);
	// MOD (Wunsch 26.11. 22:19): "Unit Color" entfernt - die Farbe kommt aus den Farbfeldern im Hauptmenue
	{
		auto optionInfo = OptionInfo("defaults.campaign.difficultyLevel", N_("Difficulty Level"), "");
		auto valueChanger = OptionsDropdown<DIFFICULTY_LEVEL>::make(
			[]() {
				OptionChoices<DIFFICULTY_LEVEL> result;
				std::vector<DIFFICULTY_LEVEL> availableDifficultyLevels = { DL_SUPER_EASY, DL_EASY, DL_NORMAL, DL_HARD, DL_INSANE };
				for (const auto& level : availableDifficultyLevels)
				{
					result.choices.push_back({ difficultyLevelToString(level), getCampaignDifficultyDescriptionString(level), level });
				}
				result.setCurrentIdxForValue(getDifficultyLevel());
				return result;
			},
			[](const auto& newValue) -> bool {
				setDifficultyLevel(newValue);
				return true;
			}, true
		);
		result->addOption(optionInfo, valueChanger, true);
	}

	// Multiplayer:
	result->addSection(OptionsSection(N_("Multiplayer"), ""), true);
	// MOD (26.11. 20:54): keine eigene Mehrspieler-Farbe mehr - es gilt die Farbe aus dem Hauptmenue
	{
		auto optionInfo = OptionInfo("defaults.multiplayer.defaultAI", N_("Default AI"), "");
		optionInfo.setRequiresRestart(true);
		auto valueChanger = OptionsDropdown<WzString>::make(
			[]() {
				OptionChoices<WzString> result;
				const auto& aidata = getAIData();
				for (size_t i = 0; i < aidata.size(); ++i)
				{
					result.choices.push_back({ aidata[i].name, "", aidata[i].js });
				}
				result.setCurrentIdxForValue(WzString::fromUtf8(getDefaultSkirmishAI()));
				return result;
			},
			[](const auto& newValue) -> bool {
				setDefaultSkirmishAI(newValue.toUtf8());
				return true;
			}, true
		);
		result->addOption(optionInfo, valueChanger, true);
	}

	// Lobby Options:
	result->addSection(OptionsSection(N_("Lobby Options"), ""), true);
	{
		auto optionInfo = OptionInfo("defaults.lobby.enableipv6", N_("Enable IPv6"), N_("Whether to list and try connecting via IPv6 (if available)."));
		auto valueChanger = OptionsDropdown<bool>::make(
			[]() {
				OptionChoices<bool> result;
				result.choices = {
					{ _("Off"), _("Disable connecting via IPv6, and listing IPv6-only games. Only recommended if your system has issues with IPv6 connectivity."), false },
					{ _("On"), _("Enable IPv6. If your system supports it, this is the recommended option."), true },
				};
				result.setCurrentIdxForValue(!war_getLobbyDisableIPv6());
				return result;
			},
			[](const auto& newValue) -> bool {
				war_setLobbyDisableIPv6(!newValue);
				return true;
			}, false
		);
		result->addOption(optionInfo, valueChanger, true);
	}
	{
		auto optionInfo = OptionInfo("defaults.lobby.preferipv6", N_("Prefer IPv6 over IPv4"), N_("Prefer connecting via IPv6 over IPv4 when a game is available via both."));
		optionInfo.addAvailabilityCondition(IsLobbyIPv6Enabled);
		auto valueChanger = OptionsDropdown<bool>::make(
			[]() {
				OptionChoices<bool> result;
				result.choices = {
					{ _("Off"), "", false },
					{ _("On"), "", true },
				};
				result.setCurrentIdxForValue(NETgetJoinPreferenceIPv6());
				return result;
			},
			[](const auto& newValue) -> bool {
				NETsetJoinPreferenceIPv6(newValue);
				return true;
			}, true
		);
		result->addOption(optionInfo, valueChanger, true);
	}

	// Hosting Options:
	result->addSection(OptionsSection(N_("Hosting Options"), ""), true);
	{
		auto optionInfo = OptionInfo("defaults.hosting.portMapping", N_("Port Mapping"), N_("Use PCP, NAT-PMP, or UPnP to help configure your router / firewall to allow connections while hosting."));
		auto valueChanger = OptionsDropdown<bool>::make(
			[]() {
				OptionChoices<bool> result;
				result.choices = {
					{ _("Off"), "", false },
					{ _("On"), "", true },
				};
				result.setCurrentIdxForValue(NetPlay.isPortMappingEnabled);
				return result;
			},
			[](const auto& newValue) -> bool {
				NetPlay.isPortMappingEnabled = newValue;
				return true;
			}, false
		);
		result->addOption(optionInfo, valueChanger, true);
	}
	{
		auto optionInfo = OptionInfo("defaults.hosting.chat", N_("Chat"), "");
		auto valueChanger = OptionsDropdown<bool>::make(
			[]() {
				OptionChoices<bool> result;
				result.choices = {
					{ _("Quick Chat Only"), "", false },
					{ _("Allow All"), "", true },
				};
				result.setCurrentIdxForValue(NETgetDefaultMPHostFreeChatPreference());
				return result;
			},
			[](const auto& newValue) -> bool {
				NETsetDefaultMPHostFreeChatPreference(newValue);
				return true;
			}, false
		);
		result->addOption(optionInfo, valueChanger, true);
	}
	{
		auto optionInfo = OptionInfo("defaults.hosting.inactivityTimeout", N_("Inactivity Timeout"), "");
		optionInfo.addAvailabilityCondition(IsNotInGame);
		auto valueChanger = OptionsDropdown<uint32_t>::make(
			makeInactivityTimeoutOptionsDropdownPopulateFunc(),
			[](const auto& newValue) -> bool {
				war_setMPInactivityMinutes(newValue);
				game.inactivityMinutes = war_getMPInactivityMinutes();
				return true;
			}, false
		);
		result->addOption(optionInfo, valueChanger, true);
	}
	{
		auto optionInfo = OptionInfo("defaults.hosting.lagKick", N_("Lag Kick"), "");
		auto valueChanger = OptionsDropdown<int>::make(
			makeLagKickOptionsDropdownPopulateFunc(),
			[](const auto& newValue) -> bool {
				war_setAutoLagKickSeconds(newValue);
				return true;
			}, false
		);
		result->addOption(optionInfo, valueChanger, true);
	}
	{
		auto optionInfo = OptionInfo("defaults.hosting.spectatorSlots", N_("Spectator Slots"), "");
		optionInfo.addAvailabilityCondition(IsNotInGame);
		auto valueChanger = OptionsDropdown<uint32_t>::make(
			[]() {
				OptionChoices<uint32_t> result;
				result.choices = {
					{ _("None"), "", 0 }
				};
				for (uint32_t openSpectatorSlots = 1; openSpectatorSlots <= MAX_SPECTATOR_SLOTS; openSpectatorSlots++)
				{
					result.choices.push_back({ WzString::number(openSpectatorSlots), "", openSpectatorSlots });
				}
				result.setCurrentIdxForValue(war_getMPopenSpectatorSlots());
				return result;
			},
			[](const auto& newValue) -> bool {
				war_setMPopenSpectatorSlots(newValue);
				return true;
			}, false
		);
		result->addOption(optionInfo, valueChanger, true);
	}
	{
		auto optionInfo = OptionInfo("defaults.hosting.onPlayerLeave", N_("On Player Leave"), "");
		optionInfo.addAvailabilityCondition(IsNotInGame);
		auto valueChanger = OptionsDropdown<PLAYER_LEAVE_MODE>::make(
			[]() {
				OptionChoices<PLAYER_LEAVE_MODE> result;
				result.choices = {
					{ _("Distribute to Team"), "", PLAYER_LEAVE_MODE::SPLIT_WITH_TEAM },
					{ _("Destroy (Classic)"), "", PLAYER_LEAVE_MODE::DESTROY_RESOURCES },
				};
				result.setCurrentIdxForValue(war_getMPPlayerLeaveMode());
				return result;
			},
			[](const auto& newValue) -> bool {
				war_setMPPlayerLeaveMode(newValue);
				return true;
			}, false
		);
		result->addOption(optionInfo, valueChanger, true);
	}
	{
		auto optionInfo = OptionInfo("defaults.hosting.gameTimeLimit", N_("Game Time Limit"), "");
		optionInfo.addAvailabilityCondition(IsNotInGame);
		auto valueChanger = OptionsDropdown<uint32_t>::make(
			makeGameTimeLimitOptionsDropdownPopulateFunc(),
			[](const auto& newValue) -> bool {
				war_setMPGameTimeLimitMinutes(newValue);
				game.gameTimeLimitMinutes = war_getMPGameTimeLimitMinutes();
				return true;
			}, false
		);
		result->addOption(optionInfo, valueChanger, true);
	}

	return result;
}
