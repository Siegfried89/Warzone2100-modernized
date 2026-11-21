// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Metal-Fatigue-Duellsystem fuer Combots (Nahkampf mit Block, Ausweichen, Teile abschlagen)
#pragma once

struct DROID;

namespace modduel
{
bool isCombot(const DROID *psDroid);
/// Einmal pro Spielschritt und Droide aufrufen (aus droidUpdate).
void update(DROID *psDroid);
/// Rumpf-Wackeln: ein Panzer (Nicht-Combot), der von einem Combot im Nahkampf getroffen wird, schaukelt kurz mit dem Rumpf.
/// Liefert true und die Kipp-Winkel (Grad: Nicken, Rollen) solange das Wackeln laeuft. Nur zum Zeichnen.
bool hullWobble(const DROID *psDroid, float &pitchDeg, float &rollDeg);
/// Laufanimation fuer Droiden mit Beinen ohne Cyborg-Bewegungsmodell (Walk/Feuer-Ereignisse).
void updateLeggedAnimation(DROID *psDroid, int moveSpeed);
/// (20.11.) Zeit vom Schlagbeginn bis zum Treffer (ms) - die Combot-Animation legt ihren MF-Treffer-Moment genau darauf
constexpr unsigned HIT_DELAY_MS = 800;
/// Waffenplatz des Arms, der im laufenden Duell-Schlag trifft; -1 = kein Duell
int swingSlot(const DROID *psDroid);
/// (21.11.) wie weit muss die Waffe im Treffer-Moment reichen (MF-Einheiten von der Mitte bis zur Brust des Gegners); 0 = egal
float swingNeedMF(const DROID *psDroid);
/// (21.11.) Parade: Spielzeit des gegnerischen Schlags, den dieser Combot abwehrt (0 = keine), slot = Waffenplatz des abwehrenden Arms
unsigned parryAt(const DROID *psDroid, int &slot);
}
