// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Gemeinsame Hilfen fuer die Metal-Fatigue-Oberflaeche (Bilder aus images/mfui.img, Textzeichnen).
#pragma once

#include <string>
#include "lib/ivis_opengl/pietypes.h"

namespace modui
{

// Reihenfolge wie in images/mfui.img (von mfui.py erzeugt)
enum Img
{
	PANEL = 0, TAB_ARMS, TAB_LEGS, TAB_TORSO, TAB_ALL,
	MELEE_OFF, MELEE_ON, ALL_OFF, ALL_ON, RANGED_OFF, RANGED_ON,
	STAY_OFF, STAY_ON, TETHER_OFF, TETHER_ON, PURSUE_OFF, PURSUE_ON,
	B_ATTACK, B_GUARD, B_STOP, B_MOVE, B_PATROL, B_REPAIR, B_RAZE, B_DROP, B_DRILL, B_INVADE,
	B_EVAC, B_EVACCREW, B_DEPLOY, B_LAUNCH, B_SEND, B_PRIMARY, B_RETFIRE, B_CLEAR, B_RALLY, B_SELL,
	PREBUILD, STAT_A, STAT_B, STAT_C, STAT_D, RESBAR, DISP_A, DISP_B,
	ARR_UP, ARR_UP_HL, ARR_DN, ARR_DN_HL, METALPANEL,
	IMG_COUNT
};

/// Bilder laden (einmalig); false = images/mfui.img fehlt
bool ready();
void shutdown();

int imgWidth(Img id);
int imgHeight(Img id);
/// Bild mit Skalierung zeichnen (oben links bei x,y)
void draw(Img id, int x, int y, float scale = 1.f, unsigned char alpha = 255);
void drawSized(Img id, int x, int y, int w, int h, unsigned char alpha = 255);
/// 1.0 bei normalen Aufloesungen, 1.5 bei sehr hohen
float uiScale();

/// Text mit Zwischenspeicher; y = Oberkante der Zeile. sizeClass: 0 klein, 1 normal, 2 gross.
void text(const std::string &s, int x, int top, PIELIGHT colour, int sizeClass = 0);
int textWidth(const std::string &s, int sizeClass = 0);
/// kuerzt den Text, bis er in maxW Punkte passt
std::string fit(const std::string &s, int maxW, int sizeClass = 0);
/// Schriftgroesse passend zum Skalierungsfaktor des Bildschirms waehlen (0..2)
int sizeClassFor(float scale);

PIELIGHT cyan(unsigned char a = 255);
PIELIGHT healthColour(int pct);   // >66 cyan, >33 gelb, sonst rot
void disc(int cx, int cy, int r, PIELIGHT colour);   // gefuellter Kreis (halbtransparent)
void ring(int cx, int cy, int r, PIELIGHT colour);

}
