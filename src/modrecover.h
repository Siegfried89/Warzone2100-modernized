// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 16.11. Punkt 10): abgeschossener Turm bleibt liegen, Bergen, Reparatur erst mit dem Teil
//
// - Wird bei einem Fahrzeug der Turm zerstoert (modparts), faellt er neben das Fahrzeug auf den Boden: Feature "ModTurretPart"
//   (gezeichnet mit der echten Waffe). Das Fahrzeug faehrt sofort zur naechsten Reparaturstation.
// - Dort wird der Rumpf repariert, der Turm aber erst wieder aufgesetzt, wenn das Teil angekommen ist. Bis dahin bleibt das
//   Fahrzeug in der Werkstatt (Rumpf knapp unter voll).
// - Bergen: Baufahrzeuge und Reparatureinheiten des Besitzers in der Naehe sammeln liegende Teile von selbst ein (oder auf Befehl:
//   Rechtsklick auf das Teil) und bringen sie zur Reparaturstation. Jede eigene Einheit, die darueber faehrt, nimmt es ebenfalls mit.
//   Ein Gegner, der es aufsammelt, verschrottet es.
// - Ist das Teil verloren (zerstoert, vom Gegner genommen, Traeger gefallen), baut die Werkstatt nach 2,5 Minuten ein Ersatzteil.
// Spielwirkung nur ueber gameTime/feste Reihenfolge (auf allen Rechnern gleich). Funken/Arm-Bewegung/Zischen sind reine Anzeige.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/mat4x4.hpp>
#include <utility>
#include <vector>

#include "modwreck.h"

struct DROID;
struct FEATURE;
struct STRUCTURE;

namespace modrecover
{
/// modparts: Turm ist ab (weaponStat = erste Waffe vor dem Verlust)
void onTurretLost(DROID *psDroid, unsigned weaponStat);
/// modparts: darf der Turm wieder aufgesetzt werden? (Teil angekommen bzw. Ersatzteil gebaut)
bool partReady(const DROID *psDroid);
/// modparts: Turm wurde wieder aufgesetzt
void onRestored(const DROID *psDroid);
/// feature.cpp removeFeature: Feature verschwindet
void onFeatureRemoved(const FEATURE *psFeature);
/// move.cpp: true = diese Einheit darf das Feature nicht aufnehmen (eigene Kampfeinheiten lassen Turm-Teile liegen)
bool blocksPickup(const FEATURE *psFeature, const DROID *psDroid);
/// move.cpp checkLocalFeatures: Einheit faehrt ueber ein Feature. true = behandelt (Feature wird entfernt)
bool onPickup(FEATURE *psFeature, DROID *psDroid);
/// jeden Spielschritt
void tick();
void reset();

/// Knopf "Recover": die gewaehlten Einheiten bergen die naechstgelegenen liegenden Turm-Teile ihres Spielers (Befehle ueber das Netz)
void recoverWithSelection(const std::vector<DROID *> &units);

/// (17.11., Wunsch 25 "Bergen") Abschlepp-Lkw = Scav-Lkw mit Ladeflaeche (ScavBed1/2/3): Rechtsklick/Befehl "Bergen" auf ein Wrack laedt es auf,
/// der Lkw faehrt es zur naechsten eigenen Reparaturstation; dort wird daraus wieder ein Fahrzeug (stark beschaedigt, wird gleich repariert).
bool isTowTruck(const DROID *psDroid);
/// kann dieses Feature abgeschleppt werden? (gelandetes Wrack mit Bauplan)
bool isTowable(const FEATURE *psFeature);
/// (17.11.) Spielstand: Abschlepp-Lkw mit geladenem Wrack
std::vector<std::pair<uint32_t, modwreck::WreckInfo>> saveTows();
void loadTows(const std::vector<std::pair<uint32_t, modwreck::WreckInfo>> &tows);
/// traegt die Einheit gerade ein Turm-Teil oder ein Wrack zur Werkstatt?
bool carrying(const DROID *psDroid);
/// Anzeige: Wrack-Modell, das dieser Lkw gerade geladen hat (nullptr = keins)
const void *towedWreckStats(const DROID *psDroid);

/// Anzeige: Waffe fuer ein liegendes Turm-Teil (0 = keins)
unsigned partWeapon(const FEATURE *psFeature);
bool isPartStats(const void *psFeatureStats);
/// Anzeige: Reparaturstation repariert gerade (Funken, Zischen, Arm wackelt). Gibt den Wackel-Winkel (Neigung, Richtung) zurueck.
void repairEffects(const STRUCTURE *psStructure, const DROID *psTarget, int &pitchWobble, int &dirWobble);
}
