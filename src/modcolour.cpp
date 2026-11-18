// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Spielerfarbe im Einzelspieler (siehe modcolour.h)

#include "modcolour.h"

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/netplay/netplay.h"
#include "lib/widget/button.h"
#include "lib/widget/widget.h"

#include "component.h"
#include "game_world.h"
#include "multiplay.h"
#include "objmem.h"
#include "warzoneconfig.h"

#include <algorithm>
#include <vector>

namespace modcolour
{
namespace
{
constexpr int NUM_COLOURS = MOD_NUM_PLAYER_COLOURS;   // (18.11.) 16 = Wuestentarn

class Swatch : public W_BUTTON
{
public:
	int colour = 0;
	bool inGame = false;

	void display(int xOffset, int yOffset) override
	{
		const int x0 = xOffset + x(), y0 = yOffset + y();
		const int x1 = x0 + width(), y1 = y0 + height();
		const bool ok = allowed(colour, inGame);
		const bool chosen = current(inGame) == colour;
		const bool hover = (getState() & WBUT_HIGHLIGHT) != 0;
		// WZ-Stil: dunkler Rahmen, darin die Teamfarbe; gewaehlt = heller Rand, gesperrt = abgedunkelt
		pie_BoxFill(x0, y0, x1, y1, WZCOL_FORM_DARK);
		PIELIGHT c = pal_GetTeamColour(colour);
		if (!ok) { c.byte.r /= 3; c.byte.g /= 3; c.byte.b /= 3; }
		pie_BoxFill(x0 + 2, y0 + 2, x1 - 2, y1 - 2, c);
		if (colour == MOD_COLOUR_DESERT_CAMO)
		{
			// (18.11.) Wuestentarn: kleines digitales Tarnmuster (Farben wie im Shader modcamo.glsl)
			static const uint8_t pat[5][5] = { { 0, 1, 2, 2, 0 }, { 1, 2, 2, 1, 0 }, { 0, 1, 0, 0, 3 }, { 2, 0, 0, 1, 2 }, { 2, 1, 0, 2, 2 } };
			static const PIELIGHT pal[4] = { { 205, 187, 149, 255 }, { 171, 149, 113, 255 }, { 128, 106, 77, 255 }, { 225, 214, 184, 255 } };
			const int w = std::max(1, x1 - x0 - 4), h = std::max(1, y1 - y0 - 4);
			for (int j = 0; j < 5; ++j)
			{
				for (int i = 0; i < 5; ++i)
				{
					PIELIGHT p = pal[pat[j][i]];
					if (!ok) { p.byte.r /= 3; p.byte.g /= 3; p.byte.b /= 3; }
					pie_BoxFill(x0 + 2 + i * w / 5, y0 + 2 + j * h / 5, x0 + 2 + (i + 1) * w / 5, y0 + 2 + (j + 1) * h / 5, p);
				}
			}
		}
		if (chosen)
		{
			iV_Box(x0, y0, x1, y1, WZCOL_TEXT_BRIGHT);
			iV_Box(x0 + 1, y0 + 1, x1 - 1, y1 - 1, WZCOL_TEXT_BRIGHT);
		}
		else if (hover && ok)
		{
			iV_Box(x0, y0, x1, y1, WZCOL_FORM_HILITE);
		}
		else
		{
			iV_Box(x0, y0, x1, y1, WZCOL_FORM_LIGHT);
		}
	}
};

const char *colourName(int c)
{
	// Namen je FARBE (getPlayerColourName erwartet einen Spieler)
	static const char *names[MOD_NUM_PLAYER_COLOURS] = {
		N_("Green"), N_("Orange"), N_("Grey"), N_("Black"), N_("Red"), N_("Blue"), N_("Pink"), N_("Cyan"), N_("Yellow"), N_("Purple"),
		N_("White"), N_("Bright blue"), N_("Neon green"), N_("Infrared"), N_("Ultraviolet"), N_("Brown"), N_("Desert camo") };
	return (c >= 0 && c < MOD_NUM_PLAYER_COLOURS) ? gettext(names[c]) : "";
}

bool hasObjects(unsigned p)
{
	if (p >= MAX_PLAYERS) { return false; }
	return !gameWorld.objects.droids[p].empty() || !gameWorld.objects.structures[p].empty();
}
} // namespace

bool allowed(int colour, bool inGame)
{
	if (colour < 0 || colour >= NUM_COLOURS) { return false; }
	if (!inGame)
	{
		return colour == 0 || colour >= 4;   // Kampagne: 1-3 tragen die Gegner
	}
	if (bMultiPlayer && NetPlay.bComms) { return false; }
	for (unsigned p = 0; p < MAX_PLAYERS; ++p)
	{
		if (p == selectedPlayer) { continue; }
		if (hasObjects(p) && getPlayerColour(p) == colour) { return false; }
	}
	if (!bMultiPlayer && colour >= 1 && colour <= 3) { return false; }
	return true;
}

int current(bool inGame)
{
	if (inGame && selectedPlayer < MAX_PLAYERS) { return getPlayerColour(selectedPlayer); }
	return war_GetSPcolor();
}

void apply(int colour, bool inGame)
{
	if (!allowed(colour, inGame)) { return; }
	if (!bMultiPlayer || !inGame)
	{
		war_SetSPcolor(colour);   // Einzelspieler: merken (gilt auch fuer die naechste Kampagne)
	}
	if (inGame && selectedPlayer < MAX_PLAYERS)
	{
		setPlayerColour(selectedPlayer, static_cast<UDWORD>(colour));
	}
}

int addSwatches(WIDGET *parent, int x, int y, int cols, int size, int gap, bool inGame, const std::function<void()> &afterPick)
{
	if (parent == nullptr) { return 0; }
	int shown = 0;
	for (int c = 0; c < NUM_COLOURS; ++c)
	{
		if (!inGame && !allowed(c, false)) { continue; }
		auto b = std::make_shared<Swatch>();
		parent->attach(b);
		b->colour = c;
		b->inGame = inGame;
		const int col = shown % cols, row = shown / cols;
		b->setGeometry(x + col * (size + gap), y + row * (size + gap), size, size);
		b->setTip(colourName(c));
		b->addOnClickHandler([c, inGame, afterPick](W_BUTTON &) {
			apply(c, inGame);
			if (afterPick) { afterPick(); }
		});
		++shown;
	}
	const int rows = (shown + cols - 1) / cols;
	return rows * size + (rows - 1) * gap;
}
} // namespace modcolour
