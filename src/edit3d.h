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

#ifndef __INCLUDED_SRC_EDIT3D_H__
#define __INCLUDED_SRC_EDIT3D_H__

#include <utility>
#include <vector>

#include "map.h"

#define TILE_RAISE	1
#define TILE_LOWER	-1

struct WorldMapState;

typedef void (*BUILDCALLBACK)(UDWORD xPos, UDWORD yPos, void *UserData);

void Edit3DInitVars();
bool found3DBuilding(Vector2i &pos);
bool found3DBuildLocTwo(Vector2i &pos, Vector2i &pos2);
void init3DBuilding(BASE_STATS *psStats, BUILDCALLBACK CallBack, void *UserData);
void kill3DBuilding();
bool process3DBuilding();
void incrementBuildingDirection(uint16_t amount);
uint16_t getBuildingDirection();

void adjustTileHeight(MAPTILE *psTile, SDWORD adjust);
void raiseTile(WorldMapState& mapState, int tile3dX, int tile3dY);
void lowerTile(WorldMapState& mapState, int tile3dX, int tile3dY);

enum BuildState
{
	BUILD3D_NONE,
	BUILD3D_POS,
	BUILD3D_FINISHED,
	BUILD3D_VALID
};

extern BuildState buildState;

struct HIGHLIGHT
{
	UWORD	xTL, yTL;		// Top left of box to highlight
	UWORD	xBR, yBR;		// Bottom right of box to highlight
};

extern HIGHLIGHT	buildSite;

struct BUILDDETAILS
{
	BUILDCALLBACK	CallBack;
	void         	*UserData;  //this holds the OBJECT_POSITION pointer for a Deliv Point
	UDWORD       	x, y;
	UDWORD       	width, height;
	BASE_STATS   	*psStats;
	uint16_t     	directionShift;
};

extern BUILDDETAILS	sBuildDetails;

// MOD (Wunsch 20.11. 19:23 "Saemtliche Gebaeude sollen nicht nur in einer Linie, sondern auch im Feld platzierbar sein ... fuer JEDES
// Gebaeude" / "Steuerung plus Ziehen macht ein Feld"): Strg + Ziehen beim Platzieren baut ein Rechteck (Zeilen x Spalten) - fuer jedes
// mehrfach baubare Gebaeude. Ziehen ohne Strg bleibt wie bisher (Linie bzw. Drehen). Der Zustand der Strg-Taste wird beim Druecken der
// Maustaste festgehalten, damit ein spaeter gedrueckter/losgelassener Strg den laufenden Zug nicht umschaltet.
bool modFieldBuild();
void modFieldLatch();
/// beim Loslassen eines Feld-Zugs: die weiteren Zeilen (die erste geht als normaler Linienauftrag hinaus); false = keine
bool modTakeFieldRows(std::vector<std::pair<Vector2i, Vector2i>> &rows);

// May only call if sBuildDetails.psStats points to a STRUCTURE_STATS.
static inline bool canLineBuild()
{
	ASSERT_OR_RETURN(false, selectedPlayer < MAX_PLAYERS, "selectedPlayer definitely can't line-build: %" PRIu32 "", selectedPlayer);
	const STRUCTURE_STATS *s = (const STRUCTURE_STATS *)sBuildDetails.psStats;
	if (s->upgrade[selectedPlayer].limit <= 1)
	{
		return false;
	}
	if (modFieldBuild())
	{
		return true;   // MOD (20.11. 19:23): Strg + Ziehen = Feld (auch Linie) fuer jedes Gebaeude
	}
	// MOD (16.11. 23:30 "Mörserstellung: klicken, halten, ziehen ändert die Richtung nicht"): bisher galt jedes mehrfach baubare
	// Gebäude als Reihen-Bau, damit wurde Ziehen nie zum Drehen. Jetzt ziehen nur Mauern/Sperren/Gräben (1 Feld) und Mauertürme
	// eine Reihe; Stellungen, Türme und alle anderen Gebäude dreht das Ziehen.
	if (s->combinesWithWall)
	{
		return true;
	}
	// (18.11.) Strommasten und Minen werden wieder in einer Linie gezogen (Masten mit Abstand, Minen dicht als Minenfeld)
	// (19.11. 17:11 "Solarpanele sollen sich nicht nach drag direction ausrichten sondern wieder in Reihe bauen lassen. Das drehen
	// soll dann automatisch per animation kommen"): Solarpaneele ebenfalls als Reihe; das Paneel schwenkt per MF-Animation (solarfix.py)
	// (Wunsch 23.11. 18:38 "Rail tracks sollen sich ohne Taste Steuerung ziehen lassen, einfach nur ziehen"): Schienen ebenfalls als Reihe
	if (s->id.startsWith("A0PowerPole") || s->id.startsWith("A0Mine") || s->id.startsWith("A0SolarPanel") || s->id.startsWith("A0RailTrack"))
	{
		return true;
	}
	return (s->type == REF_WALL || s->type == REF_WALLCORNER || s->type == REF_GATE)
		&& s->baseWidth <= (UDWORD)(DIST_TILE / TILE_UNITS) && s->baseBreadth <= (UDWORD)(DIST_TILE / TILE_UNITS);
}

extern bool buildRotateDrag;      // MOD: Ziehen beim Platzieren bestimmt die Richtung
extern int buildRotateAnchorX, buildRotateAnchorY;
extern int brushSize;
extern bool quickQueueMode;

/*returns true if the build state is not equal to BUILD3D_NONE*/
bool   tryingToGetLocation();

#endif // __INCLUDED_SRC_EDIT3D_H__
