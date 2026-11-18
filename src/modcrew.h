// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Ja vom 18.11. 17:52 zu "Metal-Fatigue-Besatzung als eigene Einheiten (Crew mit Stufen, Combots ohne Besatzung neutral)"):
// Besatzung wie in Metal Fatigue (MF-Analyse: CRobot::Crewless, DeployRobot braucht eine Crew, "Eject Crew", Stufen Rekrut/Veteran).
//  - Jeder Combot hat einen Piloten mit Namen und Stufe. Die Stufe ist die WZ-Erfahrung (Rangabzeichen): der Pilot nimmt sie mit von
//    Combot zu Combot. Neue Combots bekommen einen Piloten aus der Halle (zuerst den erfahrensten), sonst einen neuen Rekruten.
//  - Wird ein Combot zerstoert, steigt der Pilot meist lebend aus (Besatzungs-Einheit zu Fuss) und laeuft zur naechsten eigenen Fabrik
//    oder Kryo-Farm; dort ist er wieder in der Halle und fliegt den naechsten Combot. Wird er unterwegs getoetet, ist er tot.
//  - "Aussteigen" (Knopf in der Rosette): der Pilot verlaesst den Combot; der Combot steht dann ohne Besatzung da = neutral: er nimmt
//    keine Befehle an, schiesst nicht und wird von niemandem von selbst angegriffen (nur auf ausdruecklichen Befehl).
//  - Jede Besatzung (auch die eines Gegners) kann einen Combot ohne Besatzung besteigen: Besatzung waehlen, Rechtsklick auf den Combot
//    (oder sie steht untaetig daneben). Ein fremder Combot wechselt dabei den Besitzer.
// Alles im Spielschritt aus Spielzustand -> netzwerkgleich; Zustand im Spielstand (modsave).
#pragma once

#include <cstdint>
#include <vector>

struct BASE_OBJECT;
struct SIMPLE_OBJECT;
struct DROID;

namespace modcrew
{
/// Besatzungs-Einheit zu Fuss (Vorlage MFCrew)?
bool isCrew(const DROID *psDroid);
/// Combot ohne Besatzung (neutral)?
bool crewless(const SIMPLE_OBJECT *psObj);
/// Rechtsklick einer Besatzung auf dieses Objekt = einsteigen?
bool canBoard(const DROID *psCrew, const BASE_OBJECT *psTarget);
/// jeden Spielschritt aus der Spielschleife
void tick();
/// Spielende / Laden
void reset();
/// Spielstand
void saveState(std::vector<int64_t> &out);
void loadState(const std::vector<int64_t> &in);
}
