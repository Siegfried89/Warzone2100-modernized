// SPDX-License-Identifier: GPL-2.0-or-later
// MOD (Wunsch 18.11. 12:03): "Den Stil der WZ-Buttons beibehalten, nur schoener machen mit Glow usw. ... sehr sehr eng am originalen
// WZ-Stil". Die Original-Knopfbilder (images/intfac) in 4-facher Aufloesung als scharfe Pixelkunst mit dezentem Glanz (images/wzhd.img,
// erzeugt von wzhd.py), dazu je eine "glow"-Fassung mit Lichthof fuer Maus darueber / ausgewaehlt. Eigene neue Knoepfe (Neu, Umbenennen,
// Ueberall, Pfeile) sind im selben Stil gezeichnet (WZ-Rahmen, WZ-Farben, Pixelkunst) und liegen unter "wz_<name>" im selben Paket.
#pragma once

#include <string>
#include "lib/ivis_opengl/pietypes.h"

namespace modwzhd
{
/// ist das HD-Paket da?
bool ready();
void shutdown();

/// Original-WZ-Bild (Nummer aus intfac.h, z. B. IMAGE_DES_BODY) in das Rechteck zeichnen - HD-Fassung, wenn vorhanden, sonst das Original
/// (vergroessert). glow: Fassung mit Lichthof (ragt etwas ueber das Rechteck hinaus)
void drawIntfac(unsigned intfacId, int x, int y, int w, int h, bool glow = false, unsigned char alpha = 255);
/// nur HD: false, wenn es fuer dieses WZ-Bild keine HD-Fassung gibt (dann zeichnet der Aufrufer das Original)
bool drawIntfacHd(unsigned intfacId, int x, int y, int w, int h, bool glow, unsigned char alpha = 255);
/// eigenes Bild im WZ-Stil ("wz_new", "wz_rename", "wz_globe", "wz_left", "wz_right"); false = nicht vorhanden
bool drawNamed(const char *base, int x, int y, int w, int h, bool glow = false, unsigned char alpha = 255);

/// Knopf im WZ-Stil ohne Bild (Rahmen + Flaeche in den WZ-Farben, Text mittig) - fuer Reiter mit Beschriftung
void textButton(int x0, int y0, int w, int h, const std::string &label, bool selected, bool hover, bool down);

/// Originalgroesse eines WZ-Bildes (Breite/Hoehe in Bildpunkten); 0, wenn unbekannt
int intfacWidth(unsigned intfacId);
int intfacHeight(unsigned intfacId);
}
