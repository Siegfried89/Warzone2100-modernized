// SPDX-License-Identifier: GPL-2.0-or-later
// MOD: Werteleiste der gewaehlten Einheit (siehe modunitstats.h)
#include "modunitstats.h"
#include "modwzhd.h"
#include "intfac.h"
#include "intimage.h"

#include <physfs.h>
#include <algorithm>
#include "lib/framework/frame.h"
#include "map.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/button.h"
#include "lib/ivis_opengl/bitimage.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/screen.h"

#include "combat.h"
#include "droid.h"
#include "game_world.h"
#include "hci.h"
#include "moddesigner.h"
#include "modui.h"
#include "projectile.h"
#include "stats.h"
#include "transporter.h"
#include "visibility.h"

#include <string>
#include <vector>

namespace modunitstats
{
namespace
{
// Bilder aus images/modfleets.img (fleeticons.py)
enum Img { IC_STAR = 8, IC_BTN_UP = 9, IC_BTN_DOWN = 10, IC_BTN_HI = 11, IC_STATS = 19, S_DAMAGE = 20, S_ROF, S_RANGE, S_ARMOUR, S_THERMAL, S_HP, S_SPEED, S_WEIGHT, S_POWER, S_SENSOR };
constexpr int ROW_H = 26, ICON_W = 36, ICON_H = 24, VAL_W = 66, PAD = 4;

std::shared_ptr<W_SCREEN> s_screen;
std::shared_ptr<W_CLICKFORM> s_panel;
std::shared_ptr<W_BUTTON> s_toggle;
std::vector<std::shared_ptr<W_BUTTON>> s_rowTips;   // MOD (21.11.): Tooltips der Zeilen (was bedeutet das Symbol)
IMAGEFILE *s_img = nullptr;
bool s_tried = false;
bool s_on = true;          // Standard: ein

bool imagesReady()
{
	if (!s_tried)
	{
		s_tried = true;
		if (PHYSFS_exists("images/modfleets.img")) { s_img = iV_LoadImageFile("images/modfleets.img"); }
	}
	return s_img != nullptr && s_img->imageDefs.size() > static_cast<size_t>(S_SENSOR);
}

void drawImg(int id, int x, int y, int w, int h)
{
	if (!imagesReady()) { return; }
	iV_DrawImageFileAnisotropic(s_img, static_cast<UWORD>(id), x, y, Vector2f(static_cast<float>(w), static_cast<float>(h)), defaultProjectionMatrix(), 255);
}

/// WZ-Befehlsknopf (Vorlage) mit leuchtendem Sinnbild
void wzIconBox(int id, int x, int y, int w, int h, bool down = false, bool hover = false)
{
	drawImg(down ? IC_BTN_DOWN : IC_BTN_UP, x, y, w, h);
	const int sz = h * 9 / 10;
	drawImg(id, x + (w - sz) / 2, y + (h - sz) / 2, sz, sz);
	if (hover) { drawImg(IC_BTN_HI, x - 1, y - 1, w + 2, h + 2); }
}

const DROID *chosen()
{
	if (selectedPlayer >= MAX_PLAYERS) { return nullptr; }
	for (const DROID *d : gameWorld.objects.droids[selectedPlayer])
	{
		if (d->selected && !d->died) { return d; }
	}
	return nullptr;
}

std::string num(int v) { return std::to_string(v); }
std::string tiles(int units)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(units) / TILE_UNITS);
	return buf;
}

struct Row { int icon; std::string value; const char *tip; };

