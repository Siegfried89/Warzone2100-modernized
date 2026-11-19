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
/**
 * @file component.c
 * Draws component objects - oh yes indeed.
*/

#include "lib/framework/frame.h"
#include "lib/framework/trig.h"
#include <unordered_map>
#include <algorithm>
#include <map>
#include <tuple>
#include <cmath>
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/netplay/netplay.h"

#include "action.h"
#include "component.h"
#include "modoil.h"
#include "modimpostor.h"
#include "modtactical.h"
#include "display3d.h"
#include "lib/ivis_opengl/pielighting.h"
#include "warzoneconfig.h"
#include "effects.h"
#include "intdisplay.h"
#include "loop.h"
#include "map.h"
#include "miscimd.h"
#include "projectile.h"
#include "terrain.h"
#include "transporter.h"
#include "mission.h"
#include "faction.h"
#include "modcombot.h"
#include "modlayer.h"
#include "modcrew.h"
#include "modgdiart.h"
#include "modicbm.h"
#include "lib/ivis_opengl/piepalette.h"
#include "modbuild.h"
#include "modduel.h"
#include "modrecoil.h"
#include "modflail.h"   // MOD (17.11. 20:06): Minenraeumer vor dem Bug
#include "modparts.h"
#include "modrecover.h"
#include "featuredef.h"
#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/transform.hpp>

#define GetRadius(x) ((x)->sradius)

#define	DEFAULT_COMPONENT_TRANSLUCENCY	128
#define	DROID_EMP_SPREAD	(20 - rand()%40)

//VTOL weapon connector start
#define VTOL_CONNECTOR_START 5

static bool		leftFirst;

// Colour Lookups
// use col = MAX_PLAYERS for anycolour (see multiint.c)
bool setPlayerColour(UDWORD player, UDWORD col)
{
	ASSERT_OR_RETURN(false, player < NetPlay.players.size(), "Invalid player: %u", player);
	if (player >= MAX_PLAYERS)
	{
		NetPlay.players[player].colour = MAX_PLAYERS;
		return true;
	}
	// Allow color values from 0 to 15 (MOD: and 16 = Wuestentarn)
	ASSERT_OR_RETURN(false, col < MOD_NUM_PLAYER_COLOURS, "Bad colour setting");
	NetPlay.players[player].colour = col;
	return true;
}

UBYTE getPlayerColour(UDWORD pl)
{
	if (pl >= MAX_PLAYERS)
	{
		return 0; // baba, or spectator slot
	}
	return NetPlay.players[pl].colour;
}

static glm::mat4 setMatrix(const Vector3i *Position, const Vector3i *Rotation, int scale)
{
	return glm::translate(glm::vec3(*Position)) *
		glm::rotate(UNDEG(DEG(Rotation->x)), glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(DEG(Rotation->y)), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(DEG(Rotation->z)), glm::vec3(0.f, 0.f, 1.f)) *
		glm::scale(glm::vec3(scale / 100.f));
}

UDWORD getComponentDroidRadius(const DROID *)
{
	return 100;
}


UDWORD getComponentDroidTemplateRadius(const DROID_TEMPLATE *psTemplate)
{
	// MOD (18.11.): grosse Ruempfe (Schiffe, ueberlange/superschwere Ruempfe) ragten aus der Vorschau im Designer - ab einer gewissen
	// Rumpfgroesse wird die Vorschau kleiner gezeichnet (normale Ruempfe bleiben wie bisher)
	if (psTemplate != nullptr && psTemplate->asParts[COMP_BODY] < asBodyStats.size())
	{
		const BODY_STATS *psBody = psTemplate->getBodyStats();
		const iIMDShape *imd = (psBody != nullptr && psBody->pIMD != nullptr) ? psBody->pIMD->displayModel() : nullptr;
		if (imd != nullptr)
		{
			// (18.11. 12:31 "grosse Modelle gehen immer noch ueber das UI hinaus") auch das Fahrwerk zaehlt - beim Zerstoerer ist der
			// Schiffsrumpf das Antriebsmodell (prhnaval3), der Rumpf nur der Aufbau
			auto extOf = [](const iIMDShape *m) {
				int e = 0;
				for (const iIMDShape *l = m; l != nullptr; l = l->next.get())
				{
					e = std::max({e, std::abs(l->min.x), std::abs(l->max.x), std::abs(l->min.z), std::abs(l->max.z), (l->max.y - l->min.y) * 2 / 3});
				}
				return e;
			};
			int ext = extOf(imd);
			const size_t prop = psTemplate->asParts[COMP_PROPULSION];
			for (int side = 0; side < NUM_PROP_SIDES; ++side)
			{
				const size_t k = prop * NUM_PROP_SIDES + static_cast<size_t>(side);
				if (k < psBody->ppIMDList.size() && psBody->ppIMDList[k] != nullptr) { ext = std::max(ext, extOf(psBody->ppIMDList[k]->displayModel())); }
			}
			return static_cast<UDWORD>(std::max(100, ext * 100 / 60));
		}
	}
	return 100;
}


UDWORD getComponentRadius(const BASE_STATS *psComponent) // DISPLAY ONLY
{
	const iIMDShape *ComponentIMD = nullptr;
	const iIMDShape *MountIMD = nullptr;
	SDWORD compID;

	compID = StatIsComponent(psComponent);
	if (compID >= 0)
	{
		StatGetComponentIMD(psComponent, compID, &ComponentIMD, &MountIMD);
		if (ComponentIMD)
		{
			return GetRadius(ComponentIMD);
		}
	}

	/* VTOL bombs are only stats allowed to have NULL ComponentIMD */
	if (StatIsComponent(psComponent) != COMP_WEAPON
	    || (((const WEAPON_STATS *)psComponent)->weaponSubClass != WSC_BOMB
	        && ((const WEAPON_STATS *)psComponent)->weaponSubClass != WSC_EMP))
	{
		ASSERT(ComponentIMD, "No ComponentIMD!");
	}

	return COMPONENT_RADIUS;
}


UDWORD getResearchRadius(const BASE_STATS *Stat) // DISPLAY ONLY
{
	const iIMDShape *ResearchIMD = safeGetDisplayModelFromBase(((const RESEARCH *)Stat)->pIMD);

	if (ResearchIMD)
	{
		return GetRadius(ResearchIMD);
	}

	debug(LOG_ERROR, "ResearchPIE == NULL");

	return 100;
}


UDWORD getStructureSizeMax(const STRUCTURE *psStructure) // DISPLAY ONLY
{
	//radius based on base plate size
	return MAX(psStructure->pStructureType->baseWidth, psStructure->pStructureType->baseBreadth);
}

UDWORD getStructureStatSizeMax(const STRUCTURE_STATS *Stats) // DISPLAY ONLY
{
	//radius based on base plate size
	return MAX(Stats->baseWidth, Stats->baseBreadth);
}

UDWORD getStructureStatHeight(const STRUCTURE_STATS *psStat) // DISPLAY ONLY
{
	if (psStat->pIMD[0])
	{
		const auto pDisplayIMD = psStat->pIMD[0]->displayModel();
		return (pDisplayIMD->max.y - pDisplayIMD->min.y);
	}

	return 0;
}

static void draw_player_3d_shape(uint32_t player_index, const iIMDShape *shape, const glm::mat4 &modelMatrix)
{
	int team = getPlayerColour(player_index);
	for (const iIMDShape *imd = getFactionDisplayIMD(getPlayerFaction(player_index), shape); imd != nullptr; imd = imd->next.get())
	{
		const PIELIGHT teamcolour = imd->getTeamColourForModel(team);
		pie_Draw3DButton(imd, teamcolour, modelMatrix, glm::mat4(1.f));
	}
}

void displayIMDButton(const iIMDShape *IMDShape, const Vector3i *Rotation, const Vector3i *Position, int scale)
{
	draw_player_3d_shape(selectedPlayer, IMDShape, setMatrix(Position, Rotation, scale));
}

static void sharedStructureButton(const STRUCTURE_STATS *Stats, const iIMDBaseShape *strBaseImd, const Vector3i *Rotation, const Vector3i *Position, int scale)
{
	if (!strBaseImd) { return; }

	const iIMDBaseShape *baseImd, *mountImd[MAX_WEAPONS], *weaponImd[MAX_WEAPONS];
	Vector3i pos = *Position;

	const iIMDShape *strImd = strBaseImd->displayModel();

	if (!strImd)
	{
		return;
	}

	/* HACK HACK HACK!
	if its a 'tall thin (ie tower)' structure stat with something on the top - offset the position to show the object on top */
	if (!strImd->connectors.empty() && scale == SMALL_STRUCT_SCALE && getStructureStatHeight(Stats) > TOWER_HEIGHT)
	{
		pos.y -= 20;
	}

	/* MOD: Metal-Fatigue-Gebaeude sind sehr unterschiedlich gross - Icon automatisch auf die Modellgroesse einpassen
	   (Skalierung nach Bounding-Box-Diagonale, senkrecht zentriert), damit nichts aus dem Knopf ragt */
	if (Stats->id.startsWith("MFS_"))
	{
		const float dx = static_cast<float>(strImd->max.x - strImd->min.x);
		const float dy = static_cast<float>(strImd->max.y - strImd->min.y);
		const float dz = static_cast<float>(strImd->max.z - strImd->min.z);
		const float diag = std::max(24.f, std::sqrt(dx * dx + dy * dy + dz * dz));
		scale = std::max(4, std::min(160, static_cast<int>(100.f * 118.f / diag)));
		pos.y -= static_cast<int>((strImd->min.y + strImd->max.y) * 0.5f * scale / 100.f);
	}

	const glm::mat4 matrix = setMatrix(&pos, Rotation, scale);

	/* Draw the building's base first */
	baseImd = Stats->pBaseIMD;

	if (baseImd != nullptr)
	{
		draw_player_3d_shape(selectedPlayer, baseImd->displayModel(), matrix);
	}
	draw_player_3d_shape(selectedPlayer, strImd, matrix);

	//and draw the turret
	if (!strImd->connectors.empty())
	{
		weaponImd[0] = nullptr;
		mountImd[0] = nullptr;
		for (int i = 0; i < Stats->numWeaps; i++)
		{
			weaponImd[i] = nullptr;//weapon is gun ecm or sensor
			mountImd[i] = nullptr;
		}
		//get an imd to draw on the connector priority is weapon, ECM, sensor
		//check for weapon
		//can only have the MAX_WEAPONS
		for (int i = 0; i < MAX(1, Stats->numWeaps); i++)
		{
			//can only have the one
			if (Stats->psWeapStat[i] != nullptr)
			{
				weaponImd[i] = Stats->psWeapStat[i]->pIMD;
				mountImd[i] = Stats->psWeapStat[i]->pMountGraphic;
			}

			if (weaponImd[i] == nullptr)
			{
				//check for ECM
				if (Stats->pECM != nullptr)
				{
					weaponImd[i] =  Stats->pECM->pIMD;
					mountImd[i] =  Stats->pECM->pMountGraphic;
				}
			}

			if (weaponImd[i] == nullptr)
			{
				//check for sensor
				if (Stats->pSensor != nullptr)
				{
					weaponImd[i] =  Stats->pSensor->pIMD;
					mountImd[i]  =  Stats->pSensor->pMountGraphic;
				}
			}
		}

		//draw Weapon/ECM/Sensor for structure
		if (weaponImd[0] != nullptr)
		{
			for (uint32_t i = 0; i < MAX(1, Stats->numWeaps); i++)
			{
				if (i >= strImd->connectors.size())
				{
					break;
				}
				glm::mat4 localMatrix = glm::translate(glm::vec3(strImd->connectors[i].xzy()));
				if (mountImd[i] != nullptr)
				{
					auto displayModel = mountImd[i]->displayModel();
					draw_player_3d_shape(selectedPlayer, displayModel, matrix * localMatrix);
					if (!displayModel->connectors.empty())
					{
						localMatrix *= glm::translate(glm::vec3(displayModel->connectors[0].xzy()));
					}
				}
				draw_player_3d_shape(selectedPlayer, weaponImd[i]->displayModel(), matrix * localMatrix);
				//we have a droid weapon so do we draw a muzzle flash
			}
		}
	}
}

