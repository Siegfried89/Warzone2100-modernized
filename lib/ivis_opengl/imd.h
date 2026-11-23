// SPDX-License-Identifier: GPL-2.0-or-later

/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2026  Warzone 2100 Project (https://github.com/Warzone2100)

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
#ifndef _imd_
#define _imd_

#include "lib/framework/wzstring.h"
#include "lib/framework/loading_task_fwd.h"
#include <functional>

class ResourceLoadingController;

struct iIMDShape;
struct iIMDBaseShape;
struct iIMDShapeTextures;

#define PIE_NAME				"PIE"  // Pumpkin image export data file
#define PIE_VER				2
#define PIE_FLOAT_VER		3
#define PIE_VER_4			4

#define PIE_MIN_VER			PIE_VER
#define PIE_MAX_VER			4

//*************************************************************************

// PIE model flags
// premultiplied implies additive
#define iV_IMD_NO_ADDITIVE     0x00000001
#define iV_IMD_ADDITIVE        0x00000002
#define iV_IMD_PREMULTIPLIED   0x00000004
// pitch to camera implies roll to camera
#define iV_IMD_ROLL_TO_CAMERA  0x00000010
#define iV_IMD_PITCH_TO_CAMERA 0x00000020
#define iV_IMD_NOSTRETCH       0x00001000
#define iV_IMD_TCMASK          0x00010000
// MOD (20.11.): jede Ebene ist ein eigenes Teil, das in der Anzeige auf dem Boden unter seiner Mitte steht und sich mit dem Hang neigt
// (z. B. fuenf Panzerigel je Feld); die Grenzen des Modells (Spielwerte) umfassen alle Ebenen
#define iV_IMD_MOD_GROUNDLEVELS 0x00100000

// polygon flags	b0..b7: col, b24..b31: anim index

#define iV_IMD_TEX 0x00000200		// this is both a polygon and pie flag
#define iV_IMD_TEXANIM 0x00004000 // iV_IMD_TEX must be set also

//*************************************************************************

void modelShutdown();

void modelUpdateTilesetIdx(size_t tilesetIdx);

// Enumerate over loaded models
void enumerateLoadedModels(const std::function<void (const std::string& modelName, iIMDBaseShape& model)>& func);

LoadingTask<> imdLoadLevelTexturesTask(ResourceLoadingController& controller, const iIMDShape& s, size_t tilesetIdx, iIMDShapeTextures& output);
LoadingTask<> preloadAllModelTexturesTask(ResourceLoadingController& controller);

/// Get filename of model pointer. This is really slow, so do not abuse for logging
/// purposes, for example.
const WzString &modelName(const iIMDShape *model);

iIMDBaseShape *modelGet(const WzString &filename);
/// MOD (23.11. 01:35, Verbundrumpfe): Modell aus PIE-Text im Speicher anlegen oder - falls es den Namen schon gibt - ersetzen
/// (die Basisform bleibt dasselbe Objekt, damit gemerkte Zeiger gueltig bleiben; Masse und Anschluesse werden neu gesetzt)
iIMDBaseShape *modelRegisterFromText(const WzString &filename, const std::string &pieText);
/// MOD (18.11., Absturz 13:25 nach neuem Spiel: zwischengespeicherte Modellzeiger zeigten auf freigegebene Modelle): zaehlt bei jedem
/// modelShutdown() hoch. modelCacheStale(gen) ist true, wenn ein gemerkter Modellzeiger (neu) geholt werden muss.
unsigned modelGeneration();
inline bool modelCacheStale(unsigned &gen)
{
	const unsigned g = modelGeneration();
	if (gen == g) { return false; }
	gen = g;
	return true;
}

void modelReloadAllModelTextures();

bool debugReloadDisplayModelsForBaseModel(iIMDBaseShape& baseModel);
void debugReloadAllDisplayModels();

size_t getModelLoadingErrorCount();
size_t getModelTextureLoadingFailuresCount();

#endif
