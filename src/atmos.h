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

#ifndef __INCLUDED_SRC_ATMOS_H__
#define __INCLUDED_SRC_ATMOS_H__

#include "lib/framework/vector.h"
#include "lib/ivis_opengl/ivisdef.h"

struct ATPART
{
	UBYTE		status;
	UBYTE		type;
	UDWORD		size;
	Vector3f	position;
	Vector3f	velocity;
	iIMDBaseShape	*imd;
};

enum WT_CLASS
{
	WT_RAINING,
	WT_SNOWING,
	WT_NONE
};

struct WorldMapState;

void atmosInitSystem();
void atmosUpdateSystem(WorldMapState& mapState);
void renderParticle(ATPART *psPart, const glm::mat4 &perspectiveViewMatrix);
void atmosDrawParticles(const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix);
void atmosSetWeatherType(WT_CLASS type);
WT_CLASS atmosGetWeatherType();
// MOD: Wetter-Erweiterung (modweather). Die "Grundwetter" (Skript, Taste, Spielstand) bleibt Regen/Schnee/Klar; das Mod-Modul setzt darueber
// nur die aktive Erscheinung (Stil, Dichte, Wind). atmosGetBaseWeatherType() liefert das Grundwetter (wird gespeichert).
enum AtmosStyle
{
	AS_NORMAL,      // wie bisher
	AS_STORM_RAIN,  // Starkregen: dichter, schneller, schraeg
	AS_SAND,        // Sandsturm: waagerechte Sandschlieren dicht ueber dem Boden
	AS_BLIZZARD     // Schneesturm: dichter, schraeger Schnee
};
WT_CLASS atmosGetBaseWeatherType();
/// aktive Erscheinung setzen (aendert das Grundwetter nicht). type = Teilchenart (RAINING/SNOWING/NONE); intensity 0..1.5; wind in Weltkoordinaten/s
void atmosModSet(WT_CLASS type, AtmosStyle style, float intensity, float windX, float windZ);
bool atmosModActive();
// User preference gating the weather particle simulation and rendering,
// independent of the weather type set by the map or scripts
void atmosSetWeatherEnabled(bool enabled);
bool atmosGetWeatherEnabled();

#endif // __INCLUDED_SRC_ATMOS_H__