void displayStructureButton(const STRUCTURE *psStructure, const Vector3i *rotation, const Vector3i *Position, int scale)
{
	sharedStructureButton(psStructure->pStructureType, psStructure->sDisplay.imd, rotation, Position, scale);
}

void displayStructureStatButton(const STRUCTURE_STATS *Stats, const Vector3i *rotation, const Vector3i *Position, int scale)
{
	ASSERT_OR_RETURN(, Stats != nullptr, "Stats is NULL");
	if (Stats->pIMD.empty() || Stats->pIMD[0] == nullptr)
	{
		return; // error with loaded stats/models - probably due to a mod
	}
	sharedStructureButton(Stats, Stats->pIMD[0], rotation, Position, scale);
}

// Render a component given a BASE_STATS structure.
//
void displayComponentButton(const BASE_STATS *Stat, const Vector3i *Rotation, const Vector3i *Position, int scale)
{
	const iIMDShape *ComponentIMD = nullptr;
	const iIMDShape *MountIMD = nullptr;
	int compID = StatIsComponent(Stat);

	if (compID >= 0)
	{
		StatGetComponentIMD(Stat, compID, &ComponentIMD, &MountIMD);
	}
	else
	{
		return;
	}
	glm::mat4 matrix = setMatrix(Position, Rotation, scale);

	/* VTOL bombs are only stats allowed to have NULL ComponentIMD */
	if (StatIsComponent(Stat) != COMP_WEAPON
	    || (((const WEAPON_STATS *)Stat)->weaponSubClass != WSC_BOMB
	        && ((const WEAPON_STATS *)Stat)->weaponSubClass != WSC_EMP))
	{
		ASSERT(ComponentIMD, "No ComponentIMD");
	}

	if (MountIMD)
	{
		draw_player_3d_shape(selectedPlayer, MountIMD, matrix);

		/* translate for weapon mount point */
		if (!MountIMD->connectors.empty())
		{
			matrix *= glm::translate(glm::vec3(MountIMD->connectors[0].xzy()));
		}
	}
	if (ComponentIMD)
	{
		draw_player_3d_shape(selectedPlayer, ComponentIMD, matrix);
	}
}


// Render a research item given a BASE_STATS structure.
//
void displayResearchButton(const BASE_STATS *Stat, const Vector3i *Rotation, const Vector3i *Position, int scale)
{
	ASSERT_OR_RETURN(, Stat != nullptr, "Stat is NULL");
	const iIMDBaseShape *ResearchIMD = ((const RESEARCH *)Stat)->pIMD;
	const iIMDBaseShape *MountIMD = ((const RESEARCH *)Stat)->pIMD2;

	ASSERT(ResearchIMD, "ResearchIMD is NULL");
	if (ResearchIMD)
	{
		const glm::mat4 &matrix = setMatrix(Position, Rotation, scale);

		if (MountIMD)
		{
			draw_player_3d_shape(selectedPlayer, MountIMD->displayModel(), matrix);
		}
		draw_player_3d_shape(selectedPlayer, ResearchIMD->displayModel(), matrix);
	}
}


static inline const iIMDBaseShape *getLeftPropulsionIMD(const DROID *psDroid)
{
	int bodyStat = psDroid->asBits[COMP_BODY];
	int propStat = psDroid->asBits[COMP_PROPULSION];
	return asBodyStats[bodyStat].ppIMDList[propStat * NUM_PROP_SIDES + LEFT_PROP];
}

static inline const iIMDBaseShape *getRightPropulsionIMD(const DROID *psDroid)
{
	int bodyStat = psDroid->asBits[COMP_BODY];
	int propStat = psDroid->asBits[COMP_PROPULSION];
	return asBodyStats[bodyStat].ppIMDList[propStat * NUM_PROP_SIDES + RIGHT_PROP];
}

