// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Sammelpunkt per Rechtsklick (siehe modrally.h)
#include "modrally.h"

#include "lib/framework/frame.h"
#include "lib/framework/vector.h"

#include "baseobject.h"
#include "game_world.h"
#include "map.h"
#include "objmem.h"
#include "structure.h"
#include "warcam.h"

namespace modrally
{
bool orderSelected(unsigned player, int worldX, int worldY)
{
	if (player >= MAX_PLAYERS || !worldOnMap(gameWorld.map, worldX, worldY)) { return false; }
	if (getNumDroidsSelected() > 0) { return false; }               // Fahrzeuge angewaehlt: normale Marschbefehle haben Vorrang
	bool any = false;
	for (STRUCTURE *s : gameWorld.objects.structures[player])
	{
		if (s == nullptr || !s->selected || s->status != SS_BUILT || s->pFunctionality == nullptr) { continue; }
		FLAG_POSITION *flag = nullptr;
		if (s->isFactory())
		{
			flag = s->pFunctionality->factory.psAssemblyPoint;
		}
		else if (s->pStructureType != nullptr && s->pStructureType->type == REF_REPAIR_FACILITY)
		{
			flag = s->pFunctionality->repairFacility.psDeliveryPoint;
		}
		if (flag == nullptr) { continue; }
		// der Sammelpunkt reist mit der Fabrik-Info jedes neuen Fahrzeugs (INITIAL_DROID_ORDERS) - kein eigener Netzwerkabgleich noetig
		setAssemblyPoint(gameWorld, flag, worldX, worldY, player, true);
		any = true;
	}
	return any;
}
}