std::vector<Row> rowsFor(const DROID *d)
{
	std::vector<Row> rows;
	const int p = d->player;
	if (d->numWeaps > 0 && d->asWeaps[0].nStat > 0)
	{
		const WEAPON_STATS &w = asWeaponStats[d->asWeaps[0].nStat];
		rows.push_back({S_DAMAGE, num(weaponDamage(w, p)), "Damage: how much one shot of the main weapon does"});
		rows.push_back({S_ROF, num(weaponROF(w, p)), "Fire rate: shots per minute of the main weapon"});
		rows.push_back({S_RANGE, tiles(proj_GetLongRange(w, p)), "Range of the main weapon (in tiles)"});
	}
	rows.push_back({S_ARMOUR, num(objArmour(d, WC_KINETIC)), "Armour: protection against bullets, shells and rockets (kinetic)"});
	rows.push_back({S_THERMAL, num(objArmour(d, WC_HEAT)), "Thermal armour: protection against fire, lasers and other heat weapons"});
	rows.push_back({S_HP, num(static_cast<int>(d->body)) + "/" + num(static_cast<int>(d->originalBody)), "Hit points: current / maximum (0 = destroyed)"});
	rows.push_back({S_SPEED, num(static_cast<int>(d->baseSpeed)), "Speed on open ground"});
	rows.push_back({S_WEIGHT, num(static_cast<int>(d->weight)), "Weight (heavier = slower, needs a stronger engine)"});
	rows.push_back({S_POWER, num(static_cast<int>(calcDroidPower(d))), "Cost: power needed to build this unit"});
	rows.push_back({S_SENSOR, tiles(objSensorRange(d)), "Sight range: how far the unit can see (in tiles)"});
	rows.push_back({IC_STAR, num(static_cast<int>(d->kills)), "Kills: enemies destroyed by this unit (gives experience and rank)"});
	return rows;
}

void drawPanel(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const DROID *d = chosen();
	if (d == nullptr || !s_on) { return; }
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	const std::vector<Row> rows = rowsFor(d);
	const int h = PAD * 2 + static_cast<int>(rows.size()) * ROW_H;
	modui::wzPanel(x0, y0, x0 + psWidget->width(), y0 + h);
	int y = y0 + PAD;
	for (const Row &r : rows)
	{
		// (Wunsch 18.11. 13:28 "diese Knoepfe sollten auch die knuddelige WZ-Grafik haben - nur mit Leuchten und Farben verschoenert"):
		// die Original-Sinnbilder des WZ-Einheiteneditors (intfac) in der HD-Fassung mit Glanz, auf dem WZ-Knopf
		{
			int wz = -1;
			switch (r.icon)
			{
			case S_DAMAGE: wz = IMAGE_DES_DAMAGE; break;
			case S_ROF: wz = IMAGE_DES_FIRERATE; break;
			case S_RANGE: wz = IMAGE_DES_RANGE; break;
			case S_ARMOUR: wz = IMAGE_DES_ARMOUR_KINETIC; break;
			case S_THERMAL: wz = IMAGE_DES_ARMOUR_EXPLOSIVE; break;
			case S_HP: wz = IMAGE_DES_BODYPOINTS; break;
			case S_SPEED: wz = IMAGE_DES_ROAD; break;
			case S_WEIGHT: wz = IMAGE_DES_WEIGHT; break;
			case S_POWER: wz = IMAGE_DES_POWER; break;
			case S_SENSOR: wz = IMAGE_ORIGIN_SENSOR_STANDARD; break;
			default: break;
			}
			if (wz >= 0)
			{
				drawImg(IC_BTN_UP, x0 + PAD, y, ICON_W, ICON_H);
				const int iw = iV_GetImageWidth(IntImages, static_cast<UWORD>(wz)), ih = iV_GetImageHeight(IntImages, static_cast<UWORD>(wz));
				const float k = std::min(static_cast<float>(ICON_W - 6) / std::max(1, iw), static_cast<float>(ICON_H - 4) / std::max(1, ih));
				const int dw = std::max(1, static_cast<int>(iw * k)), dh = std::max(1, static_cast<int>(ih * k));
				modwzhd::drawIntfac(static_cast<unsigned>(wz), x0 + PAD + (ICON_W - dw) / 2, y + (ICON_H - dh) / 2, dw, dh, true);
			}
			else
			{
				wzIconBox(r.icon, x0 + PAD, y, ICON_W, ICON_H);
			}
		}
		// Wertefeld wie im Bild: dunkles Kaestchen mit hellem Rand, Zahl rechts davon
		const int vx = x0 + PAD + ICON_W + 3;
		pie_UniTransBoxFill(static_cast<float>(vx), static_cast<float>(y + 2), static_cast<float>(vx + VAL_W), static_cast<float>(y + ICON_H - 2), pal_RGBA(16, 18, 40, 230));
		iV_Box(vx, y + 2, vx + VAL_W, y + ICON_H - 2, pal_RGBA(103, 103, 163, 255));
		modui::text(modui::fit(r.value, VAL_W - 6, 1), vx + 4, y + 4, pal_RGBA(230, 236, 255, 255), 1);
		y += ROW_H;
	}
}