// MOD (18.11.): Lage der beiden Laufwerke quer zur Fahrtrichtung (Modell-x: Mitte und Breite links/rechts) fuer die Fahrspuren (modtracks)
// (19.11. 17:06 "Die Kettenspuren sind zu klein und passen nicht zu den Kettengroessen"): Die WZ-Daten haben fuer beide Seiten EIN
// gemeinsames Laufwerk-Modell (Schluessel "left", "right" leer) - bisher fiel deshalb jedes Fahrzeug auf eine Schaetzung aus dem Rumpf
// zurueck (schmale Spur weit innen; beim Fatboy mit vier Kettensystemen besonders falsch). Jetzt: wie viele Flaechen des Laufwerk-Modells
// (alle Ebenen) jeden halben x-Streifen bedecken; Streifen mit mindestens 20 % der staerksten Bedeckung bilden Gruppen, die aeusserste
// Gruppe links und rechts = Lage und Breite der Ketten/Raeder (durchlaufende Querstreben zaehlen kaum). Ohne eigenes Laufwerk
// (Scav-Fahrzeuge): dasselbe mit den bodennahen Flaechen des Rumpfs, erst danach die alte Schaetzung.
static bool modSplitDrives(const iIMDShape *sh, bool lowOnly, float &lc, float &lw, float &rc, float &rw)
{
	if (sh == nullptr) { return false; }
	float y0 = 1e9f, y1 = -1e9f;
	for (const iIMDShape *lv = sh; lv != nullptr; lv = lv->next.get())
	{
		for (const Vector3f &p : lv->points) { y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
	}
	if (y1 < y0) { return false; }
	const float yCut = lowOnly ? y0 + 0.3f * (y1 - y0) : 1e9f;
	std::vector<std::pair<float, float>> iv;
	float xmin = 1e9f, xmax = -1e9f;
	for (const iIMDShape *lv = sh; lv != nullptr; lv = lv->next.get())
	{
		for (const iIMDPoly &poly : lv->polys)
		{
			float a = 1e9f, b = -1e9f, ylo = 1e9f;
			bool ok = true;
			for (uint32_t k : poly.pindex)
			{
				if (k >= lv->points.size()) { ok = false; break; }
				const Vector3f &p = lv->points[k];
				a = std::min(a, p.x); b = std::max(b, p.x); ylo = std::min(ylo, p.y);
			}
			if (!ok || ylo > yCut) { continue; }
			iv.emplace_back(a, b);
			xmin = std::min(xmin, a); xmax = std::max(xmax, b);
		}
	}
	const float span = xmax - xmin;
	if (iv.size() < 4 || span < 4.f || span > 2000.f) { return false; }
	constexpr float BIN = 0.5f;
	const int nb = static_cast<int>(std::ceil(span / BIN)) + 1;
	std::vector<int> cnt(static_cast<size_t>(nb), 0);
	for (const auto &ab : iv)
	{
		const int i0 = std::clamp(static_cast<int>(std::floor((ab.first - xmin) / BIN)), 0, nb - 1);
		const int i1 = std::clamp(static_cast<int>(std::ceil((ab.second - xmin) / BIN)), i0 + 1, nb);
		for (int i = i0; i < i1; ++i) { ++cnt[static_cast<size_t>(i)]; }
	}
	const int peak = *std::max_element(cnt.begin(), cnt.end());
	const int thr = std::max(1, (peak + 4) / 5);
	int la = -1, lb = -1, ra = -1, rb = -1, groups = 0;
	for (int i = 0; i < nb;)
	{
		if (cnt[static_cast<size_t>(i)] < thr) { ++i; continue; }
		int j = i;
		while (j + 1 < nb && cnt[static_cast<size_t>(j + 1)] >= thr) { ++j; }
		if (groups == 0) { la = i; lb = j; }
		ra = i; rb = j;
		++groups;
		i = j + 1;
	}
	if (groups < 2) { return false; }
	const float L0 = xmin + la * BIN, L1 = xmin + (lb + 1) * BIN, R0 = xmin + ra * BIN, R1 = xmin + (rb + 1) * BIN;
	// eine Gruppe breiter als 40 % des Ganzen ist kein Laufwerk mehr (Rumpfboden); beide Seiten etwa gleich weit von der Mitte
	if (L1 - L0 > 0.4f * span || R1 - R0 > 0.4f * span || L1 - L0 < 1.f || R1 - R0 < 1.f) { return false; }
	if (std::abs((L0 + L1) + (R0 + R1)) * 0.5f > 0.25f * span) { return false; }
	lc = 0.5f * (L0 + L1); lw = L1 - L0;
	rc = 0.5f * (R0 + R1); rw = R1 - R0;
	return true;
}

namespace
{
struct ModDriveSpan { bool ok; float lc, lw, rc, rw; };
}

bool modDroidPropulsionSpan(const DROID *psDroid, float &leftCenter, float &leftWidth, float &rightCenter, float &rightWidth)
{
	if (psDroid == nullptr || psDroid->asBits[COMP_BODY] >= asBodyStats.size()) { return false; }
	if (static_cast<size_t>(psDroid->asBits[COMP_PROPULSION]) * NUM_PROP_SIDES + RIGHT_PROP >= asBodyStats[psDroid->asBits[COMP_BODY]].ppIMDList.size()) { return false; }
	auto disp = [](const iIMDBaseShape *b) -> const iIMDShape * { return b != nullptr ? b->displayModel() : nullptr; };
	const iIMDShape *L = disp(getLeftPropulsionIMD(psDroid));
	const iIMDShape *R = disp(getRightPropulsionIMD(psDroid));
	const BODY_STATS *psBody = psDroid->getBodyStats();
	const iIMDShape *B = (psBody != nullptr && psBody->pIMD != nullptr) ? psBody->pIMD->displayModel() : nullptr;
	// Ergebnis je Modell-Kombination merken (die Flaechen-Auswertung laeuft so nur einmal je Fahrzeugtyp); neu, wenn Modelle neu geladen werden
	static std::map<std::tuple<const void *, const void *, const void *>, ModDriveSpan> s_cache;
	static unsigned s_gen = 0;
	if (modelCacheStale(s_gen)) { s_cache.clear(); }
	const auto key = std::make_tuple(static_cast<const void *>(L), static_cast<const void *>(R), static_cast<const void *>(B));
	auto it = s_cache.find(key);
	if (it == s_cache.end())
	{
		ModDriveSpan r{ false, 0.f, 0.f, 0.f, 0.f };
		auto span = [](const iIMDShape *sh, float &c, float &w) -> bool {
			if (sh == nullptr) { return false; }
			c = 0.5f * (static_cast<float>(sh->min.x) + static_cast<float>(sh->max.x));
			w = static_cast<float>(sh->max.x) - static_cast<float>(sh->min.x);
			return w > 0.5f;
		};
		if (L != nullptr && R != nullptr && L != R)
		{
			// aeltere Daten: je Seite ein eigenes Modell (keins reicht ueber die Mitte)
			const bool l = span(L, r.lc, r.lw), rr = span(R, r.rc, r.rw);
			r.ok = l && rr && !(L->min.x < 0 && L->max.x > 0) && !(R->min.x < 0 && R->max.x > 0);
		}
		// ein gemeinsames Modell fuer beide Seiten: Laufwerke aus den Flaechen heraussuchen
		if (!r.ok) { r.ok = modSplitDrives(L, false, r.lc, r.lw, r.rc, r.rw); }
		if (!r.ok) { r.ok = modSplitDrives(R, false, r.lc, r.lw, r.rc, r.rw); }
		// Scav-Fahrzeuge u. a.: Raeder/Ketten sind Teil des Rumpf-Modells - bodennahe Rumpfflaechen, sonst Schaetzung unter den Rumpfkanten
		if (!r.ok) { r.ok = modSplitDrives(B, true, r.lc, r.lw, r.rc, r.rw); }
		if (!r.ok && B != nullptr)
		{
			const float ex = std::max(std::abs(static_cast<float>(B->min.x)), std::abs(static_cast<float>(B->max.x)));
			if (ex >= 2.f)
			{
				r.lc = -0.74f * ex; r.rc = 0.74f * ex;
				r.lw = r.rw = 0.24f * ex;
				r.ok = true;
			}
		}
		it = s_cache.emplace(key, r).first;
	}
	if (!it->second.ok) { return false; }
	leftCenter = it->second.lc; leftWidth = it->second.lw;
	rightCenter = it->second.rc; rightWidth = it->second.rw;
	return true;
}

// MOD (19.11. "Die Walkeranimation von allen Mechs soll realistisch sein"): Laufzyklus nach der zurueckgelegten Strecke statt nach der Zeit.
// Rumpf mit "gaitStride" (body.json, Weg je Schrittzyklus): der Kilometerzaehler der Einheit bestimmt das Bild - die Fuesse stehen in
// der Standphase still auf dem Boden, egal wie schnell die Einheit gerade laeuft (Gelaende, Steigung, Anfahren). Nur Anzeige.
namespace
{
struct ModGait { int32_t x = 0, y = 0; double odo = 0.0; uint32_t seen = 0; };
std::unordered_map<uint32_t, ModGait> s_modGait;
uint32_t s_modGaitPrune = 0;
}
static UDWORD modGaitAnimStart(const DROID *psDroid, const iIMDShape *animShape, UDWORD fallback)
{
	const BODY_STATS *body = psDroid->getBodyStats();
	if (body == nullptr || body->gaitStride <= 0 || animShape == nullptr || animShape->objanimframes <= 0 || animShape->objanimtime <= 0)
	{
		return fallback;
	}
	ModGait &g = s_modGait[psDroid->id];
	if (g.seen != graphicsTime)
	{
		const int64_t dx = psDroid->pos.x - g.x, dy = psDroid->pos.y - g.y;
		const double step = std::sqrt(static_cast<double>(dx * dx + dy * dy));
		if (g.seen != 0 && step < 3.0 * body->gaitStride) { g.odo += step; }   // Spruenge (Transport, Laden) zaehlen nicht
		g.x = psDroid->pos.x; g.y = psDroid->pos.y; g.seen = graphicsTime;
	}
	if (graphicsTime - s_modGaitPrune > 10000)
	{
		s_modGaitPrune = graphicsTime;
		for (auto it = s_modGait.begin(); it != s_modGait.end();) { if (graphicsTime - it->second.seen > 10000) { it = s_modGait.erase(it); } else { ++it; } }
	}
	const double cycleMs = static_cast<double>(animShape->objanimtime) * animShape->objanimframes;
	const double phase = std::fmod(s_modGait[psDroid->id].odo / body->gaitStride, 1.0);
	return graphicsTime - static_cast<UDWORD>(phase * cycleMs);
}

// MOD (19.11. "Die Walkeranimation von allen Mechs soll realistisch sein"): Blaetter-Animation (je Bild eine Ebene, ANIMOBJECT blendet die
// Ebenen der Reihe nach ein): die gerade sichtbare Ebene. Hat sie eigene Anschlusspunkte, sitzen Torso/Waffen daran - so wippt der
// Oberkoerper eines Mechs mit jedem Schritt mit (Becken senkt sich beim Auftreten, hebt sich in der Standphase).
static const iIMDShape *modVisibleFlipLevel(const iIMDShape *shape, UDWORD timeAnimationStarted)
{
	for (const iIMDShape *lvl = shape; lvl != nullptr; lvl = lvl->next.get())
	{
		if (lvl->objanimframes <= 0 || lvl->objanimtime <= 0 || lvl->objanimdata.empty()) { return nullptr; }
		int elapsed = static_cast<int>(graphicsTime - timeAnimationStarted);
		if (elapsed < 0) { elapsed = 0; }
		const size_t frame = static_cast<size_t>((elapsed / lvl->objanimtime) % lvl->objanimframes);
		if (frame < lvl->objanimdata.size() && lvl->objanimdata[frame].scale.x != -1.0f) { return lvl; }
	}
	return nullptr;
}

int modDroidFootprintRadius(const DROID *psDroid)
{
	if (psDroid == nullptr) { return 0; }
	float ex = 0.f, ez = 0.f;
	auto take = [&](const iIMDBaseShape *b) {
		if (b == nullptr) { return; }
		const iIMDShape *sh = b->displayModel();
		if (sh == nullptr) { return; }
		ex = std::max({ex, std::abs(static_cast<float>(sh->min.x)), std::abs(static_cast<float>(sh->max.x))});
		ez = std::max({ez, std::abs(static_cast<float>(sh->min.z)), std::abs(static_cast<float>(sh->max.z))});
	};
	const BODY_STATS *psBody = psDroid->getBodyStats();
	if (psBody != nullptr) { take(psBody->pIMD); }
	if (psDroid->asBits[COMP_BODY] < asBodyStats.size()
	    && static_cast<size_t>(psDroid->asBits[COMP_PROPULSION]) * NUM_PROP_SIDES + RIGHT_PROP < asBodyStats[psDroid->asBits[COMP_BODY]].ppIMDList.size())
	{
		take(getLeftPropulsionIMD(psDroid));
		take(getRightPropulsionIMD(psDroid));
	}
	if (psDroid->droidType == DROID_CONSTRUCT || psDroid->droidType == DROID_CYBORG_CONSTRUCT)
	{
		const CONSTRUCT_STATS *cs = psDroid->getConstructStats();
		if (cs != nullptr) { take(cs->pIMD); }
	}
	return static_cast<int>(std::hypot(ex, ez));
}

//! Whether a weapon's muzzle graphic stands for a single shot, which is what the light below
//! reads as. The commander and NEXUS link turrets aim a continuous beam and fire again every
//! few frames, so a light for each shot would sit there flickering rather than punch out once.
//! Their graphic stays, as it always has, since only the light is distracting at that rate.
static bool muzzleFlashIsAnImpulse(WEAPON_SUBCLASS weaponSubClass)
{
	return weaponSubClass != WSC_COMMAND && weaponSubClass != WSC_ELECTRONIC;
}

//! A muzzle flash throws light forward down the barrel.
//! Requires directional point lights (the lightmap has no notion of a cone, and would just do an even spread).
static void addMuzzleFlashLight(const glm::mat4& muzzleModelMatrix, const iIMDShape *flashImd, WEAPON_SUBCLASS weaponSubClass, float heightAboveTerrain, float age)
{
	if (!war_getMuzzleFlashLighting() || !war_getPointLightPerPixelLighting() || getTerrainShaderQuality() != TerrainShaderQuality::NORMAL_MAPPING)
	{
		return;
	}

	if (!muzzleFlashIsAnImpulse(weaponSubClass))
	{
		return;
	}

	// A flash is an impulse, brightest at ignition and gone almost at once, so it finishes well before the graphic does.
	constexpr float lightLifeFraction = 0.4f;
	const float lightAge = age / lightLifeFraction;
	if (lightAge >= 1.f)
	{
		return;
	}

	// The matrix places the barrel in render space, where z runs opposite to the game's y, so both
	// the position and the direction go back through that flip.
	const glm::vec3 renderPosition = glm::vec3(muzzleModelMatrix * glm::vec4(0.f, 0.f, 0.f, 1.f));
	// Recoil translates along +z to rock the weapon backwards, so the barrel points down -z
	const glm::vec3 renderDirection = glm::normalize(glm::mat3(muzzleModelMatrix) * glm::vec3(0.f, 0.f, -1.f));

	LIGHT light;
	light.position = Vector3i(static_cast<int>(renderPosition.x), static_cast<int>(renderPosition.y), static_cast<int>(-renderPosition.z));
	light.direction = Vector3f(renderDirection.x, renderDirection.y, -renderDirection.z);
	// The gases expand as they cool, so the cone opens out while it dies.
	// Only the last, much darker frames are wide - which resembles a burst spreading (instead of a wide beam).
	light.cosOuter = glm::mix(0.58f, 0.32f, lightAge); // approx 55 degrees opening to 71
	// The flash graphic already says how big the flash is, so take the light's reach and brightness from it.
	// A light machinegun's flash model is about a quarter the size of a cannon's and should light about that much less.
	// (Clamped so an unusual model cannot produce something too absurd, hopefully.)
	constexpr float referenceFlashRadius = 32.f; // a cannon, where the values here were set
	const float rawScale = glm::clamp(static_cast<float>(flashImd->radius) / referenceFlashRadius, 0.25f, 2.5f);
	// Above the reference the flash models grow far faster than the light they should throw, so
	// only a fraction of the excess counts
	const float flashScale = std::min(rawScale, 1.f + (rawScale - 1.f) * 0.35f);
	// The cone points down the barrel, so from a tower it passes over the ground while from a cyborg's shoulder it rakes along the ground it is standing on.
	// The same flash graphic serves both, so the height is the only thing that tells them apart.
	constexpr float shoulderHeight = 32.f;
	constexpr float clearOfTheGround = 64.f;
	const float mountHeight = glm::clamp((heightAboveTerrain - shoulderHeight) / (clearOfTheGround - shoulderHeight), 0.f, 1.f);
	const float heightTaper = glm::mix(0.6f, 1.f, mountHeight);
	light.range = static_cast<UDWORD>(440.f * flashScale * heightTaper);
	// Burning gas cools as it expands, so the flash runs from a warm white at ignition down through orange as it dies.
	// The cooling finishes at four tenths of the light's life because brightness falls as the fourth power of what
	// remains, which leaves the tail too dark to carry a color.
	constexpr float coolingFraction = 0.4f;
	const float cooling = glm::min(lightAge / coolingFraction, 1.f);
	const glm::vec3 flashColor = glm::mix(glm::vec3(255.f, 224.f, 176.f), glm::vec3(255.f, 150.f, 64.f), cooling);
	light.colour = pal_Colour(static_cast<UBYTE>(flashColor.r), static_cast<UBYTE>(flashColor.g), static_cast<UBYTE>(flashColor.b));
	const float remaining = 1.f - lightAge;
	const float remainingSq = remaining * remaining;
	light.intensity = 3.2f * flashScale * heightTaper * remainingSq * remainingSq;
	getCurrentLightingData().lights.push_back(light);
}

void drawMuzzleFlash(WEAPON sWeap, const iIMDShape *weaponImd, const iIMDShape *flashImd, PIELIGHT buildingBrightness, int pieFlag, int iPieData, glm::mat4 modelMatrix, const glm::mat4 &viewMatrix, float heightAboveTerrain, UBYTE colour)
{
	if (!weaponImd || !flashImd || weaponImd->connectors.empty() || sWeap.lastFired == 0 || graphicsTime < sWeap.lastFired)
	{
		return;
	}

	int connector_num = 0;

	// which barrel is firing if model have multiple muzzle connectors?
	if (sWeap.shotsFired && (weaponImd->connectors.size() > 1))
	{
		// shoot first, draw later - substract one shot to get correct results
		connector_num = (sWeap.shotsFired - 1) % (static_cast<uint32_t>(weaponImd->connectors.size()));
	}

	/* Now we need to move to the end of the firing barrel */
	modelMatrix *= glm::translate(glm::vec3(weaponImd->connectors[connector_num].xzy()));
	heightAboveTerrain += weaponImd->connectors[connector_num].z;

	// assume no clan colours for muzzle effects
	if (flashImd->numFrames == 0 || flashImd->animInterval <= 0)
	{
		// no anim so display one frame for a fixed time
		if (graphicsTime >= sWeap.lastFired && graphicsTime < sWeap.lastFired + BASE_MUZZLE_FLASH_DURATION)
		{
			pie_Draw3DShape(flashImd, 0, colour, buildingBrightness, pieFlag | pie_ADDITIVE, EFFECT_MUZZLE_ADDITIVE, modelMatrix, viewMatrix, -heightAboveTerrain);
			addMuzzleFlashLight(modelMatrix, flashImd, asWeaponStats[sWeap.nStat].weaponSubClass, heightAboveTerrain, static_cast<float>(graphicsTime - sWeap.lastFired) / static_cast<float>(BASE_MUZZLE_FLASH_DURATION));
		}
	}
	else if (graphicsTime >= sWeap.lastFired)
	{
		// animated muzzle
		const int DEFAULT_ANIM_INTERVAL = 17; // A lot of PIE files specify 1, which is too small, so set something bigger as a fallback
		int animRate = MAX(flashImd->animInterval, DEFAULT_ANIM_INTERVAL);
		int frame = (graphicsTime - sWeap.lastFired) / animRate;
		if (frame < flashImd->numFrames)
		{
			pie_Draw3DShape(flashImd, frame, colour, buildingBrightness, pieFlag | pie_ADDITIVE, EFFECT_MUZZLE_ADDITIVE, modelMatrix, viewMatrix, -heightAboveTerrain);
			addMuzzleFlashLight(modelMatrix, flashImd, asWeaponStats[sWeap.nStat].weaponSubClass, heightAboveTerrain, static_cast<float>(graphicsTime - sWeap.lastFired) / static_cast<float>(flashImd->numFrames * animRate));
		}
	}
}

/* Assumes matrix context is already set */
// this is able to handle multiple weapon graphics now
// removed mountRotation,they get such stuff from psObj directly now
// MOD (Wunsch 14.11. Punkt 4): Scav-MG-Schuetzen zielen beim Feuern ueber Kimme und Korn (Animation "<Name>-aim.pie" neben der
// Schuss-Animation, vom Mod erzeugt). Das schwere MG (BabaHeavyMG) und alle anderen Waffen behalten die Animation aus der Huefte.
static const iIMDBaseShape *modAimVariant(const DROID *psDroid, const iIMDBaseShape *fireShape)
{
	if (fireShape == nullptr || psDroid->numWeaps == 0) { return fireShape; }
	const WEAPON_STATS *w = psDroid->getWeaponStats(0);
	if (w == nullptr || w->weaponSubClass != WSC_MGUN || w->id.compare("BabaHeavyMG") == 0) { return fireShape; }
	std::string name = fireShape->filename.toStdString();
	const size_t dot = name.rfind(".pie");
	if (dot == std::string::npos) { return fireShape; }
	name.insert(dot, "-aim");
	const iIMDBaseShape *aim = modelGet(WzString::fromUtf8(name));
	return aim ? aim : fireShape;
}

// MOD (Frage 94, 17.11.): laufende Ketten - die Ketten-Modelle haben Textur-Animation (tracksanim.py, 16 Bilder je Glied); das Bild
// folgt der gefahrenen Strecke (vorwaerts/rueckwaerts), Stillstand = Glieder stehen. Nur Anzeige (Bildzeit, kein Spielzustand).
static int modTrackFrame(const DROID *psDroid, const iIMDShape *shape, bool bButton)
{
	if (bButton || shape == nullptr || shape->numFrames <= 1) { return 0; }
	struct Odo { Vector2i last; float acc; unsigned seen; };
	static std::unordered_map<uint32_t, Odo> s_odo;
	auto it = s_odo.find(psDroid->id);
	if (it == s_odo.end())
	{
		if (s_odo.size() > 4096) { s_odo.clear(); }
		it = s_odo.emplace(psDroid->id, Odo{psDroid->pos.xy(), 0.f, graphicsTime}).first;
	}
	Odo &o = it->second;
	if (o.seen != graphicsTime)
	{
		const Vector2i d = psDroid->pos.xy() - o.last;
		if (std::abs(d.x) + std::abs(d.y) < 384)   // Spruenge (Laden, Ebenenwechsel) zaehlen nicht
		{
			// Strecke in Fahrtrichtung (rueckwaerts laufen die Glieder andersherum)
			const float fx = static_cast<float>(iSin(psDroid->rot.direction)) / 65536.f;
			const float fy = static_cast<float>(iCos(psDroid->rot.direction)) / 65536.f;
			o.acc += static_cast<float>(d.x) * fx + static_cast<float>(d.y) * fy;
		}
		o.last = psDroid->pos.xy();
		o.seen = graphicsTime;
	}
	constexpr float STEP = 2.5f;   // Weltmass je Bild (1/16 Glied): ruhige, gut sichtbare Bewegung ohne Stroboskop-Effekt
	const int n = shape->numFrames;
	const int f = static_cast<int>(std::floor(o.acc / STEP));
	return ((f % n) + n) % n;
}

static bool displayCompObj(const DROID *psDroid, bool bButton, const glm::mat4& modelMatrix2, const glm::mat4 &viewMatrix)
{
	iIMDBaseShape *psMoveAnim, *psStillAnim;
	SDWORD				iConnector;
	PROPULSION_STATS	*psPropStats;
	SDWORD				pieFlag, iPieData;
	SDWORD				shieldPieFlag = 0, iShieldPieData = 0;
	PIELIGHT			brightness;
	UDWORD				colour;
	size_t	i = 0;
	bool				didDrawSomething = false;

	if (!bButton && psDroid->shieldPoints > 0 && droidGetMaxShieldPoints(psDroid) > 0)
	{
		float factor = static_cast<float>(psDroid->shieldPoints) / droidGetMaxShieldPoints(psDroid);
		iShieldPieData = static_cast<SDWORD>(std::round(255.0f * factor));
		shieldPieFlag = pie_FORCELIGHT | pie_TRANSLUCENT | pie_SHIELD;
	}

	glm::mat4 modifiedModelMatrix = modelMatrix2;

	if (psDroid->timeLastHit - graphicsTime < ELEC_DAMAGE_DURATION && psDroid->lastHitWeapon == WSC_ELECTRONIC && !gamePaused())
	{
		colour = getPlayerColour(rand() % MAX_PLAYERS);
	}
	else
	{
		colour = getPlayerColour(psDroid->player);
	}

	/* get propulsion stats */
	psPropStats = psDroid->getPropulsionStats();
	ASSERT_OR_RETURN(didDrawSomething, psPropStats != nullptr, "invalid propulsion stats pointer");

	//set pieflag for button object or ingame object
	if (bButton)
	{
		pieFlag = pie_BUTTON;
		brightness = WZCOL_WHITE;
	}
	else
	{
		pieFlag = pie_SHADOW;
		brightness = pal_SetBrightness(psDroid->illumination);
		// NOTE: Beware of transporters that are offscreen, on a mission!  We should *not* be checking tiles at this point in time!
		if (!psDroid->isTransporter() && !missionIsOffworld())
		{
			MAPTILE *psTile = worldTile(gameWorld.map, psDroid->pos.x, psDroid->pos.y);
			if (psTile->jammerBits & alliancebits[psDroid->player])
			{
				pieFlag |= pie_ECM;
			}
		}
	}

	/* set default components transparent */
	if (psDroid->asBits[COMP_PROPULSION] == 0)
	{
		pieFlag  |= pie_TRANSLUCENT;
		iPieData  = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else
	{
		iPieData = 0;
	}

	if (!bButton && psPropStats->propulsionType == PROPULSION_TYPE_PROPELLOR)
	{
		// MOD (17.11., NTW-Schiffe): Schiffe liegen mit wenig Tiefgang im Wasser (Original: 1/2,3 Feld tief = U-Boot)
		modifiedModelMatrix *= glm::translate(glm::vec3(0.f, -9.f, 0.f));
	}

	// MOD: modulare Combots (Torso/Beine/Arme) werden Teil fuer Teil mit den Original-MF-Animationen zusammengesetzt
	if (modcombot::isModular(psDroid))
	{
		if (!bButton && modcrew::crewless(psDroid))
		{
			// (18.11., MF-Besatzung) Combot ohne Besatzung: dunkel wie abgeschaltet - so findet man ihn zum Besteigen
			brightness.byte.r = static_cast<uint8_t>(brightness.byte.r * 45 / 100);
			brightness.byte.g = static_cast<uint8_t>(brightness.byte.g * 45 / 100);
			brightness.byte.b = static_cast<uint8_t>(brightness.byte.b * 45 / 100);
		}
		return modcombot::draw(const_cast<DROID *>(psDroid), bButton, modifiedModelMatrix, viewMatrix, static_cast<int>(colour), brightness, pieFlag, iPieData);
	}

	// MOD (Wunsch 17.11. 18:48): taktische Symbole eingeschaltet und weit genug herausgezoomt - statt des Modells zeichnet modtactical
	// das Symbol ueber das Bild
	if (!bButton && modtactical::active())
	{
		return true;
	}
	// MOD (Wunsch 17.11. 18:02): weit herausgezoomt erscheinen alle Einheiten als Sprite (Detailstufe wie in Rome Total War)
	if (!bButton && !(pieFlag & pie_TRANSLUCENT) && modimpostor::draw(psDroid, modifiedModelMatrix, viewMatrix, static_cast<int>(colour), brightness))
	{
		return true;
	}

	// MOD: Hover-Fahrzeuge schweben und wippen leicht (nur Anzeige)
	{
		glm::mat4 hoverMat(1.f);
		if (!bButton && modrecoil::hover(psDroid, graphicsTime, hoverMat)) { modifiedModelMatrix *= hoverMat; }
		if (!bButton && modrecoil::duck(psDroid, graphicsTime, hoverMat)) { modifiedModelMatrix *= hoverMat; }   // MOD: Infanterie unter Beschuss geduckt
		if (!bButton && modparts::slump(psDroid, hoverMat)) { modifiedModelMatrix *= hoverMat; }   // MOD: Laufwerk zerstoert - Fahrzeug sackt ab
	}
	// MOD: Rueckstoss-Wackeln - Ketten/Beine und Rumpf (mit Turm und Waffen) schwingen nach jedem Schuss getrennt nach
	glm::mat4 propMatrix = modifiedModelMatrix;
	glm::mat4 hullShake(1.f), propShake(1.f);
	const bool shaking = !bButton && modrecoil::shake(psDroid, graphicsTime, hullShake, propShake);
	if (shaking) { propMatrix = modifiedModelMatrix * propShake; }
	const iIMDBaseShape *psShapeProp = (leftFirst ? getLeftPropulsionIMD(psDroid) : getRightPropulsionIMD(psDroid));
	if (psShapeProp)
	{
		if (pie_Draw3DShape(psShapeProp->displayModel(), modTrackFrame(psDroid, psShapeProp->displayModel(), bButton), colour, brightness, pieFlag, iPieData, propMatrix, viewMatrix, -(psDroid->heightAboveMap)))
		{
			didDrawSomething = true;
		}
		if (!bButton && psDroid->shieldPoints > 0)
		{
			if (pie_Draw3DShape(psShapeProp->displayModel(), modTrackFrame(psDroid, psShapeProp->displayModel(), bButton), colour, brightness, shieldPieFlag, iShieldPieData, propMatrix, viewMatrix, -(psDroid->heightAboveMap)))
			{
				didDrawSomething = true;
			}
		}
	}

	if (shaking) { modifiedModelMatrix *= hullShake; }   // MOD: ab hier (Rumpf, Turm, Waffen) mit dem Rumpf-Wackeln

	/* set default components transparent */
	if (psDroid->asBits[COMP_BODY] == 0)
	{
		pieFlag  |= pie_TRANSLUCENT;
		iPieData  = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else
	{
		pieFlag  &= ~pie_TRANSLUCENT;
		iPieData = 0;
	}

	/* Get the body graphic now*/
	const iIMDBaseShape *psBaseShapeBody = BODY_IMD(psDroid, psDroid->player);
	const iIMDShape *psShapeBody = (psBaseShapeBody) ? psBaseShapeBody->displayModel() : nullptr;
	if (psShapeBody)
	{
		const iIMDShape *strImd = psShapeBody;
		if (psDroid->droidType == DROID_PERSON)
		{
			// previously, used to scale the model by 0.75 - no longer needed with the fixed models though...
		}
		if (strImd->objanimpie[psDroid->animationEvent])
		{
			const iIMDBaseShape *evShape = strImd->objanimpie[psDroid->animationEvent];
			if (psDroid->animationEvent == ANIM_EVENT_FIRING && psDroid->droidType == DROID_PERSON)
			{
				evShape = modAimVariant(psDroid, evShape);
			}
			strImd = evShape->displayModel();
		}
		// MOD: Rumpf-Wackeln nach Combot-Nahkampftreffern - nur der Rumpf kippt, Turm und Waffen bleiben ruhig
		glm::mat4 bodyMatrix = modifiedModelMatrix;
		float wobblePitch = 0.f, wobbleRoll = 0.f;
		if (!bButton && modduel::hullWobble(psDroid, wobblePitch, wobbleRoll))
		{
			bodyMatrix = modifiedModelMatrix * glm::rotate(glm::radians(wobblePitch), glm::vec3(1.f, 0.f, 0.f)) * glm::rotate(glm::radians(wobbleRoll), glm::vec3(0.f, 0.f, 1.f));
		}
		const UDWORD bodyAnimStart = bButton ? psDroid->timeAnimationStarted : modGaitAnimStart(psDroid, strImd, psDroid->timeAnimationStarted);   // MOD (19.11.)
		while (strImd)
		{
			if (drawShape(strImd, bodyAnimStart, colour, brightness, pieFlag, iPieData, bodyMatrix, viewMatrix, -(psDroid->heightAboveMap)))
			{
				didDrawSomething = true;
			}
			if (!bButton && psDroid->shieldPoints > 0)
			{
				if (drawShape(strImd, psDroid->timeAnimationStarted, colour, brightness, shieldPieFlag, iShieldPieData, bodyMatrix, viewMatrix, -(psDroid->heightAboveMap)))
				{
					didDrawSomething = true;
				}
			}
			// MOD (19.11.): mit Ebenen-Animation (Blaetter/Skelett) hat drawShape schon alle Ebenen gezeichnet - nicht jede Ebene erneut
			if (strImd->next && strImd->next->objanimframes > 0) { break; }
			strImd = strImd->next.get();
		}
		// MOD (18.11. 19:09): GDI-Artillerie - Hydraulikstuetzen an den Beinen (nur beim Verankern/verankert)
		if (!bButton && modgdiart::drawStruts(psDroid, bodyMatrix, viewMatrix, static_cast<int>(colour), brightness, pieFlag, psDroid->heightAboveMap))
		{
			didDrawSomething = true;
		}
		if (!bButton && modicbm::drawTelJacks(psDroid, bodyMatrix, viewMatrix, static_cast<int>(colour), brightness, pieFlag, psDroid->heightAboveMap))
		{
			didDrawSomething = true;   // MOD (18.11. 20:46): Stuetzen des Atomraketen-Werfers
		}
		// MOD (17.11., Wunsch 25 "Bergen"): Abschlepp-Lkw mit geladenem Wrack - das Wrack liegt verkleinert auf der Ladeflaeche (nur Anzeige)
		if (!bButton)
		{
			const FEATURE_STATS *towed = static_cast<const FEATURE_STATS *>(modrecover::towedWreckStats(psDroid));
			if (towed != nullptr && towed->psImd != nullptr)
			{
				const float back = static_cast<float>(psShapeBody->max.z) * 0.35f;
				const glm::mat4 towMatrix = bodyMatrix * glm::translate(glm::vec3(0.f, static_cast<float>(psShapeBody->max.y) + 2.f, back)) * glm::scale(glm::vec3(0.62f));
				if (pie_Draw3DShape(towed->psImd->displayModel(), 0, 0, brightness, pieFlag, 0, towMatrix, viewMatrix, -(psDroid->heightAboveMap)))
				{
					didDrawSomething = true;
				}
			}
		}
	}

	/* Render animation effects based on movement or lack thereof, if any */
	const auto* bodyStats = psDroid->getBodyStats();
	psMoveAnim = bodyStats->ppMoveIMDList[psDroid->asBits[COMP_PROPULSION]];
	psStillAnim = bodyStats->ppStillIMDList[psDroid->asBits[COMP_PROPULSION]];
	if (!bButton && psMoveAnim && psDroid->sMove.Status != MOVEINACTIVE)
	{
		const iIMDShape *displayModel = psMoveAnim->displayModel();
		if (pie_Draw3DShape(displayModel, getModularScaledGraphicsTime(displayModel->animInterval, displayModel->numFrames), colour, brightness, pie_ADDITIVE, 200, propMatrix, viewMatrix, -(psDroid->heightAboveMap)))
		{
			didDrawSomething = true;
		}
	}
	else if (!bButton && psStillAnim) // standing still
	{
		const iIMDShape *displayModel = psStillAnim->displayModel();
		if (pie_Draw3DShape(displayModel, getModularScaledGraphicsTime(displayModel->animInterval, displayModel->numFrames), colour, brightness, 0, 0, propMatrix, viewMatrix, -(psDroid->heightAboveMap)))
		{
			didDrawSomething = true;
		}
	}

	/* set default components transparent */
	if (psDroid->asWeaps[0].nStat        == 0 &&
	    psDroid->asBits[COMP_SENSOR]     == 0 &&
	    psDroid->asBits[COMP_ECM]        == 0 &&
	    psDroid->asBits[COMP_BRAIN]      == 0 &&
	    psDroid->asBits[COMP_REPAIRUNIT] == 0 &&
	    psDroid->asBits[COMP_CONSTRUCT]  == 0)
	{
		pieFlag  |= pie_TRANSLUCENT;
		iPieData  = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else
	{
		pieFlag  &= ~pie_TRANSLUCENT;
		iPieData = 0;
	}

	if (psShapeBody && !psShapeBody->connectors.empty())
	{
		/* vtol weapons attach to connector 2 (underneath);
		 * all others to connector 1 */
		/* VTOL's now skip the first 5 connectors(0 to 4),
		VTOL's use 5,6,7,8 etc now */
		if (psPropStats->propulsionType == PROPULSION_TYPE_LIFT && psDroid->droidType == DROID_WEAPON)
		{
			iConnector = VTOL_CONNECTOR_START;
		}
		else
		{
			iConnector = 0;
		}

		// MOD (19.11.): Laufende Mechs/Cyborgs mit Blaetter-Animation - Anschlusspunkte der gerade sichtbaren Ebene (Oberkoerper wippt mit)
		const iIMDShape *connBody = psShapeBody;
		if (!bButton && (psDroid->droidType == DROID_CYBORG_SUPER || psDroid->droidType == DROID_CYBORG))
		{
			const iIMDShape *animBody = psShapeBody->objanimpie[psDroid->animationEvent] ? psShapeBody->objanimpie[psDroid->animationEvent]->displayModel() : psShapeBody;
			const iIMDShape *vis = modVisibleFlipLevel(animBody, modGaitAnimStart(psDroid, animBody, psDroid->timeAnimationStarted));
			if (vis != nullptr && vis->connectors.size() >= psShapeBody->connectors.size()) { connBody = vis; }
		}
		switch (psDroid->droidType)
		{
		case DROID_DEFAULT:
		case DROID_TRANSPORTER:
		case DROID_SUPERTRANSPORTER:
		case DROID_CYBORG:
		case DROID_CYBORG_SUPER:
		case DROID_WEAPON:
		case DROID_COMMAND:		// command droids have a weapon to store all the graphics
			/*	Get the mounting graphic - we've already moved to the right position
			Allegedly - all droids will have a mount graphic so this shouldn't
			fall on it's arse......*/
			/* Double check that the weapon droid actually has any */
			for (i = 0; i < psDroid->numWeaps; i++)
			{
				if ((psDroid->asWeaps[i].nStat > 0 || psDroid->droidType == DROID_DEFAULT)
				    && !psShapeBody->connectors.empty())
				{
					Rotation rot = getInterpolatedWeaponRotation(psDroid, i, graphicsTime);
					if (!bButton) { rot.pitch = modgdiart::displayPitch(psDroid, rot.pitch, graphicsTime); }   // MOD (18.11.): Rohre erst verankert hoch
					rot.pitch = modicbm::telPitch(psDroid, rot.pitch, graphicsTime);   // MOD (18.11. 20:46): Aufrichter des Atomraketen-Werfers

					glm::mat4 localModelMatrix = modifiedModelMatrix;
					float localHeightAboveTerrain = psDroid->heightAboveMap;

					//to skip number of VTOL_CONNECTOR_START ground unit connectors
					size_t bodyConnectorIndex = (iConnector < VTOL_CONNECTOR_START) ? i : iConnector + i;
					if (bodyConnectorIndex < connBody->connectors.size())
					{
						localModelMatrix *= glm::translate(glm::vec3(connBody->connectors[bodyConnectorIndex].xzy()));
						localHeightAboveTerrain += connBody->connectors[bodyConnectorIndex].z;
					}
					else
					{
						debug(LOG_INFO, "Model lacks sufficient connectors?: %s", psShapeBody->modelName.toUtf8().c_str());
					}
					if (!bButton)
					{
						const float lift = modgdiart::torsoLift(psDroid, graphicsTime);   // MOD (18.11.): Aufbau hebt sich beim Verankern
						if (lift != 0.f) { localModelMatrix *= glm::translate(glm::vec3(0.f, lift, 0.f)); localHeightAboveTerrain += lift; }
					}
					localModelMatrix *= glm::rotate(UNDEG(-rot.direction), glm::vec3(0.f, 1.f, 0.f));

					/* vtol weapons inverted */
					if (iConnector >= VTOL_CONNECTOR_START)
					{
						//this might affect gun rotation
						localModelMatrix *= glm::rotate(UNDEG(65536 / 2), glm::vec3(0.f, 0.f, 1.f));
					}

					/* Get the mount graphic */
					const iIMDBaseShape *psBaseShape = WEAPON_MOUNT_IMD(psDroid, i);
					const iIMDShape *psShape = (psBaseShape) ? psBaseShape->displayModel() : nullptr;

					int recoilValue = getRecoil(psDroid->asWeaps[i]);
					localModelMatrix *= glm::translate(glm::vec3(0.f, 0.f, recoilValue / 3.f));

					/* Draw it */
					if (psShape)
					{
						if (pie_Draw3DShape(psShape, 0, colour, brightness, pieFlag, iPieData, localModelMatrix, viewMatrix, -localHeightAboveTerrain))
						{
							didDrawSomething = true;
						}
						if (!bButton && psDroid->shieldPoints > 0)
						{
							if (pie_Draw3DShape(psShape, 0, colour, brightness, shieldPieFlag, iShieldPieData, localModelMatrix, viewMatrix, -localHeightAboveTerrain))
							{
								didDrawSomething = true;
							}
						}
					}
					localModelMatrix *= glm::translate(glm::vec3(0, 0, recoilValue));

					/* translate for weapon mount point */
					if (psShape && !psShape->connectors.empty())
					{
						localModelMatrix *= glm::translate(glm::vec3(psShape->connectors[0].xzy()));
						localHeightAboveTerrain += psShape->connectors[0].z;
					}
					// MOD (18.11.): GDI Juggernaut - Turm der mittleren Kanone am Drehpunkt der Rohre (dreht mit, neigt sich nicht)
					if (modgdiart::drawHousing(psDroid, localModelMatrix, viewMatrix, static_cast<int>(colour), brightness, pieFlag, iPieData, localHeightAboveTerrain))
					{
						didDrawSomething = true;
					}

					/* vtol weapons inverted */
					if (iConnector >= VTOL_CONNECTOR_START)
					{
						//pitch the barrel down
						localModelMatrix *= glm::rotate(UNDEG(-rot.pitch), glm::vec3(1.f, 0.f, 0.f));
					}
					else
					{
						//pitch the barrel up
						localModelMatrix *= glm::rotate(UNDEG(rot.pitch), glm::vec3(1.f, 0.f, 0.f));
					}

					/* Get the weapon (gun?) graphic */
					psBaseShape = WEAPON_IMD(psDroid, i);
					psShape = (psBaseShape) ? psBaseShape->displayModel() : nullptr;

					// We have a weapon so we draw it and a muzzle flash from weapon connector
					if (psShape)
					{
						if (pie_Draw3DShape(psShape, 0, colour, brightness, pieFlag, iPieData, localModelMatrix, viewMatrix, -localHeightAboveTerrain))
						{
							didDrawSomething = true;
						}
						// MOD (18.11. 20:46): Atomraketen-Werfer - die Rakete liegt auf dem Aufrichter, solange sie geladen ist
						if (modicbm::drawTelRocket(psDroid, localModelMatrix, viewMatrix, static_cast<int>(colour), brightness, pieFlag, iPieData, localHeightAboveTerrain))
						{
							didDrawSomething = true;
						}
						if (!bButton && psDroid->shieldPoints > 0)
						{
							if (pie_Draw3DShape(psShape, 0, colour, brightness, shieldPieFlag, iShieldPieData, localModelMatrix, viewMatrix, -localHeightAboveTerrain))
							{
								didDrawSomething = true;
							}
						}
						auto flashBaseModel = MUZZLE_FLASH_PIE(psDroid, i);
						const iIMDShape *pMuzzleFlash = (flashBaseModel) ? flashBaseModel->displayModel() : nullptr;
						drawMuzzleFlash(psDroid->asWeaps[i], psShape, pMuzzleFlash, brightness, pieFlag, iPieData, localModelMatrix, viewMatrix, localHeightAboveTerrain);
					}
				}
			}
			break;

		case DROID_SENSOR:
		case DROID_CONSTRUCT:
		case DROID_CYBORG_CONSTRUCT:
		case DROID_ECM:
		case DROID_REPAIR:
		case DROID_CYBORG_REPAIR:
			{
				Rotation rot = getInterpolatedWeaponRotation(psDroid, 0, graphicsTime);
				iIMDBaseShape *psBaseShape = nullptr;
				iIMDBaseShape *psBaseMountShape = nullptr;

				switch (psDroid->droidType)
				{
				default:
					ASSERT(false, "Bad component type");
					break;
				case DROID_SENSOR:
					psBaseMountShape = SENSOR_MOUNT_IMD(psDroid, psDroid->player);
					/* Get the sensor graphic, assuming it's there */
					psBaseShape = SENSOR_IMD(psDroid, psDroid->player);
					break;
				case DROID_CONSTRUCT:
				case DROID_CYBORG_CONSTRUCT:
					psBaseMountShape = CONSTRUCT_MOUNT_IMD(psDroid, psDroid->player);
					/* Get the construct graphic assuming it's there */
					psBaseShape = CONSTRUCT_IMD(psDroid, psDroid->player);
					break;
				case DROID_ECM:
					psBaseMountShape = ECM_MOUNT_IMD(psDroid, psDroid->player);
					/* Get the ECM graphic assuming it's there.... */
					psBaseShape = ECM_IMD(psDroid, psDroid->player);
					break;
				case DROID_REPAIR:
				case DROID_CYBORG_REPAIR:
					psBaseMountShape = REPAIR_MOUNT_IMD(psDroid, psDroid->player);
					/* Get the Repair graphic assuming it's there.... */
					psBaseShape = REPAIR_IMD(psDroid, psDroid->player);
					break;
				}

				const iIMDShape *psShape = (psBaseShape) ? psBaseShape->displayModel() : nullptr;
				const iIMDShape *psMountShape = (psBaseMountShape) ? psBaseMountShape->displayModel() : nullptr;

				/*	Get the mounting graphic - we've already moved to the right position
				Allegedly - all droids will have a mount graphic so this shouldn't
				fall on it's arse......*/
				//sensor and cyborg and ecm uses connectors[0]

				// MOD (17.11. 20:06): Minenraeumer sitzt vor dem Bug jedes Rumpfes (nicht auf dem Turm-Anschluss); Walze dreht beim Minenraeumen
				if (psDroid->droidType == DROID_ECM
				    && modflail::draw(psDroid, psShapeBody, psShapeProp ? psShapeProp->displayModel() : nullptr, modifiedModelMatrix, viewMatrix,
				                      static_cast<int>(colour), brightness, pieFlag, iPieData, psDroid->heightAboveMap, bButton, didDrawSomething))
				{
					break;
				}

				glm::mat4 localModelMatrix = modifiedModelMatrix;
				float localHeightAboveTerrain = psDroid->heightAboveMap;
				/* vtol weapons inverted */
				if (iConnector >= VTOL_CONNECTOR_START)
				{
					//this might affect gun rotation
					localModelMatrix *= glm::rotate(UNDEG(65536 / 2), glm::vec3(0.f, 0.f, 1.f));
				}

				localModelMatrix *= glm::translate(glm::vec3(psShapeBody->connectors[0].xzy()));
				localHeightAboveTerrain += psShapeBody->connectors[0].z;

				localModelMatrix *= glm::rotate(UNDEG(-rot.direction), glm::vec3(0.f, 1.f, 0.f));
				/* Draw it */
				if (psMountShape)
				{
					if (pie_Draw3DShape(psMountShape, 0, colour, brightness, pieFlag, iPieData, localModelMatrix, viewMatrix, -localHeightAboveTerrain))
					{
						didDrawSomething = true;
					}
				}

				/* translate for construct mount point if cyborg */
				if (psDroid->isCyborg() && psMountShape && !psMountShape->connectors.empty())
				{
					localModelMatrix *= glm::translate(glm::vec3(psMountShape->connectors[0].xzy()));
					localHeightAboveTerrain += psMountShape->connectors[0].z;
				}

				/* MOD: Arbeitsbewegung des Baggerarms / Abrissbirne (nur Anzeige) */
				if (psDroid->droidType == DROID_CONSTRUCT)
				{
					float awYaw = 0.f, awPitch = 0.f;
					if (modbuild::armWork(psDroid, graphicsTime, awYaw, awPitch))
					{
						localModelMatrix *= glm::rotate(awYaw * 0.01745329f, glm::vec3(0.f, 1.f, 0.f));
						localModelMatrix *= glm::rotate(awPitch * 0.01745329f, glm::vec3(1.f, 0.f, 0.f));
					}
				}

				/* Draw it */
				if (psShape)
				{
					if (pie_Draw3DShape(psShape, 0, colour, brightness, pieFlag, iPieData, localModelMatrix, viewMatrix, -localHeightAboveTerrain))
					{
						didDrawSomething = true;
					}
					if (psDroid->droidType == DROID_CONSTRUCT && !bButton)   // MOD (18.11.): geladene Faesser/Kisten auf der Ladeflaeche
					{
						modoil::drawCargo(psDroid, psShape, localModelMatrix, viewMatrix, colour, brightness, pieFlag, iPieData, -localHeightAboveTerrain);
					}
					// MOD (Wunsch 18.11. 12:22): Abrissbirne (Original-WZ-Modell, Kran + Pendel) - die Birne haengt am Anschluss 0 des Krans
					// und pendelt (beim Abreissen grosse Schwuenge); im Designer/auf Knoepfen haengt sie ruhig
					if (psDroid->droidType == DROID_CONSTRUCT && !psShape->connectors.empty())
					{
						const CONSTRUCT_STATS *wcs = psDroid->getConstructStats();
						if (wcs != nullptr && wcs->id.startsWith("WreckBall"))
						{
							static const iIMDBaseShape *s_pend = nullptr;
							static unsigned s_pendTried = 0;   // MOD (18.11.): Modell-Generation (Neuladen zwischen Spielen)
							if (modelCacheStale(s_pendTried)) { s_pend = modelGet("modwreckpend.pie"); }
							if (s_pend != nullptr)
							{
								const float sw = bButton ? 0.f : modbuild::ballSwing(psDroid, graphicsTime);
								const glm::mat4 pm = localModelMatrix * glm::translate(glm::vec3(psShape->connectors[0].xzy()))
								                   * glm::rotate(glm::radians(-sw), glm::vec3(1.f, 0.f, 0.f));
								if (pie_Draw3DShape(s_pend->displayModel(), 0, colour, brightness, pieFlag, iPieData, pm, viewMatrix, -localHeightAboveTerrain))
								{
									didDrawSomething = true;
								}
							}
						}
					}

					// In repair droid case only:
					if ((psDroid->droidType == DROID_REPAIR || psDroid->droidType == DROID_CYBORG_REPAIR) &&
					    !psShape->connectors.empty() && psDroid->action == DACTION_DROIDREPAIR)
					{
						Spacetime st = interpolateObjectSpacetime(psDroid, graphicsTime);
						localModelMatrix *= glm::translate(glm::vec3(psShape->connectors[0].xzy()));
						localHeightAboveTerrain += psShape->connectors[0].z;
						localModelMatrix *= glm::translate(glm::vec3(0.f, -20.f, 0.f));

						psShape = getImdFromIndex(MI_FLAME)->displayModel();

						/* Rotate for droid */
						localModelMatrix *= glm::rotate(UNDEG(st.rot.direction), glm::vec3(0.f, 1.f, 0.f));
						localModelMatrix *= glm::rotate(UNDEG(-st.rot.pitch), glm::vec3(1.f, 0.f, 0.f));
						localModelMatrix *= glm::rotate(UNDEG(-st.rot.roll), glm::vec3(0.f, 0.f, 1.f));
						//rotate Y
						localModelMatrix *= glm::rotate(UNDEG(rot.direction), glm::vec3(0.f, 1.f, 0.f));

						localModelMatrix *= glm::rotate(UNDEG(-playerPos.r.y), glm::vec3(0.f, 1.f, 0.f));
						localModelMatrix *= glm::rotate(UNDEG(-playerPos.r.x), glm::vec3(1.f, 0.f, 0.f));

						if (pie_Draw3DShape(psShape, getModularScaledGraphicsTime(psShape->animInterval, psShape->numFrames), 0, brightness, pie_ADDITIVE, 140, localModelMatrix, viewMatrix, -localHeightAboveTerrain))
						{
							didDrawSomething = true;
						}

//						localModelMatrix *= glm::rotate(UNDEG(playerPos.r.x), glm::vec3(1.f, 0.f, 0.f)); // Not used?
//						localModelMatrix *= glm::rotate(UNDEG(playerPos.r.y), glm::vec3(0.f, 1.f, 0.f)); // Not used?
					}
				}
				break;
			}
		case DROID_PERSON:
			// no extra mounts for people
			break;
		default:
			ASSERT(!"invalid droid type", "Whoa! Weirdy type of droid found in drawComponentObject!!!");
			break;
		}
	}

	/* set default components transparent */
	if (psDroid->asBits[COMP_PROPULSION] == 0)
	{
		pieFlag  |= pie_TRANSLUCENT;
		iPieData  = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else
	{
		pieFlag  &= ~pie_TRANSLUCENT;
		iPieData = 0;
	}

	// now render the other propulsion side
	psShapeProp = (leftFirst ? getRightPropulsionIMD(psDroid) : getLeftPropulsionIMD(psDroid));
	if (psShapeProp)
	{
		if (pie_Draw3DShape(psShapeProp->displayModel(), modTrackFrame(psDroid, psShapeProp->displayModel(), bButton), colour, brightness, pieFlag, iPieData, modifiedModelMatrix, viewMatrix, -(psDroid->heightAboveMap)))
		{
			didDrawSomething = true;
		}
		if (!bButton && psDroid->shieldPoints > 0)
		{
			if (pie_Draw3DShape(psShapeProp->displayModel(), modTrackFrame(psDroid, psShapeProp->displayModel(), bButton), colour, brightness, shieldPieFlag, iShieldPieData, modifiedModelMatrix, viewMatrix, -(psDroid->heightAboveMap)))
			{
				didDrawSomething = true;
			}
		}
	}

	return didDrawSomething;
}


// Render a composite droid given a DROID_TEMPLATE structure.
//
void displayComponentButtonTemplate(const DROID_TEMPLATE *psTemplate, const Vector3i *Rotation, const Vector3i *Position, int scale)
{
	const glm::mat4 matrix = setMatrix(Position, Rotation, scale);

	// Decide how to sort it.
	leftFirst = angleDelta(DEG(Rotation->y)) < 0;

	DROID Droid(0, selectedPlayer);
	memset(Droid.asBits, 0, sizeof(Droid.asBits));
	droidSetBits(psTemplate, &Droid);

	Droid.pos = Vector3i(0, 0, 0);
	Droid.rot = Vector3i(0, 0, 0);

	//draw multi component object as a button object
	displayCompObj(&Droid, true, matrix, glm::mat4(1.f));
}


// Render a composite droid given a DROID structure.
//
void displayComponentButtonObject(const DROID *psDroid, const Vector3i *Rotation, const Vector3i *Position, int scale)
{
	SDWORD		difference;

	const glm::mat4 matrix = setMatrix(Position, Rotation, scale);

	// Decide how to sort it.
	difference = Rotation->y % 360;

	leftFirst = !((difference > 0 && difference < 180) || difference < -180);

	// And render the composite object.
	//draw multi component object as a button object
	displayCompObj(psDroid, true, matrix, glm::mat4(1.f));
}

/* Assumes matrix context is already set */
// multiple turrets display removed the pointless mountRotation
void displayComponentObject(DROID *psDroid, const glm::mat4 &viewMatrix, const glm::mat4 &perspectiveViewMatrix)
{
	Vector3i position, rotation;
	Spacetime st = interpolateObjectSpacetime(psDroid, graphicsTime);

	leftFirst = angleDelta(playerPos.r.y - st.rot.direction) <= 0;

	/* Get the real position */
	position.x = st.pos.x;
	position.z = -(st.pos.y);
	position.y = st.pos.z;

	// settle ground units onto the drawn terrain surface, which can deviate
	// slightly from the gameplay surface when terrain mesh smoothing is active
	position.y += static_cast<int>(getTerrainVisualObjectHeightDelta(gameWorld.map, st.pos.x, st.pos.y, st.pos.z));

	if (psDroid->isTransporter())
	{
		position.y += bobTransporterHeight();
	}
	// MOD: fliessender Ebenenwechsel (Orbit) und Flughoehe der Sprungduesen-Combots - nur Anzeige
	// (18.11. 17:52) Ebenenwechsel als fast senkrechter Steigflug: Hoehe, Versatz in Flugrichtung und Nickwinkel
	const modlayer::FlightPose flightPose = modlayer::transitionPose(psDroid, graphicsTime);
	position.y += static_cast<int>(flightPose.up + modcombot::flightOffset(psDroid));
	if (flightPose.forward != 0.f)
	{
		const int fw = static_cast<int>(flightPose.forward);
		position.x += iSinR(st.rot.direction, fw);
		position.z -= iCosR(st.rot.direction, fw);
	}

	/* Get all the pitch,roll,yaw info */
	rotation.y = -st.rot.direction;
	rotation.x = st.rot.pitch + flightPose.pitch;
	rotation.z = st.rot.roll;

	/* Translate origin */
	/* Rotate for droid */
	glm::mat4 modelMatrix = glm::translate(glm::vec3(position)) *
		glm::rotate(UNDEG(rotation.y), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(rotation.x), glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(rotation.z), glm::vec3(0.f, 0.f, 1.f));

	if (psDroid->timeLastHit - graphicsTime < ELEC_DAMAGE_DURATION && psDroid->lastHitWeapon == WSC_ELECTRONIC)
	{
		modelMatrix *= objectShimmy((BASE_OBJECT *) psDroid);
	}

	// now check if the projected circle is within the screen boundaries
	if(!clipDroidOnScreen(psDroid, perspectiveViewMatrix * modelMatrix, (getIsCloseDistance()) ? 150 : 25))
	{
		return;
	}

	if (psDroid->lastHitWeapon == WSC_EMP && graphicsTime - psDroid->timeLastHit < EMP_DISABLE_TIME)
	{
		Vector3i effectPosition;

		//add an effect on the droid
		effectPosition.x = st.pos.x + DROID_EMP_SPREAD;
		effectPosition.y = st.pos.z + rand() % 8;
		effectPosition.z = st.pos.y + DROID_EMP_SPREAD;
		effectGiveAuxVar(90 + rand() % 20);
		addEffect(&effectPosition, EFFECT_EXPLOSION, EXPLOSION_TYPE_PLASMA, false, nullptr, 0);
	}

	if (psDroid->visibleForLocalDisplay() == UBYTE_MAX)
	{
		//ingame not button object
		//should render 3 mounted weapons now
		if (displayCompObj(psDroid, false, modelMatrix, viewMatrix))
		{
			// did draw something to the screen - update the framenumber
			psDroid->sDisplay.frameNumber = frameGetFrameNumber();
		}

		/* set up all the screen coords stuff */
		calcScreenCoords(psDroid, perspectiveViewMatrix * modelMatrix);
	}
	else
	{
		int frame = graphicsTime / BLIP_ANIM_DURATION + psDroid->id % 8192; // de-sync the blip effect, but don't overflow the int
		if (pie_Draw3DShape(getImdFromIndex(MI_BLIP)->displayModel(), frame, 0, WZCOL_WHITE, pie_ADDITIVE, psDroid->visibleForLocalDisplay() / 2, modelMatrix, viewMatrix))
		{
			psDroid->sDisplay.frameNumber = frameGetFrameNumber();
		}
	}
}


void destroyFXDroid(DROID *psDroid, unsigned impactTime, Vector3f &velocity)
{
	for (int i = 0; i < 5; ++i)
	{
		const iIMDBaseShape *psImd = nullptr;

		int maxHorizontalScatter = DIST_TILE / 4;
		int heightScatter = DIST_TILE / 5;
		Vector2i horizontalScatter = iSinCosR(rand(), rand() % maxHorizontalScatter);

		Vector3i pos = (psDroid->pos + Vector3i(horizontalScatter, 16 + heightScatter)).xzy();
		switch (i)
		{
		case 0:
			switch (psDroid->droidType)
			{
			case DROID_DEFAULT:
			case DROID_CYBORG:
			case DROID_CYBORG_SUPER:
			case DROID_CYBORG_CONSTRUCT:
			case DROID_CYBORG_REPAIR:
			case DROID_WEAPON:
			case DROID_COMMAND:
				if (psDroid->numWeaps > 0)
				{
					if (psDroid->asWeaps[0].nStat > 0)
					{
						psImd = WEAPON_MOUNT_IMD(psDroid, 0);
					}
				}
				break;
			default:
				break;
			}
			break;
		case 1:
			switch (psDroid->droidType)
			{
			case DROID_DEFAULT:
			case DROID_CYBORG:
			case DROID_CYBORG_SUPER:
			case DROID_CYBORG_CONSTRUCT:
			case DROID_CYBORG_REPAIR:
			case DROID_WEAPON:
			case DROID_COMMAND:
				if (psDroid->numWeaps)
				{
					// get main weapon
					psImd = WEAPON_IMD(psDroid, 0);
				}
				break;
			default:
				break;
			}
			break;
		}
		if (psImd == nullptr)
		{
			psImd = getRandomDebrisImd();
		}
		// Tell the effect system that it needs to use this player's color for the next effect
		SetEffectForPlayer(psDroid->player);
		addEffect(&pos, EFFECT_GRAVITON, GRAVITON_TYPE_EMITTING_DR, true, psImd->displayModel(), getPlayerColour(psDroid->player), impactTime, nullptr, &velocity);
	}
}


void	compPersonToBits(DROID *psDroid, Vector3f &velocity)
{
	Vector3i position;	//,rotation,velocity;
	iIMDBaseShape	*headImd, *legsImd, *armImd, *bodyImd;
	UDWORD		col;

	if (!psDroid->visibleForLocalDisplay()) // display only - should not affect game state
	{
		/* We can't see the person or cyborg - so get out */
		return;
	}
	/* get bits pointers according to whether baba or cyborg*/
	if (psDroid->isCyborg())
	{
		// This is probably unused now, since there's a more appropriate effect for cyborgs.
		headImd = getImdFromIndex(MI_CYBORG_HEAD);
		legsImd = getImdFromIndex(MI_CYBORG_LEGS);
		armImd  = getImdFromIndex(MI_CYBORG_ARM);
		bodyImd = getImdFromIndex(MI_CYBORG_BODY);
	}
	else
	{
		headImd = getImdFromIndex(MI_BABA_HEAD);
		legsImd = getImdFromIndex(MI_BABA_LEGS);
		armImd  = getImdFromIndex(MI_BABA_ARM);
		bodyImd = getImdFromIndex(MI_BABA_BODY);
	}

	/* Get where he's at */
	position.x = psDroid->pos.x;
	position.y = psDroid->pos.z + 1;
	position.z = psDroid->pos.y;

	/* Tell about player colour */
	col = getPlayerColour(psDroid->player);

	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, headImd->displayModel(), col, gameTime - deltaGameTime + 1, nullptr, &velocity);
	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, legsImd->displayModel(), col, gameTime - deltaGameTime + 1, nullptr, &velocity);
	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, armImd->displayModel(), col, gameTime - deltaGameTime + 1, nullptr, &velocity);
	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, bodyImd->displayModel(), col, gameTime - deltaGameTime + 1, nullptr, &velocity);
}


SDWORD	rescaleButtonObject(SDWORD radius, SDWORD baseScale, SDWORD baseRadius)
{
	// MOD (18.11. 19:55, Absturz "Integer Divide By Zero" im Konstruktions-/Forschungsknopf): ein Modell ohne Ausdehnung (Halbmesser 0)
	// darf den Knopf nicht teilen lassen - dann einfach in Grundgroesse zeigen
	if (radius <= 0)
	{
		return baseScale > 0 ? baseScale : 100;
	}
	SDWORD newScale;
	newScale = 100 * baseRadius;
	newScale /= radius;
	if (baseScale > 0)
	{
		newScale += baseScale;
		newScale /= 2;
	}
	return newScale;
}
