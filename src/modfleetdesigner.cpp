// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Einheiteneditor aus dem Flotten-Menue (siehe modfleetdesigner.h)
#include "modfleetdesigner.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"

#include "campaigninfo.h"
#include "console.h"
#include "design.h"
#include "display.h"
#include "frontend.h"
#include "hci.h"
#include "input/manager.h"
#include "levels.h"
#include "objmem.h"
#include "qtscript.h"
#include "stats.h"
#include "structure.h"
#include "wrappers.h"

namespace modfleetdesigner
{
namespace
{
bool s_active = false;      // Designer-Sitzung laeuft (Spiel wurde dafuer gestartet)
bool s_opened = false;      // Design-Bildschirm wurde geoeffnet
bool s_titleReturn = false; // Hauptmenue soll ins Flotten-Menue wechseln
bool s_fleetUpdate = false; // Flotten-Menue soll die gespeicherten Entwuerfe uebernehmen
unsigned s_t0 = 0;

void unlockParts(unsigned player)
{
	if (player >= MAX_PLAYERS) { return; }
	for (size_t i = 0; i < asWeaponStats.size(); ++i) { apCompLists[player][COMP_WEAPON][i] = AVAILABLE; }
	for (size_t i = 0; i < asBodyStats.size(); ++i) { apCompLists[player][COMP_BODY][i] = AVAILABLE; }
	for (size_t i = 0; i < asPropulsionStats.size(); ++i) { apCompLists[player][COMP_PROPULSION][i] = AVAILABLE; }
	for (size_t i = 0; i < asSensorStats.size(); ++i) { apCompLists[player][COMP_SENSOR][i] = AVAILABLE; }
	for (size_t i = 0; i < asECMStats.size(); ++i) { apCompLists[player][COMP_ECM][i] = AVAILABLE; }
	for (size_t i = 0; i < asConstructStats.size(); ++i) { apCompLists[player][COMP_CONSTRUCT][i] = AVAILABLE; }
	for (size_t i = 0; i < asBrainStats.size(); ++i) { apCompLists[player][COMP_BRAIN][i] = AVAILABLE; }
	for (size_t i = 0; i < asRepairStats.size(); ++i) { apCompLists[player][COMP_REPAIRUNIT][i] = AVAILABLE; }
}
}

void request()
{
	s_active = true;
	s_opened = false;
	s_titleReturn = false;
	s_fleetUpdate = false;
	s_t0 = 0;
	SPinit(LEVEL_TYPE::CAMPAIGN);
	sstrcpy(aLevelName, "CAM_1A");
	setCampaignName("Alpha Campaign");
	setCamTweakOptions({});
	changeTitleMode(STARTGAME);
}

bool active()
{
	return s_active;
}

void tick()
{
	if (!s_active) { return; }
	if (!s_opened)
	{
		if (s_t0 == 0) { s_t0 = realTime; }
		if (realTime - s_t0 < 1500) { return; }   // Karte erst ganz aufbauen lassen
		unlockParts(selectedPlayer);
		intResetScreen(false);
		intShowPowerBar();
		intAddDesign(false);
		intMode = INT_DESIGN;
		gInputManager.contexts().pushState();
		gInputManager.contexts().makeAllInactive();
		triggerEvent(TRIGGER_MENU_DESIGN_UP);
		s_opened = true;
		addConsoleMessage(_("Unit designer: save designs with the star - close the designer to return to Fleets"), CENTRE_JUSTIFY, SYSTEM_MESSAGE);
		return;
	}
	if (intMode != INT_DESIGN)
	{
		// Designer geschlossen: zurueck ins Hauptmenue, dort ins Flotten-Menue
		s_active = false;
		s_opened = false;
		s_titleReturn = true;
		s_fleetUpdate = true;
		intRequestQuitToMainMenu();
	}
}

bool titleReturn()
{
	s_active = false;   // im Hauptmenue laeuft keine Sitzung (falls das Spiel anders beendet wurde)
	s_opened = false;
	const bool r = s_titleReturn;
	s_titleReturn = false;
	return r;
}

bool consumeFleetUpdate()
{
	const bool r = s_fleetUpdate;
	s_fleetUpdate = false;
	return r;
}
}