void drawToggle(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const W_BUTTON *b = static_cast<const W_BUTTON *>(psWidget);
	const int x0 = static_cast<int>(xOffset) + psWidget->x(), y0 = static_cast<int>(yOffset) + psWidget->y();
	wzIconBox(IC_STATS, x0, y0, psWidget->width(), psWidget->height(), s_on || (b->getState() & WBUT_DOWN) != 0, b->isMouseOverWidget());
}

void ensureScreen()
{
	if (s_screen) { return; }
	auto screen = W_SCREEN::make();
	screen->psForm->hide();   // verborgene Wurzel = durchlaessig fuer Mausklicks
	auto panel = std::make_shared<W_CLICKFORM>();
	screen->psForm->attach(panel);
	panel->displayFunction = drawPanel;
	panel->setTransparentToClicks(true);
	// (Wunsch 21.11. 18:31 "Tooltips ... auch die Einheitenwerte links oben in der Liste, was die ganzen Symbole bedeuten")
	s_rowTips.clear();
	for (int i = 0; i < 14; ++i)
	{
		auto tb = std::make_shared<W_BUTTON>();
		panel->attach(tb);
		tb->displayFunction = [](WIDGET *, UDWORD, UDWORD) {};
		tb->hide();
		s_rowTips.push_back(tb);
	}
	auto t = std::make_shared<W_BUTTON>();
	screen->psForm->attach(t);
	t->displayFunction = drawToggle;
	t->setTip(_("Unit values on/off"));
	t->addOnClickHandler([](W_BUTTON &) { s_on = !s_on; });
	widgRegisterOverlayScreenOnTopOfScreen(screen, psWScreen);
	s_screen = screen;
	s_panel = panel;
	s_toggle = t;
}
}

void tick()
{
	const bool want = imagesReady() && !moddesigner::isOpen() && selectedPlayer < MAX_PLAYERS && intMode != INT_DESIGN;
	if (!want && !s_screen) { return; }
	ensureScreen();
	const DROID *d = chosen();
	s_toggle->show(want && d != nullptr);
	s_panel->show(want && d != nullptr && s_on);
	if (!want || d == nullptr) { return; }
	// oben links unter der oberen Leiste - (18.11. 12:10 "die Menues ueberlappen sich immer noch extrem") ist links ein WZ-Fenster offen
	// (Bau-/Fabrik-/Forschungsliste), steht die Werteleiste rechts daneben statt darauf
	int x = 6, y = 44;
	for (UDWORD fid : {static_cast<UDWORD>(IDSTAT_FORM), static_cast<UDWORD>(IDTRANS_FORM), static_cast<UDWORD>(IDTRANS_CONTENTFORM)})
	{
		WIDGET *f = widgGetFromID(psWScreen, fid);
		if (f != nullptr && f->visible() && f->width() > 0 && f->screenPosX() < x + 60 && f->screenPosY() < 300)
		{
			x = std::max(x, f->screenPosX() + f->width() + 6);
		}
	}
	s_toggle->setGeometry(x, y, ICON_W, ICON_H);
	const std::vector<Row> rowList = rowsFor(d);
	const int rows = static_cast<int>(rowList.size());
	s_panel->setGeometry(x, y + ICON_H + 4, PAD * 2 + ICON_W + 3 + VAL_W + 4, PAD * 2 + rows * ROW_H);
	static std::vector<std::string> s_lastTips;
	s_lastTips.resize(s_rowTips.size());
	for (size_t i = 0; i < s_rowTips.size(); ++i)
	{
		if (i >= rowList.size()) { s_rowTips[i]->hide(); continue; }
		s_rowTips[i]->show();
		s_rowTips[i]->setGeometry(PAD, PAD + static_cast<int>(i) * ROW_H, ICON_W + 3 + VAL_W, ROW_H);
		if (s_lastTips[i] != rowList[i].tip) { s_rowTips[i]->setTip(rowList[i].tip); s_lastTips[i] = rowList[i].tip; }
	}
}

void shutdown()
{
	if (s_screen) { widgRemoveOverlayScreen(s_screen); }
	s_screen = nullptr; s_panel = nullptr; s_toggle = nullptr; s_rowTips.clear();
	if (s_img != nullptr) { iV_FreeImageFile(s_img); s_img = nullptr; }
	s_tried = false;
}

IMAGEFILE *fleetImages()
{
	imagesReady();
	return s_img;
}
}
