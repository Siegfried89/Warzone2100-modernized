// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch: Schildsystem wie in Supreme Commander): Schildgeneratoren spannen eine Kuppel (Halbkugel) auf. Geschosse feindlicher
// Spieler, die von aussen in die Kuppel eindringen, werden an der Kuppelwand aufgehalten - der Schild verliert dafuer Lebenspunkte
// (Schaden der Waffe). Bricht er zusammen (0 Punkte), faellt er aus und laedt sich nach einer Wartezeit von null wieder auf.
// Geschosse von innen nach aussen fliegen durch; Atomraketen und der Lasersatellit gehen durch (strategische Waffen).
// Werte je Bauwerk in structure.json: "shieldRadius" (Welteinheiten), "shieldHP", "shieldRegen" (Punkte je Sekunde),
// "shieldRecharge" (ms Ausfall nach dem Zusammenbruch). Ohne Strom (PowerDown) ist die Kuppel aus.
// Spiellogik ganzzahlig im Spieltakt (auf allen Rechnern gleich); Kuppel, Einschlag-Aufblitzen und Balken sind nur Anzeige.
#pragma once

#include "lib/framework/vector.h"   // muss vor jedem glm-Header kommen (GLM_FORCE_SWIZZLE)
#include <glm/fwd.hpp>
#include <vector>

struct PROJECTILE;
struct STRUCTURE;
struct BASE_OBJECT;

namespace modshield
{
/// einmal je Spieltakt (loop.cpp): Aufladen, Ausfall, Liste der aktiven Kuppeln
void tick();
/// nach dem Flugschritt eines Geschosses: true = von einem Schild aufgehalten (der Aufrufer nimmt es aus dem Spiel)
bool tryAbsorb(PROJECTILE *proj);
/// Ersatz-Kuppeln aus Modellen (nur wenn die Supreme-Commander-Texturen fehlen), in die Modell-Warteschlange
void draw3D(const glm::mat4 &viewMatrix);
/// Kuppeln und Einschlaege wie Supreme Commander (eigenes Grafikprogramm), im Durchgang fuer Durchsichtiges
void drawDomes(const glm::mat4 &perspectiveMatrix, const glm::mat4 &viewMatrix);
/// Grafik-Mittel freigeben (Ende der 3D-Ansicht)
void gpuShutdown();
/// Schild-Fuellstand in Prozent fuer den Balken; -1 = kein Schild (Bauwerk oder Fahrzeug)
int shieldPercent(const BASE_OBJECT *o);
/// Spielstand
void saveState(std::vector<int> &out);
void loadState(const std::vector<int> &in);
/// neues Spiel / Spielstand geladen
void reset();
}
