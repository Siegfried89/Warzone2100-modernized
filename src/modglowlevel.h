// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 22.11. 22:54 "Glow maps"): Leucht-Ebenen in Modellen (siehe modglowlevel.cpp)
#pragma once

namespace modglowlevel
{
/// Rueckruf fuer Leucht-Ebenen eintragen (beim Spielstart)
void init();
/// Blink-Wert fuer die naechsten Leucht-Ebenen "glowlvl-blink" (0..255; -1 = ruhiges Leuchten) - modcmdantenna.cpp
void setBlinkAlpha(int alpha);
}
